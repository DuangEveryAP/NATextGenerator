# NATextGenerator

## 简介

NATextGenerator 是一个程序，用于给软件 NavalArt 生成文本：输入一段文字，就能生成对应的 NavalArt 存档（`output.na`），
导入游戏后可以做成可建造的字体零件。

v0.4.0 起改为**矢量**方案（字体轮廓 → 多边形 → 三角面/四边形），并自带一个只在本机运行的**网页界面**，
可以边调参数边看 2D 对比与 3D 效果。

## 安装与运行

1. 下载并解压本分发包。
2. 运行 NATextGenerator.exe（Windows 可执行文件）。程序会启动网页界面并自动用浏览器打开
   （默认 `http://127.0.0.1:8765/`，端口被占用会自动往后找一个，控制台里会打印实际地址）。
   程序只监听本机回环地址，不会暴露到局域网。
3. 如果需要不同的字体，请把它们移动到 fonts 文件夹中（支持 `.ttf` / `.otf` / `.ttc`）。
   分发包里只附带思源黑体（SIL OPEN FONT LICENSE 1.1）；自行添加的字体请自行确认其许可是否允许你的使用场合。

关闭控制台窗口或按 Ctrl+C 即可结束程序。

## 使用说明

双击程序运行后，在弹出的网页界面里：

1、选择字体（也可以直接填字体完整路径，如 `C:\Windows\Fonts\msyh.ttc`）；
   设定字号（单位 m）、曲线质量（1~10，越大越贴合曲线、面数也越多）、厚度（单位 m）。
2、输入想要生成的文本（如 `CLLL` 或中文），换行会被当作空格。
3、右侧三个标签页分别是：**2D 叠图**（绿色是浏览器用同一字体渲染的参考，红色是生成的形状，黄色表示完全重合）、
   **3D 预览**（看厚度与立体效果；点一下画面进入旋转，再点一下退出，滚轮缩放）、
   **统计**（字符数、孔洞、零件数、耗时等）。
4、需要把文字贴到弯面上时，把「放置方式」改成「贝塞尔曲面」：
   用 4×4 个控制点（或"平直 / 水平弯 / 上下拱 / 波浪 / 穹顶"预设）调整曲面，
   并可以在 3D 视图里直接点选控制点、拖动 X/Y/Z 轴柄来移动它，勾选"吸附"还能按步长取整。
   控制点可以用归一化单位（以字高为 1）或实测单位（米）输入。
5、生成完毕后，刷新文件资源管理器，你会发现 output.na 文件。你可以把它复制或移动到 NavalArt 的存档文件夹中
   （通常位于 `C:\Users\你的用户名\AppData\LocalLow\RZEntertainment\NavalArt\ShipSaves`，
   或在 NavalArt 加载界面选择"浏览本地文件夹"），然后在游戏里载入它，
   用高级功能中的全选把它制作为自定义零件，然后放在你的船上。
6、程序可以继续生成文本，改参数会立刻重新生成；若要结束程序，请关闭控制台窗口或按 Ctrl+C。

如果不想用界面，也可以直接用命令行批量生成，例如：

```
NATextGenerator.exe -f SourceHanSansSC-Regular-2.otf -s 10 -q 5 -t "Navalart" -o output.na
```

常用参数：`-f/--font` 字体、`-t/--text` 文本、`--text-file` 从文本文件读取、`-s/--size` 字号（m）、
`-q/--quality` 曲线质量 1~10、`-e/--tolerance` 直接给曲线容差（em）、`-o/--output` 输出文件、
`-w/--thickness` 厚度（m）、`--no-quad` 不合并四边形、`--max-parts` 零件数上限、
`--surface "<48 个数>"` + `--surface-unit em|m` 贴到贝塞尔曲面、`--serve` 打开网页界面、`-h` 帮助。

## 从源码构建

### 目录结构

