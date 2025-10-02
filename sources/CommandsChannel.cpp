#include "../includes/Server.hpp"
#include "../includes/responseCodes.hpp"
#include "../includes/macros.hpp"

std::vector<std::string> split(const std::string& input, const char delmiter) {

	std::vector<std::string> tokens;
	std::stringstream ss(input);

	std::string token;
	while (std::getline(ss, token, delmiter)) {
		tokens.push_back(token);
	}
	return tokens;
}

bool Server::checkInvitation(Client &client, Channel &channel) {
	if (channel.isInviteOnly() && !channel.isClientInvited(&client)) {
		messageHandle(ERR_INVITEONLYCHAN, client, channel.getName(), {});
		logMessage(WARNING, "CHANNEL",
		"Client '" + client.getNickname() + "' attempted to join invite-only channel '" +
		channel.getName() + "' without an invitation.");
		return false;
	}
	return true;
}

bool checkChannelName(Client &client, const std::string& name) {
	if (isValidChannelName(name))
		return true;
	logMessage(ERROR, "JOIN",
	"Client " + client.getNickname() + " attempted to join with invalid channel. Name: " + name);
	return false;
}

bool Server::checkChannelLimit(Client &client, Channel &channel) {
	if ((static_cast<int>(channel.getMembers().size()) < channel.getUserLimit()) || channel.getUserLimit() < 0)
		return true;
	messageHandle(ERR_CHANNELISFULL, client, channel.getName(), {});
	logMessage(ERROR, "CHANNEL", "Client '" + client.getNickname() +
	"' attempted to join channel '" + channel.getName() +
	"', but the channel is full (limit: " + std::to_string(channel.getUserLimit()) + ").");
	return false;
}

void Server::handleJoin(Client& client, const std::vector<std::string>& params) {

	if (params.empty() || params.size() < 1) {
		messageHandle(ERR_NEEDMOREPARAMS, client, "JOIN", params);
		logMessage(ERROR, "JOIN", "Client '" + client.getNickname()
			+ "' sent JOIN command with insufficient parameters.");
		return;
	}

	std::vector<std::string> requestedChannels = split(params[0], ',');
	std::vector<std::string> providedKeys = (params.size() > 1) ? split(params[1], ',') : std::vector<std::string>{};

	for (size_t i = 0; i < requestedChannels.size(); i++) {
		const std::string& channelName = requestedChannels[i];
		const std::string& channelKey = (i < providedKeys.size()) ? providedKeys[i] : "";

		if (!checkChannelName(client, channelName)) {
			continue; // should we continue
		}
		if (client.isInChannel(channelName)) {
			logMessage(WARNING, "JOIN", "Client '" + client.getNickname() + "' attempted to re-join channel '"
				+ channelName + "' but is already a member.");
			continue;
		}
		Channel* channel;
		if (channelExists(channelName)) {
			channel = getChannel(channelName);
			if (!checkInvitation(client, *channel))
				continue;
			if (!channel->checkKey(channel, &client, channelKey)) {
				continue;
			}
			if (!channel->checkChannelLimit(client, *channel)) {
				continue;
			}
		}
		else {
			channel = createChannel(&client, channelName, channelKey);
			if (!channel) {
				logMessage(ERROR, "CHANNEL", "Failed to create channel: '" + channelName + "'.");
				continue;
			}
		}
		channel->addChannelMember(&client);
		client.addToJoinedChannelList(channel->getName());
		logMessage(INFO, "CHANNEL", "Client '" + client.getNickname() + "' joined channel [" + channel->getName() + "]");

		messageBroadcast(*channel, client, "JOIN", ""); // need to set it if  above mehtods have no error
		if (channel->getTopic() != "") {
			messageHandle(RPL_TOPIC, client, "JOIN", {channel->getName() + " :" + channel->getTopic()});
		}
		std::string replyMsg2 = "= " + channel->getName() + " :";
		const std::set<Client*>& members = channel->getMembers();
		for (Client* member : members) {
			if (channel->isOperator(member))
				replyMsg2 += "@";
			replyMsg2 += member->getNickname() + " ";
		}
		messageHandle(RPL_NAMREPLY, client, "JOIN", {replyMsg2}); // what if list is longer then MAX_LEN
		messageHandle(RPL_ENDOFNAMES, client, "JOIN", {channel->getName(), " :End of /NAMES list\r\n"});
	}
}

