// NATextGenerator - 放置方式: 平面 / 双三次贝塞尔曲面
//
// 平面(plane): 与旧版一致，文字立在 x = ±厚度/2 的竖直平面内，
//              x = 厚度方向, y = 向上, z = 文字方向。
// 曲面(surface): 用 4x4 控制点的双三次贝塞尔曲面片描述一个弯面，
//              文字按包围盒归一化到参数 (u,v)，再沿曲面法向偏移出厚度。
//
// 控制点坐标可以有两种单位:
//   Unit::Em   归一化: 数值是"字高的倍数"，最终乘以字号(米)，曲面随字号缩放；
//   Unit::Meter 实测: 数值就是米，曲面大小固定。
#ifndef NATG_PLACEMENT_H
#define NATG_PLACEMENT_H

#include "geometry.h"

#include <string>
#include <vector>

namespace natg {

// 双三次贝塞尔曲面片: cp[v][u]，v/u 取 0..3 对应参数 0..1
struct BezierPatch {
	Vec3 cp[4][4];

	Vec3 eval(double u, double v) const;
	Vec3 normal(double u, double v) const;   // 已归一化
};

enum class PlacementMode { Plane, Surface };
enum class SurfaceUnit { Em, Meter };

struct PlacementOptions {
	PlacementMode mode = PlacementMode::Plane;
	BezierPatch patch;              // 世界坐标(米)
	bool flipNormal = false;        // 翻转厚度方向

	// 文字包围盒(em 单位)，用于把字形坐标归一化到曲面参数
	double bboxMinX = 0.0;
	double bboxMaxX = 1.0;
	double bboxMinY = 0.0;
	double bboxMaxY = 1.0;

	double sizeMeters = 10.0;       // 字号
	double halfThickness = 0.1;     // 厚度的一半(米)
};

// 字形平面坐标(em) + side(+1 前脸 / -1 后脸) -> 世界坐标(米)
Vec3 placeVertex(const PlacementOptions &opt, const Vec2 &glyphEm, double side);

// 把"平面"预设的控制点填进 patch: 直接匹配文字包围盒(em)
BezierPatch flatPatchEm(const PlacementOptions &opt);

// 解析 "x,y,z x,y,z ..." 形式的 48 个数(4 行 x 4 点)，单位为 unit
// 成功返回 true；失败时 err 给出原因
bool parsePatch(const std::string &text, SurfaceUnit unit, double sizeMeters,
                BezierPatch &out, std::string &err);

// 面积/尺寸参考: 曲面在展平意义上的宽度(u 方向)与高度(v 方向)，单位米
void patchExtent(const BezierPatch &patch, double &widthMeters, double &heightMeters);

} // namespace natg

#endif // NATG_PLACEMENT_H
