// Benchmark maddy on synthetic markdown of a given size.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include "maddy/parser.h"

int main(int argc, char** argv) {
    size_t target = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1000000;
    std::string md;
    int i = 0;
    while (md.size() < target) {
        md += "## Section " + std::to_string(i) + "\n\n";
        md += "Some **bold** text, *italic*, `code` and a [link](http://x.y/" + std::to_string(i) + ").\n";
        md += "Another line of the same paragraph with ~~strike~~ text.\n\n";
        md += "- item one\n- item two\n- item three\n\n";
        md += "| a | b |\n|---|---|\n| 1 | 2 |\n\n";
        md += "```\ncode block line\n```\n\n";
        ++i;
    }
    auto cfg = std::make_shared<maddy::ParserConfig>();
    maddy::Parser p(cfg);
    std::stringstream ss(md);
    auto t0 = std::chrono::steady_clock::now();
    std::string html = p.Parse(ss);
    auto t1 = std::chrono::steady_clock::now();
    std::printf("input=%zu bytes html=%zu bytes time=%.2f s\n", md.size(), html.size(),
                std::chrono::duration<double>(t1 - t0).count());
    return 0;
}
