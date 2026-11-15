#include "render/view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace mdview {

namespace {

constexpr double kDefaultPxPerByte = 0.33;  // until the first chunk has been measured

litehtml::pixel_t px(int v) { return static_cast<litehtml::pixel_t>(v); }

std::uint32_t parse_hex_color(const char* hex) {
    unsigned v = 0;
    std::sscanf(hex + 1, "%x", &v);
    return v;
}

}  // namespace

View::View(litehtml::document_container* container, ViewOptions options)
    : container_(container), options_(options) {
    css_ = default_css(dark_);
}

void View::set_document(std::shared_ptr<const Document> doc) {
    doc_ = std::move(doc);
    chunks_.clear();
    if (doc_) chunks_.resize(doc_->chunks().size());
    anchor_chunk_ = 0;
    anchor_offset_ = 0;
    measured_px_ = measured_bytes_ = 0;
    match_ = {};
    hover_chunk_ = static_cast<std::size_t>(-1);
}

void View::set_dark(bool dark) {
    if (dark == dark_) return;
    dark_ = dark;
    reset_layout(false);
}

void View::set_zoom(float zoom) {
    zoom = std::clamp(zoom, 0.3f, 5.0f);
    if (std::fabs(zoom - zoom_) < 0.001f) return;
    anchor_offset_ = static_cast<int>(anchor_offset_ * (zoom / zoom_));
    zoom_ = zoom;
    reset_layout(false);
}

void View::reset_layout(bool keep_docs) {
    css_ = default_css(dark_);
    char extra[96];
    std::snprintf(extra, sizeof(extra), "\nbody { font-size: %dpx; }\n", static_cast<int>(std::lround(16 * zoom_)));
    css_ += extra;
    for (auto& c : chunks_) {
        if (!keep_docs) c.doc.reset();
        c.height = -1;
        c.layout_width = 0;
    }
    measured_px_ = measured_bytes_ = 0;
}

void View::resize(int width, int height) {
    int old_content_width = content_width();
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    if (content_width() != old_content_width) {
        // Keep documents (re-render is cheaper than re-parse), drop heights.
        for (auto& c : chunks_) c.height = -1;
        measured_px_ = measured_bytes_ = 0;
    }
}

int View::content_width() const {
    return std::min(width_, static_cast<int>(options_.max_content_width * zoom_));
}

int View::x_offset() const { return std::max(0, (width_ - content_width()) / 2); }

int View::estimate_height(std::size_t i) const {
    double ratio = measured_bytes_ > 0 ? measured_px_ / measured_bytes_ : kDefaultPxPerByte * zoom_;
    // Narrower layouts wrap more; the learned ratio is for the current width.
    auto bytes = static_cast<double>(doc_->chunks()[i].length);
    return std::max(1, static_cast<int>(bytes * ratio));
}

int View::chunk_height(std::size_t i) const {
    const ChunkState& c = chunks_[i];
    if (c.height >= 0 && c.layout_width == content_width()) return c.height;
    return estimate_height(i);
}

std::int64_t View::chunk_top(std::size_t i) const {
    std::int64_t y = 0;
    for (std::size_t k = 0; k < i; k++) y += chunk_height(k);
    return y;
}

std::int64_t View::content_height() const {
    if (!doc_) return 0;
    return chunk_top(chunks_.size());
}

std::string View::chunk_document_html(std::size_t i) const {
    std::string cls = "md";
    if (i == 0) cls += " first";
    if (i + 1 == chunks_.size()) cls += " last";
    std::string html = "<html><head></head><body><div class=\"" + cls + "\">\n";
    html += doc_->chunk_html(i);
    if (i == 0 && doc_->truncated()) {
        char note[160];
        std::snprintf(note, sizeof(note),
                      "<blockquote><p>File is %.1f MB; showing only the first %.1f MB.</p></blockquote>",
                      doc_->file_size() / 1048576.0, doc_->text().size() / 1048576.0);
        html.insert(html.find('\n') + 1, note);
    }
    html += "\n</div></body></html>";
    return html;
}

