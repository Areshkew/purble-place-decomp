#include "app.hpp"
#include "audio.hpp"
#include "pple/cakes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace pple::viewer {

// The six tool machines and the animations a button click plays on them.  See cakes_tools.hpp
// for the document layout; the addresses below are the engine's own.
//
// A click on a tool button is Tool::UseTool (0x1000751CC):
//   * it locks the belt (AddBeltLock) and arms the machine's animation, which the tool's own
//     PlayAddToolAnimation / PlayRemoveToolAnimation virtual starts;
//   * the running animation hides the cake's current top layer and, for the batter machine,
//     re-parents that tool's shape fill sprite onto the cake so the ingredient is drawn
//     growing into it;
//   * Tool::FinishAddTool (0x10007548A) applies the layer when the animation completes, and
//     ComfyCakesGame::OnToolUsed (0x10005C624) compares the cake with the TV's order.
//
// A click the cake cannot answer takes Tool::UseTool's else branch (0x10007539B): two
// GameAudio protos, MachineUnusableShakeAnim.xml resumed on the machine Base, and the
// "NeedWaxPaper" bubble when there is no cake under the machine at all.

namespace {

// Where a machine's container sits and which station it serves.  The document order of
// COMFYCAKESGAME.XML's <Tools> is the order InitGameUI loads them in, and that is also the order
// InitGameUI hands out belt positions in (0x10005DABC: Tool+60 = the tool count so far + 2), so
// CakePanTool takes belt slot 2, BatterTool 3, FillingTool 4, IcingTool 5, DecorationTool 6 and
// FinalTouchTool 7.
struct ToolEntry {
    const char* name;
    const char* document;
    int station;
    int slot;
};
const ToolEntry kTools[] = {
    {
        "CakePanTool", "UI\\COMFYCAKES\\TOOLS\\CAKEPANTOOL.XML", 1, 2
    },
    {
        "BatterTool", "UI\\COMFYCAKES\\TOOLS\\BATTERTOOL.XML", 0, 3
    },
    {
        "FillingTool", "UI\\COMFYCAKES\\TOOLS\\FILLINGTOOL.XML", 3, 4
    },
    {
        "IcingTool", "UI\\COMFYCAKES\\TOOLS\\ICINGTOOL.XML", 5, 5
    },
    {
        "DecorationTool", "UI\\COMFYCAKES\\TOOLS\\DECORATIONTOOL.XML", 2, 6
    },
    {
        "FinalTouchTool", "UI\\COMFYCAKES\\TOOLS\\FINALTOUCHTOOL.XML", 4, 7
    },
};

// The tool documents spell the element names <FallingSprite>, <ActionSprite>, <SplatSprite>,
// <StreamSprite>, <BaseSprite> and the three shape fills; the flavour buttons live under the
// <Array Name="Buttons"> that Tool::Init walks.
struct PartEntry {
    const char* element;      // the tool document's element
    CakePartIndex index_from;
    int type_base;            // the <Type> the part's first animation serves
    const char* anim_element; // where its animations are listed
    bool rides_the_cake;
};

const PartEntry kPanParts[] = {
    {
        "FallingSprite", CakePartIndex::Single, 0, "FallingAnim", false
    },
    {
        "ActionSprite", CakePartIndex::Single, 0, "ActionAnim", false
    },
};
const PartEntry kBatterParts[] = {
    {
        "FallingSprite", CakePartIndex::Single, 0, "FallingAnim", false
    },
    {
        "ActionSprite", CakePartIndex::ByType, 0, "ActionAnims", false
    },
    {
        "RoundFillingSprite", CakePartIndex::ByShape, 0, "FillAnims", true
    },
    {
        "RectangleFillingSprite", CakePartIndex::ByShape, 0, "FillAnims", true
    },
    {
        "HeartFillingSprite", CakePartIndex::ByShape, 0, "FillAnims", true
    },
};
const PartEntry kFillingParts[] = {
    {
        "SplatSprite", CakePartIndex::Single, 0, "SplatAnim", false
    },
    {
        "BaseSprite", CakePartIndex::ByType, 10, "BaseAnims", false
    },
    {
        "ActionSprite", CakePartIndex::Single, 0, "ActionAnim", false
    },
};
const PartEntry kIcingParts[] = {
    {
        "SplatSprite", CakePartIndex::Single, 0, "SplatAnim", false
    },
    {
        "StreamSprite", CakePartIndex::Single, 0, "StreamAnim", false
    },
    {
        "ActionSprite", CakePartIndex::ByType, 15, "ActionAnims", false
    },
};
const PartEntry kDecorationParts[] = {
    {
        "FallingSprite", CakePartIndex::Single, 0, "FallingAnim", false
    },
    {
        "ActionSprite", CakePartIndex::ByType, 6, "ActionAnims", false
    },
};
const PartEntry kFinalParts[] = {
    {
        "ActionSprite", CakePartIndex::ByType, 13, "ActionAnims", false
    },
    {
        "BaseSprite", CakePartIndex::Single, 0, "BaseAnims", false
    },
};

struct PartsEntry {
    const char* tool;
    const PartEntry* parts;
    std::size_t count;
};
const PartsEntry kParts[] = {
    {
        "CakePanTool", kPanParts, std::size(kPanParts)
    },
    {
        "BatterTool", kBatterParts, std::size(kBatterParts)
    },
    {
        "FillingTool", kFillingParts, std::size(kFillingParts)
    },
    {
        "IcingTool", kIcingParts, std::size(kIcingParts)
    },
    {
        "DecorationTool", kDecorationParts, std::size(kDecorationParts)
    },
    {
        "FinalTouchTool", kFinalParts, std::size(kFinalParts)
    },
};

const pple::XmlNode* child_ci(const pple::XmlNode& node, const char* name) {
    std::string want = name;
    for (char& c : want) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const pple::XmlNode& c : node.children) {
        std::string have = c.name;
        for (char& ch : have)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (have == want) return &c;
    }
    return nullptr;
}

