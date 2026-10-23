// Linux presenter for the guest display: scales the published GOP preview frame (guest pixels, neutral checker where the guest provided none) into
// the kernel framebuffer (/dev/fb0) and turns evdev key presses into virtual-remote function names. Same contract as src/ui/native_window.cpp.
#include "ui/native_window.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/fb.h>
#include <linux/input.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace manta {
std::wstring pickFirmware() { return L""; }

namespace {
struct Fb {
  int fd = -1;
  uint8_t* mem = nullptr;
  unsigned w = 0, h = 0, bpp = 0, line = 0, ro = 16, go = 8, bo = 0;
  size_t size = 0;
  bool open_() {
    fd = ::open("/dev/fb0", O_RDWR);
    if (fd < 0) return false;
    fb_var_screeninfo v; fb_fix_screeninfo f;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &v) || ioctl(fd, FBIOGET_FSCREENINFO, &f)) return false;
    w = v.xres; h = v.yres; bpp = v.bits_per_pixel; line = f.line_length; ro = v.red.offset; go = v.green.offset; bo = v.blue.offset; size = f.smem_len;
    if (bpp != 24 && bpp != 32) return false;
    mem = static_cast<uint8_t*>(mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    return mem != MAP_FAILED;
  }
  void put(unsigned x, unsigned y, uint32_t rgb) {
    uint32_t px = ((rgb >> 16 & 255) << ro) | ((rgb >> 8 & 255) << go) | ((rgb & 255) << bo);
    uint8_t* p = mem + size_t(y) * line + size_t(x) * (bpp / 8);
    if (bpp == 32) std::memcpy(p, &px, 4); else { p[0] = uint8_t(px); p[1] = uint8_t(px >> 8); p[2] = uint8_t(px >> 16); }
  }
};
const char* functionFor(unsigned code) {
  switch (code) {
    case KEY_M: return "MENU";  case KEY_S: return "SOURCE";  case KEY_ESC: return "EXIT";  case KEY_ENTER: case KEY_KPENTER: return "OK";
    case KEY_UP: return "UP";  case KEY_DOWN: return "DOWN";  case KEY_LEFT: return "LEFT";  case KEY_RIGHT: return "RIGHT";
    case KEY_P: return "POWER";  case KEY_I: return "INFO";
    case KEY_F1: return "SOURCE_AV1";  case KEY_F2: return "SOURCE_SVIDEO";  case KEY_F3: return "SOURCE_YPBPR";  case KEY_F4: return "SOURCE_VGA";  case KEY_F5: return "SOURCE_HDMI1";
    default: return nullptr;
  }
}
// Character typed by a PC key while the on-screen keyboard is open (US layout); 0 = none.
char charFor(unsigned code, bool shift) {
  static const struct { unsigned code; char lo, up; } map[] = {
    {16,'q','Q'},{17,'w','W'},{18,'e','E'},{19,'r','R'},{20,'t','T'},{21,'y','Y'},{22,'u','U'},{23,'i','I'},{24,'o','O'},{25,'p','P'},
    {30,'a','A'},{31,'s','S'},{32,'d','D'},{33,'f','F'},{34,'g','G'},{35,'h','H'},{36,'j','J'},{37,'k','K'},{38,'l','L'},
    {44,'z','Z'},{45,'x','X'},{46,'c','C'},{47,'v','V'},{48,'b','B'},{49,'n','N'},{50,'m','M'},
    {2,'1','!'},{3,'2','@'},{4,'3','#'},{5,'4','$'},{6,'5','%'},{7,'6','^'},{8,'7','&'},{9,'8','*'},{10,'9','('},{11,'0',')'},
    {12,'-','_'},{13,'=','+'},{39,';',':'},{40,'\'','"'},{51,',','<'},{52,'.','>'},{53,'/','?'},{57,' ',' '}};
  for (const auto& m : map) if (m.code == code) return shift ? m.up : m.lo;
  return 0;
}
int digitFor(unsigned code) {
  static const int top[] = {11, 2, 3, 4, 5, 6, 7, 8, 9, 10};                    // KEY_0 .. KEY_9
  static const int pad[] = {82, 79, 80, 81, 75, 76, 77, 71, 72, 73};            // KEY_KP0 .. KEY_KP9
  for (int d = 0; d < 10; ++d) if (int(code) == top[d] || int(code) == pad[d]) return d;
  return -1;
}
}  // namespace

