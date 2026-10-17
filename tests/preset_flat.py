#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""端到端校验网页"预设控制点"的逻辑：

1. 用平面模式取到文字包围盒(em)；
2. 按网页里 makePresetArray('flat') 的算法生成控制点字符串(归一化 em 单位)；
3. 用这个控制点走曲面模式生成，应当与平面模式逐顶点一致(说明"平直"预设＝平面)；
4. 再按 'hbend' 生成，确认 x 方向出现预期的鼓包。

用法: python tests/preset_flat.py --port 8841 [--text 大蓝猫叔] [--size 10]
"""
import argparse
import json
import sys
import urllib.parse
import urllib.request

import xml.etree.ElementTree as ET


def post(port, params):
    url = 'http://127.0.0.1:%d/api/generate' % port
    data = urllib.parse.urlencode(params).encode('utf-8')
    with urllib.request.urlopen(url, data=data, timeout=60) as r:
        return json.loads(r.read().decode('utf-8'))


def fmt(v):
    s = '%.3f' % v
    if '.' in s:
        s = s.rstrip('0').rstrip('.')
    return '0' if s == '-0' else s


def make_preset(kind, bounds, size, unit='em'):
    k = 1.0 if unit == 'em' else size
    W = max(bounds['maxX'] - bounds['minX'], 1e-6) * k
    H = max(bounds['maxY'] - bounds['minY'], 1e-6) * k
    peak = 0.25 * W
    arc = peak / 0.75
    out = []
    for j in range(4):
        v = j / 3.0
        y = (bounds['minY'] + (bounds['maxY'] - bounds['minY']) * v) * k
        for i in range(4):
            u = i / 3.0
            z = bounds['minX'] * k + W * u
            x = 0.0
            arcU = 1 if i in (1, 2) else 0
            arcV = 1 if j in (1, 2) else 0
            if kind == 'hbend':
                x = arcU * arc
            elif kind == 'vbend':
                x = arcV * arc
            elif kind == 'wave':
                x = arc if i == 1 else (-arc if i == 2 else 0.0)
            elif kind == 'dome':
                x = peak / 0.5625 * (arcU * arcV)
            out.append((x, y, z))
    return ' '.join(','.join(fmt(c) for c in p) for p in out)


def verts(path):
    out = []
    for part in ET.parse(path).getroot().iter('part'):
        vd = part.find('vertexData')
        if vd is None:
            continue
        out.append([float(vd.get(k + c)) for k in ('a', 'b', 'c', 'd') for c in ('x', 'y', 'z')])
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=8841)
    ap.add_argument('--text', default='大蓝猫叔')
    ap.add_argument('--size', type=float, default=10.0)
    ap.add_argument('--font', default='SourceHanSansSC-Regular-2.otf')
    args = ap.parse_args()

    base = {'font': args.font, 'text': args.text, 'size': str(args.size),
            'quality': '5', 'thickness': '0.2', 'write': '1', 'kerning': '1'}

    # 1) 平面模式取包围盒
    p = dict(base)
    p.update({'placement': 'plane', 'out': 'preset_plane.na'})
    j = post(args.port, p)
    if not j.get('ok'):
        print('平面生成失败: %s' % j.get('error'))
        return 2
    bounds = j['bounds']
    print('文字包围盒 %.3f..%.3f × %.3f..%.3f em' % (bounds['minX'], bounds['maxX'], bounds['minY'], bounds['maxY']))

    # 2) 平直预设 -> 曲面
    cp_flat = make_preset('flat', bounds, args.size)
    p = dict(base)
    p.update({'placement': 'surface', 'surfaceUnit': 'em', 'cp': cp_flat, 'out': 'preset_flat.na'})
    j2 = post(args.port, p)
    if not j2.get('ok'):
        print('曲面生成失败: %s' % j2.get('error'))
        return 2

    vp = verts('preset_plane.na')
    vf = verts('preset_flat.na')
    if len(vp) != len(vf):
        print('零件数不同: %d vs %d' % (len(vp), len(vf)))
        return 1
    worst = 0.0
    for a, b in zip(vp, vf):
        for k in range(4):
            worst = max(worst, max(abs(a[k * 3 + c] - b[k * 3 + c]) for c in range(3)))
    print('平面 vs 平直曲面 顶点最大偏差 %.6f m' % worst)

    # 3) 水平弯预设
    cp_hb = make_preset('hbend', bounds, args.size)
    p = dict(base)
    p.update({'placement': 'surface', 'surfaceUnit': 'em', 'cp': cp_hb, 'out': 'preset_hbend.na'})
    j3 = post(args.port, p)
    if not j3.get('ok'):
        print('水平弯生成失败: %s' % j3.get('error'))
        return 2
    vh = verts('preset_hbend.na')
    xs = [v[0] for part in vh for v in [part[0:3], part[3:6], part[6:9], part[9:12]]]
    print('水平弯时 x 范围 %.3f .. %.3f m (平直时 %.3f .. %.3f)'
          % (min(xs), max(xs), min(v[0] for part in vp for v in [part[0:3]]),
             max(v[0] for part in vp for v in [part[0:3]])))

    ok = worst < 1e-3 and (max(xs) - min(xs)) > 1.0
    print('\nRESULT: %s' % ('OK' if ok else 'PROBLEMS'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
