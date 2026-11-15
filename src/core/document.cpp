#include "core/document.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

extern "C" {
#include "md4c-html.h"
}

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace mdview {

namespace {

constexpr unsigned kMd4cFlags = MD_DIALECT_GITHUB;

void append_utf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string utf16_to_utf8(const std::string& bytes, std::size_t start, bool big_endian) {
    std::string out;
    out.reserve(bytes.size());
    std::size_t n = (bytes.size() - start) / 2;
    auto unit = [&](std::size_t i) -> char16_t {
        auto b0 = static_cast<unsigned char>(bytes[start + 2 * i]);
        auto b1 = static_cast<unsigned char>(bytes[start + 2 * i + 1]);
        return big_endian ? static_cast<char16_t>((b0 << 8) | b1) : static_cast<char16_t>((b1 << 8) | b0);
    };
    for (std::size_t i = 0; i < n; i++) {
        char32_t c = unit(i);
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < n) {
            char32_t c2 = unit(i + 1);
            if (c2 >= 0xDC00 && c2 < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00);
                i++;
            }
        }
        if (c >= 0xD800 && c < 0xE000) c = 0xFFFD;  // unpaired surrogate
        append_utf8(out, c);
    }
    return out;
}

// Valid UTF-8? An incomplete sequence at the very end is accepted only when
// the input was truncated at max_bytes.
bool is_utf8(const std::string& data, bool truncated) {
    auto b = reinterpret_cast<const unsigned char*>(data.data());
    std::size_t len = data.size();
    for (std::size_t i = 0; i < len;) {
        if (b[i] < 0x80) {
            i++;
            continue;
        }
        std::size_t extra;
        if ((b[i] & 0xE0) == 0xC0 && b[i] >= 0xC2) extra = 1;
        else if ((b[i] & 0xF0) == 0xE0) extra = 2;
        else if ((b[i] & 0xF8) == 0xF0 && b[i] <= 0xF4) extra = 3;
        else return false;
        for (std::size_t k = 1; k <= extra; k++) {
            if (i + k >= len) return truncated;
            if ((b[i + k] & 0xC0) != 0x80) return false;
        }
        i += extra + 1;
    }
    return true;
}

std::string legacy_to_utf8(const std::string& bytes) {
#ifdef _WIN32
    // System ANSI code page (what Notepad used to write).
    if (bytes.empty()) return {};
    int wn = MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(wn), L'\0');
    MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), w.data(), wn);
    int un = WideCharToMultiByte(CP_UTF8, 0, w.data(), wn, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(un), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), wn, out.data(), un, nullptr, nullptr);
    return out;
#else
    // No locale-independent way to know the code page: treat as Latin-1.
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char c : bytes) append_utf8(out, c);
    return out;
#endif
}

bool is_blank(std::string_view line) {
    for (char c : line)
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
    return true;
}

// "[label]: destination" with up to 3 spaces of indentation (CommonMark link
// reference definition, single-line form).
bool is_reference_definition(std::string_view line) {
    std::size_t i = 0;
    while (i < line.size() && i < 3 && line[i] == ' ') i++;
    if (i >= line.size() || line[i] != '[' || (i + 1 < line.size() && line[i + 1] == '^')) return false;
    std::size_t close = line.find("]:", i + 1);
    if (close == std::string_view::npos || close == i + 1) return false;
    for (std::size_t k = i + 1; k < close; k++)
        if (line[k] == '[' || line[k] == ']') return false;
    std::size_t d = close + 2;
    while (d < line.size() && (line[d] == ' ' || line[d] == '\t')) d++;
    return d < line.size() && line[d] != '\n' && line[d] != '\r';
}

bool is_fence(std::string_view line) {
    std::size_t i = 0;
    while (i < line.size() && i < 3 && line[i] == ' ') i++;
    if (line.size() - i < 3) return false;
    char c = line[i];
    return (c == '`' || c == '~') && line[i + 1] == c && line[i + 2] == c;
}

