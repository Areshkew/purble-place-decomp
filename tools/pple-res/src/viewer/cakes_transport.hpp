#pragma once
#include "pple/animation.hpp"
#include "pple/cakes.hpp"
#include "pple/scene.hpp"
#include <map>
#include <string>
#include <vector>

namespace pple::viewer {
struct CakeRoomAnimation {
    pple::SceneItem item;
    std::string key;
    pple::AnimationSet animation;
    int draw_at = -1;
    int state = 0;
};
struct CakeBeltMotion {
    int slot = 0;
    float x = 0, y = 0;
    pple::AnimationSet animation;
    // Fit the authored path to the destination slot; never accumulate its shorter displacement.
    float scale_x = 1, scale_y = 1;
};
// App owns these clocks; room rebuilds only replace node descriptions, never active motion.
struct CakeShipment {
    pple::Cake cake;
    bool correct = false;
    float x = 0, y = 0;
};
struct CakeTransport {
    std::vector<CakeRoomAnimation> nodes;
    std::map<std::string, pple::AnimationSet> animations;
    std::vector<CakeBeltMotion> motion;
    std::vector<std::pair<float, float>> positions;
    float belt_time = -1, belt_total = 0;
    float inlet_time = -1; // new paper's chute motion is independent of existing belt cakes
    bool reverse = false, judge_after_slide = false;
    // Rejection flash starts at judging, independently of queued gift dispatch.
    float readout_time = -1;
    int readout_slot = -1;
    float shipping_time = -1;
    bool correct = false;
    pple::Cake shipping_cake;
    float shipping_x = 0, shipping_y = 0;
    std::vector<CakeShipment> shipping_pending;
};
}
