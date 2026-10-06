#include "audio.hpp"

namespace pple::viewer {
// AUDIO.XML: <Audio><Music>…</Music><Sounds><EVENT><File>…</File>…</EVENT></Sounds></Audio>.
// Returns the <File> value for an event key, or an empty string.
std::string audio_event_file(const Bundle& bundle, const std::string& event) {
    std::string how;
    const auto* slot = bundle.find("AUDIO\\AUDIO.XML");
    if (slot == nullptr) return {};
    pple::XmlNode root;
    std::string error;
    if (!pple::xml_parse(bundle.read(*slot), root, error)) return {};
    std::string found;
    const auto scan = [&](const pple::XmlNode& group) {
        for (const pple::XmlNode& candidate : group.children) {
            if (candidate.name != event) continue;
            if (const pple::XmlNode* file = candidate.child("File")) found = file->text;
        }
    };
    for (const pple::XmlNode& group : root.children) {
        if (group.name == "Music" || group.name == "Sounds") scan(group);
        else if (group.name == event) {
            if (const pple::XmlNode* file = group.child("File")) found = file->text;
        }
    }
    return found;
}

// Plays interleaved 16-bit PCM once, then drains.  Returns the device name used, or a reason.
std::string play_pcm(const pple::PcmAudio& audio, float volume, std::string& error) {
    if (!audio.ok) {
        error = "no samples";
        return {};
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        error = std::string("SDL_InitSubSystem(AUDIO): ") + SDL_GetError();
        return {};
    }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = static_cast<int>(audio.channels);
    spec.freq = static_cast<int>(audio.sample_rate);
    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (stream == nullptr) {
        error = std::string("SDL_OpenAudioDeviceStream: ") + SDL_GetError();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return {};
    }
    const char* name = SDL_GetCurrentAudioDriver();
    const std::string device = name == nullptr ? "(unknown driver)" : name;
    if (volume != 1.0f) SDL_SetAudioStreamGain(stream, volume);
    SDL_PutAudioStreamData(stream, audio.samples.data(),
                           static_cast<int>(audio.samples.size() * sizeof(std::int16_t)));
    SDL_ResumeAudioStreamDevice(stream);
    // Wait for the queue to drain, with a hard cap so a stuck device cannot hang the tool.
    const Uint64 deadline = SDL_GetTicks() + static_cast<Uint64>(audio.seconds() * 1000.0) + 3000;
    while (SDL_GetAudioStreamQueued(stream) > 0 && SDL_GetTicks() < deadline) SDL_Delay(10);
    SDL_DestroyAudioStream(stream);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return device;
}

// Purble Pairs sound effects, decoded once from the AUDIO.XML manifest and queued on their own
// SDL streams so a click never waits for the previous sound to finish.
//
// The engine plays sounds by "proto": a compile-time index into GameAudio::aSoundEffects.
// GameAudio::LoadGameAudio (0x10009DDF8) fills that array by walking <Sounds> in document order
// and calling Array<SoundEffect*>::SetAt(index, ...), and XmlNode children are elements only --
// comments are not nodes -- so a proto is the element's position among the elements of <Sounds>,
// counting from zero.  Reading the array off AUDIO\AUDIO.XML of PurblePlace.dll that way gives
// the names below, and every proto this file uses was checked against its call site:
//
//   0x6D Con_BoardCleared      Jar::Update 0x1000838A7
//   0x6E Con_Correct           Card::DoMatcher 0x10007D8B5   (the match branch)
//   0x6F Con_GameOver          OnEnterLoseGameState 0x1000825C2 (and again at 0x1000825FF)
//   0x70 Con_Mixer             Card::DoMixerCheck 0x10007DA1C
//   0x71 Con_NewScreen         Board::InitUI 0x10008934D
//   0x72 Con_PowerUp           Card::DoMatcher 0x10007D75D   (exactly one Wild Card)
//   0x73 Con_TileFlip          Card::FlipForward 0x10007CFCD
//   0x74 Con_TileFlip_Incorrect Card::DoMatcher 0x10007D298  (the mismatch branch)
//   0x75 Con_Win               OnEnterWonGameState 0x100082512
//   0x76 Con_PurbleChef        Card::DoMatcher 0x10007D68C   (the Chef's cake sweep)
//   0x77 Con_TimeDelay         Board::MatchMade 0x10008B115, Board::DoTimeBonus 0x10008B1BA
//   0x78 Con_SneakPeek_Starburst SneakPeakArea::HandleEvent 0x100084FEB
//   0x79 Con_SneakPeek_Get     Card::DoMatcher 0x10007D6FD   (the sneak-peek icon match)
//   0x7A Con_SneakPeek_Click   SneakPeakArea::HandleEvent 0x100084EE0
//
// Note that OnWinAnimStop (0x100082670) and OnLoseAnimStop (0x10008272C) play nothing at all:
// Con_BoardCleared comes from the jar, not from the Professor's exit.
struct PairSfx {
    pple::PcmAudio audio;
    SDL_AudioStream* stream = nullptr;
    float volume = 1.0f;
    std::string event;
    std::string resource;
    bool ok = false;
    bool tried = false;
};

const Bundle* g_sfx_bundle = nullptr;
std::map<std::string, PairSfx> g_sfx;
bool g_sfx_ready = false;
bool g_sfx_enabled = true;
std::string g_sfx_driver;

PairSfx& pair_sfx(const std::string& event) {
    static PairSfx missing;
    PairSfx& slot = g_sfx[event];
    if (slot.tried) return slot;
    slot.tried = true;
    slot.event = event;
    if (g_sfx_bundle == nullptr) return slot;
    const Bundle& bundle = *g_sfx_bundle;
    const std::string file = audio_event_file(bundle, event);
    if (file.empty()) return slot;
    std::string resource = "AUDIO\\SFX\\" + file;
    for (char& c : resource)
        if (c == '/') c = '\\';
    std::string how;
    const auto* res = bundle.resolve(resource, how);
    if (res == nullptr) return slot;
    std::string error;
    if (!pple::wma_decode(bundle.read(*res), slot.audio, error)) return slot;
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = static_cast<int>(slot.audio.channels);
    spec.freq = static_cast<int>(slot.audio.sample_rate);
    slot.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (slot.stream == nullptr) return slot;
    slot.resource = res->entry->name;
    // The manifest's own <Volume> field, as a fraction.
    std::string vol_text;
    {
        pple::XmlNode root;
        std::string xml_error;
        if (const auto* audio_xml = bundle.find("AUDIO\\AUDIO.XML");
            audio_xml != nullptr && pple::xml_parse(bundle.read(*audio_xml), root, xml_error)) {
            const auto find_volume = [&](const pple::XmlNode& group) {
                for (const pple::XmlNode& candidate : group.children)
                    if (candidate.name == event) vol_text = candidate.child_text("Volume");
            };
            for (const pple::XmlNode& group : root.children)
                if (group.name == "Music" || group.name == "Sounds") find_volume(group);
        }
    }
    const int volume = vol_text.empty() ? 100 : std::atoi(vol_text.c_str());
    slot.volume = static_cast<float>(volume) / 100.0f;
    SDL_SetAudioStreamGain(slot.stream, slot.volume);
    slot.ok = true;
    return slot;
}

void play_pair_sfx(const std::string& event) {
    if (!g_sfx_enabled || !g_sfx_ready) return;
    PairSfx& sfx = pair_sfx(event);
    if (!sfx.ok || sfx.stream == nullptr) return;
    SDL_PutAudioStreamData(sfx.stream, sfx.audio.samples.data(),
                           static_cast<int>(sfx.audio.samples.size() * sizeof(std::int16_t)));
    SDL_ResumeAudioStreamDevice(sfx.stream);
}

// The sounds one click owes, in the order Card::DoMatcher reaches them.  Card::FlipForward
// (0x10007CFA4) plays Con_TileFlip for every card, including the first of a pair; everything
// else is inside DoMatcher's `v6 != 0` block, i.e. only once the second card has been revealed.
void play_click_sfx(const pple::ClickResult& r) {
    play_pair_sfx("Con_TileFlip");
    if (!r.second) return;
    if (r.mismatch) play_pair_sfx("Con_TileFlip_Incorrect");
    if (r.chef) play_pair_sfx("Con_PurbleChef");
    if (r.sneak_get) play_pair_sfx("Con_SneakPeek_Get");
    if (r.wild_match) play_pair_sfx("Con_PowerUp");
    if (r.match) play_pair_sfx("Con_Correct");
    if (r.mixer) play_pair_sfx("Con_Mixer");
    if (r.time_bonus > 0) play_pair_sfx("Con_TimeDelay");
}

// A sprite node definition, resolved to a sheet and one frame rectangle.

void initialise_pair_audio(const Bundle& bundle, bool mute) {
        if (!g_sfx_ready) {
            g_sfx_ready = true;
            g_sfx_enabled = !mute;
            g_sfx_bundle = &bundle;
            if (g_sfx_enabled && SDL_InitSubSystem(SDL_INIT_AUDIO)) {
                // The three the sound check in the README names, plus every other event this
                // screen can reach.  Each is decoded once, at the manifest's own <Volume>.
                const char* kEvents[] = {
                    "Con_TileFlip",     "Con_Correct",    "Con_TileFlip_Incorrect",
                    "Con_NewScreen",    "Con_BoardCleared", "Con_Win",
                    "Con_GameOver",     "Con_PowerUp",    "Con_Mixer",
                    "Con_PurbleChef",   "Con_TimeDelay",  "Con_SneakPeek_Get",
                    "Con_SneakPeek_Click", "Con_SneakPeek_Starburst",
                };
                int loaded = 0;
                std::string names;
                for (const char* event : kEvents) {
                    PairSfx& sfx = pair_sfx(event);
                    if (sfx.ok) ++loaded;
                    if (sfx.ok) {
                        if (!names.empty()) names += ", ";
                        names += event;
                    }
                }
                const char* driver = SDL_GetCurrentAudioDriver();
                g_sfx_driver = driver == nullptr ? "(none)" : driver;
                std::printf("  audio %s: %d/%zu events decoded: %s\n", g_sfx_driver.c_str(), loaded,
                            sizeof(kEvents) / sizeof(kEvents[0]), names.c_str());
            } else {
                std::printf("  audio: disabled\n");
            }
        }
}

} // namespace pple::viewer