```
src/      C++ 源码
  font_outline.*   用 FreeType 取字形轮廓，并把贝塞尔曲线按弦高误差扁平化
  polygonize.*     判断轮廓嵌套关系，区分实心区域与孔洞
  earcut.h         孔洞多边形三角化算法(ISC)，triangulate.* 是它的封装
  mesh.*           网格优化：边界简化 / 保角翻转 / 三角面合并成四边形
  placement.*      放置方式：竖直平面、或双三次贝塞尔曲面(归一化/实测参数)
  na_writer.*      写出 NavalArt 存档(.na)，处理 8 个角点的环绕序
  generator.*      把上面几步串起来：文本 -> 零件多边形
  http_server.*    极简本地 HTTP 服务，webapp.* 是路由与 API
  platform.*       UTF-8 路径/参数、目录列举、调用浏览器
  web_assets.cpp   前端资源(由 web/ 生成，请勿手改)
web/      网页界面源码(index.html / app.js / style.css)，构建时内嵌进 exe
tools/    embed_web.py(把 web/ 转成 src/web_assets.cpp)、list_upload.py(列出应上传文件)
tests/    校验脚本(见下)
libs/     FreeType 头文件与静态库(已自带，无需另装)
fonts/    随包的思源黑体
legacy/   旧版光栅实现的源码与一份参照输出，仅作对照
```

### 依赖

* Windows + MinGW-w64 的 `g++`（C++17）与 `windres`（Dev-C++ 自带的即可）；
* FreeType：`libs/` 里已有头文件与静态库，不必另外安装；
* 可选：Python（重新生成内嵌前端资源、跑部分测试脚本）、Node.js（跑前端逻辑测试）、
  Pillow（跑图像对比测试）。

### 编译

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

这个脚本会依次：把 `web/` 内嵌成 `src/web_assets.cpp` → 用 `windres` 编译图标 → 用 g++ 编译并静态链接 FreeType，
产物是同目录下的 `NATextGenerator.exe`。可用参数：`-Clean`（先清理 `build/`）、`-NoWeb`（跳过前端内嵌）、
`-Out <文件名>`（指定输出名）。

也可以用 `make -f Makefile.win`（目标 `all` / `assets` / `clean`）。

注意：**如果 `NATextGenerator.exe` 正在运行**，链接阶段会报 `cannot open output file ... Permission denied`，
先关掉程序再编译（或者先把它改名再编译）。

### 改了网页界面之后

`web/` 下的文件会被内嵌进 exe，所以改完要重新编译；`build.ps1` 会自动调用
`python tools/embed_web.py` 重新生成 `src/web_assets.cpp`。如果本机没有 Python，可以用
`build.ps1 -NoWeb` 跳过这一步（沿用已有的 `src/web_assets.cpp`）。

调试界面时建议用固定端口、不自动开浏览器：

```powershell
.\NATextGenerator.exe --serve --no-open --port 8888
```

然后浏览器访问 `http://127.0.0.1:8888/`，改完前端重新编译再刷新页面即可。

### 测试脚本

```powershell
python tests\check_web.py         # 前端与后端接口/DOM 是否一致
node   tests\web_logic.js         # 用 DOM 桩直接跑 web/app.js，校验前端逻辑(不需要浏览器)
python tests\check_licenses.py    # readme/LICENSES 路径与许可文件完整性
python tests\legacy_layout.py <na文件>   # 角点环绕序是否与旧版存档一致(面朝向)
python tests\winding.py <na文件>  # 前脸(a,b,c)/(a,c,d) 两个三角面的朝向是否一致
python tests\verify.py <na文件> --font fonts\SourceHanSansSC-Regular-2.otf --text "文本" --size 10
                                  # 把 .na 栅格化，与 Pillow 渲染的参考图算 IoU 并输出叠图
python tests\glyph_report.py      # 逐字生成并与参考图比较，找出有问题的字形
python tests\quality.py <na文件>  # 零件数、四边/三角比例、最小角、大细长面占比
python tests\slivers.py <na文件>  # 列出最差的细长零件及其顶点
python tests\surface.py <na文件> --cp "<48 个数>"   # 校验贝塞尔曲面贴合(顶点是否落在等距面上)
python tests\surface_consistency.py <平面na> <曲面na> --cp "<48 个数>"
                                  # 曲面顶点反解回字形坐标，与平面模式逐点比对
python tests\preset_flat.py --port 8888   # 端到端校验网页预设("平直"应等价于平面模式)
```

