#include "ui/native_window.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
namespace manta {
namespace {
struct WindowState {
  std::shared_ptr<GopPreviewStore> store;
  PreviewWindowOptions options;
  std::shared_ptr<const GopPreviewFrame> frame;   // immutable copy owned by this thread
  uint64_t shownSequence = 0;
  bool smoke = false;
  DWORD finishedAt = 0;
  bool captured = false;
  bool showOffscreen = false;   // toggled with O
  std::shared_ptr<const GopPreviewFrame> offscreen;
};
constexpr UINT_PTR kTimer = 1;
constexpr int kBar = 80;   // host status bar height (window chrome, not guest content)

// Renders exactly the window content (same drawContent as WM_PAINT) into an off-screen DIB and saves it as a
// top-down 32-bit BMP: repeatable evidence of what the window shows, independent of occlusion or minimization.
void drawContent(HDC dc, RECT r, WindowState &st);
void captureClient(HWND w, const std::string &path, WindowState &st) {
  RECT r; GetClientRect(w, &r);
  if (r.right - r.left < 64 || r.bottom - r.top < 128) r = RECT{0, 0, 1424, 861};
  int cw = r.right - r.left, ch = r.bottom - r.top;
  HDC dc = GetDC(w), mem = CreateCompatibleDC(dc);
  BITMAPINFO bi{}; bi.bmiHeader = {sizeof(BITMAPINFOHEADER), cw, -ch, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
  void *bits = nullptr;
  HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  auto old = SelectObject(mem, dib);
  drawContent(mem, RECT{0, 0, cw, ch}, st);
  GdiFlush();
  std::ofstream f(path, std::ios::binary);
  BITMAPFILEHEADER fh{}; fh.bfType = 0x4d42; fh.bfOffBits = sizeof fh + sizeof(BITMAPINFOHEADER);
  fh.bfSize = fh.bfOffBits + cw * ch * 4;
  f.write(reinterpret_cast<const char *>(&fh), sizeof fh);
  f.write(reinterpret_cast<const char *>(&bi.bmiHeader), sizeof(BITMAPINFOHEADER));
  f.write(static_cast<const char *>(bits), std::streamsize(cw) * ch * 4);
  SelectObject(mem, old); DeleteObject(dib); DeleteDC(mem); ReleaseDC(w, dc);
}

void drawContent(HDC dc, RECT r, WindowState &st) {
  auto shown = st.showOffscreen && st.offscreen ? st.offscreen : st.frame;
  HBRUSH bar = CreateSolidBrush(RGB(32, 32, 48));
  RECT barRect{0, 0, r.right, kBar}; FillRect(dc, &barRect, bar); DeleteObject(bar);
  SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 210, 90));
  std::string line1 = shown && !shown->empty() ? shown->label : "DIAGNOSTIC GOP PREVIEW - no enabled, latched GOP window has been programmed by the guest yet";
  TextOutA(dc, 8, 6, line1.c_str(), int(line1.size()));
  SetTextColor(dc, RGB(200, 200, 215));
  std::string line2 = shown ? shown->description : std::string();
  line2 += st.store ? "  [" + st.store->status() + (st.store->finished() ? "; guest run stopped" : "; running") + "]" : "";
  TextOutA(dc, 8, 24, line2.c_str(), int(line2.size()));
  char sum[96] = "";
  if (shown && !shown->empty()) snprintf(sum, sizeof sum, "frame checksum %016llx (host bar above is window chrome, not guest content)%s", (unsigned long long)shown->checksum, st.offscreen ? "  [O toggles GOP / offscreen]" : "");
  TextOutA(dc, 8, 42, sum, int(strlen(sum)));
  SetTextColor(dc, RGB(140, 200, 140));
  const char *help = "Remote (original firmware input path): 0-9 digits (verified)  M menu (CANDIDATE, raw 0xFC)  S/Esc/Enter/arrows/P/I unmapped: no evidence yet  |  O toggles offscreen inspector";
  TextOutA(dc, 8, 60, help, int(strlen(help)));
  RECT area{0, kBar, r.right, r.bottom};
  HBRUSH back = CreateSolidBrush(RGB(20, 20, 20)); FillRect(dc, &area, back); DeleteObject(back);
  if (shown && !shown->empty()) {
    const auto &f = *shown;
    // Pixels the guest did not provide are shown as a neutral checker so they cannot be mistaken for guest black.
    std::vector<uint32_t> out(f.bgra.size());
    for (uint32_t y = 0; y < f.height; ++y)
      for (uint32_t x = 0; x < f.width; ++x) {
        size_t i = size_t(y) * f.width + x;
        out[i] = f.covered[i] ? (f.bgra[i] & 0x00ffffffu) : ((((x >> 4) ^ (y >> 4)) & 1) ? 0x00505050u : 0x00383838u);
      }
    BITMAPINFO bi{}; bi.bmiHeader = {sizeof(BITMAPINFOHEADER), LONG(f.width), -LONG(f.height), 1, 32, BI_RGB, 0, 0, 0, 0, 0};
    int aw = r.right, ah = r.bottom - kBar;
    double scale = std::min(double(aw) / f.width, double(ah) / f.height);
    if (scale > 1.0) scale = std::max(1.0, double(int(scale)));
    int dw = int(f.width * scale), dh = int(f.height * scale);
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, (aw - dw) / 2, kBar + (ah - dh) / 2, dw, dh, 0, 0, f.width, f.height, out.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
  }
}
void paint(HWND w, WindowState &st) {
  PAINTSTRUCT ps; HDC dc = BeginPaint(w, &ps);
  RECT r; GetClientRect(w, &r);
  drawContent(dc, r, st);
  EndPaint(w, &ps);
}

LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM a, LPARAM b) {
  auto *st = reinterpret_cast<WindowState *>(GetWindowLongPtrW(w, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW *>(b)->lpCreateParams));
    return DefWindowProcW(w, msg, a, b);
  }
  if (!st) return DefWindowProcW(w, msg, a, b);
  if (msg == WM_TIMER) {
    if (st->smoke) { DestroyWindow(w); return 0; }
    if (st->store) {
      auto latest = st->store->latest();
      if (latest && latest->sequence != st->shownSequence) {
        st->frame = latest; st->shownSequence = latest->sequence;
        InvalidateRect(w, nullptr, FALSE);
      }
      if (auto off = st->store->latestOffscreen(); off && (!st->offscreen || off->sequence != st->offscreen->sequence)) {
        st->offscreen = off; InvalidateRect(w, nullptr, FALSE);
      }
      if (st->store->finished()) {
        if (!st->finishedAt) { st->finishedAt = GetTickCount(); InvalidateRect(w, nullptr, FALSE); }
        else if (GetTickCount() - st->finishedAt > 700 && !st->captured) {
          st->captured = true;
          UpdateWindow(w);
          if (!st->options.capturePath.empty()) { st->showOffscreen = false; captureClient(w, st->options.capturePath, *st); }
          if (!st->options.captureOffscreenPath.empty() && st->offscreen) { st->showOffscreen = true; captureClient(w, st->options.captureOffscreenPath, *st); st->showOffscreen = false; }
        }
        if (st->options.closeAfterFinishMs && st->finishedAt && GetTickCount() - st->finishedAt > st->options.closeAfterFinishMs + 800 &&
            (st->captured || st->options.capturePath.empty()))
          DestroyWindow(w);
      }
    }
    return 0;
  }
  if (msg == WM_PAINT) { paint(w, *st); return 0; }
  if (msg == WM_KEYDOWN && st->store && !(b & (1 << 30))) {   // ignore Windows auto-repeat: one press = one emulated IR frame
    const char *function = nullptr;
    switch (a) {
      case 'M': function = "MENU"; break;   case 'S': function = "SOURCE"; break;  case VK_ESCAPE: function = "EXIT"; break;
      case VK_RETURN: function = "OK"; break;  case VK_UP: function = "UP"; break;  case VK_DOWN: function = "DOWN"; break;
      case VK_LEFT: function = "LEFT"; break;  case VK_RIGHT: function = "RIGHT"; break;  case 'P': function = "POWER"; break;  case 'I': function = "INFO"; break;
      default: break;
    }
    static char digitName[] = "DIGIT_0";   // digits are VERIFIED functions (original control flow, see docs/ir-input-path.md)
    if (!function && a >= '0' && a <= '9') { digitName[6] = char(a); function = digitName; }
    if (!function && a >= VK_NUMPAD0 && a <= VK_NUMPAD9) { digitName[6] = char('0' + (a - VK_NUMPAD0)); function = digitName; }
    if (function) { st->store->remote->push(function); return 0; }
  }
  if (msg == WM_KEYDOWN && (a == 'O' || a == 'o')) { if (st->offscreen) st->showOffscreen = !st->showOffscreen; InvalidateRect(w, nullptr, FALSE); return 0; }
  if (msg == WM_ERASEBKGND) return 1;
  if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
  return DefWindowProcW(w, msg, a, b);
}
} // namespace
std::wstring pickFirmware() {
  wchar_t path[32768]{};
  OPENFILENAMEW o{};
  o.lStructSize = sizeof(o);
  o.lpstrFilter = L"Original SPI backup (*.bin)\0*.bin\0";
  o.lpstrFile = path;
  o.nMaxFile = 32768;
  o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  return GetOpenFileNameW(&o) ? path : L"";
}
void nativeWindow(const std::string &, bool smoke, std::shared_ptr<GopPreviewStore> store, const PreviewWindowOptions &options) {
  WindowState state;
  state.store = std::move(store); state.options = options; state.smoke = smoke;
  auto instance = GetModuleHandleW(nullptr);
  WNDCLASSW c{};
  c.lpfnWndProc = proc;
  c.hInstance = instance;
  c.lpszClassName = L"MantaGuestDisplay";
  c.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  if (!RegisterClassW(&c) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    throw std::runtime_error("RegisterClassW failed: " + std::to_string(GetLastError()));
  auto w = CreateWindowW(c.lpszClassName,
                         L"Manta LCD4214 — guest display (diagnostic GOP preview)",
                         WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1440, 900, nullptr,
                         nullptr, instance, &state);
  if (!w)
    throw std::runtime_error("CreateWindowW failed: " + std::to_string(GetLastError()));
  ShowWindow(w, SW_SHOW);
  for (auto key : options.injectSelftestKeys) PostMessageW(w, WM_KEYDOWN, key, 0);
  std::cout << (state.store ? "NATIVE_WINDOW_CREATED; diagnostic GOP preview (guest pixels only once a GOP window is enabled)\n"
                            : "NATIVE_WINDOW_CREATED; guest display not initialized\n");
  if (!SetTimer(w, kTimer, smoke ? 400 : 100, nullptr)) {
    auto error = GetLastError(); DestroyWindow(w);
    throw std::runtime_error("Window timer failed: " + std::to_string(error));
  }
  MSG msg;
  int result;
  while ((result = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (result == -1) throw std::runtime_error("GetMessageW failed: " + std::to_string(GetLastError()));
  if (!options.injectSelftestKeys.empty() && state.store) {
    std::cout << "REMOTE_QUEUED:";
    for (auto &f : state.store->remote->drain()) std::cout << ' ' << f;
    std::cout << '\n';
  }
}
} // namespace manta
#else
namespace manta {
std::wstring pickFirmware() { return L""; }
void nativeWindow(const std::string &, bool, std::shared_ptr<GopPreviewStore>, const PreviewWindowOptions &) {
  throw std::runtime_error(
      "Native display backend currently Windows only; use --headless on Linux");
}
} // namespace manta
#endif
