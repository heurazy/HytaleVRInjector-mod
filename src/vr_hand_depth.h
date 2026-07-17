#pragma once

#include <algorithm>

namespace hytalevr {

inline constexpr float kMinimumInverseHandDepth = 0.0001f;
inline constexpr float kMinimumValidSceneDepth = 0.001f;

inline float linear_scene_depth(float normalized_depth, float far_clip) {
    return normalized_depth * far_clip;
}

inline float linear_hand_depth(float inverse_depth) {
    return 1.0f / (std::max)(inverse_depth, kMinimumInverseHandDepth);
}

inline bool hand_is_occluded(float normalized_scene_depth,
                             float far_clip,
                             float inverse_hand_depth,
                             float depth_bias) {
    const float scene_depth = linear_scene_depth(normalized_scene_depth, far_clip);
    const float hand_depth = linear_hand_depth(inverse_hand_depth);
    return scene_depth > kMinimumValidSceneDepth &&
           hand_depth > scene_depth + depth_bias;
}

// Both values are linear view-space distances. No model-specific scale factor is
// needed; uDepthBias is the only user-facing tolerance.
inline constexpr char kHandDepthFragmentShader[] = R"GLSL(
#version 130
uniform sampler2D uTexture;
uniform sampler2D uSceneDepth;
uniform int uUseSceneDepth;
uniform vec2 uViewportSize;
uniform float uDepthFarClip;
uniform float uDepthBias;
in vec2 vUvOverDepth;
in float vShade;
in float vInverseDepth;
out vec4 fragColor;
void main() {
    if (uUseSceneDepth != 0 && uViewportSize.x > 0.5 && uViewportSize.y > 0.5) {
        vec2 depthUv = gl_FragCoord.xy / uViewportSize;
        float sceneDepth = texture(uSceneDepth, depthUv).r * uDepthFarClip;
        float handDepth = 1.0 / max(vInverseDepth, 0.0001);
        if (sceneDepth > 0.001 && handDepth > sceneDepth + uDepthBias) {
            discard;
        }
    }
    vec2 perspectiveUv =
        vUvOverDepth / max(vInverseDepth, 0.0001);
    vec4 texel = texture(uTexture, perspectiveUv);
    if (texel.a < 0.01) {
        discard;
    }
    float overlayDepth = 1.0 / max(vInverseDepth, 0.0001);
    gl_FragDepth = clamp(overlayDepth / 16.0, 0.0, 0.999999);
    vec3 color = texel.rgb * clamp(vShade, 0.35, 1.08);
    fragColor = vec4(color, texel.a);
}
)GLSL";

} // namespace hytalevr
