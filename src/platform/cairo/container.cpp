#include "platform/cairo/container.h"

#include <chrono>
#include <cstdio>
#include <cstring>

#include "core/document.h"
#include "render/view.h"

namespace mdview::cairo {

Container::~Container() {
    for (auto& kv : images_)
        if (kv.second) cairo_surface_destroy(kv.second);
}

void Container::get_viewport(litehtml::position& vp) const {
    vp = litehtml::position(0, 0, static_cast<litehtml::pixel_t>(viewport_width),
                            static_cast<litehtml::pixel_t>(viewport_height));
}

void Container::on_anchor_click(const char* url, const litehtml::element::ptr&) {
    if (on_link && url) on_link(url);
}

void Container::make_url(const char* url, const char*, litehtml::string& out) {
    std::string u = url ? url : "";
    if (u.rfind("file://", 0) == 0) {
        out = u.substr(7);
        return;
    }
    if (u.find("://") != std::string::npos) {
        out = u;  // remote: never fetched
        return;
    }
    std::filesystem::path p(u);
    if (p.is_relative() && !base_dir_.empty()) p = base_dir_ / p;
    out = p.lexically_normal().string();
}

void Container::load_image(const char* src, const char* baseurl, bool) {
    std::string url;
    make_url(src, baseurl, url);
    if (images_.count(url)) return;
    cairo_surface_t* img = nullptr;
    // PNG is decoded by cairo itself; other formats need an image library
    // (not linked yet, see obsidian TODO).
    if (url.find("://") == std::string::npos && url.size() > 4 &&
        strcasecmp(url.c_str() + url.size() - 4, ".png") == 0) {
        img = cairo_image_surface_create_from_png(url.c_str());
        if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(img);
            img = nullptr;
        }
    }
    images_[url] = img;
}

cairo_surface_t* Container::get_image(const std::string& url) {
    auto it = images_.find(url);
    if (it == images_.end() || !it->second) return nullptr;
    return cairo_surface_reference(it->second);  // caller destroys its reference
}

bool render_file_to_png(const std::filesystem::path& md, const std::filesystem::path& png, int width, int height,
                        std::int64_t scroll_y, float zoom, bool dark, std::string* stats) {
    auto t0 = std::chrono::steady_clock::now();
    std::string error;
    auto doc = Document::load_file(md, {}, &error);
    if (!doc) {
        if (stats) *stats = "load failed: " + error;
        return false;
    }
    auto t1 = std::chrono::steady_clock::now();
    Container container;
    container.viewport_width = width;
    container.viewport_height = height;
    container.set_base_dir(doc->base_dir());
    View view(&container);
    view.set_dark(dark);
    view.set_zoom(zoom);
    view.resize(width, height);
    view.set_document(doc);
    view.scroll_to(scroll_y);

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
    cairo_t* cr = cairo_create(surface);
    std::uint32_t bg = view.background_rgb();
    cairo_set_source_rgb(cr, ((bg >> 16) & 0xFF) / 255.0, ((bg >> 8) & 0xFF) / 255.0, (bg & 0xFF) / 255.0);
    cairo_paint(cr);
    view.paint(reinterpret_cast<litehtml::uint_ptr>(cr),
               litehtml::position(0, 0, static_cast<litehtml::pixel_t>(width), static_cast<litehtml::pixel_t>(height)));
    auto t2 = std::chrono::steady_clock::now();
    cairo_surface_flush(surface);
    bool ok = cairo_surface_write_to_png(surface, png.string().c_str()) == CAIRO_STATUS_SUCCESS;
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    if (stats) {
        char s[256];
        std::snprintf(s, sizeof(s), "load+chunk=%.3fs layout+paint=%.3fs chunks=%zu laid_out=%zu cached=%zu height=%lld",
                      std::chrono::duration<double>(t1 - t0).count(), std::chrono::duration<double>(t2 - t1).count(),
                      doc->chunks().size(), view.laid_out_chunks(), view.cached_docs(),
                      static_cast<long long>(view.content_height()));
        *stats = s;
    }
    return ok;
}

}  // namespace mdview::cairo
