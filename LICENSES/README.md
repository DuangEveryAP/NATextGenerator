# 许可文件索引

| 文件 | 覆盖对象 |
| --- | --- |
| `../LICENSE` | 本程序本身（NATextGenerator）：GNU GPL v2 |
| `LIBS/FreeType_LICENSE.TXT` | FreeType 的许可说明（双许可：FTL 或 GPLv2，二选一） |
| `../docs/FTL.TXT` | FreeType Project License 全文 |
| `../docs/GPLv2.TXT` | GPLv2 全文（本程序使用） |
| `LIBS/earcut.txt` | earcut 三角化算法：ISC 许可证 |
| `LIBS/MinGW-w64_runtime.txt` | MinGW-w64 / GCC 运行时库（ZPL 2.1；libstdc++/libgcc 为 GPLv3 + 运行时例外） |
| `FONTS/SourceHanSansSC_LICENSE.txt` | 思源黑体 Source Han Sans SC：SIL OFL 1.1 |

## 关于 FreeType 的许可选择

FreeType 的 `LICENSE.TXT` 说明它同时以两种许可发布，使用者必须选择其一：

* **FreeType Project License (FTL)**：类似 BSD 但带广告条款；官方说明明确写着
  "FTL 与 GPLv2 不兼容"（仅与 GPLv3 兼容）。
* **GNU GPL v2**：适用于本身已使用 GPL 的程序。

本程序以 **GPLv2** 发布，因此按 FreeType 的**第二个选项（GPLv2）**使用 FreeType，
两者许可一致，不存在冲突。`docs/FTL.TXT` 仍然随包附带，仅供参考。

FreeType 源码可从 https://freetype.org/ 获取；本程序使用的版本即 `libs/` 中自带的
头文件与静态库所对应的版本。
