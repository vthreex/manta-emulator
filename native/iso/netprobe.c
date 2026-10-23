// Boot probe (PID 1): what networking does the kernel on the ISO offer? Lists /sys/class/net, /proc/modules, /proc/net/dev, tries to bring up the first interface
// with a static address and sends one ICMP echo through a raw socket.
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>

static void cat(const char* path) {
  char b[4096];
  int fd = open(path, O_RDONLY);
  int n = fd >= 0 ? read(fd, b, sizeof b - 1) : -1;
  if (n > 0) { b[n] = 0; printf("--- %s\n%s", path, b); } else printf("--- %s unreadable\n", path);
  if (fd >= 0) close(fd);
}
static unsigned short csum(void* p, int n) {
  unsigned long s = 0; unsigned short* w = p;
  for (; n > 1; n -= 2) s += *w++;
  if (n) s += *(unsigned char*)w;
  s = (s >> 16) + (s & 0xffff); s += s >> 16;
  return (unsigned short)~s;
}

int main() {
  mount("proc", "/proc", "proc", 0, 0);
  mount("sysfs", "/sys", "sysfs", 0, 0);
  mknod("/dev/console", S_IFCHR | 0600, makedev(5, 1));
  int c = open("/dev/console", O_RDWR);
  if (c >= 0) { dup2(c, 0); dup2(c, 1); dup2(c, 2); }
  setvbuf(stdout, 0, _IONBF, 0);
  printf("\n=== NET PROBE ===\n");
  cat("/proc/version");
  cat("/proc/modules");
  cat("/proc/net/dev");
  cat("/proc/net/protocols");
  DIR* d = opendir("/sys/class/net");
  if (d) { struct dirent* e; while ((e = readdir(d))) if (e->d_name[0] != '.') printf("net iface: %s\n", e->d_name); closedir(d); }
  else printf("no /sys/class/net\n");
  d = opendir("/sys/bus/pci/drivers");
  if (d) { struct dirent* e; while ((e = readdir(d))) if (e->d_name[0] != '.') printf("pci driver: %s\n", e->d_name); closedir(d); }
  int s = socket(AF_INET, SOCK_DGRAM, 0);
  printf("inet dgram socket: %d\n", s);
  if (s >= 0) {
    struct ifreq ifr; memset(&ifr, 0, sizeof ifr);
    strcpy(ifr.ifr_name, "eth0");
    if (ioctl(s, SIOCGIFFLAGS, &ifr) == 0) {
      ifr.ifr_flags |= IFF_UP | IFF_RUNNING; ioctl(s, SIOCSIFFLAGS, &ifr);
      struct sockaddr_in* a = (struct sockaddr_in*)&ifr.ifr_addr; a->sin_family = AF_INET; inet_pton(AF_INET, "10.0.2.15", &a->sin_addr);
      printf("set addr %d\n", ioctl(s, SIOCSIFADDR, &ifr));
      inet_pton(AF_INET, "255.255.255.0", &a->sin_addr); printf("set mask %d\n", ioctl(s, SIOCSIFNETMASK, &ifr));
      printf("eth0 up\n");
    } else printf("no eth0\n");
  }
  int r = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  printf("raw icmp socket: %d\n", r);
  int p = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
  printf("dgram icmp socket: %d\n", p);
  printf("=== probe done ===\n");
  for (;;) sleep(60);
}
