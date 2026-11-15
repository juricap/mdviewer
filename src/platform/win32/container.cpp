#include "platform/win32/container.h"

#include <gdiplus.h>
#include <shlwapi.h>

namespace mdview::win32 {

std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

Container::Container() = default;

Container::~Container() { clear_images(); }

void Container::on_anchor_click(const char* url, const litehtml::element::ptr&) {
    if (on_link && url) on_link(url);
}

void Container::import_css(litehtml::string&, const litehtml::string&, litehtml::string&) {
    // External stylesheets are never loaded (no network, no surprises).
}

void Container::get_viewport(litehtml::position& vp) const {
    int w = 800, h = 600;
    if (viewport) viewport(w, h);
    vp = litehtml::position(0, 0, static_cast<litehtml::pixel_t>(w), static_cast<litehtml::pixel_t>(h));
}

void Container::make_url(LPCWSTR url, LPCWSTR, std::wstring& out) {
    std::wstring u = url ? url : L"";
    // file:///C:/x.png -> C:\x.png
    if (_wcsnicmp(u.c_str(), L"file:", 5) == 0) {
        wchar_t path[MAX_PATH * 4];
        DWORD len = static_cast<DWORD>(std::size(path));
        if (SUCCEEDED(PathCreateFromUrlW(u.c_str(), path, &len, 0))) {
            out = path;
            return;
        }
    }
    if (u.find(L"://") != std::wstring::npos || PathIsURLW(u.c_str())) {
        out = u;  // remote: kept as-is, never fetched
        return;
    }
    for (auto& c : u)
        if (c == L'/') c = L'\\';
    if (PathIsRelativeW(u.c_str()) && !base_dir_.empty()) {
        wchar_t combined[MAX_PATH * 4];
        if (PathCombineW(combined, base_dir_.c_str(), u.c_str())) {
            out = combined;
            return;
        }
    }
    out = u;
}

litehtml::uint_ptr Container::get_image(LPCWSTR url_or_path, bool) {
    if (!url_or_path || wcsstr(url_or_path, L"://")) return 0;  // local files only
    auto* bmp = Gdiplus::Bitmap::FromFile(url_or_path);
    if (!bmp) return 0;
    if (bmp->GetLastStatus() != Gdiplus::Ok) {
        delete bmp;
        return 0;
    }
    return reinterpret_cast<litehtml::uint_ptr>(bmp);
}

}  // namespace mdview::win32
