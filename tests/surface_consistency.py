#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""对比"平面"与"曲面"两种输出: 把曲面顶点反解 (u,v) 后换回字形平面坐标，
应当与平面模式的顶点逐一吻合(证明贴合没有扭曲字形)。并渲染可靠的 3D 预览。

用法: python tests/surface_consistency.py <平面na> <曲面na> --cp "..." [--unit em] [--size 10]
"""
import argparse
import math
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, __file__.rsplit('\\', 1)[0].rsplit('/', 1)[0])
import surface  # noqa: E402
import quality  # noqa: E402


def parse_front(path):
    import xml.etree.ElementTree as ET
    out = []
    for part in ET.parse(path).getroot().iter('part'):
        vd = part.find('vertexData')
        if vd is None:
            continue
        # 行优先 A,B,C,D；面序应为 A,B,D,C
        order = ('a', 'b', 'd', 'c')
        front = [(float(vd.get(k + 'x')), float(vd.get(k + 'y')), float(vd.get(k + 'z')))
                 for k in order]
        back = [(float(vd.get(k + 'x')), float(vd.get(k + 'y')), float(vd.get(k + 'z')))
                for k in ('i', 'ii', 'iv', 'iii')]
        out.append((front, back))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('plane_na')
    ap.add_argument('surface_na')
    ap.add_argument('--cp', required=True)
    ap.add_argument('--unit', default='em', choices=['em', 'm'])
    ap.add_argument('--size', type=float, default=10.0)
    ap.add_argument('--render')
    args = ap.parse_args()

    nums = [float(x) for x in args.cp.replace(';', ' ').replace('|', ' ').replace(',', ' ').split()]
    k = args.size if args.unit == 'em' else 1.0
    cp = [(nums[i * 3] * k, nums[i * 3 + 1] * k, nums[i * 3 + 2] * k) for i in range(16)]
    patch = surface.Patch(cp)

    plane = parse_front(args.plane_na)
    surf = parse_front(args.surface_na)
    print('平面零件 %d, 曲面零件 %d' % (len(plane), len(surf)))
    if len(plane) != len(surf):
        print('零件数不一致，无法逐一对齐')
        return 2

    # 平面模式: ay = gy*size, az = gx*size -> 反推包围盒(em)
    gy = [p[1] for f, b in plane for p in f]
    gx = [p[2] for f, b in plane for p in f]
    bboxMinX, bboxMaxX = min(gx) / args.size, max(gx) / args.size
    bboxMinY, bboxMaxY = min(gy) / args.size, max(gy) / args.size
    W = max(bboxMaxX - bboxMinX, 1e-12)
    H = max(bboxMaxY - bboxMinY, 1e-12)

    worst = 0.0
    for (pf, pb), (sf, sb) in zip(plane, surf):
        for pp, sp in zip(pf, sf):
            # 平面模式的字形平面坐标(米)
            px, py = pp[2], pp[1]
            dist, u, v = surface.invert(patch, sp)
            # 换回字形平面坐标(米)
            rx = (bboxMinX + u * W) * args.size
            ry = (bboxMinY + v * H) * args.size
            e = math.hypot(rx - px, ry - py)
            worst = max(worst, e)

    print('反解回字形平面后的最大偏差 %.6f m（字高 %.1f m）' % (worst, args.size))
    ok = worst < 1e-3

    if args.render:
        # 只画前脸(避免前后脸互相遮挡)
        img = Image.new('RGB', (1100, 700), (18, 21, 26))
        d = ImageDraw.Draw(img)
        pts = [p for f, b in surf for p in f]
        minx = min(p[0] for p in pts); maxx = max(p[0] for p in pts)
        miny = min(p[1] for p in pts); maxy = max(p[1] for p in pts)
        minz = min(p[2] for p in pts); maxz = max(p[2] for p in pts)
        cx, cy, cz = (minx + maxx) / 2, (miny + maxy) / 2, (minz + maxz) / 2
        span = max(maxx - minx, maxy - miny, maxz - minz) or 1.0
        S = 620 * 0.9 / span
        yaw, pitch = -0.65, 0.35
        cyaw, syaw = math.cos(yaw), math.sin(yaw)
        cpit, spit = math.cos(pitch), math.sin(pitch)

        def proj(p):
            x, y, z = p[0] - cx, p[1] - cy, p[2] - cz
            x1 = x * cyaw + z * syaw
            z1 = -x * syaw + z * cyaw
            y2 = y * cpit - z1 * spit
            z2 = y * spit + z1 * cpit
            return (550 + x1 * S, 350 - y2 * S, z2)

        faces = []
        for f, b in surf:
            q = [proj(p) for p in f]
            faces.append((sum(t[2] for t in q) / len(q), [t[:2] for t in q]))
        faces.sort(key=lambda t: t[0])
        for z, poly in faces:
            d.polygon(poly, fill=(150, 156, 164))
        img.save(args.render)
        print('预览(仅前脸) %s' % args.render)

    print('\nRESULT: %s' % ('OK' if ok else 'PROBLEMS'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
