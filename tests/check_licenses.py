#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""检查 readme 与 LICENSES 的一致性（防止许可文件缺失、路径写错、留有占位符）。

检查项:
 1. readme 里出现的 "./xxx" 路径必须真实存在；
 2. LICENSES/ 与 docs/ 下的许可文件要非空，且不能残留 "<copyright holders>" 之类的占位符；
 3. LICENSE 必须是 GPLv2，且 docs/GPLv2.TXT 与之一致；
 4. release/ 里必须带上 readme 引用的所有许可文件；
 5. 所有随包分发的字体都要有对应许可文件说明（至少 LICENSES/FONTS 下有内容）；
 6. readme 里提到的第三方组件(FreeType / earcut / MinGW / 思源字体)都要在 readme 里出现。

用法: python tests/check_licenses.py
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RELEASE = os.path.join(ROOT, 'release')

problems = []
notes = []


def read(p):
    with io.open(p, encoding='utf-8', errors='replace') as f:
        return f.read()


def main():
    readme_path = os.path.join(ROOT, 'readme.md')
    readme = read(readme_path)

    # 1) readme 里引用的相对路径
    refs = sorted(set(re.findall(r'"(\./[^"]+)"', readme)))
    for r in refs:
        p = os.path.join(ROOT, r[2:].replace('/', os.sep))
        if not os.path.exists(p):
            problems.append('readme 引用了不存在的文件: %s' % r)
    notes.append('readme 引用路径 %d 个，全部存在' % len(refs) if not problems else '')

    # 2) 许可文件非空、无占位符
    license_files = []
    for base in ('LICENSES', 'docs'):
        for dirpath, _dirs, files in os.walk(os.path.join(ROOT, base)):
            for fn in files:
                license_files.append(os.path.join(dirpath, fn))
    placeholders = ['<copyright holder', 'TODO', 'FIXME', 'XXX', '[year]', '[fullname]']
    for p in license_files:
        rel = os.path.relpath(p, ROOT).replace(os.sep, '/')
        text = read(p)
        if not text.strip():
            problems.append('许可文件为空: %s' % rel)
        for ph in placeholders:
            if ph.lower() in text.lower():
                problems.append('许可文件里残留占位符 "%s": %s' % (ph, rel))
    notes.append('检查了 %d 个许可/文档文件' % len(license_files))

    # 3) LICENSE 与 docs/GPLv2.TXT
    lic = read(os.path.join(ROOT, 'LICENSE'))
    if 'GNU GENERAL PUBLIC LICENSE' not in lic or 'Version 2, June 1991' not in lic:
        problems.append('LICENSE 看起来不是 GPLv2')
    gpl_copy = os.path.join(ROOT, 'docs', 'GPLv2.TXT')
    if not os.path.exists(gpl_copy):
        problems.append('缺少 docs/GPLv2.TXT（FreeType 的许可说明引用了它）')
    elif read(gpl_copy).strip() != lic.strip():
        problems.append('docs/GPLv2.TXT 与 LICENSE 内容不一致')

    # 4) release 里是否带齐被引用的许可文件
    if os.path.isdir(RELEASE):
        need = ['readme.md', 'LICENSE', 'docs/FTL.TXT', 'docs/GPLv2.TXT',
                'LICENSES/LIBS/FreeType_LICENSE.TXT', 'LICENSES/LIBS/earcut.txt',
                'LICENSES/LIBS/MinGW-w64_runtime.txt', 'LICENSES/FONTS/SourceHanSansSC_LICENSE.txt']
        for r in need:
            p = os.path.join(RELEASE, r.replace('/', os.sep))
            if not os.path.exists(p):
                problems.append('release/ 里缺少: %s' % r)
        notes.append('release 目录已检查')
        # 字体与许可对应
        fonts = []
        fdir = os.path.join(RELEASE, 'fonts')
        if os.path.isdir(fdir):
            fonts = [f for f in os.listdir(fdir) if f.lower().endswith(('.ttf', '.otf', '.ttc'))]
        font_lic = os.path.join(RELEASE, 'LICENSES', 'FONTS')
        if fonts:
            if not os.path.isdir(font_lic) or not os.listdir(font_lic):
                problems.append('release/ 里有字体但没有 LICENSES/FONTS 说明')
            for f in fonts:
                if 'SourceHanSans' not in f:
                    notes.append('注意: release 分发了字体 %s，请确认其许可允许再分发' % f)
    else:
        notes.append('release/ 不存在，跳过打包检查')

    # 5) readme 里应提到所有第三方组件
    for comp in ('FreeType', 'earcut', 'MinGW', 'SIL OPEN FONT LICENSE'):
        if comp not in readme:
            problems.append('readme 里缺少第三方组件说明: %s' % comp)

    # 6) 仓库里存在但没有许可说明的字体（提示）
    fdir = os.path.join(ROOT, 'fonts')
    if os.path.isdir(fdir):
        for f in sorted(os.listdir(fdir)):
            if not f.lower().endswith(('.ttf', '.otf', '.ttc')):
                continue
            if 'SourceHanSans' in f:
                continue
            notes.append('仓库 fonts/ 里有未附许可说明的字体: %s（若公开仓库需确认可再分发）' % f)

    for n in notes:
        if n:
            print('note: ' + n)
    if problems:
        print('')
        for p in problems:
            print('PROBLEM: ' + p)
        print('\nRESULT: %d 个问题' % len(problems))
        return 1
    print('\nRESULT: OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())
