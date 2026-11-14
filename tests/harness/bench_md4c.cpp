// md4c throughput benchmark.
// usage: bench_md4c <bytes> [mode]   mode 0 = mixed doc, 1 = one long paragraph, 2 = single long line
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
extern "C" {
#include "md4c/md4c-html.h"
}

static void out(const MD_CHAR* t, MD_SIZE n, void* u) { ((std::string*)u)->append(t, n); }

int main(int argc, char** argv) {
    size_t target = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 20000000;
    int mode = argc > 2 ? atoi(argv[2]) : 0;
    std::string md;
    int i = 0;
    while (md.size() < target) {
        if (mode == 2) md += "word **b** *i* [l](u) ";
        else if (mode == 1) md += "word **b** *i* line\n";
        else {
            md += "## Section " + std::to_string(i++) + "\n\nSome **bold**, *italic*, `code`, [link](http://x/).\n\n";
            md += "- a\n- b\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n```\ncode\n```\n\n";
        }
    }
    std::string html;
    html.reserve(md.size() * 2);
    auto t0 = std::chrono::steady_clock::now();
    int rc = md_html(md.data(), (MD_SIZE)md.size(), out, &html, MD_DIALECT_GITHUB, 0);
    auto t1 = std::chrono::steady_clock::now();
    std::printf("mode=%d input=%zu html=%zu rc=%d time=%.3f s\n", mode, md.size(), html.size(), rc,
                std::chrono::duration<double>(t1 - t0).count());
    return rc;
}
