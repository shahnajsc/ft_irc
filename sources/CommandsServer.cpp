#include "../includes/Server.hpp"
#include "../includes/responseCodes.hpp"
#include "../includes/macros.hpp"

void Server::registerCommands() {

	commands["CAP"] = [this](Client& client, const std::vector<std::string>& params) {
		(void)params;
		(void)this;
		logMessage(WARNING, "CAP", "CAP command ignored. ClientFD: " + std::to_string(client.getClientFD()));
	};

	commands["WHO"] = [this](Client& client, const std::vector<std::string>& params) {
		(void)params;
		(void)this;
		logMessage(WARNING, "WHO", "WHO command ignored. ClientFD: " + std::to_string(client.getClientFD()));
	};

	commands["PING"] = [this](Client& client, const std::vector<std::string>& params) {
		handlePing(client, params);
	};

	commands["NICK"] = [this](Client& client, const std::vector<std::string>& params) {
		handleNick(client, params);
    };

    commands["JOIN"] = [this](Client& client, const std::vector<std::string>& params) {
	    handleJoin(client, params);
    };
	commands["QUIT"] = [this](Client& client, const std::vector<std::string>& params) {
		handleQuit(client, params);
    };

	commands["USER"] = [this](Client& client, const std::vector<std::string>& params) {
		handleUser(client, params);
	};

	commands["PASS"] = [this](Client& client, const std::vector<std::string>& params) {
		handlePass(client, params);
	};

	commands["MODE"] = [this](Client& client, const std::vector<std::string>& params) {
		handleMode(client, params);
	};

	commands["PRIVMSG"] = [this](Client& client, const std::vector<std::string>& params) {
		handlePrivMsg(client, params);
	};

	commands["KICK"] = [this](Client& client, const std::vector<std::string>& params) {
		handleKick(client, params);
	};

	commands["INVITE"] = [this](Client& client, const std::vector<std::string>& params) {
		handleInvite(client, params);
	};

	commands["TOPIC"] = [this](Client& client, const std::vector<std::string>& params) {
		handleTopic(client, params);
	};

	commands["WHOIS"] = [this](Client& client, const std::vector<std::string>& params) {
		handleWhois(client, params);
	};

	// std::cout << "PARAM SIZE: " << params.size() << std::endl;
	// for (const std::string& param : params) {
	// 	std::cout << "- " << param << std::endl;
	// }
}

void Server::handlePing(Client& client, const std::vector<std::string>& params) {

	if (params.empty()) {
		messageHandle(ERR_NOORIGIN, client, "PING", params);
	}
	else if (params[0] != this->serverName_) {
		messageHandle(ERR_NOSUCHSERVER, client, "PING", params);
	}
	else {
		messageHandle(RPL_PONG, client, "PING", params);
		logMessage(DEBUG, "PING", "PING received. Client FD: " + std::to_string(client.getClientFD()));
	}
}

void Server::handleQuit(Client& client, const std::vector<std::string>& params) {
    if (!client.isConnected() || !client.isAuthenticated()) {//no broadcasting from unconnected or unregistered clients
		logMessage(INFO, "QUIT", "Closed unauthenticated/unresponsive client " + client.getNickname());
		return closeClient(client);
	}
	std::string reason = "Client quit";
	if (!params.empty())
		reason = params[0];
	messageBroadcast(client, "QUIT", " :" + reason);
	logMessage(INFO, "QUIT", "User " + client.getNickname() + " quit (reason: " + reason + ")");
	closeClient(client);
	logMessage(DEBUG, "QUIT", "Server still alive after closing client");
}

void Server::closeClient(Client& client) {

	int clientfd = client.getClientFD();
	int epollfd = client.getEpollFd();
	leaveAllChannels(client); // remove client from Channel member lists and clear joinedChannels
	client.setConnected(false);
	struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = clientfd;
	epoll_ctl(epollfd, EPOLL_CTL_DEL, clientfd, &ev);
	clients_.erase(clientfd);
}

void Server::handleWhois(Client& client, const std::vector<std::string>& params) {

	std::string nickName;

	if (params.empty()) {
		messageHandle(ERR_NONICKNAMEGIVEN, client, "WHOIS", params);
		return;
	}
	else if (params.size() > 1) {
		if (params[0].empty() && params[0] != this->serverName_) {
			messageHandle(ERR_NOSUCHSERVER, client, "WHOIS", params);
			return;
		}
		nickName = params[1];
	}
	else
		nickName = params[0];

	if (nickName == "") {
		messageHandle(ERR_NONICKNAMEGIVEN, client, "WHOIS", params);
		return;
	}

	if (getClient(nickName) == nullptr) {
		messageHandle(ERR_NOSUCHNICK, client, "WHOIS", params);
		return;
	}
	messageHandle(RPL_ENDOFWHOIS, client, "WHOIS", {nickName, ":End of /WHOIS list"});
}
