// Network bring-up for the ISO (runs in a background child of /init so that boot is not delayed):
//   1. load the NIC driver modules listed in /modules.list (decompressed .ko files in /lib/modules/),
//   2. wait for a physical interface to appear (PCI NICs at once, USB ones after enumeration),
//   3. bring it up and get an address with a small DHCP client (address, mask, router, DNS),
//   4. write /etc/resolv.conf and the status file /run/net.txt (key=value lines) that the firmware-side network feature reads.
// Nothing here depends on the firmware; the status file is the only interface.
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include "../runtime/net_ping.h"

static char g_status[1024];
static void writeStatus() {
  mkdir("/run", 0755);
  int fd = open("/run/net.txt.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) return;
  write(fd, g_status, strlen(g_status));
  close(fd);
  rename("/run/net.txt.tmp", "/run/net.txt");
}
static void setStatus(const char* s) { snprintf(g_status, sizeof g_status, "%s", s); writeStatus(); }

static void loadModules() {
  FILE* l = fopen("/modules.list", "r");
  if (!l) return;
  char name[128];
  int ok = 0, bad = 0;
  while (fgets(name, sizeof name, l)) {
    name[strcspn(name, "\r\n")] = 0;
    if (!name[0]) continue;
    char path[192]; snprintf(path, sizeof path, "/lib/modules/%s.ko", name);
    int fd = open(path, O_RDONLY);
    if (fd < 0) continue;
    struct stat st; fstat(fd, &st);
    void* buf = malloc(st.st_size);
    if (buf && read(fd, buf, st.st_size) == st.st_size) {
      if (syscall(SYS_init_module, buf, (unsigned long)st.st_size, "") == 0 || errno == EEXIST) ++ok; else ++bad;
    }
    free(buf); close(fd);
  }
  fclose(l);
  printf("net: modules loaded %d, failed %d\n", ok, bad);
}

static int physicalInterface(char* out, size_t n) {
  DIR* d = opendir("/sys/class/net");
  if (!d) return 0;
  struct dirent* e;
  int found = 0;
  while (!found && (e = readdir(d))) {
    if (e->d_name[0] == '.') continue;
    char p[300]; snprintf(p, sizeof p, "/sys/class/net/%s/device", e->d_name);
    struct stat st;
    if (stat(p, &st) == 0) { snprintf(out, n, "%s", e->d_name); found = 1; }   // only real devices have a "device" link (lo, dummy0, tunl0 do not)
  }
  closedir(d);
  return found;
}

struct Lease { uint32_t ip, mask, router, server, dns[3]; int ndns; uint32_t lease; };

static int dhcpExchange(int s, const uint8_t* mac, uint32_t xid, int type, const struct Lease* want, struct Lease* got, int* gotType) {
  uint8_t pkt[548]; memset(pkt, 0, sizeof pkt);
  pkt[0] = 1; pkt[1] = 1; pkt[2] = 6;
  uint32_t x = htonl(xid); memcpy(pkt + 4, &x, 4);
  pkt[10] = 0x80;   // broadcast flag: the server answers to 255.255.255.255 so a socket without an address can receive it
  memcpy(pkt + 28, mac, 6);
  uint8_t* o = pkt + 236;
  *o++ = 99; *o++ = 130; *o++ = 83; *o++ = 99;
  *o++ = 53; *o++ = 1; *o++ = (uint8_t)type;
  *o++ = 61; *o++ = 7; *o++ = 1; memcpy(o, mac, 6); o += 6;
  if (type == 3) {
    *o++ = 50; *o++ = 4; memcpy(o, &want->ip, 4); o += 4;
    *o++ = 54; *o++ = 4; memcpy(o, &want->server, 4); o += 4;
  }
  *o++ = 55; *o++ = 4; *o++ = 1; *o++ = 3; *o++ = 6; *o++ = 15;
  *o++ = 255;
  struct sockaddr_in to; memset(&to, 0, sizeof to);
  to.sin_family = AF_INET; to.sin_port = htons(67); to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
  for (int attempt = 0; attempt < 4; ++attempt) {
    sendto(s, pkt, o - pkt, 0, (struct sockaddr*)&to, sizeof to);
    time_t end = time(0) + 3;
    while (time(0) < end) {
      struct pollfd p = {s, POLLIN, 0};
      if (poll(&p, 1, 500) <= 0) continue;
      uint8_t rx[1500];
      int n = recv(s, rx, sizeof rx, 0);
      if (n < 240 || rx[0] != 2 || memcmp(rx + 4, &x, 4) != 0 || memcmp(rx + 28, mac, 6) != 0) continue;
      memset(got, 0, sizeof *got);
      memcpy(&got->ip, rx + 16, 4);
      *gotType = 0;
      for (int i = 240; i + 1 < n && rx[i] != 255;) {
        if (rx[i] == 0) { ++i; continue; }
        int code = rx[i], len = rx[i + 1];
        if (i + 2 + len > n) break;
        const uint8_t* v = rx + i + 2;
        if (code == 53 && len == 1) *gotType = v[0];
        else if (code == 1 && len == 4) memcpy(&got->mask, v, 4);
        else if (code == 3 && len >= 4) memcpy(&got->router, v, 4);
        else if (code == 6) for (int k = 0; k + 4 <= len && got->ndns < 3; k += 4) memcpy(&got->dns[got->ndns++], v + k, 4);
        else if (code == 54 && len == 4) memcpy(&got->server, v, 4);
        else if (code == 51 && len == 4) { uint32_t t; memcpy(&t, v, 4); got->lease = ntohl(t); }
        i += 2 + len;
      }
      return 1;
    }
  }
  return 0;
}

static void applyLease(int s, const char* ifname, const struct Lease* l) {
  struct ifreq ifr; memset(&ifr, 0, sizeof ifr);
  snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
  struct sockaddr_in* a = (struct sockaddr_in*)&ifr.ifr_addr;
  a->sin_family = AF_INET; a->sin_addr.s_addr = l->ip; ioctl(s, SIOCSIFADDR, &ifr);
  a->sin_addr.s_addr = l->mask ? l->mask : htonl(0xffffff00u); ioctl(s, SIOCSIFNETMASK, &ifr);
  if (l->router) {
    struct rtentry rt; memset(&rt, 0, sizeof rt);
    struct sockaddr_in* d = (struct sockaddr_in*)&rt.rt_dst; struct sockaddr_in* g = (struct sockaddr_in*)&rt.rt_gateway; struct sockaddr_in* m = (struct sockaddr_in*)&rt.rt_genmask;
    d->sin_family = g->sin_family = m->sin_family = AF_INET; g->sin_addr.s_addr = l->router;
    rt.rt_flags = RTF_UP | RTF_GATEWAY; rt.rt_dev = (char*)ifname;
    ioctl(s, SIOCADDRT, &rt);
  }
  mkdir("/etc", 0755);
  FILE* f = fopen("/etc/resolv.conf", "w");
  if (f) {
    for (int i = 0; i < l->ndns; ++i) { char b[32]; inet_ntop(AF_INET, &l->dns[i], b, sizeof b); fprintf(f, "nameserver %s\n", b); }
    fclose(f);
  }
}

int main(int argc, char** argv) {
  int selftest = argc > 1 && strcmp(argv[1], "--selftest") == 0;
  setvbuf(stdout, 0, _IONBF, 0);
  setStatus("state=starting\n");
  loadModules();
  char ifname[IFNAMSIZ] = "";
  for (int i = 0; i < 40 && !physicalInterface(ifname, sizeof ifname); ++i) usleep(250000);
  if (!ifname[0]) { setStatus("state=no-interface\n"); printf("net: no network interface found\n"); return 1; }
  printf("net: interface %s\n", ifname);
  int s = socket(AF_INET, SOCK_DGRAM, 0);
  struct ifreq ifr; memset(&ifr, 0, sizeof ifr); snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
  if (ioctl(s, SIOCGIFFLAGS, &ifr) == 0) { ifr.ifr_flags |= IFF_UP | IFF_BROADCAST; ioctl(s, SIOCSIFFLAGS, &ifr); }
  uint8_t mac[6] = {0};
  if (ioctl(s, SIOCGIFHWADDR, &ifr) == 0) memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
  char st[256]; snprintf(st, sizeof st, "state=link-wait\niface=%s\nmac=%02x:%02x:%02x:%02x:%02x:%02x\n", ifname, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  setStatus(st);
  // the link may need a moment (carrier); DHCP retries cover it
  int d = socket(AF_INET, SOCK_DGRAM, 0);
  int one = 1;
  setsockopt(d, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
  setsockopt(d, SOL_SOCKET, SO_BINDTODEVICE, ifname, (socklen_t)strlen(ifname) + 1);
  struct sockaddr_in me; memset(&me, 0, sizeof me); me.sin_family = AF_INET; me.sin_port = htons(68);
  if (bind(d, (struct sockaddr*)&me, sizeof me) != 0) { setStatus("state=dhcp-bind-failed\n"); return 1; }
  uint32_t xid = (uint32_t)time(0) ^ ((uint32_t)mac[5] << 8) ^ (uint32_t)getpid();
  struct Lease offer, ack; int t = 0;
  for (int round = 0; round < 3; ++round) {
    if (!dhcpExchange(d, mac, xid, 1, 0, &offer, &t) || t != 2) { printf("net: no DHCP offer (round %d)\n", round); continue; }
    if (!dhcpExchange(d, mac, xid, 3, &offer, &ack, &t) || t != 5) { printf("net: no DHCP ack\n"); continue; }
    ack.server = offer.server ? offer.server : ack.server;
    applyLease(s, ifname, &ack);
    char ip[32], gw[32], mk[32], dns[96] = "";
    inet_ntop(AF_INET, &ack.ip, ip, sizeof ip); inet_ntop(AF_INET, &ack.router, gw, sizeof gw); inet_ntop(AF_INET, &ack.mask, mk, sizeof mk);
    for (int i = 0; i < ack.ndns; ++i) { char b[32]; inet_ntop(AF_INET, &ack.dns[i], b, sizeof b); strcat(dns, i ? "," : ""); strcat(dns, b); }
    snprintf(g_status, sizeof g_status, "state=up\niface=%s\nmac=%02x:%02x:%02x:%02x:%02x:%02x\nip=%s\nmask=%s\ngateway=%s\ndns=%s\nlease=%u\n", ifname, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], ip, mk, gw, dns, ack.lease);
    writeStatus();
    printf("net: %s up, ip %s gateway %s dns %s\n", ifname, ip, gw, dns);
    if (selftest) {
      struct ping_result pr; struct in_addr g = {ack.router};
      for (int i = 1; i <= 3; ++i) { ping_once(g, 0x4d41, i, 1500, &pr); printf("net: ping %s seq %d: %s %.1f ms ttl %d\n", pr.address, i, pr.ok ? "reply" : "timeout", pr.ms, pr.ttl); }
      struct in_addr h; int rr = ping_resolve("example.com", &h);
      printf("net: resolve example.com: %s\n", rr == 0 ? inet_ntoa(h) : "failed");
      if (rr == 0) { ping_once(h, 0x4d41, 9, 3000, &pr); printf("net: ping example.com (%s): %s %.1f ms\n", pr.address, pr.ok ? "reply" : "timeout", pr.ms); }
    }
    return 0;
  }
  snprintf(st, sizeof st, "state=no-dhcp\niface=%s\nmac=%02x:%02x:%02x:%02x:%02x:%02x\n", ifname, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  setStatus(st);
  return 1;
}
