// /init (PID 1) of the Manta ISO: mounts the pseudo file systems, creates the device nodes the presenter needs (framebuffer, evdev keyboards), then runs the
// recompiled-firmware program /manta with the arguments listed in /manta.args (one per line). The firmware runs natively; there is no emulated CPU.
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>

int main() {
  mount("proc", "/proc", "proc", 0, 0);
  mount("sysfs", "/sys", "sysfs", 0, 0);
  mkdir("/dev/input", 0755);
  mknod("/dev/console", S_IFCHR | 0600, makedev(5, 1));
  mknod("/dev/tty0", S_IFCHR | 0600, makedev(4, 0));
  mknod("/dev/fb0", S_IFCHR | 0600, makedev(29, 0));
  mknod("/dev/urandom", S_IFCHR | 0666, makedev(1, 9));   // entropy for TLS
  mknod("/dev/random", S_IFCHR | 0666, makedev(1, 8));
  mknod("/dev/null", S_IFCHR | 0666, makedev(1, 3));
  for (int i = 0; i < 8; ++i) { char p[32]; snprintf(p, sizeof p, "/dev/input/event%d", i); mknod(p, S_IFCHR | 0600, makedev(13, 64 + i)); }
  int c = open("/dev/console", O_RDWR);
  if (c >= 0) { dup2(c, 0); dup2(c, 1); dup2(c, 2); }
  setvbuf(stdout, 0, _IONBF, 0);
  printf("\nMANTA LCD4214 - original firmware, native build\n");
  mkdir("/run", 0755); mkdir("/etc", 0755);
  if (access("/netup", X_OK) == 0 && fork() == 0) {   // network bring-up runs in the background: modules, interface, DHCP -> /run/net.txt
    if (access("/netup.selftest", F_OK) == 0) execl("/netup", "/netup", "--selftest", (char*)0);
    execl("/netup", "/netup", (char*)0);
    _exit(127);
  }
  static char buf[65536]; int n = 0;
  int fd = open("/manta.args", O_RDONLY);
  if (fd >= 0) { n = read(fd, buf, sizeof buf - 1); close(fd); }
  if (n < 0) n = 0;
  buf[n] = 0;
  char* argv[1024]; int argc = 0; argv[argc++] = "/manta";
  for (char* p = strtok(buf, "\n"); p && argc < 1000; p = strtok(0, "\n")) argv[argc++] = p;
  argv[argc] = 0;
  pid_t pid = fork();
  if (pid == 0) { execv("/manta", argv); printf("exec /manta failed\n"); _exit(127); }
  int st = 0;
  for (int t = 0;; ++t) {   // heartbeat: child state, user/system CPU ticks, resident pages (visible on the console / serial line)
    if (waitpid(pid, &st, WNOHANG) == pid) break;
    if (t % 15 == 0) {
      char path[64], line[512]; snprintf(path, sizeof path, "/proc/%d/stat", pid);
      int f = open(path, O_RDONLY); int k = f >= 0 ? read(f, line, sizeof line - 1) : 0; if (f >= 0) close(f); line[k > 0 ? k : 0] = 0;
      char state = '?'; unsigned long ut = 0, stt = 0, rss = 0; char* r = strrchr(line, ')');
      if (r) { sscanf(r + 2, "%c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu %*d %*d %*d %*d %*d %*d %*u %*u %ld", &state, &ut, &stt, (long*)&rss); }
      printf("[heartbeat %ds] manta state %c user %lu sys %lu ticks rss %lu pages\n", t, state, ut, stt, rss);
    }
    sleep(1);
  }
  printf("manta exited, status %d\n", st);
  for (;;) sleep(3600);
}
