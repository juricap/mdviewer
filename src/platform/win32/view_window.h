// Win32 Markdown view control: a child window that loads a file on a worker
// thread and shows it with mdview::View. Used by the standalone app and the
// TC/Double Commander lister plugin.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace mdview::win32 {

struct ViewWindowOptions {
    bool dark = false;
    float zoom = 1.0f;  // multiplied by the monitor DPI scale
    // Called for keys the view does not handle (e.g. forward to the TC lister).
    // Return true if consumed.
    std::function<bool(UINT msg, WPARAM wp, LPARAM lp)> unhandled_key;
    // Called when a document finished loading (ok=false on error).
    std::function<void(bool ok)> loaded;
};

class ViewWindow;

// Creates the control as a child of parent (WS_CHILD | WS_VISIBLE implied).
HWND create_view_window(HWND parent, const RECT& rc, const ViewWindowOptions& options);
ViewWindow* view_from_hwnd(HWND hwnd);

// Opens a file asynchronously (replaces the current document).
void view_open(HWND hwnd, const std::filesystem::path& path);
// Find in rendered text. Returns false if not found.
bool view_find(HWND hwnd, const std::wstring& needle, bool match_case, bool backwards, bool from_start);
void view_select_all(HWND hwnd);
void view_copy(HWND hwnd);
void view_set_dark(HWND hwnd, bool dark);
// Percentage of the document above the viewport bottom (0-100), for status bars.
int view_percent(HWND hwnd);
void view_scroll_to_percent(HWND hwnd, int percent);

// Headless: renders the Markdown file to a PNG (for tests and thumbnails).
// Returns false on error. scroll_y in CSS pixels.
bool render_file_to_png(const std::filesystem::path& md, const std::filesystem::path& png, int width, int height,
                        std::int64_t scroll_y, float zoom, bool dark, std::string* stats = nullptr);

}  // namespace mdview::win32
