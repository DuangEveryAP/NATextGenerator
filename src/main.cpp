// NATextGenerator v0.4.0
//
// 处理链路:
//   字体文件 -> FreeType 矢量轮廓(二次/三次贝塞尔 + 直线)
//            -> 自适应扁平化为折线环 -> 嵌套分析(外轮廓 / 内部孔洞)
//            -> earcut 三角化(孔洞桥接 + 耳切，处理凹陷)
//            -> 每个三角面写成一个 NavalArt 零件(退化四边形三角棱柱)
//
// 两种使用方式:
//   * 无参数启动: 起一个本地网页界面(带 2D 叠图与 3D 预览)
//   * 带参数启动: 命令行批量生成

#include "generator.h"
#include "na_writer.h"
#include "platform.h"
#include "webapp.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using natg::GenerateRequest;
using natg::GenerateResult;
using natg::PartSettings;
using natg::WriterStats;

namespace {

const char *kVersion = "0.4.0";
const char *kDefaultFont = "SourceHanSansSC-Regular-2.otf";
const double kDefaultSize = 10.0;
const int kDefaultQuality = 5;

struct Options {
	std::string font;
	std::string text;
	std::string textFile;
	std::string output = "output.na";
	double size = kDefaultSize;
	bool sizeSet = false;
	int quality = kDefaultQuality;
	bool qualitySet = false;
	double tolerance = 0.0;
	bool toleranceSet = false;
	double thickness = 0.2;
	bool kerning = true;
	bool loop = false;
	size_t maxParts = 200000;
	bool serve = false;
	int port = 8765;
	bool noOpen = false;
	bool hasWork = false;   // 是否给出了生成相关的参数(否则默认进网页界面)

	// 网格优化
	bool mergeQuads = true;         // 三角面 -> 四边形
	bool flipEdges = true;          // 保角翻转
	double mergeMinAngle = 20.0;    // 合并成四边形的最小角门限
	double simplifyEps = -1.0;      // 边界简化阈值(em)，<0 = 自动

