// litehtml container for cairo + pango (Linux, BSDs; usable on any cairo
// surface: image buffers for headless rendering, GTK/Qt windows later).
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>

#include "container_cairo_pango.h"

namespace mdview::cairo {

class Container : public container_cairo_pango {
public:
    Container() = default;
    ~Container() override;

    std::function<void(const std::string& url)> on_link;
    int viewport_width = 800, viewport_height = 600;
    double dpi = 96;

    void set_base_dir(const std::filesystem::path& dir) { base_dir_ = dir; }
    const std::string& cursor() const { return cursor_; }

    // litehtml::document_container
    const char* get_default_font_name() const override { return "sans-serif"; }
    void load_image(const char* src, const char* baseurl, bool redraw_on_ready) override;
    void set_caption(const char*) override {}
    void set_base_url(const char*) override {}
    void on_anchor_click(const char* url, const litehtml::element::ptr& el) override;
    void on_mouse_event(const litehtml::element::ptr&, litehtml::mouse_event) override {}
    void set_cursor(const char* cursor) override { cursor_ = cursor ? cursor : ""; }
    void import_css(litehtml::string&, const litehtml::string&, litehtml::string&) override {}
    void get_viewport(litehtml::position& viewport) const override;

    // container_cairo
    void make_url(const char* url, const char* basepath, litehtml::string& out) override;
    cairo_surface_t* get_image(const std::string& url) override;
    double get_screen_dpi() const override { return dpi; }
    int get_screen_width() const override { return viewport_width; }
    int get_screen_height() const override { return viewport_height; }

private:
    std::filesystem::path base_dir_;
    std::string cursor_;
    std::map<std::string, cairo_surface_t*> images_;  // owned references
};

// Headless: renders a Markdown file into a PNG. scroll_y in CSS pixels.
bool render_file_to_png(const std::filesystem::path& md, const std::filesystem::path& png, int width, int height,
                        std::int64_t scroll_y, float zoom, bool dark, std::string* stats = nullptr);

}  // namespace mdview::cairo