`verify.py`、`surface.py` 等会在 `--out` 指定的位置写出预览图，`glyph_report.py` 会写临时目录；
自己生成的 `.na`、`tests/preview*.png`、`tests/samples/` 都已被 `.gitignore` 忽略，可以随时删除。

## 第三方库信用

本程序使用了 FreeType 库进行字体处理。部分代码基于 FreeType 项目的工作：
FreeType 项目：Portions of this software are copyright © 2025 The FreeType Project (https://freetype.org). All rights reserved.

本程序的多边形三角化使用了 earcut 算法：Copyright (c) 2016 Mapbox，ISC 许可证。

本程序由 MinGW-w64 工具链编译，静态链接了 GCC 运行时库（libstdc++、libgcc）与 MinGW-w64 的 C 运行时。

本软件按原样提供，无任何担保。详情请参阅附带的 LICENSE 文件。

## 程序说明

NATextGenerator 处于测试阶段，生成内容可能需要手动调整，请您见谅。

生成结果里每个三角面（或四边形）都是一个独立零件；文本很长时零件数会比较多，
建议降低曲线质量、加大容差，或调整"零件数上限"。
字形本身的尖锐特征（例如"大"的捺笔收笔尖）不可避免会产生少量细长零件。

部分字体可能不支持所有文本；本程序可能不支持某些字体类型（位图字体、字体里缺失的字符会被跳过并提示）。

程序使用了人工智能辅助编写。

## 支持与反馈

如有问题或建议，请联系 2442729142@qq.com 并说明来意。

本项目在 GitHub 上开源：https://github.com/DuangEveryAP/NATextGenerator

## 许可

本程序是自由软件，遵循 GPLv2 发布。

详情请见：https://www.gnu.org/licenses/

本程序的完整源码在项目仓库/GitHub 上公开提供，符合 GPLv2 对随二进制提供源码的要求。

本软件按原样提供，无任何明示或暗示担保。

详情请参阅 LICENSE。

## 第三方库及字体许可证

本软件使用了 FreeType 库。FreeType 同时以 FreeType Project License (FTL) 和 GNU GPL v2
两种许可发布，使用者需选择其一；FreeType 官方说明指出 FTL 与 GPLv2 不兼容，
因此本程序按**第二个选项（GPLv2）**使用 FreeType，与本程序自身的 GPLv2 许可一致。

FreeType 库按原样提供，无任何担保。

许可证详情见 "./LICENSES/LIBS/FreeType_LICENSE.TXT"（FreeType 的许可说明）、
"./docs/FTL.TXT"（FTL 全文，随包附带供参考）与 "./docs/GPLv2.TXT"（GPLv2 全文）。

本软件使用了 earcut 算法，其许可证为 ISC License。

earcut 按原样提供，无任何担保。

许可证详情见 "./LICENSES/LIBS/earcut.txt"。

本程序包含思源系列字体，许可证基于 SIL OPEN FONT LICENSE Version 1.1。

思源系列字体按原样提供，无任何担保。

详情见 "./LICENSES/FONTS/SourceHanSansSC_LICENSE.txt"。

分发包中只附带思源黑体这一款字体；本程序不再随包分发任何商业字体，
用户自行放入 fonts 文件夹的字体，其授权由用户自行负责。

本程序由 MinGW-w64 工具链编译，静态链接了 GCC 运行时库（GPLv3 + GCC 运行时例外，
该例外允许静态链接且不要求本程序改用 GPLv3）与 MinGW-w64 的 C 运行时（Zope Public License 2.1 等）。

详情见 "./LICENSES/LIBS/MinGW-w64_runtime.txt"。