float num_ci(const pple::XmlNode& node, const char* name, float fallback) {
    const pple::XmlNode* c = child_ci(node, name);
    if (c == nullptr || c->text.empty()) return fallback;
    return static_cast<float>(std::atof(c->text.c_str()));
}

bool read_anim(const std::vector<std::uint8_t>& bytes, CakeAnimSet& out) {
    return pple::animation_parse(bytes, out);
}

// The first non-empty text of an element's child list, which is how the tool documents name a
// single animation (<FallingAnim>UI\...xml</FallingAnim>).
std::string element_text(const pple::XmlNode& node, const char* name) {
    const pple::XmlNode* c = child_ci(node, name);
    return c == nullptr ? std::string {} : c->text;
}

// Every <Anim> text under an element, which is how the per-flavour lists are spelled.
std::vector<std::string> anim_list(const pple::XmlNode& node, const char* name) {
    std::vector<std::string> out;
    const pple::XmlNode* list = child_ci(node, name);
    if (list == nullptr) return out;
    for (const pple::XmlNode& c : list->children)
        if (c.text.size() > 4) out.push_back(c.text);
    return out;
}

// The value one run of an animation holds at time t, accumulated over the whole timeline so a
// set that drives two properties keeps both.
using AnimValue = pple::AnimationValue;
using pple::animation_value;

}  // namespace

bool App::cakes_tool_visible(int station) const {
    // ComfyCakesGame::ShowTools (0x100060954).
    return (station != 3 || cakes_difficulty != 0) &&
           (station != 4 || cakes_difficulty == 2);
}

int App::cakes_tool_slot_for(int station) const {
    if (!cakes_tool_visible(station)) return -1;
    for (const ToolEntry& tool : kTools)
        if (tool.station == station)
            return tool.slot + (cakes_difficulty == 0 && tool.slot < 4 ? 1 : 0);
    return -1;
}

