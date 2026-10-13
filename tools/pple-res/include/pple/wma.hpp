// WMA (ASF) decoding through Media Foundation.
//
// SDL3_mixer is not an option: its decoder set is aiff, au, flac, fluidsynth, gme, mp3, opus,
// raw, sinewave, timidity, vorbis, voc, wav, wavpack and xmp, with no ASF/WMA entry (checked
// against src/decoder_*.c in libsdl-org/SDL_mixer).  Windows ships the ASF byte-stream handler
// and the WMA decoders, so IMFSourceReader decodes these files directly and needs no new
// third-party library.  All 201 shipped resources are WMA v2, 2 channels, 44100 Hz, 16-bit
// (see `pple-res asf`).
//
// The module is deliberately free of SDL: it turns bytes into interleaved 16-bit PCM, and the
// caller decides what to do with them.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pple {

struct PcmAudio {
    unsigned sample_rate = 0;
    unsigned channels = 0;
    unsigned bits = 0;
    std::vector<std::int16_t> samples;  // interleaved
    unsigned long long bytes_per_second = 0;  // as reported by the decoder
    bool ok = false;

    std::size_t frames() const {
        return channels == 0 ? 0 : samples.size() / channels;
    }
    double seconds() const {
        return sample_rate == 0 ? 0.0
                                : static_cast<double>(frames()) / static_cast<double>(sample_rate);
    }
};

// Starts COM and Media Foundation once per process; safe to call repeatedly.
bool mf_start(std::string& error);
void mf_stop();

// Decodes an ASF/WMA buffer to 16-bit PCM.  Returns false with a message on failure; the HRESULT
// of the failing step is included because MF errors are otherwise anonymous.
bool wma_decode(std::span<const std::uint8_t> data, PcmAudio& out, std::string& error);

// Writes the decoded audio as a plain RIFF/WAVE file, so a decode can be checked by ear.
bool wav_write(const std::string& path, const PcmAudio& audio);

// Reads a 16-bit PCM RIFF/WAVE buffer — what wav_write produces and what `pple-res audio-pack`
// writes for the web build, which has no Media Foundation and therefore cannot decode the
// shipped WMA resources itself (see the web/README.md).
bool wav_read(std::span<const std::uint8_t> data, PcmAudio& out, std::string& error);

}  // namespace pple
