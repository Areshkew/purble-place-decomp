#include "app.hpp"
#include <algorithm>
#include <cstdio>

namespace pple::viewer {
namespace {
std::string key_of(const pple::SceneItem& item) {
    return item.name.empty() ? item.instance : item.name;
}
}
void App::cakes_transport_load() {
    if (!cakes_transport.animations.empty()) return;
    static const char* names[] = {"MainConveyorBelt", "MainConveyorBeltEnd", "LeftConveyorBelt",
        "RightConveyorBelt", "ConveyorPlatform", "CakeSlideLeft", "CakeSlideRight",
        "CakeSlideDownLeft", "CakeSlideUpRight", "CakeMoveToBox", "CakeMoveToTrash",
        "CakeBox", "BoxDrop", "BoxBounce", "RedLight", "ShippingSignDelivery",
        "ShippingSignNoDelivery", "ScreenSlot", "CakeFadeOut", "TrashCake"};
    for (const auto* name : names) {
        const std::string resource = std::string("UI\\COMFYCAKES\\") + name + "Anim.xml";
        std::string how;
        const auto* slot = bundle.resolve(resource, how);
        pple::AnimationSet animation;
        if (slot && pple::animation_parse(bundle.read(*slot), animation))
            cakes_transport.animations[name] = std::move(animation);
        else std::fprintf(stderr, "cakes transport: missing animation %s\n", resource.c_str());
    }
}
bool App::cakes_transport_node(const pple::SceneItem& item, int state) {
    const std::string key = key_of(item);
    std::string anim;
    if (key == "MainBeltSprite") anim = state == 1 ? "MainConveyorBeltEnd" : "MainConveyorBelt";
    else if (key == "LeftBeltSprite") anim = "LeftConveyorBelt";
    else if (key == "RightBelt") anim = "RightConveyorBelt";
    else if (key == "ConveyorPlatform") anim = "ConveyorPlatform";
    else if (key == "DroppingBox") anim = "BoxDrop";
    else if (key == "CakeBox") anim = "CakeBox";
    else if (key == "RedLight") anim = "RedLight";
    else if (key == "TrashCan") anim = "TrashCake";
    else if (key == "ShippingSign") anim = "ShippingSignNoDelivery";
    else if (key == "ScreenSlot") anim = "ScreenSlot";
    else return false;
    const bool foreground = key == "CakeBox" || key == "DroppingBox";
    cakes_transport.nodes.push_back({item, key, cakes_transport.animations[anim],
        foreground ? -1 : static_cast<int>(scene_draws.size()), state});
    return foreground; // inactive gift props have no static draw owner
}
void App::cakes_transport_position(int slot, float& x, float& y) const {
    pple::CakeModel::belt_position(slot, x, y);
    if (slot >= 0 && static_cast<std::size_t>(slot) < cakes_transport.positions.size()) {
        x = cakes_transport.positions[slot].first;
        y = cakes_transport.positions[slot].second;
    }
    if (cakes_transport.belt_time < 0) return;
    for (const auto& motion : cakes_transport.motion) {
        if (motion.slot != slot) continue;
        const auto value = pple::animation_value(motion.animation, cakes_transport.belt_time);
        x = motion.x + value.dx * motion.scale_x;
        y = motion.y + value.dy * motion.scale_y;
        return;
    }
}
void App::cakes_transport_finish_slide() {
    if (cakes_transport.belt_time < 0) return;
    for (const auto& motion : cakes_transport.motion) {
        const auto value = pple::animation_value(motion.animation, motion.animation.total);
        cakes_transport.positions[motion.slot] = {
            motion.x + value.dx * motion.scale_x, motion.y + value.dy * motion.scale_y};
    }
    cakes_transport.belt_time = -1;
    cakes_transport.motion.clear();
    const bool judge = cakes_transport.judge_after_slide;
    cakes_transport.judge_after_slide = false;
    if (judge) cakes_judge();
}
void App::cakes_transport_slide(int direction) {
    cakes_transport.motion.clear();
    cakes_transport.reverse = direction < 0;
    cakes_transport.belt_time = 0;
    cakes_transport.belt_total = cakes_transport.animations["MainConveyorBelt"].total;
    if (cakes_transport.belt_total <= 0) cakes_transport.belt_total = 0.5f;
    for (std::size_t slot = 0; slot < cakes_belt.size(); ++slot) {
        if (!cakes_belt_present[slot]) continue;
        const int from = static_cast<int>(slot) - direction;
        float x, y;
        x = cakes_transport.positions[from].first;
        y = cakes_transport.positions[from].second;
        const std::string anim = from == 0 ? "CakeSlideDownLeft" : slot == 0 ? "CakeSlideUpRight"
            : direction > 0 ? "CakeSlideRight" : "CakeSlideLeft";
        CakeBeltMotion motion{static_cast<int>(slot), x, y, cakes_transport.animations[anim]};
        float target_x, target_y;
        pple::CakeModel::belt_position(static_cast<int>(slot), target_x, target_y);
        const auto end = pple::animation_value(motion.animation, motion.animation.total);
        // The resource moves 92px, but the slot grid is 98px. Retaining the raw
        // endpoint puts cakes increasingly left of the machines. Fit each axis
        // to the slot while preserving the resource's timing and continuous path.
        // This also brings replacement cakes from the chute onto the same grid.
        if (end.dx != 0) motion.scale_x = (target_x - x) / end.dx;
        if (end.dy != 0) motion.scale_y = (target_y - y) / end.dy;
        cakes_transport.motion.push_back(std::move(motion));
    }
    cakes_transport.judge_after_slide = direction > 0 && cakes_belt_present.back();
}
void App::cakes_transport_ship(const pple::Cake& cake, bool correct, float x, float y) {
    if (cakes_transport.shipping_time >= 0) {
        cakes_transport.shipping_pending.push_back({cake, correct, x, y});
        std::printf("cakes dispatch: queued, %zu pending\n", cakes_transport.shipping_pending.size());
        return;
    }
    cakes_transport.shipping_cake = cake;
    cakes_transport.shipping_x = x; cakes_transport.shipping_y = y;
    cakes_transport.correct = correct;
    cakes_transport.shipping_time = 0;
    std::printf("cakes dispatch: %s, %s\n", correct ? "correct" : "wrong",
        correct ? "box drop -> wrapped gift -> shipping" : "red light -> trash");
}
bool App::cakes_transport_advance(float dt) {
    const bool moved = cakes_transport.belt_time >= 0 || cakes_transport.shipping_time >= 0 ||
                       cakes_transport.readout_time >= 0;
    if (cakes_transport.readout_time >= 0) {
        cakes_transport.readout_time += dt;
        if (cakes_transport.readout_time >= cakes_transport.animations["ScreenSlot"].total)
            cakes_transport.readout_time = -1;
    }
    if (cakes_transport.shipping_time >= 0) {
        const float old = cakes_transport.shipping_time;
        cakes_transport.shipping_time += dt;
        const float wrap = cakes_transport.animations["BoxDrop"].total;
        const float end = cakes_transport.correct ? wrap + std::max(cakes_transport.animations["CakeBox"].total,
                cakes_transport.animations["ShippingSignDelivery"].total)
            : std::max({cakes_transport.animations["TrashCake"].total, cakes_transport.animations["RedLight"].total,
                cakes_transport.animations["ScreenSlot"].total});
        if (cakes_transport.correct && old < wrap && cakes_transport.shipping_time >= wrap)
            std::printf("cakes dispatch: wrapped gift shipping\n");
        if (cakes_transport.shipping_time >= end) {
            std::printf("cakes dispatch: complete\n");
            cakes_transport.shipping_time = -1;
            cakes_transport.shipping_cake = {};
            if (!cakes_transport.shipping_pending.empty()) {
                const auto next = cakes_transport.shipping_pending.front();
                cakes_transport.shipping_pending.erase(cakes_transport.shipping_pending.begin());
                cakes_transport_ship(next.cake, next.correct, next.x, next.y);
            }
        }
    }
    if (cakes_transport.belt_time >= 0) {
        cakes_transport.belt_time += dt;
        if (cakes_transport.belt_time >= cakes_transport.belt_total) cakes_transport_finish_slide();
    }
    return moved;
}
void App::cakes_transport_draw(bool foreground) {
    const float shipping = cakes_transport.shipping_time;
    const bool dispatch = shipping >= 0;
    const float wrap = cakes_transport.animations["BoxDrop"].total;
    if (foreground && dispatch && (!cakes_transport.correct || shipping < wrap)) {
        const auto& anim = cakes_transport.animations[cakes_transport.correct ? "CakeFadeOut" : "CakeMoveToTrash"];
        const auto value = pple::animation_value(anim, shipping);
        const auto begin = scene_draws.size();
        cakes_draw_cake(cakes_transport.shipping_cake, cakes_transport.shipping_x + value.dx,
            cakes_transport.shipping_y + value.dy);
        for (std::size_t i = begin; i < scene_draws.size(); ++i)
            scene_draws[i].alpha = static_cast<int>(value.alpha);
    }
    int readout = 0;
    for (const auto& node : cakes_transport.nodes) {
        if ((node.draw_at < 0) != foreground) continue;
        pple::SceneItem item = node.item;
        int state = node.state, frame = 0;
        if (node.key == "CakeBox") {
            if (!dispatch || !cakes_transport.correct || shipping < wrap || shipping >= wrap + node.animation.total) continue;
            const auto value = pple::animation_value(node.animation, shipping - wrap);
            item.x += value.dx; item.y += value.dy;
        } else if (node.key == "DroppingBox") {
            if (!dispatch || !cakes_transport.correct || shipping >= wrap) continue;
            const auto value = pple::animation_value(node.animation, shipping);
            frame = static_cast<int>(value.frame);
            state = 0;
            for (const auto& cue : node.animation.cues)
                if (cue.complete && shipping >= cue.time) state = cue.value;
        } else if (node.key == "ScreenSlot") {
            // ScreenSlotSprite has ONE animation with three FRAMES. Selecting
            // animation 1/2 silently resolves animation 0 and leaves the dash unchanged.
            // InitGameUI (0x10005D018) starts slots transparent; OnCakeDone
            // (0x10005C120) makes frame 2 permanent for a success, or blinks frame 1
            // on the next unfilled slot for a rejection. Unfilled slots stay blank.
            state = 0;
            frame = readout < cakes_correct ? 2 : 0;
            item.alpha = readout < cakes_correct ? 255 : 0;
            if (readout >= cakes_correct && readout == cakes_transport.readout_slot &&
                cakes_transport.readout_time >= 0) {
                frame = 1;
                item.alpha = static_cast<int>(pple::animation_value(node.animation, cakes_transport.readout_time).alpha);
            }
            ++readout;
        } else if (node.key == "ShippingSign") {
            const bool delivery = dispatch && cakes_transport.correct && shipping >= wrap;
            const auto& anim = cakes_transport.animations[delivery ? "ShippingSignDelivery" : "ShippingSignNoDelivery"];
            state = delivery ? 1 : 0;
            frame = static_cast<int>(pple::animation_value(anim, delivery ? shipping - wrap : anim.total).frame);
        } else if (node.key == "TrashCan") {
            if (dispatch && !cakes_transport.correct) {
                const auto value = pple::animation_value(node.animation, shipping);
                frame = static_cast<int>(value.frame);
                for (const auto& cue : node.animation.cues)
                    if (cue.complete && shipping >= cue.time) state = cue.value;
            }
        } else if (node.key == "RedLight") {
            if (dispatch && !cakes_transport.correct)
                frame = static_cast<int>(pple::animation_value(node.animation, shipping).frame);
        } else if (node.key == "ConveyorPlatform") {
            // This is the dispatch platform, not a main-belt roller. Belt slides and
            // replacement chute motion must not start/reset its frames on every update.
            if (dispatch && cakes_transport.correct && shipping < node.animation.total)
                frame = static_cast<int>(pple::animation_value(node.animation, shipping).frame);
        } else if (node.key == "RightBelt") {
            // OnAnimationComplete(BoxDrop), 0x10005F2E0, resumes the shipping belt.
            if (dispatch && cakes_transport.correct && shipping >= wrap &&
                shipping < wrap + node.animation.total)
                frame = static_cast<int>(pple::animation_value(node.animation, shipping - wrap).frame);
        } else if (cakes_transport.belt_time >= 0) {
            frame = static_cast<int>(pple::animation_value(node.animation, cakes_transport.belt_time,
                cakes_transport.reverse).frame);
        }
        const std::size_t at = scene_draws.size();
        if (!cakes_append(item, state, frame)) continue;
        if (node.draw_at >= 0 && static_cast<std::size_t>(node.draw_at) < at) {
            scene_draws[static_cast<std::size_t>(node.draw_at)] = scene_draws.back();
            scene_draws.pop_back(); cakes_draw_names.pop_back();
        }
    }
    // Animated offscreen parts must not alter viewport sizing.
    scene_w = pple::kScreenWidth; scene_h = pple::kScreenHeight;
}
void App::cakes_next_target() {
    const pple::Cake previous = cakes_target;
    for (int tries = 0; tries < 32; ++tries) {
        cakes_target = cakes_model.generate_correct(cakes_difficulty, [] { return std::rand(); });
        const bool same_visible_order = cakes_target.layers.size() == previous.layers.size() &&
            std::equal(cakes_target.layers.begin(), cakes_target.layers.end(), previous.layers.begin(),
                [](const auto& a, const auto& b) { return a.station == b.station && a.index == b.index && a.shape == b.shape; });
        if (!same_visible_order) break;
    }
    cakes_matched_seen = false;
    std::printf("cakes: next target cake, %zu layer(s)\n", cakes_target.layers.size());
    for (const auto& layer : cakes_target.layers)
        std::printf("  target station %d, LAYERS %d, shape %d\n", layer.station, layer.index, layer.shape);
}
}
