// NATextGenerator - 内嵌的 Web 前端资源(由 tools/embed_web.py 生成 web_assets.cpp)
// 把 index.html / app.js / style.css 编进 exe，避免分发时漏文件。
#ifndef NATG_WEB_ASSETS_H
#define NATG_WEB_ASSETS_H

namespace natg {
namespace web {

extern const char *kIndexHtml;
extern const char *kAppJs;
extern const char *kStyleCss;

} // namespace web
} // namespace natg

#endif // NATG_WEB_ASSETS_H
