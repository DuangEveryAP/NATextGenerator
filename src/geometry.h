// NATextGenerator - 基础几何类型与谓词
// 本文件提供二维向量、环(闭合折线)、三角形以及若干几何判定。
// 所有坐标均为「em 单位」(1.0 = 一个字号高度)，y 轴向上，基线为 y = 0。
#ifndef NATG_GEOMETRY_H
#define NATG_GEOMETRY_H

#include <cmath>
#include <cstddef>
#include <vector>
#include <algorithm>

namespace natg {

struct Vec2 {
	double x = 0.0;
	double y = 0.0;

	Vec2() = default;
	Vec2(double x_, double y_) : x(x_), y(y_) {}

	Vec2 operator+(const Vec2 &o) const { return Vec2(x + o.x, y + o.y); }
	Vec2 operator-(const Vec2 &o) const { return Vec2(x - o.x, y - o.y); }
	Vec2 operator*(double s) const { return Vec2(x * s, y * s); }
	Vec2 &operator+=(const Vec2 &o) { x += o.x; y += o.y; return *this; }
	bool operator==(const Vec2 &o) const { return x == o.x && y == o.y; }
};

// 闭合环(不做首尾重复点)
using Ring = std::vector<Vec2>;

// 三维点/向量(世界坐标，单位: 米)
struct Vec3 {
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;

	Vec3() = default;
	Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

	Vec3 operator+(const Vec3 &o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
	Vec3 operator-(const Vec3 &o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
	Vec3 operator*(double s) const { return Vec3(x * s, y * s, z * s); }
	Vec3 &operator+=(const Vec3 &o) { x += o.x; y += o.y; z += o.z; return *this; }
};

inline double dot3(const Vec3 &a, const Vec3 &b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross3(const Vec3 &a, const Vec3 &b) {
	return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

inline double length3(const Vec3 &v) {
	return std::sqrt(dot3(v, v));
}

inline Vec3 normalize3(const Vec3 &v) {
	const double len = length3(v);
	if (!(len > 1e-12)) return Vec3(1.0, 0.0, 0.0);
	return Vec3(v.x / len, v.y / len, v.z / len);
}

struct Triangle {
	Vec2 a, b, c;
};

inline double cross(const Vec2 &o, const Vec2 &a, const Vec2 &b) {
	// (a - o) x (b - o)
	return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

inline double crossVec(const Vec2 &u, const Vec2 &v) {
	return u.x * v.y - u.y * v.x;
}

inline double dot(const Vec2 &u, const Vec2 &v) {
	return u.x * v.x + u.y * v.y;
}

inline double length(const Vec2 &v) {
	return std::sqrt(v.x * v.x + v.y * v.y);
}

inline double distance(const Vec2 &a, const Vec2 &b) {
	return length(b - a);
}

// 点到线段的距离
inline double distancePointSegment(const Vec2 &p, const Vec2 &a, const Vec2 &b) {
	Vec2 ab = b - a;
	double len2 = dot(ab, ab);
	if (len2 <= 0.0) return distance(p, a);
	double t = dot(p - a, ab) / len2;
	if (t < 0.0) t = 0.0;
	else if (t > 1.0) t = 1.0;
	Vec2 proj = a + ab * t;
	return distance(p, proj);
}

// 有符号面积(y 轴向上时: >0 表示逆时针 CCW)
inline double ringSignedArea(const Ring &r) {
	const size_t n = r.size();
	if (n < 3) return 0.0;
	double s = 0.0;
	for (size_t i = 0, j = n - 1; i < n; j = i++) {
		s += (r[j].x * r[i].y) - (r[i].x * r[j].y);
	}
	return s * 0.5;
}

inline double triangleSignedArea(const Triangle &t) {
	return cross(t.a, t.b, t.c) * 0.5;
}

// 奇偶规则点在多边形内判定
// 返回 1: 内部, 0: 边界上(容差 eps), -1: 外部
inline int pointInRing(const Vec2 &p, const Ring &r, double eps = 1e-12) {
	const size_t n = r.size();
	if (n < 3) return -1;
	int winding = 0;
	bool onBoundary = false;
	for (size_t i = 0, j = n - 1; i < n; j = i++) {
		const Vec2 &a = r[j];
		const Vec2 &b = r[i];
		// 边界检测
		if (distancePointSegment(p, a, b) <= eps) {
			onBoundary = true;
			break;
		}
		if (((a.y > p.y) != (b.y > p.y))) {
			double xCross = a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y);
			if (p.x < xCross) {
				winding = !winding;
			}
		}
	}
	if (onBoundary) return 0;
	return winding ? 1 : -1;
}

// 严格(不含端点接触)线段相交判定
inline bool segmentsProperlyIntersect(const Vec2 &p1, const Vec2 &p2,
                                      const Vec2 &p3, const Vec2 &p4) {
	const double d1 = cross(p3, p4, p1);
	const double d2 = cross(p3, p4, p2);
	const double d3 = cross(p1, p2, p3);
	const double d4 = cross(p1, p2, p4);
	if (((d1 > 0.0 && d2 < 0.0) || (d1 < 0.0 && d2 > 0.0)) &&
	        ((d3 > 0.0 && d4 < 0.0) || (d3 < 0.0 && d4 > 0.0))) {
		return true;
	}
	return false;
}

// 去掉相邻重复点与首尾重复点
inline void dedupeRing(Ring &r, double eps = 1e-9) {
	if (r.empty()) return;
	Ring out;
	out.reserve(r.size());
	for (const Vec2 &p : r) {
		if (out.empty() || distance(out.back(), p) > eps) {
			out.push_back(p);
		}
	}
	while (out.size() > 1 && distance(out.front(), out.back()) <= eps) {
		out.pop_back();
	}
	r.swap(out);
}

// 移除共线(几乎共线)顶点，减小后续三角面数
inline void removeCollinear(Ring &r, double tol = 1e-7) {
	const size_t n = r.size();
	if (n < 4) return;
	Ring out;
	out.reserve(n);
	for (size_t i = 0; i < n; i++) {
		const Vec2 &prev = r[(i + n - 1) % n];
		const Vec2 &cur = r[i];
		const Vec2 &next = r[(i + 1) % n];
		if (distancePointSegment(cur, prev, next) <= tol) {
			continue; // 共线点，丢弃
		}
		out.push_back(cur);
	}
	if (out.size() >= 3) r.swap(out);
}

} // namespace natg

#endif // NATG_GEOMETRY_H
