#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""列出"应当上传到 GitHub 仓库"的文件（按 .gitignore 的规则过滤），并给出统计。

用法: python tools/list_upload.py            # 明细
      python tools/list_upload.py --summary  # 只看统计
"""
import fnmatch
import io
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 与 .gitignore 保持一致的忽略规则(目录规则以 / 结尾)
IGNORE_DIRS = ['build', 'release', 'tests/samples', '__pycache__', '.git',
               '.vscode', '.idea']
IGNORE_PATTERNS = ['*.exe', '*.o', '*.obj', '*.res', '*.layout', '*.na', '*.pyc',
                   'Thumbs.db', 'desktop.ini', '.DS_Store', 'output.na']

# 明确要上传的顶层条目（其余一律不上传，避免误传本地产物）
INCLUDE_TOP = [
    '.gitignore', 'README.md', 'LICENSE', 'build.ps1', 'Makefile.win',
    'NATextGenerator.dev', 'icon.ico', 'icon.rc',
    'NATextGenerator_private.h', 'NATextGenerator_private.rc',
    'src', 'web', 'tools', 'tests', 'docs', 'LICENSES', 'libs', 'fonts', 'legacy',
]


def ignored(rel):
    rel = rel.replace(os.sep, '/')
    for d in IGNORE_DIRS:
        if rel == d or rel.startswith(d + '/'):
            return True
    base = rel.rsplit('/', 1)[-1]
    for pat in IGNORE_PATTERNS:
        if fnmatch.fnmatch(base, pat) or fnmatch.fnmatch(rel, pat):
            return True
    return False


def main():
    summary = '--summary' in sys.argv
    rows = []
    for top in INCLUDE_TOP:
        p = os.path.join(ROOT, top)
        if not os.path.exists(p):
            continue
        if os.path.isfile(p):
            rows.append((top, os.path.getsize(p)))
            continue
        for dirpath, dirnames, files in os.walk(p):
            dirnames[:] = [d for d in sorted(dirnames) if not ignored(os.path.join(dirpath, d))]
            for fn in sorted(files):
                full = os.path.join(dirpath, fn)
                rel = os.path.relpath(full, ROOT)
                if ignored(rel):
                    continue
                rows.append((rel.replace(os.sep, '/'), os.path.getsize(full)))

    total = sum(s for _n, s in rows)
    if summary:
        by_top = {}
        for n, s in rows:
            k = n.split('/')[0]
            by_top[k] = by_top.get(k, (0, 0))
            by_top[k] = (by_top[k][0] + 1, by_top[k][1] + s)
        for k in sorted(by_top):
            c, s = by_top[k]
            print('%-24s %4d 个文件  %10s' % (k, c, '{:,}'.format(s)))
        print('%-24s %4d 个文件  %10s' % ('合计', len(rows), '{:,}'.format(total)))
        return 0

    for n, s in rows:
        print('%-58s %10s' % (n, '{:,}'.format(s)))
    print('-' * 70)
    print('%-58s %10s' % ('共 %d 个文件' % len(rows), '{:,}'.format(total)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
