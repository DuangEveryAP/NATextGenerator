// NATextGenerator - 极简本地 HTTP 服务(仅监听 127.0.0.1)
// 只实现浏览器做本地 UI 所需要的那部分 HTTP/1.1:
// 每个连接读一个请求，回一个响应，然后关闭连接(Connection: close)。
#ifndef NATG_HTTP_SERVER_H
#define NATG_HTTP_SERVER_H

#include <functional>
#include <map>
#include <string>

namespace natg {

struct HttpRequest {
	std::string method;
	std::string target;   // 原始请求目标(含查询串)
	std::string path;     // 解码后的路径
	std::string query;    // 查询串(原始)
	std::string body;
	std::map<std::string, std::string> headers;   // 键已转小写
	std::map<std::string, std::string> params;    // 查询串 + 表单，值已 URL 解码
};

struct HttpResponse {
	int status = 200;
	std::string contentType = "text/plain; charset=utf-8";
	std::string body;
	std::string extraHeaders;   // 额外响应头(每行 "Key: Value\r\n")
};

using HttpHandler = std::function<bool(const HttpRequest &, HttpResponse &)>;

class HttpServer {
public:
	~HttpServer();

	// 绑定并监听；port 传 0 表示由系统分配
	bool start(const std::string &host, int port, std::string &err);
	int boundPort() const { return port_; }

	// 循环处理请求，直到进程退出
	void serveForever(const HttpHandler &handler);

private:
	bool handleOne(const HttpHandler &handler);
	unsigned long long listenSock_ = ~0ull;
	int port_ = 0;
};

const char *httpStatusText(int code);
std::string urlDecode(const std::string &s);

} // namespace natg

#endif // NATG_HTTP_SERVER_H
