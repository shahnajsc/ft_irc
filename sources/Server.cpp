#include "../includes/Server.hpp"
#include "../includes/Client.hpp"
#include "../includes/responseCodes.hpp"

volatile sig_atomic_t Server::isRunning_ = true; // change the value to true when it start

Server::Server(int port, std::string password) : port_(port), password_(password), serverSocket_(-1), epollFd(-1) {
	initAddrInfo();
	createAddrInfo();
	createServSocket();
	setNonBlocking();
	setSocketOption();
	bindSocket();
	initListen();

	logMessage(INFO, "SERVER", "Server created. PORT[" + std::to_string(port_) + "] PASSWORD[" + password_ + "]");
	customSignals(true);
	registerCommands();
}

Server::~Server() {
	if (isRunning_)
		closeServer();
}

void Server::closeServer() {
	logMessage(INFO, "SERVER", "Server closing [closeServer()]");

	auto it = clients_.begin();
	while (it != clients_.end()) 
	{
		if (it->first > 4 && it->second) {
			auto next = std::next(it);
			closeClient(*it->second);
			it = next;
		} else {
			it++;
		}
	}
	clients_.clear();
	for (auto& [channelName, channel] : channelMap_) {
		if (channel)
			delete channel;
	}
	channelMap_.clear();
	if (epollFd >= 0)
		close(epollFd);
	if (serverSocket_ >= 0)
		close(serverSocket_);
	if (res_ != nullptr) {
		freeaddrinfo(res_);
		res_ = nullptr;
	}
	commands.clear();
	password_.clear();
	customSignals(false);
	isRunning_ = false;
	exit(0);
}

// PRIVATE MEMBER FUNCTIONS USED WITHIN THE SERVER CONSTRUCTOR
// ===========================================================

// add default settings to addrinfo struct
void Server::initAddrInfo() {
	std::memset(&hints_, 0, sizeof(hints_));
	hints_.ai_family = AF_INET;       // Allow IPv4
	hints_.ai_socktype = SOCK_STREAM;   // TCP socket
	hints_.ai_flags = AI_PASSIVE;       // suitable for server use with bind()
}

// translates service location/name to a set of socket addresses in the addrinfo struct
void Server::createAddrInfo() {
	int error = getaddrinfo(NULL, std::to_string(port_).c_str(), &hints_, &res_);
	if (error)
		throw std::runtime_error("getaddrinfo: " + std::string(gai_strerror(error)));
}

// creates a new TCP socket
void Server::createServSocket() {
	serverSocket_ = socket(res_->ai_family, res_->ai_socktype, 0);
	if (serverSocket_ < 0)
		throw std::runtime_error("failed to create socket");
}

void Server::setNonBlocking() {
	if (fcntl(serverSocket_, F_SETFL, O_NONBLOCK) == -1)
		throw std::runtime_error("fcntl failed to set non-blocking");
}

void Server::customSignals(bool customSignals) {
	if (customSignals) {
		signal(SIGINT, stop); // ctrl+c
		signal(SIGTERM, stop); // kill command shutdown
		signal(SIGTSTP, stop); // ctrl+z
		signal(SIGPIPE, SIG_IGN); // prevent crashes from broken pipes
	}
	else {
		signal(SIGINT, SIG_DFL);
		signal(SIGTERM, SIG_DFL);
		signal(SIGTSTP, SIG_DFL);
		signal(SIGPIPE, SIG_DFL);
	}
}

void Server::stop(int signum) {
	if (signum == SIGINT || signum == SIGTSTP || signum == SIGTERM)
		isRunning_ = false;
}

void Server::startServer() {
	logMessage(INFO, "SERVER", "Server started. SOCKET[" + std::to_string(serverSocket_) + "]");
	epollFd = epoll_create1(EPOLL_CLOEXEC); // check later if we need this EPOLL_CLOEXEC flag(1)

	if (epollFd < 0) {
		throw std::runtime_error("epoll fd creating failed");
	}
	struct epoll_event serverEvent; // epoll event for Listening socket (new connections monitoring)
	serverEvent.events = EPOLLIN;
	serverEvent.data.fd = serverSocket_;
	if (epoll_ctl(epollFd, EPOLL_CTL_ADD, serverEvent.data.fd, &serverEvent) < 0) {
		close (epollFd);
		throw std::runtime_error("Adding server socket to epoll failed");
	}

	struct epoll_event epEventList[MAX_EVENTS];

	logMessage(INFO, "SERVER", "Waiting for events. ServerFD[" + std::to_string(epollFd) + "]");

	while(true) {
		int epActiveSockets = epoll_wait(epollFd, epEventList, MAX_EVENTS, 4200); // timeout time?

		if (!isRunning_)
			closeServer();
		if (epActiveSockets < 0) {
			throw std::runtime_error("Epoll waiting failed");
		}
		if (epActiveSockets > 0) {
			for (int i = 0; i < epActiveSockets; ++i)
			{
				if (epEventList[i].data.fd == serverSocket_) {
					acceptNewClient(epollFd);
				}
				else if (epEventList[i].events & EPOLLIN) {
					receiveData(epEventList[i].data.fd);
				}
				else if (epEventList[i].events & EPOLLOUT) {
					sendData(epEventList[i].data.fd);
				}
			}
		}
	}
}