void View::ensure_layout(std::size_t i) {
    ChunkState& c = chunks_[i];
    c.last_used = ++use_counter_;
    int w = content_width();
    if (!c.doc) {
        c.doc = litehtml::document::createFromString(chunk_document_html(i), container_, litehtml::master_css, css_);
        c.height = -1;
    }
    if (c.height < 0 || c.layout_width != w) {
        c.doc->render(static_cast<litehtml::pixel_t>(w));
        c.height = static_cast<int>(c.doc->height());
        c.layout_width = w;
        measured_px_ += c.height;
        measured_bytes_ += static_cast<double>(doc_->chunks()[i].length);
    }
}

void View::evict() {
    std::vector<std::pair<std::uint64_t, std::size_t>> cached;
    for (std::size_t i = 0; i < chunks_.size(); i++)
        if (chunks_[i].doc) cached.emplace_back(chunks_[i].last_used, i);
    if (cached.size() <= options_.max_cached_docs) return;
    std::sort(cached.begin(), cached.end());
    std::size_t drop = cached.size() - options_.max_cached_docs;
    for (std::size_t k = 0; k < drop; k++) {
        std::size_t i = cached[k].second;
        if (match_.valid && match_.chunk == i) continue;
        chunks_[i].doc.reset();  // height stays valid for this width
    }
}

void View::normalize_anchor() {
    if (!doc_ || chunks_.empty()) return;
    while (anchor_offset_ < 0 && anchor_chunk_ > 0) {
        anchor_chunk_--;
        ensure_layout(anchor_chunk_);  // nearby: use the real height
        anchor_offset_ += chunk_height(anchor_chunk_);
    }
    if (anchor_offset_ < 0) anchor_offset_ = 0;
    while (anchor_chunk_ + 1 < chunks_.size() && anchor_offset_ >= chunk_height(anchor_chunk_)) {
        anchor_offset_ -= chunk_height(anchor_chunk_);
        anchor_chunk_++;
    }
}

void View::clamp_bottom() {
    if (!doc_ || chunks_.empty()) return;
    // Lay out from the anchor down to the end of the viewport; if the
    // document ends above the viewport bottom, scroll back up.
    for (int guard = 0; guard < 64; guard++) {
        std::int64_t y = -anchor_offset_;
        std::size_t i = anchor_chunk_;
        for (; i < chunks_.size() && y < height_; i++) {
            ensure_layout(i);
            y += chunk_height(i);
        }
        if (i < chunks_.size() || y >= height_) return;
        if (anchor_chunk_ == 0 && anchor_offset_ == 0) return;  // whole document fits
        std::int64_t gap = height_ - y;
        anchor_offset_ -= static_cast<int>(std::min<std::int64_t>(gap, 1 << 30));
        normalize_anchor();
        if (anchor_chunk_ == 0 && anchor_offset_ <= 0) {
            anchor_offset_ = 0;
            return;
        }
    }
}

std::int64_t View::scroll_y() const {
    if (!doc_) return 0;
    return chunk_top(anchor_chunk_) + anchor_offset_;
}

void View::scroll_to(std::int64_t y) {
    if (!doc_ || chunks_.empty()) return;
    y = std::max<std::int64_t>(0, std::min(y, content_height() - height_));
    // Locate using current (possibly estimated) heights.
    std::size_t i = 0;
    std::int64_t top = 0;
    while (i + 1 < chunks_.size() && top + chunk_height(i) <= y) {
        top += chunk_height(i);
        i++;
    }
    anchor_chunk_ = i;
    anchor_offset_ = static_cast<int>(y - top);
    if (anchor_offset_ > 0) {
        // Keep the same relative position once the chunk is measured.
        int est = chunk_height(i);
        ensure_layout(i);
        if (est > 0 && chunk_height(i) != est)
            anchor_offset_ = static_cast<int>(static_cast<double>(anchor_offset_) * chunk_height(i) / est);
    }
    normalize_anchor();
    clamp_bottom();
}