	// 放置方式
	std::string surfaceCp;                                    // 48 个数 => 贝塞尔曲面
	natg::SurfaceUnit surfaceUnit = natg::SurfaceUnit::Em;    // 归一化 / 实测
	bool flipNormal = false;
};

void printUsage() {
	std::printf(
	    "NATextGenerator v%s - 由文字生成 NavalArt 存档\n"
	    "\n"
	    "用法:\n"
	    "  NATextGenerator                       打开本地网页界面(推荐)\n"
	    "  NATextGenerator --serve               同上(显式指定)\n"
	    "  NATextGenerator [选项]                命令行直接生成\n"
	    "\n"
	    "选项:\n"
	    "  -f, --font <名称>       字体文件名(默认在 fonts/ 下查找)或完整路径\n"
	    "  -t, --text <文本>       要生成的文本\n"
	    "      --text-file <文件>  从 UTF-8 文本文件读取内容\n"
	    "  -s, --size <米>         字号大小(em 高度，默认 %g)\n"
	    "  -q, --quality <1-10>    曲线细分质量(默认 %d)\n"
	    "  -e, --tolerance <em>    直接指定曲线容差(覆盖 -q)\n"
	    "  -o, --output <文件>     输出文件(默认 output.na)\n"
	    "  -w, --thickness <米>    总厚度(默认 0.2，即 x = ±0.1)\n"
	    "      --no-kerning        不使用字距调整\n"
	    "      --no-quad           不把三角面合并成四边形\n"
	    "      --no-flip           不做保角翻转(可能残留更多细长三角面)\n"
	    "      --merge-angle <度>  合并成四边形的最小角门限(默认 20)\n"
	    "      --simplify <em>     边界简化阈值(默认 0.5×曲线容差)\n"
	    "      --surface <控制点>  贴到贝塞尔曲面上: 4 行 x 4 点，共 48 个数\n"
	    "                          (每点 x,y,z；行 = v 方向，列 = u 方向)\n"
	    "                          ⚠ 坐标轴: x = 厚度方向, y = 向上, z = 文字方向\n"
	    "      --surface-unit <em|m>  控制点单位: em = 字高倍数(默认), m = 米\n"
	    "      --flip-normal       翻转厚度方向\n"
	    "      --max-parts <数量>  零件数上限(默认 200000)\n"
	    "      --loop              生成后继续输入下一段文本\n"
	    "      --serve             启动本地网页界面\n"
	    "      --port <端口>       网页界面端口(默认 8765)\n"
	    "      --no-open           不自动打开浏览器\n"
	    "  -h, --help              显示本帮助\n",
	    kVersion, kDefaultSize, kDefaultQuality);
}

bool parseArgs(const std::vector<std::string> &args, Options &opt, std::string &err) {
	for (size_t i = 1; i < args.size(); i++) {
		const std::string &a = args[i];
		auto needValue = [&](const char *name) -> std::string {
			if (i + 1 >= args.size()) {
				err = std::string("选项 ") + name + " 缺少参数";
				return std::string();
			}
			return args[++i];
		};
		if (a == "-h" || a == "--help") {
			printUsage();
			std::exit(0);
		} else if (a == "-f" || a == "--font") {
			std::string v = needValue("--font");
			if (v.empty()) return false;
			opt.font = v;
			opt.hasWork = true;
		} else if (a == "-t" || a == "--text") {
			std::string v = needValue("--text");
			if (v.empty()) return false;
			opt.text = v;
			opt.hasWork = true;
		} else if (a == "--text-file") {
			std::string v = needValue("--text-file");
			if (v.empty()) return false;
			opt.textFile = v;
			opt.hasWork = true;
		} else if (a == "-s" || a == "--size") {
			std::string v = needValue("--size");
			if (v.empty()) return false;
			opt.size = std::atof(v.c_str());
			opt.sizeSet = true;
			opt.hasWork = true;
			if (!(opt.size > 0.0)) {
				err = "字号必须大于 0";
				return false;
			}
		} else if (a == "-q" || a == "--quality") {
			std::string v = needValue("--quality");
			if (v.empty()) return false;
			opt.quality = std::atoi(v.c_str());
			opt.qualitySet = true;
			opt.hasWork = true;
		} else if (a == "-e" || a == "--tolerance") {
			std::string v = needValue("--tolerance");
			if (v.empty()) return false;
			opt.tolerance = std::atof(v.c_str());
			opt.toleranceSet = true;
			opt.hasWork = true;
			if (!(opt.tolerance > 0.0)) {
				err = "容差必须大于 0";
				return false;
			}
		} else if (a == "-o" || a == "--output") {
			std::string v = needValue("--output");
			if (v.empty()) return false;
			opt.output = v;
			opt.hasWork = true;
		} else if (a == "-w" || a == "--thickness") {
			std::string v = needValue("--thickness");
			if (v.empty()) return false;
			opt.thickness = std::atof(v.c_str());
			opt.hasWork = true;
			if (!(opt.thickness > 0.0)) {
				err = "厚度必须大于 0";
				return false;
			}
		} else if (a == "--no-kerning") {
			opt.kerning = false;
			opt.hasWork = true;
		} else if (a == "--no-quad") {
			opt.mergeQuads = false;
			opt.hasWork = true;
		} else if (a == "--no-flip") {
			opt.flipEdges = false;
			opt.hasWork = true;
		} else if (a == "--merge-angle") {
			std::string v = needValue("--merge-angle");
			if (v.empty()) return false;
			opt.mergeMinAngle = std::atof(v.c_str());
			opt.hasWork = true;
		} else if (a == "--simplify") {
			std::string v = needValue("--simplify");
			if (v.empty()) return false;
			opt.simplifyEps = std::atof(v.c_str());
			opt.hasWork = true;
		} else if (a == "--surface") {
			std::string v = needValue("--surface");
			if (v.empty()) return false;
			opt.surfaceCp = v;
			opt.hasWork = true;
		} else if (a == "--surface-unit") {
			std::string v = needValue("--surface-unit");
			if (v.empty()) return false;
			opt.surfaceUnit = (v == "m" || v == "meter" || v == "meters")
			                  ? natg::SurfaceUnit::Meter : natg::SurfaceUnit::Em;
			opt.hasWork = true;
		} else if (a == "--flip-normal") {
			opt.flipNormal = true;
			opt.hasWork = true;
		} else if (a == "--max-parts") {
			std::string v = needValue("--max-parts");
			if (v.empty()) return false;
			long long n = std::atoll(v.c_str());
			if (n <= 0) {
				err = "零件上限必须大于 0";
				return false;
			}
			opt.maxParts = static_cast<size_t>(n);
			opt.hasWork = true;
		} else if (a == "--loop") {
			opt.loop = true;
			opt.hasWork = true;
		} else if (a == "--serve") {
			opt.serve = true;
		} else if (a == "--port") {
			std::string v = needValue("--port");
			if (v.empty()) return false;
			opt.port = std::atoi(v.c_str());
			if (opt.port <= 0 || opt.port > 65535) {
				err = "端口不合法";
				return false;
			}
		} else if (a == "--no-open") {
			opt.noOpen = true;
		} else {
			err = "无法识别的参数: " + a;
			printUsage();
			return false;
		}
	}
	return true;
}

// ---------------- 命令行生成 ----------------

bool runGenerate(const std::string &fontBanner, const Options &opt, natg::FontCache &cache) {
	std::string text = opt.text;
	if (!opt.textFile.empty() && !natg::readFileUtf8(opt.textFile, text)) {
		std::printf("错误: 无法读取文本文件 %s\n", opt.textFile.c_str());
		return false;
	}
	// 文本文件末尾的换行不应变成多余空格
	while (!text.empty() && (text.back() == '\n' || text.back() == '\r' ||
	                         text.back() == ' ' || text.back() == '\t')) {
		text.pop_back();
	}
	if (text.empty()) {
		std::printf("错误: 文本为空\n");
		return false;
	}

	GenerateRequest req;
	req.fontPath = natg::resolveFontPath(opt.font);
	req.text = text;
	req.sizeMeters = opt.size;
	req.toleranceEm = opt.toleranceSet ? opt.tolerance : natg::qualityToTolerance(opt.quality);
	req.kerning = opt.kerning;
	req.maxParts = opt.maxParts;
	req.mergeQuads = opt.mergeQuads;
	req.flipEdges = opt.flipEdges;
	req.mergeMinAngleDeg = opt.mergeMinAngle;
	req.simplifyEps = opt.simplifyEps;
	req.placementMode = opt.surfaceCp.empty() ? natg::PlacementMode::Plane : natg::PlacementMode::Surface;
	req.surfaceCp = opt.surfaceCp;
	req.surfaceUnit = opt.surfaceUnit;
	req.flipNormal = opt.flipNormal;
	req.thickness = opt.thickness;

	if (!fontBanner.empty()) std::printf("%s\n", fontBanner.c_str());

	GenerateResult res;
	if (!natg::generatePolygons(req, res, cache)) {
		std::printf("错误: %s\n", res.error.c_str());
		return false;
	}
	for (uint32_t cp : res.stats.missing) {
		std::printf("警告: 字体不含字符 U+%04X，已跳过\n", cp);
	}
	if (res.truncated) {
		std::printf("警告: 零件数超过上限 %zu，已截断\n", opt.maxParts);
	}

	PartSettings ps;
	WriterStats wst;
	std::string err;
	if (!natg::writeNaFile(opt.output, text, res.parts, res.placement, ps, &wst, err)) {
		std::printf("错误: %s\n", err.c_str());
		return false;
	}

	std::printf("字号 %g m，曲线容差 %g em，厚度 %g m，放置 %s\n", opt.size, req.toleranceEm,
	            opt.thickness, res.placement.mode == natg::PlacementMode::Surface ? "贝塞尔曲面" : "平面");
	std::printf("字符 %zu 个(有轮廓 %zu，空白 %zu)，轮廓环 %zu 个，孔洞 %zu 个\n",
	            res.stats.codepoints, res.stats.glyphsWithOutline, res.stats.blanks,
	            res.stats.rings, res.stats.holes);
	std::printf("三角面 %zu 个 -> 零件 %zu 个(四边形 %zu，三角 %zu)，翻转 %zu 次\n",
	            res.stats.triangles, wst.parts, res.stats.quads, wst.parts - res.stats.quads,
	            res.stats.flips);
	std::printf("细长面(<5°) %zu -> %zu，大细长面 %zu -> %zu(面积占比 %.2f%% -> %.2f%%)\n",
	            res.stats.sliversIn, res.stats.sliversOut, res.stats.bigThinIn, res.stats.bigThinOut,
	            res.stats.bigThinShareIn, res.stats.bigThinShareOut);
	std::printf("最小角 %.2f° -> %.2f°，几何用时 %.1f ms -> %s\n",
	            res.stats.minAngleIn, res.stats.minAngleOut, res.stats.ms, opt.output.c_str());
	return wst.parts > 0;
}

// ---------------- 交互式(命令行) ----------------

bool promptLine(const char *prompt, std::string &out) {
	std::fputs(prompt, stdout);
	std::fflush(stdout);
	out.clear();
	int ch;
	bool got = false;
	while ((ch = std::fgetc(stdin)) != EOF && ch != '\n') {
		got = true;
		if (ch != '\r') out.push_back(static_cast<char>(ch));
	}
	// 管道/重定向输入可能带 UTF-8 BOM
	if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
	        static_cast<unsigned char>(out[1]) == 0xBB && static_cast<unsigned char>(out[2]) == 0xBF) {
		out.erase(0, 3);
	}
	// 去掉首尾空白
	const size_t b = out.find_first_not_of(" \t");
	const size_t e = out.find_last_not_of(" \t");
	if (b == std::string::npos) {
		out.clear();
	} else {
		out = out.substr(b, e - b + 1);
	}
	return got || ch != EOF;
}

