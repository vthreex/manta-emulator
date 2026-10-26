// HTML (and plain text) to wrapped text lines for the TV browser: ASCII output (the TV font has no general Unicode), numbered links, form fields, entities, word
// wrap by an estimate of the glyph widths of the firmware's text font.
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace web {

struct Input {
  int form = -1;                                              // index into Rendered::forms, -1 = not inside a form
  std::string tag;                                            // input, select, textarea, button
  std::string type;                                           // text, password, search, submit, button, checkbox, radio, hidden, image, ...
  std::string name, value, placeholder, label;
  bool checked = false;
  std::vector<std::pair<std::string, std::string>> options;   // select: (value, label)
  int selected = 0;
};

struct Form {
  std::string method = "GET";
  std::string action;                                         // absolute URL (the page's own URL when the form has no action)
};

struct InputState { std::string value; bool checked = false; int selected = 0; };   // what the user changed; carried across re-renders (same input order)

struct Line {
  std::string text;
  std::vector<int> links;        // indices (into Rendered::links) of the links that touch this line
  std::vector<int> inputs;       // indices (into Rendered::inputs) of the form fields shown on this line
};

struct Rendered {
  std::string title;
  std::vector<Line> lines;
  std::vector<std::string> links;   // absolute URLs, link n is shown as "[n+1]"
  std::vector<Form> forms;
  std::vector<Input> inputs;
  std::vector<std::pair<std::string, int>> anchors;   // element id -> line index (for #fragment links)
  std::string refreshUrl;           // <meta http-equiv="refresh" content="N; url=..."> with a short delay
};

int textWidthPx(const std::string& ascii);                                   // estimated width of a text line in the firmware's font
std::string latin1ToUtf8(const std::string& s);
Rendered renderHtml(const std::string& html, const std::string& baseUrl, int widthPx, const std::vector<InputState>* state = nullptr);
Rendered renderPlain(const std::string& text, int widthPx);

}  // namespace web
