#include "html_text.hpp"
#include "http.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>

namespace web {

int textWidthPx(const std::string& s) {
  int w = 0;
  for (unsigned char ch : s) {
    if (ch == ' ') w += 7;
    else if (std::strchr("il.,:;!|'`()[]{}/\\jtfrI1", ch)) w += 8;
    else if (ch == 'm' || ch == 'w') w += 20;
    else if (ch == 'M' || ch == 'W') w += 23;
    else if (std::isupper(ch)) w += 17;
    else if (std::isdigit(ch)) w += 14;
    else if (std::islower(ch)) w += 14;
    else w += 15;
  }
  return w;
}

namespace {

// UTF-8 code point -> ASCII approximation (the TV font only covers ASCII reliably)
void appendCodePoint(std::string& out, unsigned cp) {
  if (cp < 0x80) { out.push_back(char(cp)); return; }
  switch (cp) {
    case 0xA0: out.push_back(' '); return;
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: out.push_back('\''); return;
    case 0x201C: case 0x201D: case 0x201E: case 0x2033: out.push_back('"'); return;
    case 0x2013: case 0x2014: case 0x2212: out.push_back('-'); return;
    case 0x2026: out += "..."; return;
    case 0x2022: case 0x25CF: case 0xB7: out.push_back('*'); return;
    case 0xA9: out += "(c)"; return;
    case 0xAE: out += "(R)"; return;
    case 0x2122: out += "(tm)"; return;
    case 0xAB: out += "<<"; return;
    case 0xBB: out += ">>"; return;
    case 0x2192: out += "->"; return;
    case 0x2190: out += "<-"; return;
    case 0x20AC: out += "EUR"; return;
    case 0xA3: out += "GBP"; return;
    default: break;
  }
  if (cp >= 0xC0 && cp <= 0x17F) {   // Latin-1 / Latin Extended-A letters without accent
    static const char* l1 = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
    if (cp <= 0xFF) { out.push_back(l1[cp - 0xC0]); return; }
  }
  out.push_back('?');
}

std::string decodeUtf8(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x80) { out.push_back(char(c)); ++i; continue; }
    int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    unsigned cp = n == 3 ? (c & 7) : n == 2 ? (c & 15) : n == 1 ? (c & 31) : c;
    size_t j = i + 1;
    for (int k = 0; k < n && j < s.size() && ((unsigned char)s[j] & 0xC0) == 0x80; ++k, ++j) cp = (cp << 6) | ((unsigned char)s[j] & 63);
    appendCodePoint(out, cp);
    i = j;
  }
  return out;
}

std::string decodeEntities(const std::string& in) {
  static const std::map<std::string, unsigned> named = {{"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0}, {"copy", 0xA9}, {"reg", 0xAE},
    {"mdash", 0x2014}, {"ndash", 0x2013}, {"hellip", 0x2026}, {"laquo", 0xAB}, {"raquo", 0xBB}, {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D},
    {"bull", 0x2022}, {"middot", 0xB7}, {"trade", 0x2122}, {"euro", 0x20AC}, {"pound", 0xA3}, {"rarr", 0x2192}, {"larr", 0x2190}, {"times", 'x'}};
  std::string out;
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] != '&') { out.push_back(in[i]); continue; }
    size_t semi = in.find(';', i);
    if (semi == std::string::npos || semi - i > 10) { out.push_back('&'); continue; }
    std::string name = in.substr(i + 1, semi - i - 1);
    unsigned cp = 0; bool ok = false;
    if (!name.empty() && name[0] == '#') {
      char* end = nullptr;
      cp = name.size() > 1 && (name[1] == 'x' || name[1] == 'X') ? unsigned(std::strtoul(name.c_str() + 2, &end, 16)) : unsigned(std::strtoul(name.c_str() + 1, &end, 10));
      ok = end && *end == 0 && cp != 0;
    } else { auto it = named.find(name); if (it != named.end()) { cp = it->second; ok = true; } }
    if (!ok) { out.push_back('&'); continue; }
    appendCodePoint(out, cp);
    i = semi;
  }
  return out;
}

std::string lowerStr(std::string s) { for (char& c : s) c = char(std::tolower((unsigned char)c)); return s; }

struct Word { std::string text; int link = -1; int input = -1; };

class Layout {
public:
  Layout(Rendered& out, int widthPx) : out_(out), width_(widthPx) {}

