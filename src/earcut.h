// earcut 算法的 C++ 实现(带孔洞的多边形三角化)
//
// 算法来自 Mapbox 的 earcut (https://github.com/mapbox/earcut)，
// 原始版本 Copyright (c) 2016 Mapbox, 以 ISC 许可证发布。
// 这里按 earcut 2.2.4 的流程重写为自包含的 C++ 代码，供 NATextGenerator 内嵌使用：
//   * 用环形链表表示多边形，耳切失败时依次尝试
//     清理自交点(cureLocalIntersections) -> 沿对角线拆分(splitEarcut)
//   * 孔洞通过 findHoleBridge 找"可见桥"接进外环，再用 filterPoints 合并共线点
//   * 顶点较多时用 z-order 曲线哈希加速点的包含测试
//
// 输入: 交错存放的 (x, y) 数组 + 每个孔洞的起始顶点下标
// 输出: 三角形顶点下标三元组(下标以"顶点"为单位，不是浮点数下标)
//
// 注意: 输入的绕向会被自动归一化，输出三角形的绕向不做保证，
//       调用方需要自行按需求统一(例如统一成逆时针)。
#ifndef NATG_EARCUT_H
#define NATG_EARCUT_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

namespace natg {
namespace earcut {

struct Node {
	double x = 0.0;
	double y = 0.0;
	uint32_t i = 0;      // 顶点下标(以顶点为单位)
	Node *prev = nullptr;
	Node *next = nullptr;
	uint32_t z = 0;      // z-order 曲线值
	Node *prevZ = nullptr;
	Node *nextZ = nullptr;
	bool steiner = false;
};

namespace detail {

inline double area(const Node *p, const Node *q, const Node *r) {
	return (q->y - p->y) * (r->x - q->x) - (q->x - p->x) * (r->y - q->y);
}

inline bool equals(const Node *p1, const Node *p2) {
	return p1->x == p2->x && p1->y == p2->y;
}

inline int sign(double num) {
	return num > 0 ? 1 : (num < 0 ? -1 : 0);
}

inline bool onSegment(const Node *p, const Node *q, const Node *r) {
	return q->x <= std::max(p->x, r->x) && q->x >= std::min(p->x, r->x) &&
	       q->y <= std::max(p->y, r->y) && q->y >= std::min(p->y, r->y);
}

inline bool intersects(const Node *p1, const Node *q1, const Node *p2, const Node *q2) {
	const int o1 = sign(area(p1, q1, p2));
	const int o2 = sign(area(p1, q1, q2));
	const int o3 = sign(area(p2, q2, p1));
	const int o4 = sign(area(p2, q2, q1));
	if (o1 != o2 && o3 != o4) return true;
	if (o1 == 0 && onSegment(p1, p2, q1)) return true;
	if (o2 == 0 && onSegment(p1, q2, q1)) return true;
	if (o3 == 0 && onSegment(p2, p1, q2)) return true;
	if (o4 == 0 && onSegment(p2, q1, q2)) return true;
	return false;
}

inline bool intersectsPolygon(const Node *a, const Node *b) {
	const Node *p = a;
	do {
		if (p->i != a->i && p->next->i != a->i && p->i != b->i && p->next->i != b->i &&
		        intersects(p, p->next, a, b)) {
			return true;
		}
		p = p->next;
	} while (p != a);
	return false;
}

inline bool locallyInside(const Node *a, const Node *b) {
	return area(a->prev, a, a->next) < 0
	       ? (area(a, b, a->next) >= 0 && area(a, a->prev, b) >= 0)
	       : (area(a, b, a->prev) < 0 || area(a, a->next, b) < 0);
}

inline bool middleInside(const Node *a, const Node *b) {
	const Node *p = a;
	bool inside = false;
	const double px = (a->x + b->x) / 2.0;
	const double py = (a->y + b->y) / 2.0;
	do {
		if (((p->y > py) != (p->next->y > py)) && p->next->y != p->y &&
		        (px < (p->next->x - p->x) * (py - p->y) / (p->next->y - p->y) + p->x)) {
			inside = !inside;
		}
		p = p->next;
	} while (p != a);
	return inside;
}

inline bool pointInTriangle(double ax, double ay, double bx, double by, double cx, double cy,
                            double px, double py) {
	return (cx - px) * (ay - py) >= (ax - px) * (cy - py) &&
	       (ax - px) * (by - py) >= (bx - px) * (ay - py) &&
	       (bx - px) * (cy - py) >= (cx - px) * (by - py);
}

inline bool isValidDiagonal(const Node *a, const Node *b) {
	return a->next->i != b->i && a->prev->i != b->i && !intersectsPolygon(a, b) &&
	       ((locallyInside(a, b) && locallyInside(b, a) && middleInside(a, b) &&
	         (area(a->prev, a, b->prev) != 0.0 || area(a, b->prev, b) != 0.0)) ||
	        (equals(a, b) && area(a->prev, a, a->next) > 0 && area(b->prev, b, b->next) > 0));
}

struct Context {
	std::deque<Node> pool;
	std::vector<double> *out = nullptr;   // 交错坐标
	uint32_t dim = 2;

