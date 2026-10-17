#include "na_writer.h"
#include "platform.h"

#include <cmath>
#include <cstdio>

namespace natg {

std::string escapeXml(const std::string &s) {
	std::string out;
	out.reserve(s.size() + 8);
	for (char ch : s) {
		switch (ch) {
			case '&': out += "&amp;"; break;
			case '<': out += "&lt;"; break;
			case '>': out += "&gt;"; break;
			case '"': out += "&quot;"; break;
			case '\'': out += "&apos;"; break;
			default: out += ch; break;
		}
	}
	return out;
}

// 分配 4 个角点。关键约束来自旧版可用的存档:
//   前脸的面序是 a,b,d,c —— 也就是说 A,B,D,C 必须是一条**不自交的闭合边环**
//   （A-B 一条边、B-D 一条边、D-C 一条边、C-A 一条边），六个面才能被游戏正确重建。
//   只要 A,B,D,C 环绕一圈，A/B/C/D 的"上下左右"字样并不重要（旧版的像素块刚好是
//   横平竖直的，所以看起来才像"行优先"）。
// 因此这里直接用多边形的环绕序: 逆时针 p0..p3 -> A=p0, B=p3, C=p1, D=p2，
// 使 A,B,D,C = p0,p3,p2,p1 成为顺时针的一圈。
void assignLegacyCorners(const Polygon &poly, Vec2 out[4]) {
	const size_t n = poly.pts.size();
	if (n == 4) {
		out[0] = poly.pts[0];   // A
		out[1] = poly.pts[3];   // B
		out[2] = poly.pts[1];   // C
		out[3] = poly.pts[2];   // D
	} else {
		out[0] = poly.pts[0];   // A
		out[1] = poly.pts[2];   // B
		out[2] = poly.pts[1];   // C
		out[3] = poly.pts[1];   // D = C(退化，表示三角面)
	}
}

void partCorners(const PlacementOptions &placement, const Polygon &poly, Vec3 front[4], Vec3 back[4]) {
	Vec2 corners[4];
	assignLegacyCorners(poly, corners);
	for (int i = 0; i < 4; i++) {
		front[i] = placeVertex(placement, corners[i], 1.0);
		back[i] = placeVertex(placement, corners[i], -1.0);
	}
}

bool writeNaFile(const std::string &path, const std::string &description, const std::vector<Polygon> &parts,
                 const PlacementOptions &placement, const PartSettings &settings, WriterStats *stats,
                 std::string &err) {
	FILE *f = fopenUtf8(path, "wb");
	if (!f) {
		err = "无法写入文件: " + path;
		return false;
	}
	static char buffer[1 << 20];
	std::setvbuf(f, buffer, _IOFBF, sizeof(buffer));

	std::fprintf(f, "<root>\n  <ship author=\"Duang's generator\" description=\"%s By Duang's generator.\""
	             " alwaysgeneratenewthumbnail=\"True\" hornType=\"1\" hornPitch=\"1\" tracerCol=\"E53D4FFF\">\n",
	             escapeXml(description).c_str());

	WriterStats st;
	long instanceId = 0;
	for (const Polygon &poly : parts) {
		const size_t n = poly.pts.size();
		if (n < 3 || n > 4) {
			st.skipped++;
			continue;
		}
		// 退化面(零面积/重合点)直接跳过，避免生成无效零件
		double area = 0.0;
		for (size_t i = 0; i < n; i++) {
			const Vec2 &p = poly.pts[i];
			const Vec2 &q = poly.pts[(i + 1) % n];
			area += p.x * q.y - q.x * p.y;
		}
		area *= 0.5;
		if (!(std::fabs(area) > 1e-13)) {
			st.skipped++;
			continue;
		}

		Vec3 front[4], back[4];
		partCorners(placement, poly, front, back);

		std::fprintf(f, "    <part id=\"%d\" instanceId=\"%ld\">\n", settings.partId, instanceId++);
		std::fprintf(f,
		             "      <vertexData ax=\"%f\" ay=\"%f\" az=\"%f\" bx=\"%f\" by=\"%f\" bz=\"%f\""
		             " cx=\"%f\" cy=\"%f\" cz=\"%f\" dx=\"%f\" dy=\"%f\" dz=\"%f\"",
		             front[0].x, front[0].y, front[0].z, front[1].x, front[1].y, front[1].z,
		             front[2].x, front[2].y, front[2].z, front[3].x, front[3].y, front[3].z);
		std::fprintf(f,
		             " ix=\"%f\" iy=\"%f\" iz=\"%f\" iix=\"%f\" iiy=\"%f\" iiz=\"%f\""
		             " iiix=\"%f\" iiiy=\"%f\" iiiz=\"%f\" ivx=\"%f\" ivy=\"%f\" ivz=\"%f\" sym=\"None\" />\n",
		             back[0].x, back[0].y, back[0].z, back[1].x, back[1].y, back[1].z,
		             back[2].x, back[2].y, back[2].z, back[3].x, back[3].y, back[3].z);
		std::fprintf(f, "      <position x=\"0\" y=\"0\" z=\"0\" />\n");
		std::fprintf(f, "      <rotation x=\"0\" y=\"0\" z=\"0\" />\n");
		std::fprintf(f, "      <scale x=\"1\" y=\"1\" z=\"1\" />\n");
		std::fprintf(f, "      <color hex=\"%s\" />\n", settings.colorHex.c_str());
		std::fprintf(f, "      <armor value=\"%g\" />\n", settings.armor);
		std::fprintf(f, "    </part>\n");
		st.parts++;
	}
	std::fprintf(f, "  </ship>\n</root>");

	if (std::fclose(f) != 0) {
		err = "写入文件时出错: " + path;
		return false;
	}
	if (stats) *stats = st;
	return true;
}

} // namespace natg
