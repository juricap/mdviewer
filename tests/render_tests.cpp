// Render tests: litehtml layout + painting through the platform container,
// headless. Windows uses the GDI/GDI+ container, other platforms cairo/pango.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>
#include "platform/win32/container.h"
#include "platform/win32/view_window.h"
#else
#include "platform/cairo/container.h"
#endif

#include "core/document.h"
#include "render/view.h"

using namespace mdview;

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                              \
        }                                                              \
    } while (0)

static double seconds_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

static litehtml::position full(int w, int h) {
    return litehtml::position(0, 0, static_cast<litehtml::pixel_t>(w), static_cast<litehtml::pixel_t>(h));
}

// --- per-platform backend ----------------------------------------------------
#ifdef _WIN32
struct TestContainer : win32::Container {
    TestContainer(int w, int h) {
        viewport = [w, h](int& vw, int& vh) {
            vw = w;
            vh = h;
        };
    }
    void base(const std::filesystem::path& dir) { set_base_dir(dir.wstring()); }
};

// Paints the view into a 32-bit DIB; returns the number of non-background pixels.
static long paint_and_count(View& view, int w, int h) {
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    HGDIOBJ old = SelectObject(dc, bmp);
    std::uint32_t bg = view.background_rgb();
    HBRUSH brush = CreateSolidBrush(RGB(bg >> 16, (bg >> 8) & 0xFF, bg & 0xFF));
    RECT rc{0, 0, w, h};
    FillRect(dc, &rc, brush);
    DeleteObject(brush);
    view.paint(reinterpret_cast<litehtml::uint_ptr>(dc), full(w, h));
    GdiFlush();
    long differ = 0;
    auto* px = static_cast<const std::uint32_t*>(bits);
    for (int i = 0; i < w * h; i++)
        if ((px[i] & 0xFFFFFF) != bg) differ++;
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    return differ;
}

static bool render_png(const std::filesystem::path& md, const std::filesystem::path& png, std::string* stats) {
    return win32::render_file_to_png(md, png, 800, 1000, 0, 1.0f, false, stats);
}
#else
struct TestContainer : cairo::Container {
    TestContainer(int w, int h) {
        viewport_width = w;
        viewport_height = h;
    }
    void base(const std::filesystem::path& dir) { set_base_dir(dir); }
};

static long paint_and_count(View& view, int w, int h) {
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cairo_t* cr = cairo_create(s);
    std::uint32_t bg = view.background_rgb();
    cairo_set_source_rgb(cr, ((bg >> 16) & 0xFF) / 255.0, ((bg >> 8) & 0xFF) / 255.0, (bg & 0xFF) / 255.0);
    cairo_paint(cr);
    view.paint(reinterpret_cast<litehtml::uint_ptr>(cr), full(w, h));
    cairo_surface_flush(s);
    long differ = 0;
    const unsigned char* data = cairo_image_surface_get_data(s);
    int stride = cairo_image_surface_get_stride(s);
    for (int y = 0; y < h; y++) {
        auto* row = reinterpret_cast<const std::uint32_t*>(data + static_cast<std::size_t>(y) * stride);
        for (int x = 0; x < w; x++)
            if ((row[x] & 0xFFFFFF) != bg) differ++;
    }
    cairo_destroy(cr);
    cairo_surface_destroy(s);
    return differ;
}

static bool render_png(const std::filesystem::path& md, const std::filesystem::path& png, std::string* stats) {
    return cairo::render_file_to_png(md, png, 800, 1000, 0, 1.0f, false, stats);
}
#endif

