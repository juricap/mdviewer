// Portable core tests: encoding detection, chunking, md4c output, search.
#include <chrono>
#include <cstdio>
#include <string>

#include "core/document.h"

using namespace mdview;

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                              \
        }                                                              \
    } while (0)

static bool contains(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }

static void test_encodings() {
    auto d = Document::from_bytes("# Hi \xC5\xBE");  // UTF-8
    CHECK(d->encoding() == SourceEncoding::Utf8);
    CHECK(d->text() == "# Hi \xC5\xBE");

    d = Document::from_bytes("\xEF\xBB\xBF# Hi");
    CHECK(d->encoding() == SourceEncoding::Utf8Bom);
    CHECK(d->text() == "# Hi");

    d = Document::from_bytes(std::string("\xFF\xFE#\0 \0\x7E\x01", 8));  // UTF-16LE "# ž"
    CHECK(d->encoding() == SourceEncoding::Utf16LE);
    CHECK(d->text() == "# \xC5\xBE");

    d = Document::from_bytes(std::string("\xFE\xFF\0#\0 \x01\x7E", 8));  // UTF-16BE
    CHECK(d->encoding() == SourceEncoding::Utf16BE);
    CHECK(d->text() == "# \xC5\xBE");

    d = Document::from_bytes("caf\xE9 ok");  // legacy code page: not UTF-8
    CHECK(d->encoding() == SourceEncoding::Legacy);
    // The result depends on the system code page (cp1252 gives "café", cp932
    // pairs \xE9 with the next byte), but it is always valid UTF-8.
    CHECK(d->text().rfind("caf", 0) == 0);
    CHECK(Document::from_bytes(d->text())->encoding() == SourceEncoding::Utf8);

    // Truncated: original size larger than the bytes we got; partial line dropped.
    d = Document::from_bytes("line one\nline tw", {}, 1000);
    CHECK(d->truncated());
    CHECK(d->text() == "line one\n");
}

static void test_markdown() {
    std::string html;
    CHECK(markdown_to_html("| a | b |\n|---|:-:|\n| 1 | 2 |\n\n- [x] done\n\n~~gone~~ www.example.com\n", html));
    CHECK(contains(html, "<table>"));
    CHECK(contains(html, "align=\"center\""));
    CHECK(contains(html, "task-list-item-checkbox"));
    CHECK(contains(html, "<del>gone</del>"));
    CHECK(contains(html, "href=\"http://www.example.com\""));
}

static void test_chunking() {
    LoadOptions o;
    o.chunk_threshold = 1000;
    o.chunk_bytes = 200;
    o.max_chunk_bytes = 800;

    // Small document: one chunk.
    CHECK(split_markdown("# x\n\npara\n", o).size() == 1);

    // Normal document: splits at blank lines, chunks cover the text exactly.
    std::string md;
    for (int i = 0; i < 100; i++) md += "## Heading " + std::to_string(i) + "\n\nSome paragraph text here.\n\n";
    auto chunks = split_markdown(md, o);
    CHECK(chunks.size() > 5);
    std::size_t pos = 0;
    for (const auto& c : chunks) {
        CHECK(c.offset == pos);
        CHECK(c.length <= o.max_chunk_bytes);
        pos += c.length;
        CHECK(md.compare(c.offset, 3, "## ") == 0);  // each chunk starts at a heading block
    }
    CHECK(pos == md.size());

    // Never split inside a code fence.
    std::string fenced = std::string(300, 'x') + "\n\n```\n";
    for (int i = 0; i < 30; i++) fenced += "code line\n\nmore\n";
    fenced += "```\n\nafter\n";
    fenced += std::string(900, 'y') + "\n";
    chunks = split_markdown(fenced, o);
    for (const auto& c : chunks) {
        std::string piece = fenced.substr(c.offset, c.length);
        CHECK(piece.rfind("code line", 0) != 0 && piece.rfind("more", 0) != 0);
    }

    // Degenerate single huge line: forced splits on UTF-8 boundaries.
    std::string line;
    while (line.size() < 5000) line += "\xC5\xBE word ";
    chunks = split_markdown(line, o);
    CHECK(chunks.size() >= 7);
    pos = 0;
    for (const auto& c : chunks) {
        CHECK(c.length <= o.max_chunk_bytes);
        CHECK((static_cast<unsigned char>(line[c.offset]) & 0xC0) != 0x80);
        pos += c.length;
    }
    CHECK(pos == line.size());
}

static void test_reference_links_across_chunks() {
    LoadOptions o;
    o.chunk_threshold = 100;
    o.chunk_bytes = 60;
    std::string md = "See [the docs][docs] first.\n\n";
    for (int i = 0; i < 20; i++) md += "Filler paragraph number " + std::to_string(i) + ".\n\n";
    md += "Also [docs] again.\n\n";
    md += "[docs]: https://example.com/docs \"Docs\"\n";
    auto d = Document::from_bytes(md, o);
    CHECK(d->chunks().size() > 3);
    CHECK(contains(d->chunk_html(0), "href=\"https://example.com/docs\""));
    // Definitions inside code fences are not definitions.
    auto d2 = Document::from_bytes("```\n[x]: http://no\n```\n\n" + md, o);
    CHECK(!contains(d2->chunk_html(0), "http://no\""));
}

static void test_search() {
    CHECK(find_text("Hello World", "world", false) == 6);
    CHECK(find_text("Hello World", "world", true) == std::string::npos);
    CHECK(rfind_text("abc abc", "ABC", false, 7) == 4);
    CHECK(rfind_text("abc abc", "abc", true, 4) == 0);

    LoadOptions o;
    o.chunk_threshold = 100;
    o.chunk_bytes = 50;
    std::string md;
    for (int i = 0; i < 20; i++) md += "para " + std::to_string(i) + (i == 13 ? " needle" : "") + " filler text here\n\n";
    auto d = Document::from_bytes(md, o);
    std::size_t c = d->find_chunk("NEEDLE", 0, false, false);
    CHECK(c < d->chunks().size());
    CHECK(d->chunk_text(c).find("needle") != std::string_view::npos);
    CHECK(d->find_chunk("needle", c + 1, false, false) == d->chunks().size());
    CHECK(d->find_chunk("needle", d->chunks().size() - 1, false, true) == c);
}

static void test_speed() {
    std::string md;
    while (md.size() < 20u * 1024u * 1024u) md += "## Section\n\nSome **bold** and *italic* text, `code`, [link](u).\n\n- a\n- b\n\n";
    auto t0 = std::chrono::steady_clock::now();
    auto d = Document::from_bytes(md);
    double split_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    t0 = std::chrono::steady_clock::now();
    std::size_t html = 0;
    for (std::size_t i = 0; i < d->chunks().size(); i++) html += d->chunk_html(i).size();
    double html_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("20 MB: %zu chunks, decode+split %.3f s, all chunks to HTML %.3f s (%zu bytes)\n",
                d->chunks().size(), split_s, html_s, html);
    CHECK(split_s < 2.0);
    CHECK(html_s < 5.0);
}

int main() {
    test_encodings();
    test_markdown();
    test_chunking();
    test_reference_links_across_chunks();
    test_search();
    test_speed();
    std::printf(g_failures ? "%d FAILURES\n" : "all core tests passed\n", g_failures);
    return g_failures ? 1 : 0;
}
