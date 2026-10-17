// NATextGenerator - 生成管线(文本 -> 零件多边形)
// 命令行与 Web 服务共用这一层：字体加载、UTF-8 处理、字形排版、三角化、网格优化。
#ifndef NATG_GENERATOR_H
#define NATG_GENERATOR_H

#include "font_outline.h"
#include "geometry.h"
#include "mesh.h"
#include "placement.h"

#include <string>
#include <vector>

namespace natg {

// 质量等级 1~10 -> 曲线扁平化容差(em)
double qualityToTolerance(int quality);

// 字体名称 -> 实际路径(原样可用就用原样，否则到 fonts/ 下找)
std::string resolveFontPath(const std::string &name);

// fonts/ 目录下可用的字体文件名
std::vector<std::string> listFonts();

// UTF-8 解码，并把换行/制表符转成空格、丢弃其它控制字符
std::vector<uint32_t> decodeText(const std::string &utf8);

struct GenerationStats {
	size_t codepoints = 0;         // 字符数
	size_t glyphsWithOutline = 0;  // 有轮廓的字形数
	size_t blanks = 0;             // 空白字符(如空格)
	size_t rings = 0;              // 轮廓环数
	size_t regions = 0;            // 实心区域数
	size_t holes = 0;              // 孔洞数
	size_t holesDropped = 0;       // 退化被丢弃的孔洞
	size_t degenerate = 0;         // 退化三角面
	size_t triangles = 0;          // 优化前的三角面数
	size_t parts = 0;              // 最终零件数
	size_t quads = 0;              // 其中四边形零件数
	size_t flips = 0;              // 保角翻转次数
	size_t sliversIn = 0;          // 优化前细长三角面数
	size_t sliversOut = 0;         // 优化后细长零件数
	size_t bigThinIn = 0;          // 优化前"又长又薄"的面数
	size_t bigThinOut = 0;         // 优化后"又长又薄"的面数
	double bigThinShareIn = 0.0;   // 又长又薄的面占总面积百分比(优化前)
	double bigThinShareOut = 0.0;  // 优化后
	double areaIn = 0.0;
	double areaOut = 0.0;
	double bigThinAreaIn = 0.0;
	double bigThinAreaOut = 0.0;
	double minAngleIn = 0.0;       // 优化前最小角最差值(度)
	double minAngleOut = 0.0;      // 优化后最小角最差值(度)
	std::vector<uint32_t> missing; // 字体缺少的字符
	double ms = 0.0;               // 几何用时(毫秒)
};

struct GenerateRequest {
	std::string fontPath;        // 字体文件路径(已解析)
	std::string text;
	double sizeMeters = 10.0;    // 字号(em 对应米数)
	double toleranceEm = 0.002;  // 曲线容差
	bool kerning = true;
	size_t maxParts = 200000;    // 零件数上限

	// 网格优化
	bool mergeQuads = true;          // 三角面合并成四边形
	bool flipEdges = true;           // 保角翻转
	double simplifyEps = -1.0;       // 边界简化阈值(em)，<0 表示自动(0.5*容差)
	double mergeMinAngleDeg = 20.0;  // 直接合并的质量门限

	// 放置方式: 平面 / 贝塞尔曲面
	PlacementMode placementMode = PlacementMode::Plane;
	std::string surfaceCp;                        // 16 个控制点 "x,y,z ..." 共 48 个数
	SurfaceUnit surfaceUnit = SurfaceUnit::Em;    // 归一化(字高倍数) / 实测(米)
	bool flipNormal = false;                      // 翻转厚度方向
	double thickness = 0.2;                       // 总厚度(米)
};

struct GenerateResult {
	bool ok = false;
	std::string error;
	std::vector<Polygon> parts;    // em 单位，逆时针
	GenerationStats stats;
	double minX = 0, maxX = 0, minY = 0, maxY = 0;
	bool truncated = false;
	PlacementOptions placement;    // 已填好包围盒/字号/厚度的放置参数

	// 每个字符的落笔位置(em)与码点，供前端"逐字"绘制参考字，
	// 避免浏览器自己的排版(含 GPOS 字距)与生成结果对不上
	std::vector<double> glyphPenX;
	std::vector<uint32_t> glyphCodepoints;
};

// 复用同一个字体对象，避免每次请求都重新解析字体
class FontCache {
public:
	~FontCache();
	bool acquire(const std::string &path, FontFace **out, std::string &err);
	const std::string &currentPath() const { return path_; }

private:
	std::string path_;
	FontFace *face_ = nullptr;
};

bool generatePolygons(const GenerateRequest &req, GenerateResult &res, FontCache &cache);

} // namespace natg

#endif // NATG_GENERATOR_H