// --- tests -------------------------------------------------------------------
static void test_features() {
    TestContainer container(900, 1200);
    View view(&container);
    auto doc = Document::load_file(MDVIEW_FIXTURES "/features.md");
    CHECK(doc != nullptr);
    if (!doc) return;
    container.base(doc->base_dir());
    view.resize(900, 1200);
    view.set_document(doc);
    long ink = paint_and_count(view, 900, 1200);
    std::printf("features.md: content height %lld px, %ld non-background pixels\n",
                static_cast<long long>(view.content_height()), ink);
    CHECK(view.content_height() > 800);
    CHECK(ink > 20000);

    // Find in rendered text, forwards and backwards.
    CHECK(view.find("Center", false, false, true));
    CHECK(!view.highlight_rects().empty());
    CHECK(view.find("nested", false, false, false));
    CHECK(!view.find("no-such-text-anywhere", false, false, true));
    CHECK(view.find("feature", false, true, true));

    // Dark mode paints a different background.
    view.set_dark(true);
    CHECK(view.background_rgb() != 0xFFFFFF);
    CHECK(paint_and_count(view, 900, 1200) > 20000);
}

static void test_large_document() {
    std::string md;
    int i = 0;
    while (md.size() < 20u * 1024u * 1024u) {
        md += "## Section " + std::to_string(i++) + "\n\nSome **bold**, *italic*, `code` and a [link](u).\n\n";
        md += "- a\n- b\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n";
    }
    md += "## The End\n\nfinal-marker\n";
    auto t0 = std::chrono::steady_clock::now();
    auto doc = Document::from_bytes(std::move(md));
    double load_s = seconds_since(t0);

    TestContainer container(1000, 900);
    View view(&container);
    view.resize(1000, 900);
    view.set_document(doc);

    t0 = std::chrono::steady_clock::now();
    long ink = paint_and_count(view, 1000, 900);
    double first_paint_s = seconds_since(t0);

    t0 = std::chrono::steady_clock::now();
    view.scroll_to(view.content_height() / 2);
    paint_and_count(view, 1000, 900);
    double middle_s = seconds_since(t0);

    t0 = std::chrono::steady_clock::now();
    view.scroll_end();
    long end_ink = paint_and_count(view, 1000, 900);
    double end_s = seconds_since(t0);

    t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < 200; k++) {
        view.scroll_by(-120);
        paint_and_count(view, 1000, 900);
    }
    double scroll_s = seconds_since(t0) / 200;

    t0 = std::chrono::steady_clock::now();
    bool found = view.find("final-marker", true, false, true);
    double find_s = seconds_since(t0);

    std::printf("20 MB: %zu chunks, load %.3f s, first paint %.3f s, jump to middle %.3f s, end %.3f s, "
                "scroll step %.1f ms, find-at-end %.3f s, cached docs %zu, est. height %lld px\n",
                doc->chunks().size(), load_s, first_paint_s, middle_s, end_s, scroll_s * 1000, find_s,
                view.cached_docs(), static_cast<long long>(view.content_height()));
    CHECK(ink > 10000);
    CHECK(end_ink > 1000);
    CHECK(first_paint_s < 1.0);
    CHECK(middle_s < 1.0);
    CHECK(end_s < 1.0);
    CHECK(scroll_s < 0.1);
    CHECK(found);
    CHECK(view.cached_docs() <= ViewOptions{}.max_cached_docs + 2);
}

static void test_png() {
    auto out = std::filesystem::temp_directory_path() / "mdview_render_test.png";
    std::string stats;
    CHECK(render_png(MDVIEW_FIXTURES "/features.md", out, &stats));
    std::error_code ec;
    CHECK(std::filesystem::file_size(out, ec) > 10000);
    std::printf("png: %s\n", stats.c_str());
    std::filesystem::remove(out, ec);
}

int main() {
#ifdef _WIN32
    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartup(&token, &gsi, nullptr);
#endif
    test_features();
    test_large_document();
    test_png();
#ifdef _WIN32
    Gdiplus::GdiplusShutdown(token);
#endif
    std::printf(g_failures ? "%d FAILURES\n" : "all render tests passed\n", g_failures);
    return g_failures ? 1 : 0;
}
