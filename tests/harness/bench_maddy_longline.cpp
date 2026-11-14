// Pathological input: a single paragraph / single line without blank lines.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include "maddy/parser.h"

int main(int argc, char** argv) {
    size_t target = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 100000;
    bool oneLine = argc > 2;
    std::string md;
    while (md.size() < target) md += oneLine ? "word **b** *i* " : "word **b** *i* line\n";
    auto cfg = std::make_shared<maddy::ParserConfig>();
    maddy::Parser p(cfg);
    std::stringstream ss(md);
    auto t0 = std::chrono::steady_clock::now();
    try {
        std::string html = p.Parse(ss);
        std::printf("ok html=%zu ", html.size());
    } catch (const std::exception& e) {
        std::printf("exception: %s ", e.what());
    }
    auto t1 = std::chrono::steady_clock::now();
    std::printf("input=%zu oneLine=%d time=%.2f s\n", md.size(), (int)oneLine,
                std::chrono::duration<double>(t1 - t0).count());
    return 0;
}
