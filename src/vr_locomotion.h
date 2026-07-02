#pragma once

#include <algorithm>

namespace hytalevr {

struct DigitalStickState {
    bool left = false;
    bool right = false;
    bool forward = false;
    bool backward = false;
};

inline bool latched_axis(float value, bool active, float press_threshold) {
    const float press = std::clamp(press_threshold, 0.05f, 0.95f);
    const float release = press * 0.65f;
    return value >= (active ? release : press);
}

inline DigitalStickState digital_stick(float x, float y,
                                       const DigitalStickState& previous,
                                       float press_threshold) {
    DigitalStickState result{};
    result.right = latched_axis(x, previous.right, press_threshold);
    result.left = latched_axis(-x, previous.left, press_threshold);
    result.forward = latched_axis(y, previous.forward, press_threshold);
    result.backward = latched_axis(-y, previous.backward, press_threshold);
    return result;
}

} // namespace hytalevr
