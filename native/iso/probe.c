// Boot probe (PID 1) for the Tiny Core kernel: creates device nodes by hand (no devtmpfs), uses the kernel framebuffer (/dev/fb0, vesafb via vga=0x318)
// and raw keyboard scancodes from the virtual console (KDSKBMODE K_RAW). Draws a test pattern and prints scancodes.
#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

int main() {
  mount("proc", "/proc", "proc", 0, 0);
  mount("sysfs", "/sys", "sysfs", 0, 0);
  mknod("/dev/console", S_IFCHR | 0600, makedev(5, 1));
  mknod("/dev/tty0", S_IFCHR | 0600, makedev(4, 0));
  mknod("/dev/tty1", S_IFCHR | 0600, makedev(4, 1));
  mknod("/dev/fb0", S_IFCHR | 0600, makedev(29, 0));
  { int c = open("/dev/console", O_RDWR); if (c >= 0) { dup2(c, 0); dup2(c, 1); dup2(c, 2); } }
  setvbuf(stdout, 0, _IONBF, 0);
  printf("\n=== MANTA PROBE v3 ===\n");
  int fb = open("/dev/fb0", O_RDWR);
  if (fb < 0) printf("no /dev/fb0\n");
  else {
    struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
    ioctl(fb, FBIOGET_VSCREENINFO, &v); ioctl(fb, FBIOGET_FSCREENINFO, &f);
    printf("fb0 %ux%u bpp %u line %u mem %u id %s\n", v.xres, v.yres, v.bits_per_pixel, f.line_length, f.smem_len, f.id);
    unsigned char* m = mmap(0, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
    if (m != MAP_FAILED && v.bits_per_pixel == 32)
      for (unsigned y = 0; y < v.yres; ++y) for (unsigned x = 0; x < v.xres; ++x) {
        unsigned char* p = m + y * f.line_length + x * 4; p[0] = 128; p[1] = y * 255 / v.yres; p[2] = x * 255 / v.xres; p[3] = 0; }
    printf("pattern %s\n", m == MAP_FAILED ? "FAILED" : "drawn");
  }
  { char b[2048]; const char* fs[] = {"/proc/bus/input/devices", "/proc/bus/input/handlers", "/proc/misc", 0}; for (int i = 0; fs[i]; ++i) { int d = open(fs[i], O_RDONLY); int n = d >= 0 ? read(d, b, sizeof b - 1) : -1; if (n > 0) { b[n] = 0; printf("--- %s%c%s", fs[i], 10, b); } else printf("--- %s unreadable%c", fs[i], 10); } }
  int tty = open("/dev/tty1", O_RDWR | O_NONBLOCK);
  printf("tty0 %d, K_RAW %d\n", tty, tty >= 0 ? ioctl(tty, KDSKBMODE, K_RAW) : -1);
  for (int i = 0; i < 6000; ++i) {
    unsigned char b[16]; int n = tty >= 0 ? read(tty, b, sizeof b) : 0;
    for (int k = 0; k < n; ++k) printf("scancode %02x\n", b[k]);
    usleep(10000);
  }
  return 0;
}
