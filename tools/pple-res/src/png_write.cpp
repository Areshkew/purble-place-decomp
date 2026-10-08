#include "pple/png_write.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>

namespace pple {
namespace {

constexpr std::array<std::uint8_t, 8> kSignature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

void push_be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

// The CRC-32 of the PNG specification: reflected 0xEDB88320 with the usual pre/post inversion.
std::uint32_t crc32(std::span<const std::uint8_t> data, std::uint32_t seed = 0xFFFFFFFFu) {
    static std::array<std::uint32_t, 256> table{};
    static bool ready = false;
    if (!ready) {
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        ready = true;
    }
    std::uint32_t c = seed;
    for (std::uint8_t b : data) c = table[(c ^ b) & 0xFFu] ^ (c >> 8);
    return c;
}

// One chunk: length, type, payload, CRC over type + payload.
void push_chunk(std::vector<std::uint8_t>& out, const char type[5],
                std::span<const std::uint8_t> payload) {
    push_be32(out, static_cast<std::uint32_t>(payload.size()));
    const std::size_t crc_begin = out.size();
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(type[i]));
    out.insert(out.end(), payload.begin(), payload.end());
    const std::uint32_t crc = crc32(std::span<const std::uint8_t>(out).subspan(crc_begin)) ^
                              0xFFFFFFFFu;
    push_be32(out, crc);
}

// An Adler-32 over the uncompressed data, which the zlib wrapper that DEFLATE rides in needs.
std::uint32_t adler32(std::span<const std::uint8_t> data) {
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t byte : data) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

// zlib stream (RFC 1950) whose single DEFLATE block is stored, not compressed: BFINAL 1, BTYPE 00,
// then LEN/NLEN and the raw bytes, in at most 65535-byte blocks.
std::vector<std::uint8_t> zlib_stored(std::span<const std::uint8_t> data) {
    std::vector<std::uint8_t> out;
    out.reserve(data.size() + data.size() / 65535 * 5 + 16);
    out.push_back(0x78);  // CM 8, CINFO 7: 32 KiB window
    out.push_back(0x01);  // FCHECK so that (0x78 << 8 | 0x01) % 31 == 0, no dictionary
    std::size_t at = 0;
    while (at < data.size() || data.empty()) {
        const std::size_t n = std::min<std::size_t>(65535, data.size() - at);
        const bool last = at + n >= data.size();
        out.push_back(static_cast<std::uint8_t>(last ? 1 : 0));
        out.push_back(static_cast<std::uint8_t>(n & 0xFF));
        out.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>(~n & 0xFF));
        out.push_back(static_cast<std::uint8_t>((~n >> 8) & 0xFF));
        out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(at),
                   data.begin() + static_cast<std::ptrdiff_t>(at + n));
        at += n;
        if (last) break;
    }
    push_be32(out, adler32(data));
    return out;
}

}  // namespace

bool png_write(std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height,
               std::vector<std::uint8_t>& out, std::string& error) {
    out.clear();
    error.clear();
    if (width == 0 || height == 0) {
        error = "png_write: zero extent";
        return false;
    }
    const std::size_t stride = static_cast<std::size_t>(width) * 4u;
    if (rgba.size() != stride * height) {
        error = "png_write: buffer is not width * height * 4";
        return false;
    }
    // Filter type 0 ("None") on every scanline: one leading byte per row.
    std::vector<std::uint8_t> raw;
    raw.reserve((stride + 1u) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba.begin() + static_cast<std::ptrdiff_t>(y * stride),
                   rgba.begin() + static_cast<std::ptrdiff_t>((y + 1) * stride));
    }
    const std::vector<std::uint8_t> idat = zlib_stored(raw);

    out.insert(out.end(), kSignature.begin(), kSignature.end());
    std::vector<std::uint8_t> ihdr;
    push_be32(ihdr, width);
    push_be32(ihdr, height);
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(6);  // colour type: truecolour with alpha
    ihdr.push_back(0);  // deflate
    ihdr.push_back(0);  // adaptive filtering
    ihdr.push_back(0);  // no interlace
    push_chunk(out, "IHDR", ihdr);
    push_chunk(out, "IDAT", idat);
    push_chunk(out, "IEND", {});
    return true;
}

}  // namespace pple
