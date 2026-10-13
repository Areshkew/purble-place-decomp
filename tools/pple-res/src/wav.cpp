// The portable half of the audio module: a decoded PcmAudio written as — and read back from — a
// plain RIFF/WAVE file, so a decode can be checked by ear.
//
// It lives here rather than in wma.cpp because that file is the Windows-only Media Foundation
// decoder; `pple-res wma --all --out` is useful on every platform that has a decoder, and this
// pair of functions needs nothing from the platform at all.  The reader is what lets the web
// build play the shipped effects, which it cannot decode itself.
#include "pple/wma.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace pple {

bool wav_write(const std::string& path, const PcmAudio& audio) {
    if (!audio.ok) return false;
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    const std::uint32_t data_bytes = static_cast<std::uint32_t>(audio.samples.size() * 2);
    const std::uint16_t block_align = static_cast<std::uint16_t>(audio.channels * 2);
    const std::uint32_t byte_rate = audio.sample_rate * block_align;
    const auto u32 = [&](std::uint32_t v) {
        const char b[4] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
                           static_cast<char>((v >> 16) & 0xFF), static_cast<char>((v >> 24) & 0xFF)};
        out.write(b, 4);
    };
    const auto u16 = [&](std::uint16_t v) {
        const char b[2] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF)};
        out.write(b, 2);
    };
    out.write("RIFF", 4);
    u32(36 + data_bytes);
    out.write("WAVEfmt ", 8);
    u32(16);
    u16(1);  // PCM
    u16(static_cast<std::uint16_t>(audio.channels));
    u32(audio.sample_rate);
    u32(byte_rate);
    u16(block_align);
    u16(16);
    out.write("data", 4);
    u32(data_bytes);
    out.write(reinterpret_cast<const char*>(audio.samples.data()),
              static_cast<std::streamsize>(data_bytes));
    return out.good();
}

// The reader for the same format.  It exists for the browser build: Media Foundation is Windows
// only, so the web page is given a WAV pack of the shipped effects (`pple-res audio-pack`) and
// plays that instead of decoding WMA.
bool wav_read(std::span<const std::uint8_t> data, PcmAudio& out, std::string& error) {
    const auto rd16 = [&](std::size_t at) {
        return static_cast<std::uint16_t>(data[at] | (static_cast<std::uint16_t>(data[at + 1]) << 8));
    };
    const auto rd32 = [&](std::size_t at) {
        return static_cast<std::uint32_t>(data[at]) | (static_cast<std::uint32_t>(data[at + 1]) << 8) |
               (static_cast<std::uint32_t>(data[at + 2]) << 16) |
               (static_cast<std::uint32_t>(data[at + 3]) << 24);
    };
    const auto tag = [&](std::size_t at, const char* want) {
        return at + 4 <= data.size() && std::memcmp(data.data() + at, want, 4) == 0;
    };

    if (data.size() < 44 || !tag(0, "RIFF") || !tag(8, "WAVE")) {
        error = "not a RIFF/WAVE file";
        return false;
    }
    unsigned channels = 0, sample_rate = 0, bits = 0;
    std::size_t data_at = 0, data_bytes = 0;
    for (std::size_t at = 12; at + 8 <= data.size();) {
        const std::uint32_t size = rd32(at + 4);
        if (tag(at, "fmt ") && at + 8 + 16 <= data.size()) {
            const std::uint16_t format = rd16(at + 8);
            channels = rd16(at + 10);
            sample_rate = rd32(at + 12);
            bits = rd16(at + 22);
            // 0xFFFE is WAVE_FORMAT_EXTENSIBLE; its real format is the first two bytes of the
            // sub-format GUID, which follows the 16 bytes of the plain fmt chunk.
            if (format != 1 && !(format == 0xFFFE && at + 8 + 26 <= data.size() && rd16(at + 32) == 1)) {
                error = "unsupported WAVE format " + std::to_string(format);
                return false;
            }
        } else if (tag(at, "data")) {
            data_at = at + 8;
            data_bytes = std::min<std::size_t>(size, data.size() - data_at);
        }
        at += 8 + size + (size & 1);  // chunks are word-aligned
    }
    if (channels == 0 || sample_rate == 0 || data_at == 0) {
        error = "no fmt or data chunk";
        return false;
    }
    if (bits != 16) {
        error = std::to_string(bits) + "-bit PCM is not supported";
        return false;
    }
    out = PcmAudio{};
    out.channels = channels;
    out.sample_rate = sample_rate;
    out.bits = bits;
    out.bytes_per_second = sample_rate * channels * 2;
    out.samples.resize(data_bytes / 2);
    for (std::size_t i = 0; i < out.samples.size(); ++i) {
        out.samples[i] = static_cast<std::int16_t>(rd16(data_at + i * 2));
    }
    out.ok = !out.samples.empty();
    if (!out.ok) error = "no samples";
    return out.ok;
}

}  // namespace pple