  void word(const std::string& w, int link, int input = -1) { if (!w.empty()) para_.push_back(Word{w, link, input}); }
  void forceBreak() { flush(); }
  void blank() { flush(); if (!out_.lines.empty() && !out_.lines.back().text.empty()) out_.lines.push_back(Line()); }
  void preLine(const std::string& text, const std::vector<int>& links) {   // a line that is kept as is (hard wrapped if too wide)
    flush();
    std::string t = text;
    while (textWidthPx(t) > width_ && t.size() > 8) {
      size_t cut = t.size();
      while (cut > 1 && textWidthPx(t.substr(0, cut)) > width_) --cut;
      out_.lines.push_back(Line{t.substr(0, cut), links});
      t = t.substr(cut);
    }
    out_.lines.push_back(Line{t, links});
  }
  void finish() { flush(); while (!out_.lines.empty() && out_.lines.back().text.empty()) out_.lines.pop_back(); }

private:
  void flush() {
    if (para_.empty()) return;
    std::string line; std::vector<int> links, inputs;
    auto emit = [&]() { if (!line.empty()) out_.lines.push_back(Line{line, links, inputs}); line.clear(); links.clear(); inputs.clear(); };
    for (const Word& w : para_) {
      std::string candidate = line.empty() ? w.text : line + " " + w.text;
      if (!line.empty() && textWidthPx(candidate) > width_) { emit(); candidate = w.text; }
      line = candidate;
      if (w.link >= 0 && std::find(links.begin(), links.end(), w.link) == links.end()) links.push_back(w.link);
      if (w.input >= 0 && std::find(inputs.begin(), inputs.end(), w.input) == inputs.end()) inputs.push_back(w.input);
      while (textWidthPx(line) > width_ && line.size() > 8) {   // a single word wider than the line
        size_t cut = line.size();
        while (cut > 1 && textWidthPx(line.substr(0, cut)) > width_) --cut;
        out_.lines.push_back(Line{line.substr(0, cut), links, inputs});
        line = line.substr(cut);
      }
    }
    emit();
    para_.clear();
  }
  Rendered& out_;
  int width_;
  std::vector<Word> para_;
};

bool isBlockTag(const std::string& t) {
  static const char* b[] = {"p", "div", "h1", "h2", "h3", "h4", "h5", "h6", "ul", "ol", "table", "tr", "blockquote", "section", "article", "header", "footer", "nav", "form",
                            "fieldset", "dl", "dt", "dd", "main", "aside", "center", "address", "figure", "figcaption", "details", "summary", nullptr};
  for (const char** p = b; *p; ++p) if (t == *p) return true;
  return false;
}

std::string attr(const std::string& tag, const std::string& name) {   // value of attribute `name` in the text of a start tag
  std::string l = lowerStr(tag);
  size_t pos = 0;
  while ((pos = l.find(name, pos)) != std::string::npos) {
    bool startOk = pos > 0 && std::isspace((unsigned char)l[pos - 1]);
    size_t p = pos + name.size();
    while (p < l.size() && std::isspace((unsigned char)l[p])) ++p;
    if (startOk && p < l.size() && l[p] == '=') {
      ++p;
      while (p < tag.size() && std::isspace((unsigned char)tag[p])) ++p;
      if (p < tag.size() && (tag[p] == '"' || tag[p] == '\'')) { char q = tag[p]; size_t e = tag.find(q, p + 1); return tag.substr(p + 1, e == std::string::npos ? std::string::npos : e - p - 1); }
      size_t e = p; while (e < tag.size() && !std::isspace((unsigned char)tag[e]) && tag[e] != '>') ++e;
      return tag.substr(p, e - p);
    }
    pos += name.size();
  }
  return std::string();
}

std::string clipText(const std::string& s, size_t n) { return s.size() <= n ? s : s.substr(0, n); }

std::string shownInput(const Input& in) {
  const std::string& t = in.type;
  if (in.tag == "select") return "[" + clipText(in.selected >= 0 && in.selected < int(in.options.size()) ? in.options[size_t(in.selected)].second : std::string("?"), 30) + " v]";
  if (in.tag == "button" || t == "submit" || t == "button" || t == "reset" || t == "image") {
    std::string l = !in.label.empty() ? in.label : !in.value.empty() ? in.value : (t == "image" ? std::string("image") : t == "reset" ? std::string("Reset") : std::string("Submit"));
    return "[" + clipText(l, 40) + "]";
  }
  if (t == "checkbox") return in.checked ? "[x]" : "[ ]";
  if (t == "radio") return in.checked ? "(o)" : "( )";
  if (t == "password") return "[" + std::string(std::min<size_t>(in.value.size(), 24), '*') + (in.value.empty() ? std::string("________") : std::string()) + "]";
  if (in.value.empty()) return "[" + (in.placeholder.empty() ? std::string("________________") : clipText(in.placeholder, 40)) + "]";
  return "[" + clipText(in.value, 40) + "]";
}

}  // namespace

