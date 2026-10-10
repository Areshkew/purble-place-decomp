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

// One <Animation> block of a ResourceAnimation.  The engine's <Affects> names the property the
// block drives and <Type Name="LinearAbsolute"> carries <Start>/<End> where
// <Type Name="LinearRelative"> carries <Count>; both carry <Time> in seconds.
bool read_run(const pple::XmlNode& anim, CakeAnimRun& run) {
    const pple::XmlNode* affects = child_ci(anim, "Affects");
    if (affects == nullptr) return false;
    const std::string& what = affects->text;
    if (what == "CurrentFrame") run.affects = CakeAnimRun::Affects::Frame;
    else if (what == "XPosition") run.affects = CakeAnimRun::Affects::X;
    else if (what == "YPosition") run.affects = CakeAnimRun::Affects::Y;
    else if (what == "Alpha") run.affects = CakeAnimRun::Affects::Alpha;
    else if (what == "Position") run.affects = CakeAnimRun::Affects::Position;
    else return false;  // the belt's <IntMessage>-only blocks drive nothing this draws
    const pple::XmlNode* type = child_ci(anim, "Type");
    if (type == nullptr) return false;
    run.seconds = num_ci(*type, "Time", 0.0f);
    // The engine spells the attribute "Name"; XmlNode::attr is case-sensitive, so look it up the
    // way the rest of this module does.
    std::string kind;
    for (const pple::XmlAttribute& attribute : type->attributes)
        if (attribute.name == "Name" || attribute.name == "name") kind = attribute.value;
    run.relative = kind == "LinearRelative";
    if (run.relative) {
        run.from = 0.0f;
        run.to = num_ci(*type, "Count", 0.0f);
    } else {
        run.from = num_ci(*type, "Start", 0.0f);
        run.to = num_ci(*type, "End", 0.0f);
    }
    return true;
}

// A whole ResourceAnimation document: its blocks, concatenated, and their total duration.
bool read_anim(const std::vector<std::uint8_t>& bytes, CakeAnimSet& out) {
    pple::XmlNode root;
    std::string error;
    if (!pple::xml_parse(bytes, root, error)) return false;
    for (const pple::XmlNode& anim : root.children) {
        if (anim.name != "Animation") continue;
        CakeAnimRun run;
        if (!read_run(anim, run)) continue;
        out.total += run.seconds;
        out.runs.push_back(run);
    }
    return !out.runs.empty();
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
struct AnimValue {
    float frame = 0.0f;
    float dx = 0.0f, dy = 0.0f;
    float alpha = 255.0f;
};

AnimValue anim_value(const CakeAnimSet& set, float t, bool reverse) {
    AnimValue v;
    float at = 0.0f;
    if (reverse) t = set.total - t;
    for (const CakeAnimRun& run : set.runs) {
        const float end = at + run.seconds;
        if (t < at) break;  // the block has not started yet, so it contributes nothing
        float value = run.to;  // past the block's end: hold its end value
        if (t < end) {
            const float k = run.seconds > 0.0f ? (t - at) / run.seconds : 1.0f;
            value = run.from + (run.to - run.from) * std::clamp(k, 0.0f, 1.0f);
        }
        switch (run.affects) {
            case CakeAnimRun::Affects::Frame: v.frame = value; break;
            case CakeAnimRun::Affects::Alpha: v.alpha = value; break;
            case CakeAnimRun::Affects::X: v.dx = run.relative ? v.dx + value : value; break;
            case CakeAnimRun::Affects::Y: v.dy = run.relative ? v.dy + value : value; break;
            case CakeAnimRun::Affects::Position: v.dx += value; v.dy += value; break;
        }
        at = end;
    }
    return v;
}

}  // namespace