const CakeTool* App::cakes_tool_for_station(int station) const {
    for (const ToolEntry& entry : kTools) {
        if (entry.station != station) continue;
        const auto it = cakes_tools.find(entry.name);
        return it == cakes_tools.end() ? nullptr : &it->second;
    }
    return nullptr;
}

// The six machines, parsed once per session, the way professor.cpp caches his movies.  The
// container positions are not in the tool documents; build_cakes records them from
// COMFYCAKESGAME.XML's own <Tool> elements, which is where the engine reads them too.
void App::cakes_tools_load() {
    if (cakes_tools_tried) return;
    cakes_tools_tried = true;
    const auto fetch = [&](const std::string& name, std::vector<std::uint8_t>& out) {
        std::string how;
        const auto* s = bundle.resolve(name, how);
        if (s == nullptr) return false;
        out = bundle.read(*s);
        return true;
    };
    std::vector<std::uint8_t> shake;
    if (fetch("UI\\COMFYCAKES\\MACHINES\\MACHINEUNUSABLESHAKEANIM.XML", shake) &&
        read_anim(shake, cakes_tools[""].shake)) {
        // The shake is shared by all six machines (Tool::Init attaches it to each machine Base).
    }
    for (const ToolEntry& entry : kTools) {
        std::vector<std::uint8_t> bytes;
        if (!fetch(entry.document, bytes)) {
            std::printf("cakes tool: %s not found\n", entry.document);
            continue;
        }
        pple::XmlNode root;
        std::string error;
        if (!pple::xml_parse(bytes, root, error)) {
            std::printf("cakes tool: %s, %s\n", entry.document, error.c_str());
            continue;
        }
        CakeTool tool;
        tool.name = entry.name;
        tool.station = static_cast<int>(num_ci(root, "Station", -1));
        tool.shake = cakes_tools[""].shake;
        const PartsEntry* parts = nullptr;
        for (const PartsEntry& candidate : kParts)
            if (std::string(candidate.tool) == entry.name) parts = &candidate;
        if (parts != nullptr) {
            for (std::size_t i = 0; i < parts->count; ++i) {
                const PartEntry& spec = parts->parts[i];
                const pple::XmlNode* sprite = child_ci(root, spec.element);
                if (sprite == nullptr) continue;
                CakeToolPart part;
                part.element = spec.element;
                part.x = num_ci(*sprite, "X", 0.0f);
                part.y = num_ci(*sprite, "Y", 0.0f);
                part.index_from = spec.index_from;
                part.type_base = spec.type_base;
                part.rides_the_cake = spec.rides_the_cake;
                part.visible_at_rest = std::string(spec.element) == "ActionSprite" ||
                                       std::string(spec.element) == "BaseSprite";
                // The sprite document lives in the part's own <Desc><Sprite>.
                const pple::XmlNode* desc = child_ci(*sprite, "Desc");
                part.sprite = desc == nullptr ? std::string {} : element_text(*desc, "Sprite");
                if (part.sprite.empty()) continue;
                std::vector<std::string> anims = anim_list(root, spec.anim_element);
                if (anims.empty()) {
                    const std::string single = element_text(root, spec.anim_element);
                    if (!single.empty()) anims.push_back(single);
                }
                for (const std::string& anim : anims) {
                    CakeAnimSet set;
                    std::vector<std::uint8_t> anim_bytes;
                    if (fetch(anim, anim_bytes) && read_anim(anim_bytes, set) && set.total > 0.0f)
                        part.anims.push_back(std::move(set));
                }
                // The fill sprites' <RegistrationOffsetX/Y> re-centres the growing ingredient on
                // the cake, so the drawn corner is the position minus that too.
                std::vector<std::uint8_t> sprite_bytes;
                if (fetch(part.sprite, sprite_bytes)) {
                    pple::XmlNode sprite_root;
                    std::string sprite_error;
                    if (pple::xml_parse(sprite_bytes, sprite_root, sprite_error)) {
                        int seen = 0;
                        for (const pple::XmlNode& anim : sprite_root.children) {
                            if (anim.name != "Animation") continue;
                            if (seen == 0) {
                                part.reg_off_x = num_ci(anim, "RegistrationOffsetX", 0.0f);
                                part.reg_off_y = num_ci(anim, "RegistrationOffsetY", 0.0f);
                            }
                            ++seen;
                        }
                    }
                }
                tool.parts.push_back(std::move(part));
            }
        }
        std::size_t animations = 0;
        for (const CakeToolPart& part : tool.parts) animations += part.anims.size();
        std::printf("cakes tool: %s station %d belt slot %d, %zu part(s), %zu animation(s)%s\n",
                    tool.name.c_str(), tool.station, entry.slot, tool.parts.size(), animations,
                    tool.shake.runs.empty() ? "" : ", shake loaded");
        cakes_tools[entry.name] = std::move(tool);
    }
}