	Node *alloc(uint32_t i, double x, double y) {
		pool.push_back(Node());
		Node *n = &pool.back();
		n->i = i;
		n->x = x;
		n->y = y;
		return n;
	}

	Node *insertNode(uint32_t i, double x, double y, Node *last) {
		Node *p = alloc(i, x, y);
		if (!last) {
			p->prev = p;
			p->next = p;
		} else {
			p->next = last->next;
			p->prev = last;
			last->next->prev = p;
			last->next = p;
		}
		return p;
	}

	void removeNode(Node *p) {
		p->next->prev = p->prev;
		p->prev->next = p->next;
		if (p->prevZ) p->prevZ->nextZ = p->nextZ;
		if (p->nextZ) p->nextZ->prevZ = p->prevZ;
	}

	double signedArea(size_t start, size_t end) const {
		const std::vector<double> &d = *out;
		double sum = 0.0;
		for (size_t i = start, j = end - dim; i < end; i += dim) {
			sum += (d[j] - d[i]) * (d[i + 1] + d[j + 1]);
			j = i;
		}
		return sum;
	}

	Node *linkedList(size_t start, size_t end, bool clockwise) {
		const std::vector<double> &d = *out;
		Node *last = nullptr;
		if (clockwise == (signedArea(start, end) > 0)) {
			for (size_t i = start; i < end; i += dim) {
				last = insertNode(static_cast<uint32_t>(i / dim), d[i], d[i + 1], last);
			}
		} else {
			for (long long i = static_cast<long long>(end) - static_cast<long long>(dim);
			        i >= static_cast<long long>(start); i -= static_cast<long long>(dim)) {
				const size_t k = static_cast<size_t>(i);
				last = insertNode(static_cast<uint32_t>(k / dim), d[k], d[k + 1], last);
			}
		}
		if (last && equals(last, last->next)) {
			removeNode(last);
			last = last->next;
		}
		return last;
	}

	Node *filterPoints(Node *start, Node *end = nullptr) {
		if (!start) return start;
		if (!end) end = start;
		Node *p = start;
		bool again;
		do {
			again = false;
			if (!p->steiner && (equals(p, p->next) || area(p->prev, p, p->next) == 0.0)) {
				removeNode(p);
				p = end = p->prev;
				if (p == p->next) break;
				again = true;
			} else {
				p = p->next;
			}
		} while (again || p != end);
		return end;
	}

	Node *splitPolygon(Node *a, Node *b) {
		Node *a2 = alloc(a->i, a->x, a->y);
		Node *b2 = alloc(b->i, b->x, b->y);
		Node *an = a->next;
		Node *bp = b->prev;

		a->next = b;
		b->prev = a;

		a2->next = an;
		an->prev = a2;

		b2->next = a2;
		a2->prev = b2;

		bp->next = b2;
		b2->prev = bp;

		return b2;
	}

