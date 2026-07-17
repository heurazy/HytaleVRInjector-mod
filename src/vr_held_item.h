#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>

namespace hytalevr {

constexpr int kHeldItemLeft = 0;
constexpr int kHeldItemRight = 1;
constexpr float kHeldItemMinAnchorDistance = 0.20f;

inline bool held_item_was_present_before_attack(uint32_t stable_presence_mask,
                                                int side) {
    return side >= kHeldItemLeft && side <= kHeldItemRight &&
           (stable_presence_mask & (1u << side)) != 0u;
}

struct HeldItemUvBounds {
    uint32_t min_u = (std::numeric_limits<uint32_t>::max)();
    uint32_t min_v = (std::numeric_limits<uint32_t>::max)();
    uint32_t max_u = 0;
    uint32_t max_v = 0;
    bool valid = false;
};

inline void include_held_item_packed_uv(uint32_t packed,
                                        HeldItemUvBounds& bounds) {
    const uint32_t u = packed & 0xffffu;
    const uint32_t v = (packed >> 16u) & 0xffffu;
    bounds.min_u = (std::min)(bounds.min_u, u);
    bounds.min_v = (std::min)(bounds.min_v, v);
    bounds.max_u = (std::max)(bounds.max_u, u);
    bounds.max_v = (std::max)(bounds.max_v, v);
    bounds.valid = true;
}

inline bool is_held_item_visual_program_label(std::string_view label) {
    return label.find("FirstPersonBlockyModelProgram") != std::string_view::npos;
}

inline bool finite_values(const float* values, int count) {
    if (!values || count <= 0) return false;
    for (int index = 0; index < count; ++index) {
        if (!std::isfinite(values[index])) return false;
    }
    return true;
}

// OpenVR poses are row-major 3x4 transforms from local tracking space to the
// standing tracking space. Convert a controller to the headset's view space.
inline bool controller_pose_relative_to_hmd(const float hmd[12],
                                            const float controller[12],
                                            float world_scale,
                                            float out[12]) {
    if (!finite_values(hmd, 12) || !finite_values(controller, 12) || !out) {
        return false;
    }
    world_scale = std::clamp(world_scale, 0.25f, 4.0f);

    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            float value = 0.0f;
            for (int axis = 0; axis < 3; ++axis) {
                value += hmd[axis * 4 + row] * controller[axis * 4 + column];
            }
            out[row * 4 + column] = value;
        }

        float translation = 0.0f;
        for (int axis = 0; axis < 3; ++axis) {
            translation += hmd[axis * 4 + row] *
                (controller[axis * 4 + 3] - hmd[axis * 4 + 3]);
        }
        out[row * 4 + 3] = translation * world_scale;
    }
    return finite_values(out, 12);
}

inline int held_item_side_from_model_matrix(const float model[16]) {
    if (!model || !std::isfinite(model[12])) return kHeldItemRight;
    return model[12] < 0.0f ? kHeldItemLeft : kHeldItemRight;
}

// Hytale submits its first-person arm through the same BlockyModel program as
// held objects. The arm matrix is centered at the origin, while an item has a
// translated viewmodel anchor (roughly one metre from the source camera).
inline bool held_item_model_matrix_has_anchor(const float model[16]) {
    if (!model || !std::isfinite(model[12]) || !std::isfinite(model[13]) ||
        !std::isfinite(model[14])) {
        return false;
    }
    const float distance_squared = model[12] * model[12] +
                                   model[13] * model[13] +
                                   model[14] * model[14];
    return distance_squared >=
           kHeldItemMinAnchorDistance * kHeldItemMinAnchorDistance;
}

// Hytale uploads uModelMatrix as an OpenGL column-major matrix. Preserve the
// item's own grip transform/animation, then apply the tracked controller pose.
inline bool compose_held_item_model_matrix(const float original[16],
                                           const float controller_view[12],
                                           float world_scale,
                                           const float local_offset[3],
                                           float visual_scale,
                                           float out[16]) {
    if (!finite_values(original, 16) || !finite_values(controller_view, 12) ||
        !finite_values(local_offset, 3) || !std::isfinite(visual_scale) || !out) {
        return false;
    }
    if (!held_item_model_matrix_has_anchor(original)) return false;
    if (std::fabs(original[15] - 1.0f) > 0.25f) return false;

    world_scale = std::clamp(world_scale, 0.25f, 4.0f);
    visual_scale = std::clamp(visual_scale, 0.10f, 2.0f);
    std::memcpy(out, original, sizeof(float) * 16);

    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            float value = 0.0f;
            for (int axis = 0; axis < 3; ++axis) {
                const float controller_rotation = controller_view[row * 4 + axis];
                const float item_rotation_scale = original[column * 4 + axis];
                value += controller_rotation * item_rotation_scale;
            }
            // Hytale's first-person matrix already contains the item's intended
            // size. World scale affects its tracked position, not its mesh a
            // second time. A small grip scale keeps blocks and tools natural
            // beside the custom hand model.
            out[column * 4 + row] = value * visual_scale;
        }
    }

    // Move the item from the controller origin toward the trigger fingers.
    // OpenVR controllers point forward along their negative Z basis.
    for (int row = 0; row < 3; ++row) {
        float translation = controller_view[row * 4 + 3];
        for (int axis = 0; axis < 3; ++axis) {
            translation += controller_view[row * 4 + axis] *
                local_offset[axis] * world_scale;
        }
        out[12 + row] = translation;
    }
    out[3] = 0.0f;
    out[7] = 0.0f;
    out[11] = 0.0f;
    out[15] = 1.0f;
    return finite_values(out, 16);
}

} // namespace hytalevr
