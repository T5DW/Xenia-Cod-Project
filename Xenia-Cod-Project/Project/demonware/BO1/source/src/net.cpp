#include "net.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

#ifdef _WIN32
#	include <iphlpapi.h>
#else
#	include <fcntl.h>
#	include <ifaddrs.h>
#	include <poll.h>
#	include <sys/time.h>
#endif

namespace net {

bool startup() {
#ifdef _WIN32
	WSADATA wsa;
	return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
	return true;
#endif
}

void close_socket(sock_t s) {
	if (s == BAD_SOCK) return;
#ifdef _WIN32
	closesocket(s);
#else
	close(s);
#endif
}

static std::string last_error() {
#ifdef _WIN32
	return "error " + std::to_string(WSAGetLastError());
#else
	return strerror(errno);
#endif
}

sock_t listen_tcp(int port, std::string& err) {
	sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == BAD_SOCK) { err = last_error(); return BAD_SOCK; }
#ifdef _WIN32
	BOOL excl = TRUE;  // don't let another program share the port
	setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&excl), sizeof(excl));
#else
	int one = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
	sockaddr_in a{};
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_ANY);
	a.sin_port = htons(static_cast<uint16_t>(port));
	if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || listen(s, 64) != 0) {
		err = last_error();
		close_socket(s);
		return BAD_SOCK;
	}
	return s;
}

sock_t bind_udp(int port, bool broadcast, std::string& err) {
	sock_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == BAD_SOCK) { err = last_error(); return BAD_SOCK; }
	int one = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
	if (broadcast) setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&one), sizeof(one));
	sockaddr_in a{};
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_ANY);
	a.sin_port = htons(static_cast<uint16_t>(port));
	if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
		err = last_error();
		close_socket(s);
		return BAD_SOCK;
	}
	return s;
}

bool send_all(sock_t s, const void* data, size_t size) {
	const char* p = static_cast<const char*>(data);
	while (size > 0) {
		int n = send(s, p, static_cast<int>(std::min<size_t>(size, 1 << 20)), 0);
		if (n <= 0) return false;
		p += n;
		size -= static_cast<size_t>(n);
	}
	return true;
}

std::string ip_to_string(const sockaddr_in& a) {
	char buf[INET_ADDRSTRLEN] = {};
	inet_ntop(AF_INET, &a.sin_addr, buf, sizeof(buf));
	return buf;
}

std::string peer_ip(sock_t s) {
	sockaddr_in a{};
	socklen_t len = sizeof(a);
	if (getpeername(s, reinterpret_cast<sockaddr*>(&a), &len) != 0) return "";
	return ip_to_string(a);
}

std::string local_ip_of(sock_t s) {
	sockaddr_in a{};
	socklen_t len = sizeof(a);
	if (getsockname(s, reinterpret_cast<sockaddr*>(&a), &len) != 0) return "";
	return ip_to_string(a);
}

std::string lan_ip() {
	sock_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == BAD_SOCK) return "127.0.0.1";
	sockaddr_in a{};
	a.sin_family = AF_INET;
	a.sin_port = htons(1);
	inet_pton(AF_INET, "10.255.255.255", &a.sin_addr);
	std::string ip = "127.0.0.1";
	if (connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0) {
		std::string got = local_ip_of(s);
		if (!got.empty() && got != "0.0.0.0") ip = got;
	}
	close_socket(s);
	return ip;
}

std::vector<std::string> local_ips() {
	std::vector<std::string> out;
#ifdef _WIN32
	ULONG size = 16 * 1024;
	std::vector<unsigned char> buf(size);
	auto* info = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
	if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
			nullptr, info, &size) == ERROR_BUFFER_OVERFLOW) {
		buf.resize(size);
		info = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
		if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
				nullptr, info, &size) != NO_ERROR) info = nullptr;
	}
	for (auto* a = info; a; a = a->Next) {
		if (a->OperStatus != IfOperStatusUp) continue;
		for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
			auto* sa = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
			std::string ip = ip_to_string(*sa);
			if (!is_loopback(ip)) out.push_back(ip);
		}
	}
#else
	ifaddrs* list = nullptr;
	if (getifaddrs(&list) == 0) {
		for (auto* a = list; a; a = a->ifa_next) {
			if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
			std::string ip = ip_to_string(*reinterpret_cast<sockaddr_in*>(a->ifa_addr));
			if (!is_loopback(ip)) out.push_back(ip);
		}
		freeifaddrs(list);
	}
#endif
	std::string lan = lan_ip();
	if (!is_loopback(lan) && std::find(out.begin(), out.end(), lan) == out.end()) out.insert(out.begin(), lan);
	return out;
}

bool is_loopback(const std::string& ip) {
	return ip.rfind("127.", 0) == 0 || ip == "::1" || ip == "localhost";
}

static std::string lower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

static void set_timeout(sock_t s, int ms) {
#ifdef _WIN32
	DWORD t = static_cast<DWORD>(ms);
#else
	timeval t{ms / 1000, (ms % 1000) * 1000};
#endif
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof(t));
	setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&t), sizeof(t));
}

