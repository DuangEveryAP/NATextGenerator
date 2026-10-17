// NATextGenerator - 网格后处理
// 目标: 把三角化结果整理成"零件"，并尽量消除细长形状。
//   1. 保角翻转(edge flip): 在不改变填充区域的前提下交换对角线，提高最小角(Delaunay 化)；
//   2. 三角面合并成四边形: 相邻两个三角面若拼成的四边形是凸的且质量达标，就合成一个四边零件，
//      零件数直接减半；
//   3. 边界简化: 去掉近共线顶点，避免沿直边产生大量"薄片"。
#ifndef NATG_MESH_H
#define NATG_MESH_H

#include "geometry.h"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace natg {

// 一个零件: 3 或 4 个顶点(逆时针，首尾不重复)
struct Polygon {
	std::vector<Vec2> pts;
};

struct PolyMesh {
	std::vector<Vec2> verts;
	std::vector<int> tris;   // 每 3 个一组，逆时针
};

struct MeshOptOptions {
	bool flipEdges = true;             // 是否做保角翻转
	int flipPasses = 12;               // 翻转迭代轮数上限
	bool mergeQuads = true;            // 是否合并四边形
	double mergeMinAngleDeg = 20.0;    // 拼成四边形后的质量门限(直接合并)
	double mergeImproveMinDeg = 5.0;   // "不恶化"合并的绝对下限
	double mergeImproveGainDeg = 0.5;  // 相对原三角面最差角需要提升的最小幅度
};

struct MeshOptStats {
	size_t trianglesIn = 0;
	size_t flips = 0;
	size_t merges = 0;
	size_t quads = 0;
	size_t parts = 0;
	size_t sliverIn = 0;     // 最小角 < kSliverAngleDeg 的输入三角面
	size_t sliverOut = 0;    // 最小角 < kSliverAngleDeg 的输出零件
	size_t bigThinIn = 0;    // 其中又长又薄的(最长边 >= kBigThinEdgeEm)
	size_t bigThinOut = 0;
	double areaIn = 0.0;     // 输入总面积(em^2)
	double areaOut = 0.0;
	double bigThinAreaIn = 0.0;   // 大细长面占的面积
	double bigThinAreaOut = 0.0;
	double minAngleIn = 0.0;
	double minAngleOut = 0.0;
};

extern const double kSliverAngleDeg;   // 细长判定阈值(度)
extern const double kBigThinEdgeEm;    // "大细长面"的最长边下限(em)

// 无向边的键(用于判断是否为多边形边界边)
uint64_t edgeKey(int a, int b);

// 在一点处的最小内角(度)
double minAngleDegTri(const Vec2 &a, const Vec2 &b, const Vec2 &c);
double minAngleDeg(const Polygon &p);

// 网格 -> 零件(三角面 + 四边形)
std::vector<Polygon> meshToPolygons(const PolyMesh &mesh, const std::unordered_set<uint64_t> &boundaryEdges,
                                    const MeshOptOptions &opt, MeshOptStats *stats);

// 迭代去掉近共线顶点(弦高 <= eps 的点)，减少薄片
void simplifyRing(Ring &ring, double eps);

} // namespace natg

#endif // NATG_MESH_H
