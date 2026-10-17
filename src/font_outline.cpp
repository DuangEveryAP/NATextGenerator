#include "font_outline.h"
#include "platform.h"

#include FT_OUTLINE_H
#include FT_ERRORS_H

#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace natg {
namespace {

// ---------------- 贝塞尔扁平化 ----------------

struct FlattenCtx {
	double scale = 1.0;        // 字体单位 -> em
	double tolerance = 0.002;  // em
	int maxDepth = 14;
	Ring current;              // 当前正在构建的环
	std::vector<Ring> *out = nullptr;
	Vec2 last;                 // 上一次落笔点(em)
	std::string *err = nullptr;
	bool failed = false;
};

inline Vec2 toEm(const FT_Vector *v, double scale) {
	return Vec2(static_cast<double>(v->x) * scale, static_cast<double>(v->y) * scale);
}

// 二次贝塞尔: 以弦高估计误差做自适应 de Casteljau 细分
void flattenQuadratic(Ring &dst, const Vec2 &p0, const Vec2 &c, const Vec2 &p1,
                      double tol, int depth, int maxDepth) {
	// 二次贝塞尔与弦的最大偏差不超过控制点到弦距离的一半
	double dev = distancePointSegment(c, p0, p1) * 0.5;
	if (dev <= tol || depth >= maxDepth) {
		dst.push_back(p1);
		return;
	}
	Vec2 p01 = (p0 + c) * 0.5;
	Vec2 p12 = (c + p1) * 0.5;
	Vec2 mid = (p01 + p12) * 0.5;
	flattenQuadratic(dst, p0, p01, mid, tol, depth + 1, maxDepth);
	flattenQuadratic(dst, mid, p12, p1, tol, depth + 1, maxDepth);
}

// 三次贝塞尔: 控制点到弦的距离乘以 3/4 作为误差上界
void flattenCubic(Ring &dst, const Vec2 &p0, const Vec2 &c1, const Vec2 &c2, const Vec2 &p1,
                  double tol, int depth, int maxDepth) {
	double d1 = distancePointSegment(c1, p0, p1);
	double d2 = distancePointSegment(c2, p0, p1);
	double dev = (d1 > d2 ? d1 : d2) * 0.75;
	if (dev <= tol || depth >= maxDepth) {
		dst.push_back(p1);
		return;
	}
	Vec2 p01 = (p0 + c1) * 0.5;
	Vec2 p12 = (c1 + c2) * 0.5;
	Vec2 p23 = (c2 + p1) * 0.5;
	Vec2 p012 = (p01 + p12) * 0.5;
	Vec2 p123 = (p12 + p23) * 0.5;
	Vec2 mid = (p012 + p123) * 0.5;
	flattenCubic(dst, p0, p01, p012, mid, tol, depth + 1, maxDepth);
	flattenCubic(dst, mid, p123, p23, p1, tol, depth + 1, maxDepth);
}

void closeRing(FlattenCtx *ctx) {
	if (ctx->current.size() >= 2) {
		Ring r = ctx->current;
		dedupeRing(r, 1e-9);
		if (r.size() >= 3 && std::fabs(ringSignedArea(r)) > 1e-12) {
			ctx->out->push_back(std::move(r));
		}
	}
	ctx->current.clear();
}

int cbMoveTo(const FT_Vector *to, void *user) {
	FlattenCtx *ctx = static_cast<FlattenCtx *>(user);
	closeRing(ctx);
	ctx->last = toEm(to, ctx->scale);
	ctx->current.push_back(ctx->last);
	return 0;
}

int cbLineTo(const FT_Vector *to, void *user) {
	FlattenCtx *ctx = static_cast<FlattenCtx *>(user);
	Vec2 p = toEm(to, ctx->scale);
	if (ctx->current.empty()) ctx->current.push_back(ctx->last);
	ctx->current.push_back(p);
	ctx->last = p;
	return 0;
}

int cbConicTo(const FT_Vector *control, const FT_Vector *to, void *user) {
	FlattenCtx *ctx = static_cast<FlattenCtx *>(user);
	Vec2 c = toEm(control, ctx->scale);
	Vec2 p = toEm(to, ctx->scale);
	if (ctx->current.empty()) ctx->current.push_back(ctx->last);
	if (distance(ctx->last, p) <= 1e-12 && distance(ctx->last, c) <= 1e-12) {
		return 0; // 退化曲线
	}
	flattenQuadratic(ctx->current, ctx->last, c, p, ctx->tolerance, 0, ctx->maxDepth);
	ctx->last = p;
	return 0;
}

int cbCubicTo(const FT_Vector *c1, const FT_Vector *c2, const FT_Vector *to, void *user) {
	FlattenCtx *ctx = static_cast<FlattenCtx *>(user);
	Vec2 p1 = toEm(c1, ctx->scale);
	Vec2 p2 = toEm(c2, ctx->scale);
	Vec2 p = toEm(to, ctx->scale);
	if (ctx->current.empty()) ctx->current.push_back(ctx->last);
	flattenCubic(ctx->current, ctx->last, p1, p2, p, ctx->tolerance, 0, ctx->maxDepth);
	ctx->last = p;
	return 0;
}

} // namespace

FontFace::~FontFace() {
	close();
}

