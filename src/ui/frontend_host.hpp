#pragma once
#include "video/gop_preview.hpp"
#include <memory>
#include <string>
namespace manta {
// Headless-window presenter for an external front end (the Qt control centre): instead of a Win32 window, every new GOP preview frame is converted to
// opaque BGRA (same pixel rules as the native window) and published in a named shared-memory block (layout in docs/frontend-protocol.md); remote key
// presses arrive as text lines on stdin ("KEY MENU", "RAW 0x0e", "QUIT"). Presentation only: nothing here touches guest state.
void frontendHost(const std::string& shmName, std::shared_ptr<GopPreviewStore> store);
} // namespace manta
