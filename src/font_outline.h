// NATextGenerator - 字体轮廓提取与贝塞尔曲线扁平化
// 使用 FreeType 以 FT_LOAD_NO_SCALE 读取字形轮廓(字体单位，与字号无关)，
// 再按弦高误差自适应地把二次/三次贝塞尔曲线细分为折线。
#ifndef NATG_FONT_OUTLINE_H
#define NATG_FONT_OUTLINE_H

#include "geometry.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdint>
#include <string>
#include <vector>

namespace natg {

// 单个字形的矢量轮廓结果，坐标为 em 单位(1.0 = 一个字身高度)，y 轴向上，基线 y = 0
struct GlyphShape {
	std::vector<Ring> contours;
	double advanceEm = 0.0;   // 字宽推进量(em)
	bool hasOutline = false;  // 是否有可三角化的轮廓(空格等为 false)
	uint32_t codepoint = 0;
};

struct FlattenOptions {
	double toleranceEm = 0.002; // 曲线扁平化允许的最大弦高误差(em)
	int maxDepth = 14;          // 递归细分最大深度(防御性上限)
};

class FontFace {
public:
	FontFace() = default;
	~FontFace();
	FontFace(const FontFace &) = delete;
	FontFace &operator=(const FontFace &) = delete;

	// 打开字体文件(ttf/otf/ttc 均可)
	bool open(const std::string &path, std::string &err);
	void close();
	bool isOpen() const { return face_ != nullptr; }

	// 读取一个字符的轮廓
	bool loadGlyph(uint32_t codepoint, const FlattenOptions &opt, GlyphShape &out, std::string &err);

	// 字符到字形索引(0 表示字体没有该字符)
	uint32_t glyphIndex(uint32_t codepoint) const;

	// 两个字形的未缩放间距调整(em)，字体无此信息时返回 0
	double kerningEm(uint32_t leftGid, uint32_t rightGid) const;

	std::string familyName() const;
	std::string styleName() const;

private:
	FT_Library lib_ = nullptr;
	FT_Face face_ = nullptr;
	std::vector<unsigned char> data_;   // 字体文件内容(内存加载，支持中文路径)
};

} // namespace natg

#endif // NATG_FONT_OUTLINE_H
