#include "webapp.h"

#include "generator.h"
#include "http_server.h"
#include "na_writer.h"
#include "platform.h"
#include "web_assets.h"

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace natg {
namespace {

const char *kVersion = "0.4.0";
const char *kDefaultFont = "SourceHanSansSC-Regular-2.otf";

// ---------------- JSON 输出 ----------------

void jsonEscape(std::string &out, const std::string &s) {
	out.push_back('"');
	for (unsigned char c : s) {
		switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (c < 0x20) {
					char buf[8];
					std::snprintf(buf, sizeof(buf), "\\u%04x", c);
					out += buf;
				} else {
					out.push_back(static_cast<char>(c));
				}
		}
	}
	out.push_back('"');
}

std::string num(double v, int digits = 5) {
	char buf[64];
	if (!std::isfinite(v)) return "0";
	std::snprintf(buf, sizeof(buf), "%.*f", digits, v);
	// 去掉多余的尾随 0
	std::string s(buf);
	if (s.find('.') != std::string::npos) {
		size_t last = s.find_last_not_of('0');
		if (s[last] == '.') last--;
		s.erase(last + 1);
	}
	if (s == "-0") s = "0";
	return s;
}

std::string jsonString(const std::string &s) {
	std::string out;
	jsonEscape(out, s);
	return out;
}

// ---------------- 参数解析 ----------------

std::string param(const HttpRequest &req, const std::string &key, const std::string &def) {
	auto it = req.params.find(key);
	if (it == req.params.end() || it->second.empty()) return def;
	return it->second;
}

double paramNum(const HttpRequest &req, const std::string &key, double def) {
	auto it = req.params.find(key);
	if (it == req.params.end() || it->second.empty()) return def;
	char *end = nullptr;
	const double v = std::strtod(it->second.c_str(), &end);
	if (end == it->second.c_str()) return def;
	return v;
}

bool paramBool(const HttpRequest &req, const std::string &key, bool def) {
	auto it = req.params.find(key);
	if (it == req.params.end()) return def;
	const std::string &v = it->second;
	if (v.empty() || v == "0" || v == "false" || v == "off") return false;
	return true;
}

// 输出文件名安全检查: 不允许目录穿越
bool safeOutputName(const std::string &name, std::string &out) {
	if (name.empty()) return false;
	if (name.find("..") != std::string::npos) return false;
	if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return false;
	if (name.find(':') != std::string::npos) return false;
	out = name;
	if (out.size() < 3 || out.compare(out.size() - 3, 3, ".na") != 0) out += ".na";
	return true;
}

std::string contentTypeForFont(const std::string &name) {
	auto dot = name.find_last_of('.');
	std::string ext = dot == std::string::npos ? std::string() : name.substr(dot);
	for (char &c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	if (ext == ".otf" || ext == ".otc") return "font/otf";
	if (ext == ".ttc") return "font/collection";
	return "font/ttf";
}

// ---------------- 各个接口 ----------------

bool serveAsset(HttpResponse &resp, const char *data, const char *contentType) {
	resp.status = 200;
	resp.contentType = contentType;
	resp.body = data;
	return true;
}

bool handleFonts(const HttpRequest &, HttpResponse &resp) {
	std::vector<std::string> fonts = listFonts();
	std::string out = "{\"ok\":true,\"fonts\":[";
	for (size_t i = 0; i < fonts.size(); i++) {
		if (i) out += ",";
		out += "{\"name\":" + jsonString(fonts[i]);
		out += ",\"path\":" + jsonString("fonts/" + fonts[i]);
		out += "}";
	}
	out += "],\"default\":" + jsonString(kDefaultFont) + "}";
	resp.contentType = "application/json; charset=utf-8";
	resp.body = out;
	return true;
}

bool handleFontFile(const HttpRequest &req, HttpResponse &resp) {
	const std::string name = param(req, "name", "");
	if (name.empty()) {
		resp.status = 400;
		resp.body = "{\"ok\":false,\"error\":\"缺少字体名\"}";
		resp.contentType = "application/json; charset=utf-8";
		return true;
	}
	const std::string path = resolveFontPath(name);
	std::vector<unsigned char> data;
	if (!readFileBinary(path, data) || data.empty()) {
		resp.status = 404;
		resp.contentType = "application/json; charset=utf-8";
		resp.body = "{\"ok\":false,\"error\":\"找不到字体\"}";
		return true;
	}
	resp.status = 200;
	resp.contentType = contentTypeForFont(path);
	resp.body.assign(reinterpret_cast<const char *>(data.data()), data.size());
	return true;
}

bool handleGenerate(const HttpRequest &req, HttpResponse &resp) {
	const auto t0 = std::chrono::steady_clock::now();

	static FontCache cache;

	const std::string fontName = param(req, "font", kDefaultFont);
	const std::string text = param(req, "text", "");
	GenerateRequest greq;
	greq.fontPath = resolveFontPath(fontName);
	greq.text = text;
	greq.sizeMeters = paramNum(req, "size", 10.0);
	greq.kerning = paramBool(req, "kerning", true);
	greq.maxParts = static_cast<size_t>(paramNum(req, "maxParts", 200000.0));
	const int quality = static_cast<int>(paramNum(req, "quality", 5.0));
	const double tolParam = paramNum(req, "tolerance", 0.0);
	greq.toleranceEm = tolParam > 0.0 ? tolParam : qualityToTolerance(quality);
	const double thickness = paramNum(req, "thickness", 0.2);
	const bool doWrite = paramBool(req, "write", true);
	std::string outName = param(req, "out", "output.na");
	greq.mergeQuads = paramBool(req, "mergeQuads", true);
	greq.flipEdges = paramBool(req, "flipEdges", true);
	greq.mergeMinAngleDeg = paramNum(req, "mergeMinAngle", 20.0);
	greq.simplifyEps = paramNum(req, "simplify", -1.0);
	greq.placementMode = param(req, "placement", "plane") == "surface" ? PlacementMode::Surface
	                     : PlacementMode::Plane;
	greq.surfaceCp = param(req, "cp", "");
	greq.surfaceUnit = param(req, "surfaceUnit", "em") == "m" ? SurfaceUnit::Meter : SurfaceUnit::Em;
	greq.flipNormal = paramBool(req, "flipNormal", false);
	greq.thickness = thickness;

	std::string body;
	body.reserve(4096);

	auto fail = [&](const std::string &msg) {
		body = "{\"ok\":false,\"error\":" + jsonString(msg) + "}";
		resp.contentType = "application/json; charset=utf-8";
		resp.body = body;
		return true;
	};

	if (text.empty()) return fail("文本为空");
	if (!(greq.sizeMeters > 0.0)) return fail("字号必须大于 0");
	if (!(thickness > 0.0)) return fail("厚度必须大于 0");
	if (!(greq.toleranceEm > 0.0)) return fail("容差必须大于 0");

	GenerateResult res;
	if (!generatePolygons(greq, res, cache)) return fail(res.error);

	std::string safeName;
	WriterStats wst;
	std::string writeErr;
	bool written = false;
	if (doWrite) {
		if (!safeOutputName(outName, safeName)) return fail("输出文件名不合法");
		PartSettings ps;
		written = writeNaFile(safeName, text, res.parts, res.placement, ps, &wst, writeErr);
		if (!written) return fail(writeErr.empty() ? "写入失败" : writeErr);
	} else {
		wst.parts = res.parts.size();
	}

	const auto t1 = std::chrono::steady_clock::now();
	const double totalMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

	body += "{\"ok\":true";
	body += ",\"version\":" + jsonString(kVersion);
	body += ",\"font\":" + jsonString(greq.fontPath);
	{
		FontFace *face = nullptr;
		std::string famErr;
		if (cache.acquire(greq.fontPath, &face, famErr) && face) {
			body += ",\"family\":" + jsonString(face->familyName());
			body += ",\"style\":" + jsonString(face->styleName());
		} else {
			body += ",\"family\":\"\",\"style\":\"\"";
		}
	}
	body += ",\"text\":" + jsonString(text);
	body += ",\"size\":" + num(greq.sizeMeters);
	body += ",\"tolerance\":" + num(greq.toleranceEm, 6);
	body += ",\"thickness\":" + num(thickness);
	body += ",\"bounds\":{\"minX\":" + num(res.minX) + ",\"maxX\":" + num(res.maxX) +
	        ",\"minY\":" + num(res.minY) + ",\"maxY\":" + num(res.maxY) + "}";
	body += ",\"stats\":{\"codepoints\":" + std::to_string(res.stats.codepoints) +
	        ",\"glyphs\":" + std::to_string(res.stats.glyphsWithOutline) +
	        ",\"blanks\":" + std::to_string(res.stats.blanks) +
	        ",\"rings\":" + std::to_string(res.stats.rings) +
	        ",\"regions\":" + std::to_string(res.stats.regions) +
	        ",\"holes\":" + std::to_string(res.stats.holes) +
	        ",\"holesDropped\":" + std::to_string(res.stats.holesDropped) +
	        ",\"degenerate\":" + std::to_string(res.stats.degenerate) +
	        ",\"trianglesIn\":" + std::to_string(res.stats.triangles) +
	        ",\"parts\":" + std::to_string(res.stats.parts) +
	        ",\"quads\":" + std::to_string(res.stats.quads) +
	        ",\"flips\":" + std::to_string(res.stats.flips) +
	        ",\"sliversIn\":" + std::to_string(res.stats.sliversIn) +
	        ",\"sliversOut\":" + std::to_string(res.stats.sliversOut) +
	        ",\"bigThinIn\":" + std::to_string(res.stats.bigThinIn) +
	        ",\"bigThinOut\":" + std::to_string(res.stats.bigThinOut) +
	        ",\"bigThinShareIn\":" + num(res.stats.bigThinShareIn, 2) +
	        ",\"bigThinShareOut\":" + num(res.stats.bigThinShareOut, 2) +
	        ",\"minAngleIn\":" + num(res.stats.minAngleIn, 2) +
	        ",\"minAngleOut\":" + num(res.stats.minAngleOut, 2) +
	        ",\"geometryMs\":" + num(res.stats.ms, 1) + "}";

	// 零件多边形(em 单位，逆时针)，格式: [顶点数, x0,y0, x1,y1, ...] 重复
	body += ",\"polygons\":[";
	body.reserve(body.size() + res.parts.size() * 80 + 4096);
	bool firstPoly = true;
	for (const Polygon &poly : res.parts) {
		if (!firstPoly) body.push_back(',');
		firstPoly = false;
		body += std::to_string(poly.pts.size());
		for (const Vec2 &v : poly.pts) {
			body.push_back(',');
			body += num(v.x);
			body.push_back(',');
			body += num(v.y);
		}
	}
	body += "]";

	// 每个字符的落笔位置(em)与码点，格式: [x0, cp0, x1, cp1, ...]
	// 前端据此"逐字"绘制参考字，避免浏览器自带排版(含 GPOS 字距)与生成结果错位
	body += ",\"layout\":[";
	for (size_t i = 0; i < res.glyphPenX.size() && i < res.glyphCodepoints.size(); i++) {
		if (i) body.push_back(',');
		body += num(res.glyphPenX[i], 5);
		body.push_back(',');
		body += std::to_string(res.glyphCodepoints[i]);
	}
	body += "]";

	body += ",\"truncated\":";
	body += res.truncated ? "true" : "false";

	// 放置方式: 平面 / 曲面
	const bool surface = res.placement.mode == PlacementMode::Surface;
	body += ",\"placement\":";
	body += surface ? "\"surface\"" : "\"plane\"";
	if (surface) {
		body += ",\"surface\":{\"cp\":[";
		for (int j = 0; j < 4; j++) {
			for (int i = 0; i < 4; i++) {
				if (j || i) body.push_back(',');
				body += num(res.placement.patch.cp[j][i].x);
				body.push_back(',');
				body += num(res.placement.patch.cp[j][i].y);
				body.push_back(',');
				body += num(res.placement.patch.cp[j][i].z);
			}
		}
		body += "],\"grid\":[";
		const int kGrid = 10;   // 10x10 网格用于前端画曲面
		for (int gj = 0; gj < kGrid; gj++) {
			for (int gi = 0; gi < kGrid; gi++) {
				const double u = static_cast<double>(gi) / (kGrid - 1);
				const double v = static_cast<double>(gj) / (kGrid - 1);
				const Vec3 p = res.placement.patch.eval(u, v);
				if (gj || gi) body.push_back(',');
				body += num(p.x);
				body.push_back(',');
				body += num(p.y);
				body.push_back(',');
				body += num(p.z);
			}
		}
		body += "],\"gridSize\":";
		body += std::to_string(kGrid);
		body += ",\"flipNormal\":";
		body += res.placement.flipNormal ? "true" : "false";
		body += "}";
	}

	// 三维棱柱(前脸 + 后脸)，米为单位，供 3D 预览直接绘制
	{
		const size_t kSolidCap = 20000;
		body += ",\"solid\":[";
		const size_t n = res.parts.size() < kSolidCap ? res.parts.size() : kSolidCap;
		bool firstPart = true;
		for (size_t i = 0; i < n; i++) {
			const Polygon &poly = res.parts[i];
			Vec3 front[4], back[4];
			partCorners(res.placement, poly, front, back);
			const size_t m = poly.pts.size();
			if (!firstPart) body.push_back(',');
			firstPart = false;
			body += std::to_string(m);
			for (size_t k = 0; k < m; k++) {
				body.push_back(',');
				body += num(front[k].x);
				body.push_back(',');
				body += num(front[k].y);
				body.push_back(',');
				body += num(front[k].z);
			}
			for (size_t k = 0; k < m; k++) {
				body.push_back(',');
				body += num(back[k].x);
				body.push_back(',');
				body += num(back[k].y);
				body.push_back(',');
				body += num(back[k].z);
			}
		}
		body += "],\"solidTruncated\":";
		body += (res.parts.size() > kSolidCap) ? "true" : "false";
	}
	body += ",\"written\":";
	body += written ? jsonString(safeName) : "null";
	body += ",\"parts\":" + std::to_string(wst.parts);
	body += ",\"requestMs\":" + num(totalMs, 1);

	body += ",\"warnings\":[";
	bool firstWarn = true;
	for (uint32_t cp : res.stats.missing) {
		char buf[96];
		std::snprintf(buf, sizeof(buf), "字体不包含字符 U+%04X，已跳过", cp);
		if (!firstWarn) body.push_back(',');
		firstWarn = false;
		body += jsonString(buf);
	}
	if (res.truncated) {
		if (!firstWarn) body.push_back(',');
		firstWarn = false;
		body += jsonString("零件数超过上限，已截断(可降低质量或加大容差)");
	}
	if (res.stats.holesDropped > 0) {
		if (!firstWarn) body.push_back(',');
		firstWarn = false;
		body += jsonString("有孔洞因退化被丢弃，可能被填充");
	}
	body += "]}";

	resp.contentType = "application/json; charset=utf-8";
	resp.body = body;
	return true;
}

bool handleDownload(const HttpRequest &req, HttpResponse &resp) {
	std::string name;
	if (!safeOutputName(param(req, "f", "output.na"), name)) {
		resp.status = 400;
		resp.body = "bad name";
		return true;
	}
	std::string data;
	if (!readFileUtf8(name, data)) {
		resp.status = 404;
		resp.body = "not found: " + name;
		return true;
	}
	resp.status = 200;
	resp.contentType = "application/xml; charset=utf-8";
	resp.extraHeaders = "Content-Disposition: attachment; filename=\"" + name + "\"\r\n";
	resp.body = data;
	return true;
}

bool handleRequest(const HttpRequest &req, HttpResponse &resp) {
	const std::string &path = req.path;

	if (req.method == "GET" || req.method == "HEAD") {
		if (path == "/" || path == "/index.html") {
			return serveAsset(resp, web::kIndexHtml, "text/html; charset=utf-8");
		}
		if (path == "/app.js") return serveAsset(resp, web::kAppJs, "application/javascript; charset=utf-8");
		if (path == "/style.css") return serveAsset(resp, web::kStyleCss, "text/css; charset=utf-8");
		if (path == "/favicon.ico") {
			resp.status = 204;
			resp.body.clear();
			return true;
		}
		if (path == "/api/fonts") return handleFonts(req, resp);
		if (path == "/api/font") return handleFontFile(req, resp);
		if (path == "/api/download") return handleDownload(req, resp);
		if (path == "/api/ping") {
			resp.contentType = "application/json; charset=utf-8";
			resp.body = std::string("{\"ok\":true,\"version\":") + jsonString(kVersion) + "}";
			return true;
		}
	}

	if (req.method == "POST" && path == "/api/generate") {
		return handleGenerate(req, resp);
	}

	resp.status = 404;
	resp.contentType = "text/plain; charset=utf-8";
	resp.body = "404 Not Found: " + path;
	return true;
}

} // namespace

int runWebServer(const WebServerOptions &opt) {
	HttpServer server;
	std::string err;
	int port = opt.port;
	if (!server.start(opt.host, port, err)) {
		// 端口被占用时自动向后找一个
		bool ok = false;
		for (int p = opt.port + 1; p < opt.port + 20; p++) {
			if (server.start(opt.host, p, err)) {
				ok = true;
				break;
			}
		}
		if (!ok) {
			std::printf("错误: 无法启动本地服务: %s\n", err.c_str());
			return 1;
		}
	}

	char url[128];
	std::snprintf(url, sizeof(url), "http://%s:%d/", opt.host.c_str(), server.boundPort());
	std::printf("NATextGenerator v%s\n", kVersion);
	std::printf("本地界面已启动: %s\n", url);
	std::printf("(关闭本窗口或按 Ctrl+C 结束程序)\n\n");
	std::fflush(stdout);
	if (opt.openBrowser) openInBrowser(url);

	server.serveForever(&handleRequest);
	return 0;
}

} // namespace natg
