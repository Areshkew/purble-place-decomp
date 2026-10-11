#pragma once
#include "resources.hpp"

namespace pple::viewer {
std::string audio_event_file(const Bundle& bundle, const std::string& event);
std::string play_pcm(const pple::PcmAudio& audio, float volume, std::string& error);
bool play_pair_sfx(const std::string& event);
void play_cake_sfx(const std::string& event);
void initialise_cake_audio(const Bundle& bundle, bool mute);
void play_click_sfx(const pple::ClickResult& result);
void initialise_pair_audio(const Bundle& bundle, bool mute);
} // namespace pple::viewer
