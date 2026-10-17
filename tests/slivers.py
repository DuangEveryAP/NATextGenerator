#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""诊断: 找出细长零件的位置，并判断它们是否紧贴"桥接缝"(两条零件共用同一条边)。

用法: python tests/slivers.py <na文件> [--min-angle 5] [--limit 12]
"""
import argparse
import math
import sys

sys.path.insert(0, __file__.rsplit('\\', 1)[0].rsplit('/', 1)[0])
import verify  # noqa: E402
import quality  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('na')
    ap.add_argument('--min-angle', type=float, default=5.0)
    ap.add_argument('--limit', type=int, default=12)
    args = ap.parse_args()

    polys = quality.parse_polys(args.na)
    # 建立边的共享计数(判断"缝")
    edge_count = {}
    for poly in polys:
        n = len(poly)
        for i in range(n):
            a = (round(poly[i][0], 6), round(poly[i][1], 6))
            b = (round(poly[(i + 1) % n][0], 6), round(poly[(i + 1) % n][1], 6))
            k = tuple(sorted([a, b]))
            edge_count[k] = edge_count.get(k, 0) + 1

    rows = []
    for poly in polys:
        ang = quality.poly_min_angle(poly)
        if ang >= args.min_angle:
            continue
        n = len(poly)
        # 统计有几条边是"缝"(同一个无向边被两个零件共用, 说明是内部对角线或桥接缝)
        shared = 0
        for i in range(n):
            a = (round(poly[i][0], 6), round(poly[i][1], 6))
            b = (round(poly[(i + 1) % n][0], 6), round(poly[(i + 1) % n][1], 6))
            k = tuple(sorted([a, b]))
            if edge_count.get(k, 0) > 1:
                shared += 1
        rows.append((ang, n, quality.poly_area(poly), quality.poly_aspect(poly), shared, poly))

    rows.sort(key=lambda r: r[0])
    print('细长零件总数: %d / %d (%.1f%%)' % (len(rows), len(polys), 100.0 * len(rows) / max(len(polys), 1)))
    print('%-8s %-3s %-12s %-9s %-5s %s' % ('minAngle', 'n', 'area', 'aspect', 'shared', 'vertices'))
    for ang, n, area, aspect, shared, poly in rows[:args.limit]:
        vs = ' '.join('(%.4f,%.4f)' % p for p in poly)
        print('%-8.3f %-3d %-12.3e %-9.1f %-5d %s' % (ang, n, area, aspect, shared, vs))
    # 统计: 有多少细长件至少有一条边是缝(内部边)
    inner_only = sum(1 for r in rows if r[4] == r[1])
    print('\n其中"所有边都是内部边"的: %d' % inner_only)
    return 0


if __name__ == '__main__':
    sys.exit(main())