int App::cakes_tool_slot_for(int station) {
    for (const ToolEntry& tool : kTools)
        if (tool.station == station) return tool.slot;
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
                part.x = num_ci(*sprite, "X", 0.0f);
                part.y = num_ci(*sprite, "Y", 0.0f);
                part.index_from = spec.index_from;
                part.type_base = spec.type_base;
                part.rides_the_cake = spec.rides_the_cake;
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
    cakes_tool_time = 0.0f;
    cakes_tool_playing.clear();
    cakes_tool_total = 0.6f;  // the fallback when the machine's animations did not load
    if (tool == nullptr) return;
    for (const CakeToolPart& part : tool->parts) {
        if (part.anims.empty()) continue;
        bool reverse = false;
        const std::size_t index = cakes_tool_flavour(part, type, shape, reverse);
        cakes_tool_playing.push_back({&part, index, reverse});
        cakes_tool_total = std::max(cakes_tool_total, part.anims[index].total);
    }
    std::printf("cakes tool: %s plays for %.2f s (%zu part(s))\n", tool->name.c_str(),
                static_cast<double>(cakes_tool_total), cakes_tool_playing.size());
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
// its own container offset plus the animation's own positional run, using the frame the
// animation's CurrentFrame run names -- which is why the frame count passed to the geometry is
// the animation's reach, not the sprite's declared FrameCount.
void App::cakes_tools_draw() {
    if (!cakes_tool_busy && cakes_tool_shake < 0.0f) return;
    const CakeTool* tool = cakes_tool_for_station(cakes_tool_busy ? cakes_tool_station_running
                                                                 : cakes_tool_shake_station);
    if (tool == nullptr) return;
    const auto anchor = cakes_tool_anchor.find(tool->name);
    const float ax = anchor == cakes_tool_anchor.end() ? 0.0f : anchor->second.first;
    const float ay = anchor == cakes_tool_anchor.end() ? 0.0f : anchor->second.second;
    // Where the cake under this machine sits, for the parts that ride it.  The belt container is
    // at (0,220) and ConveyorBelt::SetCakePosition works inside it.
    const int slot = cakes_tool_slot_running;
    float belt_x = 0.0f, belt_y = 0.0f;
    pple::CakeModel::belt_position(slot, belt_x, belt_y);
    const float cake_x = belt_x;
    const float cake_y = 220.0f + belt_y + cakes_cake_top_y(cakes_belt[static_cast<std::size_t>(
                                                       std::clamp(slot, 0, static_cast<int>(cakes_belt.size()) - 1))], 1);
    // A refusal shakes the whole machine, which is the machine Base the animation is attached to
    // in Tool::Init.
    float shake_dx = 0.0f;
    if (cakes_tool_shake >= 0.0f) {
        const AnimValue v = anim_value(tool->shake, cakes_tool_shake, false);
        shake_dx = v.dx;
    }
    for (const CakeToolPlaying& playing : cakes_tool_playing) {
        const CakeToolPart& part = *playing.part;
        const CakeAnimSet& set = part.anims[playing.index];
        const AnimValue v = anim_value(set, cakes_tool_time, playing.reverse);
        const viewer::Sprite sp =
            resolve_sprite(bundle, part.sprite, static_cast<int>(playing.index));
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
        // The animation names frames beyond the sprite's declared FrameCount for several of the
        // machines, so the reach is the animation's own.
        geometry.frames = std::max(sp.frames, static_cast<int>(v.frame) + 1);
        geometry.per_row = sp.per_row;
        geometry.prepped = sp.prepped;
        float fx = 0.0f, fy = 0.0f, fw = 0.0f, fh = 0.0f;
        pple::sprite_frame_rect(geometry, static_cast<int>(v.frame), fx, fy, fw, fh);
        DrawItem d;
        d.texture = tex;
        d.src = SDL_FRect{fx, fy, fw, fh};
        const float base_x = part.rides_the_cake ? cake_x : ax + part.x;
        const float base_y = part.rides_the_cake ? cake_y : ay + part.y;
        d.dst = SDL_FRect{base_x + v.dx + shake_dx - (sp.reg_x + part.reg_off_x),
                          base_y + v.dy - (sp.reg_y + part.reg_off_y), fw, fh};
        d.alpha = static_cast<int>(std::lround(std::clamp(v.alpha, 0.0f, 255.0f)));
        scene_draws.push_back(d);
        cakes_draw_names.push_back(tool->name + "/" + part.sprite);
    }
    if (cakes_tool_shake >= 0.0f && cakes_tool_playing.empty()) {
        // The idle machine parts shake with it, so the whole Base is redrawn once more, offset.
        for (const CakeToolPart& part : tool->parts) {
            const viewer::Sprite sp = resolve_sprite(bundle, part.sprite, 0);
            if (!sp.ok) continue;
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
            geometry.frames = sp.frames;
            geometry.per_row = sp.per_row;
            geometry.prepped = sp.prepped;
            float fx = 0.0f, fy = 0.0f, fw = 0.0f, fh = 0.0f;
            pple::sprite_frame_rect(geometry, 0, fx, fy, fw, fh);
            DrawItem d;
            d.texture = tex;
            d.src = SDL_FRect{fx, fy, fw, fh};
            d.dst = SDL_FRect{ax + part.x + shake_dx - sp.reg_x, ay + part.y - sp.reg_y, fw, fh};
            scene_draws.push_back(d);
            cakes_draw_names.push_back(tool->name + "/" + part.sprite);
        }
    }
}

}  // namespace pple::viewer