// here we split the line into command and arguments (params)
std::pair<std::string, std::vector<std::string>> Server::parseCommand(const std::string& line) {
	std::string cmd; // the command to be stored
	std::vector<std::string> params; // the arguments to be stored
	std::istringstream iss(line);
	iss >> cmd;
	std::string token;
	while (iss >> token) {

		if (!token.empty() && token[0] == ':') { //check for ':' to indicate the last argument
			std::string lastParam;
			std::getline(iss, lastParam);
			params.push_back(token.substr(1) + lastParam);
			break; // Stop parsing as this is the last parameter.
		}
		params.push_back(token);
		}
	return {cmd, params};
}

void Server::processBuffer(Client& client) {
	std::string buf = client.getReadBuffer();
	size_t pos;

	while ((pos = buf.find("\r\n")) != std::string::npos) {
		logMessage(DEBUG, "PRINT", "BUFFER: " + buf);
		std::string line = buf.substr(0, pos);
		buf.erase(0, pos + 2);
		std::pair<std::string, std::vector<std::string>> parsed = parseCommand(line);
		std::string commandStr = parsed.first;
        std::vector<std::string> params = parsed.second;
		std::transform(commandStr.begin(), commandStr.end(), commandStr.begin(), ::toupper);
		logMessage(DEBUG, "COMMAND", "C[" + commandStr + "]");
		if (commandStr == "QUIT") // we need to check for commands that close the client separately as we don't want to try to access a client (eg. client.setBuffer(buf);)that's already terminated (seg fault..)
			return handleQuit(client, params);
		if ((commandStr == "USER" || commandStr == "PASS" || commandStr == "CAP") && client.isAuthenticated()) {
			messageHandle(ERR_ALREADYREGISTERED, client, commandStr, params);
			continue;
		}
		if ((commandStr != "NICK" && commandStr != "USER" && commandStr != "PASS" && commandStr != "CAP") && !client.isAuthenticated()) {
			messageHandle(ERR_NOTREGISTERED, client, commandStr, params);
			continue;
		}
		auto it = commands.find(commandStr);
		if (it == commands.end()) {
			logMessage(WARNING, "COMMAND", "Unknown command: [" + commandStr + "]" + std::to_string(client.getClientFD()));
			messageHandle(ERR_UNKNOWNCOMMAND, client, commandStr, params);
			continue;
		}
		it->second(client, params);
	}
	client.setBuffer(buf);
}

void Server::receiveData(int currentFD) {
	std::unique_ptr<Client>& client = clients_.at(currentFD); //get current Client from map
	if (client->receiveData() == FAIL) {
		client->setConnected(false);
		epoll_ctl(client->getEpollFd(), EPOLL_CTL_DEL, currentFD, NULL);
		clients_.erase(currentFD);
		return;
	}
	processBuffer(*client);
}

void Server::sendData(int currentFD) {
	std::unique_ptr<Client>& client = clients_.at(currentFD);
	if (client->sendData() == FAIL) {
		throw std::runtime_error("Sending Msg Failed");
	}
}

std::string Server::getClientIP(struct sockaddr_in clientSocAddr) {
	char clientIP[INET_ADDRSTRLEN]; // if needed convert clientIP to std::string "std::string(clientIP)"
		 // logging client IP | Do we need to log?? or only IP
	if (!inet_ntop(AF_INET, &clientSocAddr.sin_addr, clientIP, sizeof(clientIP))) {
		return ("");
	}
	//std::cout << "Client IP from GET Method: " << clientIP << std::endl; // remove later***
	return (std::string(clientIP));
}

