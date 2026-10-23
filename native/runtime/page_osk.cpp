// The on-screen keyboard page (see docs/osk.md). Windows, rectangles, styles and text are the firmware's own ZUI elements; this file only lays them out and
// implements the behaviour (focus movement, typing, shift, accept/cancel) on top of the page engine in pages.cpp.
#include "pages.hpp"
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace pages {
namespace {

enum Action { kChar, kShift, kSymbols, kBackspace, kSpace, kCancel, kOk };
enum Layer { kLower, kUpper, kSym };

struct KeyInfo {
  unsigned hwnd = 0;
  Action action = kChar;
  std::u16string label[3];            // label on the lower / upper / symbol layer
};

struct OskState {
  std::string text;
  Layer layer = kLower;
  std::vector<KeyInfo> keys;          // same order as PageDef::items
  unsigned fieldHwnd = 0;
  std::function<void(bool, const std::string&)> done;
  PagePtr returnTo;
  bool finished = false;
};

std::u16string widen(const char* s) { std::u16string o; while (*s) o.push_back(char16_t(uint8_t(*s++))); return o; }

void refreshText(Host& host, OskState& st) {
  std::u16string t;
  for (char ch : st.text) t.push_back(char16_t(uint8_t(ch)));
  t.push_back(u'_');
  host.setLabel(st.fieldHwnd, t);
}
void refreshKeys(Host& host, OskState& st) {
  for (const KeyInfo& k : st.keys) host.setLabel(k.hwnd, k.label[st.layer]);
}
void typeChar(Host& host, OskState& st, char ch) {
  if (st.text.size() < 100) st.text.push_back(ch);
  refreshText(host, st);
}
void finish(Host& host, OskState& st, bool accepted) {
  if (st.finished) return;
  st.finished = true;
  if (st.done) st.done(accepted, st.text);
  if (st.returnTo) host.request(st.returnTo); else host.closeAll();
}
void press(Host& host, OskState& st, const KeyInfo& k) {
  switch (k.action) {
    case kChar: {
      const std::u16string& l = k.label[st.layer];
      if (!l.empty()) typeChar(host, st, char(l[0]));
      if (st.layer == kUpper) { st.layer = kLower; refreshKeys(host, st); }   // one-shot shift
      break;
    }
    case kSpace: typeChar(host, st, ' '); break;
    case kBackspace: if (!st.text.empty()) { st.text.pop_back(); refreshText(host, st); } break;
    case kShift: st.layer = st.layer == kUpper ? kLower : kUpper; refreshKeys(host, st); break;
    case kSymbols: st.layer = st.layer == kSym ? kLower : kSym; refreshKeys(host, st); break;
    case kCancel: finish(host, st, false); break;
    case kOk: finish(host, st, true); break;
  }
}

}  // namespace

void requestKeyboard(Host& host, const std::string& initial, std::function<void(bool, const std::string&)> done, PagePtr returnTo) {
  auto st = std::make_shared<OskState>();
  st->text = initial; st->done = std::move(done); st->returnTo = std::move(returnTo);
  auto pg = std::make_shared<PageDef>();
  pg->name = "keyboard";

  constexpr int u = 66, x0 = 20, keyH = 44, rowPitch = 50, firstRow = 84;
  struct Spec { const char* lo; const char* up; const char* sym; float w; Action act; };
  const std::vector<std::vector<Spec>> rows = {
    {{"1","!","1",1,kChar},{"2","@","2",1,kChar},{"3","#","3",1,kChar},{"4","$","4",1,kChar},{"5","%","5",1,kChar},{"6","^","6",1,kChar},{"7","&","7",1,kChar},
     {"8","*","8",1,kChar},{"9","(","9",1,kChar},{"0",")","0",1,kChar},{"Del","Del","Del",1.5f,kBackspace}},
    {{"q","Q","~",1,kChar},{"w","W","`",1,kChar},{"e","E","|",1,kChar},{"r","R","\\",1,kChar},{"t","T","[",1,kChar},{"y","Y","]",1,kChar},{"u","U","{",1,kChar},
     {"i","I","}",1,kChar},{"o","O","<",1,kChar},{"p","P",">",1,kChar},{"@","@","@",1.5f,kChar}},
    {{"a","A",";",1,kChar},{"s","S",":",1,kChar},{"d","D","=",1,kChar},{"f","F","+",1,kChar},{"g","G","-",1,kChar},{"h","H","_",1,kChar},{"j","J","/",1,kChar},
     {"k","K","?",1,kChar},{"l","L","!",1,kChar},{"'","\"","\"",1,kChar},{"_","_","_",1.5f,kChar}},
    {{"Shift","Shift","Shift",1.5f,kShift},{"z","Z","#",1,kChar},{"x","X","$",1,kChar},{"c","C","%",1,kChar},{"v","V","&",1,kChar},{"b","B","*",1,kChar},{"n","N","(",1,kChar},
     {"m","M",")",1,kChar},{",",",",",",1,kChar},{".",".",".",1,kChar},{"/","/","/",1,kChar}},
    {{"123","abc","abc",1.5f,kSymbols},{"Space","Space","Space",5.5f,kSpace},{"Cancel","Cancel","Cancel",2.25f,kCancel},{"OK","OK","OK",2.25f,kOk}}};
  const int pageW = int(11.5f * u) + 2 * x0 - 6, pageH = firstRow + int(rows.size()) * rowPitch + 52;

  Win root; root.cls = 0; root.flags = 0x81; root.w = uint16_t(pageW); root.h = uint16_t(pageH);
  root.x = uint16_t((1360 - pageW) / 2); root.y = uint16_t(768 - pageH - 24);
  pg->add(root);
  Win panel; panel.w = root.w; panel.h = root.h; panel.normal = {kPanelTeal};
  pg->add(panel);
  Win field; field.cls = 3; field.x = x0; field.y = 16; field.w = uint16_t(pageW - 2 * x0); field.h = 52;
  field.normal = {kFillBlack, comp(3, kTextLeft)}; field.focus = field.normal; field.disabled = field.normal;
  std::u16string initialText; for (char ch : st->text) initialText.push_back(char16_t(uint8_t(ch))); initialText.push_back(u'_');
  st->fieldHwnd = pg->add(field, initialText);
  unsigned focusItem = 0;
  for (unsigned r = 0; r < rows.size(); ++r) {
    float cursor = 0;
    for (const Spec& s : rows[r]) {
      Win k; k.cls = 3; k.x = uint16_t(x0 + int(cursor * u)); k.y = uint16_t(firstRow + int(r) * rowPitch); k.w = uint16_t(int(s.w * u) - 6); k.h = keyH;
      k.normal = {kFillDark, comp(3, kTextCentre)}; k.focus = {kFillHighlight, comp(3, kTextCentreDark)}; k.disabled = {kFillDark, comp(3, kTextCentreGrey)};
      KeyInfo ki; ki.action = s.act;
      ki.label[0] = widen(s.lo); ki.label[1] = widen(s.up); ki.label[2] = widen(s.sym);
      ki.hwnd = pg->add(k, ki.label[0]);
      Item it; it.hwnd = ki.hwnd; it.row = r; it.cx = k.x + k.w / 2;
      if (ki.label[0] == u"q") focusItem = unsigned(pg->items.size());
      pg->items.push_back(it);
      st->keys.push_back(ki);
      cursor += s.w;
    }
  }
  Win hint; hint.cls = 3; hint.x = x0; hint.y = uint16_t(firstRow + int(rows.size()) * rowPitch + 4); hint.w = uint16_t(pageW - 2 * x0); hint.h = 34;
  hint.normal = {comp(3, kTextCentre)};
  pg->add(hint, u"Arrows: move   OK: type   MENU: shift   F10: accept   EXIT: cancel");
  pg->focus = focusItem;

  pg->onPress = [st](Host& host, unsigned item) { if (item < st->keys.size()) press(host, *st, st->keys[item]); };
  pg->onKey = [st](Host& host, const std::string& f) {
    if (f == "MENU") { st->layer = st->layer == kUpper ? kLower : kUpper; refreshKeys(host, *st); return true; }
    if (f == "EXIT") { finish(host, *st, false); return true; }
    if (f == "OSK_ENTER") { finish(host, *st, true); return true; }
    if (f == "OSK_BACKSPACE") { if (!st->text.empty()) { st->text.pop_back(); refreshText(host, *st); } return true; }
    if (f.rfind("OSK_CHAR:", 0) == 0 && f.size() > 9) { typeChar(host, *st, f[9]); return true; }
    if (f.rfind("DIGIT_", 0) == 0 && f.size() == 7) { typeChar(host, *st, f[6]); return true; }
    return false;
  };
  host.request(pg);
}

}  // namespace pages
