#include "polygonize.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace natg {
namespace {

struct RingInfo {
	const Ring *ring = nullptr;
	double areaAbs = 0.0;   // |有符号面积|
	double areaSigned = 0.0;
	Vec2 probe;             // 严格位于内部的测试点
	bool hasProbe = false;
	int parent = -1;        // 直接包含它的最小环
	int depth = 0;          // 嵌套深度: 0 = 最外层
};

} // namespace

bool findInteriorPoint(const Ring &ring, Vec2 &out) {
	const size_t n = ring.size();
	if (n < 3) return false;

	double minx = ring[0].x, maxx = ring[0].x, miny = ring[0].y, maxy = ring[0].y;
	for (const Vec2 &p : ring) {
		minx = std::min(minx, p.x);
		maxx = std::max(maxx, p.x);
		miny = std::min(miny, p.y);
		maxy = std::max(maxy, p.y);
	}
	const double diag = std::sqrt((maxx - minx) * (maxx - minx) + (maxy - miny) * (maxy - miny));
	if (!(diag > 0.0)) return false;

	// 先试包围盒中心
	Vec2 center((minx + maxx) * 0.5, (miny + maxy) * 0.5);
	if (pointInRing(center, ring, diag * 1e-12) == 1) {
		out = center;
		return true;
	}

	// 再沿各边中点向"内部"偏移采样。逆时针环的内部在边方向左侧。
	const double ccw = ringSignedArea(ring) > 0.0 ? 1.0 : -1.0;
	double eps = diag * 1e-4;
	for (int attempt = 0; attempt < 12; attempt++) {
		for (size_t i = 0, j = n - 1; i < n; j = i++) {
			const Vec2 &a = ring[j];
			const Vec2 &b = ring[i];
			Vec2 dir = b - a;
			double len = length(dir);
			if (len <= 0.0) continue;
			dir = dir * (1.0 / len);
			Vec2 left(-dir.y, dir.x);
			Vec2 mid((a.x + b.x) * 0.5, (a.y + b.y) * 0.5);
			Vec2 cand = mid + left * (eps * ccw);
			for (int k = 0; k < 6; k++) {
				int r = pointInRing(cand, ring, diag * 1e-12);
				if (r == 1) {
					out = cand;
					return true;
				}
				if (r == -1) break;      // 已经跑到外面，换一条边
				cand = mid + left * (eps * ccw * std::pow(2.0, k + 1));
			}
		}
		eps *= 0.2;
	}
	return false;
}

std::vector<PolygonRegion> buildRegions(const std::vector<Ring> &contours, PolygonizeStats *stats) {
	PolygonizeStats st;
	st.ringsIn = static_cast<int>(contours.size());

	std::vector<Ring> rings;
	rings.reserve(contours.size());
	std::vector<RingInfo> infos;
	infos.reserve(contours.size());

	for (const Ring &src : contours) {
		Ring r = src;
		dedupeRing(r, 1e-9);
		removeCollinear(r, 1e-9);
		double a = ringSignedArea(r);
		if (r.size() < 3 || std::fabs(a) < 1e-12) {
			st.ringsDropped++;
			continue;
		}
		rings.push_back(std::move(r));
		RingInfo info;
		info.areaSigned = a;
		info.areaAbs = std::fabs(a);
		infos.push_back(info);
	}
	for (size_t i = 0; i < rings.size(); i++) {
		infos[i].ring = &rings[i];
		infos[i].hasProbe = findInteriorPoint(rings[i], infos[i].probe);
	}

	// 按面积从大到小建立包含关系: 每个环的父环 = 包含它且面积最小的环
	const size_t m = infos.size();
	std::vector<size_t> order(m);
	std::iota(order.begin(), order.end(), size_t(0));
	std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
		return infos[a].areaAbs > infos[b].areaAbs;
	});

	for (size_t oi = 0; oi < m; oi++) {
		RingInfo &cur = infos[order[oi]];
		if (!cur.hasProbe) continue;
		int bestParent = -1;
		double bestArea = 1e300;
		for (size_t oj = 0; oj < oi; oj++) {           // 只看面积更大的环
			RingInfo &cand = infos[order[oj]];
			if (cand.areaAbs <= cur.areaAbs) continue;
			if (cand.areaAbs >= bestArea) continue;
			if (pointInRing(cur.probe, *cand.ring) == 1) {
				bestParent = static_cast<int>(order[oj]);
				bestArea = cand.areaAbs;
			}
		}
		cur.parent = bestParent;
	}

	// 深度 = 父链长度
	for (size_t i = 0; i < m; i++) {
		int d = 0;
		int p = infos[i].parent;
		int guard = 0;
		while (p >= 0 && guard++ < static_cast<int>(m)) {
			d++;
			p = infos[p].parent;
		}
		infos[i].depth = d;
	}

	std::vector<PolygonRegion> regions;
	std::vector<int> regionOf(m, -1);   // 环 -> 所属实心区域

	for (size_t oi = 0; oi < m; oi++) {
		const size_t idx = order[oi];
		RingInfo &info = infos[idx];
		if (info.depth % 2 != 0) continue;   // 孔洞稍后统一挂到外层上

		PolygonRegion region;
		region.outer = rings[idx];
		// 统一成逆时针
		if (ringSignedArea(region.outer) < 0.0) {
			std::reverse(region.outer.begin(), region.outer.end());
		}
		regionOf[idx] = static_cast<int>(regions.size());
		regions.push_back(std::move(region));
	}

	// 每个孔洞挂到最近的偶数深度祖先(即它所在的实心区域)上
	for (size_t i = 0; i < m; i++) {
		RingInfo &info = infos[i];
		if (info.depth % 2 == 0) continue;
		int p = info.parent;
		while (p >= 0 && infos[p].depth % 2 != 0) p = infos[p].parent;
		if (p < 0) continue;   // 理论上不会发生
		int regionIdx = regionOf[p];
		if (regionIdx < 0) continue;

		Ring hole = rings[i];
		// 孔洞统一成顺时针
		if (ringSignedArea(hole) > 0.0) {
			std::reverse(hole.begin(), hole.end());
		}
		regions[regionIdx].holes.push_back(std::move(hole));
	}

	st.regions = static_cast<int>(regions.size());
	for (const PolygonRegion &r : regions) st.holes += static_cast<int>(r.holes.size());
	if (stats) *stats = st;
	return regions;
}

} // namespace natg
