#include "Channel.hpp"
#include "../includes/macros.hpp"
#include "../includes/Server.hpp"
#include "../includes/responseCodes.hpp"


Channel::Channel(Client* client, const std::string &name, const std::string& key)
	: name_(name), key_(""), keyProtected_(false), inviteOnly_(false), topicOperatorOnly_(true), userLimit_(-1), topic_("") {

	if (!key.empty())
		setChannelKey(key);
	logMessage(INFO, "CHANNEL", "New channel created. Name: ["+ this->getName() + "].");
	setOperator(client, true); // set the client creating the channel as operator by default
	if (isOperator(client))
		logMessage(DEBUG, "CLIENT", "Client " + client->getNickname() + "set as operator.");
}

Channel::~Channel() {
	members_.clear();
	operators_.clear();
	topic_.clear();
	logMessage(WARNING, "CHANNEL", ": Channel destroyed");
}

//PUBLIC METHODS
std::string Channel::getModeString() {
    std::string modes = "+";
    if (isInviteOnly())
        modes += "i";
    if (isKeyProtected())
        modes += "t";
    if (getUserLimit() > 0)
        modes += "l";
    if (modes == "+")  // No modes active
        return "";
    return modes;
}

bool Channel::isMember(Client* client) {
	auto it = members_.find(client);
	if (it == members_.end()) {
		return false;
	}
	return true;
}

void Channel::addChannelMember(Client *client) {

	members_.insert(client);
	logMessage(INFO, "CHANNEL", this->getName() +
		": Client " +  client->getNickname() + " Joined");
}

void Channel::removeMember(Client *client) {

 	size_t status =  members_.erase(client);
 	if (status)
		logMessage(DEBUG, "CHANNEL", "Member <" + client->getNickname() + "> is removed from channel " + this->getName());
 	else
		logMessage(DEBUG, "CHANNEL", "Member <" + client->getNickname() + "> not found on channel " + this->getName());
 }

 void Channel::addInvite(Client *client) {
	invited_.insert(client);
	logMessage(DEBUG, "CHANNEL", this->getName() +
		": Client " +  client->getNickname() + " added to invited list");
}

bool Channel::isKeyProtected() {
	return (this->keyProtected_);
}

bool Channel::isInviteOnly() {
	return inviteOnly_;
}

bool Channel::isClientInvited(Client* client) const {
	auto it = invited_.find(client);
	if (it != invited_.end())
		return true;
	return false;
}

bool isValidChannelKey(const std::string &key) {

	if (key.length() < 1 || key.length() > 32)
		return false;

	const std::string allowedSymbols = "!@#$%^&*()-_+=~";

	for (char c: key) {
		if (!std::isalnum(c) && allowedSymbols.find(c) == std::string::npos)
			return false;
	}
	return true;
}

bool isValidChannelName(const std::string& name) {

	if (name.empty() || name.size() > 50)
		return false;
	if (name[0] != '#')
		return false;

	static const std::string inavalidChar = " ,\a:";
	for (size_t i = 0; i < name.size(); i++) {
		if (inavalidChar.find(name[i]) != std::string::npos)
			return false;
	}
	return true;
}

bool Channel::checkKey(Channel* channel, Client* client, const std::string& providedKey) {
    if (!providedKey.empty() && !isValidChannelKey(providedKey)) {
        logMessage(ERROR, "CHANNEL", "Client '" + client->getNickname()
		+ "' provided an invalid channel key format for channel '" + getName() + "'.");
        return false;
    }
    if (!channel->isKeyProtected())
        return true;

    if (providedKey.empty()) {
        logMessage(ERROR, "CHANNEL", "Client '" + client->getNickname()
		+ "' attempted to join key-protected channel '" + getName() + "' without providing a key.");
        return false;
    }

    if (getChannelKey() != providedKey) {
        logMessage(ERROR, "CHANNEL", "Client '" + client->getNickname()
		+ "' provided an incorrect key for channel '" + getName() + "'. Join rejected.");
        return false;
    }
    logMessage(INFO, "CHANNEL", "Client '" + client->getNickname() +
	"' successfully joined key-protected channel '" + getName() + "'.");
    return true;
}

bool Channel::isOperator(Client* client) const {
	return operators_.find(client) != operators_.end();
}


/* bool Channel::checkKey(Channel* channel, Client* client, const std::string& providedKey) {

	if (!channel->isKeyProtected())
		return true;
	if (providedKey.empty()) {
		logMessage(ERROR, "CHANNEL",
			"Key required: " + client->getNickname() + " failed to join");
		return false;
	}
	return true;
} */


bool Channel::checkChannelLimit(Client &client, Channel &channel) {
    if (static_cast<int>(channel.getMembers().size()) < channel.getUserLimit())
        return true;
   	//messageHandle(ERR_CHANNELISFULL, client, "JOIN", {channel.getName(), std::to_string(channel.getUserLimit())});
    logMessage(ERROR, "CHANNEL", "Client '" + client.getNickname() +
	"' attempted to join channel '" + channel.getName() + 
    "', but the channel is full (limit: " + std::to_string(channel.getUserLimit()) + ").");
    return false;
}

// TOPIC ACCESSORS

bool Channel::isTopicOperatorOnly() const {
	return topicOperatorOnly_;
}

std::string Channel::getTopic() const {
	return topic_;
}

void Channel::setTopic(const std::string& topic) {
	topic_ = topic;
}

void Channel::setTopicOperatorOnly(bool topicOperatorOnly) {
	topicOperatorOnly_ = topicOperatorOnly;
}

// ACCESSORS
void Channel::setChannelKey(const std::string& key) {

	key_ = key;
	keyProtected_ = !key.empty();
}

void Channel::setInviteOnly(bool inviteOnly) {
	inviteOnly_ = inviteOnly;
}

std::string Channel::getChannelKey() const {
	return this->key_;
}

std::string Channel::getName() const {
	return this->name_;
}

const std::set<Client*>& Channel::getMembers() const {
	return members_;
}

const std::set<Client*>& Channel::getOperators() const {
	return operators_;
}

void Channel::setOperator(Client* client, bool isOperator) {
	if (isOperator)
		operators_.insert(client);
	else
		operators_.erase(client);

}

void Channel::setUserLimit(int userLimit) {
	userLimit_ = userLimit;
}

int Channel::getUserLimit() const {
	return userLimit_;
}
