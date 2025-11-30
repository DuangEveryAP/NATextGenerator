NATextGenerator

简介

NATextGenerator是一个程序，用于给软件 NavalArt 生成文本，对输入的文字生成对应的NavalArt存档。

安装与运行

1.	下载并解压本分发包。
2.	运行NATextGenerator.exe（Windows 可执行文件）。
3.	如果需要不同的字体，请将它们移动到fonts文件夹中 。

使用说明

双击程序运行后：
1、请输入带后缀的字体文件名。
2、输入字体精度（建议在100~500）和大小（单位：m）。
3、输入想要生成的文本（如CLLL）并结束输入。
4、生成完毕后，刷新文件资源管理器，你会发现output.na文件。你可以把它复制或移动到NavalArt的存档文件夹中（通常位于C:\Users\你的用户名\AppData\LocalLow\RZEntertainment\NavalArt\ShipSaves或在NavalArt加载界面选择浏览本地文件夹）。
5、程序可以继续生成文本，若要结束程序，请按下Ctrl+C或点击右上角关闭程序。

第三方库信用

本程序使用了 FreeType 库进行字体处理。部分代码基于 FreeType 项目的工作：
FreeType 项目：Portions of this software are copyright © 2025 The FreeType Project (https://freetype.org). All rights reserved.
本软件按原样提供，无任何担保。详情请参阅附带的 LICENSE 文件。

程序说明

NATextGenerator处于测试阶段，生成内容不稳定，需要手动调整，请您见谅。
部分字体可能不支持所有文本。
本程序可能不支持某些字体类型。
支持与反馈
如有问题或建议，请联系2442729142@qq.com或wu_xingjian@126.com并说明来意。
本项目在GitHub上开源：https://github.com/DuangEveryAP/NATextGenerator

许可

版权所有 (c) 2025 WuXingJian。保留所有权利。
本程序是自由软件，遵循GPLv2发布。
详情请见：https://www.gnu.org/licenses/
本软件按原样提供，无任何明示或暗示担保。
详情请参阅LICENSE。

第三方库及字体许可证

本软件使用了 FreeType 库，其许可证为 FreeType Project License (FTL)。
FreeType 库按原样提供，无任何担保。
许可证详情见"./LICENSES/LIBS/FreeTypeLICENSE.txt"和"./docs/FTL.TXT"。

本程序包含思源系列字体，许可证基于SIL OPEN FONT LICENSE Version 1.1。
思源系列字体按原样提供，无任何担保。
详情见"./LICENSES/FONTS/SourceHanSansSC_LICENSE.txt"。

本程序使用了 MinGW-64 运行时库（基于 MIT 许可证）。
详情见"./LICENSES/LIBS/MIT.txt"