	static uint32_t zOrder(double x, double y, double minX, double minY, double invSize) {
		uint32_t ix = static_cast<uint32_t>((x - minX) * invSize);
		uint32_t iy = static_cast<uint32_t>((y - minY) * invSize);
		ix = (ix | (ix << 8)) & 0x00FF00FF;
		ix = (ix | (ix << 4)) & 0x0F0F0F0F;
		ix = (ix | (ix << 2)) & 0x33333333;
		ix = (ix | (ix << 1)) & 0x55555555;
		iy = (iy | (iy << 8)) & 0x00FF00FF;
		iy = (iy | (iy << 4)) & 0x0F0F0F0F;
		iy = (iy | (iy << 2)) & 0x33333333;
		iy = (iy | (iy << 1)) & 0x55555555;
		return ix | (iy << 1);
	}

	void indexCurve(Node *start, double minX, double minY, double invSize) {
		Node *p = start;
		std::vector<Node *> nodes;
		do {
			if (p->z == 0) p->z = zOrder(p->x, p->y, minX, minY, invSize);
			p->prevZ = p->prev;
			p->nextZ = p->next;
			nodes.push_back(p);
			p = p->next;
		} while (p != start);

		std::stable_sort(nodes.begin(), nodes.end(), [](const Node *a, const Node *b) {
			return a->z < b->z;
		});
		for (size_t i = 0; i < nodes.size(); i++) {
			nodes[i]->prevZ = (i > 0) ? nodes[i - 1] : nullptr;
			nodes[i]->nextZ = (i + 1 < nodes.size()) ? nodes[i + 1] : nullptr;
		}
	}

	bool isEar(Node *ear) {
		Node *a = ear->prev;
		Node *b = ear;
		Node *c = ear->next;
		if (area(a, b, c) >= 0) return false;   // 凹点或退化，不是耳朵

		const double ax = a->x, bx = b->x, cx = c->x;
		const double ay = a->y, by = b->y, cy = c->y;
		const double x0 = std::min(ax, std::min(bx, cx));
		const double y0 = std::min(ay, std::min(by, cy));
		const double x1 = std::max(ax, std::max(bx, cx));
		const double y1 = std::max(ay, std::max(by, cy));

		Node *p = c->next;
		while (p != a) {
			if (p->x >= x0 && p->x <= x1 && p->y >= y0 && p->y <= y1 &&
			        pointInTriangle(ax, ay, bx, by, cx, cy, p->x, p->y) &&
			        area(p->prev, p, p->next) >= 0) {
				return false;
			}
			p = p->next;
		}
		return true;
	}

	bool isEarHashed(Node *ear, double minX, double minY, double invSize) {
		Node *a = ear->prev;
		Node *b = ear;
		Node *c = ear->next;
		if (area(a, b, c) >= 0) return false;

		const double ax = a->x, bx = b->x, cx = c->x;
		const double ay = a->y, by = b->y, cy = c->y;
		const double x0 = std::min(ax, std::min(bx, cx));
		const double y0 = std::min(ay, std::min(by, cy));
		const double x1 = std::max(ax, std::max(bx, cx));
		const double y1 = std::max(ay, std::max(by, cy));

		const uint32_t minZ = zOrder(x0, y0, minX, minY, invSize);
		const uint32_t maxZ = zOrder(x1, y1, minX, minY, invSize);

		Node *p = ear->prevZ;
		Node *n = ear->nextZ;

		while (p && p->z >= minZ && n && n->z <= maxZ) {
			if (p->x >= x0 && p->x <= x1 && p->y >= y0 && p->y <= y1 && p != a && p != c &&
			        pointInTriangle(ax, ay, bx, by, cx, cy, p->x, p->y) &&
			        area(p->prev, p, p->next) >= 0) {
				return false;
			}
			p = p->prevZ;
			if (n->x >= x0 && n->x <= x1 && n->y >= y0 && n->y <= y1 && n != a && n != c &&
			        pointInTriangle(ax, ay, bx, by, cx, cy, n->x, n->y) &&
			        area(n->prev, n, n->next) >= 0) {
				return false;
			}
			n = n->nextZ;
		}
		while (p && p->z >= minZ) {
			if (p->x >= x0 && p->x <= x1 && p->y >= y0 && p->y <= y1 && p != a && p != c &&
			        pointInTriangle(ax, ay, bx, by, cx, cy, p->x, p->y) &&
			        area(p->prev, p, p->next) >= 0) {
				return false;
			}
			p = p->prevZ;
		}
		while (n && n->z <= maxZ) {
			if (n->x >= x0 && n->x <= x1 && n->y >= y0 && n->y <= y1 && n != a && n != c &&
			        pointInTriangle(ax, ay, bx, by, cx, cy, n->x, n->y) &&
			        area(n->prev, n, n->next) >= 0) {
				return false;
			}
			n = n->nextZ;
		}
		return true;
	}

