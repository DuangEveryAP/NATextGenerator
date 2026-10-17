#include "mesh.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace natg {

const double kSliverAngleDeg = 5.0;
const double kBigThinEdgeEm = 0.08;

uint64_t edgeKey(int a, int b) {
	if (a > b) std::swap(a, b);
	return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) |
	       static_cast<uint64_t>(static_cast<uint32_t>(b));
}

double minAngleDegTri(const Vec2 &a, const Vec2 &b, const Vec2 &c) {
	auto angleAt = [](const Vec2 &p, const Vec2 &q, const Vec2 &r) -> double {
		const Vec2 u = p - q;
		const Vec2 v = r - q;
		const double lu = length(u);
		const double lv = length(v);
		if (!(lu > 0.0) || !(lv > 0.0)) return 0.0;
		double cs = dot(u, v) / (lu * lv);
		if (cs > 1.0) cs = 1.0;
		if (cs < -1.0) cs = -1.0;
		return std::acos(cs) * 180.0 / 3.14159265358979323846;
	};
	return std::min(angleAt(a, b, c), std::min(angleAt(b, c, a), angleAt(c, a, b)));
}

double minAngleDeg(const Polygon &p) {
	const size_t n = p.pts.size();
	if (n == 3) return minAngleDegTri(p.pts[0], p.pts[1], p.pts[2]);
	if (n == 4) {
		// 四边形最小内角 = 四个顶点处内角的最小值(要求凸，否则视为 0)
		double best = 180.0;
		for (size_t i = 0; i < 4; i++) {
			const Vec2 &prev = p.pts[(i + 3) % 4];
			const Vec2 &cur = p.pts[i];
			const Vec2 &next = p.pts[(i + 1) % 4];
			if (cross(prev, cur, next) <= 0.0) return 0.0;   // 凹点
			const Vec2 u = prev - cur;
			const Vec2 v = next - cur;
			double cs = dot(u, v) / (length(u) * length(v));
			if (cs > 1.0) cs = 1.0;
			if (cs < -1.0) cs = -1.0;
			const double a = std::acos(cs) * 180.0 / 3.14159265358979323846;
			best = std::min(best, a);
		}
		return best;
	}
	return 0.0;
}

void simplifyRing(Ring &ring, double eps) {
	const size_t n = ring.size();
	if (eps <= 0.0 || n < 4) return;

	// 关键: 每次删点都要用"原始轮廓上的点"来校验偏差，否则会一轮轮累积漂移
	// (曾经因为只和"已简化的折线"比较，导致 q10 下单字被削掉 2% 面积)。
	const std::vector<Vec2> orig = ring;
	std::vector<int> prevIdx(n), nextIdx(n);
	for (size_t i = 0; i < n; i++) {
		prevIdx[i] = static_cast<int>((i + n - 1) % n);
		nextIdx[i] = static_cast<int>((i + 1) % n);
	}
	std::vector<char> alive(n, 1);
	size_t aliveCount = n;

	bool changed = true;
	int pass = 0;
	while (changed && pass++ < 64 && aliveCount > 3) {
		changed = false;
		for (size_t i = 0; i < n && aliveCount > 3; i++) {
			if (!alive[i]) continue;
			const int ii = static_cast<int>(i);
			const int p = prevIdx[ii];
			const int q = nextIdx[ii];
			// 被这次删点"吸收"的原始点: p 与 q 之间的所有原始点(含此前已吸收的)
			bool ok = true;
			for (size_t k = static_cast<size_t>(p + 1) % n; k != static_cast<size_t>(q);
			        k = (k + 1) % n) {
				if (distancePointSegment(orig[k], orig[static_cast<size_t>(p)], orig[static_cast<size_t>(q)]) > eps) {
					ok = false;
					break;
				}
			}
			if (!ok) continue;
			alive[ii] = 0;
			nextIdx[static_cast<size_t>(p)] = q;
			prevIdx[static_cast<size_t>(q)] = p;
			aliveCount--;
			changed = true;
		}
	}

	Ring out;
	out.reserve(aliveCount);
	for (size_t i = 0; i < n; i++) {
		if (alive[i]) out.push_back(ring[i]);
	}
	if (out.size() >= 3) ring.swap(out);
}

