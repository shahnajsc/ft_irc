#include "../includes/Client.hpp"

Client::Client(int clientFD, std::string clientIP, int epollFd)
: clientFD_(clientFD), epollFd_(epollFd), nickname_(""), username_(""),
  hostname_(clientIP), realName_(""), password_(""), authenticated_(false), connected_(true), isPassValid_(false) {
	//std::cout << GREEN "\n=== CLIENT CREATED ===\n" END_COLOR;
	//std::cout << "clientFD: " << clientFD_ << "\n";
	//std::cout << "hostname: " << hostname_ << "\n";
	logMessage(INFO, "CLIENT", "New client created. ClientFD[" + std::to_string(clientFD_) + "]");
}

Client::~Client() {
	joinedChannels_.clear();
	nickname_.clear();
	readBuffer_.clear();
	sendBuffer_.clear();
	logMessage(DEBUG, "CLIENT", "Client destroyed");
	close(clientFD_);
}

// PUBLIC MEMBER FUNCTIONS
// =======================

int Client::receiveData() {
	char buffer[BUF_SIZE];

	ssize_t bytesRead = recv(clientFD_, buffer, BUF_SIZE, MSG_DONTWAIT);
    if (bytesRead > 0) {

    std::string received(buffer, bytesRead);
		addReadBuffer(received);
		//std::cout << readBuffer_ << "\n";
		return SUCCESS;
	}
	else if (!bytesRead) {
		std::cout << "Client " << clientFD_ << " disconnected." << std::endl;
	}
	else if (bytesRead < 0) {
		std::cerr << "recv failed\n";
	}
	return FAIL;
}


int Client::sendData() {
	if (sendBuffer_.empty())
		return (SUCCESS);

	int sentByte = send(this->clientFD_, this->sendBuffer_.c_str(), this->sendBuffer_.size(), MSG_DONTWAIT);
	if (sentByte < 0 && errno != EWOULDBLOCK && errno != EAGAIN)
		return (FAIL);
	this->sendBuffer_.erase(0, sentByte);
	if (sendBuffer_.empty()) {
		epollEventChange(EPOLLIN);
	}
	return (SUCCESS);
}

// After successfull msg process method will call appendSendBuffer to create EPOLLOUT event
void Client::appendSendBuffer(std::string sendMsg) {
	if (sendMsg.length() >= 2 && 
        sendMsg.substr(sendMsg.length() - 2) != "\r\n") {
        sendMsg += "\r\n";
    }
	this->sendBuffer_.append(sendMsg);
	epollEventChange(EPOLLOUT);
}

void Client::addReadBuffer(const std::string& received) {
	readBuffer_.append(received);
}

// Method to change EPOLL IN/OUT event depending on client request
void Client::epollEventChange(uint32_t eventType) {
	//std::cout << "INSIDE event change: " << sendBuffer_ << "\n";
	struct epoll_event newEvent;
	newEvent.events = eventType;
	newEvent.data.fd = this->getClientFD();
	//std::cout << "Event created, client fd: " << newEvent.data.fd << "\n";

	if (epoll_ctl(this->epollFd_, EPOLL_CTL_MOD, newEvent.data.fd, &newEvent) < 0) {
		this->sendBuffer_.clear();
		throw std::runtime_error("epoll_ctl() failed for client data receive/send " + std::string(strerror(errno))); // change error msg
	}

}
//------ CHANNEL --------
void Client::addToJoinedChannelList(const std::string &channelName) {

	joinedChannels_.insert(channelName);
	logMessage(INFO, "CLIENT",
		getNickname() + " joined channel: " + channelName);
}

bool Client::isInChannel(const std::string& channelName) {

	return joinedChannels_.find(channelName) != joinedChannels_.end();
}

void Client::leaveChannel(const std::string &channelName) {
	if (isInChannel(channelName))
		joinedChannels_.erase(channelName);
}

bool Client::isConnected() const {
	if (!connected_) {
		return false;
	}
	if (clientFD_ < 0) {
		return false;
	}
	if (!isSocketValid()) {
		return false;
	}
    return true;
}

bool Client::isAuthenticated() {
	if (realName_.empty() || username_.empty() || nickname_.empty()
		|| password_.empty() || !isPassValid_ || !clientFD_ || hostname_.empty()) {
		return false;
	}
	authenticated_ = true;
	return authenticated_;
}

// PRIVATE MEMBER FUNCTIONS
// ========================

bool Client::isSocketValid() const {
	if (clientFD_ < 0)
		return false;

	// Use send with MSG_NOSIGNAL to test if socket is alive
	// This doesn't actually send data
	int result = send(clientFD_, "", 0, MSG_NOSIGNAL);
	if (result == -1) {
		if (errno == EBADF || errno == ENOTSOCK || errno == EPIPE || errno == ECONNRESET) {
			return false;
		}
	}
	return true;
}

// ACCESSORS
// =========

int Client::getClientFD () const {
	return clientFD_;
}

int Client::getEpollFd() const {
	return (this->epollFd_);
}

std::string Client::getHostname () const {
	return hostname_;
}

std::string Client::getNickname () const {
	return nickname_;
}

std::string Client::getUsername () const {
	return username_;
}

std::string Client::getRealName() const {
	return realName_;
}

std::string Client::getPassword() const {
	return password_;
}

std::string Client::getReadBuffer() const {
	return readBuffer_;
}

std::string Client::getSendBuffer() const {
	return sendBuffer_;
}

bool Client::getIsAuthenticated() const {
	return authenticated_;
}

std::string Client::getClientIdentifier() const {
	return (":" + nickname_ + "!" + username_ + "@" + hostname_);
}

bool Client::getIsPassValid() const {
	return isPassValid_;
}

const std::set<std::string>& Client::getJoinedChannels() const {
	return joinedChannels_;
}

void Client::setHostname(std::string hostname) {
	hostname_ = hostname;
}

void Client::setNickname(std::string nickname) {
	nickname_ = nickname;
}

void Client::setUsername(std::string username) {
	username_ = username;
}

void Client::setRealName(std::string realName) {
	realName_ = realName;
}

void Client::setPassword(std::string password) {
	password_ = password;
}

void Client::setBuffer(std::string buffer) {
	readBuffer_ = buffer;
}

void Client::setConnected(bool connected) {
	connected_ = connected;
}

void Client::setAuthenticated(bool authenticated) {
	authenticated_ = authenticated;
}

void Client::setIsPassValid(bool isPassValid) {
	isPassValid_ = isPassValid;
}