bool promptFont(Options &opt) {
	std::string line;
	if (!promptLine("请输入字体文件名(回车用默认 SourceHanSansSC-Regular-2.otf，输入 q 退出)：", line)) {
		return false;
	}
	if (line == "q" || line == "Q") return false;
	opt.font = line.empty() ? kDefaultFont : line;
	return true;
}

void promptQuality(Options &opt) {
	std::string line;
	promptLine("请输入曲线细分质量 1~10(回车用默认 5)：", line);
	opt.quality = line.empty() ? kDefaultQuality : std::atoi(line.c_str());
	if (opt.quality < 1) opt.quality = 1;
	if (opt.quality > 10) opt.quality = 10;
	opt.qualitySet = true;
}

void promptSize(Options &opt) {
	std::string line;
	char buf[160];
	std::snprintf(buf, sizeof(buf), "请输入字号大小(单位 m，回车用默认 %g)：", kDefaultSize);
	promptLine(buf, line);
	if (line.empty()) {
		opt.size = kDefaultSize;
	} else {
		const double v = std::atof(line.c_str());
		opt.size = (v > 0.0) ? v : kDefaultSize;
	}
	opt.sizeSet = true;
}

bool promptText(Options &opt) {
	std::printf("请输入文本(回车换行，空行结束；支持空格)：\n");
	std::string text;
	std::string line;
	bool any = false;
	while (promptLine("> ", line)) {
		if (line.empty()) {
			if (any) break;
			continue;
		}
		any = true;
		text += line;
	}
	if (!any) return false;
	opt.text = text;
	return true;
}