	Node *cureLocalIntersections(Node *start, std::vector<int> &triangles) {
		Node *p = start;
		do {
			Node *a = p->prev;
			Node *b = p->next->next;
			if (!equals(a, b) && intersects(a, p, p->next, b) && locallyInside(a, b) &&
			        locallyInside(b, a)) {
				triangles.push_back(static_cast<int>(a->i));
				triangles.push_back(static_cast<int>(p->i));
				triangles.push_back(static_cast<int>(b->i));
				removeNode(p);
				removeNode(p->next);
				p = start = b;
			}
			p = p->next;
		} while (p != start);
		return filterPoints(p);
	}

	void splitEarcut(Node *start, std::vector<int> &triangles, double minX, double minY, double invSize) {
		Node *a = start;
		do {
			Node *b = a->next->next;
			while (b != a->prev) {
				if (a->i != b->i && isValidDiagonal(a, b)) {
					Node *c = splitPolygon(a, b);
					a = filterPoints(a, a->next);
					c = filterPoints(c, c->next);
					earcutLinked(a, triangles, minX, minY, invSize, 0);
					earcutLinked(c, triangles, minX, minY, invSize, 0);
					return;
				}
				b = b->next;
			}
			a = a->next;
		} while (a != start);
	}

	void earcutLinked(Node *ear, std::vector<int> &triangles, double minX, double minY, double invSize,
	                  int pass) {
		if (!ear) return;
		if (!pass && invSize != 0.0) indexCurve(ear, minX, minY, invSize);

		Node *stop = ear;
		while (ear->prev != ear->next) {
			Node *prev = ear->prev;
			Node *next = ear->next;

			if (invSize != 0.0 ? isEarHashed(ear, minX, minY, invSize) : isEar(ear)) {
				triangles.push_back(static_cast<int>(prev->i));
				triangles.push_back(static_cast<int>(ear->i));
				triangles.push_back(static_cast<int>(next->i));
				removeNode(ear);
				ear = next->next;
				stop = next->next;
				continue;
			}

			ear = next;
			if (ear == stop) {
				if (!pass) {
					earcutLinked(filterPoints(ear), triangles, minX, minY, invSize, 1);
				} else if (pass == 1) {
					ear = cureLocalIntersections(filterPoints(ear), triangles);
					earcutLinked(ear, triangles, minX, minY, invSize, 2);
				} else if (pass == 2) {
					splitEarcut(ear, triangles, minX, minY, invSize);
				}
				break;
			}
		}
	}

	Node *getLeftmost(Node *start) {
		Node *p = start;
		Node *leftmost = start;
		do {
			if (p->x < leftmost->x || (p->x == leftmost->x && p->y < leftmost->y)) leftmost = p;
			p = p->next;
		} while (p != start);
		return leftmost;
	}