void Server::handleMode(Client& client, const std::vector<std::string>& params) {

	if (params.empty()) {
		messageHandle(ERR_NEEDMOREPARAMS, client, "MODE", params);
		logMessage(ERROR, "MODE", "Client '" + client.getNickname()
			+ "' sent MODE command with insufficient parameters.");
		return;
	}
	Channel *channel;
	std::string target = params[0];
	if (target[0] != '#') {
		Client* targetClient = getClient(target);
		if (targetClient) {
			if (targetClient->getNickname() != client.getNickname()) {
				messageHandle(ERR_USERSDONTMATCH, client, "MODE", params);
				logMessage(ERROR, "MODE", "Client " + client.getNickname()
				+ "' attempted MODE command for another user '" + targetClient->getNickname() + "'.");
				return;
			}
			if (!params[1].empty() && params[1] == "+i")
				messageHandle(RPL_UMODEIS, client, "MODE", {"+i"});
			else {
				messageHandle(ERR_UMODEUNKNOWNFLAG, client, "MODE", {});
				logMessage(WARNING, "MODE", "Client '" + client.getNickname() +
				"' attempted unsupported user MODE. User modes are not supported.");
			}
			return;
		} else {
		messageHandle(ERR_NOSUCHNICK, client,"MODE",params);
		logMessage(ERROR, "MODE", "Client '" + client.getNickname() +  "' attempted MODE for nonexistent user '" + target + "'.");
		}
	}
	channel = getChannel(target);
	if (!channel) {
		messageHandle(ERR_NOSUCHCHANNEL, client, target, {});
		logMessage(ERROR, "MODE", "Client '" + client.getNickname()
			+ "' attempted MODE on non-existent channel [" + target + "].");
		return;
	}
	if (params.size() == 1) {
		std::string currentModes = channel->getModeString();
		messageHandle(RPL_CHANNELMODEIS, client, client.getNickname(), {target, currentModes});
		logMessage(INFO, "MODE", "Channel [" + channel->getName() + "] current modes: " + currentModes + ".");
		return;
	}

	if (!channel->isMember(&client)) {
		messageHandle(ERR_NOTONCHANNEL, client, "MODE", {channel->getName()});
		logMessage(ERROR, "MODE",
        	"Client '" + client.getNickname() + "' attempted MODE on channel '"
        	+ channel->getName() + "', but is not a member.");
		return;
	}
	handleChannelMode(client, *channel, params);
}

void Server::handleSingleMode(Client &client, Channel &channel, const char &operation, char &modeChar,
	const std::string &modeParam, const std::vector<std::string>& params) {

		switch (modeChar) {
		case 'i':
			inviteOnlyMode(client, channel, operation);
			break;
		case 't':
			topicRestrictionMode(client, channel, operation);
			break;
		case 'k':
			channelKeyMode(client, channel, operation, modeParam);
			break;
		case 'o':
			operatorMode(client, channel, operation, modeParam); 
			break;											
		case 'l':
			userLimitMode(client, channel, operation, modeParam);
			break;
		default:
			messageHandle(ERR_UNKNOWNMODE, client, "MODE", params);
			logMessage(WARNING, "MODE", "Client " + client.getNickname()
				+ " sent unknown mode character  '" + modeChar + "' on channel " + channel.getName() + ".");
			break;
		}
}

bool Server::checkModeParam(const char modeChar, const char operation) {

	if ((modeChar == 'k' || modeChar == 'o' || modeChar == 'l' || modeChar == 'o') 
		&& operation == '+')
		return true;
	return false;
}

