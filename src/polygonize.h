// NATextGenerator - 轮廓组装: 判断嵌套关系、区分外轮廓与内部区域(孔洞)
// FreeType 返回的是一串互不相连的闭合轮廓；要正确填充字形，必须知道
// 哪些轮廓是"外面"，哪些是"里面"(如 口、O、A 的三角孔、ä 的两点等)。
// 这里采用嵌套深度(奇偶规则)判定: 被包含偶数层 = 实心区域, 奇数层 = 孔洞,
// 与字体自身的顺时针/逆时针约定无关，因此对 TrueType 与 CFF 字体都适用。
#ifndef NATG_POLYGONIZE_H
#define NATG_POLYGONIZE_H

#include "geometry.h"

#include <vector>

namespace natg {

// 一个实心区域: 一个外环(逆时针) + 若干孔洞(顺时针)
struct PolygonRegion {
	Ring outer;
	std::vector<Ring> holes;
};

struct PolygonizeStats {
	int ringsIn = 0;       // 输入轮廓数
	int ringsDropped = 0;  // 因退化被丢弃
	int regions = 0;       // 生成的实心区域数
	int holes = 0;         // 孔洞总数
};

std::vector<PolygonRegion> buildRegions(const std::vector<Ring> &contours, PolygonizeStats *stats = nullptr);

// 求一个严格位于环内部的点(用于嵌套判定)
bool findInteriorPoint(const Ring &ring, Vec2 &out);

} // namespace natg

#endif // NATG_POLYGONIZE_H
