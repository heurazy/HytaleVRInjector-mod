#pragma once

#include <algorithm>
#include <cmath>

namespace hytalevr {

inline bool is_likely_linear_scene_depth_texture(int width,
                                                 int height,
                                                 int client_width,
                                                 int client_height) {
    if (width < 512 || height < 256 || width > 4096 || height > 4096) {
        return false;
    }

    if (client_width > 0 && client_height > 0) {
        const int tolerance_x = (std::max)(8, client_width / 32);
        const int tolerance_y = (std::max)(8, client_height / 32);
        const bool half_window =
            std::abs(width * 2 - client_width) <= tolerance_x &&
            std::abs(height * 2 - client_height) <= tolerance_y;
        if (half_window) return true;
    }

    const float aspect = static_cast<float>(width) /
        static_cast<float>((std::max)(1, height));
    return aspect >= 1.2f && aspect <= 2.4f;
}

inline int scene_depth_candidate_score(int width,
                                       int height,
                                       int client_width,
                                       int client_height) {
    if (!is_likely_linear_scene_depth_texture(
            width, height, client_width, client_height)) {
        return 0;
    }

    if (client_width > 0 && client_height > 0) {
        const int tolerance_x = (std::max)(8, client_width / 32);
        const int tolerance_y = (std::max)(8, client_height / 32);
        if (std::abs(width * 2 - client_width) <= tolerance_x &&
            std::abs(height * 2 - client_height) <= tolerance_y) {
            return 100;
        }

        const float client_aspect = static_cast<float>(client_width) /
            static_cast<float>((std::max)(1, client_height));
        const float texture_aspect = static_cast<float>(width) /
            static_cast<float>((std::max)(1, height));
        if (width >= 1024 && std::abs(texture_aspect - client_aspect) <= 0.08f) {
            return 50;
        }
    }

    return width >= 1024 ? 25 : 10;
}

} // namespace hytalevr