// The flavour index one part plays for a button's <Type>, and the way the icing machine rotates
// its base between flavours.  ToolIcing::PlayRotateAnimation (0x1000783B8) picks one of the
// three pair animations and reverses half of them; the reconstruction keeps the strip and its
// direction.
std::size_t App::cakes_tool_flavour(const CakeToolPart& part, int type, int shape,
                                    bool& reverse) {
    reverse = false;
    if (part.index_from == CakePartIndex::ByShape)
        return static_cast<std::size_t>(std::clamp(shape, 0, static_cast<int>(part.anims.size()) - 1));
    if (part.index_from == CakePartIndex::Single) return 0;
    const int raw = type - part.type_base;
    if (raw < 0 || raw >= static_cast<int>(part.anims.size())) return 0;
    return static_cast<std::size_t>(raw);
}

// Tool::UseTool's accepted branch: lock the belt, hide the cake's top layer and run the
// machine.  The layer is already in the cake (Cake::AddLayer is applied by FinishAddTool, which
// this stands in for by completing the animation) and is drawn again when it ends.
void App::cakes_tools_start(int station, int type, int shape, int slot) {
    const CakeTool* tool = cakes_tool_for_station(station);
    cakes_tool_busy = true;
    cakes_tool_station_running = station;
    cakes_tool_slot_running = slot;
    cakes_tool_type_running = type;
    cakes_tool_shape_running = shape;
    cakes_tool_time = 0;
    cakes_tool_playing.clear();
    cakes_tool_total = 0;
    cakes_tool_reveal_at = 0;
    if (tool == nullptr) { cakes_tool_total = 0.6f; return; }
    const auto find = [&](const char* element) -> const CakeToolPart* {
        for (const auto& part : tool->parts) if (part.element == element) return &part;
        return nullptr;
    };
    const auto play = [&](const CakeToolPart* part, float start, int override_index = -1,
                          bool backwards = false) -> float {
        if (!part || part->anims.empty()) return start;
        bool reverse = false;
        const auto index = override_index < 0 ? cakes_tool_flavour(*part, type, shape, reverse)
            : static_cast<std::size_t>(override_index);
        if (index >= part->anims.size()) return start;
        const float end = start + part->anims[index].total;
        cakes_tool_playing.push_back({part, index, reverse || backwards, start, end});
        cakes_tool_total = std::max(cakes_tool_total, end);
        return end;
    };
    const auto cue = [&](const CakeToolPart* part, int index, float fallback) {
        if (!part || index < 0 || index >= static_cast<int>(part->anims.size())) return fallback;
        for (const auto& event : part->anims[index].cues) if (!event.complete) return event.time;
        return fallback;
    };
    const auto* action = find("ActionSprite");
    const auto* base = find("BaseSprite");
    const auto* falling = find("FallingSprite");
    float action_start = 0;
    const int flavour = station == 5 ? type - 15 : station == 4 ? type - 13 : 0;
    // ToolIcing::PlayRotateAnimation: pair strips 3/4/5, reversed for the opposite turn.
    if (station == 5 && action && cakes_machine_type[5] != flavour) {
        const int old = cakes_machine_type[5];
        const int pair = (old == 0 && flavour == 1) || (old == 1 && flavour == 0) ? 3
            : (old == 1 && flavour == 2) || (old == 2 && flavour == 1) ? 4 : 5;
        const bool reverse = (old == 1 && flavour == 0) || (old == 2 && flavour == 1) || (old == 0 && flavour == 2);
        action_start = play(action, 0, pair, reverse);
    } else if (station == 4 && cakes_machine_type[4] != flavour) {
        action_start = play(base, 0, 0, flavour == 0);
    } else if (station == 3) {
        action_start = play(base, 0);
    }
    const float action_end = play(action, action_start);
    bool reverse = false;
    const auto action_index = action && !action->anims.empty()
        ? cakes_tool_flavour(*action, type, shape, reverse) : 0;
    const float trigger = action_start + cue(action, static_cast<int>(action_index), 0);
    if (station == 0) {
        play(falling, 0);
        static const char* fills[] = {"RoundFillingSprite", "RectangleFillingSprite", "HeartFillingSprite"};
        play(find(fills[std::clamp(shape, 0, 2)]), 0);
    } else if (station == 1 || station == 2) {
        // Pan: speed 4 then +1/tick. Decoration: speed 1 then +0.4/tick, both 60 Hz.
        const float start_y = station == 1 ? -220.0f : -170.0f;
        const float end_y = cakes_cake_top_y(cakes_tool_before, 0) - 65;
        const auto drop = pple::animation_drop(start_y, end_y, 0, station == 1 ? 4 : 1,
                                              station == 1 ? 1 : 0.4f, station == 1);
        const float land = trigger + drop.seconds;
        play(falling, trigger);
        if (station == 1 && falling && !cakes_tool_playing.empty()) {
            cakes_tool_playing.back().finish += drop.seconds;
            cakes_tool_total = std::max(cakes_tool_total, cakes_tool_playing.back().finish);
        }
        if (station == 2 && !cakes_tool_playing.empty()) {
            cakes_tool_playing.back().finish = land;
            cakes_tool_total = std::max(action_end, land);
        }
        cakes_tool_reveal_at = trigger;
    } else if (station == 3) {
        play(find("SplatSprite"), trigger);
    } else if (station == 5) {
        const auto* stream = find("StreamSprite");
        play(stream, trigger);
        const float splat = trigger + cue(stream, 0, 0);
        play(find("SplatSprite"), splat);
        cakes_tool_reveal_at = splat;
    } else if (station == 4) {
        cakes_tool_reveal_at = trigger;
    }
    cakes_machine_type[station] = station == 0 ? type : station == 3 ? type - 10 : flavour;
    if (cakes_tool_total <= 0) cakes_tool_total = 0.6f;
    std::printf("cakes tool: %s plays for %.2f s (%zu part(s)), effect cue %.3f s\n",
                tool->name.c_str(), static_cast<double>(cakes_tool_total), cakes_tool_playing.size(),
                static_cast<double>(cakes_tool_reveal_at));
}