	Node *findHoleBridge(Node *hole, Node *outerNode) {
		Node *p = outerNode;
		const double hx = hole->x;
		const double hy = hole->y;
		double qx = -1e300;
		Node *m = nullptr;

		// 从孔洞最左点向左发射线，找到第一个被穿过的外环边
		do {
			if (hy <= p->y && hy >= p->next->y && p->next->y != p->y) {
				const double x = p->x + (hy - p->y) * (p->next->x - p->x) / (p->next->y - p->y);
				if (x <= hx && x > qx) {
					qx = x;
					m = p->x < p->next->x ? p : p->next;
					if (x == hx) return m;   // 命中孔洞顶点本身
				}
			}
			p = p->next;
		} while (p != outerNode);

		if (!m) return nullptr;

		// 在三角形(孔洞点、交点、候选点)内寻找更优的可视顶点
		Node *stop = m;
		const double mx = m->x;
		const double my = m->y;
		double tanMin = 1e300;
		p = m;
		do {
			if (hx >= p->x && p->x >= mx && hx != p->x &&
			        pointInTriangle(hy < my ? hx : qx, hy, mx, my, hy < my ? qx : hx, hy, p->x, p->y)) {
				const double tan = std::fabs(hy - p->y) / (hx - p->x);
				if (locallyInside(p, hole) &&
				        (tan < tanMin || (tan == tanMin && (p->x > m->x ||
				                                            (p->x == m->x && sectorContainsSector(m, p)))))) {
					m = p;
					tanMin = tan;
				}
			}
			p = p->next;
		} while (p != stop);

		return m;
	}

	static bool sectorContainsSector(const Node *m, const Node *p) {
		return area(m->prev, m, p->prev) < 0 && area(p->next, m, m->next) < 0;
	}

	Node *eliminateHole(Node *hole, Node *outerNode) {
		Node *bridge = findHoleBridge(hole, outerNode);
		if (!bridge) return outerNode;
		Node *bridgeReverse = splitPolygon(bridge, hole);
		filterPoints(bridgeReverse, bridgeReverse->next);
		return filterPoints(bridge, bridge->next);
	}

	Node *eliminateHoles(const std::vector<size_t> &holeIndices, Node *outerNode) {
		std::vector<Node *> queue;
		const size_t len = holeIndices.size();
		for (size_t i = 0; i < len; i++) {
			const size_t start = holeIndices[i] * dim;
			const size_t end = (i < len - 1) ? holeIndices[i + 1] * dim : out->size();
			if (end <= start) continue;
			Node *list = linkedList(start, end, false);
			if (!list) continue;
			if (list == list->next) list->steiner = true;
			queue.push_back(getLeftmost(list));
		}
		std::sort(queue.begin(), queue.end(), [](const Node *a, const Node *b) {
			return a->x < b->x;
		});
		for (Node *hole : queue) {
			outerNode = eliminateHole(hole, outerNode);
		}
		return outerNode;
	}

	void run(std::vector<double> &data, const std::vector<size_t> &holeIndices,
	         std::vector<int> &triangles) {
		out = &data;
		const bool hasHoles = !holeIndices.empty();
		const size_t outerLen = hasHoles ? holeIndices[0] * dim : data.size();
		Node *outerNode = linkedList(0, outerLen, true);
		if (!outerNode || outerNode->next == outerNode->prev) return;

		double minX = 0.0, minY = 0.0, invSize = 0.0;
		if (hasHoles) outerNode = eliminateHoles(holeIndices, outerNode);

		// 顶点较多时启用 z-order 哈希加速
		if (data.size() > 80 * dim) {
			double maxX = data[0], maxY = data[1];
			minX = data[0];
			minY = data[1];
			for (size_t i = dim; i < outerLen; i += dim) {
				const double x = data[i];
				const double y = data[i + 1];
				if (x < minX) minX = x;
				if (y < minY) minY = y;
				if (x > maxX) maxX = x;
				if (y > maxY) maxY = y;
			}
			const double span = std::max(maxX - minX, maxY - minY);
			invSize = span != 0.0 ? 32767.0 / span : 0.0;
		}
		earcutLinked(outerNode, triangles, minX, minY, invSize, 0);
	}
};

} // namespace detail

// 三角化: coords 为交错的 (x, y)，holeIndices 为各孔洞的起始顶点下标
inline std::vector<int> triangulate(const std::vector<double> &coords, const std::vector<size_t> &holeIndices) {
	detail::Context ctx;
	std::vector<double> data = coords;
	std::vector<int> triangles;
	ctx.run(data, holeIndices, triangles);
	return triangles;
}

} // namespace earcut
} // namespace natg

#endif // NATG_EARCUT_H
