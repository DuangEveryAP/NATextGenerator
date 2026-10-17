#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""度量 .na 的网格质量：零件数、三/四边形数、细长零件(按尺寸分档)。

零件顶点约定: a(左上) b(右上) c(左下) d(右下)，面序为 a,b,d,c。
三角面用一对重合顶点表示(c == d)。

"大细长面" = 最小角 < angle 且 最长边 >= --big-edge (em，默认 0.08)。
这类零件在游戏里最显眼(长针形)，小薄片则多位于曲线密集处、几乎看不出来。

用法: python tests/quality.py <na文件> [--size 10] [--min-angle 5] [--big-edge 0.08] [--json]
"""
import argparse
import math
import sys
import xml.etree.ElementTree as ET


def parse_polys(path):
    tree = ET.parse(path)
    out = []
    for part in tree.getroot().iter('part'):
        vd = part.find('vertexData')
        if vd is None:
            continue
        pts = [(float(vd.get(n + 'z')), float(vd.get(n + 'y'))) for n in ('a', 'b', 'c', 'd')]
        poly = [pts[0], pts[1], pts[3], pts[2]]      # a,b,d,c
        uniq = []
        for p in poly:
            if not uniq or dis(p, uniq[-1]) > 1e-9:
                uniq.append(p)
        while len(uniq) > 1 and dis(uniq[0], uniq[-1]) <= 1e-9:
            uniq.pop()
        out.append(uniq)
    return out


def dis(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def angle_at(p, q, r):
    ux, uy = p[0] - q[0], p[1] - q[1]
    vx, vy = r[0] - q[0], r[1] - q[1]
    lu = math.hypot(ux, uy)
    lv = math.hypot(vx, vy)
    if lu <= 0 or lv <= 0:
        return 0.0
    c = (ux * vx + uy * vy) / (lu * lv)
    c = max(-1.0, min(1.0, c))
    return math.degrees(math.acos(c))


def poly_min_angle(poly):
    n = len(poly)
    return min(angle_at(poly[(i - 1) % n], poly[i], poly[(i + 1) % n]) for i in range(n))


def poly_area(poly):
    s = 0.0
    n = len(poly)
    for i in range(n):
        x1, y1 = poly[i]
        x2, y2 = poly[(i + 1) % n]
        s += x1 * y2 - x2 * y1
    return s * 0.5


def poly_longest_edge(poly):
    n = len(poly)
    return max(dis(poly[i], poly[(i + 1) % n]) for i in range(n))


def poly_aspect(poly):
    n = len(poly)
    longest = poly_longest_edge(poly)
    area = abs(poly_area(poly))
    if longest <= 0 or area <= 0:
        return float('inf')
    return longest / (2.0 * area / longest)


def analyze(path, size=10.0, min_angle=5.0, big_edge=0.08):
    polys = parse_polys(path)
    n = len(polys)
    res = {
        'parts': n,
        'tris': 0,
        'quads': 0,
        'degenerate': 0,
        'thin': 0,          # 最小角 < min_angle
        'bigThin': 0,       # 最小角 < min_angle 且 最长边 >= big_edge(em)
        'bigThinArea': 0.0,
        'totalArea': 0.0,
        'minAngleAvg': 0.0,
        'minAngleWorst': 0.0,
        'bigThinList': [],
    }
    if n == 0:
        return res
    angles = []
    total_area = 0.0
    for poly in polys:
        a = poly_min_angle(poly)
        angles.append(a)
        area = abs(poly_area(poly))
        total_area += area
        if len(poly) == 3:
            res['tris'] += 1
        elif len(poly) == 4:
            res['quads'] += 1
        if area < 1e-12:
            res['degenerate'] += 1
        if a < min_angle:
            res['thin'] += 1
            longest_em = poly_longest_edge(poly) / size if size else 0.0
            if longest_em >= big_edge:
                res['bigThin'] += 1
                res['bigThinArea'] += area
                res['bigThinList'].append((area, a, longest_em, len(poly), poly))
    res['totalArea'] = total_area
    res['minAngleAvg'] = sum(angles) / n
    res['minAngleWorst'] = min(angles)
    res['bigThinAreaShare'] = (res['bigThinArea'] / total_area * 100.0) if total_area else 0.0
    res['bigThinList'].sort(key=lambda r: -r[0])
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('na')
    ap.add_argument('--size', type=float, default=10.0, help='字号(米)，用于把边长换算成 em')
    ap.add_argument('--min-angle', type=float, default=5.0)
    ap.add_argument('--big-edge', type=float, default=0.08, help='"大细长面"的最长边下限(em)')
    ap.add_argument('--label', default='')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--top', type=int, default=5)
    args = ap.parse_args()

    r = analyze(args.na, args.size, args.min_angle, args.big_edge)
    if args.json:
        import json
        keys = ('parts', 'tris', 'quads', 'degenerate', 'thin', 'bigThin',
                'bigThinAreaShare', 'minAngleAvg', 'minAngleWorst')
        d = {k: (round(r[k], 4) if isinstance(r[k], float) else r[k]) for k in keys}
        d['label'] = args.label
        print(json.dumps(d, ensure_ascii=False))
        return 0

    n = r['parts']
    pc = (lambda x: 100.0 * x / n if n else 0.0)
    print('零件数        %d  (三角 %d, 四边 %d, 退化 %d)' % (n, r['tris'], r['quads'], r['degenerate']))
    print('最小角        平均 %.1f°, 最差 %.2f°' % (r['minAngleAvg'], r['minAngleWorst']))
    print('细长面(<%.0f°)   %d (%.1f%%)' % (args.min_angle, r['thin'], pc(r['thin'])))
    print('大细长面        %d (%.1f%%), 占面积 %.3f%%  [最长边>=%.2f em]' % (
        r['bigThin'], pc(r['bigThin']), r['bigThinAreaShare'], args.big_edge))
    if r['bigThinList'] and args.top > 0:
        print('  面积最大的几个:')
        for area, a, longest, np_, poly in r['bigThinList'][:args.top]:
            print('    面积 %.2e  最小角 %.2f°  最长边 %.2f em  %d边形' % (area, a, longest, np_))
    return 0


if __name__ == '__main__':
    sys.exit(main())
