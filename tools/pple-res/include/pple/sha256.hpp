// Self-contained SHA-256, used only so the C++ extractor's output can be compared
// byte-for-byte against the reference extraction hashes recorded in the report.
#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace pple {

std::string sha256_hex(std::span<const std::uint8_t> data);

}  // namespace pple