void FontFace::close() {
	if (face_) {
		FT_Done_Face(face_);
		face_ = nullptr;
	}
	if (lib_) {
		FT_Done_FreeType(lib_);
		lib_ = nullptr;
	}
	data_.clear();
	data_.shrink_to_fit();
}

bool FontFace::open(const std::string &path, std::string &err) {
	close();
	FT_Error e = FT_Init_FreeType(&lib_);
	if (e) {
		err = std::string("FreeType 初始化失败: ") + (FT_Error_String(e) ? FT_Error_String(e) : "未知错误");
		lib_ = nullptr;
		return false;
	}
	// 先把字体读进内存再交给 FreeType: 这样中文路径也能正常工作
	if (!readFileBinary(path, data_) || data_.empty()) {
		err = "无法读取字体文件 \"" + path + "\"";
		close();
		return false;
	}
	e = FT_New_Memory_Face(lib_, data_.data(), static_cast<FT_Long>(data_.size()), 0, &face_);
	if (e) {
		const char *msg = FT_Error_String(e);
		char tmp[512];
		std::snprintf(tmp, sizeof(tmp), "无法解析字体 \"%s\" (错误码 %d: %s)", path.c_str(), e, msg ? msg : "未知错误");
		err = tmp;
		close();
		return false;
	}
	// 优先使用 Unicode 字符映射表
	if (FT_Select_Charmap(face_, FT_ENCODING_UNICODE)) {
		FT_Select_Charmap(face_, FT_ENCODING_NONE);
	}
	if (face_->units_per_EM == 0) {
		err = "字体缺少 units_per_EM 信息，无法按矢量方式处理";
		close();
		return false;
	}
	return true;
}

uint32_t FontFace::glyphIndex(uint32_t codepoint) const {
	if (!face_) return 0;
	return static_cast<uint32_t>(FT_Get_Char_Index(face_, codepoint));
}

std::string FontFace::familyName() const {
	return (face_ && face_->family_name) ? face_->family_name : "";
}

std::string FontFace::styleName() const {
	return (face_ && face_->style_name) ? face_->style_name : "";
}

double FontFace::kerningEm(uint32_t leftGid, uint32_t rightGid) const {
	if (!face_ || !FT_HAS_KERNING(face_) || leftGid == 0 || rightGid == 0) return 0.0;
	FT_Vector kern;
	if (FT_Get_Kerning(face_, leftGid, rightGid, FT_KERNING_UNSCALED, &kern)) return 0.0;
	return static_cast<double>(kern.x) / static_cast<double>(face_->units_per_EM);
}

bool FontFace::loadGlyph(uint32_t codepoint, const FlattenOptions &opt, GlyphShape &out, std::string &err) {
	out = GlyphShape();
	out.codepoint = codepoint;
	if (!face_) {
		err = "字体尚未打开";
		return false;
	}

	FT_UInt gid = FT_Get_Char_Index(face_, codepoint);
	uint32_t actual = codepoint;
	if (gid == 0) {
		// 字体不含该字符: 退回 U+FFFD 替换字符
		gid = FT_Get_Char_Index(face_, 0xFFFD);
		actual = 0xFFFD;
	}
	if (gid == 0) {
		char tmp[256];
		std::snprintf(tmp, sizeof(tmp), "字体不包含字符 U+%04X，已跳过", codepoint);
		err = tmp;
		return false;
	}

	// 关键: NO_SCALE 让 FreeType 不做栅格化/缩放，直接返回字体单位轮廓
	FT_Error e = FT_Load_Glyph(face_, gid, FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING);
	if (e) {
		const char *msg = FT_Error_String(e);
		char tmp[256];
		std::snprintf(tmp, sizeof(tmp), "载入字形 U+%04X 失败(错误码 %d: %s)", codepoint, e, msg ? msg : "未知");
		err = tmp;
		return false;
	}
	if (actual != codepoint) {
		char tmp[256];
		std::snprintf(tmp, sizeof(tmp), "字体不含字符 U+%04X，已用 U+FFFD 代替", codepoint);
		err = tmp;
	}

	const double upem = static_cast<double>(face_->units_per_EM);
	out.advanceEm = static_cast<double>(face_->glyph->advance.x) / upem;

	FT_Outline *outline = &face_->glyph->outline;
	if (outline->n_points == 0 || outline->n_contours == 0) {
		out.hasOutline = false;
		return true; // 空格一类: 只有字宽，没有轮廓
	}

	FlattenCtx ctx;
	ctx.scale = 1.0 / upem;
	ctx.tolerance = opt.toleranceEm;
	ctx.maxDepth = opt.maxDepth;
	ctx.out = &out.contours;
	ctx.err = &err;

	FT_Outline_Funcs funcs;
	funcs.move_to = cbMoveTo;
	funcs.line_to = cbLineTo;
	funcs.conic_to = cbConicTo;
	funcs.cubic_to = cbCubicTo;
	funcs.shift = 0;  // 不做定点移位(坐标已是字体单位)
	funcs.delta = 0;

	e = FT_Outline_Decompose(outline, &funcs, &ctx);
	closeRing(&ctx);
	if (e) {
		const char *msg = FT_Error_String(e);
		char tmp[256];
		std::snprintf(tmp, sizeof(tmp), "解析字形 U+%04X 轮廓失败(错误码 %d: %s)", codepoint, e, msg ? msg : "未知");
		err = tmp;
		return false;
	}

	out.hasOutline = !out.contours.empty();
	return true;
}

} // namespace natg
