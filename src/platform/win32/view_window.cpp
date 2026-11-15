#include "platform/win32/view_window.h"

#include <gdiplus.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <memory>

#include "core/document.h"
#include "platform/win32/container.h"
#include "render/view.h"

namespace mdview::win32 {

namespace {

constexpr wchar_t kClassName[] = L"MdViewWindow";
constexpr UINT WM_APP_LOADED = WM_APP + 0x4D44;  // 'MD'
constexpr int kLineStep = 48;

HMODULE this_module() {
    HMODULE m = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&this_module), &m);
    return m;
}

// State shared between the UI thread and a loader thread.
struct LoadJob {
    std::filesystem::path path;
    std::atomic<bool> cancelled{false};
    std::shared_ptr<Document> doc;
    std::string error;
    double seconds = 0;
};

struct LoaderArg {
    std::shared_ptr<LoadJob> job;
    HWND hwnd;
    HMODULE module;  // reference held so a plugin DLL can't unload under the thread
};

DWORD WINAPI loader_thread(LPVOID p) {
    auto* arg = static_cast<LoaderArg*>(p);
    HMODULE module = arg->module;
    {
        auto job = arg->job;
        HWND hwnd = arg->hwnd;
        delete arg;
        auto t0 = std::chrono::steady_clock::now();
        try {
            job->doc = Document::load_file(job->path, {}, &job->error);
        } catch (const std::exception& e) {
            job->error = e.what();
        } catch (...) {
            job->error = "failed to load";
        }
        job->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (!job->cancelled) PostMessageW(hwnd, WM_APP_LOADED, 0, 0);
    }
    FreeLibraryAndExitThread(module, 0);
}

float dpi_scale(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static auto fn = reinterpret_cast<GetDpiForWindowFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    UINT dpi = fn ? fn(hwnd) : 96;
    return dpi ? dpi / 96.0f : 1.0f;
}

void fill_rgb(HDC dc, const RECT& rc, std::uint32_t rgb) {
    HBRUSH b = CreateSolidBrush(RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF));
    FillRect(dc, &rc, b);
    DeleteObject(b);
}

void draw_highlights(HDC dc, const std::vector<litehtml::position>& rects) {
    if (rects.empty()) return;
    Gdiplus::Graphics g(dc);
    Gdiplus::SolidBrush brush(Gdiplus::Color(110, 255, 196, 0));
    for (const auto& r : rects)
        g.FillRectangle(&brush, static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.width),
                        static_cast<float>(r.height));
}

bool set_clipboard_text(HWND hwnd, const std::wstring& text) {
    if (!OpenClipboard(hwnd)) return false;
    EmptyClipboard();
    std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool ok = false;
    if (mem) {
        memcpy(GlobalLock(mem), text.c_str(), bytes);
        GlobalUnlock(mem);
        ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
        if (!ok) GlobalFree(mem);
    }
    CloseClipboard();
    return ok;
}

}  // namespace

class ViewWindow {
public:
    ViewWindow(HWND hwnd, const ViewWindowOptions& options) : hwnd_(hwnd), options_(options), view_(&container_) {
        container_.viewport = [this](int& w, int& h) {
            w = view_.width();
            h = view_.height();
        };
        container_.on_link = [this](const std::string& url) { pending_link_ = url; };
        view_.set_dark(options_.dark);
        apply_zoom();
    }

    ~ViewWindow() {
        if (job_) job_->cancelled = true;
        if (back_bmp_) DeleteObject(back_bmp_);
        if (back_dc_) DeleteDC(back_dc_);
    }

