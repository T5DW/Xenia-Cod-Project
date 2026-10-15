// Small socket and HTTP helpers shared by the DemonWare lobby, the Xenia web API and the peer links.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	include <winsock2.h>
#	include <ws2tcpip.h>
using sock_t = SOCKET;
#	define BAD_SOCK INVALID_SOCKET
#else
#	include <arpa/inet.h>
#	include <netdb.h>
#	include <netinet/in.h>
#	include <netinet/tcp.h>
#	include <sys/socket.h>
#	include <unistd.h>
using sock_t = int;
#	define BAD_SOCK (-1)
#endif

namespace net {

bool startup();
void close_socket(sock_t s);
// TCP listener on 0.0.0.0:port, or BAD_SOCK (error in `err`)
sock_t listen_tcp(int port, std::string& err);
sock_t bind_udp(int port, bool broadcast, std::string& err);
bool send_all(sock_t s, const void* data, size_t size);
std::string peer_ip(sock_t s);
std::string local_ip_of(sock_t s);
std::string ip_to_string(const sockaddr_in& a);
// The LAN address of this PC (the interface used for outgoing traffic), 127.0.0.1 if none
std::string lan_ip();
std::vector<std::string> local_ips();
bool is_loopback(const std::string& ip);

struct HttpRequest {
	std::string method, path, query, body;
	std::map<std::string, std::string> headers;  // lower-case names
	std::string client_ip;
};

struct HttpResponse {
	int status = 200;
	std::string body;
	std::string content_type = "application/json";
};

// Reads one HTTP/1.x request from a connected socket.
bool read_request(sock_t s, HttpRequest& req);
void write_response(sock_t s, const HttpResponse& res, bool head_only = false);

// Plain HTTP/1.0 client with a timeout, for talking to peers. Returns false on network errors.
bool http_call(const std::string& host, int port, const std::string& method, const std::string& path,
	const std::string& body, int timeout_ms, int& status, std::string& response);

}  // namespace net