// Moves pos back to the start of a UTF-8 character.
std::size_t utf8_boundary(std::string_view s, std::size_t pos) {
    while (pos > 0 && pos < s.size() && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80) pos--;
    return pos;
}

char ascii_lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

void md4c_append(const MD_CHAR* text, MD_SIZE size, void* userdata) {
    static_cast<std::string*>(userdata)->append(text, size);
}

}  // namespace

std::vector<Chunk> split_markdown(std::string_view md, const LoadOptions& options) {
    std::vector<Chunk> chunks;
    if (md.size() < options.chunk_threshold) {
        chunks.push_back({0, md.size()});
        return chunks;
    }
    std::size_t start = 0, pos = 0;
    bool in_fence = false, prev_blank = false;
    while (pos < md.size()) {
        std::size_t eol = md.find('\n', pos);
        std::size_t next = (eol == std::string_view::npos) ? md.size() : eol + 1;
        std::string_view line = md.substr(pos, next - pos);
        bool blank = is_blank(line);
        bool starts_block = !line.empty() && line[0] != ' ' && line[0] != '\t';

        if (pos > start && prev_blank && !blank && !in_fence && starts_block && pos - start >= options.chunk_bytes) {
            chunks.push_back({start, pos - start});
            start = pos;
        }
        if (!blank && is_fence(line)) in_fence = !in_fence;

        // Degenerate input: force splits so no chunk exceeds max_chunk_bytes.
        // First at the start of this line; inside one huge line, every
        // chunk_bytes, preferring a space, always on a UTF-8 boundary.
        for (;;) {
            std::size_t limit = pos > start ? options.max_chunk_bytes : options.chunk_bytes;
            if (next - start <= limit) break;
            std::size_t cut;
            if (pos > start) {
                cut = pos;
            } else {
                cut = utf8_boundary(md, start + options.chunk_bytes);
                std::size_t sp = md.rfind(' ', cut);
                if (sp != std::string_view::npos && sp > start + options.chunk_bytes / 2) cut = sp + 1;
                if (cut <= start) cut = start + options.chunk_bytes;
            }
            chunks.push_back({start, cut - start});
            start = cut;
        }
        prev_blank = blank;
        pos = next;
    }
    if (start < md.size() || chunks.empty()) chunks.push_back({start, md.size() - start});
    return chunks;
}

bool markdown_to_html(std::string_view md, std::string& out) {
    out.reserve(out.size() + md.size() * 5 / 2);
    return md_html(md.data(), static_cast<MD_SIZE>(md.size()), md4c_append, &out, kMd4cFlags, 0) == 0;
}

std::shared_ptr<Document> Document::from_bytes(std::string bytes, const LoadOptions& options,
                                               std::uint64_t original_size) {
    auto doc = std::make_shared<Document>();
    doc->file_size_ = original_size ? original_size : bytes.size();
    doc->truncated_ = doc->file_size_ > bytes.size();
    auto u = [&](std::size_t i) { return static_cast<unsigned char>(bytes[i]); };

    if (bytes.size() >= 3 && u(0) == 0xEF && u(1) == 0xBB && u(2) == 0xBF) {
        doc->encoding_ = SourceEncoding::Utf8Bom;
        doc->text_ = bytes.substr(3);
    } else if (bytes.size() >= 2 && u(0) == 0xFF && u(1) == 0xFE) {
        doc->encoding_ = SourceEncoding::Utf16LE;
        doc->text_ = utf16_to_utf8(bytes, 2, false);
    } else if (bytes.size() >= 2 && u(0) == 0xFE && u(1) == 0xFF) {
        doc->encoding_ = SourceEncoding::Utf16BE;
        doc->text_ = utf16_to_utf8(bytes, 2, true);
    } else if (is_utf8(bytes, doc->truncated_)) {
        doc->encoding_ = SourceEncoding::Utf8;
        doc->text_ = std::move(bytes);
    } else {
        doc->encoding_ = SourceEncoding::Legacy;
        doc->text_ = legacy_to_utf8(bytes);
    }
    if (doc->truncated_) {
        // Drop the partial last line (and any partial UTF-8 sequence).
        std::size_t nl = doc->text_.find_last_of('\n');
        if (nl != std::string::npos) doc->text_.resize(nl + 1);
    }
    doc->chunks_ = split_markdown(doc->text_, options);
    if (doc->chunks_.size() > 1) {
        // Chunks are parsed separately; give each one every link reference
        // definition so [text][ref] links resolve across chunk boundaries.
        std::string_view t(doc->text_);
        bool in_fence = false;
        for (std::size_t pos = 0; pos < t.size();) {
            std::size_t eol = t.find('\n', pos);
            std::size_t next = eol == std::string_view::npos ? t.size() : eol + 1;
            std::string_view line = t.substr(pos, next - pos);
            if (is_fence(line)) in_fence = !in_fence;
            else if (!in_fence && is_reference_definition(line)) {
                doc->reference_definitions_ += line;
                if (line.back() != '\n') doc->reference_definitions_ += '\n';
            }
            pos = next;
        }
        // Pathological amounts would slow down every chunk; give up on them.
        if (doc->reference_definitions_.size() > 64u * 1024u) doc->reference_definitions_.clear();
    }
    return doc;
}

