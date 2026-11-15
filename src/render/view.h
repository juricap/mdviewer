// mdview render: portable, virtualized litehtml view over a chunked Document.
//
// Each chunk is its own litehtml document, created and laid out only when it
// is near the viewport; least-recently-used chunk documents are released.
// Chunk heights are estimated until laid out. The scroll position is kept as
// (chunk, offset inside chunk), so refining estimates never moves visible
// content. Drawing goes through the platform's litehtml::document_container.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <litehtml.h>

#include "core/document.h"

namespace mdview {

struct ViewOptions {
    int max_content_width = 980;   // px (at zoom 1), including side padding
    std::size_t max_cached_docs = 24;
};

class View {
public:
    explicit View(litehtml::document_container* container, ViewOptions options = {});

    void set_document(std::shared_ptr<const Document> doc);
    const std::shared_ptr<const Document>& document() const { return doc_; }

    void set_dark(bool dark);
    bool dark() const { return dark_; }
    void set_zoom(float zoom);  // 1.0 = 100%
    float zoom() const { return zoom_; }

    void resize(int width, int height);
    int width() const { return width_; }
    int height() const { return height_; }

    // Scrolling (pixels). content_height() is estimated for chunks not laid out yet.
    std::int64_t content_height() const;
    std::int64_t scroll_y() const;
    void scroll_to(std::int64_t y);
    void scroll_by(std::int64_t dy);
    void scroll_home();
    void scroll_end();

    // Background color as 0xRRGGBB.
    std::uint32_t background_rgb() const;

    // Draws the viewport. clip is in client coordinates.
    void paint(litehtml::uint_ptr hdc, const litehtml::position& clip);

    // Mouse in client coordinates. Return true when a repaint is needed.
    // Link clicks reach the container's on_anchor_click().
    bool mouse_move(int x, int y);
    bool mouse_down(int x, int y);
    bool mouse_up(int x, int y);
    bool mouse_leave();

    // Finds the next/previous match of needle in rendered text, scrolls to it
    // and highlights it. from_start restarts at the top (or bottom if backwards).
    bool find(std::string_view needle, bool match_case, bool backwards, bool from_start);
    void clear_find();
    // Highlight rectangles of the current match in client coordinates.
    std::vector<litehtml::position> highlight_rects() const;

    // Diagnostics.
    std::size_t cached_docs() const;
    std::size_t laid_out_chunks() const;

private:
    struct ChunkState {
        litehtml::document::ptr doc;
        int height = -1;          // actual height at layout_width_, -1 if unknown
        int layout_width = 0;     // width height was measured at
        std::uint64_t last_used = 0;
    };
    struct Match {
        std::size_t chunk = 0;
        std::size_t start = 0;   // in the chunk's flattened text
        std::size_t length = 0;
        bool valid = false;
    };
    struct TextRun {
        litehtml::element::ptr el;
        std::size_t start;  // offset in flattened text
        std::size_t length;
    };

    int content_width() const;
    int x_offset() const;
    int chunk_height(std::size_t i) const;  // actual or estimated
    int estimate_height(std::size_t i) const;
    std::int64_t chunk_top(std::size_t i) const;  // in content coordinates
    void ensure_layout(std::size_t i);
    void reset_layout(bool keep_docs);
    void normalize_anchor();
    void clamp_bottom();
    void evict();
    std::string chunk_document_html(std::size_t i) const;
    void flatten(std::size_t chunk, std::string& text, std::vector<TextRun>& runs);
    // Chunk under client y, with its top in client coordinates.
    bool chunk_at(int y, std::size_t& chunk, int& top);

    litehtml::document_container* container_;
    ViewOptions options_;
    std::shared_ptr<const Document> doc_;
    std::vector<ChunkState> chunks_;
    std::string css_;
    bool dark_ = false;
    float zoom_ = 1.0f;
    int width_ = 0, height_ = 0;
    std::size_t anchor_chunk_ = 0;
    int anchor_offset_ = 0;
    std::uint64_t use_counter_ = 0;
    // Learned layout density for height estimates.
    double measured_px_ = 0, measured_bytes_ = 0;
    Match match_;
    std::size_t hover_chunk_ = static_cast<std::size_t>(-1);
};

}  // namespace mdview