void Server::handleChannelMode(Client& client, Channel &channel, const std::vector<std::string>& params) {

	std::string modeString = params[1];
	if (modeString == "b" || modeString == "+b")
		return messageHandle(RPL_ENDOFBANLIST, client, channel.getName(), {client.getNickname()});
	if (modeString.size() < 2 || (modeString[0] != '+' && modeString[0] != '-')) {
		//messageHandle(ERR_UNKNOWNMODE, client, "MODE", params);
		logMessage(ERROR, "MODE", "Client '" + client.getNickname()
			+ "' used unkown mode character" + modeString[0] + " on channel '" + channel.getName() + "'.");
		return;
	}
	const char operation = modeString[0];
	size_t paramIndex = 2;
	for (size_t i = 1; i < modeString.size(); i++) {
		char modeChar = modeString[i];
		std::string modeParam = "";

		if (checkModeParam(modeChar, operation)) {
			if (paramIndex >= params.size()) {
				messageHandle(ERR_NEEDMOREPARAMS, client, "MODE", params);          
				logMessage(ERROR, "MODE", "Client '" + client.getNickname()				
				+ "' sent MODE command with insufficient parameters for mode character "
				+ modeChar + ".");
				return;
			}
			modeParam = params[paramIndex++];
		}
		handleSingleMode(client, channel, operation, modeChar, modeParam, params);
	}
}

void Server::inviteOnlyMode(Client& client, Channel& channel, char operation) {
	if (!channel.isOperator(&client)) {
		messageHandle(ERR_CHANOPRIVSNEEDED, client, "MODE", {channel.getName(), ":You're not a channel operator"});
		logMessage(WARNING, "MODE", "Client '" + client.getNickname() + "' attempted to change +i on '"
		+ channel.getName() + "' without operator privileges.");
		return;
	}
	if (operation == '+') {
		if (!channel.isInviteOnly()) {
			channel.setInviteOnly(true);
			messageBroadcast(channel, client, "MODE", "+i");
			logMessage(INFO, "MODE", "Invite-only mode enabled on channel [" + channel.getName() + "] by client '"
			+ client.getNickname() + "'");
		} else {

			logMessage(DEBUG, "MODE", "Invite-only mode already active on '" + channel.getName() + "'");
		}
	} else if (operation == '-') {
		if (channel.isInviteOnly()) {
			channel.setInviteOnly(false);
			messageBroadcast(channel, client, "MODE", channel.getName() + " -i");
			logMessage(INFO, "MODE", "Client '" + client.getNickname() + "' disabled invite-only mode on channel '"
			+ channel.getName() + "'");
		} else {
			logMessage(DEBUG, "MODE", "Invite-only mode already disabled on '" + channel.getName() + "'");
		}
	}
}

void Server::topicRestrictionMode(Client& client, Channel& channel, char operation) {
	if (!channel.isOperator(&client)) {
		messageHandle(ERR_CHANOPRIVSNEEDED, client, channel.getName(), {});
		return logMessage(ERROR, "MODE", "User not an operator");
	}
	if (operation == '+') {
		if (!channel.isTopicOperatorOnly()) {
			channel.setTopicOperatorOnly(true);
			logMessage(DEBUG, "MODE", "Topic settable by channel operator only");
			messageBroadcast(channel, client, "MODE", "+t");
		} else {
			logMessage(WARNING, "MODE", "Topic is already set as operator-only");
		}
	} else if (operation == '-') { // if '-
		if (channel.isTopicOperatorOnly()) {
			channel.setTopicOperatorOnly(false);
			logMessage(DEBUG, "MODE", "Topic settable by every channel member");
			messageBroadcast(channel, client, "MODE", "-t");
		} else {
			logMessage(WARNING, "MODE", "Topic is already possible to be set by all");
		}
	}
}

void Server::operatorMode(Client& client, Channel& channel, char operation, const std::string& user) {
	if (!channel.isOperator(&client)) {
		messageHandle(ERR_CHANOPRIVSNEEDED, client, channel.getName(), {});
		return logMessage(ERROR, "MODE", "User not an operator");
	}
	if (user.empty()) {
		messageHandle(ERR_NEEDMOREPARAMS, client, "MODE", {});
		return logMessage(ERROR, "MODE", "No target user specified");
	}
	Client* targetClient = nullptr;
	targetClient = getClient(user);
	if (!channel.isMember(targetClient)) {
		messageHandle(ERR_USERNOTINCHANNEL, client, channel.getName(), {"", user});
		return logMessage(ERROR, "MODE", "Target user not on channel");
	}
	if (operation == '+') {
		channel.setOperator(targetClient, true);
		messageBroadcast(channel, client, "MODE", "+o " + targetClient->getNickname());
		return logMessage(DEBUG, "MODE", "User " + user + " given operator rights by " + client.getNickname());
	}
	else if (operation == '-') {
		channel.setOperator(targetClient, false);
		messageBroadcast(channel, client, "MODE", "-o " + targetClient->getNickname());
		return logMessage(DEBUG, "MODE", "User " + user + " operator rights removed by " + client.getNickname());
	}

}