// Tool::UseTool's refusal branch: the shake on the machine Base and two GameAudio protos
// (0x10 and 3), which the reconstruction already answers with Con_CakeButtons.
void App::cakes_tools_shake(int station) {
    const CakeTool* tool = cakes_tool_for_station(station);
    if (tool == nullptr || tool->shake.runs.empty()) return;
    cakes_tool_shake = 0.0f;
    cakes_tool_shake_station = station;
    std::printf("cakes tool: %s shakes, the cake cannot answer it\n", tool->name.c_str());
}

// The animation clock.  The engine runs these on the same 30 Hz step as everything else, which
// is what PurbleChef::Update and Board::Update share; the wall clock is used here so a capture
// with --shot-delay lands at the same frame a player would see.
void App::cakes_tools_advance(float dt) {
    if (cakes_tool_busy) {
        cakes_tool_time += dt;
        if (cakes_tool_time >= cakes_tool_total) cakes_tool_complete();
        return;
    }
    if (cakes_tool_shake >= 0.0f) {
        const CakeTool* tool = cakes_tool_for_station(cakes_tool_shake_station);
        if (tool == nullptr) {
            cakes_tool_shake = -1.0f;
            return;
        }
        cakes_tool_shake += dt;
        if (cakes_tool_shake >= tool->shake.total) cakes_tool_shake = -1.0f;
    }
}

