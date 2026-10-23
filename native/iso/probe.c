// Boot probe (PID 1) for the Tiny Core kernel (no fbdev/evdev drivers built in): drives the Bochs/QEMU std VGA (PCI 1234:1111) through its VBE registers
// and a memory-mapped LFB, and reads the PS/2 keyboard controller directly (ports 0x60/0x64). Draws a test pattern and prints scancodes to the console.
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <unistd.h>

int main() {
  mount("proc", "/proc", "proc", 0, 0);
  mount("sysfs", "/sys", "sysfs", 0, 0);
  { int c = open("/dev/console", O_RDWR); if (c < 0) { mount("devtmpfs", "/dev", "devtmpfs", 0, 0); c = open("/dev/console", O_RDWR); } if (c >= 0) { dup2(c, 0); dup2(c, 1); dup2(c, 2); } }
  setvbuf(stdout, 0, _IONBF, 0);
  printf("\n=== MANTA PROBE v2 ===\n");
  printf("iopl %d\n", iopl(3));
  const char* res = "/sys/bus/pci/devices/0000:00:02.0/resource0";
  int fd = open(res, O_RDWR | O_SYNC);
  printf("open %s -> %d\n", res, fd);
  const unsigned W = 1024, H = 768;
  outw(4, 0x1CE); outw(0, 0x1CF);                       // disable
  outw(1, 0x1CE); outw(W, 0x1CF); outw(2, 0x1CE); outw(H, 0x1CF); outw(3, 0x1CE); outw(32, 0x1CF);
  outw(6, 0x1CE); outw(W, 0x1CF); outw(7, 0x1CE); outw(H, 0x1CF);
  outw(4, 0x1CE); outw(0x41, 0x1CF);                    // enabled + LFB
  outw(0, 0x1CE); printf("VBE id %04x\n", inw(0x1CF));
  for (int r = 1; r <= 7; ++r) { outw(r, 0x1CE); printf("vbe[%d]=%u\n", r, inw(0x1CF)); }
  unsigned char* m = fd >= 0 ? mmap(0, W * H * 4, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) : MAP_FAILED;
  printf("mmap %s\n", m == MAP_FAILED ? "FAILED" : "ok");
  if (m != MAP_FAILED) {
    for (unsigned y = 0; y < H; ++y)
      for (unsigned x = 0; x < W; ++x) { unsigned char* p = m + (y * W + x) * 4; p[0] = 128; p[1] = y * 255 / H; p[2] = x * 255 / W; p[3] = 0; }
    printf("test pattern drawn\n");
  }
  while (inb(0x64) & 2) {} outb(0x60, 0x64); while (inb(0x64) & 2) {} outb(0x45, 0x60);   // controller: XT translation, kbd enabled, interrupts off
  while (inb(0x64) & 2) {} outb(0xAE, 0x64);
  printf("press keys (PS/2 scancodes follow)\n");
  for (int i = 0; i < 6000; ++i) {                      // ~60 s
    while (inb(0x64) & 1) printf("scancode %02x\n", inb(0x60));
    usleep(10000);
  }
  return 0;
}
