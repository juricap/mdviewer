// litehtml container for Windows: GDI text (ClearType) + GDI+ shapes/images,
// based on litehtml's bundled gdiplus_container.
#pragma once

#include <functional>
#include <string>

#include "gdiplus/gdiplus_container.h"

namespace mdview::win32 {

class Container : public gdiplus_container {
public:
    Container();
    ~Container() override;

    std::function<void(const std::string& url)> on_link;    // anchor clicked (UTF-8 href)
    std::function<void(int& width, int& height)> viewport;  // current client size

    void set_base_dir(const std::wstring& dir) { base_dir_ = dir; }
    const std::string& cursor() const { return cursor_; }

    // litehtml::document_container
    const char* get_default_font_name() const override { return "Segoe UI"; }
    void set_caption(const char*) override {}
    void set_base_url(const char*) override {}
    void on_anchor_click(const char* url, const litehtml::element::ptr& el) override;
    void on_mouse_event(const litehtml::element::ptr&, litehtml::mouse_event) override {}
    void set_cursor(const char* cursor) override { cursor_ = cursor ? cursor : ""; }
    void import_css(litehtml::string& text, const litehtml::string& url, litehtml::string& baseurl) override;
    void get_viewport(litehtml::position& viewport) const override;

protected:
    // win32_container
    void make_url(LPCWSTR url, LPCWSTR basepath, std::wstring& out) override;
    uint_ptr get_image(LPCWSTR url_or_path, bool redraw_on_ready) override;

private:
    std::wstring base_dir_;
    std::string cursor_;
};

std::wstring utf8_to_wide(const std::string& s);
std::string wide_to_utf8(const std::wstring& w);

}  // namespace mdview::win32
