#include "triangulate.h"

#include "earcut.h"

#include <cmath>

namespace natg {

void triangulateRegionToMesh(const PolygonRegion &region, PolyMesh &mesh,
                             std::unordered_set<uint64_t> &boundaryEdges,
                             double simplifyEps, TriangulateStats *stats) {
	TriangulateStats st;

	Ring outer = region.outer;
	if (simplifyEps > 0.0) simplifyRing(outer, simplifyEps);
	dedupeRing(outer, 1e-12);
	removeCollinear(outer, 1e-12);
	if (outer.size() < 3) {
		if (stats) *stats = st;
		return;
	}

	// 组装 earcut 需要的交错坐标数组: 外环在前，孔洞依次在后
	std::vector<double> coords;
	coords.reserve((outer.size() + 32) * 2);
	std::vector<size_t> holeStarts;
	std::vector<size_t> ringSizes;   // 每个环的顶点数(用于记录边界边)
	size_t vertexCount = 0;

	for (const Vec2 &p : outer) {
		coords.push_back(p.x);
		coords.push_back(p.y);
		vertexCount++;
	}
	ringSizes.push_back(outer.size());

	for (const Ring &src : region.holes) {
		Ring h = src;
		if (simplifyEps > 0.0) simplifyRing(h, simplifyEps);
		dedupeRing(h, 1e-12);
		removeCollinear(h, 1e-12);
		if (h.size() < 3 || std::fabs(ringSignedArea(h)) < 1e-12) {
			st.holesDropped++;
			continue;
		}
		holeStarts.push_back(vertexCount);
		for (const Vec2 &p : h) {
			coords.push_back(p.x);
			coords.push_back(p.y);
			vertexCount++;
		}
		ringSizes.push_back(h.size());
		st.holes++;
	}

	std::vector<int> idx = earcut::triangulate(coords, holeStarts);
	st.regions = 1;

	// 顶点追加到 mesh
	const int base = static_cast<int>(mesh.verts.size());
	for (size_t i = 0; i < vertexCount; i++) {
		mesh.verts.push_back(Vec2(coords[2 * i], coords[2 * i + 1]));
	}

	// 记录边界边(外环 + 各孔洞的相邻顶点)
	{
		size_t offset = 0;
		for (size_t size : ringSizes) {
			for (size_t k = 0; k < size; k++) {
				const int a = base + static_cast<int>(offset + k);
				const int b = base + static_cast<int>(offset + (k + 1) % size);
				boundaryEdges.insert(edgeKey(a, b));
			}
			offset += size;
		}
	}

	for (size_t i = 0; i + 2 < idx.size(); i += 3) {
		int ia = base + idx[i];
		int ib = base + idx[i + 1];
		int ic = base + idx[i + 2];
		const Vec2 &a = mesh.verts[static_cast<size_t>(ia)];
		const Vec2 &b = mesh.verts[static_cast<size_t>(ib)];
		const Vec2 &c = mesh.verts[static_cast<size_t>(ic)];
		const double area = cross(a, b, c);
		if (std::fabs(area) < 1e-13) {
			st.degenerate++;
			continue;
		}
		if (area < 0.0) std::swap(ib, ic);   // 统一为逆时针
		mesh.tris.push_back(ia);
		mesh.tris.push_back(ib);
		mesh.tris.push_back(ic);
		st.triangles++;
	}

	if (stats) *stats = st;
}

void triangulateContoursToMesh(const std::vector<Ring> &contours, PolyMesh &mesh,
                               std::unordered_set<uint64_t> &boundaryEdges,
                               double simplifyEps, TriangulateStats *stats) {
	PolygonizeStats pst;
	std::vector<PolygonRegion> regions = buildRegions(contours, &pst);
	TriangulateStats total;
	for (const PolygonRegion &r : regions) {
		TriangulateStats st;
		triangulateRegionToMesh(r, mesh, boundaryEdges, simplifyEps, &st);
		total.regions += st.regions;
		total.holes += st.holes;
		total.holesDropped += st.holesDropped;
		total.triangles += st.triangles;
		total.degenerate += st.degenerate;
	}
	if (stats) *stats = total;
}

} // namespace natg
