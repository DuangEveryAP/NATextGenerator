#include "generator.h"

#include "platform.h"
#include "polygonize.h"
#include "triangulate.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <unordered_set>

namespace natg {

double qualityToTolerance(int quality) {
	static const double table[10] = {
		0.020, 0.010, 0.0050, 0.0030, 0.0020, 0.0012, 0.0008, 0.0005, 0.0003, 0.00015
	};
	if (quality < 1) quality = 1;
	if (quality > 10) quality = 10;
	return table[quality - 1];
}

std::string resolveFontPath(const std::string &name) {
	if (name.empty()) return name;
	if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return name;
	FILE *f = fopenUtf8(name, "rb");
	if (f) {
		std::fclose(f);
		return name;
	}
	return std::string("fonts/") + name;
}

std::vector<std::string> listFonts() {
	return listDirFiles("fonts", {".ttf", ".otf", ".ttc", ".otc"});
}

std::vector<uint32_t> decodeText(const std::string &utf8) {
	std::vector<uint32_t> out;
	const unsigned char *p = reinterpret_cast<const unsigned char *>(utf8.c_str());
	const unsigned char *end = p + utf8.size();
	while (p < end) {
		uint32_t ch = 0;
		int bytes = 0;
		if (*p < 0x80) {
			ch = *p;
			bytes = 1;
		} else if ((*p & 0xE0) == 0xC0) {
			ch = *p & 0x1Fu;
			bytes = 2;
		} else if ((*p & 0xF0) == 0xE0) {
			ch = *p & 0x0Fu;
			bytes = 3;
		} else if ((*p & 0xF8) == 0xF0) {
			ch = *p & 0x07u;
			bytes = 4;
		} else {
			p++;
			continue;
		}
		if (p + bytes > end) break;
		bool ok = true;
		for (int i = 1; i < bytes; i++) {
			if ((p[i] & 0xC0) != 0x80) {
				ok = false;
				break;
			}
			ch = (ch << 6) | (p[i] & 0x3Fu);
		}
		if (!ok) {
			p++;
			continue;
		}
		if (ch == '\r' || ch == '\n' || ch == '\t') {
			out.push_back(' ');
		} else if (ch >= 0x20 && ch != 0x7F) {
			out.push_back(ch);
		}
		p += bytes;
	}
	return out;
}

FontCache::~FontCache() {
	delete face_;
	face_ = nullptr;
}

bool FontCache::acquire(const std::string &path, FontFace **out, std::string &err) {
	if (!face_ || path_ != path) {
		delete face_;
		face_ = new FontFace();
		if (!face_->open(path, err)) {
			delete face_;
			face_ = nullptr;
			path_.clear();
			return false;
		}
		path_ = path;
	}
	*out = face_;
	return true;
}

bool generatePolygons(const GenerateRequest &req, GenerateResult &res, FontCache &cache) {
	const auto t0 = std::chrono::steady_clock::now();

	FontFace *font = nullptr;
	std::string err;
	if (!cache.acquire(req.fontPath, &font, err)) {
		res.error = err;
		return false;
	}

	const std::vector<uint32_t> cps = decodeText(req.text);
	res.stats.codepoints = cps.size();
	res.stats.minAngleIn = 180.0;
	res.stats.minAngleOut = 180.0;

	FlattenOptions flat;
	flat.toleranceEm = req.toleranceEm;

	MeshOptOptions mopt;
	mopt.mergeQuads = req.mergeQuads;
	mopt.flipEdges = req.flipEdges;
	mopt.mergeMinAngleDeg = req.mergeMinAngleDeg;
	// 边界简化阈值: 默认取曲线容差的一半，但不低于 0.0008 em。
	// 理由: 曲线被细分成很多"几乎共线"的顶点时，这些顶点必然产生又长又薄的三角面
	// (游戏里最显眼的就是这种长针形零件)。0.0008 em 在 10 m 字高下约 8 mm，
	// 几乎不影响外形，却能显著减少细长零件。
	double autoSimplify = req.toleranceEm * 0.5;
	if (autoSimplify < 0.0008) autoSimplify = 0.0008;
	const double simplifyEps = req.simplifyEps >= 0.0 ? req.simplifyEps : autoSimplify;

	std::vector<Polygon> &parts = res.parts;
	double penX = 0.0;
	uint32_t prevGid = 0;

	for (uint32_t cp : cps) {
		const uint32_t gid = font->glyphIndex(cp);
		if (req.kerning && prevGid != 0 && gid != 0) penX += font->kerningEm(prevGid, gid);
		prevGid = gid;

		GlyphShape shape;
		std::string glyphErr;
		if (!font->loadGlyph(cp, flat, shape, glyphErr)) {
			res.stats.missing.push_back(cp);
			continue;
		}
		if (!shape.hasOutline) {
			res.stats.blanks++;
			res.glyphPenX.push_back(penX);
			res.glyphCodepoints.push_back(cp);
			penX += shape.advanceEm;
			continue;
		}

		// 记录这个字的落笔位置(前端逐字绘制参考字用)
		res.glyphPenX.push_back(penX);
		res.glyphCodepoints.push_back(cp);

		// 一个字形一个网格: 孔洞/边界信息只在字形内有效
		PolyMesh mesh;
		std::unordered_set<uint64_t> boundary;
		PolygonizeStats pst;
		std::vector<PolygonRegion> regions = buildRegions(shape.contours, &pst);
		TriangulateStats tst;
		for (const PolygonRegion &r : regions) {
			TriangulateStats one;
			triangulateRegionToMesh(r, mesh, boundary, simplifyEps, &one);
			tst.regions += one.regions;
			tst.holes += one.holes;
			tst.holesDropped += one.holesDropped;
			tst.triangles += one.triangles;
			tst.degenerate += one.degenerate;
		}

		MeshOptStats mst;
		std::vector<Polygon> polys = meshToPolygons(mesh, boundary, mopt, &mst);
		for (Polygon &poly : polys) {
			for (Vec2 &v : poly.pts) v.x += penX;
			parts.push_back(std::move(poly));
		}

		res.stats.rings += shape.contours.size();
		res.stats.regions += static_cast<size_t>(tst.regions);
		res.stats.holes += pst.holes;
		res.stats.holesDropped += static_cast<size_t>(tst.holesDropped);
		res.stats.degenerate += static_cast<size_t>(tst.degenerate);
		res.stats.triangles += static_cast<size_t>(tst.triangles);
		res.stats.parts += mst.parts;
		res.stats.quads += mst.quads;
		res.stats.flips += mst.flips;
		res.stats.sliversIn += mst.sliverIn;
		res.stats.sliversOut += mst.sliverOut;
		res.stats.bigThinIn += mst.bigThinIn;
		res.stats.bigThinOut += mst.bigThinOut;
		res.stats.areaIn += mst.areaIn;
		res.stats.areaOut += mst.areaOut;
		res.stats.bigThinAreaIn += mst.bigThinAreaIn;
		res.stats.bigThinAreaOut += mst.bigThinAreaOut;
		res.stats.minAngleIn = std::min(res.stats.minAngleIn, mst.minAngleIn);
		res.stats.minAngleOut = std::min(res.stats.minAngleOut, mst.minAngleOut);
		res.stats.glyphsWithOutline++;
		penX += shape.advanceEm;
	}

	if (parts.empty()) {
		res.stats.minAngleIn = 0.0;
		res.stats.minAngleOut = 0.0;
		res.error = "文本中没有可见轮廓(字体可能不含这些字符，或全都是空白字符)";
		return false;
	}

	// 包围盒
	res.minX = res.maxX = parts[0].pts[0].x;
	res.minY = res.maxY = parts[0].pts[0].y;
	for (const Polygon &p : parts) {
		for (const Vec2 &v : p.pts) {
			if (v.x < res.minX) res.minX = v.x;
			if (v.x > res.maxX) res.maxX = v.x;
			if (v.y < res.minY) res.minY = v.y;
			if (v.y > res.maxY) res.maxY = v.y;
		}
	}

	if (req.maxParts > 0 && parts.size() > req.maxParts) {
		parts.resize(req.maxParts);
		res.truncated = true;
	}
	res.stats.parts = parts.size();

	res.stats.bigThinShareIn = res.stats.areaIn > 0.0 ? res.stats.bigThinAreaIn / res.stats.areaIn * 100.0 : 0.0;
	res.stats.bigThinShareOut = res.stats.areaOut > 0.0 ? res.stats.bigThinAreaOut / res.stats.areaOut * 100.0 : 0.0;

	// 放置参数(平面/曲面)
	res.placement.mode = req.placementMode;
	res.placement.flipNormal = req.flipNormal;
	res.placement.bboxMinX = res.minX;
	res.placement.bboxMaxX = res.maxX;
	res.placement.bboxMinY = res.minY;
	res.placement.bboxMaxY = res.maxY;
	res.placement.sizeMeters = req.sizeMeters;
	res.placement.halfThickness = req.thickness * 0.5;
	if (req.placementMode == PlacementMode::Surface) {
		if (req.surfaceCp.empty()) {
			// 没给控制点时退回"与平面一致"的曲面(方便用户从平的状态开始调)
			res.placement.patch = flatPatchEm(res.placement);
		} else {
			std::string perr;
			if (!parsePatch(req.surfaceCp, req.surfaceUnit, req.sizeMeters, res.placement.patch, perr)) {
				res.error = perr;
				return false;
			}
		}
	}

	const auto t1 = std::chrono::steady_clock::now();
	res.stats.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
	res.ok = true;
	return true;
}

} // namespace natg
