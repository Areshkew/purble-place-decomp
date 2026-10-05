// Raw DEFLATE (RFC 1951) decoder, no external dependencies.
//
// MSZIP cabinet segments are raw DEFLATE streams and the 32 KiB history window is
// carried across the CFDATA blocks of one folder.  That falls out naturally here
// because back-references are resolved against the caller's output buffer: append
// every segment to the same vector and history carry-over works by construction.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pple {

struct InflateStatus {
    bool ok = false;
    std::string error;
    std::size_t in_consumed = 0;  // bytes of input consumed up to the final block
    std::size_t out_begin = 0;    // index in `out` where this stream started writing
};

// Decodes one complete DEFLATE stream from `in`, appending the result to `out`.
// Stops at the end-of-block symbol 256, so trailing padding bits are ignored.
InflateStatus inflate_raw(std::span<const std::uint8_t> in, std::vector<std::uint8_t>& out);

}  // namespace pple
