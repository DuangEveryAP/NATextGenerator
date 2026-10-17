#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""NATextGenerator 输出验证 / 预览

把 .na 里每个零件的前脸三角面投影回 (z,y) 平面并栅格化，
再与 Pillow 用同一字体、同一字号渲染出来的参考图对比，输出 IoU 和并排预览图。

用法:
  python tests/verify.py <na文件> --font <字体> [--text <文本>|--text-file <文件>] --size <米>
                         [--scale 30] [--out tests/preview.png] [--no-ref]
"""
import argparse
import sys
import xml.etree.ElementTree as ET

from PIL import Image, ImageDraw, ImageFont

try:
    import numpy as np
except ImportError:            # pragma: no cover
    np = None


def parse_na(path):
    """返回 [(p0, p1, p2), ...] 前脸三角面(米)，坐标为 (z=文字方向, y=向上)"""
    tree = ET.parse(path)
    root = tree.getroot()
    tris = []
    ids = []
    bad = 0
    for part in root.iter('part'):
        ids.append(part.get('instanceId'))
        vd = part.find('vertexData')
        if vd is None:
            continue
        pts = [(float(vd.get(n + 'z')), float(vd.get(n + 'y'))) for n in ('a', 'b', 'c', 'd')]
        poly = [pts[0], pts[1], pts[3], pts[2]]      # 行优先 A,B / C,D 对应的面序
        uniq = []
        for p in poly:
            if not uniq or abs(p[0] - uniq[-1][0]) > 1e-9 or abs(p[1] - uniq[-1][1]) > 1e-9:
                uniq.append(p)
        while len(uniq) > 1 and abs(uniq[0][0] - uniq[-1][0]) <= 1e-9 and abs(uniq[0][1] - uniq[-1][1]) <= 1e-9:
            uniq.pop()
        if len(uniq) == 3:
            tris.append(tuple(uniq))
        elif len(uniq) > 3:
            for k in range(1, len(uniq) - 1):
                tris.append((uniq[0], uniq[k], uniq[k + 1]))
        else:
            bad += 1
    return tris, ids, bad


def tri_area(t):
    (x1, y1), (x2, y2), (x3, y3) = t
    return (x2 - x1) * (y3 - y1) - (x3 - x1) * (y2 - y1)


def render_mine(tris, scale, margin, height, width):
    img = Image.new('L', (width, height), 0)
    d = ImageDraw.Draw(img)
    base = height - margin
    for t in tris:
        d.polygon([(margin + x * scale, base - y * scale) for (x, y) in t], fill=255)
    return img


def render_ref(font_path, text, size_m, scale, margin, height, width):
    """用 Pillow 渲染参考图(基线对齐到 height-margin)"""
    img = Image.new('L', (width, height), 0)
    d = ImageDraw.Draw(img)
    font = ImageFont.truetype(font_path, int(round(size_m * scale)))
    ascent = font.getmetrics()[0]
    base = height - margin
    # 默认锚点是 "la"(左-顶)，即 y 给出的是 ascender 线位置，基线 = y + ascent
    d.text((margin, base - ascent), text, font=font, fill=255)
    return img


def iou(a, b):
    if np is not None:
        pa = np.asarray(a) > 127
        pb = np.asarray(b) > 127
        union = int((pa | pb).sum())
        inter = int((pa & pb).sum())
        return (inter / union) if union else 1.0
    pa, pb = a.load(), b.load()
    inter = union = 0
    for y in range(a.size[1]):
        for x in range(a.size[0]):
            va = pa[x, y] > 127
            vb = pb[x, y] > 127
            if va or vb:
                union += 1
                if va and vb:
                    inter += 1
    return (inter / union) if union else 1.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('na')
    ap.add_argument('--font', required=True)
    ap.add_argument('--text')
    ap.add_argument('--text-file')
    ap.add_argument('--size', type=float, required=True)
    ap.add_argument('--scale', type=float, default=30.0, help='px per meter')
    ap.add_argument('--out', default='tests/preview.png')
    ap.add_argument('--no-ref', action='store_true')
    args = ap.parse_args()

    if args.text_file:
        with open(args.text_file, encoding='utf-8') as f:
            text = f.read().strip('\ufeff\r\n')
    else:
        text = args.text or ''
    if not text:
        print('ERROR: no text given')
        return 2

    tris, ids, bad = parse_na(args.na)
    xs = [p[0] for t in tris for p in t] or [1.0]
    ys = [p[1] for t in tris for p in t] or [1.0]
    margin = 40
    width = int(max(max(xs) - min(xs), args.size) * args.scale) + 2 * margin
    height = int(max(max(ys) - min(ys), args.size) * args.scale) + 2 * margin + int(args.size * args.scale)

    mine = render_mine(tris, args.scale, margin, height, width)
    degen = sum(1 for t in tris if abs(tri_area(t)) < 1e-12)
    print('parts/triangles : %d' % len(tris))
    print('unparsable parts: %d' % bad)
    print('degenerate tris : %d' % degen)
    print('instanceId uniq : %s' % (len(set(ids)) == len(ids)))
    print('bbox x          : %.4f .. %.4f m' % (min(xs), max(xs)))
    print('bbox y          : %.4f .. %.4f m' % (min(ys), max(ys)))

    ref = None
    if not args.no_ref:
        ref = render_ref(args.font, text, args.size, args.scale, margin, height, width)
        print('ref bbox        : %s' % (ref.getbbox(),))
        print('mine bbox       : %s' % (mine.getbbox(),))
        print('IoU vs Pillow   : %.4f' % iou(mine, ref))

    prev = Image.merge('RGB', (mine, ref if ref is not None else Image.new('L', mine.size, 0),
                               Image.new('L', mine.size, 0)))
    prev.save(args.out)
    print('preview saved   : %s' % args.out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
