// HTML (and plain text) to wrapped text lines for the TV browser: ASCII output (the TV font has no general Unicode), numbered links, entities, word wrap by an
// estimate of the glyph widths of the firmware's text font.
#pragma once
#include <string>
#include <vector>

namespace web {

struct Line {
  std::string text;
  std::vector<int> links;        // indices (into Rendered::links) of the links that touch this line
};

struct Rendered {
  std::string title;
  std::vector<Line> lines;
  std::vector<std::string> links;   // absolute URLs, link n is shown as "[n+1]"
};

int textWidthPx(const std::string& ascii);                                   // estimated width of a text line in the firmware's font
Rendered renderHtml(const std::string& html, const std::string& baseUrl, int widthPx);
Rendered renderPlain(const std::string& text, int widthPx);

}  // namespace web
