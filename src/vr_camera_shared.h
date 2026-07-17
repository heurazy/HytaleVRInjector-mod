#pragma once

#include <cstdint>

constexpr wchar_t kVrCameraMappingName[] = L"Local\\HytaleVrCamera_v122FloorAligned";
constexpr uint32_t kVrCameraMagic = 0x48595652; // HYVR
constexpr uint32_t kVrCameraVersion = 122;

constexpr uint32_t kVrEffectShadows = 1u << 0;
constexpr uint32_t kVrEffectParticles = 1u << 1;
constexpr uint32_t kVrEffectDistortion = 1u << 2;
constexpr uint32_t kVrEffectSupersampling = 1u << 3;
constexpr uint32_t kVrEffectSharpening = 1u << 4;
constexpr uint32_t kVrEffectEdgeAa = 1u << 5;
constexpr uint32_t kVrEffectProjectionCorrection = 1u << 6;
constexpr uint32_t kVrEffectQualityControlsPresent = 1u << 31;
constexpr uint32_t kVrEffectDefaults =
    kVrEffectShadows | kVrEffectParticles | kVrEffectDistortion |
    kVrEffectEdgeAa | kVrEffectProjectionCorrection |
    kVrEffectQualityControlsPresent;

struct VrCameraControls {
    uint32_t enabled = 0;
    uint32_t non_vr_mode = 0;
    uint32_t stereo_enabled = 0;
    uint32_t swap_eyes = 0;
    uint32_t shutdown_requested = 0;
    uint32_t unload_requested = 0;
    uint32_t install_sequence = 0;
    uint32_t recenter_sequence = 0;
    float ipd_meters = 0.064f;
    float stereo_separation = 1.0f;
    // Reuses the former render-scale ABI slot. 1.0 means normal world scale.
    float world_scale = 1.30f;
    float translation_scale = 1.0f;
    float translation_y_scale = 1.0f;
    uint32_t sneak_active = 0;
    uint32_t invert_translation_xz = 0;
    uint32_t hand_pointer_enabled = 1;
    uint32_t hide_center_reticle = 1;
    uint32_t ui_overlay_enabled = 1;
    uint32_t shadows_disabled = 0;
    uint32_t particles_disabled = 0;
    // 0 = native pass, 1 = disabled, 2 = neutralize mono pass and reapply per eye.
    uint32_t distortion_disabled = 2;
    float ui_overlay_distance = 1.65f;
    float ui_overlay_width = 1.50f;
    float ui_scale = 1.00f;
    float ui_eye_offset = 0.245f;
    float ui_offset_y = 0.0f;
    uint32_t ui_ubo_scaling_disabled = 1;
    uint32_t menu_ignore_draw_threshold = 1;
    float hand_pointer_distance = 4.0f;
    float turn_speed = 450.0f;
    // Reuses a legacy ABI slot so dashboard and hook keep the same structure size.
    float vr_resolution_scale = 1.0f;
    uint32_t first_person_hand_hidden = 1;
    // Bitmask of kVrEffect* flags. Kept in the existing ABI slot.
    uint32_t reserved_render_option = kVrEffectDefaults;
    float reserved_render_value = 0.20f;
    uint32_t hmd_culling_view_enabled = 1;
    float hand_model_scale = 1.0f;
    float hand_model_pitch_degrees = -90.0f;
    float hand_model_yaw_degrees = 0.0f;
    float hand_model_roll_degrees = 0.0f;
    float hand_depth_tolerance = 0.02f;
    float held_item_offset_x = 0.0f;
    float held_item_offset_y = 0.0f;
    float held_item_offset_z = -0.10f;
    float held_item_scale = 0.30f;
};

struct VrCameraShared {
    uint32_t magic = kVrCameraMagic;
    uint32_t version = kVrCameraVersion;
    // Odd while the dashboard writes controls, even when a coherent snapshot
    // can be consumed by the injected render hook.
    volatile uint32_t control_sequence = 0;
    uint32_t enabled = 0;
    uint32_t non_vr_mode = 0;
    uint32_t stereo_enabled = 0;
    uint32_t swap_eyes = 0;
    uint32_t shutdown_requested = 0;
    uint32_t unload_requested = 0;
    uint32_t install_sequence = 0;
    uint32_t recenter_sequence = 0;
    float ipd_meters = 0.064f;
    float stereo_separation = 1.0f;
    // Reuses the former render-scale ABI slot. 1.0 means normal world scale.
    float world_scale = 1.30f;
    float translation_scale = 1.0f;
    float translation_y_scale = 1.0f;
    uint32_t sneak_active = 0;
    uint32_t invert_translation_xz = 0;
    uint32_t hand_pointer_enabled = 1;
    uint32_t hide_center_reticle = 1;
    uint32_t ui_overlay_enabled = 1;
    uint32_t shadows_disabled = 0;
    uint32_t particles_disabled = 0;
    // 0 = native pass, 1 = disabled, 2 = neutralize mono pass and reapply per eye.
    uint32_t distortion_disabled = 2;
    float ui_overlay_distance = 1.65f;
    float ui_overlay_width = 1.50f;
    float ui_scale = 1.00f;
    float ui_eye_offset = 0.245f;
    float ui_offset_y = 0.0f;
    uint32_t ui_ubo_scaling_disabled = 1;
    uint32_t menu_ignore_draw_threshold = 1;
    float hand_pointer_distance = 4.0f;
    float turn_speed = 450.0f;
    // Reuses a legacy ABI slot so dashboard and hook keep the same structure size.
    float vr_resolution_scale = 1.0f;
    uint32_t first_person_hand_hidden = 1;
    // Bitmask of kVrEffect* flags. Kept in the existing ABI slot.
    uint32_t reserved_render_option = kVrEffectDefaults;
    float reserved_render_value = 0.20f;
    uint32_t hmd_culling_view_enabled = 1;
    float hand_model_scale = 1.0f;
    float hand_model_pitch_degrees = -90.0f;
    float hand_model_yaw_degrees = 0.0f;
    float hand_model_roll_degrees = 0.0f;
    float hand_depth_tolerance = 0.02f;
    float held_item_offset_x = 0.0f;
    float held_item_offset_y = 0.0f;
    float held_item_offset_z = -0.10f;
    float held_item_scale = 0.30f;