void Server::channelKeyMode(Client& client, Channel& channel, char operation, const std::string& key) {
	if (!channel.isOperator(&client)) {
		messageHandle(ERR_CHANOPRIVSNEEDED, client, "MODE", {channel.getName(), ":You're not a channel operator"});
		logMessage(WARNING, "MODE", "Unauthorized key mode change attempt on " + channel.getName());
		return;
	}
	if (operation == '+') {
		if (key.empty()) {
		messageHandle(ERR_NEEDMOREPARAMS, client, "MODE", {channel.getName(), "+k", ":Key required for +k mode"});
		logMessage(WARNING, "MODE", "Missing key in +k mode on " + channel.getName());
		return;
	}
	channel.setChannelKey(key);
	messageBroadcast(channel, client, "MODE", channel.getName() + " +k");
	//messageHandle(RPL_MODECHANGE, client, "MODE", {channel.getName(), "+k", ":Channel key set"});
	logMessage(INFO, "MODE", "+k set on " + channel.getName());
    } else if (operation == '-') {
		channel.setChannelKey("");
		messageBroadcast(channel, client, "MODE", channel.getName() + " -k");
		/*messageHandle(RPL_MODECHANGE, client, "MODE", {channel.getName(), "-k", ":Channel key removed"});*/
		logMessage(INFO, "MODE", "+k removed from " + channel.getName());
	}
}

bool Server::isValidUserLimit(const std::string& str, int& userLimit) {
	std::istringstream iss(str);
	int temp;
	char remain;

	if (!(iss >> temp) || (iss >> remain) || temp <= 0)
		return false;
	userLimit = temp;
	return true;
}

void Server::userLimitMode(Client& client, Channel& channel, char operation, const std::string& userLimitStr) {
	if (operation == '-') {
		channel.setUserLimit(CHAN_USER_LIMIT);
		messageBroadcast(channel, client, "MODE", "-l");
		return logMessage(DEBUG, "MODE", "User limit removed (defaulted back to 100)");
	}
	if (userLimitStr.empty()) {
		messageHandle(ERR_NEEDMOREPARAMS, client, "MODE", {});
		return logMessage(ERROR, "MODE", "No user limit specified for the channel");
	}
	if (!channel.isOperator(&client)) {
		messageHandle(ERR_CHANOPRIVSNEEDED, client, channel.getName(), {});
		return logMessage(ERROR, "MODE", "User does not have operator rights");
	}
	int userLimit = 0;
	if (!isValidUserLimit(userLimitStr, userLimit))
		return logMessage(ERROR, "MODE", "Faulty user limit");
	if (userLimit > 0 && userLimit <= CHAN_USER_LIMIT) {
		channel.setUserLimit(userLimit);
		messageBroadcast(channel, client, "MODE", "+l " + std::to_string(userLimit));
		return logMessage(DEBUG, "MODE", "User limit set to: " + std::to_string(userLimit));
	}
	else if (userLimit > CHAN_USER_LIMIT)
		return logMessage(ERROR, "MODE", "User limit set too high > 100");
	else
		logMessage(ERROR, "MODE", "Faulty user limit");
}

int Server::handleKickParams(Client& client, const std::vector<std::string>& params) {
	if (params.empty()) {
		messageHandle(ERR_NEEDMOREPARAMS, client, "KICK", params);
		logMessage(ERROR, "KICK", "No channel or user specified");
		return ERR;
	}
	if (params[0].empty()) {
		messageHandle(ERR_NEEDMOREPARAMS, client, "KICK", params);
		logMessage(ERROR, "KICK", "No channel specified");
		return ERR;
	}
	if (params[1].empty()) {
		messageHandle(ERR_NEEDMOREPARAMS, client, "KICK", params);
		logMessage(ERROR, "KICK", "No user specified");
		return ERR;
	}
	return SUCCESS;
}

