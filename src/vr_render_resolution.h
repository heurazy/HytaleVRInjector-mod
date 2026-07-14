#pragma once

#include <algorithm>
#include <cmath>

namespace hytalevr {

struct RenderResolution {
    int width = 0;
    int height = 0;
    float scale = 1.0f;
};

inline RenderResolution scaled_render_resolution(int source_width,
                                                 int source_height,
                                                 float requested_scale,
                                                 int max_texture_size) {
    if (source_width <= 0 || source_height <= 0 || max_texture_size <= 0) return {};

    float scale = std::clamp(requested_scale, 0.5f, 2.0f);
    scale = (std::min)(scale,
                       static_cast<float>(max_texture_size) /
                           static_cast<float>((std::max)(source_width, source_height)));
    scale = (std::max)(scale, 1.0f / static_cast<float>(
        (std::max)(source_width, source_height)));

    return {
        (std::max)(1, static_cast<int>(std::lround(source_width * scale))),
        (std::max)(1, static_cast<int>(std::lround(source_height * scale))),
        scale,
    };
}

} // namespace hytalevr