void App::cakes_tools_stop() {
    cakes_tool_busy = false;
    cakes_tool_playing.clear();
    cakes_tool_station_running = -1;
    cakes_tool_slot_running = -1;
    cakes_tool_type_running = -1;
    cakes_tool_time = 0.0f;
}

// The machine parts of the running animation, drawn over the room.  cakes_redraw has already
// truncated the draw list back to the static room, so this only appends.  Each part is drawn at
// its own container offset plus the animation's positional run. CurrentFrame still drives
// callback cues beyond the sheet's last frame, but the UVs clamp to the declared FrameCount.
void App::cakes_tools_draw() {
    for (const auto& [name, machine] : cakes_tools) {
        const CakeTool* tool = &machine;
        if (!cakes_tool_visible(tool->station)) continue;
        const bool active = cakes_tool_busy && cakes_tool_station_running == tool->station;
        const bool shaking = cakes_tool_shake >= 0.0f && cakes_tool_shake_station == tool->station;
        const auto anchor = cakes_tool_anchor.find(tool->name);
        const float ax = anchor == cakes_tool_anchor.end() ? 0.0f : anchor->second.first;
        const float ay = anchor == cakes_tool_anchor.end() ? 0.0f : anchor->second.second;
        // Where the cake under this machine sits, for the parts that ride it.  The belt container is
        // at (0,220) and ConveyorBelt::SetCakePosition works inside it.
        const int slot = cakes_tool_slot_running;
        float belt_x = 0.0f, belt_y = 0.0f;
        cakes_transport_position(slot, belt_x, belt_y);
        const float cake_x = belt_x;
        const float top_y = active && slot >= 0 && static_cast<std::size_t>(slot) < cakes_belt.size()
            ? cakes_cake_top_y(cakes_belt[static_cast<std::size_t>(slot)], 1) : 0.0f;
        const float cake_y = 220.0f + belt_y + top_y;
        // A refusal shakes the whole machine, which is the machine Base the animation is attached to
        // in Tool::Init.
        float shake_dx = 0.0f;
        if (shaking) {
            const AnimValue v = animation_value(tool->shake, cakes_tool_shake, false);
            shake_dx = v.dx;
        }
        std::vector<CakeToolPlaying> parts;
        for (const CakeToolPart& part : tool->parts) {
            const CakeToolPlaying* playing = nullptr;
            if (active) for (const auto& candidate : cakes_tool_playing)
                if (candidate.part == &part && cakes_tool_time >= candidate.start &&
                    cakes_tool_time < candidate.finish) playing = &candidate;
            if (playing) parts.push_back(*playing);
            else if (part.visible_at_rest) {
                if (active && tool->station == 4 && part.element == "ActionSprite" &&
                    std::any_of(cakes_tool_playing.begin(), cakes_tool_playing.end(),
                        [&](const auto& p) { return p.part == &part && cakes_tool_time < p.start; })) continue;
                const int state = part.index_from == CakePartIndex::Single ? 0 : cakes_machine_type[tool->station];
                parts.push_back({&part, static_cast<std::size_t>(state), false, -1, 0});
            }
        }
        for (const CakeToolPlaying& playing : parts) {
            const CakeToolPart& part = *playing.part;
            float local_time = cakes_tool_time - playing.start;
            if (tool->station == 1 && part.element == "FallingSprite") {
                const auto drop = pple::animation_drop(-220, cakes_cake_top_y(cakes_tool_before, 0) - 65,
                    local_time, 4, 1, true);
                local_time = std::max(0.0f, local_time - drop.seconds);
            }
            AnimValue v = active && playing.start >= 0 && !part.anims.empty()
                ? animation_value(part.anims[playing.index], local_time, playing.reverse) : AnimValue{};
            if (active && tool->station == 0 && part.element == "FallingSprite" && v.frame >= 7) continue;
            int sprite_state = static_cast<int>(playing.index);
            if (tool->station == 4 && part.element == "BaseSprite")
                sprite_state = active && playing.start >= 0 ? 2 : cakes_machine_type[4];
            // A shared effect animation does not imply a shared sprite state: OnAnimationInt
            // selects the pan shape or ingredient type independently of the animation handle.
            if (active && !part.visible_at_rest) {
                switch (tool->station) {
                    case 1: sprite_state = cakes_tool_shape_running; break;
                    case 0: sprite_state = cakes_tool_type_running; break;
                    case 3: sprite_state = cakes_tool_type_running - 10; break;
                    case 5: sprite_state = cakes_tool_type_running - 15; break;
                    case 2: sprite_state = cakes_tool_type_running - 6; break;
                }
            }
            const viewer::Sprite sp = resolve_sprite(bundle, part.sprite, sprite_state);
            if (!sp.ok) {
                std::fprintf(stderr, "cakes tool: %s, %s\n", part.sprite.c_str(), sp.note.c_str());
                continue;
            }
            std::string how;
            const auto* sheet_slot = bundle.resolve(sp.requested, how);
            if (sheet_slot == nullptr) continue;
            std::string note;
            SDL_Texture* tex = load_sheet_texture(sp.requested, sheet_slot->entry->name, note);
            if (tex == nullptr) continue;
            pple::SceneItem geometry;
            geometry.src_x = sp.src.x;
            geometry.src_y = sp.src.y;
            geometry.src_w = sp.src.w;
            geometry.src_h = sp.src.h;
            // NodeSprite::UpdateCurrentUVs (0x100094484) clamps UVs to FrameCount-1.
            // Logical CurrentFrame still crosses cues, but must never read neighboring art.
            geometry.frames = sp.frames;
            geometry.per_row = sp.per_row;
            geometry.prepped = sp.prepped;
            float fx = 0.0f, fy = 0.0f, fw = 0.0f, fh = 0.0f;
            pple::sprite_frame_rect(geometry, static_cast<int>(v.frame), fx, fy, fw, fh);
            DrawItem d;
            d.texture = tex;
            d.src = SDL_FRect{fx, fy, fw, fh};
            const float base_x = part.rides_the_cake ? cake_x : ax + part.x;
            float base_y = part.rides_the_cake ? cake_y : ay + part.y;
            const float height = cake_y - 220.0f - belt_y;
            float scale = 1;
            if (active && part.element == "FallingSprite" && (tool->station == 1 || tool->station == 2)) {
                const bool pan = tool->station == 1;
                const auto drop = pple::animation_drop(pan ? -220 : -170,
                    cakes_cake_top_y(cakes_tool_before, 0) - 65, cakes_tool_time - playing.start,
                    pan ? 4 : 1, pan ? 1 : 0.4f, pan);
                base_y = ay + drop.y;
                scale = drop.scale;
                if (pan) v.alpha *= scale;
            } else if (active && tool->station == 0 && part.element == "FallingSprite") {
                if (v.frame >= 2) base_y += cakes_cake_top_y(cakes_tool_before, cakes_tool_replacing ? 1 : 0);
            } else if (active && tool->station == 5 &&
                       (part.element == "StreamSprite" || part.element == "SplatSprite")) {
                // ToolIcing::OnAnimationInt (0x100078690) anchors both effects to Cake+20 - 35.
                base_y = ay + height - 35.0f;
            }
            d.dst = SDL_FRect{base_x + v.dx + shake_dx - (sp.reg_x + part.reg_off_x) * scale,
                              base_y + v.dy - (sp.reg_y + part.reg_off_y) * scale, fw * scale, fh * scale};
            d.alpha = static_cast<int>(std::lround(std::clamp(v.alpha, 0.0f, 255.0f)));
            scene_draws.push_back(d);
            cakes_draw_names.push_back(tool->name + "/" + part.sprite);
        }
    }  // machines
}

}  // namespace pple::viewer