namespace {

struct EdgeSlot {
	int a = 0;
	int b = 0;
	int t0 = -1;
	int t1 = -1;
};

struct Adjacency {
	std::vector<EdgeSlot> slots;
	std::unordered_map<uint64_t, int> index;

	void clear() {
		slots.clear();
		index.clear();
	}

	void add(int t, int a, int b) {
		const uint64_t k = edgeKey(a, b);
		auto it = index.find(k);
		if (it == index.end()) {
			EdgeSlot s;
			s.a = std::min(a, b);
			s.b = std::max(a, b);
			s.t0 = t;
			index.emplace(k, static_cast<int>(slots.size()));
			slots.push_back(s);
		} else if (slots[static_cast<size_t>(it->second)].t1 < 0) {
			slots[static_cast<size_t>(it->second)].t1 = t;
		}
	}
};

void buildAdjacency(const std::vector<int> &tris, Adjacency &adj) {
	adj.clear();
	adj.slots.reserve(tris.size());
	for (size_t t = 0; t * 3 + 2 < tris.size(); t++) {
		const int i0 = tris[3 * t];
		const int i1 = tris[3 * t + 1];
		const int i2 = tris[3 * t + 2];
		adj.add(static_cast<int>(t), i0, i1);
		adj.add(static_cast<int>(t), i1, i2);
		adj.add(static_cast<int>(t), i2, i0);
	}
}

inline bool hasDirectedEdge(const std::vector<int> &tris, int t, int from, int to) {
	const int i0 = tris[3 * static_cast<size_t>(t)];
	const int i1 = tris[3 * static_cast<size_t>(t) + 1];
	const int i2 = tris[3 * static_cast<size_t>(t) + 2];
	return (i0 == from && i1 == to) || (i1 == from && i2 == to) || (i2 == from && i0 == to);
}

inline int thirdVertex(const std::vector<int> &tris, int t, int a, int b) {
	const int i0 = tris[3 * static_cast<size_t>(t)];
	const int i1 = tris[3 * static_cast<size_t>(t) + 1];
	const int i2 = tris[3 * static_cast<size_t>(t) + 2];
	if (i0 != a && i0 != b) return i0;
	if (i1 != a && i1 != b) return i1;
	return i2;
}

// 四边形 (p0,p1,p2,p3) 是否严格凸(逆时针)
inline bool isConvexQuad(const Vec2 &p0, const Vec2 &p1, const Vec2 &p2, const Vec2 &p3, double eps) {
	return cross(p0, p1, p2) > eps && cross(p1, p2, p3) > eps &&
	       cross(p2, p3, p0) > eps && cross(p3, p0, p1) > eps;
}

inline double quadMinAngleDeg(const Vec2 &p0, const Vec2 &p1, const Vec2 &p2, const Vec2 &p3) {
	Polygon q;
	q.pts = {p0, p1, p2, p3};
	return minAngleDeg(q);
}

inline double polyLongestEdge(const Polygon &p) {
	const size_t n = p.pts.size();
	double best = 0.0;
	for (size_t i = 0; i < n; i++) {
		best = std::max(best, distance(p.pts[i], p.pts[(i + 1) % n]));
	}
	return best;
}

inline double polyArea(const Polygon &p) {
	const size_t n = p.pts.size();
	double s = 0.0;
	for (size_t i = 0; i < n; i++) {
		const Vec2 &a = p.pts[i];
		const Vec2 &b = p.pts[(i + 1) % n];
		s += a.x * b.y - b.x * a.y;
	}
	return std::fabs(s) * 0.5;
}

} // namespace

