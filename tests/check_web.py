#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""检查前端静态资源的一致性：
   1. app.js 里用到的元素 id 是否都在 index.html 中定义；
   2. app.js 里使用的接口路径是否都被服务端实现；
   3. style.css 里被用到的类名是否有定义（粗略检查）。
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(p):
    with io.open(p, encoding='utf-8') as f:
        return f.read()


def main():
    html = read(os.path.join(ROOT, 'web', 'index.html'))
    js = read(os.path.join(ROOT, 'web', 'app.js'))
    css = read(os.path.join(ROOT, 'web', 'style.css'))
    webapp = read(os.path.join(ROOT, 'src', 'webapp.cpp'))

    ids = set(re.findall(r'id="([A-Za-z0-9_-]+)"', html))
    used = set(re.findall(r"\$\('([A-Za-z0-9_-]+)'\)", js))
    missing = sorted(u for u in used if u not in ids)
    print('html ids      : %d' % len(ids))
    print('js used ids   : %d' % len(used))
    print('missing in html: %s' % (missing if missing else 'none'))

    # 接口路径
    api_used = sorted(set(re.findall(r"'(/api/[a-z]+)", js)))
    api_impl = sorted(set(re.findall(r'path == "(/api/[a-z]+)"', webapp)))
    api_impl += sorted(set(re.findall(r'path == "(/[a-z.]*)"', webapp)))
    missing_api = [a for a in api_used if a not in api_impl]
    print('api used      : %s' % ', '.join(api_used))
    print('api missing   : %s' % (missing_api if missing_api else 'none'))

    # 粗略检查 CSS 类名
    js_classes = set()
    for m in re.findall(r'classList\.(?:add|toggle|remove)\(\'([A-Za-z0-9_-]+)\'', js):
        js_classes.add(m)
    for m in re.findall(r'className = \'([^\']*)\'', js):
        for c in m.split():
            js_classes.add(c)
    html_classes = set()
    for m in re.findall(r'class="([^"]*)"', html):
        for c in m.split():
            html_classes.add(c)
    css_classes = set(re.findall(r'\.([A-Za-z0-9_-]+)', css))
    all_used = js_classes | html_classes
    missing_css = sorted(c for c in all_used if c not in css_classes)
    print('classes used  : %d' % len(all_used))
    print('missing in css: %s' % (missing_css if missing_css else 'none'))

    ok = not missing and not missing_api and not missing_css
    print('\nRESULT: %s' % ('OK' if ok else 'PROBLEMS FOUND'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
