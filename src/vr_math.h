#pragma once

#include <cmath>
#include <cstddef>

namespace hytalevr {

struct Matrix4 {
    float value[16]{};
};

inline Matrix4 identity_matrix() {
    Matrix4 result{};
    result.value[0] = 1.0f;
    result.value[5] = 1.0f;
    result.value[10] = 1.0f;
    result.value[15] = 1.0f;
    return result;
}

inline Matrix4 multiply(const Matrix4& a, const Matrix4& b) {
    Matrix4 result{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) {
                result.value[column * 4 + row] +=
                    a.value[k * 4 + row] * b.value[column * 4 + k];
            }
        }
    }
    return result;
}

// Builds a recenter origin that preserves room position and heading while
// keeping pitch and roll tied to SteamVR's Standing tracking space. A recenter
// must never redefine the physical floor from the headset's current tilt.
inline void make_yaw_only_tracking_origin(const float current[12], float origin[12]) {
    for (int index = 0; index < 12; ++index) origin[index] = current[index];

    float forward[3]{
        -current[2],
        0.0f,
        -current[10],
    };
    float length = std::sqrt(forward[0] * forward[0] +
                             forward[2] * forward[2]);
    if (length < 0.0001f) {
        forward[0] = 0.0f;
        forward[2] = -1.0f;
        length = 1.0f;
    }
    forward[0] /= length;
    forward[2] /= length;

    const float right[3]{-forward[2], 0.0f, forward[0]};
    origin[0] = right[0];
    origin[4] = right[1];
    origin[8] = right[2];
    origin[1] = 0.0f;
    origin[5] = 1.0f;
    origin[9] = 0.0f;
    origin[2] = -forward[0];
    origin[6] = 0.0f;
    origin[10] = -forward[2];
}

// Builds the inverse of the headset rotation relative to its recentered pose:
// R_view = R_current^T * R_origin. Inputs are row-major 3x3 device poses and
// the result is a column-major OpenGL matrix.
inline Matrix4 relative_view_rotation(const float origin[9], const float current[9]) {
    Matrix4 result = identity_matrix();
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            float value = 0.0f;
            for (int k = 0; k < 3; ++k) {
                value += current[k * 3 + row] * origin[k * 3 + column];
            }
            result.value[column * 4 + row] = value;
        }
    }
    return result;
}

// Returns inverse(current) * origin for two row-major rigid 3x4 device poses.
// This converts headset motion relative to the recentered pose into the view
// transform that must be prepended to the game's camera.
inline Matrix4 relative_view_pose(const float origin[12], const float current[12],
                                  float horizontal_scale = 1.0f,
                                  float vertical_scale = 1.0f,
                                  bool invert_horizontal = false) {
    Matrix4 result = identity_matrix();
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            float value = 0.0f;
            for (int k = 0; k < 3; ++k) {
                value += current[k * 4 + row] * origin[k * 4 + column];
            }
            result.value[column * 4 + row] = value;
        }
    }

    float translation[3]{};
    for (int row = 0; row < 3; ++row) {
        for (int k = 0; k < 3; ++k) {
            translation[row] += current[k * 4 + row] *
                                (origin[k * 4 + 3] - current[k * 4 + 3]);
        }
    }
    const float horizontal_sign = invert_horizontal ? -1.0f : 1.0f;
    result.value[12] = translation[0] * horizontal_scale * horizontal_sign;
    result.value[13] = translation[1] * vertical_scale;
    result.value[14] = translation[2] * horizontal_scale * horizontal_sign;
    return result;
}

// Extracts a rigid camera pose from a column-major world-to-view matrix.
// Hytale keeps the world origin separate from this matrix, so position is an
// offset relative to the native eye position and forward is the camera -Z axis.
inline void view_pose(const Matrix4& view, float position[3], float forward[3]) {
    const float translation[3]{view.value[12], view.value[13], view.value[14]};
    for (int column = 0; column < 3; ++column) {
        position[column] = 0.0f;
        for (int row = 0; row < 3; ++row) {
            position[column] -= view.value[column * 4 + row] * translation[row];
        }
    }

    forward[0] = -view.value[2];
    forward[1] = -view.value[6];
    forward[2] = -view.value[10];
    const float length = std::sqrt(forward[0] * forward[0] +
                                   forward[1] * forward[1] +
                                   forward[2] * forward[2]);
    if (length > 0.0001f) {
        forward[0] /= length;
        forward[1] /= length;
        forward[2] /= length;
    }
}

// Keeps the native camera's horizontal heading while removing its pitch and
// roll. In VR those two axes must come exclusively from the headset, otherwise
// mouse look rotates the tracked floor plane and produces a tilted horizon.
inline Matrix4 horizontal_view(const Matrix4& view) {
    float camera_position[3]{};
    float forward[3]{};
    view_pose(view, camera_position, forward);
    forward[1] = 0.0f;
    const float horizontal_length = std::sqrt(forward[0] * forward[0] +
                                              forward[2] * forward[2]);
    if (horizontal_length <= 0.0001f) return view;
    forward[0] /= horizontal_length;
    forward[2] /= horizontal_length;

    const float right[3]{-forward[2], 0.0f, forward[0]};
    const float up[3]{0.0f, 1.0f, 0.0f};
    Matrix4 result = identity_matrix();
    for (int column = 0; column < 3; ++column) {
        result.value[column * 4 + 0] = right[column];
        result.value[column * 4 + 1] = up[column];
        result.value[column * 4 + 2] = -forward[column];
    }
    for (int row = 0; row < 3; ++row) {
        result.value[12 + row] =
            -(result.value[0 * 4 + row] * camera_position[0] +
              result.value[1 * 4 + row] * camera_position[1] +
              result.value[2 * 4 + row] * camera_position[2]);
    }
    return result;
}

inline bool nearly_equal(const Matrix4& a, const Matrix4& b, float epsilon = 0.0001f) {
    for (std::size_t i = 0; i < 16; ++i) {
        const float difference = a.value[i] - b.value[i];
        if (difference < -epsilon || difference > epsilon) return false;
    }
    return true;
}

} // namespace hytalevr
