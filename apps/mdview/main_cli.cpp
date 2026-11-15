// mdview command-line frontend for non-Windows platforms (cairo/pango).
//
//   mdview --png out.png [--width 1000] [--height 1400] [--scroll 0] [--zoom 1] [--dark] file.md
//
// An interactive window (GTK) and the Double Commander Linux plugin are
// planned on the same core/render/cairo layers.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "platform/cairo/container.h"

int main(int argc, char** argv) {
    std::string out, file;
    int width = 1000, height = 1400;
    long long scroll = 0;
    float zoom = 1.0f;
    bool dark = false;
    for (int i = 1; i < argc; i++) {
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (!std::strcmp(argv[i], "--png")) out = next();
        else if (!std::strcmp(argv[i], "--width")) width = std::atoi(next().c_str());
        else if (!std::strcmp(argv[i], "--height")) height = std::atoi(next().c_str());
        else if (!std::strcmp(argv[i], "--scroll")) scroll = std::atoll(next().c_str());
        else if (!std::strcmp(argv[i], "--zoom")) zoom = static_cast<float>(std::atof(next().c_str()));
        else if (!std::strcmp(argv[i], "--dark")) dark = true;
        else file = argv[i];
    }
    if (out.empty() || file.empty()) {
        std::fprintf(stderr, "usage: mdview --png out.png [--width W] [--height H] [--scroll Y] [--zoom Z] [--dark] file.md\n");
        return 2;
    }
    std::string stats;
    bool ok = mdview::cairo::render_file_to_png(file, out, width, height, scroll, zoom, dark, &stats);
    std::printf("%s %s\n", ok ? "ok" : "FAILED", stats.c_str());
    return ok ? 0 : 1;
}
