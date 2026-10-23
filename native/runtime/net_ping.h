// ICMP echo ("ping") over a raw socket (needs root, which the ISO runs as). Plain C so that both the boot-time network helper and the firmware-side
// network feature use the same code. Linux only.
#pragma once
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

struct ping_result {
  int ok;                 // 1 = reply received
  int error;              // errno-like or negative resolver code when !ok and no reply (0 = timeout)
  int ttl;                // TTL of the reply
  int bytes;              // ICMP bytes in the reply
  double ms;              // round trip time
  char address[48];       // resolved address (dotted quad)
};

static inline uint16_t ping_checksum(const void* p, int n) {
  uint32_t s = 0;
  const uint16_t* w = (const uint16_t*)p;
  for (; n > 1; n -= 2) s += *w++;
  if (n) s += *(const uint8_t*)w;
  s = (s >> 16) + (s & 0xffff);
  s += s >> 16;
  return (uint16_t)~s;
}

static inline double ping_now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

// Resolves `host` (name or dotted quad) to an IPv4 address; returns 0 on success.
static inline int ping_resolve(const char* host, struct in_addr* out) {
  if (inet_pton(AF_INET, host, out) == 1) return 0;
  struct addrinfo hints, *res = 0;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  int rc = getaddrinfo(host, 0, &hints, &res);
  if (rc != 0 || !res) return rc ? rc : -1;
  *out = ((struct sockaddr_in*)res->ai_addr)->sin_addr;
  freeaddrinfo(res);
  return 0;
}

// One echo request to `addr`; sequence number `seq`, identifier `id`. Fills `r` and returns r->ok.
static inline int ping_once(struct in_addr addr, int id, int seq, int timeout_ms, struct ping_result* r) {
  memset(r, 0, sizeof *r);
  inet_ntop(AF_INET, &addr, r->address, sizeof r->address);
  int s = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (s < 0) { r->error = errno; return 0; }
  uint8_t pkt[64];
  memset(pkt, 0, sizeof pkt);
  struct icmphdr* h = (struct icmphdr*)pkt;
  h->type = ICMP_ECHO; h->code = 0; h->un.echo.id = htons((uint16_t)id); h->un.echo.sequence = htons((uint16_t)seq);
  for (int i = sizeof *h; i < (int)sizeof pkt; ++i) pkt[i] = (uint8_t)i;
  h->checksum = ping_checksum(pkt, sizeof pkt);
  struct sockaddr_in to; memset(&to, 0, sizeof to);
  to.sin_family = AF_INET; to.sin_addr = addr;
  double t0 = ping_now_ms();
  if (sendto(s, pkt, sizeof pkt, 0, (struct sockaddr*)&to, sizeof to) < 0) { r->error = errno; close(s); return 0; }
  for (;;) {
    double left = timeout_ms - (ping_now_ms() - t0);
    if (left <= 0) break;
    struct pollfd p = {s, POLLIN, 0};
    if (poll(&p, 1, (int)left) <= 0) break;
    uint8_t rx[512];
    struct sockaddr_in from; socklen_t fl = sizeof from;
    int n = (int)recvfrom(s, rx, sizeof rx, 0, (struct sockaddr*)&from, &fl);
    if (n < (int)(sizeof(struct iphdr) + sizeof(struct icmphdr))) continue;
    struct iphdr* ip = (struct iphdr*)rx;
    int hl = ip->ihl * 4;
    if (n < hl + (int)sizeof(struct icmphdr)) continue;
    struct icmphdr* ic = (struct icmphdr*)(rx + hl);
    if (ic->type == ICMP_ECHOREPLY && ntohs(ic->un.echo.id) == (uint16_t)id && ntohs(ic->un.echo.sequence) == (uint16_t)seq && from.sin_addr.s_addr == addr.s_addr) {
      r->ok = 1; r->ttl = ip->ttl; r->bytes = n - hl; r->ms = ping_now_ms() - t0;
      close(s);
      return 1;
    }
  }
  close(s);
  return 0;   // timeout
}