void Server::handleKick(Client& client, const std::vector<std::string>& params) {

	if (handleKickParams(client, params) == ERR)
		return;
	std::string channel = params[0];
	std::string userToKick = params[1];
	std::string kickReason = (params.size() > 2) ? params[2] : "No reason given"; //optional reason for KICK
	auto it = channelMap_.find(channel);
	if (it == channelMap_.end()) {
		messageHandle(ERR_NOSUCHCHANNEL, client, channel, params);
		return logMessage(ERROR, "KICK", "Channel " + channel + " does not exist");
	}
	Channel* targetChannel = it->second;
	const std::set<Client*>& members = targetChannel->getMembers();
	if (members.find(&client) == members.end()) {
		messageHandle(ERR_NOTONCHANNEL, client, channel, params);
		return logMessage(ERROR, "KICK", "User " + client.getNickname() + " not on channel " + channel);
	}
	if (!targetChannel->isOperator(&client)) { // check whether the user has operator rights on the channel
		messageHandle(ERR_CHANOPRIVSNEEDED, client, channel, params);
		return logMessage(ERROR, "KICK", "User " + client.getNickname() + " doesn't have operator rights on channel " + channel);
	}
	Client* clientToKick = nullptr;
	for (Client* member : members) {
		if (member->getNickname() == userToKick) {
			clientToKick = member;
			break;
		}
	}
	if (clientToKick == &client) {
		return logMessage(WARNING, "KICK", "User " + client.getNickname() + " attempted to kick themselves out of channel " + channel);
	}
	if (clientToKick == nullptr) {
		// MESSAGE client the user they tried to kick is not on the channel
		messageHandle(ERR_USERNOTINCHANNEL, client, channel, params);
		return logMessage(ERROR, "KICK", "User " + userToKick + " not found on channel " + channel);
	}
	messageBroadcast(*targetChannel, client, "KICK", clientToKick->getNickname() + " :" + kickReason);
	targetChannel->removeMember(clientToKick);
	clientToKick->leaveChannel(channel);
	logMessage(INFO, "KICK", "User " + userToKick + " kicked from " + channel + " by " + client.getNickname() + " (reason: " + kickReason + ")");
}

int Server::handleInviteParams(Client& client, const std::vector<std::string>& params) {
	if (params.empty()) {
		messageHandle(ERR_NEEDMOREPARAMS, client, "INVITE", params);
		logMessage(ERROR, "INVITE", "No nickname or channel specified");
		return ERR;
	}
	if (params[0].empty()) {
		messageHandle(ERR_NEEDMOREPARAMS, client, "INVITE", params);
		logMessage(ERROR, "INVITE", "No nickname specified");
		return ERR;
	}
	if (params[1].empty()) {
		messageHandle(ERR_NEEDMOREPARAMS, client, "INVITE", params);
		logMessage(ERROR, "INVITE", "No channel specified");
		return ERR;
	}
	return SUCCESS;
}

void Server::handleInvite(Client& client, const std::vector<std::string>& params) {
	if (handleKickParams(client, params) == ERR)
		return;
	std::string userToBeInvited = params[0];
	std::string channelInvitedTo = params[1];
	if (!channelExists(channelInvitedTo)) {
		messageHandle(ERR_NOSUCHCHANNEL, client, channelInvitedTo, params);
		return logMessage(WARNING, "INVITE", "Channel " + channelInvitedTo + " does not exist");
	}
	Channel* targetChannel = getChannel(channelInvitedTo);
	if (!targetChannel->isMember(&client)) {
		messageHandle(ERR_NOTONCHANNEL, client, targetChannel->getName(), params);
		return logMessage(ERROR, "INVITE", "User " + client.getNickname() + " not on channel " + channelInvitedTo);
	}
	if (targetChannel->isInviteOnly() && !targetChannel->isOperator(&client)) {
		messageHandle(ERR_CHANOPRIVSNEEDED, client, channelInvitedTo, params);
		return logMessage(ERROR, "INVITE", "User " + client.getNickname() + " does not have operator rights for invite-only channel " + channelInvitedTo);
	}
	Client* clientToBeInvited = nullptr;
	for (auto& pair : clients_) {
		if (pair.second && pair.second->getNickname() == userToBeInvited) {
			clientToBeInvited = pair.second.get();
			break;
		}
	}
	if (clientToBeInvited == nullptr) {
		messageHandle(ERR_NOSUCHNICK, client, "INVITE", params);
		return logMessage(ERROR, "INVITE", "User " + userToBeInvited + " does not exist");
	}
	if (targetChannel->isMember(clientToBeInvited)) { // user to be invited already a member of the channel
		messageHandle(ERR_USERONCHANNEL, client, channelInvitedTo, params);
		return logMessage(ERROR, "INVITE", "User " + client.getNickname() + " already on channel " + channelInvitedTo);
	}
	if (clientToBeInvited == &client) { // user can't invite themselves
		return logMessage(WARNING, "INVITE", "User " + client.getNickname() + " attempted to invite themselves to channel " + channelInvitedTo);
	}
	targetChannel->addInvite(clientToBeInvited); // add client to the invited_ list for the channel

	messageHandle(RPL_INVITING, client, channelInvitedTo, params);
	messageToClient(*clientToBeInvited, client, "INVITE", channelInvitedTo);
    logMessage(INFO, "INVITE", "User " + client.getNickname() + " inviting " + userToBeInvited + " to " + channelInvitedTo);
}