std::shared_ptr<Document> Document::load_file(const std::filesystem::path& path, const LoadOptions& options,
                                              std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open file";
        return nullptr;
    }
    in.seekg(0, std::ios::end);
    auto size = static_cast<std::uint64_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    auto to_read = static_cast<std::size_t>(std::min<std::uint64_t>(size, options.max_bytes));
    std::string bytes(to_read, '\0');
    in.read(bytes.data(), static_cast<std::streamsize>(to_read));
    bytes.resize(static_cast<std::size_t>(in.gcount()));
    auto doc = from_bytes(std::move(bytes), options, size);
    doc->path_ = path;
    return doc;
}

std::string_view Document::chunk_text(std::size_t index) const {
    const Chunk& c = chunks_.at(index);
    return std::string_view(text_).substr(c.offset, c.length);
}

std::string Document::chunk_html(std::size_t index) const {
    std::string html;
    bool ok;
    if (reference_definitions_.empty()) {
        ok = markdown_to_html(chunk_text(index), html);
    } else {
        std::string md(chunk_text(index));
        md += "\n\n";
        md += reference_definitions_;
        ok = markdown_to_html(md, html);
    }
    if (!ok) {
        // md4c only fails on allocation failure; show escaped text instead.
        html = "<pre>";
        for (char c : chunk_text(index)) {
            switch (c) {
                case '&': html += "&amp;"; break;
                case '<': html += "&lt;"; break;
                case '>': html += "&gt;"; break;
                default: html += c;
            }
        }
        html += "</pre>";
    }
    return html;
}

std::size_t find_text(std::string_view hay, std::string_view needle, bool match_case, std::size_t from) {
    if (needle.empty() || needle.size() > hay.size()) return std::string_view::npos;
    if (match_case) return hay.find(needle, from);
    const char first = ascii_lower(needle[0]);
    for (std::size_t i = from; i + needle.size() <= hay.size(); i++) {
        if (ascii_lower(hay[i]) != first) continue;
        std::size_t k = 1;
        while (k < needle.size() && ascii_lower(hay[i + k]) == ascii_lower(needle[k])) k++;
        if (k == needle.size()) return i;
    }
    return std::string_view::npos;
}

std::size_t rfind_text(std::string_view hay, std::string_view needle, bool match_case, std::size_t before) {
    if (needle.empty() || needle.size() > hay.size()) return std::string_view::npos;
    std::size_t i = std::min(before, hay.size() - needle.size() + 1);
    while (i-- > 0) {
        std::size_t k = 0;
        while (k < needle.size() &&
               (match_case ? hay[i + k] == needle[k] : ascii_lower(hay[i + k]) == ascii_lower(needle[k])))
            k++;
        if (k == needle.size()) return i;
    }
    return std::string_view::npos;
}

