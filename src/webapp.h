// NATextGenerator - 本地 Web UI 服务
#ifndef NATG_WEBAPP_H
#define NATG_WEBAPP_H

#include <string>

namespace natg {

struct WebServerOptions {
	std::string host = "127.0.0.1";
	int port = 8765;
	bool openBrowser = true;
};

// 启动本地服务并进入请求循环(阻塞)
int runWebServer(const WebServerOptions &opt);

} // namespace natg

#endif // NATG_WEBAPP_H
