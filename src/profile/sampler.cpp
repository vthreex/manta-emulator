#include "profile/sampler.hpp"
#include <fstream>
#include <map>
#include <string>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#endif
namespace manta {
#ifdef _WIN32
namespace {
std::map<std::pair<std::string, uint64_t>, uint64_t> g_hist;
uint64_t g_samples = 0, g_failed = 0;
}
void Sampler::start(unsigned intervalMicroseconds) {
  HANDLE self = nullptr;
  DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &self, THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
  target_ = self;
  run_ = true;
  worker_ = std::thread([this, intervalMicroseconds] {
    HANDLE h = static_cast<HANDLE>(target_);
    while (run_) {
      std::this_thread::sleep_for(std::chrono::microseconds(intervalMicroseconds));
      // Nothing may allocate or take a lock while the target is suspended (it could hold the heap lock): capture RIP, resume, then resolve.
      if (SuspendThread(h) == DWORD(-1)) { ++g_failed; continue; }
      CONTEXT c{};
      c.ContextFlags = CONTEXT_CONTROL;
      bool ok = GetThreadContext(h, &c) != 0;
      uint64_t rip = ok ? c.Rip : 0;
      ResumeThread(h);
      MEMORY_BASIC_INFORMATION mbi{};
      std::string module = "anon";
      uint64_t offset = rip;
      if (ok && VirtualQuery(reinterpret_cast<void*>(rip), &mbi, sizeof mbi)) {
        char name[MAX_PATH]{};
        if (mbi.Type == MEM_IMAGE && GetModuleFileNameA(static_cast<HMODULE>(mbi.AllocationBase), name, MAX_PATH)) {
          std::string n = name; module = n.substr(n.find_last_of("\/") + 1);
          offset = rip - reinterpret_cast<uint64_t>(mbi.AllocationBase);
        } else { module = (mbi.Protect & (PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE)) ? "jit" : "anon-data"; offset = 0; }
      }
      if (!ok) { ++g_failed; continue; }
      ++g_hist[{module, offset}];
      ++g_samples;
    }
  });
}
void Sampler::stopAndWrite(const std::filesystem::path& path) {
  run_ = false;
  if (worker_.joinable()) worker_.join();
  if (target_) { CloseHandle(static_cast<HANDLE>(target_)); target_ = nullptr; }
  std::ofstream f(path);
  f << "samples " << g_samples << " failed " << g_failed << "\n";
  for (auto& [key, count] : g_hist) f << key.first << ' ' << std::hex << key.second << std::dec << ' ' << count << "\n";
}
Sampler::~Sampler() { if (run_) { run_ = false; if (worker_.joinable()) worker_.join(); } }
#else
void Sampler::start(unsigned) {}
void Sampler::stopAndWrite(const std::filesystem::path&) {}
Sampler::~Sampler() {}
#endif
}  // namespace manta
