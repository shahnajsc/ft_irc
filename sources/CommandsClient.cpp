#include "../includes/Server.hpp"
#include "../includes/responseCodes.hpp"
#include "../includes/macros.hpp"

void Server::handlePass(Client& client, const std::vector<std::string>& params) {

	if (params.empty() || params[0].empty()) {
		messageHandle(ERR_NEEDMOREPARAMS, client, "PASS", params);
		logMessage(ERROR, "PASS", "Empty password");
		return;
	}
	else if (params[0] != this->getPassword()) {
		messageHandle(ERR_PASSWDMISMATCH, client, "PASS", params);
		logMessage(ERROR, "PASS", "Password mismatch. Given Password: " + params[0]);
		return;
	}
	else if (client.getIsAuthenticated()) {
		messageHandle(ERR_ALREADYREGISTERED, client, "PASS", params);
		logMessage(WARNING, "PASS", "Client is already registered");
	}
	else {
		client.setPassword(params[0]);
		client.setIsPassValid(true);
		logMessage(INFO, "PASS", "Password validated for ClientFD: " + std::to_string(client.getClientFD()));
	}
}

int Server::handleNickParams(Client& client, const std::vector<std::string>& params) {

	if (!client.getIsPassValid()) {
		messageHandle(ERR_PASSWDMISMATCH, client, "NICK", params);
		logMessage(ERROR, "NICK", "Password is not set yet" + params[0]);
		return (FAIL);
	}
	else if (params.empty() || params[0].empty()) {
		messageHandle(ERR_NONICKNAMEGIVEN, client, "NICK", params);
		logMessage(ERROR, "NICK", "No nickname given. Client FD: " + std::to_string(client.getClientFD()));
		return (FAIL);
	}
	else if (params.size() > 1) {
		messageHandle(ERR_ERRONEUSNICKNAME, client, "NICK", params);
		logMessage(ERROR, "NICK", "Invalid nickname format. Given Nickname: " + params[0]);
		return (FAIL);
	}
	else if (!isNickUserValid("NICK", params[0])) {
		messageHandle(ERR_ERRONEUSNICKNAME, client, "NICK", params);
		logMessage(ERROR, "NICK", "Invalid nickname format. Given Nickname: " + params[0]);
		return (FAIL);
	}
	else if (client.getNickname() == params[0]) {
		logMessage(ERROR, "NICK", "Nickname is same as current one. Given Nickname: " + params[0]);
		return (FAIL);
	}
	else if (isNickDuplicate(params[0])) {
		messageHandle(ERR_NICKNAMEINUSE, client, "NICK", params);
		logMessage(WARNING, "NICK", "Nickname is already in use. Given Nickname: " + params[0]);
		return (FAIL);
	}
	return (SUCCESS);
}

void Server::handleNick(Client& client, const std::vector<std::string>& params) {

	if (handleNickParams(client, params) == FAIL)
		return;

	if (client.isAuthenticated())
	{
		std::string replyMsg = client.getClientIdentifier() + " NICK :" + params[0] + "\r\n";
		client.appendSendBuffer(replyMsg);
		messageBroadcast(client, "NICK", replyMsg);
		logMessage(INFO, "NICK", "Nickname changed to " + params[0] + ". Old Nickname: " + client.getNickname());
		client.setNickname(params[0]);
	}
	else {
		client.setNickname(params[0]);
		logMessage(INFO, "NICK", "Nickname set to " + client.getNickname());
		if (client.isAuthenticated()) {
			messageHandle(client, "NICK", params);
			logMessage(INFO, "REGISTRATION", "Client registration is successful. Nickname: " + client.getNickname());
		}
	}
}

