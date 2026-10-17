#include "platform.h"

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <cwctype>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <stringapiset.h>
#endif

namespace natg {
namespace {

#ifdef _WIN32
std::wstring utf8ToWide(const std::string &s) {
	if (s.empty()) return std::wstring();
	int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
	if (n <= 0) return std::wstring();
	std::wstring w(static_cast<size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
	return w;
}

std::string wideToUtf8(const wchar_t *s, int len) {
	if (!s || len <= 0) return std::string();
	int n = WideCharToMultiByte(CP_UTF8, 0, s, len, nullptr, 0, nullptr, nullptr);
	if (n <= 0) return std::string();
	std::string out(static_cast<size_t>(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, s, len, &out[0], n, nullptr, nullptr);
	return out;
}
#endif

} // namespace

std::FILE *fopenUtf8(const std::string &path, const char *mode) {
#ifdef _WIN32
	std::wstring wpath = utf8ToWide(path);
	std::wstring wmode;
	for (const char *p = mode; *p; ++p) wmode.push_back(static_cast<wchar_t>(*p));
	return _wfopen(wpath.c_str(), wmode.c_str());
#else
	return std::fopen(path.c_str(), mode);
#endif
}

bool readFileBinary(const std::string &path, std::vector<unsigned char> &out) {
	std::FILE *f = fopenUtf8(path, "rb");
	if (!f) return false;
	out.clear();
	unsigned char buf[65536];
	size_t n;
	while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
		out.insert(out.end(), buf, buf + n);
	}
	std::fclose(f);
	return true;
}

bool readFileUtf8(const std::string &path, std::string &out) {
	std::FILE *f = fopenUtf8(path, "rb");
	if (!f) return false;
	out.clear();
	char buf[8192];
	size_t n;
	while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
	std::fclose(f);
	if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
	        static_cast<unsigned char>(out[1]) == 0xBB && static_cast<unsigned char>(out[2]) == 0xBF) {
		out.erase(0, 3);
	}
	return true;
}

std::vector<std::string> getUtf8Args(int argc, char **argv) {
	std::vector<std::string> out;
#ifdef _WIN32
	int wargc = 0;
	LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
	if (wargv && wargc > 0) {
		out.reserve(static_cast<size_t>(wargc));
		for (int i = 0; i < wargc; i++) {
			out.push_back(wideToUtf8(wargv[i], static_cast<int>(wcslen(wargv[i]))));
		}
		LocalFree(wargv);
		return out;
	}
#endif
	out.reserve(static_cast<size_t>(argc));
	for (int i = 0; i < argc; i++) out.push_back(argv[i]);
	return out;
}

void setupConsoleUtf8() {
#ifdef _WIN32
	SetConsoleOutputCP(65001);
	SetConsoleCP(65001);
#endif
}

std::vector<std::string> listDirFiles(const std::string &dirUtf8, const std::vector<std::string> &extensions) {
	std::vector<std::string> out;
#ifdef _WIN32
	std::wstring pattern = utf8ToWide(dirUtf8) + L"\\*";
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return out;
	do {
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		const std::wstring name(fd.cFileName);
		const size_t dot = name.find_last_of(L'.');
		if (dot == std::wstring::npos) continue;
		std::wstring ext = name.substr(dot);
		for (wchar_t &c : ext) c = static_cast<wchar_t>(towlower(c));
		for (const std::string &want : extensions) {
			std::wstring wext = utf8ToWide(want);
			for (wchar_t &c : wext) c = static_cast<wchar_t>(towlower(c));
			if (ext == wext) {
				out.push_back(wideToUtf8(name.c_str(), static_cast<int>(name.size())));
				break;
			}
		}
	} while (FindNextFileW(h, &fd));
	FindClose(h);
#else
	(void)dirUtf8;
	(void)extensions;
#endif
	std::sort(out.begin(), out.end());
	return out;
}

void openInBrowser(const std::string &url) {
#ifdef _WIN32
	ShellExecuteW(nullptr, L"open", utf8ToWide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
	(void)url;
#endif
}

std::string executableDir() {
#ifdef _WIN32
	wchar_t buf[MAX_PATH * 4];
	DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
	if (n == 0) return std::string("./");
	std::wstring path(buf, n);
	const size_t slash = path.find_last_of(L"\\/");
	if (slash == std::wstring::npos) return std::string("./");
	std::wstring dir = path.substr(0, slash + 1);
	return wideToUtf8(dir.c_str(), static_cast<int>(dir.size()));
#else
	return std::string("./");
#endif
}

} // namespace natg
