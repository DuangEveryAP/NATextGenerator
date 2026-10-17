#include "placement.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace natg {
namespace {

double bernstein3(int i, double t) {
	const double mt = 1.0 - t;
	switch (i) {
		case 0: return mt * mt * mt;
		case 1: return 3.0 * t * mt * mt;
		case 2: return 3.0 * t * t * mt;
		default: return t * t * t;
	}
}

double bernstein3d(int i, double t) {
	const double mt = 1.0 - t;
	switch (i) {
		case 0: return -3.0 * mt * mt;
		case 1: return 3.0 * mt * mt - 6.0 * t * mt;
		case 2: return 6.0 * t * mt - 3.0 * t * t;
		default: return 3.0 * t * t;
	}
}

} // namespace

Vec3 BezierPatch::eval(double u, double v) const {
	double bu[4], bv[4];
	for (int i = 0; i < 4; i++) {
		bu[i] = bernstein3(i, u);
		bv[i] = bernstein3(i, v);
	}
	Vec3 p;
	for (int j = 0; j < 4; j++) {
		for (int i = 0; i < 4; i++) {
			const double w = bu[i] * bv[j];
			p += cp[j][i] * w;
		}
	}
	return p;
}

Vec3 BezierPatch::normal(double u, double v) const {
	double bu[4], bv[4], du[4];
	for (int i = 0; i < 4; i++) {
		bu[i] = bernstein3(i, u);
		bv[i] = bernstein3(i, v);
		du[i] = bernstein3d(i, u);
	}
	Vec3 dpdu, dpdv;
	for (int j = 0; j < 4; j++) {
		for (int i = 0; i < 4; i++) {
			dpdu += cp[j][i] * (du[i] * bv[j]);
			dpdv += cp[j][i] * (bu[i] * bernstein3d(j, v));
		}
	}
	// 取 cross(dPdv, dPdu): 对"平直"预设(u 沿 z、v 沿 y)得到 +x，
	// 与平面模式的"前脸朝 +x"一致，厚度方向才不会整体反过来。
	Vec3 n = cross3(dpdv, dpdu);
	if (length3(n) < 1e-12) return Vec3(1.0, 0.0, 0.0);   // 退化时退回厚度方向
	return normalize3(n);
}

Vec3 placeVertex(const PlacementOptions &opt, const Vec2 &glyphEm, double side) {
	if (opt.mode == PlacementMode::Plane) {
		// 与旧版一致: x = 厚度方向, y = 向上, z = 文字方向
		return Vec3(side * opt.halfThickness, glyphEm.y * opt.sizeMeters, glyphEm.x * opt.sizeMeters);
	}
	const double w = (opt.bboxMaxX - opt.bboxMinX) > 1e-12 ? (opt.bboxMaxX - opt.bboxMinX) : 1.0;
	const double h = (opt.bboxMaxY - opt.bboxMinY) > 1e-12 ? (opt.bboxMaxY - opt.bboxMinY) : 1.0;
	const double u = (glyphEm.x - opt.bboxMinX) / w;
	const double v = (glyphEm.y - opt.bboxMinY) / h;
	const Vec3 p = opt.patch.eval(u, v);
	Vec3 n = opt.patch.normal(u, v);
	if (opt.flipNormal) n = n * -1.0;
	return p + n * (side * opt.halfThickness);
}

BezierPatch flatPatchEm(const PlacementOptions &opt) {
	BezierPatch p;
	for (int j = 0; j < 4; j++) {
		const double v = static_cast<double>(j) / 3.0;
		for (int i = 0; i < 4; i++) {
			const double u = static_cast<double>(i) / 3.0;
			const double x = opt.bboxMinX + (opt.bboxMaxX - opt.bboxMinX) * u;
			const double y = opt.bboxMinY + (opt.bboxMaxY - opt.bboxMinY) * v;
			// 与平面模式同构: 世界 x = 厚度, y = 向上, z = 文字方向
			p.cp[j][i] = Vec3(0.0, y * opt.sizeMeters, x * opt.sizeMeters);
		}
	}
	return p;
}

bool parsePatch(const std::string &text, SurfaceUnit unit, double sizeMeters,
                BezierPatch &out, std::string &err) {
	std::vector<double> nums;
	nums.reserve(48);
	const char *p = text.c_str();
	while (*p) {
		if (std::strchr(" \t\r\n,;|", *p)) {
			p++;
			continue;
		}
		char *end = nullptr;
		const double v = std::strtod(p, &end);
		if (end == p) {
			err = std::string("控制点里有无法识别的字符: ") + *p;
			return false;
		}
		nums.push_back(v);
		p = end;
	}
	if (nums.size() != 48) {
		char buf[128];
		std::snprintf(buf, sizeof(buf), "控制点需要 48 个数(4 行 x 4 点 x 3 坐标)，实际读到 %zu 个", nums.size());
		err = buf;
		return false;
	}
	const double k = (unit == SurfaceUnit::Em) ? sizeMeters : 1.0;
	for (int j = 0; j < 4; j++) {
		for (int i = 0; i < 4; i++) {
			const size_t base = static_cast<size_t>(j * 4 + i) * 3;
			out.cp[j][i] = Vec3(nums[base] * k, nums[base + 1] * k, nums[base + 2] * k);
		}
	}
	return true;
}

void patchExtent(const BezierPatch &patch, double &widthMeters, double &heightMeters) {
	// 用 u/v 方向的角点弦长作为"展平尺寸"的粗略参考
	widthMeters = length3(patch.cp[0][3] - patch.cp[0][0]);
	heightMeters = length3(patch.cp[3][0] - patch.cp[0][0]);
}

} // namespace natg