void Server::acceptNewClient(int epollFd) {
	//std::cout << "Inside Accept" << std::endl;
	struct  sockaddr_in clientSocAddr;
	socklen_t clientSocLen = sizeof(clientSocAddr);

	int clientFd = accept(serverSocket_, (struct sockaddr*)&clientSocAddr, &clientSocLen);
	if  (clientFd < 0) {
		throw std::runtime_error("Client accept() failed");
		// should exit or return to main/server loop???
	}
	else
	{
		//usleep(10000);
		if (fcntl(clientFd, F_SETFL, O_NONBLOCK) < 0) { // Make client non-blocking
			close(clientFd);
			throw std::runtime_error("fcntl() failed for client");
		}
		std::string clientIP = getClientIP(clientSocAddr);
		if (clientIP.empty()) {
			close(clientFd);
			throw std::runtime_error("Failed to retrieve client IP");
		}

		// Prepare epoll_event for this client
		struct epoll_event clientEvent;
		clientEvent.events = EPOLLIN; // start listening for read events
		clientEvent.data.fd = clientFd;

		if (epoll_ctl(epollFd, EPOLL_CTL_ADD, clientFd, &clientEvent) < 0) {
			close(clientFd);
			throw std::runtime_error("epoll_ctl() failed for client");
		}
		// After succesfull steps here you list(add) your new client std::string clientIP = inet_ntoa(clientSocAddr.sin_addr);
		clients_[clientFd] = std::make_unique<Client>(clientFd, clientIP, epollFd); // what about client Index 0-3??*****
		//logMessage(INFO, "CLIENT", "New client accepted. ClientFD [ " + clientFd + " ]");
	}
}

// we set socket option for all sockets (SOL_SOCKET) to SO_REUSEADDR which enables us to reuse local addresses
// to avoid "address already in use" error
void Server::setSocketOption() {
	int opt = 1;
	if (setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
		throw std::runtime_error("failed to set socket option: SO_REUSEADDR");
}

// we bind the socket to the address
void Server::bindSocket() {
	if (bind(serverSocket_, res_->ai_addr, res_->ai_addrlen) == -1)
		throw(std::runtime_error("failed to bind the socket"));
}

// prepare to listen for incoming connections on socket fd. we set the amount of connection requests to max (SOMAXCONN)
void Server::initListen() {
	if (listen(serverSocket_, SOMAXCONN) == -1)
		throw(std::runtime_error("failed to init listen()"));
}

// ACCESSORS
// =========

int Server::getPort() const {
	return port_;
}

std::string Server::getPassword() const {
	return password_;
}

int Server::getServerSocket() const {
	return serverSocket_;
}

std::string Server::getServerName() const {
	return (this->serverName_);
}

bool Server::stringCompCaseIgnore(const std::string &str1, const std::string &str2) {
	std::string str1Lower = str1;
	std::transform(str1Lower.begin(), str1Lower.end(), str1Lower.begin(),
	               [](unsigned char c){ return std::tolower(c); });

	std::string str2Lower = str2;
	std::transform(str2Lower.begin(), str2Lower.end(), str2Lower.begin(),
	               [](unsigned char c){ return std::tolower(c); });

	if (str1Lower == str2Lower)
	{
		return (true);
	}
	else
		return (false);
}

// **Structured bindings ([fd, client]) were added in C++17, so g++/clang++ complains.

// bool Server::isUserDuplicate(std::string userName) {
// 	for (auto& [fd, client] : this->clients_) {
// 		if (client && stringCompCaseIgnore(client->getUsername(), userName))
// 		{
// 			return (true); // Duplicate found
// 		}
// 	}
// 	return (false);   //  this exits after first client!
// }

bool	Server::isNickDuplicate(std::string  nickName) {

	for (auto& [fd, client] : this->clients_) {
		if (client && stringCompCaseIgnore(client->getNickname(), nickName))
		{
			return (true);// Duplicate found
		}
	}
	return (false);
}

Channel* Server::getChannel(const std::string& channelName) {
	auto it = channelMap_.find(channelName);
	if (it != channelMap_.end())
		return it->second;
	return nullptr;
}

bool Server::isClientChannelMember(Channel *channel, Client& client) {
	const std::set<Client*>& members = channel->getMembers();
	if (members.find(&client) == members.end()) {
		return false;
	}
	return true;
}

Client* Server::getClient(const std::string& nickName) {
	for (auto& [fd, clientPtr] : clients_) {
		if (clientPtr && stringCompCaseIgnore(clientPtr->getNickname(), nickName)) {
			return clientPtr.get();  // return raw pointer from unique_ptr
		}
	}
	return nullptr;  // not found
}