void View::scroll_by(std::int64_t dy) {
    if (!doc_ || chunks_.empty()) return;
    std::int64_t off = anchor_offset_ + dy;
    anchor_offset_ = static_cast<int>(std::clamp<std::int64_t>(off, -(1LL << 30), 1LL << 30));
    normalize_anchor();
    clamp_bottom();
}

void View::scroll_home() {
    anchor_chunk_ = 0;
    anchor_offset_ = 0;
}

void View::scroll_end() {
    if (!doc_ || chunks_.empty()) return;
    anchor_chunk_ = chunks_.size() - 1;
    ensure_layout(anchor_chunk_);
    anchor_offset_ = chunk_height(anchor_chunk_);
    clamp_bottom();
}

std::uint32_t View::background_rgb() const { return parse_hex_color(dark_ ? "#0d1117" : "#ffffff"); }

void View::paint(litehtml::uint_ptr hdc, const litehtml::position& clip) {
    if (!doc_ || chunks_.empty()) return;
    normalize_anchor();
    clamp_bottom();
    int x = x_offset();
    std::int64_t y = -anchor_offset_;
    for (std::size_t i = anchor_chunk_; i < chunks_.size() && y < height_; i++) {
        ensure_layout(i);
        int h = chunk_height(i);
        if (y + h > clip.top() && y < clip.bottom()) chunks_[i].doc->draw(hdc, static_cast<litehtml::pixel_t>(x), static_cast<litehtml::pixel_t>(y), &clip);
        y += h;
    }
    evict();
}

bool View::chunk_at(int y, std::size_t& chunk, int& top) {
    if (!doc_ || chunks_.empty()) return false;
    std::int64_t t = -anchor_offset_;
    for (std::size_t i = anchor_chunk_; i < chunks_.size() && t < height_; i++) {
        int h = chunk_height(i);
        if (y >= t && y < t + h) {
            ensure_layout(i);
            chunk = i;
            top = static_cast<int>(t);
            return true;
        }
        t += h;
    }
    return false;
}

bool View::mouse_move(int x, int y) {
    std::size_t chunk;
    int top;
    litehtml::position::vector boxes;
    bool redraw = false;
    if (!chunk_at(y, chunk, top)) return mouse_leave();
    if (hover_chunk_ != chunk && hover_chunk_ < chunks_.size() && chunks_[hover_chunk_].doc)
        redraw |= chunks_[hover_chunk_].doc->on_mouse_leave(boxes);
    hover_chunk_ = chunk;
    redraw |= chunks_[chunk].doc->on_mouse_over(px(x - x_offset()), px(y - top), px(x), px(y), boxes);
    return redraw || !boxes.empty();
}

bool View::mouse_down(int x, int y) {
    std::size_t chunk;
    int top;
    litehtml::position::vector boxes;
    if (!chunk_at(y, chunk, top)) return false;
    return chunks_[chunk].doc->on_lbutton_down(px(x - x_offset()), px(y - top), px(x), px(y), boxes) || !boxes.empty();
}

bool View::mouse_up(int x, int y) {
    std::size_t chunk;
    int top;
    litehtml::position::vector boxes;
    if (!chunk_at(y, chunk, top)) return false;
    return chunks_[chunk].doc->on_lbutton_up(px(x - x_offset()), px(y - top), px(x), px(y), boxes) || !boxes.empty();
}

bool View::mouse_leave() {
    litehtml::position::vector boxes;
    bool redraw = false;
    if (hover_chunk_ < chunks_.size() && chunks_[hover_chunk_].doc)
        redraw = chunks_[hover_chunk_].doc->on_mouse_leave(boxes);
    hover_chunk_ = static_cast<std::size_t>(-1);
    return redraw;
}

