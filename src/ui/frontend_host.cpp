#include "ui/frontend_host.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
namespace manta {
namespace {
constexpr uint32_t kMagic = 0x4154'4e4d;      // "MNTA"
constexpr size_t kHeader = 1024;
constexpr size_t kMaxPixels = 1920 * 1080;
struct ShmHeader {
  uint32_t magic, version;
  volatile uint32_t seqBegin;                  // writer: seqBegin = n, payload, seqEnd = n; a reader accepts a frame when both are equal
  volatile uint32_t seqEnd;
  uint32_t width, height;
  volatile uint32_t flags;                     // bit0 guest run finished, bit1 on-screen keyboard / browser page open
  uint32_t reserved;
  uint64_t checksum;
  char label[256];
  char description[480];
  char status[64];                             // e.g. "MIPS dispatches 1234567" (offset 776)
};
static_assert(sizeof(ShmHeader) <= kHeader);
}  // namespace

void frontendHost(const std::string& shmName, std::shared_ptr<GopPreviewStore> store) {
  HANDLE map = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, DWORD(kHeader + kMaxPixels * 4), shmName.c_str());
  if (!map) throw std::runtime_error("CreateFileMapping failed: " + std::to_string(GetLastError()));
  auto* base = static_cast<uint8_t*>(MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 0));
  if (!base) throw std::runtime_error("MapViewOfFile failed");
  auto* hdr = reinterpret_cast<ShmHeader*>(base);
  std::memset(hdr, 0, kHeader);
  hdr->magic = kMagic; hdr->version = 1;
  std::atomic<bool> quit{false};
  std::thread reader([&] {
    std::string line;
    while (std::getline(std::cin, line)) {
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
      std::istringstream in(line);
      std::string cmd, arg;
      in >> cmd >> arg;
      if (cmd == "KEY" && !arg.empty()) store->remote->push(arg);
      else if (cmd == "RAW" && !arg.empty()) store->remote->push("raw=" + arg);
      else if (cmd == "PAUSE") store->remote->paused = true;
      else if (cmd == "RESUME") store->remote->paused = false;
      else if (cmd == "QUIT") break;
    }
    quit = true;
  });
  uint64_t shown = 0;
  uint32_t seq = 0;
  std::vector<uint32_t> out;
  std::cout << "FRONTEND_READY shm=" << shmName << std::endl;
  while (!quit) {
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    uint32_t flags = (store->finished() ? 1u : 0u) | (store->remote->oskActive ? 2u : 0u);
    auto f = store->latest();
    if (f && f->sequence != shown && !f->empty() && size_t(f->width) * f->height <= kMaxPixels) {
      shown = f->sequence;
      out.resize(f->bgra.size());
      for (uint32_t y = 0; y < f->height; ++y)
        for (uint32_t x = 0; x < f->width; ++x) {
          size_t i = size_t(y) * f->width + x;
          out[i] = f->covered[i] ? (f->bgra[i] | 0xff000000u) : !f->planeBgra.empty() ? (f->planeBgra[i] & 0x00ffffffu) : f->planeActive ? (f->planeRgb & 0x00ffffffu) : 0u;   // alpha 0xFF = guest pixel, 0 = backdrop (video plane colour/picture)
        }
      hdr->seqBegin = ++seq;
      MemoryBarrier();
      hdr->width = f->width; hdr->height = f->height; hdr->checksum = f->checksum;
      std::snprintf(hdr->label, sizeof hdr->label, "%s", f->label.c_str());
      std::snprintf(hdr->description, sizeof hdr->description, "%s", f->description.c_str());
      std::memcpy(base + kHeader, out.data(), out.size() * 4);
      MemoryBarrier();
      hdr->seqEnd = seq;
    }
    { auto st = store->status(); std::snprintf(hdr->status, sizeof hdr->status, "%s", st.c_str()); }
    hdr->flags = flags | (store->remote->paused ? 4u : 0u);
    if ((flags & 1) && quit) break;
  }
  // the guest thread may still be running: leave the process through the normal path once the owner asked to stop
  UnmapViewOfFile(base);
  CloseHandle(map);
  if (reader.joinable()) reader.join();
}
} // namespace manta
#else
namespace manta {
void frontendHost(const std::string&, std::shared_ptr<GopPreviewStore>) { throw std::runtime_error("--frontend-shm is Windows only"); }
}
#endif
