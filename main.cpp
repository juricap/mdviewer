#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include "maddy/parser.h"  
#include "webview.h"       

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: md_viewer <file.md>" << std::endl;
        return 1;
    }

    std::ifstream file(argv[1]);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << argv[1] << std::endl;
        return 1;
    }
    
    std::string md_content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    // Parse Markdown to HTML
    std::shared_ptr<maddy::ParserConfig> config = std::make_shared<maddy::ParserConfig>();
    std::stringstream md_stream(md_content);
    maddy::Parser parser(config);
    std::string html = parser.Parse(md_stream);

    // Minimal wrapper
    std::string full_html = R"(
        <!DOCTYPE html>
        <html>
        <head>
            <meta charset="UTF-8">
            <style>
                body { 
                    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;
                    padding: 20px 40px;
                    max-width: 900px;
                    margin: 0 auto;
                    line-height: 1.6;
                    color: #333;
                }
                h1, h2, h3, h4, h5, h6 { 
                    margin-top: 24px;
                    margin-bottom: 16px;
                    font-weight: 600;
                }
                h1 { font-size: 2em; border-bottom: 1px solid #eaecef; padding-bottom: .3em; }
                h2 { font-size: 1.5em; border-bottom: 1px solid #eaecef; padding-bottom: .3em; }
                h3 { font-size: 1.25em; }
                code {
                    background-color: rgba(27,31,35,.05);
                    padding: .2em .4em;
                    margin: 0;
                    font-size: 85%;
                    border-radius: 3px;
                    font-family: Consolas, 'Courier New', monospace;
                }
                pre {
                    background-color: #f6f8fa;
                    padding: 16px;
                    overflow: auto;
                    font-size: 85%;
                    line-height: 1.45;
                    border-radius: 3px;
                }
                pre code {
                    background-color: transparent;
                    padding: 0;
                }
                blockquote {
                    margin: 0;
                    padding: 0 1em;
                    color: #6a737d;
                    border-left: .25em solid #dfe2e5;
                }
                table {
                    border-spacing: 0;
                    border-collapse: collapse;
                    margin-top: 0;
                    margin-bottom: 16px;
                }
                table th, table td {
                    padding: 6px 13px;
                    border: 1px solid #dfe2e5;
                }
                table tr:nth-child(2n) {
                    background-color: #f6f8fa;
                }
                a {
                    color: #0366d6;
                    text-decoration: none;
                }
                a:hover {
                    text-decoration: underline;
                }
                img {
                    max-width: 100%;
                    height: auto;
                }
            </style>
        </head>
        <body>)" + html + R"(</body>
        </html>
    )";

    // Create web view
    webview::webview w(true, nullptr);
    w.set_title("Markdown Viewer");
    w.set_size(800, 600, WEBVIEW_HINT_NONE);
    w.set_html(full_html.c_str());
    w.run();

    return 0;
}