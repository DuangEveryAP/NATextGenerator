// NATextGenerator - 平台相关小工具
// Windows 下进程参数、文件路径都可能是非 ASCII(中文)，需要显式按 UTF-8 处理，
// 否则中文文本/字体名会被 ANSI 代码页破坏。
#ifndef NATG_PLATFORM_H
#define NATG_PLATFORM_H

#include <cstdio>
#include <string>
#include <vector>

namespace natg {

// 以 UTF-8 路径打开文件(Windows 下内部走 _wfopen)
std::FILE *fopenUtf8(const std::string &path, const char *mode);

// 读取整个文件为 UTF-8 文本(自动去掉 BOM)
bool readFileUtf8(const std::string &path, std::string &out);

// 读取整个文件为二进制
bool readFileBinary(const std::string &path, std::vector<unsigned char> &out);

// 取 UTF-8 编码的命令行参数(Windows 下用 GetCommandLineW 重新解析)
std::vector<std::string> getUtf8Args(int argc, char **argv);

// 把控制台输入输出切成 UTF-8
void setupConsoleUtf8();

// 列出目录下指定扩展名的文件(返回文件名，不含目录)
std::vector<std::string> listDirFiles(const std::string &dirUtf8, const std::vector<std::string> &extensions);

// 用默认浏览器打开 URL
void openInBrowser(const std::string &url);

// 取 exe 所在目录(UTF-8，末尾带分隔符)
std::string executableDir();

} // namespace natg

#endif // NATG_PLATFORM_H
