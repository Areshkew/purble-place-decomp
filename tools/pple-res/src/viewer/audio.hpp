#pragma once
#include "resources.hpp"

namespace pple::viewer {
std::string audio_event_file(const Bundle& bundle, const std::string& event);
std::string play_pcm(const pple::PcmAudio& audio, float volume, std::string& error);
bool play_pair_sfx(const std::string& event);
void play_cake_sfx(const std::string& event);
void initialise_cake_audio(const Bundle& bundle, bool mute);
// The sound an event key names when the engine asks for a "proto": GameAudio::LoadGameAudio
// (0x10009DDF8) fills its array by walking <Sounds> in document order, so a proto is the
// element's index there.  Returns an empty string when the index is out of range.
std::string audio_event_at(const Bundle& bundle, int proto);
void play_shop_sfx(const std::string& event);
void initialise_shop_audio(const Bundle& bundle, bool mute);
void play_click_sfx(const pple::ClickResult& result);
void initialise_pair_audio(const Bundle& bundle, bool mute);
} // namespace pple::viewer
