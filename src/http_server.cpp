#include "http_server.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace natg {
namespace {

#ifdef _WIN32
using socket_t = SOCKET;
const socket_t kInvalidSocket = INVALID_SOCKET;
void closeSocket(socket_t s) { closesocket(s); }
bool initSockets(std::string &err) {
	static bool done = false;
	if (done) return true;
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		err = "WSAStartup 失败";
		return false;
	}
	done = true;
	return true;
}
#else
using socket_t = int;
const socket_t kInvalidSocket = -1;
void closeSocket(socket_t s) { ::close(s); }
bool initSockets(std::string &) { return true; }
#endif

std::string toLower(const std::string &s) {
	std::string out = s;
	for (char &c : out) {
		if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
	}
	return out;
}

std::string trim(const std::string &s) {
	size_t b = s.find_first_not_of(" \t\r\n");
	if (b == std::string::npos) return std::string();
	size_t e = s.find_last_not_of(" \t\r\n");
	return s.substr(b, e - b + 1);
}

void parseParams(const std::string &raw, std::map<std::string, std::string> &out) {
	size_t pos = 0;
	while (pos < raw.size()) {
		size_t amp = raw.find('&', pos);
		if (amp == std::string::npos) amp = raw.size();
		std::string pair = raw.substr(pos, amp - pos);
		if (!pair.empty()) {
			size_t eq = pair.find('=');
			if (eq == std::string::npos) {
				out[urlDecode(pair)] = std::string();
			} else {
				out[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
			}
		}
		pos = amp + 1;
	}
}

bool sendAll(socket_t s, const char *data, size_t len) {
	size_t sent = 0;
	while (sent < len) {
		int n = static_cast<int>(::send(s, data + sent, static_cast<int>(len - sent), 0));
		if (n <= 0) return false;
		sent += static_cast<size_t>(n);
	}
	return true;
}

} // namespace

const char *httpStatusText(int code) {
	switch (code) {
		case 200: return "OK";
		case 204: return "No Content";
		case 400: return "Bad Request";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 413: return "Payload Too Large";
		case 500: return "Internal Server Error";
		default: return "OK";
	}
}

std::string urlDecode(const std::string &s) {
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); i++) {
		const char c = s[i];
		if (c == '+') {
			out.push_back(' ');
		} else if (c == '%' && i + 2 < s.size()) {
			auto hex = [](char h) -> int {
				if (h >= '0' && h <= '9') return h - '0';
				if (h >= 'a' && h <= 'f') return h - 'a' + 10;
				if (h >= 'A' && h <= 'F') return h - 'A' + 10;
				return -1;
			};
			const int hi = hex(s[i + 1]);
			const int lo = hex(s[i + 2]);
			if (hi >= 0 && lo >= 0) {
				out.push_back(static_cast<char>(hi * 16 + lo));
				i += 2;
			} else {
				out.push_back(c);
			}
		} else {
			out.push_back(c);
		}
	}
	return out;
}

HttpServer::~HttpServer() {
	if (listenSock_ != ~0ull) {
		closeSocket(static_cast<socket_t>(listenSock_));
		listenSock_ = ~0ull;
	}
}

bool HttpServer::start(const std::string &host, int port, std::string &err) {
	if (!initSockets(err)) return false;

	socket_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == kInvalidSocket) {
		err = "创建 socket 失败";
		return false;
	}
	int yes = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof(yes));

	sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(static_cast<unsigned short>(port));
	// 只监听本机回环地址，避免把界面暴露到局域网
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (!host.empty() && host != "127.0.0.1") {
		const unsigned long ip = inet_addr(host.c_str());
		if (ip != INADDR_NONE) addr.sin_addr.s_addr = ip;
	}

	if (bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
		err = "绑定端口失败(端口可能已被占用)";
		closeSocket(s);
		return false;
	}
	if (listen(s, 32) != 0) {
		err = "监听失败";
		closeSocket(s);
		return false;
	}

	// 查询实际端口(port 传 0 时由系统分配)
	sockaddr_in bound;
#ifdef _WIN32
	int len = sizeof(bound);
