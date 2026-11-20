#include "server/browser_launcher.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <cstdlib>
#endif

namespace wb::api {

#ifdef _WIN32
namespace {
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
bool shellOpen(const wchar_t* file, const std::wstring& params) {
    const auto rc = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", file, params.empty() ? nullptr : params.c_str(), nullptr, SW_SHOWNORMAL));
    return rc > 32;  // ShellExecute convention: >32 means success
}
}  // namespace

std::string openUserInterface(const std::string& url) {
    const std::wstring wurl = widen(url);
    if (shellOpen(L"msedge.exe", L"--app=" + wurl + L" --start-maximized")) {
        return "opened in Microsoft Edge (app window)";
    }
    if (shellOpen(wurl.c_str(), L"")) return "opened in the default browser";
    return "could not open a browser automatically; open " + url + " manually";
}
#else
std::string openUserInterface(const std::string& url) {
    const std::string cmd = "xdg-open '" + url + "' >/dev/null 2>&1 &";
    return std::system(cmd.c_str()) == 0 ? "opened with xdg-open" : "open " + url + " manually";
}
#endif

}  // namespace wb::api