bool read_request(sock_t s, HttpRequest& req) {
	set_timeout(s, 15000);
	std::string buf;
	char chunk[8192];
	size_t header_end = std::string::npos;
	while (header_end == std::string::npos) {
		int n = recv(s, chunk, sizeof(chunk), 0);
		if (n <= 0) return false;
		buf.append(chunk, static_cast<size_t>(n));
		header_end = buf.find("\r\n\r\n");
		if (buf.size() > 64 * 1024 && header_end == std::string::npos) return false;
	}
	std::string head = buf.substr(0, header_end);
	std::string rest = buf.substr(header_end + 4);
	size_t line_end = head.find("\r\n");
	std::string first = head.substr(0, line_end);
	size_t sp1 = first.find(' '), sp2 = first.rfind(' ');
	if (sp1 == std::string::npos || sp2 == sp1) return false;
	req.method = first.substr(0, sp1);
	std::string target = first.substr(sp1 + 1, sp2 - sp1 - 1);
	size_t q = target.find('?');
	req.path = target.substr(0, q);
	req.query = q == std::string::npos ? "" : target.substr(q + 1);
	size_t pos = line_end == std::string::npos ? head.size() : line_end + 2;
	while (pos < head.size()) {
		size_t e = head.find("\r\n", pos);
		if (e == std::string::npos) e = head.size();
		std::string line = head.substr(pos, e - pos);
		size_t c = line.find(':');
		if (c != std::string::npos) {
			std::string v = line.substr(c + 1);
			v.erase(0, v.find_first_not_of(" \t"));
			req.headers[lower(line.substr(0, c))] = v;
		}
		pos = e + 2;
	}
	size_t length = 0;
	if (auto it = req.headers.find("content-length"); it != req.headers.end())
		length = static_cast<size_t>(std::strtoull(it->second.c_str(), nullptr, 10));
	if (length > 16 * 1024 * 1024) return false;
	while (rest.size() < length) {
		int n = recv(s, chunk, sizeof(chunk), 0);
		if (n <= 0) return false;
		rest.append(chunk, static_cast<size_t>(n));
	}
	req.body = rest.substr(0, length);
	req.client_ip = peer_ip(s);
	return true;
}

static const char* reason(int status) {
	switch (status) {
	case 200: return "OK";
	case 201: return "Created";
	case 204: return "No Content";
	case 400: return "Bad Request";
	case 404: return "Not Found";
	case 500: return "Internal Server Error";
	case 502: return "Bad Gateway";
	default: return "Status";
	}
}

void write_response(sock_t s, const HttpResponse& res, bool head_only) {
	std::string out = "HTTP/1.1 " + std::to_string(res.status) + " " + reason(res.status) + "\r\n";
	out += "Server: BO1Live\r\nConnection: close\r\nContent-Type: " + res.content_type + "\r\n";
	out += "Content-Length: " + std::to_string(res.body.size()) + "\r\n\r\n";
	if (!head_only) out += res.body;
	send_all(s, out.data(), out.size());
}

static bool connect_timeout(sock_t s, const sockaddr_in& a, int timeout_ms) {
#ifdef _WIN32
	u_long nb = 1;
	ioctlsocket(s, FIONBIO, &nb);
#else
	int flags = fcntl(s, F_GETFL, 0);
	fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
	int r = connect(s, reinterpret_cast<const sockaddr*>(&a), sizeof(a));
	bool ok = r == 0;
	if (!ok) {
		fd_set w, e;
		FD_ZERO(&w); FD_ZERO(&e);
		FD_SET(s, &w); FD_SET(s, &e);
		timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
		if (select(static_cast<int>(s) + 1, nullptr, &w, &e, &tv) > 0 && FD_ISSET(s, &w)) {
			int err = 0;
			socklen_t len = sizeof(err);
			getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
			ok = err == 0;
		}
	}
#ifdef _WIN32
	nb = 0;
	ioctlsocket(s, FIONBIO, &nb);
#else
	fcntl(s, F_SETFL, flags);
#endif
	return ok;
}

bool http_call(const std::string& host, int port, const std::string& method, const std::string& path,
		const std::string& body, int timeout_ms, int& status, std::string& response) {
	addrinfo hints{}, *res = nullptr;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) return false;
	sockaddr_in a = *reinterpret_cast<sockaddr_in*>(res->ai_addr);
	freeaddrinfo(res);
	sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == BAD_SOCK) return false;
	if (!connect_timeout(s, a, timeout_ms)) { close_socket(s); return false; }
	set_timeout(s, timeout_ms);
	std::string req = method + " " + path + " HTTP/1.0\r\nHost: " + host + ":" + std::to_string(port) +
		"\r\nUser-Agent: BO1Live-peer\r\nContent-Type: application/json\r\nContent-Length: " +
		std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
	if (!send_all(s, req.data(), req.size())) { close_socket(s); return false; }
	std::string buf;
	char chunk[8192];
	for (;;) {
		int n = recv(s, chunk, sizeof(chunk), 0);
		if (n <= 0) break;
		buf.append(chunk, static_cast<size_t>(n));
	}
	close_socket(s);
	size_t he = buf.find("\r\n\r\n");
	if (buf.rfind("HTTP/", 0) != 0 || he == std::string::npos) return false;
	status = std::atoi(buf.c_str() + buf.find(' ') + 1);
	response = buf.substr(he + 4);
	return true;
}

}  // namespace net