    void open(const std::filesystem::path& path) {
        if (job_) job_->cancelled = true;
        job_ = std::make_shared<LoadJob>();
        job_->path = path;
        status_ = L"Loading…";
        view_.set_document(nullptr);
        select_all_ = false;
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&loader_thread), &module);
        auto* arg = new LoaderArg{job_, hwnd_, module};
        HANDLE th = CreateThread(nullptr, 0, loader_thread, arg, 0, nullptr);
        if (th) {
            CloseHandle(th);
        } else {
            delete arg;
            FreeLibrary(module);
            status_ = L"Error: cannot start loader thread";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void on_loaded() {
        auto job = job_;
        if (!job || job->cancelled) return;
        if (!job->doc) {
            status_ = L"Cannot open file: " + utf8_to_wide(job->error);
            if (options_.loaded) options_.loaded(false);
        } else {
            status_.clear();
            container_.set_base_dir(job->doc->base_dir().wstring());
            view_.set_document(job->doc);
            if (options_.loaded) options_.loaded(true);
        }
        update_scrollbar();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    bool find(const std::wstring& needle, bool match_case, bool backwards, bool from_start) {
        bool ok = view_.find(wide_to_utf8(needle), match_case, backwards, from_start);
        update_scrollbar();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return ok;
    }

    void select_all() { select_all_ = true; }

    void copy() {
        auto doc = view_.document();
        if (!doc) return;
        // No text selection yet: copy the whole Markdown source after
        // select-all, otherwise nothing.
        if (select_all_) set_clipboard_text(hwnd_, utf8_to_wide(doc->text()));
    }

    void set_dark(bool dark) {
        view_.set_dark(dark);
        update_scrollbar();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    int percent() const {
        std::int64_t total = view_.content_height();
        if (total <= 0) return 0;
        return static_cast<int>(std::min<std::int64_t>(100, (view_.scroll_y() + view_.height()) * 100 / total));
    }

    void scroll_to_percent(int p) {
        view_.scroll_to(view_.content_height() * std::clamp(p, 0, 100) / 100);
        update_scrollbar();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_APP_LOADED: on_loaded(); return 0;
            case WM_SIZE: {
                view_.resize(LOWORD(lp), HIWORD(lp));
                update_scrollbar();
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
            case WM_DPICHANGED_AFTERPARENT: apply_zoom(); return 0;
            case WM_ERASEBKGND: return 1;
            case WM_PAINT: paint(); return 0;
            case WM_VSCROLL: on_vscroll(LOWORD(wp)); return 0;
            case WM_MOUSEWHEEL: {
                int delta = GET_WHEEL_DELTA_WPARAM(wp);
                if (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) {
                    user_zoom_ *= delta > 0 ? 1.1f : 1.0f / 1.1f;
                    apply_zoom();
                } else {
                    UINT lines = 3;
                    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
                    scroll_by(-static_cast<std::int64_t>(delta) * static_cast<int>(lines) * kLineStep / 3 / WHEEL_DELTA);
                }
                return 0;
            }
            case WM_MOUSEMOVE: {
                if (!tracking_) {
                    TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                    tracking_ = TrackMouseEvent(&tme) != FALSE;
                }
                if (view_.mouse_move(GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) InvalidateRect(hwnd_, nullptr, FALSE);
                update_cursor();
                return 0;
            }
            case WM_MOUSELEAVE:
                tracking_ = false;
                if (view_.mouse_leave()) InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            case WM_SETCURSOR:
                if (LOWORD(lp) == HTCLIENT) {
                    update_cursor();
                    return TRUE;
                }
                break;
            case WM_LBUTTONDOWN:
                SetFocus(hwnd_);
                if (view_.mouse_down(GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            case WM_LBUTTONUP: {
                pending_link_.clear();
                if (view_.mouse_up(GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) InvalidateRect(hwnd_, nullptr, FALSE);
                if (!pending_link_.empty()) follow_link(pending_link_);
                return 0;
            }
            case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
            case WM_KEYDOWN:
                if (on_key(wp)) return 0;
                if (options_.unhandled_key && options_.unhandled_key(msg, wp, lp)) return 0;
                break;
            case WM_CHAR:
            case WM_SYSKEYDOWN:
                if (options_.unhandled_key && options_.unhandled_key(msg, wp, lp)) return 0;
                break;
        }
        return DefWindowProcW(hwnd_, msg, wp, lp);
    }

    void render_to(HDC dc, const RECT& rc) {
        fill_rgb(dc, rc, view_.background_rgb());
        if (!status_.empty() || !view_.document()) {
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, view_.dark() ? RGB(145, 152, 161) : RGB(89, 99, 110));
            HFONT font = CreateFontW(-static_cast<int>(15 * dpi_), 0, 0, 0, FW_NORMAL, TRUE, 0, 0, DEFAULT_CHARSET, 0, 0,
                                     CLEARTYPE_QUALITY, 0, L"Segoe UI");
            HGDIOBJ old = SelectObject(dc, font);
            RECT t = rc;
            InflateRect(&t, -static_cast<int>(24 * dpi_), -static_cast<int>(20 * dpi_));
            DrawTextW(dc, status_.c_str(), -1, &t, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(dc, old);
            DeleteObject(font);
            return;
        }
        litehtml::position clip(static_cast<litehtml::pixel_t>(rc.left), static_cast<litehtml::pixel_t>(rc.top),
                                static_cast<litehtml::pixel_t>(rc.right - rc.left), static_cast<litehtml::pixel_t>(rc.bottom - rc.top));
        view_.paint(reinterpret_cast<litehtml::uint_ptr>(dc), clip);
        draw_highlights(dc, view_.highlight_rects());
    }

    View& view() { return view_; }

private:
    void apply_zoom() {
        dpi_ = dpi_scale(hwnd_);
        view_.set_zoom(user_zoom_ * options_.zoom * dpi_);
        update_scrollbar();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void paint() {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd_, &ps);
        RECT rc;
        GetClientRect(hwnd_, &rc);
        int w = std::max(1L, rc.right), h = std::max(1L, rc.bottom);
        if (!back_dc_ || back_w_ != w || back_h_ != h) {
            if (back_bmp_) DeleteObject(back_bmp_);
            if (!back_dc_) back_dc_ = CreateCompatibleDC(dc);
            back_bmp_ = CreateCompatibleBitmap(dc, w, h);
            SelectObject(back_dc_, back_bmp_);
            back_w_ = w;
            back_h_ = h;
        }
        render_to(back_dc_, rc);
        BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
               ps.rcPaint.bottom - ps.rcPaint.top, back_dc_, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        EndPaint(hwnd_, &ps);
        update_scrollbar();  // layout may have refined heights
    }

    // Scrollbar positions are int; scale large documents down.
    void update_scrollbar() {
        std::int64_t total = view_.document() ? view_.content_height() : 0;
        scroll_scale_ = std::max<std::int64_t>(1, total / (INT_MAX / 2) + 1);
        SCROLLINFO si{sizeof(si)};
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
        si.nMin = 0;
        si.nMax = static_cast<int>(total / scroll_scale_);
        si.nPage = static_cast<UINT>(view_.height() / scroll_scale_);
        si.nPos = static_cast<int>(view_.scroll_y() / scroll_scale_);
        SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
    }

    void on_vscroll(WORD code) {
        int page = std::max(kLineStep, view_.height() - kLineStep);
        switch (code) {
            case SB_LINEUP: scroll_by(-kLineStep); break;
            case SB_LINEDOWN: scroll_by(kLineStep); break;
            case SB_PAGEUP: scroll_by(-page); break;
            case SB_PAGEDOWN: scroll_by(page); break;
            case SB_TOP: view_.scroll_home(); refresh(); break;
            case SB_BOTTOM: view_.scroll_end(); refresh(); break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: {
                SCROLLINFO si{sizeof(si)};
                si.fMask = SIF_TRACKPOS;
                GetScrollInfo(hwnd_, SB_VERT, &si);
                view_.scroll_to(static_cast<std::int64_t>(si.nTrackPos) * scroll_scale_);
                refresh();
                break;
            }
        }
    }

    void scroll_by(std::int64_t dy) {
        view_.scroll_by(dy);
        refresh();
    }

    void refresh() {
        update_scrollbar();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    bool on_key(WPARAM key) {
        bool ctrl = GetKeyState(VK_CONTROL) < 0;
        bool shift = GetKeyState(VK_SHIFT) < 0;
        int page = std::max(kLineStep, view_.height() - kLineStep);
        switch (key) {
            case VK_UP: scroll_by(-kLineStep); return true;
            case VK_DOWN: scroll_by(kLineStep); return true;
            case VK_PRIOR: scroll_by(-page); return true;
            case VK_NEXT: scroll_by(page); return true;
            case VK_SPACE: scroll_by(shift ? -page : page); return true;
            case VK_HOME: view_.scroll_home(); refresh(); return true;
            case VK_END: view_.scroll_end(); refresh(); return true;
            case VK_ADD:
            case VK_OEM_PLUS:
                if (!ctrl) return false;
                user_zoom_ *= 1.1f;
                apply_zoom();
                return true;
            case VK_SUBTRACT:
            case VK_OEM_MINUS:
                if (!ctrl) return false;
                user_zoom_ /= 1.1f;
                apply_zoom();
                return true;
            case '0':
            case VK_NUMPAD0:
                if (!ctrl) return false;
                user_zoom_ = 1.0f;
                apply_zoom();
                return true;
            case 'A':
                if (!ctrl) return false;
                select_all();
                return true;
            case 'C':
            case VK_INSERT:
                if (!ctrl) return false;
                copy();
                return true;
        }
        return false;
    }

    void update_cursor() {
        SetCursor(LoadCursorW(nullptr, container_.cursor() == "pointer" ? IDC_HAND : IDC_ARROW));
    }

    void follow_link(const std::string& href) {
        std::wstring url = utf8_to_wide(href);
        if (url.empty() || url[0] == L'#') return;  // in-page anchors: not supported yet
        if (url.find(L"://") != std::wstring::npos || _wcsnicmp(url.c_str(), L"mailto:", 7) == 0) {
            if (_wcsnicmp(url.c_str(), L"http://", 7) == 0 || _wcsnicmp(url.c_str(), L"https://", 8) == 0 ||
                _wcsnicmp(url.c_str(), L"mailto:", 7) == 0)
                ShellExecuteW(hwnd_, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }
        // Relative/local path: strip fragment, resolve against the document folder.
        std::size_t hash = url.find(L'#');
        if (hash != std::wstring::npos) url.resize(hash);
        auto doc = view_.document();
        std::filesystem::path p(url);
        if (p.is_relative() && doc) p = doc->base_dir() / p;
        std::error_code ec;
        if (!std::filesystem::exists(p, ec)) return;
        std::wstring ext = p.extension().wstring();
        if (_wcsicmp(ext.c_str(), L".md") == 0 || _wcsicmp(ext.c_str(), L".markdown") == 0) {
            open(p);  // navigate inside the viewer
        } else {
            ShellExecuteW(hwnd_, L"open", p.c_str(), nullptr, p.parent_path().c_str(), SW_SHOWNORMAL);
        }
    }

    HWND hwnd_;
    ViewWindowOptions options_;
    Container container_;
    View view_;
    std::shared_ptr<LoadJob> job_;
    std::wstring status_ = L"Loading…";
    std::string pending_link_;
    bool select_all_ = false;
    bool tracking_ = false;
    float user_zoom_ = 1.0f;
    float dpi_ = 1.0f;
    std::int64_t scroll_scale_ = 1;
    HDC back_dc_ = nullptr;
    HBITMAP back_bmp_ = nullptr;
    int back_w_ = 0, back_h_ = 0;
};

namespace {

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<ViewWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        auto* opts = static_cast<const ViewWindowOptions*>(cs->lpCreateParams);
        try {
            self = new ViewWindow(hwnd, *opts);
        } catch (...) {
            return FALSE;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete self;
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    try {
        return self->handle(msg, wp, lp);
    } catch (...) {
        // Never let an exception escape into the host (Total Commander).
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

bool register_class() {
    static bool done = false;
    if (done) return true;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = this_module();
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    done = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    return done;
}

}  // namespace

HWND create_view_window(HWND parent, const RECT& rc, const ViewWindowOptions& options) {
    if (!register_class()) return nullptr;
    return CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | WS_CLIPCHILDREN,
                           rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, parent, nullptr, this_module(),
                           const_cast<ViewWindowOptions*>(&options));
}

ViewWindow* view_from_hwnd(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return nullptr;
    wchar_t cls[64];
    if (!GetClassNameW(hwnd, cls, 64) || wcscmp(cls, kClassName) != 0) return nullptr;
    return reinterpret_cast<ViewWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void view_open(HWND hwnd, const std::filesystem::path& path) {
    if (auto* v = view_from_hwnd(hwnd)) v->open(path);
}

bool view_find(HWND hwnd, const std::wstring& needle, bool match_case, bool backwards, bool from_start) {
    auto* v = view_from_hwnd(hwnd);
    return v && v->find(needle, match_case, backwards, from_start);
}

void view_select_all(HWND hwnd) {
    if (auto* v = view_from_hwnd(hwnd)) v->select_all();
}

void view_copy(HWND hwnd) {
    if (auto* v = view_from_hwnd(hwnd)) v->copy();
}

void view_set_dark(HWND hwnd, bool dark) {
    if (auto* v = view_from_hwnd(hwnd)) v->set_dark(dark);
}

int view_percent(HWND hwnd) {
    auto* v = view_from_hwnd(hwnd);
    return v ? v->percent() : 0;
}

void view_scroll_to_percent(HWND hwnd, int percent) {
    if (auto* v = view_from_hwnd(hwnd)) v->scroll_to_percent(percent);
}

bool render_file_to_png(const std::filesystem::path& md, const std::filesystem::path& png, int width, int height,
                        std::int64_t scroll_y, float zoom, bool dark, std::string* stats) {
    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &gsi, nullptr) != Gdiplus::Ok) return false;
    bool ok = false;
    {
        auto t0 = std::chrono::steady_clock::now();
        std::string error;
        auto doc = Document::load_file(md, {}, &error);
        if (doc) {
            auto t1 = std::chrono::steady_clock::now();
            Container container;
            container.set_base_dir(doc->base_dir().wstring());
            View view(&container);
            container.viewport = [&](int& w, int& h) {
                w = width;
                h = height;
            };
            view.set_dark(dark);
            view.set_zoom(zoom);
            view.resize(width, height);
            view.set_document(doc);
            view.scroll_to(scroll_y);

            HDC screen = GetDC(nullptr);
            HDC dc = CreateCompatibleDC(screen);
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
            bi.bmiHeader.biWidth = width;
            bi.bmiHeader.biHeight = -height;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            void* bits = nullptr;
            HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            ReleaseDC(nullptr, screen);
            HGDIOBJ old = SelectObject(dc, bmp);
            RECT rc{0, 0, width, height};
            fill_rgb(dc, rc, view.background_rgb());
            view.paint(reinterpret_cast<litehtml::uint_ptr>(dc),
                       litehtml::position(0, 0, static_cast<litehtml::pixel_t>(width), static_cast<litehtml::pixel_t>(height)));
            auto t2 = std::chrono::steady_clock::now();
            SelectObject(dc, old);

            CLSID png_clsid{};
            UINT num = 0, size = 0;
            Gdiplus::GetImageEncodersSize(&num, &size);
            std::vector<BYTE> buf(size);
            auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
            Gdiplus::GetImageEncoders(num, size, encoders);
            for (UINT i = 0; i < num; i++)
                if (wcscmp(encoders[i].MimeType, L"image/png") == 0) png_clsid = encoders[i].Clsid;
            {
                Gdiplus::Bitmap out(bmp, nullptr);
                ok = out.Save(png.c_str(), &png_clsid, nullptr) == Gdiplus::Ok;
            }
            DeleteObject(bmp);
            DeleteDC(dc);
            if (stats) {
                char s[256];
                snprintf(s, sizeof(s),
                         "load+chunk=%.3fs layout+paint=%.3fs chunks=%zu laid_out=%zu cached=%zu height=%lld",
                         std::chrono::duration<double>(t1 - t0).count(), std::chrono::duration<double>(t2 - t1).count(),
                         doc->chunks().size(), view.laid_out_chunks(), view.cached_docs(),
                         static_cast<long long>(view.content_height()));
                *stats = s;
            }
        } else if (stats) {
            *stats = "load failed: " + error;
        }
    }
    Gdiplus::GdiplusShutdown(token);
    return ok;
}

}  // namespace mdview::win32