#else
	socklen_t len = sizeof(bound);
#endif
	if (getsockname(s, reinterpret_cast<sockaddr *>(&bound), &len) == 0) {
		port_ = ntohs(bound.sin_port);
	} else {
		port_ = port;
	}
	listenSock_ = static_cast<unsigned long long>(s);
	return true;
}

bool HttpServer::handleOne(const HttpHandler &handler) {
	socket_t client = ::accept(static_cast<socket_t>(listenSock_), nullptr, nullptr);
	if (client == kInvalidSocket) return false;

	std::string raw;
	char buf[8192];
	size_t headerEnd = std::string::npos;

	// 读请求头
	while (headerEnd == std::string::npos && raw.size() < 64 * 1024) {
		int n = ::recv(client, buf, sizeof(buf), 0);
		if (n <= 0) break;
		raw.append(buf, static_cast<size_t>(n));
		headerEnd = raw.find("\r\n\r\n");
	}
	if (headerEnd == std::string::npos) {
		closeSocket(client);
		return true;
	}

	HttpRequest req;
	std::istringstream hs(raw.substr(0, headerEnd));
	std::string line;
	std::getline(hs, line);   // 请求行
	{
		std::istringstream ls(line);
		std::string version;
		ls >> req.method >> req.target >> version;
	}
	req.path = req.target;
	const size_t qpos = req.target.find('?');
	if (qpos != std::string::npos) {
		req.path = req.target.substr(0, qpos);
		req.query = req.target.substr(qpos + 1);
		parseParams(req.query, req.params);
	}
	while (std::getline(hs, line)) {
		const size_t colon = line.find(':');
		if (colon == std::string::npos) continue;
		req.headers[toLower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
	}

	// 读请求体
	size_t contentLength = 0;
	auto it = req.headers.find("content-length");
	if (it != req.headers.end()) contentLength = static_cast<size_t>(std::strtoul(it->second.c_str(), nullptr, 10));
	if (contentLength > 8 * 1024 * 1024) {
		HttpResponse resp;
		resp.status = 413;
		resp.body = "request too large";
		std::ostringstream out;
		out << "HTTP/1.1 413 Payload Too Large\r\nContent-Length: " << resp.body.size()
		    << "\r\nConnection: close\r\n\r\n"
		    << resp.body;
		sendAll(client, out.str().data(), out.str().size());
		closeSocket(client);
		return true;
	}
	const size_t bodyStart = headerEnd + 4;
	if (raw.size() > bodyStart) req.body = raw.substr(bodyStart);
	while (req.body.size() < contentLength) {
		int n = ::recv(client, buf, sizeof(buf), 0);
		if (n <= 0) break;
		req.body.append(buf, static_cast<size_t>(n));
	}
	if (req.body.size() > contentLength) req.body.resize(contentLength);

	// 表单参数
	auto ct = req.headers.find("content-type");
	if (ct != req.headers.end() &&
	        ct->second.find("application/x-www-form-urlencoded") != std::string::npos) {
		parseParams(req.body, req.params);
	}

	HttpResponse resp;
	if (!handler || !handler(req, resp)) {
		resp.status = 404;
		resp.contentType = "text/plain; charset=utf-8";
		resp.body = "404 Not Found";
	}

	std::ostringstream out;
	out << "HTTP/1.1 " << resp.status << ' ' << httpStatusText(resp.status) << "\r\n"
	    << "Content-Type: " << resp.contentType << "\r\n"
	    << "Content-Length: " << resp.body.size() << "\r\n"
	    << "Cache-Control: no-store\r\n"
	    << "Connection: close\r\n"
	    << resp.extraHeaders
	    << "\r\n";
	const std::string head = out.str();
	sendAll(client, head.data(), head.size());
	if (!resp.body.empty()) sendAll(client, resp.body.data(), resp.body.size());

#ifdef _WIN32
	shutdown(client, SD_SEND);
#endif
	closeSocket(client);
	return true;
}

void HttpServer::serveForever(const HttpHandler &handler) {
	for (;;) {
		handleOne(handler);
	}
}

} // namespace natg
