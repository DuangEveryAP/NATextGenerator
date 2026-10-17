// NATextGenerator - NavalArt 存档(.na)写出
// NavalArt 的 <part> 由 8 个顶点构成一个六面体(前 4 点 + 后 4 点)，
// 顶点编号约定为 A,B,C,D (前) 与 I,II,III,IV (后)，面序为 A,B,D,C，
// 也就是"行优先": A 左上、B 右上、C 左下、D 右下。
//
// 零件可以是四边形，也可以是三角面(让 C 与 D 重合即可)；
// 顶点最终位置由 PlacementOptions 决定(平面或贝塞尔曲面)。
#ifndef NATG_NA_WRITER_H
#define NATG_NA_WRITER_H

#include "geometry.h"
#include "mesh.h"
#include "placement.h"

#include <string>
#include <vector>

namespace natg {

struct PartSettings {
	std::string colorHex = "6F7275";
	double armor = 5.0;
	int partId = 701;
};

struct WriterStats {
	size_t parts = 0;       // 实际写出的零件数
	size_t skipped = 0;     // 因退化被跳过的
};

// parts 为 em 单位、逆时针的多边形(3 或 4 个顶点)
bool writeNaFile(const std::string &path, const std::string &description, const std::vector<Polygon> &parts,
                 const PlacementOptions &placement, const PartSettings &settings, WriterStats *stats,
                 std::string &err);

// 把多边形的 4 个角按"行优先"角色算出前后脸的世界坐标(米)
// A 左上、B 右上、C 左下、D 右下(与旧版约定一致)
void partCorners(const PlacementOptions &placement, const Polygon &poly,
                 Vec3 front[4], Vec3 back[4]);

// 只算角色分配(em 单位)，便于测试与预览
void assignLegacyCorners(const Polygon &poly, Vec2 out[4]);

// XML 属性值转义
std::string escapeXml(const std::string &s);

} // namespace natg

#endif // NATG_NA_WRITER_H
