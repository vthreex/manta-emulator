// Browser build: there is no native window. The page draws the published GOP preview frames itself (manta_frame_* in native/runtime/native_runner.cpp)
// and sends remote functions with manta_key(); main() hands the machine to webStart() before any window would be created.
#include "ui/native_window.hpp"

namespace manta {
std::wstring pickFirmware() { return L""; }
void nativeWindow(const std::string&, bool, std::shared_ptr<GopPreviewStore>, const PreviewWindowOptions&) {}
}  // namespace manta
