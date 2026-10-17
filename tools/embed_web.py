#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""把 web/ 下的静态资源转成 C++ 字符串，生成 src/web_assets.cpp。"""
import io
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.path.join(ROOT, 'web')
OUT = os.path.join(ROOT, 'src', 'web_assets.cpp')

FILES = [
    ('kIndexHtml', 'index.html'),
    ('kAppJs', 'app.js'),
    ('kStyleCss', 'style.css'),
]


def to_cpp_string(name, text):
    # 用原始字符串字面量，避免大量转义；选取内容中不出现的分隔符
    delim = 'NATG'
    i = 0
    while (')' + delim + '"') in text:
        i += 1
        delim = 'NATG%d' % i
    return 'const char *%s = R"%s(%s)%s";\n' % (name, delim, text, delim)


def main():
    parts = [
        '// 本文件由 tools/embed_web.py 自动生成，请勿手工修改。\n'
        '#include "web_assets.h"\n\nnamespace natg {\nnamespace web {\n\n'
    ]
    for name, fname in FILES:
        path = os.path.join(WEB, fname)
        if not os.path.exists(path):
            print('missing: %s' % path, file=sys.stderr)
            return 1
        with io.open(path, encoding='utf-8') as f:
            text = f.read()
        if '\0' in text:
            print('NUL byte in %s' % fname, file=sys.stderr)
            return 1
        parts.append(to_cpp_string(name, text))
        parts.append('\n')
    parts.append('} // namespace web\n} // namespace natg\n')
    with io.open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(''.join(parts))
    total = sum(os.path.getsize(os.path.join(WEB, f)) for _, f in FILES)
    print('embedded %d files, %d bytes -> %s' % (len(FILES), total, OUT))
    return 0


if __name__ == '__main__':
    sys.exit(main())