    volatile uint32_t hook_active = 0;
    volatile int32_t hook_error = 0;
    volatile uint32_t updates = 0;
    volatile uint32_t stereo_active = 0;
    volatile uint32_t stereo_frames = 0;
    volatile uint32_t swap_hook_active = 0;
    volatile uint32_t swap_calls = 0;
    volatile int32_t stereo_error = 0;
    volatile int32_t capture_error = 0;
    volatile int32_t submit_left_error = 0;
    volatile int32_t submit_right_error = 0;
    volatile uint32_t scene_focus_pid = 0;
    volatile uint32_t compositor_focus_pid = 0;
    volatile int32_t identify_error = 0;
    volatile uint32_t wait_pose_calls = 0;
    volatile uint32_t wait_pose_us = 0;
    volatile uint32_t shadow_cameras_disabled = 0;
    volatile uint32_t effects_stabilized = 0;
    volatile uint32_t recommended_width = 0;
    volatile uint32_t recommended_height = 0;
    volatile uint32_t backbuffer_width = 0;
    volatile uint32_t backbuffer_height = 0;
    volatile uint32_t capture_width = 0;
    volatile uint32_t capture_height = 0;
    volatile int32_t resolution_error = 0;
    volatile uint32_t current_eye = 0;
    volatile uint32_t completed_pairs = 0;
    volatile uint64_t primary_camera = 0;
    volatile uint32_t camera_calls = 0;
    volatile uint32_t camera_hook_entries = 0;
    volatile uint32_t controller_active = 0;
    volatile float controller_move_x = 0.0f;
    volatile float controller_move_y = 0.0f;
    volatile float controller_turn_x = 0.0f;
    volatile float controller_turn_y = 0.0f;
    volatile uint32_t controller_jump = 0;
    volatile uint32_t controller_sprint = 0;
    volatile int32_t controller_input_error = 0;
    volatile uint32_t controller_right_pose_active = 0;
    volatile float controller_right_trigger = 0.0f;
    volatile uint32_t controller_right_trigger_pressed = 0;
    volatile float controller_left_trigger = 0.0f;
    volatile uint32_t controller_left_trigger_pressed = 0;
    volatile uint32_t controller_button_x = 0;
    volatile uint32_t controller_button_y = 0;
    volatile uint32_t controller_button_b = 0;
    volatile uint32_t controller_left_grip = 0;
    volatile uint32_t controller_right_grip = 0;
    volatile uint32_t controller_right_stick_click = 0;
    volatile uint64_t controller_left_button_mask = 0;
    volatile uint64_t controller_right_button_mask = 0;
    volatile uint32_t controller_ray_active = 0;
    volatile uint32_t pointer_visible = 0;
    volatile int32_t pointer_x = 0;
    volatile int32_t pointer_y = 0;
    volatile uint32_t pointer_surface_width = 0;
    volatile uint32_t pointer_surface_height = 0;
    volatile uint32_t pointer_menu_mode = 0;
    volatile uint32_t pointer_draws = 0;
    volatile uint32_t interaction_hook_active = 0;
    volatile uint32_t interaction_ray_calls = 0;
    volatile uint32_t interaction_ray_overrides = 0;
    volatile uint32_t ui_overlay_active = 0;
    volatile uint32_t ui_overlay_frames = 0;
    volatile int32_t ui_overlay_error = 0;
    volatile float controller_ray_origin_x = 0.0f;
    volatile float controller_ray_origin_y = 0.0f;
    volatile float controller_ray_origin_z = 0.0f;
    volatile float controller_ray_direction_x = 0.0f;
    volatile float controller_ray_direction_y = 0.0f;
    volatile float controller_ray_direction_z = 0.0f;
    volatile uint32_t camera_yaw_valid = 0;
    volatile float native_camera_yaw = 0.0f;
    volatile float body_camera_yaw = 0.0f;
    volatile uint32_t native_head_sync_active = 0;
    volatile uint32_t scene_depth_texture_id = 0;
    volatile uint32_t scene_depth_width = 0;
    volatile uint32_t scene_depth_height = 0;
    volatile uint32_t scene_depth_frame = 0;
    volatile uint32_t hand_depth_draws = 0;
    volatile uint32_t hand_depth_active_draws = 0;
    volatile uint32_t physical_jump_active = 0;
    volatile uint32_t physical_sneak_active = 0;
    volatile uint32_t physical_attack_sequence = 0;
    volatile uint32_t physical_attack_ray_sequence = 0;
    volatile uint32_t physical_attack_hand = 1;
    volatile float physical_hmd_height = 0.0f;
    volatile float physical_hmd_vertical_movement = 0.0f;
    volatile float physical_left_swing_speed = 0.0f;
    volatile float physical_right_swing_speed = 0.0f;
};