bool promptContinue() {
	std::string line;
	if (!promptLine("\n回车继续生成下一段文本，输入 q 退出：", line)) return false;
	return !(line == "q" || line == "Q");
}

} // namespace

int main(int argc, char **argv) {
	natg::setupConsoleUtf8();
	const std::vector<std::string> args = natg::getUtf8Args(argc, argv);

	Options opt;
	std::string err;
	if (!parseArgs(args, opt, err)) {
		if (!err.empty()) std::printf("参数错误: %s\n\n", err.c_str());
		return 1;
	}

	// 没有任何"生成相关"参数(或显式 --serve) -> 本地网页界面
	if (args.size() <= 1 || opt.serve || !opt.hasWork) {
		natg::WebServerOptions wopt;
		wopt.port = opt.port;
		wopt.openBrowser = !opt.noOpen;
		return natg::runWebServer(wopt);
	}

	natg::FontCache cache;
	int exitCode = 0;

	auto fontBanner = [&](const std::string &path) -> std::string {
		natg::FontFace *face = nullptr;
		std::string ferr;
		if (cache.acquire(path, &face, ferr) && face) {
			return "字体: " + path + " (" + face->familyName() + " " + face->styleName() + ")";
		}
		return std::string();
	};

	const bool needInteractive = opt.font.empty() || !opt.sizeSet ||
	                             (!opt.qualitySet && !opt.toleranceSet) ||
	                             (opt.text.empty() && opt.textFile.empty());

	if (!needInteractive) {
		const std::string fontPath = natg::resolveFontPath(opt.font);
		const std::string banner = fontBanner(fontPath);
		if (banner.empty()) {
			std::printf("错误: 无法打开字体 %s\n", fontPath.c_str());
			return 1;
		}
		if (!runGenerate(banner, opt, cache)) exitCode = 1;
		return exitCode;
	}

	for (;;) {
		if (!promptFont(opt)) break;
		if (!opt.qualitySet && !opt.toleranceSet) promptQuality(opt);
		if (!opt.sizeSet) promptSize(opt);
		if (!promptText(opt)) break;
		const std::string banner = fontBanner(natg::resolveFontPath(opt.font));
		if (banner.empty()) {
			std::printf("错误: 无法打开字体 %s\n", opt.font.c_str());
			exitCode = 1;
		} else if (!runGenerate(banner, opt, cache)) {
			exitCode = 1;
		}
		if (!opt.loop && !promptContinue()) break;
		opt.text.clear();
		opt.textFile.clear();
	}
	return exitCode;
}