void nativeWindow(const std::string&, bool smoke, std::shared_ptr<GopPreviewStore> store, const PreviewWindowOptions&) {
  if (smoke) return;
  Fb fb;
  if (!fb.open_()) { std::cerr << "no usable /dev/fb0 (need 24/32 bpp)\n"; return; }
  std::cout << "NATIVE_WINDOW_CREATED; Linux framebuffer " << fb.w << "x" << fb.h << "x" << fb.bpp << "\n";
  std::vector<int> keys;
  for (int i = 0; i < 8; ++i) {
    std::string p = "/dev/input/event" + std::to_string(i);
    int fd = ::open(p.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd >= 0) keys.push_back(fd);
  }
  for (unsigned y = 0; y < fb.h; ++y) for (unsigned x = 0; x < fb.w; ++x) fb.put(x, y, 0x141414);
  uint64_t shown = 0;
  bool shiftDown = false;
  for (;;) {
    for (int fd : keys) {
      input_event ev;
      while (read(fd, &ev, sizeof ev) == sizeof ev)
        if (ev.type == EV_KEY && (ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT)) shiftDown = ev.value != 0;
        else if (ev.type == EV_KEY && ev.value == 1 && (ev.code == KEY_K || ev.code == KEY_N || ev.code == KEY_B) && store && !store->remote->oskActive.load()) {   // K: on-screen keyboard, N: network page, B: browser
          const char* fn = ev.code == KEY_K ? "OSK" : ev.code == KEY_N ? "NET" : "BROWSER";
          store->remote->push(fn); std::cout << "[key " << fn << "]" << std::endl;
        } else if (ev.type == EV_KEY && ev.value >= 1 && store && store->remote->oskActive.load()) {
          // keyboard open: navigation keys and characters go to it (key repeat only for the keys where it is useful)
          std::string fn;
          switch (ev.code) {
            case KEY_UP: fn = "UP"; break;  case KEY_DOWN: fn = "DOWN"; break;  case KEY_LEFT: fn = "LEFT"; break;  case KEY_RIGHT: fn = "RIGHT"; break;
            case KEY_BACKSPACE: fn = "OSK_BACKSPACE"; break;
            case KEY_PAGEUP: fn = "PAGEUP"; break;  case KEY_PAGEDOWN: fn = "PAGEDOWN"; break;
            case KEY_ENTER: if (ev.value == 1) fn = "OK"; break;
            case KEY_KPENTER: case KEY_F10: if (ev.value == 1) fn = "OSK_ENTER"; break;   // accept the entered text
            case KEY_ESC: if (ev.value == 1) fn = "EXIT"; break;
            case KEY_TAB: if (ev.value == 1) fn = "MENU"; break;   // toggles shift on the keyboard
            default: if (ev.value == 1) if (char ch = charFor(ev.code, shiftDown)) fn = std::string("OSK_CHAR:") + ch;
          }
          if (!fn.empty()) { store->remote->push(fn); std::cout << "[osk " << fn << "]" << std::endl; }
        } else if (ev.type == EV_KEY && ev.value == 1 && store) {
          if (const char* fn = functionFor(ev.code)) { store->remote->push(fn); std::cout << "[key " << fn << "]" << std::endl; }
          else if (int d = digitFor(ev.code); d >= 0) { char name[] = "DIGIT_0"; name[6] = char('0' + d); store->remote->push(name); std::cout << "[key " << name << "]" << std::endl; }
        }
    }
    auto latest = store ? store->latest() : nullptr;
    if (latest && !latest->empty() && latest->sequence != shown) {
      shown = latest->sequence;
      std::cout << "[frame " << shown << " " << latest->width << "x" << latest->height << "]\n" << std::flush;
      const auto& f = *latest;
      unsigned scale = std::max(1u, std::min(fb.w / f.width, fb.h / f.height));
      unsigned dw = f.width * scale, dh = f.height * scale, ox = (fb.w - std::min(dw, fb.w)) / 2, oy = (fb.h - std::min(dh, fb.h)) / 2;
      if (dw > fb.w || dh > fb.h) {   // frame larger than the screen: aspect-preserving nearest-neighbour fit, letterboxed
        double sc = std::min(double(fb.w) / f.width, double(fb.h) / f.height);
        unsigned fw = unsigned(f.width * sc), fh = unsigned(f.height * sc), lx = (fb.w - fw) / 2, ly = (fb.h - fh) / 2;
        for (unsigned y = 0; y < fb.h; ++y) for (unsigned x = 0; x < fb.w; ++x) {
          if (x < lx || x >= lx + fw || y < ly || y >= ly + fh) { fb.put(x, y, 0x141414); continue; }
          unsigned sx = (x - lx) * f.width / fw, sy = (y - ly) * f.height / fh; size_t i = size_t(sy) * f.width + sx;
          fb.put(x, y, f.covered[i] ? f.bgra[i] & 0xffffffu : f.planeActive ? f.planeRgb : ((((sx >> 4) ^ (sy >> 4)) & 1) ? 0x505050u : 0x383838u));
        }
      } else {
        for (unsigned y = 0; y < dh; ++y) for (unsigned x = 0; x < dw; ++x) {
          unsigned sx = x / scale, sy = y / scale; size_t i = size_t(sy) * f.width + sx;
          fb.put(ox + x, oy + y, f.covered[i] ? f.bgra[i] & 0xffffffu : f.planeActive ? f.planeRgb : ((((sx >> 4) ^ (sy >> 4)) & 1) ? 0x505050u : 0x383838u));
        }
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}
}  // namespace manta
