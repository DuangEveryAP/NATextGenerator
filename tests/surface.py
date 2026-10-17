#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""校验"贝塞尔曲面放置"的正确性，并渲染 3D 预览。

校验内容:
  1. 每个零件的 4 个前脸顶点都应落在贝塞尔曲面上(反解 (u,v) 后残差 ≈ 0)；
  2. 后脸顶点 = 前脸顶点 - 2*halfThickness*法向；
  3. 可选: 渲染一张 3D 预览图(等距投影 + 画家算法)。

用法:
  python tests/surface.py <na文件> --cp "<48个数>" [--unit em|m] [--size 10] [--thickness 0.2]
                            [--bbox minX,maxX,minY,maxY] [--render out.png]
"""
import argparse
import math
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, __file__.rsplit('\\', 1)[0].rsplit('/', 1)[0])
import quality  # noqa: E402


def bern(i, t):
    mt = 1.0 - t
    return [mt * mt * mt, 3 * t * mt * mt, 3 * t * t * mt, t * t * t][i]


def bern_d(i, t):
    mt = 1.0 - t
    return [-3 * mt * mt, 3 * mt * mt - 6 * t * mt, 6 * t * mt - 3 * t * t, 3 * t * t][i]


class Patch:
    def __init__(self, cp):          # cp: 16 个 (x,y,z)，按 v 行优先
        self.cp = cp

    def eval(self, u, v):
        p = [0.0, 0.0, 0.0]
        for j in range(4):
            bv = bern(j, v)
            for i in range(4):
                w = bern(i, u) * bv
                c = self.cp[j * 4 + i]
                p[0] += c[0] * w
                p[1] += c[1] * w
                p[2] += c[2] * w
        return p

    def normal(self, u, v):
        du = [0.0, 0.0, 0.0]
        dv = [0.0, 0.0, 0.0]
        for j in range(4):
            for i in range(4):
                c = self.cp[j * 4 + i]
                wu = bern_d(i, u) * bern(j, v)
                wv = bern(i, u) * bern_d(j, v)
                du[0] += c[0] * wu; du[1] += c[1] * wu; du[2] += c[2] * wu
                dv[0] += c[0] * wv; dv[1] += c[1] * wv; dv[2] += c[2] * wv
        # 与 C++ 一致: cross(dPdv, dPdu)
        n = [dv[1] * du[2] - dv[2] * du[1], dv[2] * du[0] - dv[0] * du[2], dv[0] * du[1] - dv[1] * du[0]]
        ln = math.sqrt(sum(x * x for x in n)) or 1.0
        return [x / ln for x in n]


def derivative(patch, u, v):
    du = [0.0, 0.0, 0.0]
    dv = [0.0, 0.0, 0.0]
    for j in range(4):
        for i in range(4):
            c = patch.cp[j * 4 + i]
            wu = bern_d(i, u) * bern(j, v)
            wv = bern(i, u) * bern_d(j, v)
            du[0] += c[0] * wu; du[1] += c[1] * wu; du[2] += c[2] * wu
            dv[0] += c[0] * wv; dv[1] += c[1] * wv; dv[2] += c[2] * wv
    return du, dv


def invert(patch, target, n=24, gn=12):
    """粗网格找初值，再用 Gauss-Newton 精确反解 (u,v) 使 P(u,v) ≈ target"""
    best = (1e300, 0.0, 0.0)
    for j in range(n + 1):
        v = j / n
        for i in range(n + 1):
            u = i / n
            p = patch.eval(u, v)
            d = sum((p[k] - target[k]) ** 2 for k in range(3))
            if d < best[0]:
                best = (d, u, v)
    d, u, v = best
    for _ in range(gn):
        p = patch.eval(u, v)
        r = [p[k] - target[k] for k in range(3)]
        ju, jv = derivative(patch, u, v)
        a = sum(x * x for x in ju)
        b = sum(ju[k] * jv[k] for k in range(3))
        c = sum(x * x for x in jv)
        g0 = sum(ju[k] * r[k] for k in range(3))
        g1 = sum(jv[k] * r[k] for k in range(3))
        det = a * c - b * b
        if abs(det) < 1e-18:
            break
        du = (-g0 * c + g1 * b) / det
        dv = (-g1 * a + g0 * b) / det
        u += du
        v += dv
        if abs(du) + abs(dv) < 1e-14:
            break
    p = patch.eval(u, v)
    d = math.sqrt(sum((p[k] - target[k]) ** 2 for k in range(3)))
    return d, u, v


def parse_parts(path):
    import xml.etree.ElementTree as ET
    out = []
    tree = ET.parse(path)
    for part in tree.getroot().iter('part'):
        vd = part.find('vertexData')
        if vd is None:
            continue
        # 注意: 文件里是行优先 A,B,C,D，面序应为 A,B,D,C(否则是蝴蝶结)
        order = ('a', 'b', 'd', 'c')
        front = [(float(vd.get(k + 'x')), float(vd.get(k + 'y')), float(vd.get(k + 'z')))
                 for k in order]
        back = [(float(vd.get(k + 'x')), float(vd.get(k + 'y')), float(vd.get(k + 'z')))
                for k in ('i', 'ii', 'iv', 'iii')]
        out.append((front, back))
    return out


def render(parts, out_png, size=1000, yaw=-0.6, pitch=0.32):
    pts = [p for f, b in parts for p in f + b]
    minx = min(p[0] for p in pts); maxx = max(p[0] for p in pts)
    miny = min(p[1] for p in pts); maxy = max(p[1] for p in pts)
    minz = min(p[2] for p in pts); maxz = max(p[2] for p in pts)
    cx, cy, cz = (minx + maxx) / 2, (miny + maxy) / 2, (minz + maxz) / 2
    span = max(maxx - minx, maxy - miny, maxz - minz) or 1.0
    S = size * 0.85 / span
    cy_, sy_ = math.cos(yaw), math.sin(yaw)
    cp_, sp_ = math.cos(pitch), math.sin(pitch)

    def proj(p):
        x, y, z = p[0] - cx, p[1] - cy, p[2] - cz
        x1 = x * cy_ + z * sy_
        z1 = -x * sy_ + z * cy_
        y2 = y * cp_ - z1 * sp_
        z2 = y * sp_ + z1 * cp_
        return (size / 2 + x1 * S, size / 2 - y2 * S, z2)

    faces = []
    for front, back in parts:
        faces.append((sum(proj(p)[2] for p in front) / 4, [proj(p)[:2] for p in front]))
        faces.append((sum(proj(p)[2] for p in back) / 4, [proj(p)[:2] for p in back[::-1]]))
    faces.sort(key=lambda f: f[0])
    img = Image.new('RGB', (size, size), (18, 21, 26))
    d = ImageDraw.Draw(img)
    for z, poly in faces:
        d.polygon(poly, fill=(120, 126, 134))
    img.save(out_png)
    return out_png


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('na')
    ap.add_argument('--cp', required=True)
    ap.add_argument('--unit', default='em', choices=['em', 'm'])
    ap.add_argument('--size', type=float, default=10.0)
    ap.add_argument('--thickness', type=float, default=0.2)
    ap.add_argument('--render')
    ap.add_argument('--limit', type=int, default=200000)
    args = ap.parse_args()

    nums = [float(x) for x in args.cp.replace(';', ' ').replace('|', ' ').replace(',', ' ').split()]
    if len(nums) != 48:
        print('cp 需要 48 个数，实际 %d' % len(nums))
        return 2
    k = args.size if args.unit == 'em' else 1.0
    cp = [(nums[i * 3] * k, nums[i * 3 + 1] * k, nums[i * 3 + 2] * k) for i in range(16)]
    patch = Patch(cp)

    parts = parse_parts(args.na)
    half = args.thickness * 0.5
    worst_on = 0.0
    worst_off = 0.0
    worst_align = 0.0
    checked = 0
    for front, back in parts[:args.limit]:
        for p, q in zip(front, back):
            dist, u, v = invert(patch, p)
            # 前脸顶点应在"中面沿法向偏移 half"的等距面上
            worst_on = max(worst_on, abs(dist - half))
            n = patch.normal(u, v)
            surf = patch.eval(u, v)
            # 偏移方向应与法向平行(单位向量点积的绝对值 ≈ 1)
            vec = [p[k] - surf[k] for k in range(3)]
            ln = math.sqrt(sum(x * x for x in vec)) or 1.0
            align = abs(sum(vec[k] * n[k] for k in range(3))) / ln
            worst_align = max(worst_align, abs(1.0 - align))
            # 后脸应在 p - 2*half*n
            expect = [p[k] - 2.0 * half * n[k] for k in range(3)]
            e = math.sqrt(sum((expect[k] - q[k]) ** 2 for k in range(3)))
            worst_off = max(worst_off, e)
            checked += 1
    print('零件数            %d' % len(parts))
    print('检查顶点数        %d' % checked)
    print('顶点到等距面偏差  %.6f m（应为 0；half=%.3f）' % (worst_on, half))
    print('偏移方向与法向    %.6f（0 = 完全沿法向）' % worst_align)
    print('前后脸最大偏差    %.6f m（理论 0）' % worst_off)

    if args.render:
        print('3D 预览           %s' % render(parts, args.render))

    ok = worst_on < 1e-3 and worst_off < 1e-3 and worst_align < 1e-3
    print('\nRESULT: %s' % ('OK' if ok else 'PROBLEMS'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
