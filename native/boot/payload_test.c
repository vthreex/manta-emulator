// Milestone-1 payload (freestanding x86_64 at 1 MB): draws a gradient into the VESA framebuffer and shows PS/2 scancodes as bars.
typedef unsigned int u32; typedef unsigned char u8; typedef unsigned long long u64;
struct Info { u32 fb, pitch, w, h, bpp, ok; };
static inline u8 inb(unsigned short p) { u8 v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
void _start(void) {
  volatile struct Info* i = (volatile struct Info*)0x500;
  if (!i->ok) for (;;) __asm__ volatile("hlt");
  volatile u32* fb = (volatile u32*)(u64)i->fb;
  u32 pitch = i->pitch / 4;
  for (u32 y = 0; y < i->h; ++y) for (u32 x = 0; x < i->w; ++x) fb[y * pitch + x] = ((x * 255 / i->w) << 16) | ((y * 255 / i->h) << 8) | 0x80;
  u32 n = 0;
  for (;;) if (inb(0x64) & 1) {
    u8 sc = inb(0x60);
    for (u32 y = 0; y < 16; ++y) for (u32 x = 0; x < 8; ++x) fb[(100 + y) * pitch + 20 + n * 10 + x] = (sc & 0x80) ? 0x00FF0000 : 0x00FFFFFF;
    for (u32 b = 0; b < 8; ++b) for (u32 y = 0; y < 4; ++y) for (u32 x = 0; x < 8; ++x) fb[(130 + y + (b * 6)) * pitch + 20 + n * 10 + x] = (sc >> b & 1) ? 0 : 0x00FFFF00;
    n = (n + 1) % 90;
  }
}
