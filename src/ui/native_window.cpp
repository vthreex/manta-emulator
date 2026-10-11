#include "ui/native_window.hpp"
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
namespace manta {
static LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM a, LPARAM b) {
  if (msg == WM_TIMER) {
    DestroyWindow(w);
    return 0;
  }
  if (msg == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(w, msg, a, b);
}
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
void nativeWindow(const std::string &, bool smoke) {
  auto instance = GetModuleHandleW(nullptr);
  WNDCLASSW c{};
  c.lpfnWndProc = proc;
  c.hInstance = instance;
  c.lpszClassName = L"MantaGuestDisplay";
  c.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  if (!RegisterClassW(&c) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    throw std::runtime_error("RegisterClassW failed: " + std::to_string(GetLastError()));
  auto w = CreateWindowW(c.lpszClassName,
                         L"Manta LCD4214 — guest display",
                         WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 760, nullptr,
                         nullptr, instance, nullptr);
  if (!w)
    throw std::runtime_error("CreateWindowW failed: " + std::to_string(GetLastError()));
  ShowWindow(w, SW_SHOW);
  std::cout << "NATIVE_WINDOW_CREATED; guest display not initialized\n";
  if (smoke && !SetTimer(w, 1, 400, nullptr)) {
    auto error = GetLastError(); DestroyWindow(w);
    throw std::runtime_error("Window smoke timer failed: " + std::to_string(error));
  }
  MSG msg;
  int result;
  while ((result = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (result == -1) throw std::runtime_error("GetMessageW failed: " + std::to_string(GetLastError()));
}
} // namespace manta
#else
namespace manta {
std::wstring pickFirmware() { return L""; }
void nativeWindow(const std::string &, bool) {
  throw std::runtime_error(
      "Native display backend currently Windows only; use --headless on Linux");
}
} // namespace manta
#endif