std::string latin1ToUtf8(const std::string& s) {
  std::string o;
  for (unsigned char c : s) { if (c < 0x80) o.push_back(char(c)); else { o.push_back(char(0xC0 | (c >> 6))); o.push_back(char(0x80 | (c & 63))); } }
  return o;
}

Rendered renderHtml(const std::string& html, const std::string& baseUrl, int widthPx, const std::vector<InputState>* state) {
  Rendered r;
  Layout lay(r, widthPx);
  std::string text;               // pending character data
  int link = -1;
  int skip = 0;                   // > 0 inside script/style/noscript/head-less content
  bool inPre = false, inTitle = false;
  std::string title;
  std::string pre;                // text accumulated inside <pre>
  std::vector<int> preLinks;
  int listDepth = 0;
  int curForm = -1;
  auto addInput = [&](Input in, bool visible) {
    in.form = curForm;
    size_t idx = r.inputs.size();
    if (state && idx < state->size()) { const InputState& st = (*state)[idx]; if (in.tag != "select") in.value = st.value; in.checked = st.checked; in.selected = st.selected; if (in.selected >= int(in.options.size())) in.selected = 0; }
    r.inputs.push_back(in);
    if (visible) lay.word(shownInput(r.inputs.back()), link, int(idx));
    return int(idx);
  };

  auto flushText = [&]() {
    if (text.empty()) return;
    std::string t = decodeEntities(decodeUtf8(text));
    text.clear();
    if (inTitle) { title += t; return; }
    if (skip) return;
    if (inPre) { pre += t; return; }
    std::string w;
    for (size_t i = 0; i <= t.size(); ++i) {
      if (i == t.size() || std::isspace((unsigned char)t[i])) { lay.word(w, link); w.clear(); }
      else w.push_back(t[i]);
    }
  };
  auto flushPre = [&]() {
    if (pre.empty()) return;
    size_t i = 0;
    while (i <= pre.size()) {
      size_t j = pre.find('\n', i);
      std::string line = pre.substr(i, j == std::string::npos ? std::string::npos : j - i);
      while (!line.empty() && (line.back() == '\r')) line.pop_back();
      std::string detab; for (char ch : line) { if (ch == '\t') detab += "    "; else detab.push_back(ch); }
      lay.preLine(detab, preLinks);
      if (j == std::string::npos) break;
      i = j + 1;
    }
    pre.clear();
  };

  for (size_t i = 0; i < html.size();) {
    if (html[i] != '<') { text.push_back(html[i]); ++i; continue; }
    if (html.compare(i, 4, "<!--") == 0) { flushText(); size_t e = html.find("-->", i + 4); i = e == std::string::npos ? html.size() : e + 3; continue; }
    size_t e = html.find('>', i);
    if (e == std::string::npos) { text.push_back(html[i]); ++i; continue; }
    std::string tag = html.substr(i + 1, e - i - 1);
    i = e + 1;
    if (!tag.empty() && (tag[0] == '!' || tag[0] == '?')) continue;   // doctype, processing instructions
    if (tag.empty() || (!std::isalpha((unsigned char)tag[0]) && tag[0] != '/')) { text += "<" + tag + ">"; continue; }
    flushText();
    bool closing = tag[0] == '/';
    size_t ns = closing ? 1 : 0, ne = ns;
    while (ne < tag.size() && (std::isalnum((unsigned char)tag[ne]) || tag[ne] == '-')) ++ne;
    std::string name = lowerStr(tag.substr(ns, ne - ns));
    if (name == "script" || name == "style" || name == "template" || name == "svg") {
      if (!closing) { std::string endTag = "</" + name; size_t k = lowerStr(html).find(endTag, i); size_t k2 = k == std::string::npos ? html.size() : html.find('>', k); i = k2 == std::string::npos ? html.size() : k2 + 1; }
      continue;
    }
    if (name == "title") { inTitle = !closing; continue; }
    if (name == "head") { continue; }
    if (name == "pre") { if (closing) { flushPre(); inPre = false; lay.blank(); } else { lay.blank(); inPre = true; preLinks.clear(); } continue; }
    if (name == "a") {
      if (closing) {
        if (link >= 0) { lay.word("[" + std::to_string(link + 1) + "]", link); }
        link = -1;
      } else {
        std::string href = attr(tag, "href");
        std::string url = href.empty() ? std::string() : resolveUrl(baseUrl, decodeEntities(href));
        if (!url.empty()) { r.links.push_back(url); link = int(r.links.size()) - 1; if (inPre) preLinks.push_back(link); } else link = -1;
      }
      continue;
    }
    if (name == "br") { if (inPre) pre += "\n"; else lay.forceBreak(); continue; }
    if (name == "hr") { lay.blank(); lay.preLine(std::string(40, '-'), {}); lay.blank(); continue; }
    if (name == "li") { if (!closing) { lay.forceBreak(); lay.word(listDepth > 1 ? "  -" : "*", -1); } else lay.forceBreak(); continue; }
    if (name == "ul" || name == "ol") { if (closing) { if (listDepth > 0) --listDepth; lay.forceBreak(); } else { ++listDepth; lay.forceBreak(); } continue; }
    if (name == "img") { std::string alt = decodeEntities(decodeUtf8(attr(tag, "alt"))); if (!alt.empty()) lay.word("[" + alt + "]", link); continue; }
    if (name == "td" || name == "th") { if (!closing) lay.word(" ", -1); continue; }
    if (name == "form") {
      if (closing) curForm = -1;
      else {
        Form f;
        std::string m = lowerStr(attr(tag, "method"));
        f.method = m == "post" ? "POST" : "GET";
        std::string a = decodeEntities(attr(tag, "action"));
        f.action = a.empty() ? baseUrl : resolveUrl(baseUrl, a);
        if (f.action.empty()) f.action = baseUrl;
        r.forms.push_back(f); curForm = int(r.forms.size()) - 1;
      }
      lay.forceBreak();
      continue;
    }
    if (name == "meta") {
      if (lowerStr(attr(tag, "http-equiv")) == "refresh") {
        std::string content = attr(tag, "content");
        std::string lc = lowerStr(content);
        size_t u = lc.find("url=");
        int delay = std::atoi(content.c_str());
        if (u != std::string::npos && delay <= 5) {
          std::string target = content.substr(u + 4);
          if (!target.empty() && (target[0] == '\'' || target[0] == '"')) target.erase(0, 1);
          while (!target.empty() && (target.back() == '\'' || target.back() == '"' || target.back() == ' ')) target.pop_back();
          r.refreshUrl = resolveUrl(baseUrl, decodeEntities(target));
        }
      }
      continue;
    }
    if (name == "input") {
      Input in; in.tag = "input"; in.type = lowerStr(attr(tag, "type"));
      if (in.type.empty()) in.type = "text";
      in.name = decodeEntities(attr(tag, "name")); in.value = decodeEntities(decodeUtf8(attr(tag, "value"))); in.placeholder = decodeEntities(decodeUtf8(attr(tag, "placeholder")));
      std::string l = lowerStr(tag);
      in.checked = l.find(" checked") != std::string::npos;
      if (in.type == "image") in.label = decodeEntities(decodeUtf8(attr(tag, "alt")));
      addInput(in, in.type != "hidden");
      continue;
    }
    if (name == "textarea") {
      if (!closing) {
        size_t k = lowerStr(html).find("</textarea", i);
        std::string raw = html.substr(i, k == std::string::npos ? std::string::npos : k - i);
        size_t k2 = k == std::string::npos ? html.size() : html.find('>', k);
        i = k2 == std::string::npos ? html.size() : k2 + 1;
        Input in; in.tag = "textarea"; in.type = "text"; in.name = decodeEntities(attr(tag, "name")); in.value = decodeEntities(decodeUtf8(raw));
        while (!in.value.empty() && (in.value.front() == char(10) || in.value.front() == char(13))) in.value.erase(0, 1);
        addInput(in, true);
      }
      continue;
    }
    if (name == "select") {
      if (!closing) {
        std::string low = lowerStr(html);
        size_t k = low.find("</select", i);
        std::string seg = html.substr(i, k == std::string::npos ? std::string::npos : k - i);
        size_t k2 = k == std::string::npos ? html.size() : html.find('>', k);
        i = k2 == std::string::npos ? html.size() : k2 + 1;
        Input in; in.tag = "select"; in.type = "select"; in.name = decodeEntities(attr(tag, "name"));
        std::string lseg = lowerStr(seg);
        for (size_t o = lseg.find("<option"); o != std::string::npos; o = lseg.find("<option", o + 7)) {
          size_t oe = seg.find('>', o);
          if (oe == std::string::npos) break;
          std::string otag = seg.substr(o + 1, oe - o - 1);
          size_t ot = seg.find('<', oe + 1);
          std::string text = decodeEntities(decodeUtf8(seg.substr(oe + 1, ot == std::string::npos ? std::string::npos : ot - oe - 1)));
          while (!text.empty() && std::isspace((unsigned char)text.back())) text.pop_back();
          size_t tb = text.find_first_not_of(" \t\r\n"); text = tb == std::string::npos ? std::string() : text.substr(tb);
          std::string val = attr(otag, "value");
          if (val.empty() && lowerStr(otag).find("value") == std::string::npos) val = text;
          if (lowerStr(otag).find(" selected") != std::string::npos) in.selected = int(in.options.size());
          in.options.emplace_back(decodeEntities(val), text);
        }
        if (in.options.empty()) in.options.emplace_back(std::string(), std::string());
        addInput(in, true);
      }
      continue;
    }
    if (name == "button") {
      if (!closing) {
        size_t k = lowerStr(html).find("</button", i);
        std::string inner = html.substr(i, k == std::string::npos ? std::string::npos : k - i);
        size_t k2 = k == std::string::npos ? html.size() : html.find('>', k);
        i = k2 == std::string::npos ? html.size() : k2 + 1;
        std::string label;   // inner text without tags
        for (size_t q = 0; q < inner.size();) { if (inner[q] == '<') { size_t e2 = inner.find('>', q); q = e2 == std::string::npos ? inner.size() : e2 + 1; } else label.push_back(inner[q++]); }
        label = decodeEntities(decodeUtf8(label));
        { std::string c2; bool sp2 = false; for (char ch : label) { if (std::isspace((unsigned char)ch)) { sp2 = true; continue; } if (sp2 && !c2.empty()) c2.push_back(' '); sp2 = false; c2.push_back(ch); } label = c2; }
        Input in; in.tag = "button"; in.type = lowerStr(attr(tag, "type")); if (in.type.empty()) in.type = "submit";
        in.name = decodeEntities(attr(tag, "name")); in.value = decodeEntities(decodeUtf8(attr(tag, "value"))); in.label = label;
        addInput(in, true);
      }
      continue;
    }
    if (isBlockTag(name)) {
      bool para = name == "p" || (name.size() == 2 && name[0] == 'h' && std::isdigit((unsigned char)name[1])) || name == "blockquote" || name == "table";
      if (para) lay.blank(); else lay.forceBreak();
      continue;
    }
  }
  flushText();
  flushPre();
  lay.finish();
  while (!title.empty() && std::isspace((unsigned char)title.back())) title.pop_back();
  size_t tb = title.find_first_not_of(" \t\r\n");
  r.title = tb == std::string::npos ? std::string() : title.substr(tb);
  { std::string collapsed; bool sp = false; for (char ch : r.title) { if (std::isspace((unsigned char)ch)) { sp = true; continue; } if (sp && !collapsed.empty()) collapsed.push_back(' '); sp = false; collapsed.push_back(ch); } r.title = collapsed; }
  return r;
}

Rendered renderPlain(const std::string& text, int widthPx) {
  Rendered r;
  Layout lay(r, widthPx);
  std::string t = decodeUtf8(text);
  size_t i = 0;
  while (i <= t.size()) {
    size_t j = t.find('\n', i);
    std::string line = t.substr(i, j == std::string::npos ? std::string::npos : j - i);
    while (!line.empty() && line.back() == '\r') line.pop_back();
    std::string detab; for (char ch : line) { if (ch == '\t') detab += "    "; else if ((unsigned char)ch >= 32 || ch == ' ') detab.push_back(ch); }
    lay.preLine(detab, {});
    if (j == std::string::npos) break;
    i = j + 1;
  }
  lay.finish();
  return r;
}

}  // namespace web
