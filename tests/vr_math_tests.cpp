#include "dashboard_key_bindings.h"
#include "ui_scene_depth_policy.h"
#include "ui_scale_shared.h"
#include "vr_camera_shared.h"
#include "vr_math.h"
#include "vr_hand_depth.h"
#include "vr_held_item.h"
#include "vr_item_assets.h"
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

    static_assert(sizeof(VrCameraControls) == 176,
                  "VrCameraControls ABI changed; bump the shared mapping version");
    UiScaleSharedData ui_shared{};
    initialize_ui_scale_shared_data(ui_shared);
    success &= check(
        ui_scale_shared_data_compatible(ui_shared) &&
            ui_shared.uiScale == 1.0f &&
            ui_shared.heldItemLocalOffset[2] == -0.10f &&
            ui_shared.heldItemVisualScale == 0.30f,
        "UI shared memory defaults or ABI header are invalid");
    ui_shared.version = 0;
    success &= check(
        !ui_scale_shared_data_compatible(ui_shared),
        "UI shared memory accepted an incompatible version");
    HANDLE read_only_mapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
        sizeof(UiScaleSharedData), nullptr);
    UiScaleSharedData* writable_ui_shared =
        read_only_mapping
            ? static_cast<UiScaleSharedData*>(MapViewOfFile(
                  read_only_mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                  sizeof(UiScaleSharedData)))
            : nullptr;
    if (writable_ui_shared) {
        initialize_ui_scale_shared_data(*writable_ui_shared);
        UnmapViewOfFile(writable_ui_shared);
    }
    const UiScaleSharedData* read_only_ui_shared =
        read_only_mapping
            ? static_cast<const UiScaleSharedData*>(MapViewOfFile(
                  read_only_mapping, FILE_MAP_READ, 0, 0,
                  sizeof(UiScaleSharedData)))
            : nullptr;
    success &= check(
        read_only_ui_shared &&
            ui_scale_shared_data_compatible(*read_only_ui_shared),
        "UI shared memory compatibility check must be read-only safe");
    if (read_only_ui_shared) UnmapViewOfFile(read_only_ui_shared);
    if (read_only_mapping) CloseHandle(read_only_mapping);

    const VrCameraControls default_controls{};
    success &= check(std::fabs(default_controls.vr_resolution_scale - 1.0f) < 0.0001f,
                     "VR resolution must default to the native AFW output size");
    success &= check(
        (default_controls.reserved_render_option & kVrEffectSupersampling) == 0u &&
            (default_controls.reserved_render_option & kVrEffectSharpening) == 0u &&
            (default_controls.reserved_render_option &
             kVrEffectProjectionCorrection) != 0u &&
            (default_controls.reserved_render_option &
             kVrEffectQualityControlsPresent) != 0u &&
            (default_controls.reserved_render_option & kVrEffectEdgeAa) != 0u &&
            std::fabs(default_controls.reserved_render_value - 0.20f) < 0.0001f,
        "VR quality defaults must keep projection and FXAA enabled without costly enhancement");
    success &= check(std::fabs(default_controls.held_item_offset_x) < 0.0001f &&
                         std::fabs(default_controls.held_item_offset_y) < 0.0001f &&
                         std::fabs(default_controls.held_item_offset_z + 0.10f) < 0.0001f &&
                         std::fabs(default_controls.held_item_scale - 0.30f) < 0.0001f,
                      "held-item grip controls must preserve the tested default placement");
    success &= check(
        vr_item_id_is_shield("Weapon_Shield_Iron") &&
            vr_item_id_is_torch("Furniture_Crude_Torch") &&
            !vr_item_id_is_torch("Furniture_Crude_Lantern") &&
            vr_item_shield_allowed_for_main_item("Weapon_Sword_Crude") &&
            vr_item_shield_allowed_for_main_item("Tool_Hatchet_Thorium") &&
            !vr_item_shield_allowed_for_main_item("Rock_Basalt_Brick_Ornate") &&
            !vr_item_shield_allowed_for_main_item("Captured_Item"),
        "transient shields must only render with a detected tool or weapon");
    VrItemAssetDefinition cube_definition{};
    cube_definition.generated_cube = true;
    VrItemAssetDefinition block_model_definition{};
    block_model_definition.model_path =
        "Common/Blocks/Furniture/Bench.blockymodel";
    VrItemAssetDefinition weapon_definition{};
    weapon_definition.model_path =
        "Common/Items/Weapons/Sword.blockymodel";
    success &= check(
        vr_item_definition_is_block_or_decor(cube_definition) &&
            vr_item_definition_is_block_or_decor(block_model_definition) &&
            !vr_item_definition_is_block_or_decor(weapon_definition),
        "placement hand locking must only classify blocks and decorations");
    success &= check(
        !held_item_was_present_before_attack(0u, kHeldItemLeft) &&
            held_item_was_present_before_attack(1u << kHeldItemLeft,
                                                kHeldItemLeft) &&
            held_item_was_present_before_attack(1u << kHeldItemRight,
                                                kHeldItemRight) &&
            !held_item_was_present_before_attack(1u << kHeldItemRight,
                                                 kHeldItemLeft),
        "attack item suppression must preserve only the hand that was already occupied");

    const RenderResolution resolution_150 =
        scaled_render_resolution(1920, 1080, 1.5f, 8192);
    success &= check(resolution_150.width == 2880 && resolution_150.height == 1620 &&
                         std::fabs(resolution_150.scale - 1.5f) < 0.0001f,
                     "VR resolution scaling must preserve the source aspect ratio");
    const RenderResolution resolution_clamped =
        scaled_render_resolution(1920, 1080, 2.0f, 2048);
    success &= check(resolution_clamped.width == 2048 && resolution_clamped.height == 1152,
                     "VR resolution scaling must respect the OpenGL texture limit");

    const std::array<unsigned char, 34> camera_hook_site{
        0x0F, 0x28, 0xB4, 0x24, 0x80, 0x03, 0x00, 0x00,
        0x0F, 0x28, 0xBC, 0x24, 0x70, 0x03, 0x00, 0x00,
        0x44, 0x0F, 0x28, 0x84, 0x24, 0x60, 0x03, 0x00, 0x00,
        0x44, 0x0F, 0x28, 0x8C, 0x24, 0x50, 0x03, 0x00, 0x00,
    };
    success &= check(camera_hook_site_valid(camera_hook_site.data()),
                     "known camera hook prologue was rejected");
    auto invalid_camera_hook_site = camera_hook_site;
    invalid_camera_hook_site[12] = 0x60;
    success &= check(!camera_hook_site_valid(invalid_camera_hook_site.data()),
                     "camera hook accepted inconsistent stack offsets");
    invalid_camera_hook_site = camera_hook_site;
    invalid_camera_hook_site[30] = 0x40;
    success &= check(!camera_hook_site_valid(invalid_camera_hook_site.data()),
                     "camera hook accepted a broken XMM restore sequence");

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
    float ui_anchor[12]{};
    success &= check(
        make_world_locked_ui_pose(origin_pose, 1.5f, ui_anchor) &&
            std::fabs(ui_anchor[3]) < 0.0001f &&
            std::fabs(ui_anchor[7] - 1.6f) < 0.0001f &&
            std::fabs(ui_anchor[11] + 1.5f) < 0.0001f &&
            std::fabs(ui_anchor[5] - 1.0f) < 0.0001f,
        "world-locked UI must be placed ahead in Standing space");
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

    const Matrix4 pitched_culling_view = roll_free_view(pitched_view);
    float pitched_culling_position[3]{};
    float pitched_culling_forward[3]{};
    view_pose(pitched_culling_view, pitched_culling_position,
              pitched_culling_forward);
    success &= check(
        std::fabs(pitched_culling_forward[0]) < 0.0001f &&
            std::fabs(pitched_culling_forward[1] - 0.6f) < 0.0001f &&
            std::fabs(pitched_culling_forward[2] + 0.8f) < 0.0001f &&
            std::fabs(pitched_culling_view.value[4]) < 0.0001f,
        "culling view must preserve headset pitch while removing roll");

    Matrix4 downward_view = identity;
    downward_view.value[0] = 1.0f;
    downward_view.value[1] = 0.0f;
    downward_view.value[2] = 0.0f;
    downward_view.value[4] = 0.0f;
    downward_view.value[5] = 0.0f;
    downward_view.value[6] = 1.0f;
    downward_view.value[8] = 0.0f;
    downward_view.value[9] = -1.0f;
    downward_view.value[10] = 0.0f;
    const Matrix4 downward_culling_view = roll_free_view(downward_view);
    float downward_position[3]{};
    float downward_forward[3]{};
    view_pose(downward_culling_view, downward_position, downward_forward);
    success &= check(
        std::isfinite(downward_culling_view.value[0]) &&
            std::isfinite(downward_culling_view.value[5]) &&
            std::fabs(downward_forward[0]) < 0.0001f &&
            std::fabs(downward_forward[1] + 1.0f) < 0.0001f &&
            std::fabs(downward_forward[2]) < 0.0001f,
        "culling view must remain valid when looking straight down");

    Matrix4 narrow_projection = identity;
    narrow_projection.value[0] = 2.0f;
    narrow_projection.value[5] = 3.0f;
    narrow_projection.value[8] = 0.4f;
    narrow_projection.value[9] = -0.3f;
    narrow_projection.value[10] = -1.01f;
    narrow_projection.value[11] = -1.0f;
    narrow_projection.value[14] = -0.2f;
    narrow_projection.value[15] = 0.0f;
    const Matrix4 wide_projection =
        expand_perspective_frustum(narrow_projection, 2.0f, 3.0f);
    success &= check(
        std::fabs(wide_projection.value[0] - 1.0f) < 0.0001f &&
            std::fabs(wide_projection.value[5] - 1.0f) < 0.0001f &&
            std::fabs(wide_projection.value[8] - 0.2f) < 0.0001f &&
            std::fabs(wide_projection.value[9] + 0.1f) < 0.0001f &&
            std::fabs(wide_projection.value[10] -
                      narrow_projection.value[10]) < 0.0001f &&
            std::fabs(wide_projection.value[14] -
                      narrow_projection.value[14]) < 0.0001f,
        "culling guard band must widen XY without changing depth mapping");

    const float yaw_pose[12]{
         0.0f, 0.0f, 1.0f, 0.0f,
         0.0f, 1.0f, 0.0f, 1.6f,
        -1.0f, 0.0f, 0.0f, 0.0f,
    };
    success &= check(nearly_equal(relative_view_pose(origin_pose, yaw_pose),
                                   expected_inverse_yaw),
                      "full 6DoF pose must preserve the proven rotation convention");
    success &= check(
        make_world_locked_ui_pose(yaw_pose, 1.5f, ui_anchor) &&
            std::fabs(ui_anchor[3] + 1.5f) < 0.0001f &&
            std::fabs(ui_anchor[7] - 1.6f) < 0.0001f &&
            std::fabs(ui_anchor[11]) < 0.0001f &&
            std::fabs(ui_anchor[5] - 1.0f) < 0.0001f,
        "world-locked UI must follow opening yaw without inheriting tilt");

    const float right_controller_pose[12]{
        1.0f, 0.0f, 0.0f, 0.25f,
        0.0f, 1.0f, 0.0f, 1.30f,
        0.0f, 0.0f, 1.0f, -0.50f,
    };
    float controller_view_pose[12]{};
    success &= check(controller_pose_relative_to_hmd(
                         origin_pose, right_controller_pose, 1.30f,
                         controller_view_pose) &&
                         std::fabs(controller_view_pose[3] - 0.325f) < 0.0001f &&
                         std::fabs(controller_view_pose[7] + 0.390f) < 0.0001f &&
                         std::fabs(controller_view_pose[11] + 0.650f) < 0.0001f,
                     "held-item controller pose must be head-relative and world-scaled");

    float right_item_model[16]{};
    right_item_model[0] = 0.015f;
    right_item_model[5] = 0.015f;
    right_item_model[10] = 0.015f;
    right_item_model[12] = 0.70f;
    right_item_model[13] = -0.90f;
    right_item_model[14] = -1.05f;
    right_item_model[15] = 1.0f;
    float tracked_item_model[16]{};
    const float item_grip_offset[3]{0.02f, -0.01f, -0.12f};
    constexpr float item_visual_scale = 0.75f;
    success &= check(is_held_item_visual_program_label(
                         "FirstPersonBlockyModelProgram") &&
                         !is_held_item_visual_program_label(
                             "FirstPersonClippingBlockyModelProgram") &&
                         !is_held_item_visual_program_label(
                             "FirstPersonDistortionBlockyModelProgram") &&
                         held_item_model_matrix_has_anchor(right_item_model) &&
                         held_item_side_from_model_matrix(right_item_model) ==
                         kHeldItemRight &&
                         compose_held_item_model_matrix(
                             right_item_model, controller_view_pose, 1.0f,
                             item_grip_offset, item_visual_scale,
                             tracked_item_model) &&
                         std::fabs(tracked_item_model[0] -
                                   0.015f * item_visual_scale) < 0.0001f &&
                         std::fabs(tracked_item_model[12] - 0.345f) < 0.0001f &&
                         std::fabs(tracked_item_model[13] + 0.400f) < 0.0001f &&
                         std::fabs(tracked_item_model[14] + 0.770f) < 0.0001f,
                     "held-item matrix must preserve the item mesh and use the controller anchor");
    right_item_model[12] = -0.70f;
    success &= check(held_item_side_from_model_matrix(right_item_model) ==
                         kHeldItemLeft,
                     "held-item side must follow Hytale's original left/right anchor");

    float vanilla_hand_model[16]{};
    vanilla_hand_model[0] = -0.015625f;
    vanilla_hand_model[5] = -0.015625f;
    vanilla_hand_model[10] = -0.015625f;
    vanilla_hand_model[15] = 1.0f;
    success &= check(!held_item_model_matrix_has_anchor(vanilla_hand_model) &&
                         !compose_held_item_model_matrix(
                             vanilla_hand_model, controller_view_pose, 1.0f,
                             item_grip_offset, item_visual_scale,
                             tracked_item_model),
                     "the origin-centered Hytale hand draw must stay hidden");

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

    HeldItemUvBounds uv_bounds{};
    include_held_item_packed_uv((20u << 16u) | 12u, uv_bounds);
    include_held_item_packed_uv((52u << 16u) | 108u, uv_bounds);
    include_held_item_packed_uv((8u << 16u) | 40u, uv_bounds);
    success &= check(uv_bounds.valid &&
                         uv_bounds.min_u == 12u && uv_bounds.min_v == 8u &&
                         uv_bounds.max_u == 108u && uv_bounds.max_v == 52u,
                     "packed item UVs must identify the real atlas region");

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

    constexpr float half_pi = 1.57079632679489661923f;
    LocomotionAxes axes = rotate_locomotion_axes(0.0f, 1.0f, 0.0f);
    success &= check(std::fabs(axes.x) < 0.0001f &&
                         std::fabs(axes.y - 1.0f) < 0.0001f,
                     "aligned head locomotion must preserve forward movement");
    axes = rotate_locomotion_axes(0.0f, 1.0f, half_pi);
    success &= check(std::fabs(axes.x - 1.0f) < 0.0001f &&
                         std::fabs(axes.y) < 0.0001f,
                     "head locomotion must follow a right-facing headset");
    axes = rotate_locomotion_axes(1.0f, 0.0f, -half_pi);
    success &= check(std::fabs(axes.x) < 0.0001f &&
                         std::fabs(axes.y - 1.0f) < 0.0001f,
                     "head locomotion must compensate the native camera yaw");

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
