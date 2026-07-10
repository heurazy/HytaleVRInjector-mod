#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace hytalevr {

constexpr float kPhysicalJumpHeightThreshold = 0.05f;
constexpr float kPhysicalJumpMovementThreshold = 0.10f;
constexpr uint64_t kPhysicalJumpWindowMs = 250;
constexpr float kPhysicalSneakThreshold = 0.40f;
constexpr float kPhysicalSwingTipOffset = 0.30f;
constexpr float kPhysicalSwingSpeedThreshold = 2.50f;
constexpr float kPhysicalSwingRearmSpeed = 1.50f;
constexpr uint64_t kPhysicalSwingWindowMs = 330;

struct PhysicalMotionSample {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    uint64_t timestamp_ms = 0;
};

class PhysicalMotionHistory {
public:
    void clear() {
        count_ = 0;
        next_ = 0;
    }

    void add(float x, float y, float z, uint64_t timestamp_ms) {
        if (count_ > 0) {
            PhysicalMotionSample& newest = sample_from_newest(0);
            if (timestamp_ms <= newest.timestamp_ms) {
                newest = {x, y, z, timestamp_ms};
                return;
            }
        }
        samples_[next_] = {x, y, z, timestamp_ms};
        next_ = (next_ + 1) % samples_.size();
        if (count_ < samples_.size()) ++count_;
    }

    float average_speed(uint64_t now_ms, uint64_t window_ms) const {
        if (count_ < 2) return 0.0f;
        const PhysicalMotionSample* newer = &sample_from_newest(0);
        float speed_total = 0.0f;
        size_t segments = 0;
        for (size_t offset = 1; offset < count_; ++offset) {
            const PhysicalMotionSample& older = sample_from_newest(offset);
            if (now_ms - older.timestamp_ms > window_ms) break;
            const uint64_t elapsed_ms = newer->timestamp_ms - older.timestamp_ms;
            if (elapsed_ms > 0) {
                const float dx = newer->x - older.x;
                const float dy = newer->y - older.y;
                const float dz = newer->z - older.z;
                const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                speed_total += distance / (static_cast<float>(elapsed_ms) * 0.001f);
                ++segments;
            }
            newer = &older;
        }
        return segments > 0 ? speed_total / static_cast<float>(segments) : 0.0f;
    }

    float net_vertical_movement(uint64_t now_ms, uint64_t window_ms) const {
        if (count_ < 2) return 0.0f;
        const PhysicalMotionSample& newest = sample_from_newest(0);
        const PhysicalMotionSample* oldest = nullptr;
        for (size_t offset = 1; offset < count_; ++offset) {
            const PhysicalMotionSample& sample = sample_from_newest(offset);
            if (now_ms - sample.timestamp_ms > window_ms) break;
            oldest = &sample;
        }
        return oldest ? newest.y - oldest->y : 0.0f;
    }

private:
    PhysicalMotionSample& sample_from_newest(size_t offset) {
        const size_t newest = (next_ + samples_.size() - 1) % samples_.size();
        return samples_[(newest + samples_.size() - offset) % samples_.size()];
    }

    const PhysicalMotionSample& sample_from_newest(size_t offset) const {
        const size_t newest = (next_ + samples_.size() - 1) % samples_.size();
        return samples_[(newest + samples_.size() - offset) % samples_.size()];
    }

    std::array<PhysicalMotionSample, 64> samples_{};
    size_t count_ = 0;
    size_t next_ = 0;
};

inline bool physical_jump_requested(float current_height, float standing_height,
                                    float upward_movement) {
    return upward_movement > kPhysicalJumpMovementThreshold &&
           current_height - standing_height > kPhysicalJumpHeightThreshold;
}

inline bool physical_sneak_requested(float current_height, float standing_height) {
    return standing_height - current_height > kPhysicalSneakThreshold;
}

inline bool physical_swing_requested(float average_tip_speed) {
    return average_tip_speed > kPhysicalSwingSpeedThreshold;
}

} // namespace hytalevr
