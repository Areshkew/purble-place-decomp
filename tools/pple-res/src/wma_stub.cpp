// The audio module on a platform without Media Foundation — the Emscripten web build.
//
// Every shipped sound effect is WMA (ASF), and Windows' ASF source reader is the only decoder
// this project can rely on: no browser ships one.  This stub keeps the interface so the game
// screens link and run unchanged — a sound request resolves, opens no SDL audio stream and stays
// silent — instead of the whole viewer failing to link on one missing function.  Playing the
// shipped effects on the web needs a real decoder (WebAudio has none for WMA either) and is a
// follow-up, not a hidden gap: `pple-view` prints "audio: unavailable" at start-up.
#include "pple/wma.hpp"

namespace pple {

bool mf_start(std::string& error) {
    error = "Media Foundation is Windows only";
    return false;
}

void mf_stop() {}

bool wma_decode(std::span<const std::uint8_t>, PcmAudio& out, std::string& error) {
    out = PcmAudio{};
    error = "WMA decoding is unavailable in this build (Media Foundation is Windows only)";
    return false;
}

}  // namespace pple
