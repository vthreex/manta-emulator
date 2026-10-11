#pragma once
#include <cstdint>
#include <span>
#include <string>
namespace manta {
std::string sha256(std::span<const uint8_t> bytes);
}
