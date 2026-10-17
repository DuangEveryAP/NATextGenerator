// NATextGenerator - 多边形三角化
// 字形区域往往是"带孔洞的凹多边形"，不能简单地按扇形三角化。
// 这里使用 earcut 算法(src/earcut.h)完成孔洞桥接与耳切，
// 结果输出到 PolyMesh(顶点 + 三角面索引)，并记录多边形的边界边，
// 供后续的保角翻转 / 四边形合并判断"哪些边不能动"。
#ifndef NATG_TRIANGULATE_H
#define NATG_TRIANGULATE_H

#include "geometry.h"
#include "mesh.h"
#include "polygonize.h"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace natg {

struct TriangulateStats {
	int regions = 0;       // 处理的实心区域数
	int holes = 0;         // 参与桥接的孔洞数
	int holesDropped = 0;  // 因退化被丢弃的孔洞数
	int triangles = 0;     // 输出三角面数
	int degenerate = 0;    // 被过滤掉的退化三角面
};

// 把一个实心区域三角化并追加到 mesh；boundaryEdges 收集边界边(不可翻转/不可跨边合并)
void triangulateRegionToMesh(const PolygonRegion &region, PolyMesh &mesh,
                             std::unordered_set<uint64_t> &boundaryEdges,
                             double simplifyEps, TriangulateStats *stats = nullptr);

// 便捷入口: 一堆轮廓 -> mesh
void triangulateContoursToMesh(const std::vector<Ring> &contours, PolyMesh &mesh,
                               std::unordered_set<uint64_t> &boundaryEdges,
                               double simplifyEps, TriangulateStats *stats = nullptr);

} // namespace natg

#endif // NATG_TRIANGULATE_H
