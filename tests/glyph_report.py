#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""逐字检测: 对每个测试字符单独生成 .na，并与 Pillow 参考渲染比较 IoU。

用法: python tests/glyph_report.py [--font 字体] [--size 10] [--quality 5] [--chars 字串]
"""
import argparse
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import verify  # noqa: E402
from PIL import ImageFont

DEFAULT_CHARS = (
    'ABCDEFGHIJKLMNOPQRSTUVWXYZ'
    'abcdefghijklmnopqrstuvwxyz'
    '0123456789'
    '.,;:!?\'"()[]{}+-*/=<>@#$%&_~^`|\\'
    '口回田目日国品BAPQRSaeogbdp890'
    '大蓝猫叔岛厨雪舞说'
    'ÄÖÜäöüßÅåØøÆæŁłŃń'
)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--font', default='SourceHanSansSC-Regular-2.otf')
    ap.add_argument('--size', type=float, default=10.0)
    ap.add_argument('--quality', type=int, default=5)
    ap.add_argument('--scale', type=float, default=80.0)
    ap.add_argument('--chars', default=DEFAULT_CHARS)
    ap.add_argument('--exe', default='NATextGenerator.exe')
    ap.add_argument('--threshold', type=float, default=0.97)
    args = ap.parse_args()

    font_path = args.font if ('/' in args.font or '\\' in args.font) else os.path.join('fonts', args.font)
    chars = []
    for ch in args.chars:
        if ch not in chars:
            chars.append(ch)

    tmpdir = tempfile.mkdtemp(prefix='natg_glyphs_')
    rows = []
    for ch in chars:
        na = os.path.join(tmpdir, 'g_%d.na' % ord(ch))
        r = subprocess.run([args.exe, '-f', args.font, '-s', str(args.size), '-q', str(args.quality),
                            '-t', ch, '-o', na], capture_output=True)
        if r.returncode != 0 or not os.path.exists(na):
            rows.append((0.0, ch, 'GEN-FAIL', 0))
            continue
        tris, ids, bad = verify.parse_na(na)
        if not tris:
            rows.append((0.0, ch, 'NO-TRI', 0))
            continue
        margin = 6
        xs = [p[0] for t in tris for p in t]
        ys = [p[1] for t in tris for p in t]
        width = int(max(max(xs) - min(xs), 0.2) * args.scale) + 2 * margin
        height = int(max(max(ys) - min(ys), 0.2) * args.scale) + 2 * margin + int(args.size * args.scale)
        mine = verify.render_mine(tris, args.scale, margin, height, width)
        ref = verify.render_ref(font_path, ch, args.size, args.scale, margin, height, width)
        rows.append((verify.iou(mine, ref), ch, 'ok', len(tris)))

    rows.sort(key=lambda r: r[0])
    print('%-6s %-6s %8s %6s  %s' % ('IoU', 'char', 'U+', 'tris', 'state'))
    bad = 0
    lines = []
    for v, ch, state, n in rows:
        if v < args.threshold:
            bad += 1
        label = ch if (0x20 <= ord(ch) < 0x7f) else '?'
        print('%-6.4f %-6s %8s %6d  %s' % (v, label, 'U+%04X' % ord(ch), n, state))
        lines.append('%.4f\tU+%04X\t%r\t%d\t%s' % (v, ord(ch), ch, n, state))
    print('\n%d chars, %d below %.3f' % (len(rows), bad, args.threshold))
    report = os.path.join(tmpdir, 'report.txt')
    with open(report, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    print('report:', report)
    print('tmpdir:', tmpdir)
    return 0


if __name__ == '__main__':
    sys.exit(main())