std::vector<Polygon> meshToPolygons(const PolyMesh &mesh, const std::unordered_set<uint64_t> &boundaryEdges,
                                    const MeshOptOptions &opt, MeshOptStats *stats) {
	MeshOptStats st;
	const std::vector<Vec2> &V = mesh.verts;
	std::vector<int> tris = mesh.tris;
	st.trianglesIn = tris.size() / 3;

	double eps = 0.0;
	for (const Vec2 &v : V) eps = std::max(eps, std::fabs(v.x) + std::fabs(v.y));
	eps = (eps > 0.0 ? eps : 1.0) * 1e-12;

	// 输入三角面的质量统计
	st.minAngleIn = 180.0;
	for (size_t t = 0; t + 2 < tris.size(); t += 3) {
		Polygon tri;
		tri.pts = {V[static_cast<size_t>(tris[t])], V[static_cast<size_t>(tris[t + 1])],
		           V[static_cast<size_t>(tris[t + 2])]};
		const double a = minAngleDegTri(tri.pts[0], tri.pts[1], tri.pts[2]);
		const double area = polyArea(tri);
		st.minAngleIn = std::min(st.minAngleIn, a);
		st.areaIn += area;
		if (a < kSliverAngleDeg) {
			st.sliverIn++;
			if (polyLongestEdge(tri) >= kBigThinEdgeEm) {
				st.bigThinIn++;
				st.bigThinAreaIn += area;
			}
		}
	}
	if (tris.empty()) st.minAngleIn = 0.0;

	// ---------- 1) 保角翻转 ----------
	if (opt.flipEdges) {
		Adjacency adj;
		for (int pass = 0; pass < opt.flipPasses; pass++) {
			buildAdjacency(tris, adj);
			std::vector<char> dirty(tris.size() / 3, 0);
			bool any = false;

			for (const EdgeSlot &slot : adj.slots) {
				if (slot.t1 < 0) continue;
				if (boundaryEdges.count(edgeKey(slot.a, slot.b))) continue;
				if (dirty[static_cast<size_t>(slot.t0)] || dirty[static_cast<size_t>(slot.t1)]) continue;

				const int u = slot.a;
				const int v = slot.b;
				int tUV = -1;
				int tVU = -1;
				if (hasDirectedEdge(tris, slot.t0, u, v)) {
					tUV = slot.t0;
					tVU = slot.t1;
				} else if (hasDirectedEdge(tris, slot.t1, u, v)) {
					tUV = slot.t1;
					tVU = slot.t0;
				}
				if (tUV < 0 || tVU < 0) continue;
				if (!hasDirectedEdge(tris, tVU, v, u)) continue;   // 朝向不一致，跳过

				const int p = thirdVertex(tris, tUV, u, v);
				const int q = thirdVertex(tris, tVU, u, v);
				const Vec2 &Pu = V[static_cast<size_t>(u)];
				const Vec2 &Pv = V[static_cast<size_t>(v)];
				const Vec2 &Pp = V[static_cast<size_t>(p)];
				const Vec2 &Pq = V[static_cast<size_t>(q)];
				if (!isConvexQuad(Pu, Pq, Pv, Pp, eps)) continue;   // 翻转会产生反向三角形

				const double oldMin = std::min(minAngleDegTri(Pu, Pv, Pp), minAngleDegTri(Pv, Pu, Pq));
				const double newMin = std::min(minAngleDegTri(Pp, Pu, Pq), minAngleDegTri(Pp, Pq, Pv));
				if (newMin <= oldMin + 0.02) continue;

				tris[3 * static_cast<size_t>(tUV)] = p;
				tris[3 * static_cast<size_t>(tUV) + 1] = u;
				tris[3 * static_cast<size_t>(tUV) + 2] = q;
				tris[3 * static_cast<size_t>(tVU)] = p;
				tris[3 * static_cast<size_t>(tVU) + 1] = q;
				tris[3 * static_cast<size_t>(tVU) + 2] = v;
				dirty[static_cast<size_t>(tUV)] = 1;
				dirty[static_cast<size_t>(tVU)] = 1;
				st.flips++;
				any = true;
			}
			if (!any) break;
		}
	}

	// ---------- 2) 合并成四边形 ----------
	std::vector<Polygon> out;
	out.reserve(tris.size() / 3);

	if (opt.mergeQuads && tris.size() >= 6) {
		Adjacency adj;
		buildAdjacency(tris, adj);

		struct Candidate {
			double score = 0.0;
			int t0 = -1;
			int t1 = -1;
			int v0 = 0, v1 = 0, v2 = 0, v3 = 0;
		};
		std::vector<Candidate> cands;
		cands.reserve(adj.slots.size());

		for (const EdgeSlot &slot : adj.slots) {
			if (slot.t1 < 0) continue;
			if (boundaryEdges.count(edgeKey(slot.a, slot.b))) continue;

			const int u = slot.a;
			const int v = slot.b;
			int tUV = -1;
			int tVU = -1;
			if (hasDirectedEdge(tris, slot.t0, u, v)) {
				tUV = slot.t0;
				tVU = slot.t1;
			} else if (hasDirectedEdge(tris, slot.t1, u, v)) {
				tUV = slot.t1;
				tVU = slot.t0;
			}
			if (tUV < 0 || tVU < 0) continue;
			if (!hasDirectedEdge(tris, tVU, v, u)) continue;

			const int p = thirdVertex(tris, tUV, u, v);
			const int q = thirdVertex(tris, tVU, u, v);
			const Vec2 &Pu = V[static_cast<size_t>(u)];
			const Vec2 &Pv = V[static_cast<size_t>(v)];
			const Vec2 &Pp = V[static_cast<size_t>(p)];
			const Vec2 &Pq = V[static_cast<size_t>(q)];

			// 合并后的四边形: u -> q -> v -> p
			if (!isConvexQuad(Pu, Pq, Pv, Pp, eps)) continue;
			const double quadAngle = quadMinAngleDeg(Pu, Pq, Pv, Pp);
			const double oldMin = std::min(minAngleDegTri(Pu, Pv, Pp), minAngleDegTri(Pv, Pu, Pq));

			Candidate c;
			c.t0 = tUV;
			c.t1 = tVU;
			c.v0 = u;
			c.v1 = q;
			c.v2 = v;
			c.v3 = p;

			if (quadAngle >= opt.mergeMinAngleDeg) {
				c.score = quadAngle;              // 一级: 质量达标的四边形
			} else if (quadAngle >= opt.mergeImproveMinDeg &&
			           quadAngle > oldMin + opt.mergeImproveGainDeg) {
				c.score = quadAngle - 1000.0;     // 二级: 至少不恶化，能少一个零件
			} else {
				continue;
			}
			cands.push_back(c);
		}

		std::sort(cands.begin(), cands.end(), [](const Candidate &a, const Candidate &b) {
			return a.score > b.score;
		});

		std::vector<char> used(tris.size() / 3, 0);
		for (const Candidate &c : cands) {
			if (used[static_cast<size_t>(c.t0)] || used[static_cast<size_t>(c.t1)]) continue;
			used[static_cast<size_t>(c.t0)] = 1;
			used[static_cast<size_t>(c.t1)] = 1;
			Polygon quad;
			quad.pts = {V[static_cast<size_t>(c.v0)], V[static_cast<size_t>(c.v1)],
			            V[static_cast<size_t>(c.v2)], V[static_cast<size_t>(c.v3)]};
			out.push_back(std::move(quad));
			st.merges++;
		}

		for (size_t t = 0; t < used.size(); t++) {
			if (used[t]) continue;
			Polygon tri;
			tri.pts = {V[static_cast<size_t>(tris[3 * t])], V[static_cast<size_t>(tris[3 * t + 1])],
			           V[static_cast<size_t>(tris[3 * t + 2])]};
			out.push_back(std::move(tri));
		}
	} else {
		for (size_t t = 0; t + 2 < tris.size(); t += 3) {
			Polygon tri;
			tri.pts = {V[static_cast<size_t>(tris[t])], V[static_cast<size_t>(tris[t + 1])],
			           V[static_cast<size_t>(tris[t + 2])]};
			out.push_back(std::move(tri));
		}
	}

	// ---------- 3) 统计 ----------
	st.minAngleOut = 180.0;
	for (const Polygon &p : out) {
		const double a = minAngleDeg(p);
		const double area = polyArea(p);
		st.minAngleOut = std::min(st.minAngleOut, a);
		st.areaOut += area;
		if (a < kSliverAngleDeg) {
			st.sliverOut++;
			if (polyLongestEdge(p) >= kBigThinEdgeEm) {
				st.bigThinOut++;
				st.bigThinAreaOut += area;
			}
		}
		if (p.pts.size() == 4) st.quads++;
	}
	if (out.empty()) st.minAngleOut = 0.0;
	st.parts = out.size();

	if (stats) *stats = st;
	return out;
}

} // namespace natg
