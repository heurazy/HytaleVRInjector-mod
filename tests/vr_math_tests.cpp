#include "dashboard_key_bindings.h"
#include "ui_scene_depth_policy.h"
#include "vr_camera_shared.h"
#include "vr_math.h"
#include "vr_hand_depth.h"
#include "vr_locomotion.h"
#include "vr_physical_interactions.h"
#include "vr_render_resolution.h"
#include "vr_hook_validation.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>

namespace {

bool check(bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

} // namespace

int main() {
    using namespace hytalevr;
    bool success = true;

    static_assert(sizeof(VrCameraControls) == 160,
                  "VrCameraControls ABI changed; bump the shared mapping version");
    const VrCameraControls default_controls{};
    success &= check(std::fabs(default_controls.vr_resolution_scale - 1.0f) < 0.0001f,
                     "VR resolution must default to the native AFW output size");

    const RenderResolution resolution_150 =
        scaled_render_resolution(1920, 1080, 1.5f, 8192);
    success &= check(resolution_150.width == 2880 && resolution_150.height == 1620 &&
                         std::fabs(resolution_150.scale - 1.5f) < 0.0001f,
                     "VR resolution scaling must preserve the source aspect ratio");
    const RenderResolution resolution_clamped =
        scaled_render_resolution(1920, 1080, 2.0f, 2048);
    success &= check(resolution_clamped.width == 2048 && resolution_clamped.height == 1152,
                     "VR resolution scaling must respect the OpenGL texture limit");

    const std::array<unsigned char, 16> camera_hook_site{
        0x0F, 0x28, 0xB4, 0x24, 0x80, 0x03, 0x00, 0x00,
        0x0F, 0x28, 0xBC, 0x24, 0x70, 0x03, 0x00, 0x00,
    };
    success &= check(camera_hook_site_valid(camera_hook_site.data()),
                     "known camera hook prologue was rejected");
    auto invalid_camera_hook_site = camera_hook_site;
    invalid_camera_hook_site[12] = 0x60;
    success &= check(!camera_hook_site_valid(invalid_camera_hook_site.data()),
                     "camera hook accepted inconsistent stack offsets");

    const std::array<unsigned char, 15> interaction_hook_site{
        0x0F, 0x29, 0xBD, 0x20, 0xFE, 0xFF, 0xFF,
        0x44, 0x0F, 0x29, 0x85, 0x10, 0xFE, 0xFF, 0xFF,
    };
    success &= check(interaction_hook_site_valid(interaction_hook_site.data()),
                     "known interaction hook prologue was rejected");
    auto invalid_interaction_hook_site = interaction_hook_site;
    invalid_interaction_hook_site[11] = 0x00;
    success &= check(!interaction_hook_site_valid(invalid_interaction_hook_site.data()),
                     "interaction hook accepted inconsistent stack offsets");

    std::array<unsigned char, 16> owned_jump{};
    owned_jump.fill(0x90);
    owned_jump[0] = 0x48;
    owned_jump[1] = 0xB8;
    const uintptr_t jump_destination = static_cast<uintptr_t>(0x12345678u);
    std::memcpy(owned_jump.data() + 2, &jump_destination, sizeof(jump_destination));
    owned_jump[10] = 0xFF;
    owned_jump[11] = 0xE0;
    success &= check(absolute_jump_patch_matches(
                         owned_jump.data(), owned_jump.size(),
                         reinterpret_cast<const void*>(jump_destination)),
                     "owned absolute jump was rejected");
    owned_jump.back() = 0xCC;
    success &= check(!absolute_jump_patch_matches(
                         owned_jump.data(), owned_jump.size(),
                         reinterpret_cast<const void*>(jump_destination)),
                     "modified absolute jump was accepted as owned");

    const Matrix4 identity = identity_matrix();
    success &= check(nearly_equal(multiply(identity, identity), identity),
                     "identity multiplication failed");

    Matrix4 invertible = identity;
    invertible.value[0] = 1.25f;
    invertible.value[5] = 0.75f;
    invertible.value[10] = -1.1f;
    invertible.value[12] = 2.0f;
    invertible.value[13] = -3.0f;
    invertible.value[14] = 0.5f;
    Matrix4 inverted{};
    success &= check(inverse(invertible, inverted) &&
                         nearly_equal(multiply(invertible, inverted), identity),
                     "general matrix inversion failed");

    const float origin[9]{
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f,
    };
    success &= check(nearly_equal(relative_view_rotation(origin, origin), identity),
                     "recentered pose must produce identity");

    constexpr float half_sqrt_three = 0.8660254037844386f;
    const float pitched_recenter_pose[12]{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, half_sqrt_three, -0.5f, 1.6f,
        0.0f, 0.5f, half_sqrt_three, 0.0f,
    };
    float standing_origin[12]{};
    make_yaw_only_tracking_origin(pitched_recenter_pose, standing_origin);
    success &= check(std::fabs(standing_origin[1]) < 0.0001f &&
                         std::fabs(standing_origin[5] - 1.0f) < 0.0001f &&
                         std::fabs(standing_origin[9]) < 0.0001f,
                     "recenter origin must keep SteamVR's physical up axis");
    const Matrix4 standing_pitch_view =
        relative_view_pose(standing_origin, pitched_recenter_pose);
    success &= check(!nearly_equal(standing_pitch_view, identity) &&
                         std::fabs(standing_pitch_view.value[6] + 0.5f) < 0.0001f,
                     "recenter must not absorb headset pitch into the floor");

    const float yaw_positive_90[9]{
         0.0f, 0.0f, 1.0f,
         0.0f, 1.0f, 0.0f,
        -1.0f, 0.0f, 0.0f,
    };
    Matrix4 expected_inverse_yaw = identity;
    expected_inverse_yaw.value[0] = 0.0f;
    expected_inverse_yaw.value[2] = 1.0f;
    expected_inverse_yaw.value[8] = -1.0f;
    expected_inverse_yaw.value[10] = 0.0f;
    success &= check(nearly_equal(relative_view_rotation(origin, yaw_positive_90),
                                  expected_inverse_yaw),
                     "head pose must be inverted when converted to a view rotation");

    const float origin_pose[12]{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 1.6f,
        0.0f, 0.0f, 1.0f, 0.0f,
    };
    const float moved_pose[12]{
        1.0f, 0.0f, 0.0f, 0.25f,
        0.0f, 1.0f, 0.0f, 1.7f,
        0.0f, 0.0f, 1.0f, -0.50f,
    };
    Matrix4 expected_translation = identity;
    expected_translation.value[12] = -0.25f;
    expected_translation.value[13] = -0.10f;
    expected_translation.value[14] = 0.50f;
    success &= check(nearly_equal(relative_view_pose(origin_pose, moved_pose),
                                  expected_translation),
                     "head translation must be inverted into camera view space");

    float extracted_position[3]{};
    float extracted_forward[3]{};
    view_pose(expected_translation, extracted_position, extracted_forward);
    success &= check(std::fabs(extracted_position[0] - 0.25f) < 0.0001f &&
                         std::fabs(extracted_position[1] - 0.10f) < 0.0001f &&
                         std::fabs(extracted_position[2] + 0.50f) < 0.0001f,
                     "view translation must recover the tracked world offset");
    success &= check(std::fabs(extracted_forward[0]) < 0.0001f &&
                         std::fabs(extracted_forward[1]) < 0.0001f &&
                         std::fabs(extracted_forward[2] + 1.0f) < 0.0001f,
                     "view forward must use the camera negative Z axis");

    const Matrix4 raised_camera = apply_camera_height_offset(identity, 0.25f);
    view_pose(raised_camera, extracted_position, extracted_forward);
    success &= check(std::fabs(extracted_position[1] - 0.25f) < 0.0001f,
                     "floor alignment must raise the virtual camera in world space");

    Matrix4 pitched_view = identity;
    pitched_view.value[5] = 0.8f;
    pitched_view.value[6] = -0.6f;
    pitched_view.value[9] = 0.6f;
    pitched_view.value[10] = 0.8f;
    const Matrix4 level_view = horizontal_view(pitched_view);
    float level_position[3]{};
    float level_forward[3]{};
    view_pose(level_view, level_position, level_forward);
    success &= check(std::fabs(level_forward[1]) < 0.0001f &&
                         std::fabs(level_view.value[1]) < 0.0001f &&
                         std::fabs(level_view.value[5] - 1.0f) < 0.0001f &&
                         std::fabs(level_view.value[9]) < 0.0001f,
                     "native mouse pitch and roll must not tilt the VR horizon");

    const float yaw_pose[12]{
         0.0f, 0.0f, 1.0f, 0.0f,
         0.0f, 1.0f, 0.0f, 1.6f,
        -1.0f, 0.0f, 0.0f, 0.0f,
    };
    success &= check(nearly_equal(relative_view_pose(origin_pose, yaw_pose),
                                  expected_inverse_yaw),
                     "full 6DoF pose must preserve the proven rotation convention");

    Matrix4 expected_scaled_translation = identity;
    expected_scaled_translation.value[12] = 0.50f;
    expected_scaled_translation.value[13] = -0.30f;
    expected_scaled_translation.value[14] = -1.00f;
    success &= check(nearly_equal(relative_view_pose(origin_pose, moved_pose, 2.0f,
                                                     3.0f, true),
                                  expected_scaled_translation),
                     "translation gain and horizontal inversion must be explicit");

    PhysicalMotionHistory motion_history{};
    motion_history.add(0.0f, 1.50f, 0.0f, 0);
    motion_history.add(0.3f, 1.56f, 0.0f, 100);
    motion_history.add(0.6f, 1.62f, 0.0f, 200);
    success &= check(std::fabs(motion_history.average_speed(200, 330) -
                               3.0594f) < 0.001f,
                     "physical swing history must average segment speeds");
    success &= check(std::fabs(motion_history.net_vertical_movement(200, 250) -
                               0.12f) < 0.0001f,
                     "physical jump history must preserve vertical displacement");
    success &= check(physical_jump_requested(1.62f, 1.50f, 0.12f) &&
                         !physical_jump_requested(1.54f, 1.50f, 0.12f),
                     "physical jump must use the configured height and movement thresholds");
    success &= check(physical_sneak_requested(1.09f, 1.50f) &&
                         !physical_sneak_requested(1.11f, 1.50f),
                     "physical sneak must use the 0.4 meter threshold");
    success &= check(physical_swing_requested(2.51f) &&
                         !physical_swing_requested(2.49f),
                     "physical swing must use the 2.5 meter per second threshold");

    success &= check(std::fabs(linear_scene_depth(0.25f, 100.0f) - 25.0f) < 0.0001f &&
                         std::fabs(linear_hand_depth(0.05f) - 20.0f) < 0.0001f,
                     "hand and scene depth must use the same linear distance units");
    success &= check(hand_is_occluded(0.10f, 100.0f, 1.0f / 10.6f, 0.5f) &&
                         !hand_is_occluded(0.10f, 100.0f, 1.0f / 10.4f, 0.5f),
                     "hand occlusion must use only the explicit additive tolerance");

    success &= check(scene_depth_candidate_score(1280, 684, 2560, 1369) == 100 &&
                         scene_depth_candidate_score(256, 128, 2560, 1369) == 0,
                     "scene depth selection must prefer the half-resolution game buffer");

    const HKL keyboard_layout = GetKeyboardLayout(0);
    const MovementKeys movement_keys = movement_keys_for_layout(keyboard_layout);
    success &= check(movement_keys.forward != 0 && movement_keys.backward != 0 &&
                         movement_keys.left != 0 && movement_keys.right != 0,
                     "physical movement keys must resolve for the active layout");
    success &= check(parse_key_name(L"  f7 ", 0, keyboard_layout) == VK_F7 &&
                         parse_key_name(L"espace", 0, keyboard_layout) == VK_SPACE &&
                         parse_key_name(L"Auto", 'Q', keyboard_layout) == 'Q',
                     "custom key names must be parsed outside the dashboard implementation");

    DigitalStickState stick{};
    stick = digital_stick(0.40f, 0.70f, stick, 0.35f);
    success &= check(stick.right && stick.forward && !stick.left && !stick.backward,
                     "quest stick must generate diagonal movement");
    stick = digital_stick(0.25f, 0.25f, stick, 0.35f);
    success &= check(stick.right && stick.forward,
                     "stick hysteresis must avoid key chatter");
    stick = digital_stick(0.10f, 0.10f, stick, 0.35f);
    success &= check(!stick.right && !stick.forward,
                     "stick keys must release below hysteresis");

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