int Server::handleTopicParams(Client& client, const std::vector<std::string>& params) {
	if (params.empty() || params[0].empty()) {
		// MESSAGE client that they didn't include any parameters
		messageHandle(ERR_NEEDMOREPARAMS, client, "TOPIC", params);
		logMessage(ERROR, "TOPIC", "No channel specified");
		return ERR;
	}
	return SUCCESS;
}

void Server::handleTopic(Client& client, const std::vector<std::string>& params) {
	if (handleTopicParams(client, params) == ERR)
		return;
	std::string channel = params[0];
	bool topicGiven = true;
	if (params[1].empty()) {
		topicGiven = false;
	}
	if (!channelExists(channel)) {
		messageHandle(ERR_NOSUCHCHANNEL, client, channel, params);
		return logMessage(WARNING, "TOPIC", "Channel " + channel + " does not exist");
	}
	Channel* targetChannel = getChannel(channel);
	logMessage(DEBUG, "TOPIC", "CURRENT TOPIC: " + targetChannel->getTopic());
	if (!topicGiven && targetChannel->getTopic().empty()) { // if topic not given and channel topic has not been set, print "no topic"
		messageHandle(RPL_NOTOPIC, client, channel, params);
		return logMessage(WARNING, "TOPIC", "No topic set for channel " + channel);
	}
	else if (!topicGiven) { // if topic not given as argument and topic is already set for channel, print the topic
		messageHandle(RPL_TOPIC, client, "JOIN", {targetChannel->getName() + " :" + targetChannel->getTopic()});
		return logMessage(DEBUG, "TOPIC", targetChannel->getTopic());
	}
	const std::set<Client*>& members = targetChannel->getMembers();
	if (members.find(&client) == members.end()) { // if user wanting to set the topic has not joined the channel they can't set the topic
		messageHandle(ERR_NOTONCHANNEL, client, channel, params);
		return logMessage(ERROR, "TOPIC", "User " + client.getNickname() + " not on channel " + channel);
	}
	std::string topic = params[1];
	if (topicGiven && topic.size() > 300) { // truncate overlong topic
		topic.resize(300);
	}
	if (topicGiven && !targetChannel->isTopicOperatorOnly()) { // if topic is given and the mode +t has not been set we can set the topic
		targetChannel->setTopic(topic);
		messageBroadcast(*targetChannel, client, "TOPIC", topic);
		return logMessage(DEBUG, "TOPIC", "User " + client.getNickname() + " set new topic: " + topic + " for channel " + channel);
	}
	else if (topicGiven && targetChannel->isTopicOperatorOnly()) { // if topic can be set by operators only (mode +t), either set the topic if user is operator or print error
		if (!targetChannel->isOperator(&client)) {
			messageHandle(ERR_CHANOPRIVSNEEDED, client, channel, params);
			return logMessage(DEBUG, "TOPIC", "User " + client.getNickname() + " unable to set topic for channel " + channel + " (NOT AN OPERATOR)");
		}
	}
	targetChannel->setTopic(topic);
	messageHandle(RPL_TOPIC, client, "JOIN", {targetChannel->getName() + " :" + targetChannel->getTopic()});
	messageBroadcast(*targetChannel, client, "TOPIC", topic);
	logMessage(DEBUG, "TOPIC", "User " + client.getNickname() + " set new topic: " + topic + " for channel " + channel);
}