std::size_t Document::find_chunk(std::string_view needle, std::size_t start, bool match_case, bool backwards) const {
    if (chunks_.empty()) return 0;
    if (start >= chunks_.size()) start = backwards ? chunks_.size() - 1 : chunks_.size();
    if (!backwards) {
        for (std::size_t i = start; i < chunks_.size(); i++)
            if (find_text(chunk_text(i), needle, match_case) != std::string_view::npos) return i;
    } else {
        for (std::size_t i = start + 1; i-- > 0;)
            if (find_text(chunk_text(i), needle, match_case) != std::string_view::npos) return i;
    }
    return chunks_.size();
}

std::string default_css(bool dark) {
    // Only CSS that litehtml supports. Colors follow GitHub's light/dark themes.
    const char* fg = dark ? "#e6edf3" : "#1f2328";
    const char* bg = dark ? "#0d1117" : "#ffffff";
    const char* muted = dark ? "#9198a1" : "#59636e";
    const char* border = dark ? "#3d444d" : "#d1d9e0";
    const char* code_bg = dark ? "#151b23" : "#f6f8fa";
    const char* inline_code_bg = dark ? "#2a313c" : "#eff1f3";
    const char* link = dark ? "#4493f8" : "#0969da";
    const char* row_alt = dark ? "#151b23" : "#f6f8fa";
    char css[4096];
    std::snprintf(css, sizeof(css), R"CSS(
html, body { margin: 0; padding: 0; background: %s; color: %s; }
body { font-family: "Segoe UI", "Noto Sans", "DejaVu Sans", "Helvetica Neue", Arial, sans-serif;
       font-size: 16px; line-height: 1.5; }
.md { padding: 0 32px; }
.md.first { padding-top: 24px; }
.md.last { padding-bottom: 32px; }
h1, h2, h3, h4, h5, h6 { margin: 24px 0 16px 0; font-weight: bold; line-height: 1.25; }
h1 { font-size: 2em; padding-bottom: 0.3em; border-bottom: 1px solid %s; }
h2 { font-size: 1.5em; padding-bottom: 0.3em; border-bottom: 1px solid %s; }
h3 { font-size: 1.25em; }
h4 { font-size: 1em; }
h5 { font-size: 0.875em; }
h6 { font-size: 0.85em; color: %s; }
p, blockquote, ul, ol, table, pre { margin: 0 0 16px 0; }
a { color: %s; text-decoration: none; }
code, pre { font-family: Consolas, "Cascadia Mono", "DejaVu Sans Mono", "Courier New", monospace; }
code { font-size: 85%%; background: %s; padding: 2px 4px; border-radius: 4px; }
pre { font-size: 85%%; line-height: 1.45; background: %s; padding: 16px; border-radius: 6px;
      white-space: pre-wrap; }
pre code { background: transparent; padding: 0; font-size: 100%%; }
blockquote { padding: 0 1em; color: %s; border-left: 4px solid %s; }
ul, ol { padding-left: 2em; }
li { margin-top: 4px; }
table { border-collapse: collapse; }
th, td { padding: 6px 13px; border: 1px solid %s; }
th { font-weight: bold; }
tr:nth-child(2n) td { background: %s; }
hr { height: 0; border: 0; border-bottom: 2px solid %s; margin: 24px 0; }
img { max-width: 100%%; }
del { color: %s; }
li.task-list-item { list-style-type: none; }
input.task-list-item-checkbox { display: inline-block; width: 12px; height: 12px; margin: 0 6px 0 -20px;
      border: 1px solid %s; border-radius: 3px; vertical-align: middle; }
input.task-list-item-checkbox[checked] { background: %s; border-color: %s; }
)CSS",
                  bg, fg, border, border, muted, link, inline_code_bg, code_bg, muted, border, border, row_alt, border,
                  muted, muted, link, link);
    return css;
}

}  // namespace mdview
