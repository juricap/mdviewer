// mdview core: portable Markdown document model. No GUI or platform types.
//
// A Document holds the Markdown source as UTF-8 and splits it into chunks.
// Each chunk is converted to HTML independently (md4c, linear time), so a
// renderer can lay out and paint only the chunks near the viewport. Small
// documents are a single chunk, so they render exactly like one md4c pass.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace mdview {

struct LoadOptions {
    // Bytes of the file that are read at most; the rest is dropped (truncated()).
    std::size_t max_bytes = 64u * 1024u * 1024u;
    // Chunk sizes are bounded by litehtml layout speed (~180 KB of Markdown
    // per second, measured): a 16 KB chunk lays out in ~0.1 s, and the UI
    // thread never lays out more than ~32 KB at once (~0.2 s).
    // Documents smaller than this are a single chunk.
    std::size_t chunk_threshold = 32u * 1024u;
    // Target chunk size; chunks end at a blank line outside code fences.
    std::size_t chunk_bytes = 16u * 1024u;
    // Hard limit: if no safe split point exists, split at a line break; one
    // huge line is cut every chunk_bytes at a UTF-8 character boundary.
    std::size_t max_chunk_bytes = 32u * 1024u;
};

enum class SourceEncoding { Utf8, Utf8Bom, Utf16LE, Utf16BE, Legacy };

struct Chunk {
    std::size_t offset = 0;  // into Document::text()
    std::size_t length = 0;
};

class Document {
public:
    // Reads and decodes a file. Returns nullptr and sets *error on failure.
    static std::shared_ptr<Document> load_file(const std::filesystem::path& path, const LoadOptions& options = {},
                                               std::string* error = nullptr);
    // Decodes raw bytes (BOM / UTF-8 / UTF-16 / legacy code page detection).
    static std::shared_ptr<Document> from_bytes(std::string bytes, const LoadOptions& options = {},
                                                std::uint64_t original_size = 0);

    const std::string& text() const { return text_; }
    const std::vector<Chunk>& chunks() const { return chunks_; }
    std::string_view chunk_text(std::size_t index) const;
    // HTML fragment for one chunk (md4c, GitHub dialect). Thread-safe.
    std::string chunk_html(std::size_t index) const;

    SourceEncoding encoding() const { return encoding_; }
    bool truncated() const { return truncated_; }
    std::uint64_t file_size() const { return file_size_; }
    const std::filesystem::path& path() const { return path_; }
    // Directory used to resolve relative links and images (empty if unknown).
    std::filesystem::path base_dir() const { return path_.empty() ? std::filesystem::path() : path_.parent_path(); }

    // First chunk at or after `start` (or at/before it when backwards) whose
    // source text contains `needle`. Used to skip chunks during search.
    // Returns chunks().size() when not found. Case-insensitive for ASCII
    // unless match_case.
    std::size_t find_chunk(std::string_view needle, std::size_t start, bool match_case, bool backwards) const;

private:
    std::string text_;
    std::vector<Chunk> chunks_;
    std::string reference_definitions_;  // appended to each chunk when chunked
    SourceEncoding encoding_ = SourceEncoding::Utf8;
    bool truncated_ = false;
    std::uint64_t file_size_ = 0;
    std::filesystem::path path_;
};

// Splits UTF-8 Markdown into chunks (see LoadOptions). Exposed for tests.
std::vector<Chunk> split_markdown(std::string_view md, const LoadOptions& options);

// Markdown -> HTML fragment. Returns false if md4c reports an error.
bool markdown_to_html(std::string_view md, std::string& out);

// Default stylesheet for rendered documents.
std::string default_css(bool dark);

// Case-insensitive (ASCII) substring search; returns npos when not found.
std::size_t find_text(std::string_view haystack, std::string_view needle, bool match_case, std::size_t from = 0);
std::size_t rfind_text(std::string_view haystack, std::string_view needle, bool match_case, std::size_t before);

}  // namespace mdview