int Server::handleUserParams(Client& client, const std::vector<std::string>& params) {

	if (!client.getIsPassValid()) {
		messageHandle(ERR_PASSWDMISMATCH, client, "USER", params);
		logMessage(ERROR, "USER", "Password is not set yet" + params[0]);
		return (FAIL);
	}
	else if (params.empty() || params[0].empty()) {
		messageHandle(ERR_NEEDMOREPARAMS, client, "USER", params); // what code to use??
		logMessage(ERROR, "USER", "No username given. Client FD: " + std::to_string(client.getClientFD()));
		return (FAIL);
	}
	else if (params.size() != 4) {
		messageHandle(ERR_NEEDMOREPARAMS, client, "USER", params);
		logMessage(ERROR, "USER", "Not enough parameters. Client FD: " + std::to_string(client.getClientFD()));
		return (FAIL);
	}
	else if (params[3].empty()) {
		messageHandle(ERR_NEEDMOREPARAMS, client, "USER", params);
		logMessage(ERROR, "USER", "Empty realname. Client FD: " + std::to_string(client.getClientFD()));
		return (FAIL);
	}
	else if (!isNickUserValid("USER", params[0])) { // do we need to check real name, host?
		messageHandle(ERR_ERRONEUSUSER, client, "NICK", params);
		logMessage(ERROR, "USER", "Invalid username format. Given Username: " + params[0]);
		return (FAIL);
	}
	return (SUCCESS);
}

void Server::handleUser(Client& client, const std::vector<std::string>& params) {

	if (handleUserParams(client, params) == FAIL)
		return;

	if (params[0][0] != '~')
		client.setUsername("~" + params[0]);
	else
		client.setUsername(params[0]);
	client.setHostname(params[1]);
	client.setRealName(params[3]);
	logMessage(INFO, "USER", "Username and details are set. Username: " + client.getUsername());
	if (client.isAuthenticated()) {
		messageHandle(client, "USER", params);
		logMessage(INFO, "REGISTRATION", "Client registration is successful. Nickname: " + client.getNickname());
	}
}

int Server::handlePrivMsgParams(Client& client, const std::vector<std::string>& params) {

	if (params.empty()) {
		messageHandle(ERR_NORECIPIENT, client, "PRIVMSG", params);
		logMessage(WARNING, "PRIVMSG", "No parameter provided. NICK: " + client.getNickname());
		return (FAIL);
	}
	else if (params[0].empty()) {
		messageHandle(ERR_NORECIPIENT, client, "PRIVMSG", params);
		logMessage(WARNING, "PRIVMSG", "No recipient to send msg. NICK: " + client.getNickname());
		return (FAIL);
	}
	else if (params.size() < 2 || params[1].empty()) {
		messageHandle(ERR_NOTEXTTOSEND, client, "PRIVMSG", params);
		logMessage(WARNING, "PRIVMSG", "No text to send. NICK: " + client.getNickname());
		return (FAIL);
	}
	return (SUCCESS);
}

void Server::handlePrivMsg(Client& client, const std::vector<std::string>& params) {

	if (handlePrivMsgParams(client, params) == FAIL)
		return;

	bool isChannel = false;
	std::string target = params[0];
	Client *targetClient;
	Channel *targetChannel;
	if (target[0] == '#') {
		isChannel = true;
		targetChannel = getChannel(target); // check the method

		if (targetChannel == nullptr || (targetChannel && !isClientChannelMember(targetChannel, client))) {
			messageHandle(ERR_CANNOTSENDTOCHAN, client, "PRIVMSG", params);
			logMessage(WARNING, "PRIVMSG", "No Channel/ client is not a member of channel: \"" + target + "\"");
			return ;
		}
	}
	else {
		targetClient = getClient(target);
		if (targetClient == nullptr || (targetClient && !targetClient->isAuthenticated())) {
			messageHandle(ERR_NOSUCHNICK, client, "PRIVMSG", params);
			logMessage(WARNING, "PRIVMSG", "No such nickname: \"" + target + "\"");
			return ;
		}
	}
	std::string msgToSend;
	if (params[1].length() > MAX_MSG_LEN) {
		msgToSend = params[1].substr(0, MAX_MSG_LEN);
	}
	else
		msgToSend = params[1];

	if (msgToSend[0] == ':') {
		msgToSend.erase(0, 1);
		if (msgToSend.empty()) {
			messageHandle(ERR_NOTEXTTOSEND, client, "PRIVMSG", params);
			logMessage(WARNING, "PRIVMSG", "No text to send");
			return;
		}
	}

	if (isChannel) {
		messageBroadcast(*targetChannel, client, "PRIVMSG", msgToSend);
		logMessage(INFO, "PRIVMSG", "Sending msg to Channel: " + targetChannel->getName());
	}
	else {
		messageToClient(*targetClient, client, "PRIVMSG", msgToSend);
		logMessage(INFO, "PRIVMSG", "Sending Msg to client: " + targetClient->getNickname());
	}
}