// Collects the rendered text of a chunk in document order (litehtml splits
// text into word and space elements).
void View::flatten(std::size_t chunk, std::string& text, std::vector<TextRun>& runs) {
    ensure_layout(chunk);
    text.clear();
    runs.clear();
    std::vector<litehtml::element::ptr> stack{chunks_[chunk].doc->root()};
    while (!stack.empty()) {
        auto el = stack.back();
        stack.pop_back();
        if (!el) continue;
        if (el->is_text()) {
            std::string t;
            el->get_text(t);
            if (!t.empty()) {
                runs.push_back({el, text.size(), t.size()});
                text += t;
            }
            continue;
        }
        const auto& kids = el->children();
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) stack.push_back(*it);
        // Separate blocks so matches don't span paragraphs by accident.
        const char* tag = el->get_tagName();
        if (tag && (!std::strcmp(tag, "p") || !std::strcmp(tag, "li") || !std::strcmp(tag, "td") ||
                    !std::strcmp(tag, "th") || tag[0] == 'h'))
            text += '\n';
    }
}

bool View::find(std::string_view needle, bool match_case, bool backwards, bool from_start) {
    if (!doc_ || chunks_.empty() || needle.empty()) return false;
    std::size_t chunk;
    std::size_t pos;  // search start inside the chunk's flattened text
    bool have_pos;
    if (from_start || !match_.valid) {
        chunk = from_start ? (backwards ? chunks_.size() - 1 : 0) : anchor_chunk_;
        have_pos = false;
        pos = 0;
    } else {
        chunk = match_.chunk;
        have_pos = true;
        pos = backwards ? match_.start : match_.start + 1;
    }
    std::string text;
    std::vector<TextRun> runs;
    while (chunk < chunks_.size()) {
        std::size_t candidate = doc_->find_chunk(needle, chunk, match_case, backwards);
        if (candidate >= chunks_.size()) break;
        if (candidate != chunk) have_pos = false;
        chunk = candidate;
        flatten(chunk, text, runs);
        std::size_t hit;
        if (!backwards) hit = find_text(text, needle, match_case, have_pos ? pos : 0);
        else hit = rfind_text(text, needle, match_case, have_pos ? pos : text.size());
        if (hit != std::string::npos) {
            match_ = {chunk, hit, needle.size(), true};
            // Scroll so the match sits about a third down the viewport.
            int match_y = 0;
            for (const auto& r : runs) {
                if (r.start + r.length > hit) {
                    match_y = static_cast<int>(r.el->get_placement().y);
                    break;
                }
            }
            scroll_to(chunk_top(chunk) + match_y - height_ / 3);
            return true;
        }
        // Source matched but rendered text didn't (e.g. markup); keep going.
        have_pos = false;
        if (backwards) {
            if (chunk == 0) break;
            chunk--;
        } else {
            chunk++;
        }
    }
    return false;
}

void View::clear_find() { match_ = {}; }

std::vector<litehtml::position> View::highlight_rects() const {
    std::vector<litehtml::position> rects;
    if (!match_.valid || match_.chunk >= chunks_.size() || !chunks_[match_.chunk].doc) return rects;
    // Recompute runs (cheap: one chunk) to map the match to elements.
    std::string text;
    std::vector<TextRun> runs;
    const_cast<View*>(this)->flatten(match_.chunk, text, runs);
    // Chunk top in client coordinates.
    std::int64_t top = -anchor_offset_;
    if (match_.chunk < anchor_chunk_) return rects;
    for (std::size_t i = anchor_chunk_; i < match_.chunk; i++) top += chunk_height(i);
    if (top > height_) return rects;
    std::size_t end = match_.start + match_.length;
    for (const auto& r : runs) {
        if (r.start + r.length <= match_.start || r.start >= end) continue;
        litehtml::position p = r.el->get_placement();
        p.x += static_cast<litehtml::pixel_t>(x_offset());
        p.y += static_cast<litehtml::pixel_t>(top);
        rects.push_back(p);
    }
    return rects;
}

std::size_t View::cached_docs() const {
    std::size_t n = 0;
    for (const auto& c : chunks_) n += c.doc ? 1 : 0;
    return n;
}

std::size_t View::laid_out_chunks() const {
    std::size_t n = 0;
    for (const auto& c : chunks_) n += c.height >= 0 ? 1 : 0;
    return n;
}

}  // namespace mdview
