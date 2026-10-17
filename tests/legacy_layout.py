#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""检查零件 4 个角点的拓扑是否正确:

游戏按固定拓扑把 8 个角点重建成 6 个面，前脸是 a,b,d,c 这一圈。
所以要求 A,B,D,C 是一条**不自交的凸环**(四个连续叉积同号)，
三角面时 a,b,c 不能退化。若这一圈被打乱(例如按 A,B,C,D 直连)，面就会扭曲/翻向。

平面模式下还额外检查: 前脸法向应朝 +x(与旧版一致)。

用法: python tests/legacy_layout.py <na文件...> [--tol 1e-9]
"""
import argparse
import math
import sys
import xml.etree.ElementTree as ET


def cross(o, a, b):
    return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])


def check(path):
    parts = 0
    ok_quad = 0
    ok_tri = 0
    quads = 0
    tris = 0
    plus = minus = 0
    degenerate = 0
    for part in ET.parse(path).getroot().iter('part'):
        vd = part.find('vertexData')
        if vd is None:
            continue
        g = lambda k: (float(vd.get(k + 'x')), float(vd.get(k + 'y')), float(vd.get(k + 'z')))
        A, B, C, D = g('a'), g('b'), g('c'), g('d')
        parts += 1
        # 前脸面序 a,b,d,c (在 (z,y) 平面里判断)
        ring = [(A[2], A[1]), (B[2], B[1]), (D[2], D[1]), (C[2], C[1])]
        n = len({ring[0], ring[1], ring[2], ring[3]})
        if n == 3:
            tris += 1
            area = cross(ring[0], ring[1], ring[2])
            if abs(area) > 1e-12:
                ok_tri += 1
            else:
                degenerate += 1
        else:
            quads += 1
            signs = [cross(ring[i], ring[(i + 1) % 4], ring[(i + 2) % 4]) for i in range(4)]
            if all(s > 1e-12 for s in signs) or all(s < -1e-12 for s in signs):
                ok_quad += 1
        # 前脸法向(3D 叉积的 x 分量): 向量在 (y,z) 平面里，x = uy*vz - uz*vy
        u = (B[1] - A[1], B[2] - A[2])
        v = (C[1] - A[1], C[2] - A[2])
        nx = u[0] * v[1] - u[1] * v[0]
        if abs(nx) > 1e-9:
            if nx > 0:
                plus += 1
            else:
                minus += 1
    print('%s' % path)
    print('  零件 %d (四边 %d, 三角 %d, 其中零面积退化 %d)' % (parts, quads, tris, degenerate))
    print('  A,B,D,C 环绕合规: 四边 %d/%d, 三角 %d/%d' % (ok_quad, quads, ok_tri, tris))
    print('  前脸法向朝 +x: %d, 朝 -x: %d' % (plus, minus))
    bad = (ok_quad != quads) or (ok_tri + degenerate != tris)
    if bad:
        print('  !! 有零件的角点拓扑不合规')
    return not bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('files', nargs='+')
    args = ap.parse_args()
    allok = True
    for f in args.files:
        allok = check(f) and allok
    print('\nRESULT: %s' % ('OK' if allok else 'PROBLEMS'))
    return 0 if allok else 1


if __name__ == '__main__':
    sys.exit(main())
