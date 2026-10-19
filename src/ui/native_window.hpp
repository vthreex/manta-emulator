#pragma once
#include "video/gop_preview.hpp"
#include <memory>
#include <string>
namespace manta {
struct PreviewWindowOptions {
  std::string capturePath;       // when set: save the window's client area (BMP) once the guest run has finished
  unsigned closeAfterFinishMs = 0; // when non-zero: close this long after the guest run finished (automation)
};
// `store` null: the legacy placeholder. Otherwise the window shows the DIAGNOSTIC GOP PREVIEW published by the emulator thread.
void nativeWindow(const std::string &status, bool smoke = false, std::shared_ptr<GopPreviewStore> store = nullptr,
                  const PreviewWindowOptions &options = {});
std::wstring pickFirmware();
} // namespace manta
