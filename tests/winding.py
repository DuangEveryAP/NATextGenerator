#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""检查 .na 里每个零件的面朝向是否一致(背剔除安全的必要条件是
(a,b,c) 与 (a,c,d) 两个三角形法向同向)。

平面模式下前脸应朝 +x(厚度方向)，后脸朝 -x。

用法: python tests/winding.py <na文件> [--mode plane|surface] [--cp "..."] [--unit em] [--size 10]
"""
import argparse
import math
import sys
import xml.etree.ElementTree as ET


def parse(path):
    parts = []
    for part in ET.parse(path).getroot().iter('part'):
        vd = part.find('vertexData')
        if vd is None:
            continue
        g = lambda k: (float(vd.get(k + 'x')), float(vd.get(k + 'y')), float(vd.get(k + 'z')))
        front = [g(k) for k in ('a', 'b', 'c', 'd')]
        back = [g(k) for k in ('i', 'ii', 'iii', 'iv')]
        parts.append((front, back))
    return parts


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def norm(v):
    ln = math.sqrt(sum(x * x for x in v))
    return (v[0] / ln, v[1] / ln, v[2] / ln) if ln > 1e-15 else (0.0, 0.0, 0.0)


def tri_normal(p0, p1, p2):
    return norm(cross(sub(p1, p0), sub(p2, p0)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('na')
    ap.add_argument('--mode', default='plane', choices=['plane', 'surface'])
    ap.add_argument('--epsilon', type=float, default=0.2, help='面积小于该值的三角形视为退化')
    args = ap.parse_args()

    parts = parse(args.na)
    bad_front = 0
    bad_back = 0
    zero_front = 0
    checked = 0
    for front, back in parts:
        a, b, c, d = front
        n1 = tri_normal(a, b, c)
        # (a,c,d) 的面积
        area2 = cross(sub(c, a), sub(d, a))
        degenerated = math.sqrt(sum(x * x for x in area2)) < args.epsilon
        if degenerated:
            zero_front += 1
            continue
        n2 = tri_normal(a, c, d)
        dot = sum(n1[k] * n2[k] for k in range(3))
        checked += 1
        if dot < 0.5:
            bad_front += 1
    print('零件数           %d' % len(parts))
    print('三角面零件(退化) %d' % zero_front)
    print('检查的四边形零件 %d' % checked)
    print('前脸两三角面朝向不一致 %d (%.1f%%)' % (bad_front, 100.0 * bad_front / max(checked, 1)))

    # 平面模式下检查前脸法向是否朝 +x
    if args.mode == 'plane':
        plus = minus = 0
        for front, back in parts:
            n = tri_normal(front[0], front[1], front[2])
            if abs(n[0]) < 0.5:
                continue
            if n[0] > 0:
                plus += 1
            else:
                minus += 1
        print('前脸法向朝 +x 的零件 %d, 朝 -x 的 %d' % (plus, minus))

    ok = bad_front == 0
    print('\nRESULT: %s' % ('OK' if ok else 'PROBLEMS'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
