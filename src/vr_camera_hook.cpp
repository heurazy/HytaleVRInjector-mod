#include <windows.h>
#include <tlhelp32.h>
#include <objidl.h>
#include <gdiplus.h>
#include <GL/gl.h>
#include <openvr.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "vr_camera_shared.h"
#include "vr_hand_depth.h"
#include "vr_held_item.h"
#include "vr_hook_validation.h"
#include "vr_item_assets.h"
#include "vr_math.h"
#include "vr_physical_interactions.h"
#include "vr_render_resolution.h"
#include "ui_scale_shared.h"

namespace {

using hytalevr::Matrix4;
using hytalevr::identity_matrix;
using hytalevr::multiply;

HMODULE g_module = nullptr;
HANDLE g_mapping = nullptr;
VrCameraShared* g_shared = nullptr;
volatile LONG g_hook_callbacks_in_flight = 0;
SRWLOCK g_hook_lifecycle_lock = SRWLOCK_INIT;
std::mutex g_render_log_mutex;
ULONGLONG g_last_camera_diag_tick = 0;
ULONGLONG g_last_camera_layout_error_tick = 0;
ULONGLONG g_last_capture_diag_tick = 0;
ULONGLONG g_last_swap_diag_tick = 0;
ULONGLONG g_last_hmd_delta_diag_tick = 0;
ULONGLONG g_last_control_fallback_log_tick = 0;
Matrix4 g_last_diag_view = identity_matrix();
Matrix4 g_last_diag_view_projection = identity_matrix();
bool g_last_diag_valid = false;
VrCameraControls g_last_valid_controls{};
bool g_last_valid_controls_valid = false;
ULONGLONG g_last_valid_controls_tick = 0;
uint32_t g_control_read_fallbacks = 0;
Matrix4 g_aggressive_native_view = identity_matrix();
bool g_aggressive_native_view_valid = false;
float g_last_observed_native_yaw = 0.0f;
bool g_last_observed_native_yaw_valid = false;
uint32_t g_aggressive_recenter_sequence = 0;
uint32_t g_aggressive_suppressed_snaps = 0;
float g_last_aggressive_native_delta = 0.0f;

class HookCallbackScope {
public:
    HookCallbackScope() {
        InterlockedIncrement(&g_hook_callbacks_in_flight);
    }

    ~HookCallbackScope() {
        InterlockedDecrement(&g_hook_callbacks_in_flight);
    }

    HookCallbackScope(const HookCallbackScope&) = delete;
    HookCallbackScope& operator=(const HookCallbackScope&) = delete;
};

using UiScaleShared = hytalevr::UiScaleSharedData;
HANDLE g_ui_scale_mapping = nullptr;
UiScaleShared* g_ui_scale_shared = nullptr;
bool menu_visible_recently();
bool ensure_ui_scale_mapping();
bool initialize_gl_capture();
bool controller_hand_ndc_for_eye(vr::EVREye eye, float& ndc_x, float& ndc_y, float& depth);
bool project_tracking_point_to_eye_ndc(int eye, const float hmd[12],
                                       const float absolute[3],
                                       float& ndc_x, float& ndc_y,
                                       float& depth);
void clear_scissor_rect(int x, int y, int width, int height,
                        int target_width, int target_height);
void* g_hook_memory = nullptr;
unsigned char* g_hook_target = nullptr;
std::array<unsigned char, 16> g_original{};
constexpr size_t kCameraViewOffset = 0x2E0;
constexpr size_t kCameraViewProjectionOffset = 0x320;
constexpr size_t kCameraProjectionOffset = 0x4E0;
void* g_interaction_hook_memory = nullptr;
unsigned char* g_interaction_hook_target = nullptr;
std::array<unsigned char, 15> g_interaction_original{};
using SdlGlSwapWindowFn = void(__cdecl*)(void*);
using SdlGetWindowSizeFn = bool(__cdecl*)(void*, int*, int*);
using SdlSetWindowSizeFn = bool(__cdecl*)(void*, int, int);
using SdlGetWindowFlagsFn = uint64_t(__cdecl*)(void*);
using SdlSyncWindowFn = bool(__cdecl*)(void*);
using NoesisRenderFn = void(__fastcall*)(void*);
using NoesisAnyFn = void(__fastcall*)(void*, void*, void*, void*);
using GlGenFramebuffersFn = void(APIENTRY*)(GLsizei, GLuint*);
using GlDeleteFramebuffersFn = void(APIENTRY*)(GLsizei, const GLuint*);
using GlBindFramebufferFn = void(APIENTRY*)(GLenum, GLuint);
using GlFramebufferTexture2DFn = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
using GlBlitFramebufferFn = void(APIENTRY*)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint,
                                             GLbitfield, GLenum);
using GlCheckFramebufferStatusFn = GLenum(APIENTRY*)(GLenum);
using GlCreateShaderFn = GLuint(APIENTRY*)(GLenum);
using GlShaderSourceFn = void(APIENTRY*)(GLuint, GLsizei, const char* const*, const GLint*);
using GlCompileShaderFn = void(APIENTRY*)(GLuint);
using GlGetShaderivFn = void(APIENTRY*)(GLuint, GLenum, GLint*);
using GlGetShaderInfoLogFn = void(APIENTRY*)(GLuint, GLsizei, GLsizei*, char*);
using GlCreateProgramFn = GLuint(APIENTRY*)();
using GlAttachShaderFn = void(APIENTRY*)(GLuint, GLuint);
using GlBindAttribLocationFn = void(APIENTRY*)(GLuint, GLuint, const char*);
using GlLinkProgramFn = void(APIENTRY*)(GLuint);
using GlGetProgramivFn = void(APIENTRY*)(GLuint, GLenum, GLint*);
using GlGetProgramInfoLogFn = void(APIENTRY*)(GLuint, GLsizei, GLsizei*, char*);
using GlDeleteShaderFn = void(APIENTRY*)(GLuint);
using GlUseProgramFn = void(APIENTRY*)(GLuint);
using GlGetUniformLocationFn = GLint(APIENTRY*)(GLuint, const char*);
using GlUniform1iFn = void(APIENTRY*)(GLint, GLint);
using GlUniform1fFn = void(APIENTRY*)(GLint, GLfloat);
using GlUniform2fFn = void(APIENTRY*)(GLint, GLfloat, GLfloat);
using GlUniformMatrix4fvFn = void(APIENTRY*)(GLint, GLsizei, GLboolean, const GLfloat*);
using GlGenVertexArraysFn = void(APIENTRY*)(GLsizei, GLuint*);
using GlBindVertexArrayFn = void(APIENTRY*)(GLuint);
using GlGenBuffersFn = void(APIENTRY*)(GLsizei, GLuint*);
using GlBindBufferFn = void(APIENTRY*)(GLenum, GLuint);
using GlBufferDataFn = void(APIENTRY*)(GLenum, ptrdiff_t, const void*, GLenum);
using GlEnableVertexAttribArrayFn = void(APIENTRY*)(GLuint);
using GlVertexAttribPointerFn = void(APIENTRY*)(GLuint, GLint, GLenum, GLboolean, GLsizei,
                                                const void*);
using GlActiveTextureFn = void(APIENTRY*)(GLenum);
using GlGetTextureSubImageFn = void(APIENTRY*)(
    GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei,
    GLenum, GLenum, GLsizei, void*);
SdlGlSwapWindowFn g_swap_trampoline = nullptr;
SdlGetWindowSizeFn g_sdl_get_window_size_in_pixels = nullptr;
SdlGetWindowSizeFn g_sdl_get_window_size = nullptr;
SdlSetWindowSizeFn g_sdl_set_window_size = nullptr;
SdlGetWindowFlagsFn g_sdl_get_window_flags = nullptr;
SdlSyncWindowFn g_sdl_sync_window = nullptr;
void* g_supersampled_window = nullptr;
int g_supersample_base_width = 0;
int g_supersample_base_height = 0;
int g_supersample_target_width = 0;
int g_supersample_target_height = 0;
bool g_source_supersampling_active = false;
unsigned char* g_swap_target = nullptr;
std::array<unsigned char, 14> g_swap_original{};
void* g_engine_render_hook_memory = nullptr;
unsigned char* g_engine_render_hook_target = nullptr;
unsigned char* g_engine_render_loop_target = nullptr;
unsigned char* g_engine_render_final_stage_target = nullptr;
unsigned char* g_engine_render_final_stage_stub = nullptr;
std::array<unsigned char, 7> g_engine_render_original{};
std::array<unsigned char, 5> g_engine_render_final_stage_original{};
void* g_noesis_begin_memory = nullptr;
void* g_noesis_end_memory = nullptr;
void* g_noesis_renderer_memory = nullptr;
unsigned char* g_noesis_begin_target = nullptr;
unsigned char* g_noesis_end_target = nullptr;
unsigned char* g_noesis_renderer_target = nullptr;
std::array<unsigned char, 12> g_noesis_begin_original{};
std::array<unsigned char, 12> g_noesis_end_original{};
std::array<unsigned char, 16> g_noesis_renderer_original{};
NoesisRenderFn g_noesis_begin_trampoline = nullptr;
NoesisRenderFn g_noesis_end_trampoline = nullptr;
NoesisAnyFn g_noesis_renderer_trampoline = nullptr;
bool g_noesis_hooks_active = false;
vr::IVRSystem* g_vr_system = nullptr;
vr::VROverlayHandle_t g_ui_overlay = vr::k_ulOverlayHandleInvalid;
vr::HmdMatrix34_t g_ui_overlay_world_transform{};
bool g_ui_overlay_world_transform_valid = false;
uint32_t g_ui_overlay_recenter_sequence = ~0u;
float g_ui_overlay_anchor_distance = 0.0f;
GLuint g_eye_textures[2]{};
GLuint g_afw_source_texture = 0;
GLuint g_eye_depth_textures[2]{};
bool g_eye_valid[2]{};
bool g_eye_depth_valid[2]{};
bool g_pre_ui_captured[2]{};
int g_texture_width = 0;
int g_texture_height = 0;
int g_source_texture_width = 0;
int g_source_texture_height = 0;
volatile LONG g_render_eye = 0;
volatile LONG g_engine_render_pass = 0;
volatile LONG g_engine_stereo_pair_ready = 0;
bool g_engine_render_hook_active = false;
uint32_t g_last_swap_recenter_sequence = 0;
uint32_t g_consecutive_submit_failures = 0;
ULONGLONG g_last_successful_submit_tick = 0;
SRWLOCK g_pose_lock = SRWLOCK_INIT;
Matrix4 g_hmd_view_delta = identity_matrix();
float g_hmd_origin_pose[12]{};
float g_hmd_current_pose[12]{};
float g_left_controller_pose[12]{};
float g_right_controller_pose[12]{};
bool g_hmd_origin_valid = false;
bool g_hmd_current_valid = false;
bool g_left_controller_valid = false;
bool g_right_controller_valid = false;
uint32_t g_recenter_sequence = 0;
uint32_t g_floor_height_alignment_sequence = ~0u;
float g_floor_height_offset = 0.0f;
float g_floor_height_world_scale = 1.30f;
float g_native_standing_camera_y = 0.0f;
bool g_native_standing_camera_y_valid = false;
float g_last_sneak_height_compensation = 0.0f;
std::atomic<bool> g_runtime_active{false};
GLuint g_capture_fbo = 0;
GLuint g_ui_fbo = 0;
GLuint g_ui_texture = 0;
int g_ui_width = 0;
int g_ui_height = 0;
bool g_ui_redirect_active = false;
GLint g_ui_saved_draw_fbo = 0;
GLint g_ui_saved_read_fbo = 0;
GLint g_ui_saved_viewport[4]{};
GlGenFramebuffersFn g_gl_gen_framebuffers = nullptr;
GlDeleteFramebuffersFn g_gl_delete_framebuffers = nullptr;
GlBindFramebufferFn g_gl_bind_framebuffer = nullptr;
GlFramebufferTexture2DFn g_gl_framebuffer_texture_2d = nullptr;
GlBlitFramebufferFn g_gl_blit_framebuffer = nullptr;
GlCheckFramebufferStatusFn g_gl_check_framebuffer_status = nullptr;
GlCreateShaderFn g_gl_create_shader = nullptr;
GlShaderSourceFn g_gl_shader_source = nullptr;
GlCompileShaderFn g_gl_compile_shader = nullptr;
GlGetShaderivFn g_gl_get_shader_iv = nullptr;
GlGetShaderInfoLogFn g_gl_get_shader_info_log = nullptr;
GlCreateProgramFn g_gl_create_program = nullptr;
GlAttachShaderFn g_gl_attach_shader = nullptr;
GlBindAttribLocationFn g_gl_bind_attrib_location = nullptr;
GlLinkProgramFn g_gl_link_program = nullptr;
GlGetProgramivFn g_gl_get_program_iv = nullptr;
GlGetProgramInfoLogFn g_gl_get_program_info_log = nullptr;
GlDeleteShaderFn g_gl_delete_shader = nullptr;
GlUseProgramFn g_gl_use_program = nullptr;
GlGetUniformLocationFn g_gl_get_uniform_location = nullptr;
GlUniform1iFn g_gl_uniform_1i = nullptr;
GlUniform1fFn g_gl_uniform_1f = nullptr;
GlUniform2fFn g_gl_uniform_2f = nullptr;
GlUniformMatrix4fvFn g_gl_uniform_matrix_4fv = nullptr;
GlGenVertexArraysFn g_gl_gen_vertex_arrays = nullptr;
GlBindVertexArrayFn g_gl_bind_vertex_array = nullptr;
GlGenBuffersFn g_gl_gen_buffers = nullptr;
GlBindBufferFn g_gl_bind_buffer = nullptr;
GlBufferDataFn g_gl_buffer_data = nullptr;
GlEnableVertexAttribArrayFn g_gl_enable_vertex_attrib_array = nullptr;
GlVertexAttribPointerFn g_gl_vertex_attrib_pointer = nullptr;
GlActiveTextureFn g_gl_active_texture = nullptr;
GlGetTextureSubImageFn g_gl_get_texture_sub_image = nullptr;
void* g_primary_camera = nullptr;
ULONGLONG g_last_primary_camera_tick = 0;
SRWLOCK g_camera_lock = SRWLOCK_INIT;
Matrix4 g_last_native_camera_view = identity_matrix();
Matrix4 g_last_written_culling_view = identity_matrix();
void* g_last_native_camera_object = nullptr;
void* g_last_written_culling_object = nullptr;
ULONGLONG g_last_native_camera_tick = 0;
ULONGLONG g_last_written_culling_tick = 0;
bool g_last_native_camera_view_valid = false;
bool g_last_written_culling_view_valid = false;
Matrix4 g_center_view_projection = identity_matrix();
Matrix4 g_culling_view_projection = identity_matrix();
Matrix4 g_culling_projection = identity_matrix();
Matrix4 g_culling_view = identity_matrix();
Matrix4 g_neutral_view = identity_matrix();
Matrix4 g_previous_vr_scene_view_projection = identity_matrix();
bool g_center_view_projection_valid = false;
bool g_culling_view_projection_valid = false;
bool g_culling_projection_valid = false;
bool g_culling_view_valid = false;
bool g_neutral_view_valid = false;
bool g_previous_vr_scene_view_projection_valid = false;
vr::VRActionSetHandle_t g_action_set = vr::k_ulInvalidActionSetHandle;
vr::VRActionHandle_t g_action_move = vr::k_ulInvalidActionHandle;
vr::VRActionHandle_t g_action_turn = vr::k_ulInvalidActionHandle;
vr::VRActionHandle_t g_action_jump = vr::k_ulInvalidActionHandle;
vr::VRActionHandle_t g_action_sprint = vr::k_ulInvalidActionHandle;
vr::VRActionHandle_t g_action_left_pose = vr::k_ulInvalidActionHandle;
vr::VRActionHandle_t g_action_right_pose = vr::k_ulInvalidActionHandle;
vr::VRActionHandle_t g_action_right_trigger = vr::k_ulInvalidActionHandle;
vr::VRActionHandle_t g_action_left_trigger = vr::k_ulInvalidActionHandle;
vr::VRActionHandle_t g_action_button_x = vr::k_ulInvalidActionHandle;
vr::VRActionHandle_t g_action_button_y = vr::k_ulInvalidActionHandle;
vr::VRActionHandle_t g_action_button_b = vr::k_ulInvalidActionHandle;
vr::VRActionHandle_t g_action_left_grip = vr::k_ulInvalidActionHandle;
vr::VRActionHandle_t g_action_right_grip = vr::k_ulInvalidActionHandle;
vr::VRActionHandle_t g_action_right_stick_click = vr::k_ulInvalidActionHandle;
bool g_actions_ready = false;
bool g_right_trigger_pressed = false;
bool g_left_trigger_pressed = false;
bool g_right_attack_suppresses_left_item = false;
bool g_left_attack_suppresses_right_item = false;
bool g_left_use_locks_placeable_to_right = false;
bool g_left_grip_pressed = false;
bool g_right_grip_pressed = false;
hytalevr::PhysicalMotionHistory g_hmd_motion_history;
hytalevr::PhysicalMotionHistory g_controller_tip_history[2];
float g_physical_standing_height = 0.0f;
bool g_physical_standing_height_valid = false;
bool g_physical_jump_armed = true;
bool g_physical_swing_armed[2]{true, true};
ULONGLONG g_physical_jump_until = 0;
ULONGLONG g_physical_jump_cooldown_until = 0;
ULONGLONG g_physical_swing_cooldown_until[2]{};
int g_physical_attack_ray_hand = 1;
ULONGLONG g_physical_attack_ray_until = 0;
SRWLOCK g_game_ray_lock = SRWLOCK_INIT;
float g_controller_ray_offset[3]{};
float g_controller_ray_direction[3]{};
bool g_controller_game_ray_valid = false;
bool g_controller_game_ray_physical_active = false;
constexpr bool kPhysicalAttacksEnabled = false;

constexpr GLenum GL_READ_FRAMEBUFFER_VALUE = 0x8CA8;
constexpr GLenum GL_DRAW_FRAMEBUFFER_VALUE = 0x8CA9;
constexpr GLenum GL_READ_FRAMEBUFFER_BINDING_VALUE = 0x8CAA;
constexpr GLenum GL_DRAW_FRAMEBUFFER_BINDING_VALUE = 0x8CA6;
constexpr GLenum GL_COLOR_ATTACHMENT0_VALUE = 0x8CE0;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE_VALUE = 0x8CD5;
constexpr GLenum GL_CLAMP_TO_EDGE_VALUE = 0x812F;
constexpr GLint GL_R16F_VALUE = 0x822D;
constexpr GLenum GL_RED_VALUE = 0x1903;
constexpr GLenum GL_SAMPLES_VALUE = 0x80A9;
constexpr GLenum GL_VIEWPORT_VALUE = 0x0BA2;
constexpr GLenum GL_VERTEX_SHADER_VALUE = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER_VALUE = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS_VALUE = 0x8B81;
constexpr GLenum GL_LINK_STATUS_VALUE = 0x8B82;
constexpr GLenum GL_INFO_LOG_LENGTH_VALUE = 0x8B84;
constexpr GLenum GL_CURRENT_PROGRAM_VALUE = 0x8B8D;
constexpr GLenum GL_ARRAY_BUFFER_VALUE = 0x8892;
constexpr GLenum GL_ARRAY_BUFFER_BINDING_VALUE = 0x8894;
constexpr GLenum GL_VERTEX_ARRAY_BINDING_VALUE = 0x85B5;
constexpr GLenum GL_DYNAMIC_DRAW_VALUE = 0x88E8;
constexpr GLenum GL_ACTIVE_TEXTURE_VALUE = 0x84E0;
constexpr GLenum GL_TEXTURE0_VALUE = 0x84C0;
constexpr GLenum GL_TEXTURE1_VALUE = 0x84C1;
constexpr GLenum GL_TEXTURE2_VALUE = 0x84C2;
constexpr GLenum GL_TEXTURE3_VALUE = 0x84C3;
constexpr GLenum GL_TEXTURE4_VALUE = 0x84C4;
constexpr GLenum GL_TEXTURE5_VALUE = 0x84C5;
constexpr GLenum GL_UNPACK_ALIGNMENT_VALUE = 0x0CF5;
constexpr GLenum GL_DEPTH_ATTACHMENT_VALUE = 0x8D00;
constexpr GLenum GL_DEPTH_COMPONENT_VALUE = 0x1902;
constexpr GLint GL_DEPTH_COMPONENT24_VALUE = 0x81A6;
constexpr GLenum GL_DEPTH_FUNC_VALUE = 0x0B74;
constexpr GLenum GL_DEPTH_WRITEMASK_VALUE = 0x0B72;

bool render_logging_enabled() {
    static int enabled = -1;
    if (enabled < 0) {
        char value[8]{};
        enabled = GetEnvironmentVariableA("HYTALEVR_DEBUG_LOGS", value, sizeof(value)) > 0 &&
                  value[0] != '\0' && value[0] != '0';
    }
    return enabled != 0;
}

std::string render_log_path() {
    char temp_path[MAX_PATH]{};
    DWORD length = GetTempPathA(static_cast<DWORD>(sizeof(temp_path)), temp_path);
    std::string base = (length > 0 && length < sizeof(temp_path)) ? temp_path : ".\\";
    std::string dir = base + "HytaleVR";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir + "\\hytale_vr_render_debug.log";
}

void render_log(const std::string& message) {
    if (!render_logging_enabled()) return;
    std::lock_guard<std::mutex> lock(g_render_log_mutex);
    std::ofstream log(render_log_path(), std::ios_base::app);
    if (!log.is_open()) return;
    log << GetTickCount64() << " " << message << "\n";
}

void item_diagnostic_log_once(uint32_t bit, const std::string& message) {
    static std::atomic<uint32_t> logged_bits{0};
    const uint32_t previous =
        logged_bits.fetch_or(bit, std::memory_order_relaxed);
    if ((previous & bit) != 0) return;
    std::lock_guard<std::mutex> lock(g_render_log_mutex);
    std::ofstream log(render_log_path(), std::ios_base::app);
    if (!log.is_open()) return;
    log << GetTickCount64() << " held-item diagnostic: " << message << "\n";
}

std::string matrix_summary(const Matrix4& matrix) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(4)
        << "r0=(" << matrix.value[0] << "," << matrix.value[4] << ","
        << matrix.value[8] << "," << matrix.value[12] << ") "
        << "r1=(" << matrix.value[1] << "," << matrix.value[5] << ","
        << matrix.value[9] << "," << matrix.value[13] << ") "
        << "r2=(" << matrix.value[2] << "," << matrix.value[6] << ","
        << matrix.value[10] << "," << matrix.value[14] << ")";
    return out.str();
}

float matrix_max_delta(const Matrix4& a, const Matrix4& b) {
    float result = 0.0f;
    for (int i = 0; i < 16; ++i) {
        result = std::max(result, std::fabs(a.value[i] - b.value[i]));
    }
    return result;
}

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct HandVertex {
    Vec3 position{};
    Vec3 normal{};
    float u = 0.5f;
    float v = 0.5f;
};

std::vector<HandVertex> g_hand_vertices;
bool g_hand_model_loaded = false;
bool g_hand_model_attempted = false;
std::vector<uint32_t> g_hand_texture_pixels;
uint32_t g_hand_texture_width = 0;
uint32_t g_hand_texture_height = 0;
bool g_hand_texture_loaded = false;
bool g_hand_texture_attempted = false;
ULONG_PTR g_hand_gdiplus_token = 0;
GLuint g_hand_texture_gl = 0;
GLuint g_torch_flame_texture_gl = 0;
uint32_t g_torch_flame_texture_width = 0;
uint32_t g_torch_flame_texture_height = 0;
bool g_torch_flame_texture_attempted = false;
GLuint g_hand_program = 0;
GLuint g_hand_vao = 0;
GLuint g_hand_vbo = 0;
GLuint g_hand_item_depth_texture = 0;
int g_hand_item_depth_width = 0;
int g_hand_item_depth_height = 0;
bool g_hand_item_depth_active = false;
GLint g_hand_texture_uniform = -1;
GLint g_hand_scene_depth_uniform = -1;
GLint g_hand_use_scene_depth_uniform = -1;
GLint g_hand_viewport_size_uniform = -1;
GLint g_hand_depth_far_clip_uniform = -1;
GLint g_hand_depth_bias_uniform = -1;
bool g_hand_gl_attempted = false;
bool g_hand_gl_ready = false;

struct HeldItemAtlasKey {
    GLuint texture = 0;
    int offset_x = 0;
    int offset_y = 0;
    int atlas_width = 0;
    int atlas_height = 0;
    GLuint vertex_array = 0;
    int index_count = 0;
    int uv_min_x = 0;
    int uv_min_y = 0;
    int uv_max_x = 0;
    int uv_max_y = 0;

    bool operator==(const HeldItemAtlasKey& other) const {
        return texture == other.texture &&
               offset_x == other.offset_x &&
               offset_y == other.offset_y &&
               atlas_width == other.atlas_width &&
               atlas_height == other.atlas_height &&
               vertex_array == other.vertex_array &&
               index_count == other.index_count &&
               uv_min_x == other.uv_min_x &&
               uv_min_y == other.uv_min_y &&
               uv_max_x == other.uv_max_x &&
               uv_max_y == other.uv_max_y;
    }
};

struct CapturedHeldItemRegion {
    std::vector<uint8_t> rgba;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t signature = 0;
};

struct HeldItemCacheKey {
    uint64_t texture_signature = 0;
    int index_count = 0;
    int uv_span_x = 0;
    int uv_span_y = 0;

    bool operator==(const HeldItemCacheKey& other) const {
        return texture_signature == other.texture_signature &&
               index_count == other.index_count &&
               uv_span_x == other.uv_span_x &&
               uv_span_y == other.uv_span_y;
    }
};

struct HeldItemCacheKeyHash {
    size_t operator()(const HeldItemCacheKey& value) const {
        size_t hash = static_cast<size_t>(value.texture_signature);
        hash ^= static_cast<size_t>(
            value.texture_signature >> 32) * 0x9e3779b1u;
        hash ^= static_cast<size_t>(
            static_cast<uint32_t>(value.index_count)) << 1;
        hash ^= static_cast<size_t>(
            static_cast<uint32_t>(value.uv_span_x)) << 17;
        hash ^= static_cast<size_t>(
            static_cast<uint32_t>(value.uv_span_y)) << 25;
        return hash;
    }
};

struct CachedHeldItemAsset {
    size_t definition_index = 0;
    hytalevr::VrItemRenderAsset asset;
    uint64_t last_used = 0;
};

struct LoadedVrItemState {
    HeldItemAtlasKey key{};
    HeldItemCacheKey identity{};
    bool key_valid = false;
    bool identity_valid = false;
    bool asset_ready = false;
    size_t definition_index = 0;
    LONG last_verified_frame = -1;
    hytalevr::VrItemRenderAsset asset;
    GLuint texture_gl = 0;
};

struct PendingHeldItemDetection {
    HeldItemCacheKey identity{};
    bool identity_valid = false;
    size_t definition_index = (std::numeric_limits<size_t>::max)();
    bool fallback = false;
    int confirmations = 0;
    LONG last_frame = -1;
};

std::mutex g_item_catalog_mutex;
std::shared_ptr<hytalevr::VrItemAssetCatalog> g_item_catalog;
std::atomic<int> g_item_catalog_state{0};
std::thread g_item_catalog_thread;
LoadedVrItemState g_loaded_items[2];
PendingHeldItemDetection g_pending_item_detections[2];
std::unordered_map<HeldItemCacheKey, CachedHeldItemAsset, HeldItemCacheKeyHash>
    g_held_item_asset_cache;
uint64_t g_held_item_cache_sequence = 0;
std::atomic<ULONGLONG> g_held_item_detection_hold_until[2]{};
std::atomic<ULONGLONG> g_held_item_suppressed_until[2]{};
std::atomic<LONG> g_stable_held_item_presence_mask{0};
LONG g_last_item_detection_frame = -1;

GLuint g_afw_program = 0;
GLuint g_afw_vao = 0;
GLuint g_afw_vbo = 0;
GLint g_afw_source_color_uniform = -1;
GLint g_afw_source_depth_uniform = -1;
GLint g_afw_held_item_color_uniform = -1;
GLint g_afw_held_item_depth_uniform = -1;
GLint g_afw_use_held_item_depth_uniform = -1;
GLint g_afw_remove_held_item_background_uniform = -1;
GLint g_afw_source_texel_size_uniform = -1;
GLint g_afw_eye_scene_depth_uniform = -1;
GLint g_afw_composite_held_item_uniform = -1;
GLint g_afw_held_item_uv_shift_uniform = -1;
GLint g_afw_held_item_source_center_uniform = -1;
GLint g_afw_held_item_radius_uniform = -1;
GLint g_afw_held_item_depth_bias_uniform = -1;
GLint g_afw_inverse_source_projection_uniform = -1;
GLint g_afw_source_projection_uniform = -1;
GLint g_afw_inverse_target_projection_uniform = -1;
GLint g_afw_source_to_target_view_uniform = -1;
GLint g_afw_target_projection_uniform = -1;
GLint g_afw_depth_far_clip_uniform = -1;
GLint g_afw_enable_warp_uniform = -1;
GLint g_afw_output_depth_uniform = -1;
GLint g_afw_distortion_field_uniform = -1;
GLint g_afw_apply_distortion_uniform = -1;
GLint g_afw_sharpen_enabled_uniform = -1;
GLint g_afw_sharpen_strength_uniform = -1;
GLint g_afw_edge_aa_enabled_uniform = -1;
Matrix4 g_afw_source_view = identity_matrix();
Matrix4 g_afw_source_projection = identity_matrix();
Matrix4 g_afw_eye_view[2]{identity_matrix(), identity_matrix()};
Matrix4 g_afw_eye_projection[2]{identity_matrix(), identity_matrix()};
Matrix4 g_afw_source_to_eye_view[2]{identity_matrix(), identity_matrix()};
bool g_afw_camera_valid = false;
bool g_afw_gl_attempted = false;
bool g_afw_gl_ready = false;
uint32_t g_afw_warped_frames = 0;

std::wstring module_directory() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(g_module, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) slash[1] = L'\0';
    return path;
}

bool file_exists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring parent_directory(std::wstring path) {
    while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) {
        path.pop_back();
    }
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring find_hytale_assets_zip() {
    wchar_t executable[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0) {
        const std::wstring client_directory = parent_directory(executable);
        const std::wstring game_directory = parent_directory(client_directory);
        const std::wstring beside_client = game_directory + L"\\Assets.zip";
        if (file_exists(beside_client)) return beside_client;
    }

    wchar_t appdata[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH) == 0) return {};
    const std::wstring install =
        std::wstring(appdata) + L"\\Hytale\\install\\";
    const std::array<std::wstring, 2> channels{L"release", L"pre-release"};
    for (const std::wstring& channel : channels) {
        const std::wstring candidate =
            install + channel + L"\\package\\game\\latest\\Assets.zip";
        if (file_exists(candidate)) return candidate;
    }
    return {};
}

void ensure_item_catalog_loading() {
    int expected = 0;
    if (!g_item_catalog_state.compare_exchange_strong(expected, 1)) return;
    const std::wstring assets_zip = find_hytale_assets_zip();
    if (assets_zip.empty()) {
        g_item_catalog_state.store(-1);
        render_log("item assets: Hytale Assets.zip was not found");
        return;
    }
    try {
        g_item_catalog_thread = std::thread([assets_zip]() {
            auto catalog = std::make_shared<hytalevr::VrItemAssetCatalog>();
            std::string error;
            if (!hytalevr::load_vr_item_asset_catalog(
                    assets_zip, *catalog, error)) {
                render_log("item assets: catalog load failed: " + error);
                item_diagnostic_log_once(
                    1u, "catalog load failed: " + error);
                g_item_catalog_state.store(-1);
                return;
            }
            const size_t definitions = catalog->definitions.size();
            const size_t candidates = catalog->texture_candidates.size();
            {
                std::lock_guard<std::mutex> lock(g_item_catalog_mutex);
                g_item_catalog = std::move(catalog);
            }
            g_item_catalog_state.store(2);
            std::ostringstream out;
            out << "item assets: catalog ready definitions=" << definitions
                << " textures=" << candidates;
            render_log(out.str());
            item_diagnostic_log_once(2u, out.str());
        });
    } catch (...) {
        g_item_catalog_state.store(-1);
        render_log("item assets: catalog worker could not be started");
    }
}

void join_item_catalog_loader() {
    if (g_item_catalog_thread.joinable()) {
        g_item_catalog_thread.join();
    }
}

bool ensure_hand_gdiplus() {
    if (g_hand_gdiplus_token != 0) return true;
    Gdiplus::GdiplusStartupInput input{};
    return Gdiplus::GdiplusStartup(&g_hand_gdiplus_token, &input, nullptr) == Gdiplus::Ok;
}

struct PngImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint32_t> pixels;
};

struct CachedAvatarPart {
    std::string id;
    std::string variant;
};

struct AvatarPartTexture {
    std::string model;
    std::string texture;
    std::string gradient_set;
    std::string gradient_variant;
    bool greyscale = false;
};

std::wstring widen_asset_path(std::string path) {
    for (char& ch : path) {
        if (ch == '/') ch = '\\';
    }
    return std::wstring(path.begin(), path.end());
}

std::wstring character_creator_asset_path(const std::string& relative_path) {
    return module_directory() + L"assets\\character_creator\\" + widen_asset_path(relative_path);
}

bool read_text_file(const std::wstring& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return true;
}

std::string extract_json_string_value(const std::string& text, const std::string& key) {
    const std::string quoted_key = "\"" + key + "\"";
    size_t pos = text.find(quoted_key);
    if (pos == std::string::npos) return {};
    pos = text.find(':', pos + quoted_key.size());
    if (pos == std::string::npos) return {};
    ++pos;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    if (pos >= text.size() || text[pos] == 'n') return {};
    if (text[pos] != '"') return {};
    ++pos;
    std::string value;
    bool escape = false;
    for (; pos < text.size(); ++pos) {
        const char ch = text[pos];
        if (escape) {
            value.push_back(ch);
            escape = false;
        } else if (ch == '\\') {
            escape = true;
        } else if (ch == '"') {
            break;
        } else {
            value.push_back(ch);
        }
    }
    return value;
}

CachedAvatarPart split_cached_avatar_part(const std::string& value) {
    CachedAvatarPart part{};
    if (value.empty()) return part;
    const size_t dot = value.rfind('.');
    if (dot == std::string::npos) {
        part.id = value;
    } else {
        part.id = value.substr(0, dot);
        part.variant = value.substr(dot + 1);
    }
    return part;
}

std::string find_top_level_json_object_by_id(const std::string& catalog, const std::string& id) {
    bool in_string = false;
    bool escape = false;
    int depth = 0;
    size_t object_start = std::string::npos;
    for (size_t i = 0; i < catalog.size(); ++i) {
        const char ch = catalog[i];
        if (in_string) {
            if (escape) {
                escape = false;
            } else if (ch == '\\') {
                escape = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (ch == '"') {
            in_string = true;
        } else if (ch == '{') {
            if (depth == 0) object_start = i;
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0 && object_start != std::string::npos) {
                const std::string object = catalog.substr(object_start, i - object_start + 1);
                if (extract_json_string_value(object, "Id") == id) return object;
                object_start = std::string::npos;
            }
        }
    }
    return {};
}

std::string find_nested_json_object(const std::string& text, const std::string& key) {
    const std::string quoted_key = "\"" + key + "\"";
    size_t pos = text.find(quoted_key);
    if (pos == std::string::npos) return {};
    pos = text.find('{', pos + quoted_key.size());
    if (pos == std::string::npos) return {};
    bool in_string = false;
    bool escape = false;
    int depth = 0;
    for (size_t i = pos; i < text.size(); ++i) {
        const char ch = text[i];
        if (in_string) {
            if (escape) {
                escape = false;
            } else if (ch == '\\') {
                escape = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (ch == '"') {
            in_string = true;
        } else if (ch == '{') {
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0) return text.substr(pos, i - pos + 1);
        }
    }
    return {};
}

float extract_json_number_value(const std::string& text, const std::string& key,
                                float fallback = 0.0f) {
    const std::string quoted_key = "\"" + key + "\"";
    size_t pos = text.find(quoted_key);
    if (pos == std::string::npos) return fallback;
    pos = text.find(':', pos + quoted_key.size());
    if (pos == std::string::npos) return fallback;
    ++pos;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    char* end = nullptr;
    const float value = std::strtof(text.c_str() + pos, &end);
    return end == text.c_str() + pos ? fallback : value;
}

bool extract_json_bool_value(const std::string& text, const std::string& key,
                             bool fallback = false) {
    const std::string quoted_key = "\"" + key + "\"";
    size_t pos = text.find(quoted_key);
    if (pos == std::string::npos) return fallback;
    pos = text.find(':', pos + quoted_key.size());
    if (pos == std::string::npos) return fallback;
    ++pos;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    if (text.compare(pos, 4, "true") == 0) return true;
    if (text.compare(pos, 5, "false") == 0) return false;
    return fallback;
}

std::string json_object_around(const std::string& text, size_t position) {
    bool in_string = false;
    bool escape = false;
    std::vector<size_t> stack;
    size_t best_start = std::string::npos;
    size_t best_end = std::string::npos;
    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (in_string) {
            if (escape) {
                escape = false;
            } else if (ch == '\\') {
                escape = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (ch == '"') {
            in_string = true;
        } else if (ch == '{') {
            stack.push_back(i);
        } else if (ch == '}') {
            if (!stack.empty()) {
                const size_t start = stack.back();
                stack.pop_back();
                if (position >= start && position <= i &&
                    (best_start == std::string::npos || start > best_start)) {
                    best_start = start;
                    best_end = i;
                }
            }
        }
    }
    if (best_start != std::string::npos && best_end >= best_start) {
        return text.substr(best_start, best_end - best_start + 1);
    }
    return {};
}

std::string first_texture_from_textures_object(const std::string& object) {
    const size_t textures_pos = object.find("\"Textures\"");
    if (textures_pos == std::string::npos) return {};
    const size_t texture_pos = object.find("\"Texture\"", textures_pos);
    if (texture_pos == std::string::npos) return {};
    return extract_json_string_value(object.substr(texture_pos), "Texture");
}

std::string texture_from_textures_object(const std::string& object,
                                         const std::string& variant) {
    if (!variant.empty()) {
        const std::string variant_object = find_nested_json_object(object, variant);
        const std::string texture = extract_json_string_value(variant_object, "Texture");
        if (!texture.empty()) return texture;
    }
    return first_texture_from_textures_object(object);
}

bool load_png_image(const std::wstring& path, PngImage& image) {
    if (!ensure_hand_gdiplus()) return false;
    Gdiplus::Bitmap bitmap(path.c_str());
    if (bitmap.GetLastStatus() != Gdiplus::Ok || bitmap.GetWidth() == 0 ||
        bitmap.GetHeight() == 0) {
        return false;
    }
    image.width = bitmap.GetWidth();
    image.height = bitmap.GetHeight();
    image.pixels.assign(static_cast<size_t>(image.width) * image.height, 0);
    for (uint32_t y = 0; y < image.height; ++y) {
        for (uint32_t x = 0; x < image.width; ++x) {
            Gdiplus::Color color{};
            bitmap.GetPixel(static_cast<INT>(x), static_cast<INT>(y), &color);
            image.pixels[static_cast<size_t>(y) * image.width + x] =
                (static_cast<uint32_t>(color.GetAlpha()) << 24) |
                (static_cast<uint32_t>(color.GetRed()) << 16) |
                (static_cast<uint32_t>(color.GetGreen()) << 8) |
                static_cast<uint32_t>(color.GetBlue());
        }
    }
    return true;
}

uint32_t apply_gradient_pixel(uint32_t greyscale_pixel, const PngImage& gradient) {
    const uint32_t alpha = (greyscale_pixel >> 24) & 0xff;
    const uint32_t red = (greyscale_pixel >> 16) & 0xff;
    const uint32_t green = (greyscale_pixel >> 8) & 0xff;
    const uint32_t blue = greyscale_pixel & 0xff;
    const uint32_t grey = (red + green + blue) / 3;
    if (gradient.pixels.empty() || gradient.width == 0 || gradient.height == 0) {
        return greyscale_pixel;
    }
    const uint32_t x = std::min<uint32_t>(
        gradient.width - 1,
        static_cast<uint32_t>((static_cast<float>(grey) / 255.0f) *
                              static_cast<float>(gradient.width - 1)));
    const uint32_t y = gradient.height / 2;
    const uint32_t mapped = gradient.pixels[static_cast<size_t>(y) * gradient.width + x];
    return (alpha << 24) | (mapped & 0x00ffffffu);
}

PngImage colorize_greyscale_image(const PngImage& greyscale, const PngImage& gradient) {
    PngImage result{};
    result.width = greyscale.width;
    result.height = greyscale.height;
    result.pixels.resize(greyscale.pixels.size());
    for (size_t i = 0; i < greyscale.pixels.size(); ++i) {
        result.pixels[i] = apply_gradient_pixel(greyscale.pixels[i], gradient);
    }
    return result;
}

void alpha_composite_same_size(PngImage& destination, const PngImage& source) {
    if (destination.width != source.width || destination.height != source.height ||
        destination.pixels.empty() || source.pixels.empty()) {
        return;
    }
    for (size_t i = 0; i < destination.pixels.size(); ++i) {
        const uint32_t src = source.pixels[i];
        const uint32_t alpha = (src >> 24) & 0xff;
        if (alpha == 0) continue;
        if (alpha == 255) {
            destination.pixels[i] = 0xff000000u | (src & 0x00ffffffu);
            continue;
        }
        const uint32_t dst = destination.pixels[i];
        const uint32_t sr = (src >> 16) & 0xff;
        const uint32_t sg = (src >> 8) & 0xff;
        const uint32_t sb = src & 0xff;
        const uint32_t dr = (dst >> 16) & 0xff;
        const uint32_t dg = (dst >> 8) & 0xff;
        const uint32_t db = dst & 0xff;
        const uint32_t inv = 255 - alpha;
        const uint32_t out_r = (sr * alpha + dr * inv) / 255;
        const uint32_t out_g = (sg * alpha + dg * inv) / 255;
        const uint32_t out_b = (sb * alpha + db * inv) / 255;
        destination.pixels[i] = 0xff000000u | (out_r << 16) | (out_g << 8) | out_b;
    }
}

std::wstring latest_cached_player_skin_path() {
    wchar_t appdata[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH) == 0) return {};
    const std::wstring pattern =
        std::wstring(appdata) + L"\\Hytale\\UserData\\CachedPlayerSkins\\*.json";
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return {};
    std::wstring best;
    FILETIME best_time{};
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        if (best.empty() || CompareFileTime(&data.ftLastWriteTime, &best_time) > 0) {
            best_time = data.ftLastWriteTime;
            const std::wstring directory = pattern.substr(0, pattern.rfind(L'\\') + 1);
            best = directory + data.cFileName;
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return best;
}

std::string catalog_file_for_part(const char* category) {
    return std::string("CharacterCreator\\") + category + ".json";
}

bool resolve_avatar_part_texture(const char* category, const std::string& cached_value,
                                 AvatarPartTexture& out) {
    const CachedAvatarPart part = split_cached_avatar_part(cached_value);
    if (part.id.empty()) return false;
    std::string catalog;
    if (!read_text_file(character_creator_asset_path(catalog_file_for_part(category)), catalog)) {
        return false;
    }
    const std::string object = find_top_level_json_object_by_id(catalog, part.id);
    if (object.empty()) return false;
    out.model = extract_json_string_value(object, "Model");
    const std::string greyscale = extract_json_string_value(object, "GreyscaleTexture");
    if (!greyscale.empty()) {
        out.texture = greyscale;
        out.gradient_set = extract_json_string_value(object, "GradientSet");
        out.gradient_variant = part.variant;
        out.greyscale = true;
        return true;
    }
    const std::string full_color = texture_from_textures_object(object, part.variant);
    if (!full_color.empty()) {
        out.texture = full_color;
        out.gradient_variant = part.variant;
        out.greyscale = false;
        return true;
    }
    return false;
}

bool gradient_texture_for_part(const AvatarPartTexture& part, std::string& out_texture) {
    if (part.gradient_set.empty() || part.gradient_variant.empty()) return false;
    std::string gradients;
    if (!read_text_file(character_creator_asset_path("CharacterCreator\\GradientSets.json"),
                        gradients)) {
        return false;
    }
    const std::string set_object =
        find_top_level_json_object_by_id(gradients, part.gradient_set);
    const std::string variant_object =
        find_nested_json_object(set_object, part.gradient_variant);
    out_texture = extract_json_string_value(variant_object, "Texture");
    return !out_texture.empty();
}

bool load_avatar_part_image(const AvatarPartTexture& part, PngImage& image) {
    PngImage source{};
    if (!load_png_image(character_creator_asset_path(part.texture), source)) return false;
    if (!part.greyscale) {
        image = std::move(source);
        return true;
    }
    std::string gradient_texture;
    if (!gradient_texture_for_part(part, gradient_texture)) {
        image = std::move(source);
        return true;
    }
    PngImage gradient{};
    if (!load_png_image(character_creator_asset_path(gradient_texture), gradient)) {
        image = std::move(source);
        return true;
    }
    image = colorize_greyscale_image(source, gradient);
    return true;
}

bool build_user_avatar_hand_texture(PngImage& result) {
    const std::wstring cache_path = latest_cached_player_skin_path();
    if (cache_path.empty()) return false;
    std::string cached_skin;
    if (!read_text_file(cache_path, cached_skin)) return false;

    AvatarPartTexture body_part{};
    const std::string body = extract_json_string_value(cached_skin, "bodyCharacteristic");
    if (!resolve_avatar_part_texture("BodyCharacteristics", body, body_part)) return false;
    if (!load_avatar_part_image(body_part, result)) return false;

    std::ostringstream out;
    out << "hand texture: built from cached avatar " << result.width << "x" << result.height;
    render_log(out.str());
    return result.width > 0 && result.height > 0 && !result.pixels.empty();
}

bool load_hand_texture() {
    if (g_hand_texture_attempted) return g_hand_texture_loaded;
    g_hand_texture_attempted = true;
    if (!ensure_hand_gdiplus()) {
        render_log("hand texture: GDI+ startup failed");
        return false;
    }

    PngImage texture{};
    if (!build_user_avatar_hand_texture(texture)) {
        const std::wstring path = module_directory() + L"assets\\vr_hands\\Player_Greyscale.png";
        if (!load_png_image(path, texture)) {
            render_log("hand texture: Player_Greyscale.png not found or invalid");
            return false;
        }
        render_log("hand texture: using fallback Player_Greyscale.png");
    }

    g_hand_texture_width = texture.width;
    g_hand_texture_height = texture.height;
    g_hand_texture_pixels.resize(texture.pixels.size());
    for (size_t i = 0; i < texture.pixels.size(); ++i) {
        g_hand_texture_pixels[i] = texture.pixels[i] & 0x00ffffffu;
    }
    g_hand_texture_loaded = true;
    std::ostringstream out;
    out << "hand texture: loaded " << g_hand_texture_width << "x" << g_hand_texture_height;
    render_log(out.str());
    return true;
}

void sample_hand_texture(float u, float v, float shade,
                         float& red, float& green, float& blue) {
    if (!g_hand_texture_loaded || g_hand_texture_pixels.empty() ||
        g_hand_texture_width == 0 || g_hand_texture_height == 0) {
        red = 0.84f * shade;
        green = 0.84f * shade;
        blue = 0.80f * shade;
        return;
    }

    u = u - std::floor(u);
    v = v - std::floor(v);
    const uint32_t x = std::min<uint32_t>(
        g_hand_texture_width - 1,
        static_cast<uint32_t>(u * static_cast<float>(g_hand_texture_width - 1)));
    const uint32_t y = std::min<uint32_t>(
        g_hand_texture_height - 1,
        static_cast<uint32_t>((1.0f - v) * static_cast<float>(g_hand_texture_height - 1)));
    const uint32_t pixel = g_hand_texture_pixels[static_cast<size_t>(y) * g_hand_texture_width + x];
    red = static_cast<float>((pixel >> 16) & 0xff) / 255.0f * shade;
    green = static_cast<float>((pixel >> 8) & 0xff) / 255.0f * shade;
    blue = static_cast<float>(pixel & 0xff) / 255.0f * shade;
}

template <typename T>
T load_gl_proc(const char* name) {
    PROC proc = wglGetProcAddress(name);
    if (!proc || proc == reinterpret_cast<PROC>(1) ||
        proc == reinterpret_cast<PROC>(2) ||
        proc == reinterpret_cast<PROC>(3) ||
        proc == reinterpret_cast<PROC>(-1)) {
        HMODULE opengl = GetModuleHandleA("opengl32.dll");
        if (opengl) proc = GetProcAddress(opengl, name);
    }
    return reinterpret_cast<T>(proc);
}

bool load_hand_gl_functions() {
    if (g_gl_create_shader) return true;
    g_gl_create_shader = load_gl_proc<GlCreateShaderFn>("glCreateShader");
    g_gl_shader_source = load_gl_proc<GlShaderSourceFn>("glShaderSource");
    g_gl_compile_shader = load_gl_proc<GlCompileShaderFn>("glCompileShader");
    g_gl_get_shader_iv = load_gl_proc<GlGetShaderivFn>("glGetShaderiv");
    g_gl_get_shader_info_log = load_gl_proc<GlGetShaderInfoLogFn>("glGetShaderInfoLog");
    g_gl_create_program = load_gl_proc<GlCreateProgramFn>("glCreateProgram");
    g_gl_attach_shader = load_gl_proc<GlAttachShaderFn>("glAttachShader");
    g_gl_bind_attrib_location = load_gl_proc<GlBindAttribLocationFn>("glBindAttribLocation");
    g_gl_link_program = load_gl_proc<GlLinkProgramFn>("glLinkProgram");
    g_gl_get_program_iv = load_gl_proc<GlGetProgramivFn>("glGetProgramiv");
    g_gl_get_program_info_log = load_gl_proc<GlGetProgramInfoLogFn>("glGetProgramInfoLog");
    g_gl_delete_shader = load_gl_proc<GlDeleteShaderFn>("glDeleteShader");
    g_gl_use_program = load_gl_proc<GlUseProgramFn>("glUseProgram");
    g_gl_get_uniform_location = load_gl_proc<GlGetUniformLocationFn>("glGetUniformLocation");
    g_gl_uniform_1i = load_gl_proc<GlUniform1iFn>("glUniform1i");
    g_gl_uniform_1f = load_gl_proc<GlUniform1fFn>("glUniform1f");
    g_gl_uniform_2f = load_gl_proc<GlUniform2fFn>("glUniform2f");
    g_gl_uniform_matrix_4fv =
        load_gl_proc<GlUniformMatrix4fvFn>("glUniformMatrix4fv");
    g_gl_gen_vertex_arrays = load_gl_proc<GlGenVertexArraysFn>("glGenVertexArrays");
    g_gl_bind_vertex_array = load_gl_proc<GlBindVertexArrayFn>("glBindVertexArray");
    g_gl_gen_buffers = load_gl_proc<GlGenBuffersFn>("glGenBuffers");
    g_gl_bind_buffer = load_gl_proc<GlBindBufferFn>("glBindBuffer");
    g_gl_buffer_data = load_gl_proc<GlBufferDataFn>("glBufferData");
    g_gl_enable_vertex_attrib_array =
        load_gl_proc<GlEnableVertexAttribArrayFn>("glEnableVertexAttribArray");
    g_gl_vertex_attrib_pointer =
        load_gl_proc<GlVertexAttribPointerFn>("glVertexAttribPointer");
    g_gl_active_texture = load_gl_proc<GlActiveTextureFn>("glActiveTexture");
    g_gl_get_texture_sub_image =
        load_gl_proc<GlGetTextureSubImageFn>("glGetTextureSubImage");

    return g_gl_create_shader && g_gl_shader_source && g_gl_compile_shader &&
        g_gl_get_shader_iv && g_gl_create_program && g_gl_attach_shader &&
        g_gl_bind_attrib_location && g_gl_link_program && g_gl_get_program_iv &&
        g_gl_delete_shader && g_gl_use_program && g_gl_get_uniform_location &&
        g_gl_uniform_1i && g_gl_uniform_1f && g_gl_uniform_2f &&
        g_gl_uniform_matrix_4fv &&
        g_gl_gen_vertex_arrays && g_gl_bind_vertex_array &&
        g_gl_gen_buffers && g_gl_bind_buffer && g_gl_buffer_data &&
        g_gl_enable_vertex_attrib_array && g_gl_vertex_attrib_pointer &&
        g_gl_active_texture;
}

GLuint compile_gl_shader(GLenum type, const char* source, const char* name) {
    GLuint shader = g_gl_create_shader(type);
    g_gl_shader_source(shader, 1, &source, nullptr);
    g_gl_compile_shader(shader);
    GLint ok = 0;
    g_gl_get_shader_iv(shader, GL_COMPILE_STATUS_VALUE, &ok);
    if (!ok) {
        GLint length = 0;
        if (g_gl_get_shader_iv) g_gl_get_shader_iv(shader, GL_INFO_LOG_LENGTH_VALUE, &length);
        std::string log(static_cast<size_t>(std::max(1, length)), '\0');
        if (g_gl_get_shader_info_log && length > 1) {
            g_gl_get_shader_info_log(shader, length, nullptr, log.data());
        }
        std::ostringstream out;
        out << name << " shader compile failed " << log.c_str();
        render_log(out.str());
        g_gl_delete_shader(shader);
        return 0;
    }
    return shader;
}

bool ensure_afw_gl_renderer() {
    if (g_afw_gl_attempted) return g_afw_gl_ready;
    g_afw_gl_attempted = true;
    if (!load_hand_gl_functions()) {
        render_log("AFW: required OpenGL functions are unavailable");
        return false;
    }

    constexpr const char* kVertexSource = R"GLSL(
#version 130
in vec2 aPos;
in vec2 aUv;
out vec2 vUv;
void main() {
    vUv = aUv;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";
    constexpr const char* kFragmentSource = R"GLSL(
#version 130
uniform sampler2D uSourceColor;
uniform sampler2D uSourceDepth;
uniform sampler2D uHeldItemColor;
uniform sampler2D uHeldItemDepth;
uniform sampler2D uEyeSceneDepth;
uniform sampler2D uDistortionField;
uniform mat4 uInverseSourceProjection;
uniform mat4 uSourceProjection;
uniform mat4 uInverseTargetProjection;
uniform mat4 uSourceToTargetView;
uniform mat4 uTargetProjection;
uniform float uDepthFarClip;
// 0: direct copy, 1: projection correction, 2: depth stereo warp.
uniform int uEnableWarp;
uniform int uOutputDepth;
uniform int uApplyDistortion;
uniform int uUseHeldItemDepth;
uniform int uRemoveHeldItemFromBackground;
uniform int uCompositeHeldItem;
uniform int uSharpenEnabled;
uniform float uSharpenStrength;
uniform int uEdgeAaEnabled;
uniform vec2 uSourceTexelSize;
uniform vec2 uHeldItemUvShift;
uniform vec2 uHeldItemSourceCenter;
uniform float uHeldItemRadius;
uniform float uHeldItemDepthBias;
in vec2 vUv;
out vec4 fragColor;

vec3 linearToSrgb(vec3 colour) {
    vec3 bounded = clamp(colour, vec3(0.0), vec3(1.0));
    vec3 low = bounded * 12.92;
    vec3 high = 1.055 * pow(bounded, vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), bounded));
}

vec3 sourceViewPosition(vec2 uv, float linearDepth) {
    vec4 ray = uInverseSourceProjection * vec4(uv * 2.0 - 1.0, -1.0, 1.0);
    ray /= max(abs(ray.w), 0.00001);
    return ray.xyz * (linearDepth / max(-ray.z, 0.0001));
}

float combinedLinearDepth(vec2 uv) {
    float linearDepth = texture(uSourceDepth, uv).r * uDepthFarClip;
    if (uUseHeldItemDepth == 0) return linearDepth;

    float hardwareDepth = texture(uHeldItemDepth, uv).r;
    if (hardwareDepth >= 0.999999) return linearDepth;
    vec4 itemView = uInverseSourceProjection *
        vec4(uv * 2.0 - 1.0, hardwareDepth * 2.0 - 1.0, 1.0);
    if (abs(itemView.w) <= 0.00001) return linearDepth;
    itemView /= itemView.w;
    float itemDepth = max(-itemView.z, 0.0);
    if (itemDepth <= 0.001 || itemDepth >= uDepthFarClip) return linearDepth;
    return min(linearDepth, itemDepth);
}

vec2 projectionCorrectedSourceUv(vec2 targetUv) {
    vec4 targetRay = uInverseTargetProjection *
        vec4(targetUv * 2.0 - 1.0, -1.0, 1.0);
    targetRay /= max(abs(targetRay.w), 0.00001);
    vec4 sourceClip = uSourceProjection * vec4(targetRay.xyz, 1.0);
    if (abs(sourceClip.w) <= 0.00001) return targetUv;
    return sourceClip.xy / sourceClip.w * 0.5 + 0.5;
}

vec2 mirroredSourceUv(vec2 uv) {
    // A generated eye can reveal a thin strip that is outside the source eye.
    // Reflect nearby real pixels instead of stretching the final texel column.
    if (uv.x < 0.0) uv.x = -uv.x;
    if (uv.x > 1.0) uv.x = 2.0 - uv.x;
    if (uv.y < 0.0) uv.y = -uv.y;
    if (uv.y > 1.0) uv.y = 2.0 - uv.y;
    return clamp(uv, vec2(0.001), vec2(0.999));
}

vec2 vrEffectSourceUv(vec2 uv) {
    if (uApplyDistortion == 0) return uv;
    // Hytale's neutral distortion value is (0, 0), so RG is a signed UV
    // displacement. Limit pathological values to avoid stretching an eye.
    vec2 displacement = clamp(texture(uDistortionField, uv).rg,
                              vec2(-0.04), vec2(0.04));
    return mirroredSourceUv(uv + displacement);
}

bool hasHeldItemDepth(vec2 uv) {
    if (uRemoveHeldItemFromBackground == 0) return false;
    return texture(uHeldItemDepth, clamp(uv, vec2(0.001), vec2(0.999))).r < 0.999999;
}

vec2 backgroundSourceUv(vec2 uv) {
    vec2 boundedUv = clamp(uv, vec2(0.001), vec2(0.999));
    if (!hasHeldItemDepth(boundedUv)) return boundedUv;

    // The final Hytale frame already contains the item. Find the nearest
    // neighbouring final-colour pixel outside its depth mask so the regular
    // AFW pass does not leave a second, flat copy behind the 3D reprojection.
    for (int stepIndex = 1; stepIndex <= 48; ++stepIndex) {
        float stepPixels = float(stepIndex * 4);
        vec2 xOffset = vec2(uSourceTexelSize.x * stepPixels, 0.0);
        vec2 yOffset = vec2(0.0, uSourceTexelSize.y * stepPixels);
        vec2 candidate = clamp(boundedUv - xOffset, vec2(0.001), vec2(0.999));
        if (!hasHeldItemDepth(candidate)) return candidate;
        candidate = clamp(boundedUv + xOffset, vec2(0.001), vec2(0.999));
        if (!hasHeldItemDepth(candidate)) return candidate;
        candidate = clamp(boundedUv - yOffset, vec2(0.001), vec2(0.999));
        if (!hasHeldItemDepth(candidate)) return candidate;
        candidate = clamp(boundedUv + yOffset, vec2(0.001), vec2(0.999));
        if (!hasHeldItemDepth(candidate)) return candidate;
    }
    return boundedUv;
}

vec4 sourceColour(vec2 uv) {
    // Item depth is expressed in the undistorted geometry coordinates. Remove
    // the source item there first, then apply the same final colour-field
    // mapping used by the rest of the AFW image.
    vec2 cleanGeometryUv = backgroundSourceUv(uv);
    return texture(uSourceColor, vrEffectSourceUv(cleanGeometryUv));
}

float colourLuma(vec3 colour) {
    return dot(colour, vec3(0.2126, 0.7152, 0.0722));
}

vec4 qualitySourceColour(vec2 uv) {
    vec4 center = sourceColour(uv);
    if (uSharpenEnabled == 0 && uEdgeAaEnabled == 0) return center;

    vec3 colour = center.rgb;
    vec3 neighbourAverage = center.rgb;
    vec3 localMin = center.rgb;
    vec3 localMax = center.rgb;

    if (uEdgeAaEnabled != 0) {
        vec3 northWest =
            sourceColour(uv + uSourceTexelSize * vec2(-1.0, -1.0)).rgb;
        vec3 northEast =
            sourceColour(uv + uSourceTexelSize * vec2(1.0, -1.0)).rgb;
        vec3 southWest =
            sourceColour(uv + uSourceTexelSize * vec2(-1.0, 1.0)).rgb;
        vec3 southEast =
            sourceColour(uv + uSourceTexelSize * vec2(1.0, 1.0)).rgb;
        neighbourAverage =
            (northWest + northEast + southWest + southEast) * 0.25;
        localMin = min(center.rgb, min(min(northWest, northEast),
                                       min(southWest, southEast)));
        localMax = max(center.rgb, max(max(northWest, northEast),
                                       max(southWest, southEast)));

        float lumaNorthWest = colourLuma(northWest);
        float lumaNorthEast = colourLuma(northEast);
        float lumaSouthWest = colourLuma(southWest);
        float lumaSouthEast = colourLuma(southEast);
        float lumaCenter = colourLuma(center.rgb);
        float lumaMin = min(lumaCenter, min(min(lumaNorthWest, lumaNorthEast),
                                            min(lumaSouthWest, lumaSouthEast)));
        float lumaMax = max(lumaCenter, max(max(lumaNorthWest, lumaNorthEast),
                                            max(lumaSouthWest, lumaSouthEast)));
        float lumaRange = lumaMax - lumaMin;
        float edgeThreshold = max(0.025, lumaMax * 0.10);
        if (lumaRange >= edgeThreshold) {
            vec2 direction;
            direction.x = -((lumaNorthWest + lumaNorthEast) -
                            (lumaSouthWest + lumaSouthEast));
            direction.y = ((lumaNorthWest + lumaSouthWest) -
                           (lumaNorthEast + lumaSouthEast));
            float directionReduce = max(
                (lumaNorthWest + lumaNorthEast +
                 lumaSouthWest + lumaSouthEast) * 0.0078125,
                0.0009765625);
            float inverseDirection =
                1.0 / (min(abs(direction.x), abs(direction.y)) +
                       directionReduce);
            direction = clamp(direction * inverseDirection,
                              vec2(-8.0), vec2(8.0)) * uSourceTexelSize;

            vec3 colourA = 0.5 * (
                sourceColour(uv + direction * (1.0 / 3.0 - 0.5)).rgb +
                sourceColour(uv + direction * (2.0 / 3.0 - 0.5)).rgb);
            vec3 colourB = colourA * 0.5 + 0.25 * (
                sourceColour(uv + direction * -0.5).rgb +
                sourceColour(uv + direction * 0.5).rgb);
            float lumaB = colourLuma(colourB);
            vec3 filteredColour = (lumaB < lumaMin || lumaB > lumaMax)
                ? colourA
                : colourB;
            float edgeStrength = clamp(
                (lumaRange - edgeThreshold) /
                    max(edgeThreshold * 2.0, 0.0001),
                0.0, 1.0);
            // Keep fine distant texture contrast while still smoothing strong
            // silhouettes. Full FXAA replacement softens foliage and blocks.
            colour = mix(center.rgb, filteredColour, edgeStrength * 0.60);
        }
    } else {
        vec3 left = sourceColour(
            uv - vec2(uSourceTexelSize.x, 0.0)).rgb;
        vec3 right = sourceColour(
            uv + vec2(uSourceTexelSize.x, 0.0)).rgb;
        vec3 up = sourceColour(
            uv - vec2(0.0, uSourceTexelSize.y)).rgb;
        vec3 down = sourceColour(
            uv + vec2(0.0, uSourceTexelSize.y)).rgb;
        neighbourAverage = (left + right + up + down) * 0.25;
        localMin = min(center.rgb, min(min(left, right), min(up, down)));
        localMax = max(center.rgb, max(max(left, right), max(up, down)));
    }

    if (uSharpenEnabled != 0) {
        vec3 detail = center.rgb - neighbourAverage;
        colour += detail * clamp(uSharpenStrength, 0.0, 1.0);
        colour = clamp(colour, localMin - vec3(0.015), localMax + vec3(0.015));
    }
    return vec4(clamp(colour, vec3(0.0), vec3(1.0)), center.a);
}

void main() {
    if (uCompositeHeldItem != 0) {
        vec2 sourceUv = vUv - uHeldItemUvShift;
        bool validItem = false;
        for (int iteration = 0; iteration < 4; ++iteration) {
            if (sourceUv.x <= 0.0 || sourceUv.x >= 1.0 ||
                sourceUv.y <= 0.0 || sourceUv.y >= 1.0 ||
                distance(sourceUv, uHeldItemSourceCenter) > uHeldItemRadius) {
                discard;
            }
            float iterationDepth = texture(uHeldItemDepth, sourceUv).r;
            if (iterationDepth >= 0.999999) discard;
            vec4 iterationView = uInverseSourceProjection *
                vec4(sourceUv * 2.0 - 1.0, iterationDepth * 2.0 - 1.0, 1.0);
            if (abs(iterationView.w) <= 0.00001) discard;
            iterationView /= iterationView.w;
            vec4 iterationTargetView = uSourceToTargetView * iterationView;
            vec4 iterationTargetClip = uTargetProjection * iterationTargetView;
            if (abs(iterationTargetClip.w) <= 0.00001) discard;
            vec2 projectedUv = iterationTargetClip.xy / iterationTargetClip.w * 0.5 + 0.5;
            sourceUv += vUv - projectedUv;
            validItem = true;
        }
        if (!validItem || sourceUv.x <= 0.0 || sourceUv.x >= 1.0 ||
            sourceUv.y <= 0.0 || sourceUv.y >= 1.0 ||
            distance(sourceUv, uHeldItemSourceCenter) > uHeldItemRadius) {
            discard;
        }
        float hardwareDepth = texture(uHeldItemDepth, sourceUv).r;
        if (hardwareDepth >= 0.999999) discard;
        vec4 itemView = uInverseSourceProjection *
            vec4(sourceUv * 2.0 - 1.0, hardwareDepth * 2.0 - 1.0, 1.0);
        if (abs(itemView.w) <= 0.00001) discard;
        itemView /= itemView.w;
        vec4 targetItemView = uSourceToTargetView * itemView;
        float itemDepth = max(-targetItemView.z, 0.0);
        float sceneDepth = texture(uEyeSceneDepth, vUv).r * uDepthFarClip;
        if (itemDepth <= 0.001 || itemDepth > sceneDepth + uHeldItemDepthBias) {
            discard;
        }
        vec4 heldItemColour = texture(uHeldItemColor, sourceUv);
        heldItemColour.rgb = linearToSrgb(heldItemColour.rgb);
        heldItemColour.a = 1.0;
        fragColor = heldItemColour;
        return;
    }
    if (uEnableWarp == 0) {
        fragColor = uOutputDepth != 0
            ? vec4(combinedLinearDepth(vUv) / uDepthFarClip, 0.0, 0.0, 1.0)
            : qualitySourceColour(vUv);
        return;
    }
    if (uEnableWarp == 1) {
        vec2 sourceUv = projectionCorrectedSourceUv(vUv);
        if (sourceUv.x <= 0.001 || sourceUv.x >= 0.999 ||
            sourceUv.y <= 0.001 || sourceUv.y >= 0.999) {
            sourceUv = vUv;
        }
        fragColor = uOutputDepth != 0
            ? vec4(combinedLinearDepth(sourceUv) / uDepthFarClip, 0.0, 0.0, 1.0)
            : qualitySourceColour(sourceUv);
        return;
    }
    vec2 projectionSourceUv = projectionCorrectedSourceUv(vUv);
    vec2 sourceUv = projectionSourceUv;
    bool validDepth = false;
    for (int iteration = 0; iteration < 3; ++iteration) {
        vec2 boundedUv = clamp(sourceUv, vec2(0.001), vec2(0.999));
        float linearDepth = 1.0;
        if (uEnableWarp == 2) {
            linearDepth = combinedLinearDepth(boundedUv);
            if (linearDepth <= 0.001 || linearDepth >= uDepthFarClip * 0.999) break;
        }
        validDepth = true;
        vec4 sourceView = vec4(sourceViewPosition(boundedUv, linearDepth), 1.0);
        vec4 targetView = uEnableWarp == 2
            ? uSourceToTargetView * sourceView
            : sourceView;
        vec4 targetClip = uTargetProjection * targetView;
        if (abs(targetClip.w) <= 0.00001) break;
        vec2 projectedUv = targetClip.xy / targetClip.w * 0.5 + 0.5;
        sourceUv += projectionSourceUv - projectionCorrectedSourceUv(projectedUv);
    }
    if (!validDepth) sourceUv = projectionSourceUv;
    vec2 displacement = sourceUv - projectionSourceUv;
    if (abs(displacement.x) > 0.15 || abs(displacement.y) > 0.08) {
        sourceUv = projectionSourceUv;
    }
    vec2 sampledUv = mirroredSourceUv(sourceUv);
    if (uOutputDepth != 0) {
        float sourceLinearDepth = combinedLinearDepth(sampledUv);
        vec4 targetDepthPosition = uSourceToTargetView *
            vec4(sourceViewPosition(sampledUv, sourceLinearDepth), 1.0);
        float targetLinearDepth = max(-targetDepthPosition.z, 0.0);
        fragColor = vec4(clamp(targetLinearDepth / uDepthFarClip, 0.0, 1.0),
                         0.0, 0.0, 1.0);
    } else {
        fragColor = qualitySourceColour(sampledUv);
    }
}
)GLSL";

    const GLuint vertex = compile_gl_shader(GL_VERTEX_SHADER_VALUE, kVertexSource,
                                            "AFW vertex");
    const GLuint fragment = compile_gl_shader(GL_FRAGMENT_SHADER_VALUE, kFragmentSource,
                                              "AFW fragment");
    if (!vertex || !fragment) return false;

    g_afw_program = g_gl_create_program();
    g_gl_attach_shader(g_afw_program, vertex);
    g_gl_attach_shader(g_afw_program, fragment);
    g_gl_bind_attrib_location(g_afw_program, 0, "aPos");
    g_gl_bind_attrib_location(g_afw_program, 1, "aUv");
    g_gl_link_program(g_afw_program);
    g_gl_delete_shader(vertex);
    g_gl_delete_shader(fragment);
    GLint linked = 0;
    g_gl_get_program_iv(g_afw_program, GL_LINK_STATUS_VALUE, &linked);
    if (!linked) {
        GLint length = 0;
        g_gl_get_program_iv(g_afw_program, GL_INFO_LOG_LENGTH_VALUE, &length);
        std::string log(static_cast<size_t>(std::max(1, length)), '\0');
        if (g_gl_get_program_info_log && length > 1) {
            g_gl_get_program_info_log(g_afw_program, length, nullptr, log.data());
        }
        render_log(std::string("AFW: program link failed ") + log.c_str());
        g_afw_program = 0;
        return false;
    }

    g_afw_source_color_uniform =
        g_gl_get_uniform_location(g_afw_program, "uSourceColor");
    g_afw_source_depth_uniform =
        g_gl_get_uniform_location(g_afw_program, "uSourceDepth");
    g_afw_held_item_color_uniform =
        g_gl_get_uniform_location(g_afw_program, "uHeldItemColor");
    g_afw_held_item_depth_uniform =
        g_gl_get_uniform_location(g_afw_program, "uHeldItemDepth");
    g_afw_use_held_item_depth_uniform =
        g_gl_get_uniform_location(g_afw_program, "uUseHeldItemDepth");
    g_afw_remove_held_item_background_uniform =
        g_gl_get_uniform_location(g_afw_program, "uRemoveHeldItemFromBackground");
    g_afw_source_texel_size_uniform =
        g_gl_get_uniform_location(g_afw_program, "uSourceTexelSize");
    g_afw_eye_scene_depth_uniform =
        g_gl_get_uniform_location(g_afw_program, "uEyeSceneDepth");
    g_afw_composite_held_item_uniform =
        g_gl_get_uniform_location(g_afw_program, "uCompositeHeldItem");
    g_afw_held_item_uv_shift_uniform =
        g_gl_get_uniform_location(g_afw_program, "uHeldItemUvShift");
    g_afw_held_item_source_center_uniform =
        g_gl_get_uniform_location(g_afw_program, "uHeldItemSourceCenter");
    g_afw_held_item_radius_uniform =
        g_gl_get_uniform_location(g_afw_program, "uHeldItemRadius");
    g_afw_held_item_depth_bias_uniform =
        g_gl_get_uniform_location(g_afw_program, "uHeldItemDepthBias");
    g_afw_distortion_field_uniform =
        g_gl_get_uniform_location(g_afw_program, "uDistortionField");
    g_afw_inverse_source_projection_uniform =
        g_gl_get_uniform_location(g_afw_program, "uInverseSourceProjection");
    g_afw_source_projection_uniform =
        g_gl_get_uniform_location(g_afw_program, "uSourceProjection");
    g_afw_inverse_target_projection_uniform =
        g_gl_get_uniform_location(g_afw_program, "uInverseTargetProjection");
    g_afw_source_to_target_view_uniform =
        g_gl_get_uniform_location(g_afw_program, "uSourceToTargetView");
    g_afw_target_projection_uniform =
        g_gl_get_uniform_location(g_afw_program, "uTargetProjection");
    g_afw_depth_far_clip_uniform =
        g_gl_get_uniform_location(g_afw_program, "uDepthFarClip");
    g_afw_enable_warp_uniform =
        g_gl_get_uniform_location(g_afw_program, "uEnableWarp");
    g_afw_output_depth_uniform =
        g_gl_get_uniform_location(g_afw_program, "uOutputDepth");
    g_afw_apply_distortion_uniform =
        g_gl_get_uniform_location(g_afw_program, "uApplyDistortion");
    g_afw_sharpen_enabled_uniform =
        g_gl_get_uniform_location(g_afw_program, "uSharpenEnabled");
    g_afw_sharpen_strength_uniform =
        g_gl_get_uniform_location(g_afw_program, "uSharpenStrength");
    g_afw_edge_aa_enabled_uniform =
        g_gl_get_uniform_location(g_afw_program, "uEdgeAaEnabled");

    constexpr float vertices[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         3.0f, -1.0f, 2.0f, 0.0f,
        -1.0f,  3.0f, 0.0f, 2.0f,
    };
    g_gl_gen_vertex_arrays(1, &g_afw_vao);
    g_gl_gen_buffers(1, &g_afw_vbo);
    g_gl_bind_vertex_array(g_afw_vao);
    g_gl_bind_buffer(GL_ARRAY_BUFFER_VALUE, g_afw_vbo);
    g_gl_buffer_data(GL_ARRAY_BUFFER_VALUE, sizeof(vertices), vertices, GL_DYNAMIC_DRAW_VALUE);
    g_gl_enable_vertex_attrib_array(0);
    g_gl_vertex_attrib_pointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    g_gl_enable_vertex_attrib_array(1);
    g_gl_vertex_attrib_pointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                               reinterpret_cast<void*>(2 * sizeof(float)));
    g_gl_bind_buffer(GL_ARRAY_BUFFER_VALUE, 0);
    g_gl_bind_vertex_array(0);

    g_afw_gl_ready = g_afw_program != 0 && g_afw_vao != 0 && g_afw_vbo != 0;
    if (g_afw_gl_ready) render_log("AFW: OpenGL depth reprojection ready");
    return g_afw_gl_ready;
}

bool ensure_hand_gl_renderer() {
    if (g_hand_gl_attempted) return g_hand_gl_ready;
    g_hand_gl_attempted = true;
    if (!load_hand_texture() || !load_hand_gl_functions()) {
        render_log("hand renderer: required OpenGL functions or texture are unavailable");
        return false;
    }

    constexpr const char* kVertexSource = R"GLSL(
#version 130
in vec2 aPos;
in vec2 aUv;
in float aShade;
in float aInverseDepth;
out vec2 vUvOverDepth;
out float vShade;
out float vInverseDepth;
void main() {
    vUvOverDepth = vec2(aUv.x, 1.0 - aUv.y) * aInverseDepth;
    vShade = aShade;
    vInverseDepth = aInverseDepth;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";
    const GLuint vertex = compile_gl_shader(GL_VERTEX_SHADER_VALUE, kVertexSource,
                                            "hand vertex");
    const GLuint fragment =
        compile_gl_shader(GL_FRAGMENT_SHADER_VALUE,
                          hytalevr::kHandDepthFragmentShader, "hand fragment");
    if (!vertex || !fragment) return false;

    g_hand_program = g_gl_create_program();
    g_gl_attach_shader(g_hand_program, vertex);
    g_gl_attach_shader(g_hand_program, fragment);
    g_gl_bind_attrib_location(g_hand_program, 0, "aPos");
    g_gl_bind_attrib_location(g_hand_program, 1, "aUv");
    g_gl_bind_attrib_location(g_hand_program, 2, "aShade");
    g_gl_bind_attrib_location(g_hand_program, 3, "aInverseDepth");
    g_gl_link_program(g_hand_program);
    g_gl_delete_shader(vertex);
    g_gl_delete_shader(fragment);
    GLint linked = 0;
    g_gl_get_program_iv(g_hand_program, GL_LINK_STATUS_VALUE, &linked);
    if (!linked) {
        GLint length = 0;
        if (g_gl_get_program_iv) g_gl_get_program_iv(g_hand_program, GL_INFO_LOG_LENGTH_VALUE, &length);
        std::string log(static_cast<size_t>(std::max(1, length)), '\0');
        if (g_gl_get_program_info_log && length > 1) {
            g_gl_get_program_info_log(g_hand_program, length, nullptr, log.data());
        }
        std::ostringstream out;
        out << "hand renderer: program link failed " << log.c_str();
        render_log(out.str());
        g_hand_program = 0;
        return false;
    }
    g_hand_texture_uniform = g_gl_get_uniform_location(g_hand_program, "uTexture");
    g_hand_scene_depth_uniform = g_gl_get_uniform_location(g_hand_program, "uSceneDepth");
    g_hand_use_scene_depth_uniform = g_gl_get_uniform_location(g_hand_program, "uUseSceneDepth");
    g_hand_viewport_size_uniform = g_gl_get_uniform_location(g_hand_program, "uViewportSize");
    g_hand_depth_far_clip_uniform = g_gl_get_uniform_location(g_hand_program, "uDepthFarClip");
    g_hand_depth_bias_uniform = g_gl_get_uniform_location(g_hand_program, "uDepthBias");

    GLint old_texture = 0;
    GLint old_unpack = 4;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT_VALUE, &old_unpack);
    glGenTextures(1, &g_hand_texture_gl);
    glBindTexture(GL_TEXTURE_2D, g_hand_texture_gl);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE_VALUE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE_VALUE);

    std::vector<uint8_t> rgba;
    rgba.reserve(static_cast<size_t>(g_hand_texture_width) *
                 static_cast<size_t>(g_hand_texture_height) * 4);
    for (const uint32_t pixel : g_hand_texture_pixels) {
        rgba.push_back(static_cast<uint8_t>((pixel >> 16) & 0xff));
        rgba.push_back(static_cast<uint8_t>((pixel >> 8) & 0xff));
        rgba.push_back(static_cast<uint8_t>(pixel & 0xff));
        rgba.push_back(0xff);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT_VALUE, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(g_hand_texture_width),
                 static_cast<GLsizei>(g_hand_texture_height), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 rgba.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT_VALUE, old_unpack);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(old_texture));

    g_gl_gen_vertex_arrays(1, &g_hand_vao);
    g_gl_gen_buffers(1, &g_hand_vbo);
    g_hand_gl_ready = g_hand_program != 0 && g_hand_texture_gl != 0 &&
        g_hand_vao != 0 && g_hand_vbo != 0;
    if (g_hand_gl_ready) render_log("hand renderer: GL textured path ready");
    return g_hand_gl_ready;
}

bool load_hand_model() {
    if (g_hand_model_attempted) return g_hand_model_loaded;
    g_hand_model_attempted = true;

    const std::wstring path = module_directory() + L"assets\\vr_hands\\left_hand.obj";
    std::ifstream file(path);
    if (!file.is_open()) {
        render_log("hand model: assets/vr_hands/left_hand.obj not found");
        return false;
    }

    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<std::array<float, 2>> texcoords;
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream stream(line);
        std::string tag;
        stream >> tag;
        if (tag == "v") {
            Vec3 position{};
            stream >> position.x >> position.y >> position.z;
            positions.push_back(position);
        } else if (tag == "vn") {
            Vec3 normal{};
            stream >> normal.x >> normal.y >> normal.z;
            normals.push_back(normal);
        } else if (tag == "vt") {
            std::array<float, 2> uv{};
            stream >> uv[0] >> uv[1];
            texcoords.push_back(uv);
        } else if (tag == "f") {
            struct Ref {
                int position = 0;
                int texcoord = 0;
                int normal = 0;
            };
            std::vector<Ref> refs;
            std::string token;
            while (stream >> token) {
                Ref ref{};
                const size_t first = token.find('/');
                const size_t second =
                    first == std::string::npos ? std::string::npos : token.find('/', first + 1);
                ref.position = std::atoi(token.substr(0, first).c_str());
                if (first != std::string::npos && second != std::string::npos &&
                    second > first + 1) {
                    ref.texcoord = std::atoi(token.substr(first + 1, second - first - 1).c_str());
                } else if (first != std::string::npos && second == std::string::npos &&
                           first + 1 < token.size()) {
                    ref.texcoord = std::atoi(token.substr(first + 1).c_str());
                }
                if (second != std::string::npos && second + 1 < token.size()) {
                    ref.normal = std::atoi(token.substr(second + 1).c_str());
                }
                refs.push_back(ref);
            }
            if (refs.size() < 3) continue;

            const auto emit = [&](const Ref& ref) {
                const int position_index = ref.position > 0
                    ? ref.position - 1
                    : static_cast<int>(positions.size()) + ref.position;
                const int normal_index = ref.normal > 0
                    ? ref.normal - 1
                    : static_cast<int>(normals.size()) + ref.normal;
                const int texcoord_index = ref.texcoord > 0
                    ? ref.texcoord - 1
                    : static_cast<int>(texcoords.size()) + ref.texcoord;
                if (position_index < 0 ||
                    position_index >= static_cast<int>(positions.size())) {
                    return;
                }
                HandVertex vertex{};
                vertex.position = positions[position_index];
                vertex.normal = {0.0f, 1.0f, 0.0f};
                if (normal_index >= 0 && normal_index < static_cast<int>(normals.size())) {
                    vertex.normal = normals[normal_index];
                }
                if (texcoord_index >= 0 && texcoord_index < static_cast<int>(texcoords.size())) {
                    vertex.u = texcoords[texcoord_index][0];
                    vertex.v = texcoords[texcoord_index][1];
                }
                g_hand_vertices.push_back(vertex);
            };

            for (size_t i = 1; i + 1 < refs.size(); ++i) {
                emit(refs[0]);
                emit(refs[i]);
                emit(refs[i + 1]);
            }
        }
    }

    load_hand_texture();
    g_hand_model_loaded = !g_hand_vertices.empty();
    if (g_hand_model_loaded) {
        std::ostringstream out;
        out << "hand model: loaded vertices=" << g_hand_vertices.size();
        render_log(out.str());
    } else {
        render_log("hand model: OBJ loaded but no triangles found");
    }
    return g_hand_model_loaded;
}

float validated_world_scale(const VrCameraControls& controls);

Vec3 rotate_hand_local(Vec3 value, const VrCameraControls& controls) {
    constexpr float kPi = 3.14159265358979323846f;
    const float pitch = controls.hand_model_pitch_degrees * kPi / 180.0f;
    const float yaw = controls.hand_model_yaw_degrees * kPi / 180.0f;
    const float roll = controls.hand_model_roll_degrees * kPi / 180.0f;

    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    value = {value.x, value.y * cp - value.z * sp, value.y * sp + value.z * cp};

    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    value = {value.x * cy + value.z * sy, value.y, -value.x * sy + value.z * cy};

    const float cr = std::cos(roll);
    const float sr = std::sin(roll);
    value = {value.x * cr - value.y * sr, value.x * sr + value.y * cr, value.z};
    return value;
}

Vec3 transform_hand_vertex(const HandVertex& vertex, const float controller[12],
                           bool right_hand, const VrCameraControls& controls) {
    const float world_scale = validated_world_scale(controls);
    const float scale = 0.135f * std::clamp(controls.hand_model_scale, 0.10f, 5.0f) *
                        world_scale;
    constexpr float offset_x = 0.0f;
    const float offset_y = -0.035f * world_scale;
    const float offset_z = -0.055f * world_scale;

    Vec3 local = vertex.position;
    local.x -= 0.32f;
    local.y -= 0.82f;
    local.z += 0.04f;
    local = rotate_hand_local(local, controls);
    local.x *= right_hand ? -scale : scale;
    local.y *= scale;
    local.z *= scale;

    const float x = local.x + offset_x;
    const float y = local.y + offset_y;
    const float z = local.z + offset_z;

    Vec3 world{};
    for (int row = 0; row < 3; ++row) {
        const float controller_right = controller[row * 4 + 0];
        const float controller_up = controller[row * 4 + 1];
        const float controller_forward = -controller[row * 4 + 2];
        const float origin = controller[row * 4 + 3];
        (&world.x)[row] = origin +
            controller_right * x +
            controller_up * y +
            controller_forward * z;
    }
    return world;
}

bool project_tracking_point_to_eye_ndc_raw(int eye, const float hmd[12],
                                           const Vec3& absolute,
                                           float& ndc_x, float& ndc_y,
                                           float& depth) {
    const float point[3]{absolute.x, absolute.y, absolute.z};
    return project_tracking_point_to_eye_ndc(eye, hmd, point, ndc_x, ndc_y, depth);
}

Vec3 transform_hand_normal(const Vec3& normal, const float controller[12],
                           bool right_hand, const VrCameraControls& controls) {
    Vec3 local = rotate_hand_local(normal, controls);
    const float x = local.x * (right_hand ? -1.0f : 1.0f);
    const float y = local.y;
    const float z = local.z;
    Vec3 world{};
    for (int row = 0; row < 3; ++row) {
        const float controller_right = controller[row * 4 + 0];
        const float controller_up = controller[row * 4 + 1];
        const float controller_forward = -controller[row * 4 + 2];
        (&world.x)[row] =
            controller_right * x +
            controller_up * y +
            controller_forward * z;
    }
    const float length = std::sqrt(world.x * world.x + world.y * world.y + world.z * world.z);
    if (length > 0.0001f) {
        world.x /= length;
        world.y /= length;
        world.z /= length;
    }
    return world;
}

struct ProjectedHandVertex {
    float x = 0.0f;
    float y = 0.0f;
    float depth = 0.0f;
    float shade = 1.0f;
    float u = 0.5f;
    float v = 0.5f;
};

struct ProjectedHandTriangle {
    ProjectedHandVertex v[3]{};
    float depth = 0.0f;
    float shade = 1.0f;
    float red = 0.84f;
    float green = 0.84f;
    float blue = 0.80f;
};

struct HandDrawVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.5f;
    float v = 0.5f;
    float shade = 1.0f;
    float depth = 0.0f;
};

struct SceneDepthTextureSnapshot {
    GLuint texture = 0;
    int width = 0;
    int height = 0;
    uint32_t frame = 0;
    float far_clip = 1024.0f;
    bool valid = false;
};

struct DistortionTextureSnapshot {
    GLuint texture = 0;
    int width = 0;
    int height = 0;
    uint32_t frame = 0;
    bool valid = false;
};

struct HeldItemDepthTextureSnapshot {
    GLuint color_texture = 0;
    GLuint depth_texture = 0;
    int width = 0;
    int height = 0;
    uint32_t frame = 0;
    LONG visible_mask = 0;
    float view_positions[2][3]{};
    bool valid = false;
};

struct HeldItemAtlasSnapshot {
    HeldItemAtlasKey keys[2]{};
    LONG frame = -1;
    LONG valid_mask = 0;
    bool valid = false;
};

SceneDepthTextureSnapshot scene_depth_texture_snapshot() {
    SceneDepthTextureSnapshot snapshot{};
    if (!ensure_ui_scale_mapping() || !g_ui_scale_shared) return snapshot;

    const LONG texture = InterlockedCompareExchange(
        &g_ui_scale_shared->sceneDepthTextureId, 0, 0);
    const LONG width = InterlockedCompareExchange(
        &g_ui_scale_shared->sceneDepthTextureWidth, 0, 0);
    const LONG height = InterlockedCompareExchange(
        &g_ui_scale_shared->sceneDepthTextureHeight, 0, 0);
    const LONG frame = InterlockedCompareExchange(
        &g_ui_scale_shared->sceneDepthTextureFrame, 0, 0);
    if (texture <= 0 || width < 512 || height < 256) return snapshot;

    snapshot.texture = static_cast<GLuint>(texture);
    snapshot.width = static_cast<int>(width);
    snapshot.height = static_cast<int>(height);
    snapshot.frame = static_cast<uint32_t>((std::max)(0L, frame));
    snapshot.far_clip = g_ui_scale_shared->sceneDepthFarClip > 1.0f
        ? g_ui_scale_shared->sceneDepthFarClip
        : 1024.0f;
    snapshot.valid = true;
    return snapshot;
}

HeldItemDepthTextureSnapshot held_item_depth_texture_snapshot() {
    HeldItemDepthTextureSnapshot snapshot{};
    if (!ensure_ui_scale_mapping() || !g_ui_scale_shared) return snapshot;

    const LONG texture = InterlockedCompareExchange(
        &g_ui_scale_shared->heldItemDepthTextureId, 0, 0);
    const LONG color_texture = InterlockedCompareExchange(
        &g_ui_scale_shared->heldItemLayerTextureId, 0, 0);
    const LONG width = InterlockedCompareExchange(
        &g_ui_scale_shared->heldItemDepthTextureWidth, 0, 0);
    const LONG height = InterlockedCompareExchange(
        &g_ui_scale_shared->heldItemDepthTextureHeight, 0, 0);
    const LONG frame = InterlockedCompareExchange(
        &g_ui_scale_shared->heldItemDepthTextureFrame, 0, 0);
    const LONG current_frame = InterlockedCompareExchange(
        &g_ui_scale_shared->renderFrameSequence, 0, 0);
    const LONG visible_mask = InterlockedCompareExchange(
        &g_ui_scale_shared->heldItemLayerVisibleMask, 0, 0);
    std::memcpy(snapshot.view_positions,
                g_ui_scale_shared->heldItemLayerViewPositions,
                sizeof(snapshot.view_positions));
    MemoryBarrier();
    if (texture <= 0 || color_texture <= 0 || width <= 0 || height <= 0 ||
        visible_mask == 0 || frame < 0 ||
        current_frame != frame + 1) {
        return snapshot;
    }

    snapshot.color_texture = static_cast<GLuint>(color_texture);
    snapshot.depth_texture = static_cast<GLuint>(texture);
    snapshot.width = static_cast<int>(width);
    snapshot.height = static_cast<int>(height);
    snapshot.frame = static_cast<uint32_t>(frame);
    snapshot.visible_mask = visible_mask;
    snapshot.valid = true;
    return snapshot;
}

HeldItemAtlasSnapshot held_item_atlas_snapshot() {
    HeldItemAtlasSnapshot snapshot{};
    if (!ensure_ui_scale_mapping() || !g_ui_scale_shared) return snapshot;

    for (int attempt = 0; attempt < 8; ++attempt) {
        const LONG before = InterlockedCompareExchange(
            &g_ui_scale_shared->heldItemAtlasSequence, 0, 0);
        if ((before & 1) != 0) {
            YieldProcessor();
            continue;
        }
        MemoryBarrier();
        snapshot.frame = InterlockedCompareExchange(
            &g_ui_scale_shared->heldItemAtlasFrame, 0, 0);
        snapshot.valid_mask = InterlockedCompareExchange(
            &g_ui_scale_shared->heldItemAtlasValidMask, 0, 0);
        for (int side = 0; side < 2; ++side) {
            HeldItemAtlasKey& key = snapshot.keys[side];
            key.texture = static_cast<GLuint>(InterlockedCompareExchange(
                &g_ui_scale_shared->heldItemAtlasTextureIds[side], 0, 0));
            key.offset_x = InterlockedCompareExchange(
                &g_ui_scale_shared->heldItemAtlasOffsets[side][0], 0, 0);
            key.offset_y = InterlockedCompareExchange(
                &g_ui_scale_shared->heldItemAtlasOffsets[side][1], 0, 0);
            key.atlas_width = InterlockedCompareExchange(
                &g_ui_scale_shared->heldItemAtlasSizes[side][0], 0, 0);
            key.atlas_height = InterlockedCompareExchange(
                &g_ui_scale_shared->heldItemAtlasSizes[side][1], 0, 0);
            key.vertex_array = static_cast<GLuint>(InterlockedCompareExchange(
                &g_ui_scale_shared->heldItemVertexArrayIds[side], 0, 0));
            key.index_count = InterlockedCompareExchange(
                &g_ui_scale_shared->heldItemIndexCounts[side], 0, 0);
            key.uv_min_x = InterlockedCompareExchange(
                &g_ui_scale_shared->heldItemUvBounds[side][0], 0, 0);
            key.uv_min_y = InterlockedCompareExchange(
                &g_ui_scale_shared->heldItemUvBounds[side][1], 0, 0);
            key.uv_max_x = InterlockedCompareExchange(
                &g_ui_scale_shared->heldItemUvBounds[side][2], 0, 0);
            key.uv_max_y = InterlockedCompareExchange(
                &g_ui_scale_shared->heldItemUvBounds[side][3], 0, 0);
        }
        MemoryBarrier();
        const LONG after = InterlockedCompareExchange(
            &g_ui_scale_shared->heldItemAtlasSequence, 0, 0);
        if (before != after || (after & 1) != 0) continue;

        const LONG current_frame = InterlockedCompareExchange(
            &g_ui_scale_shared->renderFrameSequence, 0, 0);
        if (snapshot.frame < 0 || current_frame != snapshot.frame + 1 ||
            snapshot.valid_mask == 0) {
            if (snapshot.frame >= 0) {
                item_diagnostic_log_once(
                    4u, "atlas snapshot is stale frame=" +
                            std::to_string(snapshot.frame) +
                            " current=" + std::to_string(current_frame) +
                            " mask=" + std::to_string(snapshot.valid_mask));
            }
            return HeldItemAtlasSnapshot{};
        }
        snapshot.valid = true;
        item_diagnostic_log_once(
            8u, "atlas snapshot received frame=" +
                    std::to_string(snapshot.frame) +
                    " mask=" + std::to_string(snapshot.valid_mask));
        return snapshot;
    }
    return HeldItemAtlasSnapshot{};
}

LONG held_item_presence_mask() {
    if (!ensure_ui_scale_mapping() || !g_ui_scale_shared) return 0;
    for (int attempt = 0; attempt < 4; ++attempt) {
        const LONG frame_before = InterlockedCompareExchange(
            &g_ui_scale_shared->heldItemMatrixFrame, 0, 0);
        MemoryBarrier();
        const LONG mask = InterlockedCompareExchange(
            &g_ui_scale_shared->heldItemLayerVisibleMask, 0, 0);
        MemoryBarrier();
        const LONG frame_after = InterlockedCompareExchange(
            &g_ui_scale_shared->heldItemMatrixFrame, 0, 0);
        const LONG current_frame = InterlockedCompareExchange(
            &g_ui_scale_shared->renderFrameSequence, 0, 0);
        if (frame_before == frame_after &&
            frame_after >= 0 && current_frame == frame_after + 1) {
            return mask;
        }
    }
    return 0;
}

void fill_projected_triangle(const ProjectedHandTriangle& triangle,
                             int width, int height) {
    const float min_y_float = std::min({triangle.v[0].y, triangle.v[1].y, triangle.v[2].y});
    const float max_y_float = std::max({triangle.v[0].y, triangle.v[1].y, triangle.v[2].y});
    int min_y = std::clamp(static_cast<int>(std::floor(min_y_float)), 0, height - 1);
    int max_y = std::clamp(static_cast<int>(std::ceil(max_y_float)), 0, height - 1);
    if (max_y <= min_y) return;

    glClearColor(triangle.red, triangle.green, triangle.blue, 1.0f);
    const int row_step = 1;
    const auto edge_crosses = [](float y, const ProjectedHandVertex& a,
                                 const ProjectedHandVertex& b, float& x) {
        if (std::fabs(a.y - b.y) < 0.001f) return false;
        const float lower = std::min(a.y, b.y);
        const float upper = std::max(a.y, b.y);
        if (y < lower || y >= upper) return false;
        const float t = (y - a.y) / (b.y - a.y);
        x = a.x + (b.x - a.x) * t;
        return std::isfinite(x);
    };

    for (int y = min_y; y <= max_y; y += row_step) {
        const float sample_y = static_cast<float>(y) + 0.5f;
        float intersections[3]{};
        int count = 0;
        for (int edge = 0; edge < 3; ++edge) {
            float x = 0.0f;
            if (edge_crosses(sample_y, triangle.v[edge], triangle.v[(edge + 1) % 3], x) &&
                count < 3) {
                intersections[count++] = x;
            }
        }
        if (count < 2) continue;
        float left = std::min(intersections[0], intersections[1]);
        float right = std::max(intersections[0], intersections[1]);
        if (count == 3) {
            left = std::min(left, intersections[2]);
            right = std::max(right, intersections[2]);
        }

        int x0 = std::clamp(static_cast<int>(std::floor(left)) - 1, 0, width - 1);
        int x1 = std::clamp(static_cast<int>(std::ceil(right)) + 1, 0, width - 1);
        if (x1 <= x0) continue;
        clear_scissor_rect(x0, std::max(0, y - 1), x1 - x0 + 1, row_step + 1,
                           width, height);
    }
}

bool begin_hand_item_depth(int width, int height) {
    if (width <= 0 || height <= 0 || !initialize_gl_capture()) return false;
    GLint draw_framebuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_VALUE, &draw_framebuffer);
    if (draw_framebuffer == 0) return false;

    GLint old_active_texture = GL_TEXTURE0_VALUE;
    GLint old_texture = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE_VALUE, &old_active_texture);
    g_gl_active_texture(GL_TEXTURE0_VALUE);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);
    if (!g_hand_item_depth_texture ||
        g_hand_item_depth_width != width ||
        g_hand_item_depth_height != height) {
        if (g_hand_item_depth_texture) {
            glDeleteTextures(1, &g_hand_item_depth_texture);
            g_hand_item_depth_texture = 0;
        }
        glGenTextures(1, &g_hand_item_depth_texture);
        glBindTexture(GL_TEXTURE_2D, g_hand_item_depth_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE_VALUE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE_VALUE);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24_VALUE,
            width, height, 0, GL_DEPTH_COMPONENT_VALUE,
            GL_UNSIGNED_INT, nullptr);
        g_hand_item_depth_width = width;
        g_hand_item_depth_height = height;
    }
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(old_texture));
    g_gl_active_texture(static_cast<GLenum>(old_active_texture));
    if (!g_hand_item_depth_texture) return false;

    g_gl_framebuffer_texture_2d(
        GL_DRAW_FRAMEBUFFER_VALUE, GL_DEPTH_ATTACHMENT_VALUE,
        GL_TEXTURE_2D, g_hand_item_depth_texture, 0);
    if (g_gl_check_framebuffer_status(GL_DRAW_FRAMEBUFFER_VALUE) !=
        GL_FRAMEBUFFER_COMPLETE_VALUE) {
        g_gl_framebuffer_texture_2d(
            GL_DRAW_FRAMEBUFFER_VALUE, GL_DEPTH_ATTACHMENT_VALUE,
            GL_TEXTURE_2D, 0, 0);
        return false;
    }

    GLboolean old_depth_mask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK_VALUE, &old_depth_mask);
    glDepthMask(GL_TRUE);
    glClearDepth(1.0);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDepthMask(old_depth_mask);
    g_hand_item_depth_active = true;
    return true;
}

void end_hand_item_depth() {
    if (!g_hand_item_depth_active) return;
    g_gl_framebuffer_texture_2d(
        GL_DRAW_FRAMEBUFFER_VALUE, GL_DEPTH_ATTACHMENT_VALUE,
        GL_TEXTURE_2D, 0, 0);
    g_hand_item_depth_active = false;
}

bool draw_projected_textured_triangles_gl(
        const std::vector<ProjectedHandTriangle>& triangles,
        int width, int height, int eye, GLuint texture, float depth_bias,
        bool alpha_blend = false) {
    if (triangles.empty() || width <= 0 || height <= 0 || texture == 0 ||
        !ensure_hand_gl_renderer()) {
        return false;
    }

    static thread_local std::vector<HandDrawVertex> vertices;
    vertices.clear();
    if (vertices.capacity() < triangles.size() * 3) {
        vertices.reserve(triangles.size() * 3);
    }
    for (const ProjectedHandTriangle& triangle : triangles) {
        for (const ProjectedHandVertex& vertex : triangle.v) {
            HandDrawVertex out{};
            out.x = (vertex.x / static_cast<float>(width)) * 2.0f - 1.0f;
            out.y = (vertex.y / static_cast<float>(height)) * 2.0f - 1.0f;
            out.u = vertex.u;
            out.v = vertex.v;
            out.shade = vertex.shade;
            out.depth = 1.0f / (std::max)(vertex.depth, 0.001f);
            vertices.push_back(out);
        }
    }
    if (vertices.empty()) return false;

    GLint old_program = 0;
    GLint old_vertex_array = 0;
    GLint old_array_buffer = 0;
    GLint old_active_texture = GL_TEXTURE0_VALUE;
    GLint old_texture0 = 0;
    GLint old_texture1 = 0;
    GLint old_viewport[4]{};
    GLint old_depth_func = GL_LESS;
    GLint old_blend_src = GL_ONE;
    GLint old_blend_dst = GL_ZERO;
    GLboolean old_color_mask[4]{};
    GLboolean old_depth_mask = GL_TRUE;
    glGetIntegerv(GL_CURRENT_PROGRAM_VALUE, &old_program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING_VALUE, &old_vertex_array);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING_VALUE, &old_array_buffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE_VALUE, &old_active_texture);
    glGetIntegerv(GL_VIEWPORT_VALUE, old_viewport);
    glGetIntegerv(GL_DEPTH_FUNC_VALUE, &old_depth_func);
    glGetIntegerv(GL_BLEND_SRC, &old_blend_src);
    glGetIntegerv(GL_BLEND_DST, &old_blend_dst);
    glGetBooleanv(GL_COLOR_WRITEMASK, old_color_mask);
    glGetBooleanv(GL_DEPTH_WRITEMASK_VALUE, &old_depth_mask);
    g_gl_active_texture(GL_TEXTURE0_VALUE);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture0);
    g_gl_active_texture(GL_TEXTURE1_VALUE);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture1);

    const GLboolean scissor_enabled = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean depth_enabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cull_enabled = glIsEnabled(GL_CULL_FACE);
    const GLboolean blend_enabled = glIsEnabled(GL_BLEND);

    glViewport(0, 0, width, height);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_SCISSOR_TEST);
    if (g_hand_item_depth_active) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(alpha_blend ? GL_FALSE : GL_TRUE);
    } else {
        glDisable(GL_DEPTH_TEST);
        if (alpha_blend) glDepthMask(GL_FALSE);
    }
    glDisable(GL_CULL_FACE);
    if (alpha_blend) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }

    g_gl_use_program(g_hand_program);
    g_gl_uniform_1i(g_hand_texture_uniform, 0);
    g_gl_uniform_1i(g_hand_scene_depth_uniform, 1);
    g_gl_uniform_2f(g_hand_viewport_size_uniform,
                    static_cast<float>(width), static_cast<float>(height));
    const SceneDepthTextureSnapshot scene_depth = scene_depth_texture_snapshot();
    GLuint scene_depth_texture = scene_depth.texture;
    int scene_depth_width = scene_depth.width;
    int scene_depth_height = scene_depth.height;
    bool use_scene_depth = scene_depth.valid && scene_depth_texture != 0;
    if (eye >= 0 && eye <= 1 && g_eye_depth_valid[eye] &&
        g_eye_depth_textures[eye] != 0) {
        scene_depth_texture = g_eye_depth_textures[eye];
        scene_depth_width = g_texture_width;
        scene_depth_height = g_texture_height;
        use_scene_depth = true;
    }
    if (g_shared) {
        g_shared->scene_depth_texture_id = scene_depth_texture;
        g_shared->scene_depth_width = static_cast<uint32_t>((std::max)(0, scene_depth_width));
        g_shared->scene_depth_height = static_cast<uint32_t>((std::max)(0, scene_depth_height));
        g_shared->scene_depth_frame = scene_depth.frame;
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->hand_depth_draws));
        if (use_scene_depth) {
            InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->hand_depth_active_draws));
        }
    }
    g_gl_uniform_1i(g_hand_use_scene_depth_uniform, use_scene_depth ? 1 : 0);
    g_gl_uniform_1f(g_hand_depth_far_clip_uniform, scene_depth.far_clip);
    g_gl_uniform_1f(g_hand_depth_bias_uniform,
                    std::clamp(depth_bias, 0.0f, 1.0f));
    g_gl_active_texture(GL_TEXTURE0_VALUE);
    glBindTexture(GL_TEXTURE_2D, texture);
    if (use_scene_depth) {
        g_gl_active_texture(GL_TEXTURE1_VALUE);
        glBindTexture(GL_TEXTURE_2D, scene_depth_texture);
    }
    g_gl_bind_vertex_array(g_hand_vao);
    g_gl_bind_buffer(GL_ARRAY_BUFFER_VALUE, g_hand_vbo);
    g_gl_buffer_data(GL_ARRAY_BUFFER_VALUE,
                     static_cast<ptrdiff_t>(vertices.size() * sizeof(HandDrawVertex)),
                     vertices.data(), GL_DYNAMIC_DRAW_VALUE);
    g_gl_enable_vertex_attrib_array(0);
    g_gl_enable_vertex_attrib_array(1);
    g_gl_enable_vertex_attrib_array(2);
    g_gl_enable_vertex_attrib_array(3);
    g_gl_vertex_attrib_pointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(HandDrawVertex),
                               reinterpret_cast<void*>(offsetof(HandDrawVertex, x)));
    g_gl_vertex_attrib_pointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(HandDrawVertex),
                               reinterpret_cast<void*>(offsetof(HandDrawVertex, u)));
    g_gl_vertex_attrib_pointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(HandDrawVertex),
                               reinterpret_cast<void*>(offsetof(HandDrawVertex, shade)));
    g_gl_vertex_attrib_pointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(HandDrawVertex),
                               reinterpret_cast<void*>(offsetof(HandDrawVertex, depth)));
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));

    glBlendFunc(static_cast<GLenum>(old_blend_src),
                static_cast<GLenum>(old_blend_dst));
    if (blend_enabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (cull_enabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glDepthFunc(static_cast<GLenum>(old_depth_func));
    glDepthMask(old_depth_mask);
    if (depth_enabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (scissor_enabled) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    glColorMask(old_color_mask[0], old_color_mask[1], old_color_mask[2], old_color_mask[3]);
    glViewport(old_viewport[0], old_viewport[1], old_viewport[2], old_viewport[3]);
    g_gl_bind_buffer(GL_ARRAY_BUFFER_VALUE, static_cast<GLuint>(old_array_buffer));
    g_gl_bind_vertex_array(static_cast<GLuint>(old_vertex_array));
    g_gl_use_program(static_cast<GLuint>(old_program));
    g_gl_active_texture(GL_TEXTURE1_VALUE);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(old_texture1));
    g_gl_active_texture(GL_TEXTURE0_VALUE);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(old_texture0));
    g_gl_active_texture(static_cast<GLenum>(old_active_texture));
    return true;
}

bool valid_item_atlas_key(const HeldItemAtlasKey& key) {
    return key.texture != 0 && key.offset_x >= 0 && key.offset_y >= 0 &&
           key.atlas_width > 0 && key.atlas_height > 0 &&
           key.offset_x < key.atlas_width && key.offset_y < key.atlas_height &&
           key.index_count > 0;
}

bool read_item_atlas_patch(const HeldItemAtlasKey& key,
                           uint32_t width,
                           uint32_t height,
                           int x_offset,
                           int y_offset,
                           std::vector<uint8_t>& rgba) {
    if (!g_gl_get_texture_sub_image || width == 0 || height == 0 ||
        x_offset < 0 ||
        x_offset + static_cast<int>(width) > key.atlas_width ||
        y_offset < 0 || y_offset + static_cast<int>(height) > key.atlas_height) {
        return false;
    }
    const size_t byte_count =
        static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    if (byte_count > static_cast<size_t>((std::numeric_limits<GLsizei>::max)())) {
        return false;
    }
    rgba.resize(byte_count);
    g_gl_get_texture_sub_image(
        key.texture, 0, x_offset, y_offset, 0,
        static_cast<GLsizei>(width), static_cast<GLsizei>(height), 1,
        GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(byte_count), rgba.data());
    return true;
}

bool match_held_item_definition(const HeldItemAtlasKey& key,
                                const hytalevr::VrItemAssetCatalog& catalog,
                                size_t& definition_index,
                                double& score,
                                bool& exact_match,
                                double& score_margin) {
    definition_index = (std::numeric_limits<size_t>::max)();
    exact_match = false;
    score_margin = (std::numeric_limits<double>::infinity)();
    if (!valid_item_atlas_key(key) || !g_gl_get_texture_sub_image) {
        item_diagnostic_log_once(
            16u, g_gl_get_texture_sub_image
                     ? "atlas key is invalid"
                     : "glGetTextureSubImage is unavailable");
        return false;
    }
    score = (std::numeric_limits<double>::infinity)();
    double best_rank = (std::numeric_limits<double>::infinity)();
    double second_best_rank = (std::numeric_limits<double>::infinity)();
    size_t best_definition = 0;
    bool found = false;
    bool alpha_match_found = false;
    double alpha_match_score =
        (std::numeric_limits<double>::infinity)();
    double alpha_match_rank =
        (std::numeric_limits<double>::infinity)();
    double alpha_second_rank =
        (std::numeric_limits<double>::infinity)();
    size_t alpha_match_definition = 0;

    const int observed_span_x =
        (std::max)(0, key.uv_max_x - key.uv_min_x);
    const int observed_span_y =
        (std::max)(0, key.uv_max_y - key.uv_min_y);
    const auto structural_score =
        [&](const hytalevr::VrItemAssetDefinition& definition) {
            const int expected_span_x =
                (std::max)(0, definition.uv_max_x - definition.uv_min_x);
            const int expected_span_y =
                (std::max)(0, definition.uv_max_y - definition.uv_min_y);
            double value = 0.0;
            if (observed_span_x > 0 && expected_span_x > 0) {
                value += std::abs(observed_span_x - expected_span_x) * 4.0;
            }
            if (observed_span_y > 0 && expected_span_y > 0) {
                value += std::abs(observed_span_y - expected_span_y) * 4.0;
            }
            if (key.index_count > 0 && definition.expected_vertex_count > 0) {
                value += std::abs(
                    key.index_count -
                    static_cast<int>(definition.expected_vertex_count)) * 0.20;
            }
            value -= static_cast<double>(definition.preference) * 0.001;
            return value;
        };
    const auto structurally_compatible =
        [&](const hytalevr::VrItemAssetDefinition& definition) {
            const int expected_span_x =
                (std::max)(0, definition.uv_max_x - definition.uv_min_x);
            const int expected_span_y =
                (std::max)(0, definition.uv_max_y - definition.uv_min_y);
            if (observed_span_x > 0 && expected_span_x > 0 &&
                std::abs(observed_span_x - expected_span_x) >
                    (std::max)(4, observed_span_x / 8)) {
                return false;
            }
            if (observed_span_y > 0 && expected_span_y > 0 &&
                std::abs(observed_span_y - expected_span_y) >
                    (std::max)(4, observed_span_y / 8)) {
                return false;
            }
            if (key.index_count > 0 && definition.expected_vertex_count > 0 &&
                std::abs(
                    key.index_count -
                    static_cast<int>(definition.expected_vertex_count)) >
                    (std::max)(48, key.index_count / 3)) {
                return false;
            }
            return true;
        };

    struct CandidateSelection {
        const hytalevr::VrItemTextureCandidate* candidate = nullptr;
        size_t definition = 0;
        double structure = 0.0;
    };
    std::vector<CandidateSelection> selections;
    const auto select_candidates = [&](bool strict) {
        selections.clear();
        for (const hytalevr::VrItemTextureCandidate& candidate :
             catalog.texture_candidates) {
            size_t selected_definition =
                (std::numeric_limits<size_t>::max)();
            double selected_score =
                (std::numeric_limits<double>::infinity)();
            for (size_t candidate_definition :
                 candidate.definition_indices) {
                if (candidate_definition >= catalog.definitions.size()) continue;
                const hytalevr::VrItemAssetDefinition& definition =
                    catalog.definitions[candidate_definition];
                if (strict && !structurally_compatible(definition)) continue;
                const double candidate_score =
                    structural_score(definition);
                if (candidate_score < selected_score) {
                    selected_score = candidate_score;
                    selected_definition = candidate_definition;
                }
            }
            if (selected_definition < catalog.definitions.size()) {
                selections.push_back({
                    &candidate, selected_definition, selected_score});
            }
        }
    };
    select_candidates(true);
    if (selections.empty()) select_candidates(false);
    // Exact matches can return immediately. Trying the closest mesh/UV layouts
    // first avoids hundreds of synchronous atlas readbacks for common items.
    std::stable_sort(
        selections.begin(), selections.end(),
        [](const CandidateSelection& left, const CandidateSelection& right) {
            return left.structure < right.structure;
        });

    struct PatchKey {
        int x = 0;
        int y = 0;
        uint32_t width = 0;
        uint32_t height = 0;

        bool operator==(const PatchKey& other) const {
            return x == other.x && y == other.y &&
                   width == other.width && height == other.height;
        }
    };
    struct PatchKeyHash {
        size_t operator()(const PatchKey& value) const {
            size_t hash = static_cast<size_t>(
                static_cast<uint32_t>(value.x));
            hash ^= static_cast<size_t>(
                static_cast<uint32_t>(value.y)) << 1;
            hash ^= static_cast<size_t>(value.width) << 32;
            hash ^= static_cast<size_t>(value.height) << 17;
            return hash;
        }
    };
    std::unordered_map<PatchKey, std::vector<uint8_t>, PatchKeyHash>
        patch_cache;

    for (const CandidateSelection& selection : selections) {
        const hytalevr::VrItemTextureCandidate& candidate =
            *selection.candidate;
        const hytalevr::VrItemAssetDefinition& definition =
            catalog.definitions[selection.definition];
        const uint32_t width = candidate.width;
        const uint32_t height = candidate.height;
        const int x_offset = key.offset_x - definition.uv_min_x;
        const int direct_y_offset = key.offset_y - definition.uv_min_y;
        if (x_offset < 0 ||
            x_offset + static_cast<int>(width) > key.atlas_width ||
            static_cast<int>(height) > key.atlas_height) {
            continue;
        }
        const std::array<int, 2> y_offsets{
            key.atlas_height - direct_y_offset - static_cast<int>(height),
            direct_y_offset};
        for (int location = 0; location < 2; ++location) {
            if (location == 1 && y_offsets[1] == y_offsets[0]) continue;
            const PatchKey patch_key{
                x_offset, y_offsets[location], width, height};
            auto patch_it = patch_cache.find(patch_key);
            if (patch_it == patch_cache.end()) {
                std::vector<uint8_t> patch;
                if (!read_item_atlas_patch(
                        key, width, height, x_offset,
                        y_offsets[location], patch)) {
                    continue;
                }
                patch_it = patch_cache.emplace(
                    patch_key, std::move(patch)).first;
            }
            const std::vector<uint8_t>& patch = patch_it->second;
            const size_t stride = static_cast<size_t>(width) * 4;
            const uint64_t fingerprint = hytalevr::vr_item_texture_fingerprint(
                patch.data(), width, height, stride, false);
            const uint64_t alpha_fingerprint =
                hytalevr::vr_item_alpha_fingerprint(
                    patch.data(), width, height, stride, false);

            if (fingerprint == candidate.fingerprint ||
                fingerprint == candidate.flipped_fingerprint) {
                definition_index = selection.definition;
                score = 0.0;
                exact_match = true;
                return true;
            }

            const double normal = hytalevr::vr_item_texture_difference(
                patch.data(), stride, false, candidate);
            const double flipped = hytalevr::vr_item_texture_difference(
                patch.data(), stride, true, candidate);
            const double difference = (std::min)(normal, flipped);
            const double rank =
                difference +
                std::clamp(selection.structure, 0.0, 256.0) * 0.12;
            if (alpha_fingerprint == candidate.alpha_fingerprint ||
                alpha_fingerprint == candidate.flipped_alpha_fingerprint) {
                if (rank < alpha_match_rank) {
                    alpha_second_rank = alpha_match_rank;
                    alpha_match_rank = rank;
                    alpha_match_score = difference;
                    alpha_match_definition = selection.definition;
                    alpha_match_found = true;
                } else if (rank < alpha_second_rank) {
                    alpha_second_rank = rank;
                }
            }
            if (rank < best_rank) {
                second_best_rank = best_rank;
                best_rank = rank;
                score = difference;
                best_definition = selection.definition;
                found = true;
            } else if (rank < second_best_rank) {
                second_best_rank = rank;
            }
        }
    }
    if (alpha_match_found) {
        definition_index = alpha_match_definition;
        score = alpha_match_score;
        score_margin = alpha_second_rank - alpha_match_rank;
        const bool separated =
            !std::isfinite(alpha_second_rank) || score <= 12.0 ||
            score_margin >= 1.0;
        return score <= 64.0 && separated;
    }
    if (!found) return false;
    definition_index = best_definition;
    score_margin = second_best_rank - best_rank;
    const bool separated =
        !std::isfinite(second_best_rank) || score <= 8.0 ||
        score_margin >= 1.0;
    return score <= 32.0 && separated;
}

bool capture_held_item_region(const HeldItemAtlasKey& key,
                              CapturedHeldItemRegion& region,
                              std::string& error) {
    region = {};
    const int observed_width =
        (std::max)(1, key.uv_max_x - key.uv_min_x);
    const int observed_height =
        (std::max)(1, key.uv_max_y - key.uv_min_y);
    const uint32_t width = static_cast<uint32_t>(
        std::clamp(observed_width, 1, 512));
    const uint32_t height = static_cast<uint32_t>(
        std::clamp(observed_height, 1, 512));
    if (key.offset_x < 0 ||
        key.offset_x + static_cast<int>(width) > key.atlas_width) {
        error = "captured item UV region is outside the atlas";
        return false;
    }

    const std::array<int, 2> y_offsets{
        key.atlas_height - key.offset_y - static_cast<int>(height),
        key.offset_y};
    std::vector<uint8_t> best_patch;
    uint64_t best_coverage = 0;
    for (int location = 0; location < 2; ++location) {
        if (location == 1 && y_offsets[1] == y_offsets[0]) continue;
        std::vector<uint8_t> patch;
        if (!read_item_atlas_patch(
                key, width, height, key.offset_x,
                y_offsets[location], patch)) {
            continue;
        }
        uint64_t coverage = 0;
        for (size_t pixel = 0; pixel + 3 < patch.size(); pixel += 4) {
            coverage += patch[pixel + 3];
        }
        if (best_patch.empty() || coverage > best_coverage) {
            best_coverage = coverage;
            best_patch = std::move(patch);
        }
    }
    if (best_patch.empty() || best_coverage == 0) {
        error = "captured item atlas region is empty";
        return false;
    }
    region.width = width;
    region.height = height;
    region.rgba = std::move(best_patch);
    const size_t stride = static_cast<size_t>(width) * 4;
    const uint64_t normal = hytalevr::vr_item_texture_fingerprint(
        region.rgba.data(), width, height, stride, false);
    const uint64_t flipped = hytalevr::vr_item_texture_fingerprint(
        region.rgba.data(), width, height, stride, true);
    region.signature = normal == 0
        ? flipped
        : (flipped == 0 ? normal : (std::min)(normal, flipped));
    return region.signature != 0;
}

HeldItemCacheKey held_item_cache_key(
        const HeldItemAtlasKey& key,
        const CapturedHeldItemRegion& region) {
    return {
        region.signature,
        key.index_count,
        (std::max)(0, key.uv_max_x - key.uv_min_x),
        (std::max)(0, key.uv_max_y - key.uv_min_y)};
}

bool confirm_held_item_detection(int side,
                                 const HeldItemCacheKey& identity,
                                 size_t definition_index,
                                 bool fallback,
                                 bool exact_match,
                                 LONG frame) {
    if (side < 0 || side > 1) return false;
    PendingHeldItemDetection& pending = g_pending_item_detections[side];
    if (exact_match) {
        pending = {};
        return true;
    }

    const bool consecutive =
        pending.identity_valid &&
        pending.identity == identity &&
        pending.definition_index == definition_index &&
        pending.fallback == fallback &&
        pending.last_frame >= 0 &&
        frame > pending.last_frame &&
        frame - pending.last_frame <= 12;
    if (!consecutive) {
        pending = {};
        pending.identity = identity;
        pending.identity_valid = true;
        pending.definition_index = definition_index;
        pending.fallback = fallback;
        pending.confirmations = 1;
    } else {
        ++pending.confirmations;
    }
    pending.last_frame = frame;
    if (pending.confirmations < 2) return false;
    pending = {};
    return true;
}

bool load_captured_item_fallback(const CapturedHeldItemRegion& region,
                                 LoadedVrItemState& state,
                                 std::string& error) {
    if (region.rgba.empty() || region.width == 0 || region.height == 0) {
        error = "captured item region is unavailable";
        return false;
    }
    state.definition_index = (std::numeric_limits<size_t>::max)();
    return hytalevr::build_vr_item_fallback_cube(
        region.rgba, region.width, region.height, state.asset, error);
}

void cache_loaded_held_item(const HeldItemCacheKey& key,
                            const LoadedVrItemState& state) {
    if (key.texture_signature == 0 || !state.asset_ready) {
        return;
    }
    CachedHeldItemAsset cached{};
    cached.definition_index = state.definition_index;
    cached.asset = state.asset;
    cached.last_used = ++g_held_item_cache_sequence;
    g_held_item_asset_cache[key] = std::move(cached);
    constexpr size_t kMaximumCachedItems = 64;
    if (g_held_item_asset_cache.size() <= kMaximumCachedItems) return;

    auto oldest = g_held_item_asset_cache.end();
    for (auto it = g_held_item_asset_cache.begin();
         it != g_held_item_asset_cache.end(); ++it) {
        if (oldest == g_held_item_asset_cache.end() ||
            it->second.last_used < oldest->second.last_used) {
            oldest = it;
        }
    }
    if (oldest != g_held_item_asset_cache.end()) {
        g_held_item_asset_cache.erase(oldest);
    }
}

bool upload_held_item_texture(LoadedVrItemState& state) {
    if (state.asset.texture_rgba.empty() ||
        state.asset.texture_width == 0 || state.asset.texture_height == 0 ||
        !ensure_hand_gl_renderer()) {
        return false;
    }
    GLint old_active_texture = GL_TEXTURE0_VALUE;
    GLint old_texture = 0;
    GLint old_unpack = 4;
    glGetIntegerv(GL_ACTIVE_TEXTURE_VALUE, &old_active_texture);
    g_gl_active_texture(GL_TEXTURE0_VALUE);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT_VALUE, &old_unpack);
    if (state.texture_gl) glDeleteTextures(1, &state.texture_gl);
    glGenTextures(1, &state.texture_gl);
    glBindTexture(GL_TEXTURE_2D, state.texture_gl);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE_VALUE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE_VALUE);
    glPixelStorei(GL_UNPACK_ALIGNMENT_VALUE, 1);
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA,
        static_cast<GLsizei>(state.asset.texture_width),
        static_cast<GLsizei>(state.asset.texture_height), 0,
        GL_RGBA, GL_UNSIGNED_BYTE, state.asset.texture_rgba.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT_VALUE, old_unpack);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(old_texture));
    g_gl_active_texture(static_cast<GLenum>(old_active_texture));
    return state.texture_gl != 0;
}

bool ensure_torch_flame_texture() {
    if (g_torch_flame_texture_gl != 0) return true;
    if (g_torch_flame_texture_attempted ||
        g_item_catalog_state.load(std::memory_order_acquire) != 2 ||
        !ensure_hand_gl_renderer()) {
        return false;
    }

    std::shared_ptr<hytalevr::VrItemAssetCatalog> catalog;
    {
        std::lock_guard<std::mutex> lock(g_item_catalog_mutex);
        catalog = g_item_catalog;
    }
    if (!catalog) return false;

    g_torch_flame_texture_attempted = true;
    std::vector<uint8_t> rgba;
    std::string error;
    if (!hytalevr::load_vr_asset_texture(
            *catalog,
            "Common/Particles/Textures/Fire/Fire_Center_Erosion32.png",
            g_torch_flame_texture_width, g_torch_flame_texture_height,
            rgba, error) ||
        g_torch_flame_texture_width < 2 ||
        g_torch_flame_texture_height < 2) {
        render_log("torch flame: texture load failed: " + error);
        return false;
    }

    GLint old_active_texture = GL_TEXTURE0_VALUE;
    GLint old_texture = 0;
    GLint old_unpack = 4;
    glGetIntegerv(GL_ACTIVE_TEXTURE_VALUE, &old_active_texture);
    g_gl_active_texture(GL_TEXTURE0_VALUE);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT_VALUE, &old_unpack);
    glGenTextures(1, &g_torch_flame_texture_gl);
    glBindTexture(GL_TEXTURE_2D, g_torch_flame_texture_gl);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE_VALUE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE_VALUE);
    glPixelStorei(GL_UNPACK_ALIGNMENT_VALUE, 1);
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA,
        static_cast<GLsizei>(g_torch_flame_texture_width),
        static_cast<GLsizei>(g_torch_flame_texture_height), 0,
        GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT_VALUE, old_unpack);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(old_texture));
    g_gl_active_texture(static_cast<GLenum>(old_active_texture));
    if (g_torch_flame_texture_gl != 0) {
        render_log(
            "torch flame: Hytale particle texture loaded " +
            std::to_string(g_torch_flame_texture_width) + "x" +
            std::to_string(g_torch_flame_texture_height));
    }
    return g_torch_flame_texture_gl != 0;
}

void update_held_item_assets(const HeldItemAtlasSnapshot& snapshot) {
    ensure_item_catalog_loading();
    if (!snapshot.valid || snapshot.frame == g_last_item_detection_frame ||
        g_item_catalog_state.load() != 2) {
        return;
    }
    g_last_item_detection_frame = snapshot.frame;
    const ULONGLONG now = GetTickCount64();
    std::shared_ptr<hytalevr::VrItemAssetCatalog> catalog;
    {
        std::lock_guard<std::mutex> lock(g_item_catalog_mutex);
        catalog = g_item_catalog;
    }
    if (!catalog) return;

    for (int side = 0; side < 2; ++side) {
        if (now < g_held_item_detection_hold_until[side].load(
                      std::memory_order_relaxed) ||
            now < g_held_item_suppressed_until[side].load(
                      std::memory_order_relaxed)) {
            continue;
        }
        if ((snapshot.valid_mask & (1L << side)) == 0) continue;
        const HeldItemAtlasKey& key = snapshot.keys[side];
        if (!valid_item_atlas_key(key)) continue;
        LoadedVrItemState& current_state = g_loaded_items[side];
        if (current_state.asset_ready && current_state.key_valid &&
            current_state.key == key) {
            // Texture id, atlas position, VAO, index count and UV bounds form
            // the draw identity. Re-reading unchanged GPU data every 30 frames
            // caused the regular render-thread freezes seen in VR.
            current_state.last_verified_frame = snapshot.frame;
            g_pending_item_detections[side] = {};
            continue;
        }

        CapturedHeldItemRegion captured_region;
        std::string capture_error;
        const bool captured = capture_held_item_region(
            key, captured_region, capture_error);
        HeldItemCacheKey cache_key{};
        if (captured) {
            cache_key = held_item_cache_key(key, captured_region);
            if (current_state.asset_ready && current_state.identity_valid &&
                current_state.identity == cache_key) {
                current_state.key = key;
                current_state.key_valid = true;
                current_state.last_verified_frame = snapshot.frame;
                g_pending_item_detections[side] = {};
                continue;
            }
            const auto cached = g_held_item_asset_cache.find(cache_key);
            if (cached != g_held_item_asset_cache.end()) {
                LoadedVrItemState restored{};
                restored.key = key;
                restored.identity = cache_key;
                restored.key_valid = true;
                restored.identity_valid = true;
                restored.definition_index = cached->second.definition_index;
                restored.last_verified_frame = snapshot.frame;
                restored.asset = cached->second.asset;
                cached->second.last_used = ++g_held_item_cache_sequence;
                if (upload_held_item_texture(restored)) {
                    restored.asset_ready = true;
                    if (current_state.texture_gl) {
                        glDeleteTextures(1, &current_state.texture_gl);
                    }
                    current_state = std::move(restored);
                    g_pending_item_detections[side] = {};
                    render_log(
                        "held item: cache restored side=" +
                        std::to_string(side) + " id=" +
                        current_state.asset.definition.id);
                    continue;
                }
            }
        }

        LoadedVrItemState next_state{};
        next_state.key = key;
        next_state.key_valid = true;
        next_state.last_verified_frame = snapshot.frame;
        if (captured) {
            next_state.identity = cache_key;
            next_state.identity_valid = true;
        }

        size_t definition_index = 0;
        double score = 0.0;
        bool exact_match = false;
        double score_margin = 0.0;
        const bool matched = match_held_item_definition(
            key, *catalog, definition_index, score, exact_match, score_margin);
        const bool fallback = !matched;
        const size_t confirmed_definition = matched
            ? definition_index
            : (std::numeric_limits<size_t>::max)();
        if (!captured && !exact_match) {
            render_log(
                "held item: capture failed side=" + std::to_string(side) +
                " reason=" + capture_error);
            continue;
        }
        if (captured &&
            !confirm_held_item_detection(
                side, cache_key, confirmed_definition, fallback,
                exact_match, snapshot.frame)) {
            continue;
        }

        if (!matched) {
            std::ostringstream out;
            out << "held item: atlas match failed side=" << side
                << " offset=" << key.offset_x << "," << key.offset_y
                << " atlas=" << key.atlas_width << "x" << key.atlas_height
                << " bestScore=" << std::fixed << std::setprecision(2) << score
                << " margin=" << score_margin;
            if (definition_index < catalog->definitions.size()) {
                out << " bestId=" << catalog->definitions[definition_index].id;
            }
            render_log(out.str());
            item_diagnostic_log_once(32u << side, out.str());
            std::string fallback_error;
            if (!captured ||
                !load_captured_item_fallback(
                    captured_region, next_state, fallback_error)) {
                render_log(
                    "held item: captured fallback failed: " +
                    (captured ? fallback_error : capture_error));
                continue;
            }
        } else {
            if (current_state.asset_ready &&
                current_state.definition_index == definition_index) {
                current_state.key = key;
                current_state.key_valid = true;
                current_state.last_verified_frame = snapshot.frame;
                if (captured) {
                    current_state.identity = cache_key;
                    current_state.identity_valid = true;
                    cache_loaded_held_item(cache_key, current_state);
                }
                continue;
            }
            std::string error;
            next_state.definition_index = definition_index;
            if (!hytalevr::load_vr_item_render_asset(
                    *catalog, definition_index, next_state.asset, error)) {
                render_log("held item: asset load failed: " + error);
                if (!captured ||
                    !load_captured_item_fallback(
                        captured_region, next_state, error)) {
                    render_log(
                        "held item: captured fallback failed: " +
                        (captured ? error : capture_error));
                    continue;
                }
            }
        }
        if (!upload_held_item_texture(next_state)) {
            render_log("held item: OpenGL texture upload failed");
            continue;
        }
        next_state.asset_ready = true;
        if (captured) cache_loaded_held_item(cache_key, next_state);
        if (current_state.texture_gl) {
            glDeleteTextures(1, &current_state.texture_gl);
        }
        current_state = std::move(next_state);
        std::ostringstream out;
        out << "held item: matched side=" << side
            << " id=" << current_state.asset.definition.id
            << " score=" << std::fixed << std::setprecision(2) << score
            << " margin=" << score_margin
            << " exact=" << (exact_match ? 1 : 0)
            << " vertices=" << current_state.asset.vertices.size();
        render_log(out.str());
        item_diagnostic_log_once(128u << side, out.str());
    }
}

Vec3 transform_item_local_point(float x, float y, float z,
                                const float controller[12],
                                bool right_hand,
                                const VrCameraControls& controls) {
    const float world_scale = validated_world_scale(controls);
    const float scale =
        (1.0f / 64.0f) * std::clamp(controls.held_item_scale, 0.10f, 2.0f) *
        world_scale;
    const float local_right =
        x * scale * (right_hand ? 1.0f : -1.0f) +
        std::clamp(controls.held_item_offset_x, -0.30f, 0.30f) * world_scale;
    const float local_up =
        z * scale +
        std::clamp(controls.held_item_offset_y, -0.30f, 0.30f) * world_scale;
    const float local_forward =
        y * scale -
        std::clamp(controls.held_item_offset_z, -0.30f, 0.30f) * world_scale;

    Vec3 world{};
    for (int row = 0; row < 3; ++row) {
        const float right = controller[row * 4 + 0];
        const float up = controller[row * 4 + 1];
        const float forward = -controller[row * 4 + 2];
        (&world.x)[row] = controller[row * 4 + 3] +
            right * local_right + up * local_up + forward * local_forward;
    }
    return world;
}

Vec3 transform_item_vertex(const hytalevr::VrItemVertex& vertex,
                           const float controller[12],
                           bool right_hand,
                           const VrCameraControls& controls) {
    return transform_item_local_point(
        vertex.x, vertex.y, vertex.z, controller, right_hand, controls);
}

Vec3 transform_item_normal(const hytalevr::VrItemVertex& vertex,
                           const float controller[12],
                           bool right_hand) {
    const float local_right = vertex.nx * (right_hand ? 1.0f : -1.0f);
    const float local_up = vertex.nz;
    const float local_forward = vertex.ny;
    Vec3 world{};
    for (int row = 0; row < 3; ++row) {
        const float right = controller[row * 4 + 0];
        const float up = controller[row * 4 + 1];
        const float forward = -controller[row * 4 + 2];
        (&world.x)[row] =
            right * local_right + up * local_up + forward * local_forward;
    }
    const float length =
        std::sqrt(world.x * world.x + world.y * world.y + world.z * world.z);
    if (length > 0.0001f) {
        world.x /= length;
        world.y /= length;
        world.z /= length;
    }
    return world;
}

void draw_vr_torch_flame(int eye, int width, int height,
                         const float hmd[12],
                         const float controller[12],
                         bool right_hand,
                         const VrCameraControls& controls,
                         const hytalevr::VrItemRenderAsset& asset) {
    if (!asset.needs_flame_effect || !asset.flame_anchor.valid ||
        !hytalevr::vr_item_id_is_torch(asset.definition.id) ||
        !ensure_torch_flame_texture()) {
        return;
    }

    Vec3 anchor = transform_item_local_point(
        asset.flame_anchor.x, asset.flame_anchor.y, asset.flame_anchor.z,
        controller, right_hand, controls);
    Vec3 billboard_right{hmd[0], hmd[4], hmd[8]};
    const float right_length = std::sqrt(
        billboard_right.x * billboard_right.x +
        billboard_right.y * billboard_right.y +
        billboard_right.z * billboard_right.z);
    if (right_length <= 0.0001f) return;
    billboard_right.x /= right_length;
    billboard_right.y /= right_length;
    billboard_right.z /= right_length;

    const float model_scale =
        (1.0f / 64.0f) *
        std::clamp(controls.held_item_scale, 0.10f, 2.0f) *
        validated_world_scale(controls);
    const float phase = static_cast<float>(
        (GetTickCount64() % 1000ull) * (6.28318530717958647692 / 1000.0));
    const float flame_width = 16.0f * model_scale *
        (0.94f + 0.06f * std::sin(phase * 1.7f));
    const float flame_height = 24.0f * model_scale *
        (0.96f + 0.04f * std::sin(phase * 1.3f + 0.7f));
    const float sway = std::sin(phase) * 1.2f * model_scale;
    anchor.x += billboard_right.x * sway;
    anchor.y += 0.5f * model_scale;
    anchor.z += billboard_right.z * sway;

    const Vec3 world_up{0.0f, 1.0f, 0.0f};
    const float half_width = flame_width * 0.5f;
    const Vec3 corners[4]{
        {anchor.x - billboard_right.x * half_width,
         anchor.y - billboard_right.y * half_width,
         anchor.z - billboard_right.z * half_width},
        {anchor.x + billboard_right.x * half_width,
         anchor.y + billboard_right.y * half_width,
         anchor.z + billboard_right.z * half_width},
        {anchor.x + billboard_right.x * half_width + world_up.x * flame_height,
         anchor.y + billboard_right.y * half_width + world_up.y * flame_height,
         anchor.z + billboard_right.z * half_width + world_up.z * flame_height},
        {anchor.x - billboard_right.x * half_width + world_up.x * flame_height,
         anchor.y - billboard_right.y * half_width + world_up.y * flame_height,
         anchor.z - billboard_right.z * half_width + world_up.z * flame_height},
    };

    ProjectedHandVertex projected[4]{};
    for (int corner = 0; corner < 4; ++corner) {
        float ndc_x = 0.0f;
        float ndc_y = 0.0f;
        float depth = 0.0f;
        if (!project_tracking_point_to_eye_ndc_raw(
                eye, hmd, corners[corner], ndc_x, ndc_y, depth)) {
            return;
        }
        projected[corner].x =
            (ndc_x * 0.5f + 0.5f) * static_cast<float>(width);
        projected[corner].y =
            (ndc_y * 0.5f + 0.5f) * static_cast<float>(height);
        projected[corner].depth = depth;
        projected[corner].shade = 1.08f;
    }

    const int frame = static_cast<int>((GetTickCount64() / 90ull) % 4ull);
    const float u0 = static_cast<float>(frame % 2) * 0.5f;
    const float v0 = static_cast<float>(frame / 2) * 0.5f;
    const float u1 = u0 + 0.5f;
    const float v1 = v0 + 0.5f;
    projected[0].u = u0;
    projected[0].v = 1.0f - v1;
    projected[1].u = u1;
    projected[1].v = 1.0f - v1;
    projected[2].u = u1;
    projected[2].v = 1.0f - v0;
    projected[3].u = u0;
    projected[3].v = 1.0f - v0;

    std::vector<ProjectedHandTriangle> triangles(2);
    triangles[0].v[0] = projected[0];
    triangles[0].v[1] = projected[1];
    triangles[0].v[2] = projected[2];
    triangles[1].v[0] = projected[0];
    triangles[1].v[1] = projected[2];
    triangles[1].v[2] = projected[3];
    draw_projected_textured_triangles_gl(
        triangles, width, height, eye, g_torch_flame_texture_gl,
        controls.hand_depth_tolerance, true);
}

void draw_vr_item_models(int eye, int width, int height,
                         const float hmd[12],
                         const float controller_poses[2][12],
                         const bool controller_valid[2],
                         const VrCameraControls& controls) {
    const HeldItemAtlasSnapshot snapshot = held_item_atlas_snapshot();
    if (snapshot.valid) update_held_item_assets(snapshot);
    const LONG presence_mask = held_item_presence_mask();
    const ULONGLONG now = GetTickCount64();
    const bool item_suppressed[2]{
        now < g_held_item_suppressed_until[0].load(
                  std::memory_order_relaxed),
        now < g_held_item_suppressed_until[1].load(
                  std::memory_order_relaxed)};
    if (!g_right_trigger_pressed && !g_left_trigger_pressed &&
        !item_suppressed[0] && !item_suppressed[1]) {
        g_stable_held_item_presence_mask.store(
            presence_mask, std::memory_order_relaxed);
    }
    const bool detection_held[2]{
        now < g_held_item_detection_hold_until[0].load(
                  std::memory_order_relaxed),
        now < g_held_item_detection_hold_until[1].load(
                  std::memory_order_relaxed)};
    if (presence_mask == 0 && !detection_held[0] && !detection_held[1]) return;

    const Vec3 eye_position{hmd[3], hmd[7], hmd[11]};
    const Vec3 light{-0.25f, 0.55f, -0.80f};
    static thread_local std::vector<ProjectedHandTriangle> triangles;
    for (int hand = 0; hand < 2; ++hand) {
        if (item_suppressed[hand]) {
            continue;
        }
        if (!controller_valid[hand] ||
            ((presence_mask & (1L << hand)) == 0 &&
             !(detection_held[hand] && g_loaded_items[hand].asset_ready))) {
            continue;
        }
        const LoadedVrItemState& state = g_loaded_items[hand];
        if (!state.asset_ready || !state.key_valid || state.texture_gl == 0) {
            continue;
        }
        if (hand == 0 &&
            hytalevr::vr_item_id_is_shield(state.asset.definition.id)) {
            const LoadedVrItemState& main_item = g_loaded_items[1];
            const bool main_item_is_current =
                !snapshot.valid ||
                (snapshot.valid_mask & (1L << 1)) == 0 ||
                (main_item.key_valid && main_item.key == snapshot.keys[1]);
            const std::string_view main_item_id =
                main_item.asset_ready && main_item_is_current
                    ? std::string_view(main_item.asset.definition.id)
                    : std::string_view();
            if (!hytalevr::vr_item_shield_allowed_for_main_item(main_item_id)) {
                continue;
            }
        }
        const bool detection_pending =
            g_pending_item_detections[hand].identity_valid;
        if (snapshot.valid &&
            (snapshot.valid_mask & (1L << hand)) != 0 &&
            !(state.key == snapshot.keys[hand]) &&
            !detection_held[hand] && !detection_pending) {
            continue;
        }

        triangles.clear();
        const size_t triangle_count = state.asset.vertices.size() / 3;
        if (triangles.capacity() < triangle_count) {
            triangles.reserve(triangle_count);
        }
        const float* controller = controller_poses[hand];
        for (size_t index = 0;
             index + 2 < state.asset.vertices.size();
             index += 3) {
            ProjectedHandTriangle triangle{};
            bool valid = true;
            Vec3 world_center{};
            float shade_sum = 0.0f;
            for (int corner = 0; corner < 3; ++corner) {
                const hytalevr::VrItemVertex& source =
                    state.asset.vertices[index + corner];
                const Vec3 world = transform_item_vertex(
                    source, controller, hand == 1, controls);
                world_center.x += world.x;
                world_center.y += world.y;
                world_center.z += world.z;
                float ndc_x = 0.0f;
                float ndc_y = 0.0f;
                float depth = 0.0f;
                if (!project_tracking_point_to_eye_ndc_raw(
                        eye, hmd, world, ndc_x, ndc_y, depth)) {
                    valid = false;
                    break;
                }
                const Vec3 normal = transform_item_normal(
                    source, controller, hand == 1);
                const float lit = std::clamp(
                    normal.x * light.x + normal.y * light.y + normal.z * light.z,
                    -1.0f, 1.0f);
                const float shade =
                    std::clamp(lit * 0.20f + 0.82f, 0.58f, 1.0f);
                triangle.v[corner].x =
                    (ndc_x * 0.5f + 0.5f) * static_cast<float>(width);
                triangle.v[corner].y =
                    (ndc_y * 0.5f + 0.5f) * static_cast<float>(height);
                triangle.v[corner].depth = depth;
                triangle.v[corner].shade = shade;
                triangle.v[corner].u = source.u;
                // The shared hand shader flips OBJ UVs. Cancel that flip for
                // BlockyModel layouts, whose offsets use image-space Y.
                triangle.v[corner].v = 1.0f - source.v;
                shade_sum += shade;
            }
            if (!valid) continue;
            const float area =
                (triangle.v[1].x - triangle.v[0].x) *
                    (triangle.v[2].y - triangle.v[0].y) -
                (triangle.v[1].y - triangle.v[0].y) *
                    (triangle.v[2].x - triangle.v[0].x);
            if (!std::isfinite(area) || std::fabs(area) < 0.25f) continue;
            world_center.x /= 3.0f;
            world_center.y /= 3.0f;
            world_center.z /= 3.0f;
            const Vec3 eye_to_center{
                world_center.x - eye_position.x,
                world_center.y - eye_position.y,
                world_center.z - eye_position.z};
            triangle.depth =
                eye_to_center.x * eye_to_center.x +
                eye_to_center.y * eye_to_center.y +
                eye_to_center.z * eye_to_center.z;
            triangle.shade = shade_sum / 3.0f;
            triangles.push_back(triangle);
        }
        std::sort(
            triangles.begin(), triangles.end(),
            [](const ProjectedHandTriangle& a, const ProjectedHandTriangle& b) {
                return a.depth > b.depth;
            });
        draw_projected_textured_triangles_gl(
            triangles, width, height, eye, state.texture_gl,
            controls.hand_depth_tolerance);
        draw_vr_torch_flame(
            eye, width, height, hmd, controller, hand == 1,
            controls, state.asset);
    }
}

struct ModuleRange {
    unsigned char* base = nullptr;
    size_t size = 0;
};

struct PatternByte {
    unsigned char value = 0;
    bool wildcard = false;
};

int hex_nibble(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool parse_pattern(const char* text, std::vector<PatternByte>& out) {
    out.clear();
    const char* cursor = text;
    while (*cursor) {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') ++cursor;
        if (!*cursor) break;
        if (*cursor == '?') {
            ++cursor;
            if (*cursor == '?') ++cursor;
            out.push_back(PatternByte{0, true});
            continue;
        }
        const int high = hex_nibble(cursor[0]);
        const int low = hex_nibble(cursor[1]);
        if (high < 0 || low < 0) return false;
        out.push_back(PatternByte{static_cast<unsigned char>((high << 4) | low), false});
        cursor += 2;
    }
    return !out.empty();
}

bool pattern_matches(const unsigned char* data, size_t available,
                     const std::vector<PatternByte>& pattern) {
    if (available < pattern.size()) return false;
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (!pattern[i].wildcard && data[i] != pattern[i].value) return false;
    }
    return true;
}

ModuleRange current_executable_range() {
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (!base) return {};
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return {};
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return {};
    return {base, static_cast<size_t>(nt->OptionalHeader.SizeOfImage)};
}

bool is_executable_page(DWORD protect) {
    if (protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    protect &= 0xff;
    return protect == PAGE_EXECUTE || protect == PAGE_EXECUTE_READ ||
           protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
}

DistortionTextureSnapshot distortion_texture_snapshot() {
    DistortionTextureSnapshot snapshot{};
    if (!ensure_ui_scale_mapping() || !g_ui_scale_shared ||
        g_ui_scale_shared->disableDistortion != 2) {
        return snapshot;
    }

    const LONG texture = InterlockedCompareExchange(
        &g_ui_scale_shared->distortionTextureId, 0, 0);
    const LONG width = InterlockedCompareExchange(
        &g_ui_scale_shared->distortionTextureWidth, 0, 0);
    const LONG height = InterlockedCompareExchange(
        &g_ui_scale_shared->distortionTextureHeight, 0, 0);
    const LONG frame = InterlockedCompareExchange(
        &g_ui_scale_shared->distortionTextureFrame, 0, 0);
    const LONG current_frame = InterlockedCompareExchange(
        &g_ui_scale_shared->renderFrameSequence, 0, 0);
    if (texture <= 0 || width <= 0 || height <= 0 || frame < 0 ||
        current_frame < frame || current_frame - frame > 2) {
        return snapshot;
    }

    snapshot.texture = static_cast<GLuint>(texture);
    snapshot.width = static_cast<int>(width);
    snapshot.height = static_cast<int>(height);
    snapshot.frame = static_cast<uint32_t>(frame);
    snapshot.valid = true;
    return snapshot;
}

bool is_writable_page(DWORD protect) {
    if (protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    protect &= 0xff;
    return protect == PAGE_READWRITE || protect == PAGE_WRITECOPY ||
           protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
}

bool writable_memory_range(const void* address, size_t size) {
    if (!address || size == 0) return false;
    auto* cursor = static_cast<const unsigned char*>(address);
    const auto* end = cursor + size;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(cursor, &memory, sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT || !is_writable_page(memory.Protect)) {
            return false;
        }
        const auto* region_end = static_cast<const unsigned char*>(memory.BaseAddress) +
                                 memory.RegionSize;
        if (region_end <= cursor) return false;
        cursor = std::min(region_end, end);
    }
    return true;
}

bool matrix_is_finite_and_bounded(const Matrix4& matrix,
                                  float absolute_limit = 100000000.0f) {
    for (float value : matrix.value) {
        if (!std::isfinite(value) || std::fabs(value) > absolute_limit) {
            return false;
        }
    }
    return true;
}

bool plausible_camera_view(const Matrix4& view) {
    if (!matrix_is_finite_and_bounded(view) ||
        std::fabs(view.value[15] - 1.0f) > 0.10f ||
        std::fabs(view.value[3]) > 0.10f ||
        std::fabs(view.value[7]) > 0.10f ||
        std::fabs(view.value[11]) > 0.10f) {
        return false;
    }

    const auto basis_length = [&](int offset) {
        const float x = view.value[offset + 0];
        const float y = view.value[offset + 1];
        const float z = view.value[offset + 2];
        return std::sqrt(x * x + y * y + z * z);
    };
    const float x_length = basis_length(0);
    const float y_length = basis_length(4);
    const float z_length = basis_length(8);
    if (x_length < 0.50f || x_length > 1.50f ||
        y_length < 0.50f || y_length > 1.50f ||
        z_length < 0.50f || z_length > 1.50f) {
        return false;
    }

    const auto normalized_dot = [&](int a, int b, float denominator) {
        return (view.value[a + 0] * view.value[b + 0] +
                view.value[a + 1] * view.value[b + 1] +
                view.value[a + 2] * view.value[b + 2]) /
               denominator;
    };
    return std::fabs(normalized_dot(0, 4, x_length * y_length)) < 0.25f &&
           std::fabs(normalized_dot(0, 8, x_length * z_length)) < 0.25f &&
           std::fabs(normalized_dot(4, 8, y_length * z_length)) < 0.25f;
}

bool plausible_camera_projection(const Matrix4& projection) {
    if (!matrix_is_finite_and_bounded(projection, 100000.0f)) return false;
    const float horizontal = std::fabs(projection.value[0]);
    const float vertical = std::fabs(projection.value[5]);
    const float perspective_term = std::fabs(projection.value[11]);
    return horizontal >= 0.05f && horizontal <= 20.0f &&
           vertical >= 0.05f && vertical <= 20.0f &&
           perspective_term >= 0.50f && perspective_term <= 1.50f &&
           std::fabs(projection.value[15]) <= 0.10f;
}

bool read_validated_camera_layout(unsigned char* camera,
                                  Matrix4& view,
                                  Matrix4& projection) {
    if (!camera ||
        !writable_memory_range(
            camera + kCameraViewOffset, sizeof(Matrix4)) ||
        !writable_memory_range(
            camera + kCameraViewProjectionOffset, sizeof(Matrix4)) ||
        !writable_memory_range(
            camera + kCameraProjectionOffset, sizeof(Matrix4))) {
        return false;
    }

    Matrix4 view_projection{};
    std::memcpy(
        &view, camera + kCameraViewOffset, sizeof(view));
    std::memcpy(
        &view_projection, camera + kCameraViewProjectionOffset,
        sizeof(view_projection));
    std::memcpy(
        &projection, camera + kCameraProjectionOffset, sizeof(projection));
    return plausible_camera_view(view) &&
           plausible_camera_projection(projection) &&
           matrix_is_finite_and_bounded(view_projection);
}

using HookSiteValidator = bool (*)(const unsigned char*);

unsigned char* scan_unique_pattern(const ModuleRange& range, const char* name,
                                   const char* pattern_text, size_t preferred_rva,
                                   HookSiteValidator validator) {
    std::vector<PatternByte> pattern;
    if (!parse_pattern(pattern_text, pattern) || !range.base || range.size < pattern.size()) {
        render_log(std::string("pattern parse failed: ") + name);
        return nullptr;
    }

    unsigned char* first = nullptr;
    size_t count = 0;
    unsigned char* cursor = range.base;
    const unsigned char* end = range.base + range.size;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(cursor, &memory, sizeof(memory)) != sizeof(memory)) break;
        auto* region = static_cast<unsigned char*>(memory.BaseAddress);
        const size_t region_size = memory.RegionSize;
        unsigned char* region_end = region + region_size;
        if (memory.State == MEM_COMMIT && is_executable_page(memory.Protect)) {
            unsigned char* scan_begin = std::max(region, range.base);
            unsigned char* scan_end = std::min(region_end, const_cast<unsigned char*>(end));
            if (scan_end > scan_begin && static_cast<size_t>(scan_end - scan_begin) >= pattern.size()) {
                for (unsigned char* at = scan_begin; at + pattern.size() <= scan_end; ++at) {
                    if (!pattern_matches(at, static_cast<size_t>(scan_end - at), pattern) ||
                        (validator && !validator(at))) {
                        continue;
                    }
                    if (!first) first = at;
                    ++count;
                    if (count > 1) {
                        std::ostringstream out;
                        out << name << " pattern is not unique: first_rva=0x"
                            << std::hex << static_cast<uintptr_t>(first - range.base)
                            << " another_rva=0x" << static_cast<uintptr_t>(at - range.base);
                        render_log(out.str());
                        return nullptr;
                    }
                }
            }
        }
        cursor = region_end > cursor ? region_end : cursor + 0x1000;
    }

    if (first) {
        std::ostringstream out;
        const size_t found_rva = static_cast<size_t>(first - range.base);
        out << name << " validated pattern found rva=0x" << std::hex << found_rva;
        if (found_rva == preferred_rva) out << " (preferred)";
        render_log(out.str());
    } else {
        render_log(std::string(name) + " pattern not found");
    }
    return first;
}

unsigned char* find_hook_target_by_patterns(const char* name, size_t preferred_rva,
                                            const char* const* patterns,
                                            size_t pattern_count,
                                            HookSiteValidator validator) {
    const ModuleRange range = current_executable_range();
    if (!range.base || !range.size) {
        render_log(std::string(name) + " module range unavailable");
        return nullptr;
    }
    for (size_t i = 0; i < pattern_count; ++i) {
        if (auto* target = scan_unique_pattern(
                range, name, patterns[i], preferred_rva, validator)) {
            return target;
        }
    }
    return nullptr;
}

void reset_eye_capture_resources(const char* reason) {
    if (g_eye_textures[0] || g_eye_textures[1]) {
        glDeleteTextures(2, g_eye_textures);
        g_eye_textures[0] = 0;
        g_eye_textures[1] = 0;
    }
    if (g_afw_source_texture) {
        glDeleteTextures(1, &g_afw_source_texture);
        g_afw_source_texture = 0;
    }
    if (g_eye_depth_textures[0] || g_eye_depth_textures[1]) {
        glDeleteTextures(2, g_eye_depth_textures);
        g_eye_depth_textures[0] = g_eye_depth_textures[1] = 0;
    }
    g_eye_valid[0] = false;
    g_eye_valid[1] = false;
    g_eye_depth_valid[0] = g_eye_depth_valid[1] = false;
    g_pre_ui_captured[0] = false;
    g_pre_ui_captured[1] = false;
    g_texture_width = 0;
    g_texture_height = 0;
    g_source_texture_width = 0;
    g_source_texture_height = 0;
    InterlockedExchange(&g_engine_render_pass, 0);
    InterlockedExchange(&g_engine_stereo_pair_ready, 0);
    InterlockedExchange(&g_render_eye, 0);
    AcquireSRWLockExclusive(&g_camera_lock);
    g_afw_camera_valid = false;
    ReleaseSRWLockExclusive(&g_camera_lock);
    if (g_shared) {
        g_shared->current_eye = 0;
        g_shared->capture_error = 0;
    }
    std::ostringstream out;
    out << "eye capture resources reset";
    if (reason && *reason) out << " reason=" << reason;
    render_log(out.str());
}

void emit8(unsigned char*& cursor, uint64_t value);
bool restore_camera_hook();
bool restore_interaction_hook();
bool restore_engine_render_hook();
bool restore_noesis_hooks();
bool restore_swap_hook();
bool restore_all_hooks(bool render_thread_cleanup);
bool capture_eye(int eye, const VrCameraControls& controls, bool include_vr_overlays = true);
bool capture_native_eye(int eye, const VrCameraControls& controls);
bool apply_cached_engine_eye(int eye, const VrCameraControls& controls);

class SuspendedProcessThreads {
public:
    SuspendedProcessThreads() {
        const DWORD process_id = GetCurrentProcessId();
        const DWORD current_thread = GetCurrentThreadId();
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return;
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        if (Thread32First(snapshot, &entry)) {
            do {
                if (entry.th32OwnerProcessID != process_id ||
                    entry.th32ThreadID == current_thread) continue;
                HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID);
                if (!thread) continue;
                if (SuspendThread(thread) == static_cast<DWORD>(-1)) {
                    CloseHandle(thread);
                    continue;
                }
                if (thread_count_ < threads_.size()) {
                    threads_[thread_count_++] = thread;
                } else {
                    ResumeThread(thread);
                    CloseHandle(thread);
                }
            } while (Thread32Next(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }

    ~SuspendedProcessThreads() {
        while (thread_count_ > 0) {
            HANDLE thread = threads_[--thread_count_];
            ResumeThread(thread);
            CloseHandle(thread);
        }
    }

    SuspendedProcessThreads(const SuspendedProcessThreads&) = delete;
    SuspendedProcessThreads& operator=(const SuspendedProcessThreads&) = delete;

private:
    std::array<HANDLE, 256> threads_{};
    size_t thread_count_ = 0;
};

bool read_controls(VrCameraControls& controls) {
    if (!g_shared) return false;
    constexpr int kControlReadAttempts = 64;
    constexpr ULONGLONG kControlFallbackMaxAgeMs = 500;
    for (int attempt = 0; attempt < kControlReadAttempts; ++attempt) {
        const LONG before = InterlockedCompareExchange(
            reinterpret_cast<volatile LONG*>(&g_shared->control_sequence), 0, 0);
        if ((before & 1) != 0) {
            YieldProcessor();
            continue;
        }
        MemoryBarrier();
        controls.enabled = g_shared->enabled;
        controls.stereo_enabled = g_shared->stereo_enabled;
        controls.swap_eyes = g_shared->swap_eyes;
        controls.shutdown_requested = g_shared->shutdown_requested;
        controls.unload_requested = g_shared->unload_requested;
        controls.install_sequence = g_shared->install_sequence;
        controls.recenter_sequence = g_shared->recenter_sequence;
        controls.non_vr_mode = g_shared->non_vr_mode;
        controls.ipd_meters = g_shared->ipd_meters;
        controls.stereo_separation = g_shared->stereo_separation;
        controls.world_scale = g_shared->world_scale;
        controls.translation_scale = g_shared->translation_scale;
        controls.translation_y_scale = g_shared->translation_y_scale;
        controls.sneak_active = g_shared->sneak_active;
        controls.invert_translation_xz = g_shared->invert_translation_xz;
        controls.hand_pointer_enabled = g_shared->hand_pointer_enabled;
        controls.hide_center_reticle = g_shared->hide_center_reticle;
        controls.ui_overlay_enabled = g_shared->ui_overlay_enabled;
        controls.shadows_disabled = g_shared->shadows_disabled;
        controls.particles_disabled = g_shared->particles_disabled;
        controls.distortion_disabled = g_shared->distortion_disabled;
        controls.ui_overlay_distance = g_shared->ui_overlay_distance;
        controls.ui_overlay_width = g_shared->ui_overlay_width;
        controls.ui_scale = g_shared->ui_scale;
        controls.ui_eye_offset = g_shared->ui_eye_offset;
        controls.ui_offset_y = g_shared->ui_offset_y;
        controls.ui_ubo_scaling_disabled = g_shared->ui_ubo_scaling_disabled;
        controls.menu_ignore_draw_threshold = g_shared->menu_ignore_draw_threshold;
        controls.hand_pointer_distance = g_shared->hand_pointer_distance;
        controls.turn_speed = g_shared->turn_speed;
        controls.vr_resolution_scale = g_shared->vr_resolution_scale;
        controls.first_person_hand_hidden = g_shared->first_person_hand_hidden;
        controls.reserved_render_option = g_shared->reserved_render_option;
        controls.reserved_render_value = g_shared->reserved_render_value;
        controls.hmd_culling_view_enabled = g_shared->hmd_culling_view_enabled;
        controls.hand_model_scale = g_shared->hand_model_scale;
        controls.hand_model_pitch_degrees = g_shared->hand_model_pitch_degrees;
        controls.hand_model_yaw_degrees = g_shared->hand_model_yaw_degrees;
        controls.hand_model_roll_degrees = g_shared->hand_model_roll_degrees;
        controls.hand_depth_tolerance = g_shared->hand_depth_tolerance;
        controls.held_item_offset_x = g_shared->held_item_offset_x;
        controls.held_item_offset_y = g_shared->held_item_offset_y;
        controls.held_item_offset_z = g_shared->held_item_offset_z;
        controls.held_item_scale = g_shared->held_item_scale;
        MemoryBarrier();
        const LONG after = InterlockedCompareExchange(
            reinterpret_cast<volatile LONG*>(&g_shared->control_sequence), 0, 0);
        if (before == after && (after & 1) == 0) {
            g_last_valid_controls = controls;
            g_last_valid_controls_valid = true;
            g_last_valid_controls_tick = GetTickCount64();
            return true;
        }
    }
    const ULONGLONG now = GetTickCount64();
    if (g_last_valid_controls_valid &&
        now - g_last_valid_controls_tick <= kControlFallbackMaxAgeMs) {
        controls = g_last_valid_controls;
        ++g_control_read_fallbacks;
        if (now - g_last_control_fallback_log_tick >= 500) {
            std::ostringstream out;
            out << "control snapshot fallback count=" << g_control_read_fallbacks
                << " ageMs=" << (now - g_last_valid_controls_tick);
            render_log(out.str());
            g_last_control_fallback_log_tick = now;
        }
        return true;
    }
    return false;
}

bool ensure_ui_scale_mapping() {
    if (g_ui_scale_shared) return true;
    g_ui_scale_mapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(UiScaleShared),
        hytalevr::kUiScaleMappingNameW);
    if (!g_ui_scale_mapping) return false;
    g_ui_scale_shared = static_cast<UiScaleShared*>(
        MapViewOfFile(g_ui_scale_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(UiScaleShared)));
    if (!g_ui_scale_shared) {
        CloseHandle(g_ui_scale_mapping);
        g_ui_scale_mapping = nullptr;
        return false;
    }
    if (!hytalevr::ui_scale_shared_data_compatible(*g_ui_scale_shared)) {
        hytalevr::initialize_ui_scale_shared_data(*g_ui_scale_shared);
    }
    return true;
}

void publish_held_item_poses(const VrCameraControls& controls) {
    if (!g_ui_scale_shared) return;

    const bool enabled = controls.enabled && controls.stereo_enabled &&
                         controls.first_person_hand_hidden;
    const float world_scale = validated_world_scale(controls);
    float hmd[12]{};
    float controllers[2][12]{};
    bool hmd_valid = false;
    bool controller_valid[2]{};
    if (enabled) {
        AcquireSRWLockShared(&g_pose_lock);
        hmd_valid = g_hmd_current_valid;
        if (hmd_valid) {
            std::copy(std::begin(g_hmd_current_pose), std::end(g_hmd_current_pose),
                      std::begin(hmd));
            controller_valid[0] = g_left_controller_valid;
            controller_valid[1] = g_right_controller_valid;
            if (controller_valid[0]) {
                std::copy(std::begin(g_left_controller_pose),
                          std::end(g_left_controller_pose),
                          std::begin(controllers[0]));
            }
            if (controller_valid[1]) {
                std::copy(std::begin(g_right_controller_pose),
                          std::end(g_right_controller_pose),
                          std::begin(controllers[1]));
            }
        }
        ReleaseSRWLockShared(&g_pose_lock);
    }

    float relative_poses[2][12]{};
    LONG valid_mask = 0;
    if (enabled && hmd_valid) {
        for (int side = 0; side < 2; ++side) {
            if (controller_valid[side] &&
                hytalevr::controller_pose_relative_to_hmd(
                    hmd, controllers[side], world_scale, relative_poses[side])) {
                valid_mask |= 1L << side;
            }
        }
    }

    LONG sequence = InterlockedIncrement(&g_ui_scale_shared->heldItemPoseSequence);
    if ((sequence & 1) == 0) {
        InterlockedIncrement(&g_ui_scale_shared->heldItemPoseSequence);
    }
    g_ui_scale_shared->heldItemReanchorEnabled = enabled ? 1 : 0;
    g_ui_scale_shared->heldItemWorldScale = world_scale;
    g_ui_scale_shared->heldItemLocalOffset[0] =
        std::clamp(controls.held_item_offset_x, -0.30f, 0.30f);
    g_ui_scale_shared->heldItemLocalOffset[1] =
        std::clamp(controls.held_item_offset_y, -0.30f, 0.30f);
    g_ui_scale_shared->heldItemLocalOffset[2] =
        std::clamp(controls.held_item_offset_z, -0.30f, 0.30f);
    g_ui_scale_shared->heldItemVisualScale =
        std::clamp(controls.held_item_scale, 0.10f, 2.0f);
    std::memcpy(g_ui_scale_shared->heldItemControllerPoses, relative_poses,
                sizeof(relative_poses));
    InterlockedExchange(&g_ui_scale_shared->heldItemPoseValidMask, valid_mask);
    MemoryBarrier();
    InterlockedIncrement(&g_ui_scale_shared->heldItemPoseSequence);
}

void publish_ui_scale_for_eye(const VrCameraControls& controls, vr::EVREye eye) {
    if (!ensure_ui_scale_mapping()) return;
    const bool active = controls.enabled && controls.stereo_enabled &&
        controls.ui_overlay_enabled;
    const bool menu_visible = active && menu_visible_recently();
    const float hud_scale = std::clamp(controls.ui_scale, 0.10f, 2.0f);
    const float scale = menu_visible ? 1.0f : hud_scale;
    const float centered_x = active ? (1.0f - scale) : 0.0f;
    const float centered_y = active ? (scale - 1.0f) : 0.0f;
    const float ui_offset_x = menu_visible
        ? 0.0f
        : std::clamp(controls.ui_eye_offset, -0.25f, 0.25f);
    g_ui_scale_shared->uiScale = active ? scale : 1.0f;
    g_ui_scale_shared->offsetX =
        active ? centered_x + ui_offset_x : 0.0f;
    g_ui_scale_shared->offsetY =
        active ? centered_y + std::clamp(controls.ui_offset_y, -0.75f, 0.75f) : 0.0f;
    g_ui_scale_shared->disableUboScaling = controls.ui_ubo_scaling_disabled ? 1 : 0;
    g_ui_scale_shared->disableShadows = controls.shadows_disabled ? 1 : 0;
    g_ui_scale_shared->disableParticles = controls.particles_disabled ? 1 : 0;
    g_ui_scale_shared->disableDistortion = static_cast<int>(controls.distortion_disabled);
    g_ui_scale_shared->currentEye = eye == vr::Eye_Left ? 0 : 1;
    g_ui_scale_shared->suppressMenuInGame = menu_visible ? 1 : 0;
    g_ui_scale_shared->menuIgnoreDrawThreshold =
        static_cast<LONG>(std::clamp(controls.menu_ignore_draw_threshold, 0u, 20000u));
    publish_held_item_poses(controls);

    // Keep Hytale's camera-anchored first-person model hidden. The UI hook lets
    // held-item draws through only after their model matrix was controller-anchored.
    g_ui_scale_shared->firstPersonControllerReanchor = 0;
    g_ui_scale_shared->firstPersonControllerPoseValid = 0;
    g_ui_scale_shared->firstPersonHandNdcX = 0.0f;
    g_ui_scale_shared->firstPersonHandNdcY = 0.0f;
    g_ui_scale_shared->firstPersonHandDepth = 0.0f;
    g_ui_scale_shared->hideFirstPerson = controls.first_person_hand_hidden ? 1 : 0;

    if (g_shared) {
        g_shared->ui_overlay_error = static_cast<int32_t>(
            InterlockedCompareExchange(&g_ui_scale_shared->menuCaptureError, 0, 0));
    }
}

void publish_ui_scale_neutral() {
    if (!ensure_ui_scale_mapping()) return;
    g_ui_scale_shared->uiScale = 1.0f;
    g_ui_scale_shared->offsetX = 0.0f;
    g_ui_scale_shared->offsetY = 0.0f;
    g_ui_scale_shared->disableUboScaling = 0;
    g_ui_scale_shared->disableShadows = 0;
    g_ui_scale_shared->disableParticles = 0;
    g_ui_scale_shared->disableDistortion = 0;
    g_ui_scale_shared->distortionTextureId = 0;
    g_ui_scale_shared->distortionTextureWidth = 0;
    g_ui_scale_shared->distortionTextureHeight = 0;
    InterlockedExchange(&g_ui_scale_shared->vrSceneMatricesValid, 0);
    g_ui_scale_shared->hideFirstPerson = 0;
    g_ui_scale_shared->suppressMenuInGame = 0;
    g_ui_scale_shared->menuIgnoreDrawThreshold = 1;
    g_ui_scale_shared->firstPersonControllerReanchor = 0;
    g_ui_scale_shared->firstPersonControllerPoseValid = 0;
    g_ui_scale_shared->firstPersonHandNdcX = 0.0f;
    g_ui_scale_shared->firstPersonHandNdcY = 0.0f;
    g_ui_scale_shared->firstPersonHandDepth = 0.0f;
    publish_held_item_poses(VrCameraControls{});
}

void publish_render_filters_only(const VrCameraControls& controls) {
    if (!ensure_ui_scale_mapping()) return;
    g_ui_scale_shared->uiScale = 1.0f;
    g_ui_scale_shared->offsetX = 0.0f;
    g_ui_scale_shared->offsetY = 0.0f;
    g_ui_scale_shared->disableUboScaling = 0;
    g_ui_scale_shared->disableShadows = controls.shadows_disabled ? 1 : 0;
    g_ui_scale_shared->disableParticles = controls.particles_disabled ? 1 : 0;
    g_ui_scale_shared->disableDistortion = 0;
    g_ui_scale_shared->distortionTextureId = 0;
    g_ui_scale_shared->distortionTextureWidth = 0;
    g_ui_scale_shared->distortionTextureHeight = 0;
    InterlockedExchange(&g_ui_scale_shared->vrSceneMatricesValid, 0);
    g_ui_scale_shared->hideFirstPerson = 0;
    g_ui_scale_shared->suppressMenuInGame = 0;
    g_ui_scale_shared->menuIgnoreDrawThreshold =
        static_cast<LONG>(std::clamp(controls.menu_ignore_draw_threshold, 0u, 20000u));
    g_ui_scale_shared->firstPersonControllerReanchor = 0;
    g_ui_scale_shared->firstPersonControllerPoseValid = 0;
    g_ui_scale_shared->firstPersonHandNdcX = 0.0f;
    g_ui_scale_shared->firstPersonHandNdcY = 0.0f;
    g_ui_scale_shared->firstPersonHandDepth = 0.0f;
    publish_held_item_poses(VrCameraControls{});
}

vr::EVREye current_camera_eye(const VrCameraControls& controls) {
    const bool left = (InterlockedCompareExchange(&g_render_eye, 0, 0) == 0) ^
                      (controls.swap_eyes != 0);
    return left ? vr::Eye_Left : vr::Eye_Right;
}

Matrix4 from_openvr(const vr::HmdMatrix44_t& source) {
    Matrix4 result{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            result.value[column * 4 + row] = source.m[row][column];
        }
    }
    return result;
}

Matrix4 inverse_eye_to_head(const vr::HmdMatrix34_t& source, float separation) {
    Matrix4 result{};
    result.value[15] = 1.0f;

    // Eye-to-head is a rigid transform. Its inverse is R^T and -R^T*t.
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            result.value[column * 4 + row] = source.m[column][row];
        }
    }
    for (int row = 0; row < 3; ++row) {
        float translated = 0.0f;
        for (int k = 0; k < 3; ++k) {
            translated -= source.m[k][row] * source.m[k][3] * separation;
        }
        result.value[12 + row] = translated;
    }
    return result;
}

void projection_planes(const Matrix4& projection, float& near_z, float& far_z) {
    const float a = projection.value[10];
    const float b = projection.value[14];
    near_z = b / (a - 1.0f);
    far_z = b / (a + 1.0f);
    if (!std::isfinite(near_z) || near_z <= 0.001f) near_z = 0.05f;
    if (!std::isfinite(far_z) || far_z <= near_z) far_z = 10000.0f;
    near_z = std::clamp(near_z, 0.01f, 10.0f);
    far_z = std::clamp(far_z, near_z + 10.0f, 100000.0f);
}

Matrix4 combined_stereo_source_projection(const Matrix4& left,
                                          const Matrix4& right) {
    const auto horizontal_bounds = [](const Matrix4& projection, float& low, float& high) {
        const float scale = projection.value[0];
        const float offset = projection.value[8];
        low = (-1.0f + offset) / scale;
        high = (1.0f + offset) / scale;
        if (low > high) std::swap(low, high);
    };
    const auto vertical_bounds = [](const Matrix4& projection, float& low, float& high) {
        const float scale = projection.value[5];
        const float offset = projection.value[9];
        low = (-1.0f + offset) / scale;
        high = (1.0f + offset) / scale;
        if (low > high) std::swap(low, high);
    };

    float left_low = 0.0f;
    float left_high = 0.0f;
    float right_low = 0.0f;
    float right_high = 0.0f;
    horizontal_bounds(left, left_low, left_high);
    horizontal_bounds(right, right_low, right_high);
    float horizontal_low = (std::min)(left_low, right_low);
    float horizontal_high = (std::max)(left_high, right_high);
    const float horizontal_margin = (horizontal_high - horizontal_low) * 0.08f;
    horizontal_low -= horizontal_margin;
    horizontal_high += horizontal_margin;

    float left_bottom = 0.0f;
    float left_top = 0.0f;
    float right_bottom = 0.0f;
    float right_top = 0.0f;
    vertical_bounds(left, left_bottom, left_top);
    vertical_bounds(right, right_bottom, right_top);
    float vertical_low = (std::min)(left_bottom, right_bottom);
    float vertical_high = (std::max)(left_top, right_top);
    const float vertical_margin = (vertical_high - vertical_low) * 0.02f;
    vertical_low -= vertical_margin;
    vertical_high += vertical_margin;

    Matrix4 result = left;
    result.value[0] = 2.0f / (horizontal_high - horizontal_low);
    result.value[8] =
        (horizontal_high + horizontal_low) / (horizontal_high - horizontal_low);
    result.value[5] = 2.0f / (vertical_high - vertical_low);
    result.value[9] =
        (vertical_high + vertical_low) / (vertical_high - vertical_low);
    return result;
}

bool active_turn_input() {
    if (!g_shared) return false;
    return std::fabs(g_shared->controller_turn_x) > 0.08f ||
           std::fabs(g_shared->controller_turn_y) > 0.08f;
}

bool native_head_sync_input() {
    return g_shared && g_shared->native_head_sync_active != 0;
}

float yaw_from_view(const Matrix4& view) {
    float position[3]{};
    float forward[3]{};
    hytalevr::view_pose(view, position, forward);
    forward[1] = 0.0f;
    const float length = std::sqrt(forward[0] * forward[0] +
                                   forward[2] * forward[2]);
    if (length <= 0.0001f) return 0.0f;
    forward[0] /= length;
    forward[2] /= length;
    return std::atan2(forward[0], -forward[2]);
}

float normalize_angle(float radians) {
    constexpr float pi = 3.14159265358979323846f;
    while (radians > pi) radians -= 2.0f * pi;
    while (radians < -pi) radians += 2.0f * pi;
    return radians;
}

Matrix4 horizontal_view_with_yaw(const Matrix4& position_source, float yaw) {
    float position[3]{};
    float existing_forward[3]{};
    hytalevr::view_pose(position_source, position, existing_forward);
    const float forward[3]{std::sin(yaw), 0.0f, -std::cos(yaw)};
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
            -(result.value[0 * 4 + row] * position[0] +
              result.value[1 * 4 + row] * position[1] +
              result.value[2 * 4 + row] * position[2]);
    }
    return result;
}

Matrix4 aggressive_game_view(const Matrix4& native_view,
                             const VrCameraControls& controls,
                             float& native_delta,
                             bool& snap_suppressed) {
    Matrix4 horizontal_native = hytalevr::horizontal_view(native_view);
    const float native_yaw = yaw_from_view(horizontal_native);
    snap_suppressed = false;
    if (!g_aggressive_native_view_valid ||
        controls.recenter_sequence != g_aggressive_recenter_sequence) {
        g_aggressive_native_view = horizontal_native;
        g_aggressive_native_view_valid = true;
        g_last_observed_native_yaw = native_yaw;
        g_last_observed_native_yaw_valid = true;
        g_aggressive_recenter_sequence = controls.recenter_sequence;
        native_delta = 0.0f;
        return g_aggressive_native_view;
    }

    native_delta = matrix_max_delta(g_aggressive_native_view, horizontal_native);
    constexpr float kAcceptSmallNativeDrift = 0.0060f;
    if (active_turn_input()) {
        const float body_yaw = yaw_from_view(g_aggressive_native_view);
        const float native_yaw_delta = g_last_observed_native_yaw_valid
            ? normalize_angle(native_yaw - g_last_observed_native_yaw)
            : 0.0f;
        g_aggressive_native_view = horizontal_view_with_yaw(
            horizontal_native, normalize_angle(body_yaw + native_yaw_delta));
        g_last_observed_native_yaw = native_yaw;
        g_last_observed_native_yaw_valid = true;
        return g_aggressive_native_view;
    }

    if (native_delta <= kAcceptSmallNativeDrift && !native_head_sync_input()) {
        g_aggressive_native_view = horizontal_native;
        g_last_observed_native_yaw = native_yaw;
        g_last_observed_native_yaw_valid = true;
        return g_aggressive_native_view;
    }

    // Keep native position/culling responsive while refusing sudden orientation
    // changes from Hytale's own camera controller.
    const float held_yaw = yaw_from_view(g_aggressive_native_view);
    g_aggressive_native_view = horizontal_view_with_yaw(horizontal_native, held_yaw);
    g_last_observed_native_yaw = native_yaw;
    g_last_observed_native_yaw_valid = true;
    snap_suppressed = true;
    ++g_aggressive_suppressed_snaps;
    return g_aggressive_native_view;
}

float validated_world_scale(const VrCameraControls& controls) {
    const float value = std::isfinite(controls.world_scale) && controls.world_scale > 0.0f
        ? controls.world_scale
        : 1.30f;
    return std::clamp(value, 0.50f, 2.00f);
}

bool update_floor_height_alignment(const Matrix4& native_view,
                                    const VrCameraControls& controls,
                                    bool hmd_origin_valid,
                                    uint32_t hmd_recenter_sequence,
                                    float hmd_standing_height) {
    constexpr float kHytaleStandingEyeHeightMeters = 66.2f / 32.0f;
    const float world_scale = validated_world_scale(controls);
    if (g_floor_height_alignment_sequence == controls.recenter_sequence &&
        std::fabs(g_floor_height_world_scale - world_scale) <= 0.0001f) {
        return true;
    }
    if (!hmd_origin_valid || hmd_recenter_sequence != controls.recenter_sequence ||
        controls.sneak_active != 0) {
        return false;
    }

    float camera_position[3]{};
    float camera_forward[3]{};
    hytalevr::view_pose(native_view, camera_position, camera_forward);
    if (!std::isfinite(hmd_standing_height) || !std::isfinite(camera_position[1]) ||
        hmd_standing_height < 0.50f || hmd_standing_height > 2.60f) {
        return false;
    }

    // OpenVR Standing gives the real headset height above the physical floor.
    // The base Player.blockymodel eye attachment is 66.2 model units high and
    // Hytale uses 32 model units per world meter. Their difference raises or
    // lowers the camera once without absorbing later room-scale movement.
    g_floor_height_offset = std::clamp(
        hmd_standing_height * world_scale - kHytaleStandingEyeHeightMeters,
        -4.00f, 4.00f);
    g_floor_height_world_scale = world_scale;
    g_native_standing_camera_y = camera_position[1];
    g_native_standing_camera_y_valid = true;
    g_floor_height_alignment_sequence = controls.recenter_sequence;

    std::ostringstream out;
    out << "floor height aligned: physical=" << std::fixed << std::setprecision(3)
        << hmd_standing_height << " hytaleEye=" << kHytaleStandingEyeHeightMeters
        << " worldScale=" << world_scale
        << " offset=" << g_floor_height_offset
        << " nativeY=" << g_native_standing_camera_y
        << " seq=" << controls.recenter_sequence;
    render_log(out.str());
    return true;
}

void track_native_standing_camera_height(const Matrix4& native_view,
                                         const VrCameraControls& controls) {
    if (controls.sneak_active != 0 ||
        g_floor_height_alignment_sequence != controls.recenter_sequence) {
        return;
    }

    float camera_position[3]{};
    float camera_forward[3]{};
    hytalevr::view_pose(native_view, camera_position, camera_forward);
    if (!std::isfinite(camera_position[1])) return;

    // Native camera Y follows the player's world elevation. Refresh the
    // standing reference only while not sneaking, then hold it during crouch.
    g_native_standing_camera_y = camera_position[1];
    g_native_standing_camera_y_valid = true;
}

Matrix4 compensate_native_sneak_height(const Matrix4& native_view,
                                       const VrCameraControls& controls) {
    g_last_sneak_height_compensation = 0.0f;
    if (controls.sneak_active == 0 || !g_native_standing_camera_y_valid ||
        g_floor_height_alignment_sequence != controls.recenter_sequence) {
        return native_view;
    }

    float camera_position[3]{};
    float camera_forward[3]{};
    hytalevr::view_pose(native_view, camera_position, camera_forward);
    if (!std::isfinite(camera_position[1])) return native_view;

    const float compensation = std::clamp(
        g_native_standing_camera_y - camera_position[1], 0.0f, 1.50f);
    if (compensation <= 0.0001f) return native_view;
    g_last_sneak_height_compensation = compensation;
    return hytalevr::apply_camera_height_offset(native_view, compensation);
}

void publish_vr_scene_matrices(const Matrix4& view,
                               const Matrix4& projection,
                               const Matrix4& view_projection) {
    if (!ensure_ui_scale_mapping() || !g_ui_scale_shared) return;

    Matrix4 inverse_view{};
    Matrix4 inverse_view_projection{};
    if (!hytalevr::inverse(view, inverse_view) ||
        !hytalevr::inverse(view_projection, inverse_view_projection)) {
        InterlockedExchange(&g_ui_scale_shared->vrSceneMatricesValid, 0);
        return;
    }

    const bool previous_is_usable =
        g_previous_vr_scene_view_projection_valid &&
        matrix_max_delta(g_previous_vr_scene_view_projection, view_projection) < 0.75f;
    const Matrix4 previous_view_projection = previous_is_usable
        ? g_previous_vr_scene_view_projection
        : view_projection;
    const Matrix4 reprojection = multiply(previous_view_projection, inverse_view);

    float projection_info[4]{};
    if (std::fabs(projection.value[0]) > 0.0001f &&
        std::fabs(projection.value[5]) > 0.0001f) {
        projection_info[0] = -2.0f / projection.value[0];
        projection_info[1] = -2.0f / projection.value[5];
        projection_info[2] = (1.0f - projection.value[2]) / projection.value[0];
        projection_info[3] = (1.0f + projection.value[6]) / projection.value[5];
    }

    // Odd sequence values mean that a writer is active. The OpenGL hook only
    // consumes a snapshot when it observes the same even value twice.
    LONG sequence = InterlockedIncrement(&g_ui_scale_shared->vrSceneMatrixSequence);
    if ((sequence & 1) == 0) {
        InterlockedIncrement(&g_ui_scale_shared->vrSceneMatrixSequence);
    }
    std::memcpy(g_ui_scale_shared->vrSceneView, view.value, sizeof(view.value));
    std::memcpy(g_ui_scale_shared->vrSceneProjection, projection.value,
                sizeof(projection.value));
    std::memcpy(g_ui_scale_shared->vrSceneViewProjection, view_projection.value,
                sizeof(view_projection.value));
    std::memcpy(g_ui_scale_shared->vrSceneInvView, inverse_view.value,
                sizeof(inverse_view.value));
    std::memcpy(g_ui_scale_shared->vrSceneInvViewProjection,
                inverse_view_projection.value, sizeof(inverse_view_projection.value));
    std::memcpy(g_ui_scale_shared->vrSceneReprojection, reprojection.value,
                sizeof(reprojection.value));
    std::memcpy(g_ui_scale_shared->vrSceneProjectionInfo, projection_info,
                sizeof(projection_info));
    MemoryBarrier();
    InterlockedExchange(&g_ui_scale_shared->vrSceneMatricesValid, 1);
    InterlockedIncrement(&g_ui_scale_shared->vrSceneMatrixSequence);

    g_previous_vr_scene_view_projection = view_projection;
    g_previous_vr_scene_view_projection_valid = true;
}

bool apply_cached_engine_eye(int eye, const VrCameraControls& controls) {
    if (eye < 0 || eye > 1) return false;

    void* camera_object = nullptr;
    Matrix4 view{};
    Matrix4 projection{};
    bool valid = false;
    AcquireSRWLockShared(&g_camera_lock);
    camera_object = g_primary_camera;
    valid = g_afw_camera_valid && camera_object != nullptr;
    if (valid) {
        view = g_afw_eye_view[eye];
        projection = g_afw_eye_projection[eye];
    }
    ReleaseSRWLockShared(&g_camera_lock);
    if (!valid) return false;

    auto* camera = static_cast<unsigned char*>(camera_object);
    Matrix4 observed_view{};
    Matrix4 observed_projection{};
    if (!read_validated_camera_layout(
            camera, observed_view, observed_projection)) {
        return false;
    }

    const Matrix4 view_projection = multiply(projection, view);
    std::memcpy(
        camera + kCameraViewOffset, &view, sizeof(view));
    std::memcpy(
        camera + kCameraViewProjectionOffset, &view_projection,
        sizeof(view_projection));
    std::memcpy(
        camera + kCameraProjectionOffset, &projection, sizeof(projection));
    publish_vr_scene_matrices(view, projection, view_projection);
    publish_ui_scale_for_eye(
        controls, eye == 0 ? vr::Eye_Left : vr::Eye_Right);

    AcquireSRWLockExclusive(&g_camera_lock);
    g_last_written_culling_view = view;
    g_last_written_culling_object = camera_object;
    g_last_written_culling_tick = GetTickCount64();
    g_last_written_culling_view_valid = true;
    ReleaseSRWLockExclusive(&g_camera_lock);
    if (g_shared) g_shared->current_eye = static_cast<uint32_t>(eye);
    return true;
}

void apply_vr_camera(void* object) {
    HookCallbackScope callback_scope;
    if (g_shared) {
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->camera_hook_entries));
    }
    VrCameraControls controls{};
    if (!object || !read_controls(controls) || !controls.enabled) return;
    const float world_scale = validated_world_scale(controls);
    if (controls.non_vr_mode) {
        publish_render_filters_only(controls);
        return;
    }

    auto* camera = static_cast<unsigned char*>(object);
    Matrix4 raw_view{};
    Matrix4 projection{};
    if (!read_validated_camera_layout(camera, raw_view, projection)) {
        const ULONGLONG now = GetTickCount64();
        if (now - g_last_camera_layout_error_tick >= 1000) {
            render_log(
                "camera hook rejected an object with an incompatible "
                "or implausible matrix layout");
            g_last_camera_layout_error_tick = now;
        }
        return;
    }

    // Latch the primary gameplay camera while it still has a perspective
    // projection. Older builds used a very specific FOV value here, but that
    // makes the VR path disappear as soon as Hytale changes FOV/resolution.
    // Orthographic shadow/effect cameras keep projection[15] at 1, while the
    // gameplay perspective camera has the usual OpenGL-ish [11] ~= -1 and
    // [15] ~= 0 profile.
    const bool primary_projection_profile =
        std::isfinite(projection.value[0]) &&
        std::isfinite(projection.value[5]) &&
        projection.value[0] > 0.2f && projection.value[0] < 5.0f &&
        projection.value[5] > 0.2f && projection.value[5] < 5.0f &&
        projection.value[10] < 0.0f &&
        projection.value[11] <= -0.9f && projection.value[11] >= -1.1f &&
        std::fabs(projection.value[15]) <= 0.001f;
    const ULONGLONG now = GetTickCount64();
    if (primary_projection_profile &&
        (!g_primary_camera || now - g_last_primary_camera_tick > 1500)) {
        if (g_primary_camera != object) {
            AcquireSRWLockExclusive(&g_camera_lock);
            g_last_native_camera_view_valid = false;
            g_last_written_culling_view_valid = false;
            g_last_native_camera_object = nullptr;
            g_last_written_culling_object = nullptr;
            ReleaseSRWLockExclusive(&g_camera_lock);
        }
        g_primary_camera = object;
    }
    if (object != g_primary_camera) return;
    g_last_primary_camera_tick = now;
    g_shared->primary_camera = reinterpret_cast<uint64_t>(object);
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->camera_calls));
    publish_ui_scale_for_eye(controls, current_camera_eye(controls));

    Matrix4 view = raw_view;
    bool reused_cached_native_view = false;
    constexpr ULONGLONG kNativeViewCacheMaxAgeMs = 500;
    constexpr float kSelfCullingViewDelta = 0.0005f;
    Matrix4 cached_native_view = identity_matrix();
    bool raw_matches_cached_culling_view = false;
    bool cached_native_view_available = false;
    AcquireSRWLockShared(&g_camera_lock);
    raw_matches_cached_culling_view =
        controls.hmd_culling_view_enabled &&
        g_last_written_culling_view_valid &&
        g_last_written_culling_object == object &&
        now - g_last_written_culling_tick <= kNativeViewCacheMaxAgeMs &&
        matrix_max_delta(raw_view, g_last_written_culling_view) <= kSelfCullingViewDelta;
    if (g_last_native_camera_view_valid &&
        g_last_native_camera_object == object &&
        now - g_last_native_camera_tick <= kNativeViewCacheMaxAgeMs) {
        cached_native_view = g_last_native_camera_view;
        cached_native_view_available = true;
    }
    ReleaseSRWLockShared(&g_camera_lock);
    if (raw_matches_cached_culling_view && cached_native_view_available) {
        view = cached_native_view;
        reused_cached_native_view = true;
    } else {
        AcquireSRWLockExclusive(&g_camera_lock);
        g_last_native_camera_view = raw_view;
        g_last_native_camera_object = object;
        g_last_native_camera_tick = now;
        g_last_native_camera_view_valid = true;
        ReleaseSRWLockExclusive(&g_camera_lock);
    }

    Matrix4 delta{};
    bool hmd_origin_valid = false;
    uint32_t hmd_recenter_sequence = 0;
    float hmd_standing_height = 0.0f;
    AcquireSRWLockShared(&g_pose_lock);
    delta = g_hmd_view_delta;
    hmd_origin_valid = g_hmd_origin_valid;
    hmd_recenter_sequence = g_recenter_sequence;
    hmd_standing_height = g_hmd_origin_pose[7];
    ReleaseSRWLockShared(&g_pose_lock);
    float aggressive_native_delta = 0.0f;
    bool aggressive_snap_suppressed = false;
    const Matrix4 leveled_view =
        aggressive_game_view(view, controls, aggressive_native_delta,
                             aggressive_snap_suppressed);
    update_floor_height_alignment(leveled_view, controls, hmd_origin_valid,
                                  hmd_recenter_sequence, hmd_standing_height);
    track_native_standing_camera_height(leveled_view, controls);
    const Matrix4 sneak_stabilized_view =
        compensate_native_sneak_height(leveled_view, controls);
    const Matrix4 floor_aligned_view =
        hytalevr::apply_camera_height_offset(sneak_stabilized_view,
                                             g_floor_height_offset);
    g_last_aggressive_native_delta = aggressive_native_delta;
    if (g_shared) {
        g_shared->native_camera_yaw = yaw_from_view(view);
        g_shared->body_camera_yaw = yaw_from_view(leveled_view);
        g_shared->camera_yaw_valid = 1;
    }

    // Hytale contributes position and yaw only. Headset pitch and roll remain
    // absolute in SteamVR's Standing space, so the physical floor stays level.
    const Matrix4 center_view = multiply(delta, floor_aligned_view);
    const Matrix4 culling_view =
        controls.hmd_culling_view_enabled
            ? hytalevr::roll_free_view(center_view)
            : floor_aligned_view;
    const Matrix4 neutral_view_projection = multiply(projection, floor_aligned_view);
    // The culling view already follows headset yaw and pitch. Keep only a
    // modest reserve for stereo edges and pose latency instead of asking
    // Hytale to prepare several screens of invisible geometry every frame.
    constexpr float kCullingHorizontalGuardBand = 1.50f;
    constexpr float kCullingVerticalGuardBand = 1.65f;
    const Matrix4 culling_projection =
        hytalevr::expand_perspective_frustum(
            projection, kCullingHorizontalGuardBand,
            kCullingVerticalGuardBand);
    const Matrix4 culling_view_projection = multiply(culling_projection, culling_view);
    AcquireSRWLockExclusive(&g_camera_lock);
    g_neutral_view = floor_aligned_view;
    g_neutral_view_valid = true;
    g_culling_view = culling_view;
    g_culling_view_valid = controls.hmd_culling_view_enabled != 0;
    g_center_view_projection = neutral_view_projection;
    g_center_view_projection_valid = true;
    // Keep +0x4E0 on Hytale's original projection between frames. Only the
    // cached culling view-projection receives the guard band; otherwise the
    // hook would read back and expand its own projection again next frame.
    g_culling_projection = projection;
    g_culling_projection_valid = true;
    g_culling_view_projection = culling_view_projection;
    g_culling_view_projection_valid = true;
    if (controls.hmd_culling_view_enabled) {
        g_last_written_culling_view = culling_view;
        g_last_written_culling_object = object;
        g_last_written_culling_tick = now;
        g_last_written_culling_view_valid = true;
    } else {
        g_last_written_culling_view_valid = false;
    }
    ReleaseSRWLockExclusive(&g_camera_lock);

    const bool engine_native_stereo =
        g_engine_render_hook_active && controls.stereo_enabled && g_vr_system;
    Matrix4 eye_view_matrix = center_view;
    if (controls.stereo_enabled && g_vr_system) {
        const auto left_eye = g_vr_system->GetEyeToHeadTransform(vr::Eye_Left);
        const auto right_eye = g_vr_system->GetEyeToHeadTransform(vr::Eye_Right);
        const float dx = right_eye.m[0][3] - left_eye.m[0][3];
        const float dy = right_eye.m[1][3] - left_eye.m[1][3];
        const float dz = right_eye.m[2][3] - left_eye.m[2][3];
        const float runtime_ipd = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float requested_ipd = std::clamp(controls.ipd_meters, 0.04f, 0.08f);
        const float ipd_scale = runtime_ipd > 0.001f ? requested_ipd / runtime_ipd : 1.0f;
        const float separation = std::clamp(controls.stereo_separation, 0.0f, 2.0f) *
                                 std::clamp(ipd_scale, 0.5f, 1.5f) * world_scale;
        float near_z = 0.05f;
        float far_z = 10000.0f;
        projection_planes(projection, near_z, far_z);
        const Matrix4 left_eye_view = inverse_eye_to_head(left_eye, separation);
        const Matrix4 right_eye_view = inverse_eye_to_head(right_eye, separation);
        const Matrix4 left_view = multiply(left_eye_view, center_view);
        const Matrix4 right_view = multiply(right_eye_view, center_view);
        const Matrix4 left_projection = from_openvr(
            g_vr_system->GetProjectionMatrix(vr::Eye_Left, near_z, far_z));
        const Matrix4 right_projection = from_openvr(
            g_vr_system->GetProjectionMatrix(vr::Eye_Right, near_z, far_z));

        if (engine_native_stereo) {
            const int render_eye = InterlockedCompareExchange(&g_render_eye, 0, 0) == 0
                ? 0
                : 1;
            eye_view_matrix = render_eye == 0 ? left_view : right_view;
            projection = render_eye == 0 ? left_projection : right_projection;
        } else {
            // AFW renders one centered camera with enough horizontal coverage
            // for both eyes, then reconstructs the headset views from it.
            eye_view_matrix = center_view;
            projection =
                combined_stereo_source_projection(left_projection, right_projection);
        }
        AcquireSRWLockExclusive(&g_camera_lock);
        g_afw_source_view = eye_view_matrix;
        g_afw_source_projection = projection;
        g_afw_eye_view[0] = left_view;
        g_afw_eye_projection[0] = left_projection;
        g_afw_eye_view[1] = right_view;
        g_afw_eye_projection[1] = right_projection;
        g_afw_source_to_eye_view[0] = left_eye_view;
        g_afw_source_to_eye_view[1] = right_eye_view;
        g_afw_camera_valid = true;
        ReleaseSRWLockExclusive(&g_camera_lock);
    }
    const Matrix4 view_projection = multiply(projection, eye_view_matrix);
    publish_vr_scene_matrices(eye_view_matrix, projection, view_projection);
    // Keep the final VR transform in +0x320. When HMD culling is enabled,
    // +0x2E0 follows headset yaw and pitch without roll so world
    // loading/culling also works above and below the player.
    if (engine_native_stereo) {
        std::memcpy(
            camera + kCameraViewOffset, &eye_view_matrix,
            sizeof(eye_view_matrix));
        std::memcpy(
            camera + kCameraProjectionOffset, &projection,
            sizeof(projection));
    } else if (controls.hmd_culling_view_enabled) {
        std::memcpy(
            camera + kCameraViewOffset, &culling_view,
            sizeof(culling_view));
    }
    std::memcpy(
        camera + kCameraViewProjectionOffset, &view_projection,
        sizeof(view_projection));
    const float native_view_delta =
        g_last_diag_valid ? matrix_max_delta(g_last_diag_view, view) : 0.0f;
    const float final_vp_delta =
        g_last_diag_valid ? matrix_max_delta(g_last_diag_view_projection, view_projection) : 0.0f;
    const ULONGLONG camera_log_now = GetTickCount64();
    if (render_logging_enabled() &&
        (!g_last_diag_valid || camera_log_now - g_last_camera_diag_tick >= 1000 ||
         native_view_delta > 0.0025f || final_vp_delta > 0.05f)) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(5)
            << "camera eye=" << static_cast<int>(current_camera_eye(controls))
            << " primary=0x" << std::hex << reinterpret_cast<uintptr_t>(object)
            << std::dec
            << " calls=" << (g_shared ? g_shared->camera_calls : 0)
            << " nativeDelta=" << native_view_delta
            << " finalDelta=" << final_vp_delta
            << " recenter=" << controls.recenter_sequence
            << " stereo=" << controls.stereo_enabled
            << " resolutionScale=" << controls.vr_resolution_scale
            << " hmdCull=" << controls.hmd_culling_view_enabled
            << " cullGuard=" << kCullingHorizontalGuardBand
            << "x" << kCullingVerticalGuardBand
            << " trackingSpace=standing"
            << " worldScale=" << world_scale
            << " floorHeightOffset=" << g_floor_height_offset
            << " sneakHeightComp=" << g_last_sneak_height_compensation
            << " hmdOrigin=" << (g_hmd_origin_valid ? 1 : 0)
            << " hmdCurrent=" << (g_hmd_current_valid ? 1 : 0)
            << " selfCullView=" << (reused_cached_native_view ? 1 : 0)
            << " aggressiveDelta=" << aggressive_native_delta
            << " aggressiveSuppressed=" << (aggressive_snap_suppressed ? 1 : 0)
            << " aggressiveTotal=" << g_aggressive_suppressed_snaps
            << " nativeView " << matrix_summary(view)
            << " heldView " << matrix_summary(leveled_view)
            << " cullView " << matrix_summary(culling_view)
            << " cullProjection " << matrix_summary(culling_projection)
            << " projection " << matrix_summary(projection)
            << " hmdDelta " << matrix_summary(delta)
            << " finalVP " << matrix_summary(view_projection);
        render_log(out.str());
        g_last_camera_diag_tick = camera_log_now;
        g_last_diag_view = view;
        g_last_diag_view_projection = view_projection;
        g_last_diag_valid = true;
    }

    bool gameplay_pose_valid = false;
    bool physical_ray_active = false;
    Matrix4 gameplay_view_delta = identity_matrix();
    AcquireSRWLockShared(&g_pose_lock);
    physical_ray_active = kPhysicalAttacksEnabled &&
                          GetTickCount64() < g_physical_attack_ray_until;
    int gameplay_hand = physical_ray_active ? g_physical_attack_ray_hand : 1;
    bool selected_pose_valid = gameplay_hand == 0
        ? g_left_controller_valid
        : g_right_controller_valid;
    if (!selected_pose_valid && g_right_controller_valid) {
        gameplay_hand = 1;
        selected_pose_valid = true;
    }
    const float* gameplay_pose = gameplay_hand == 0
        ? g_left_controller_pose
        : g_right_controller_pose;
    if (g_hmd_origin_valid && selected_pose_valid) {
        gameplay_view_delta = hytalevr::relative_view_pose(
            g_hmd_origin_pose, gameplay_pose,
            std::clamp(controls.translation_scale, 0.0f, 10.0f) * world_scale,
            std::clamp(controls.translation_y_scale, 0.0f, 10.0f) * world_scale,
            controls.invert_translation_xz != 0);
        gameplay_pose_valid = true;
    }
    ReleaseSRWLockShared(&g_pose_lock);

    float ray_offset[3]{};
    float ray_direction[3]{};
    const bool ray_valid = (controls.hand_pointer_enabled || physical_ray_active) &&
                           gameplay_pose_valid;
    if (ray_valid) {
        const Matrix4 controller_view = multiply(gameplay_view_delta, floor_aligned_view);
        hytalevr::view_pose(controller_view, ray_offset, ray_direction);
    }
    AcquireSRWLockExclusive(&g_game_ray_lock);
    std::copy(std::begin(ray_offset), std::end(ray_offset),
              std::begin(g_controller_ray_offset));
    std::copy(std::begin(ray_direction), std::end(ray_direction),
              std::begin(g_controller_ray_direction));
    g_controller_game_ray_valid = ray_valid;
    g_controller_game_ray_physical_active = ray_valid && physical_ray_active;
    ReleaseSRWLockExclusive(&g_game_ray_lock);

    g_shared->controller_ray_origin_x = ray_offset[0];
    g_shared->controller_ray_origin_y = ray_offset[1];
    g_shared->controller_ray_origin_z = ray_offset[2];
    g_shared->controller_ray_direction_x = ray_direction[0];
    g_shared->controller_ray_direction_y = ray_direction[1];
    g_shared->controller_ray_direction_z = ray_direction[2];
    g_shared->controller_ray_active =
        ray_valid && g_shared->interaction_hook_active ? 1u : 0u;
    g_shared->physical_attack_ray_sequence =
        ray_valid && physical_ray_active
            ? g_shared->physical_attack_sequence
            : 0u;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->effects_stabilized));
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->updates));
}

void apply_controller_gameplay_ray(float* origin, float* direction) {
    HookCallbackScope callback_scope;
    if (g_shared) {
        InterlockedIncrement(
            reinterpret_cast<volatile LONG*>(&g_shared->interaction_ray_calls));
    }
    VrCameraControls controls{};
    if (!origin || !direction || !read_controls(controls) || !controls.enabled) return;

    float offset[3]{};
    float controller_direction[3]{};
    bool valid = false;
    bool physical = false;
    AcquireSRWLockShared(&g_game_ray_lock);
    valid = g_controller_game_ray_valid;
    physical = g_controller_game_ray_physical_active;
    std::copy(std::begin(g_controller_ray_offset), std::end(g_controller_ray_offset),
              std::begin(offset));
    std::copy(std::begin(g_controller_ray_direction),
              std::end(g_controller_ray_direction), std::begin(controller_direction));
    ReleaseSRWLockShared(&g_game_ray_lock);
    if (!valid || (!controls.hand_pointer_enabled && !physical)) return;

    for (int axis = 0; axis < 3; ++axis) {
        origin[axis] += offset[axis];
        direction[axis] = controller_direction[axis];
    }
    if (g_shared) {
        InterlockedIncrement(
            reinterpret_cast<volatile LONG*>(&g_shared->interaction_ray_overrides));
    }
}

void reset_physical_tracking_locked() {
    g_hmd_motion_history.clear();
    g_controller_tip_history[0].clear();
    g_controller_tip_history[1].clear();
    g_physical_standing_height = 0.0f;
    g_physical_standing_height_valid = false;
    g_physical_jump_armed = true;
    g_physical_swing_armed[0] = true;
    g_physical_swing_armed[1] = true;
    g_physical_jump_until = 0;
    g_physical_jump_cooldown_until = 0;
    g_physical_swing_cooldown_until[0] = 0;
    g_physical_swing_cooldown_until[1] = 0;
    g_physical_attack_ray_hand = 1;
    g_physical_attack_ray_until = 0;
    if (g_shared) {
        g_shared->physical_jump_active = 0;
        g_shared->physical_sneak_active = 0;
        g_shared->physical_attack_ray_sequence = 0;
        g_shared->physical_attack_hand = 1;
        g_shared->physical_hmd_height = 0.0f;
        g_shared->physical_hmd_vertical_movement = 0.0f;
        g_shared->physical_left_swing_speed = 0.0f;
        g_shared->physical_right_swing_speed = 0.0f;
    }
}

void update_physical_hmd_locked(const float current[12], ULONGLONG now,
                                bool recentered) {
    const float height = current[7];
    if (recentered || !g_physical_standing_height_valid) {
        g_hmd_motion_history.clear();
        g_physical_standing_height = height;
        g_physical_standing_height_valid = true;
        g_physical_jump_armed = true;
        g_physical_jump_until = 0;
        g_physical_jump_cooldown_until = now + 350;
    }

    g_hmd_motion_history.add(current[3], height, current[11], now);
    const float upward_movement = g_hmd_motion_history.net_vertical_movement(
        now, hytalevr::kPhysicalJumpWindowMs);
    const bool sneaking = hytalevr::physical_sneak_requested(
        height, g_physical_standing_height);

    if (height <= g_physical_standing_height + 0.02f && upward_movement <= 0.02f) {
        g_physical_jump_armed = true;
    }
    if (g_physical_jump_armed && !sneaking &&
        now >= g_physical_jump_cooldown_until &&
        hytalevr::physical_jump_requested(height, g_physical_standing_height,
                                          upward_movement)) {
        g_physical_jump_armed = false;
        g_physical_jump_until = now + 160;
        g_physical_jump_cooldown_until = now + 600;
    }

    if (g_shared) {
        g_shared->physical_hmd_height = height;
        g_shared->physical_hmd_vertical_movement = upward_movement;
        g_shared->physical_jump_active = now < g_physical_jump_until ? 1u : 0u;
        g_shared->physical_sneak_active = sneaking ? 1u : 0u;
    }
}

void update_physical_controller_locked(int hand, const vr::TrackedDevicePose_t& pose,
                                       bool pose_active, ULONGLONG now) {
    if (hand < 0 || hand > 1) return;
    float speed = 0.0f;
    if (!pose_active) {
        g_controller_tip_history[hand].clear();
        g_physical_swing_armed[hand] = true;
    } else {
        const auto& matrix = pose.mDeviceToAbsoluteTracking;
        const float tip_x = matrix.m[0][3] -
            matrix.m[0][2] * hytalevr::kPhysicalSwingTipOffset;
        const float tip_y = matrix.m[1][3] -
            matrix.m[1][2] * hytalevr::kPhysicalSwingTipOffset;
        const float tip_z = matrix.m[2][3] -
            matrix.m[2][2] * hytalevr::kPhysicalSwingTipOffset;
        g_controller_tip_history[hand].add(tip_x, tip_y, tip_z, now);
        speed = g_controller_tip_history[hand].average_speed(
            now, hytalevr::kPhysicalSwingWindowMs);

        if (speed < hytalevr::kPhysicalSwingRearmSpeed) {
            g_physical_swing_armed[hand] = true;
        }
        const bool jumping = now < g_physical_jump_until;
        if (!jumping && g_physical_swing_armed[hand] &&
            now >= g_physical_swing_cooldown_until[hand] &&
            hytalevr::physical_swing_requested(speed)) {
            g_physical_swing_armed[hand] = false;
            g_physical_swing_cooldown_until[hand] = now + 350;
            g_physical_attack_ray_hand = hand;
            g_physical_attack_ray_until = now + 350;
            if (g_shared) {
                g_shared->physical_attack_hand = static_cast<uint32_t>(hand);
                MemoryBarrier();
                InterlockedIncrement(reinterpret_cast<volatile LONG*>(
                    &g_shared->physical_attack_sequence));
            }
        }
    }

    if (g_shared) {
        if (hand == 0) {
            g_shared->physical_left_swing_speed = speed;
        } else {
            g_shared->physical_right_swing_speed = speed;
        }
    }
}

void update_hmd_pose(const vr::TrackedDevicePose_t& pose,
                     const VrCameraControls& controls) {
    if (!pose.bDeviceIsConnected || !pose.bPoseIsValid) return;

    float current[12]{};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 4; ++column) {
            current[row * 4 + column] = pose.mDeviceToAbsoluteTracking.m[row][column];
        }
    }

    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&g_pose_lock);
    bool recentered = false;
    if (!g_hmd_origin_valid || controls.recenter_sequence != g_recenter_sequence) {
        hytalevr::make_yaw_only_tracking_origin(current, g_hmd_origin_pose);
        g_recenter_sequence = controls.recenter_sequence;
        g_hmd_origin_valid = true;
        recentered = true;
        std::ostringstream out;
        out << "hmd recenter: standing yaw-only origin seq=" << controls.recenter_sequence
            << " poseY=" << std::fixed << std::setprecision(3) << current[7];
        render_log(out.str());
    }
    update_physical_hmd_locked(current, now, recentered);
    const float world_scale = validated_world_scale(controls);
    g_hmd_view_delta = hytalevr::relative_view_pose(
        g_hmd_origin_pose, current,
        std::clamp(controls.translation_scale, 0.0f, 10.0f) * world_scale,
        std::clamp(controls.translation_y_scale, 0.0f, 10.0f) * world_scale,
        controls.invert_translation_xz != 0);
    if (render_logging_enabled() &&
        (recentered || now - g_last_hmd_delta_diag_tick >= 1500)) {
        const Matrix4 identity = identity_matrix();
        std::ostringstream out;
        out << "hmd delta diag: seq=" << controls.recenter_sequence
            << " recentered=" << (recentered ? 1 : 0)
            << " identityDelta=" << std::fixed << std::setprecision(5)
            << matrix_max_delta(identity, g_hmd_view_delta)
            << " delta " << matrix_summary(g_hmd_view_delta);
        render_log(out.str());
        g_last_hmd_delta_diag_tick = now;
    }
    std::copy(std::begin(current), std::end(current), std::begin(g_hmd_current_pose));
    g_hmd_current_valid = true;
    ReleaseSRWLockExclusive(&g_pose_lock);
}

bool initialize_vr_actions() {
    if (g_actions_ready) return true;
    auto* input = vr::VRInput();
    if (!input) {
        if (g_shared) g_shared->controller_input_error = vr::VRInputError_IPCError;
        return false;
    }

    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(g_module, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return false;
    wcscpy_s(slash + 1, MAX_PATH - static_cast<size_t>(slash + 1 - path),
             L"hytalevr_actions.json");
    char manifest_path[MAX_PATH * 3]{};
    WideCharToMultiByte(CP_UTF8, 0, path, -1, manifest_path,
                        static_cast<int>(sizeof(manifest_path)), nullptr, nullptr);

    vr::EVRInputError error = input->SetActionManifestPath(manifest_path);
    if (error == vr::VRInputError_None) {
        error = input->GetActionSetHandle("/actions/hytale", &g_action_set);
    }
    if (error == vr::VRInputError_None) {
        error = input->GetActionHandle("/actions/hytale/in/move", &g_action_move);
    }
    if (error == vr::VRInputError_None) {
        error = input->GetActionHandle("/actions/hytale/in/turn", &g_action_turn);
    }
    if (error == vr::VRInputError_None) {
        error = input->GetActionHandle("/actions/hytale/in/jump", &g_action_jump);
    }
    if (error == vr::VRInputError_None) {
        error = input->GetActionHandle("/actions/hytale/in/sprint", &g_action_sprint);
    }
    // Optional: old installs may still have a cached action manifest without
    // left_pose. Keep the rest of the controls working in that case.
    if (error == vr::VRInputError_None) {
        const auto left_pose_error =
            input->GetActionHandle("/actions/hytale/in/left_pose", &g_action_left_pose);
        if (left_pose_error != vr::VRInputError_None) {
            g_action_left_pose = vr::k_ulInvalidActionHandle;
        }
    }
    if (error == vr::VRInputError_None) {
        error = input->GetActionHandle("/actions/hytale/in/right_pose", &g_action_right_pose);
    }
    if (error == vr::VRInputError_None) {
        error = input->GetActionHandle("/actions/hytale/in/right_trigger", &g_action_right_trigger);
    }
    if (error == vr::VRInputError_None) {
        error = input->GetActionHandle("/actions/hytale/in/left_trigger", &g_action_left_trigger);
    }
    if (error == vr::VRInputError_None) {
        error = input->GetActionHandle("/actions/hytale/in/button_x", &g_action_button_x);
    }
    if (error == vr::VRInputError_None) {
        error = input->GetActionHandle("/actions/hytale/in/button_y", &g_action_button_y);
    }
    if (error == vr::VRInputError_None) {
        error = input->GetActionHandle("/actions/hytale/in/button_b", &g_action_button_b);
    }
    if (error == vr::VRInputError_None) {
        error = input->GetActionHandle("/actions/hytale/in/left_grip", &g_action_left_grip);
    }
    if (error == vr::VRInputError_None) {
        error = input->GetActionHandle("/actions/hytale/in/right_grip", &g_action_right_grip);
    }
    if (error == vr::VRInputError_None) {
        error = input->GetActionHandle("/actions/hytale/in/right_stick_click", &g_action_right_stick_click);
    }
    g_actions_ready = error == vr::VRInputError_None;
    if (g_shared) g_shared->controller_input_error = static_cast<int32_t>(error);
    return g_actions_ready;
}

void update_vr_actions() {
    if (!g_shared) return;
    if (!initialize_vr_actions()) {
        g_shared->controller_active = 0;
        g_shared->controller_move_x = 0.0f;
        g_shared->controller_move_y = 0.0f;
        g_shared->controller_turn_x = 0.0f;
        g_shared->controller_turn_y = 0.0f;
        g_shared->controller_jump = 0;
        g_shared->controller_sprint = 0;
        g_shared->controller_right_pose_active = 0;
        g_shared->controller_right_trigger = 0.0f;
        g_shared->controller_right_trigger_pressed = 0;
        g_shared->controller_left_trigger = 0.0f;
        g_shared->controller_left_trigger_pressed = 0;
        g_shared->controller_button_x = 0;
        g_shared->controller_button_y = 0;
        g_shared->controller_button_b = 0;
        g_shared->controller_left_grip = 0;
        g_shared->controller_right_grip = 0;
        g_shared->controller_right_stick_click = 0;
        g_shared->controller_left_button_mask = 0;
        g_shared->controller_right_button_mask = 0;
        g_shared->controller_ray_active = 0;
        return;
    }

    auto* input = vr::VRInput();
    vr::VRActiveActionSet_t active{};
    active.ulActionSet = g_action_set;
    active.ulRestrictedToDevice = vr::k_ulInvalidInputValueHandle;
    active.ulSecondaryActionSet = vr::k_ulInvalidActionSetHandle;
    const auto update_error = input->UpdateActionState(&active, sizeof(active), 1);
    if (update_error != vr::VRInputError_None) {
        g_shared->controller_active = 0;
        g_shared->controller_input_error = static_cast<int32_t>(update_error);
        return;
    }

    vr::InputAnalogActionData_t move{};
    vr::InputAnalogActionData_t turn{};
    vr::InputDigitalActionData_t jump{};
    vr::InputDigitalActionData_t sprint{};
    vr::InputAnalogActionData_t trigger{};
    vr::InputAnalogActionData_t left_trigger{};
    vr::InputAnalogActionData_t left_grip{};
    vr::InputAnalogActionData_t right_grip{};
    vr::InputDigitalActionData_t button_x{};
    vr::InputDigitalActionData_t button_y{};
    vr::InputDigitalActionData_t button_b{};
    vr::InputDigitalActionData_t right_stick_click{};
    vr::InputPoseActionData_t left_pose{};
    vr::InputPoseActionData_t right_pose{};
    const auto move_error = input->GetAnalogActionData(
        g_action_move, &move, sizeof(move), vr::k_ulInvalidInputValueHandle);
    const auto turn_error = input->GetAnalogActionData(
        g_action_turn, &turn, sizeof(turn), vr::k_ulInvalidInputValueHandle);
    const auto jump_error = input->GetDigitalActionData(
        g_action_jump, &jump, sizeof(jump), vr::k_ulInvalidInputValueHandle);
    const auto sprint_error = input->GetDigitalActionData(
        g_action_sprint, &sprint, sizeof(sprint), vr::k_ulInvalidInputValueHandle);
    const auto trigger_error = input->GetAnalogActionData(
        g_action_right_trigger, &trigger, sizeof(trigger), vr::k_ulInvalidInputValueHandle);
    const auto left_trigger_error = input->GetAnalogActionData(
        g_action_left_trigger, &left_trigger, sizeof(left_trigger), vr::k_ulInvalidInputValueHandle);
    const auto left_grip_error = input->GetAnalogActionData(
        g_action_left_grip, &left_grip, sizeof(left_grip), vr::k_ulInvalidInputValueHandle);
    const auto right_grip_error = input->GetAnalogActionData(
        g_action_right_grip, &right_grip, sizeof(right_grip), vr::k_ulInvalidInputValueHandle);
    const auto button_x_error = input->GetDigitalActionData(
        g_action_button_x, &button_x, sizeof(button_x), vr::k_ulInvalidInputValueHandle);
    const auto button_y_error = input->GetDigitalActionData(
        g_action_button_y, &button_y, sizeof(button_y), vr::k_ulInvalidInputValueHandle);
    const auto button_b_error = input->GetDigitalActionData(
        g_action_button_b, &button_b, sizeof(button_b), vr::k_ulInvalidInputValueHandle);
    const auto right_stick_click_error = input->GetDigitalActionData(
        g_action_right_stick_click, &right_stick_click, sizeof(right_stick_click),
        vr::k_ulInvalidInputValueHandle);
    vr::EVRInputError left_pose_error = vr::VRInputError_None;
    if (g_action_left_pose != vr::k_ulInvalidActionHandle) {
        left_pose_error = input->GetPoseActionDataRelativeToNow(
            g_action_left_pose, vr::TrackingUniverseStanding, 0.0f,
            &left_pose, sizeof(left_pose), vr::k_ulInvalidInputValueHandle);
    }
    const auto pose_error = input->GetPoseActionDataRelativeToNow(
        g_action_right_pose, vr::TrackingUniverseStanding, 0.0f,
        &right_pose, sizeof(right_pose), vr::k_ulInvalidInputValueHandle);
    const bool move_active = move_error == vr::VRInputError_None && move.bActive;
    const bool turn_active = turn_error == vr::VRInputError_None && turn.bActive;
    const bool trigger_active = trigger_error == vr::VRInputError_None && trigger.bActive;
    const bool left_trigger_active =
        left_trigger_error == vr::VRInputError_None && left_trigger.bActive;
    const bool left_grip_active = left_grip_error == vr::VRInputError_None && left_grip.bActive;
    const bool right_grip_active = right_grip_error == vr::VRInputError_None && right_grip.bActive;
    const bool x_active = button_x_error == vr::VRInputError_None && button_x.bActive;
    const bool y_active = button_y_error == vr::VRInputError_None && button_y.bActive;
    const bool b_active = button_b_error == vr::VRInputError_None && button_b.bActive;
    const bool right_stick_click_active =
        right_stick_click_error == vr::VRInputError_None && right_stick_click.bActive;
    const bool left_pose_active = g_action_left_pose != vr::k_ulInvalidActionHandle &&
        left_pose_error == vr::VRInputError_None && left_pose.bActive &&
        left_pose.pose.bDeviceIsConnected && left_pose.pose.bPoseIsValid;
    const bool pose_active = pose_error == vr::VRInputError_None && right_pose.bActive &&
        right_pose.pose.bDeviceIsConnected && right_pose.pose.bPoseIsValid;
    uint64_t left_button_mask = 0;
    uint64_t right_button_mask = 0;
    bool legacy_left_x = false;
    bool legacy_left_grip = false;
    bool legacy_right_grip = false;
    if (g_vr_system) {
        const vr::TrackedDeviceIndex_t left_controller =
            g_vr_system->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand);
        if (left_controller != vr::k_unTrackedDeviceIndexInvalid) {
            vr::VRControllerState_t state{};
            if (g_vr_system->GetControllerState(left_controller, &state, sizeof(state))) {
                left_button_mask = state.ulButtonPressed;
                legacy_left_x = !x_active &&
                    (((left_button_mask & vr::ButtonMaskFromId(vr::k_EButton_A)) != 0) ||
                     ((left_button_mask &
                       vr::ButtonMaskFromId(vr::k_EButton_ApplicationMenu)) != 0));
                legacy_left_grip =
                    (left_button_mask & vr::ButtonMaskFromId(vr::k_EButton_Grip)) != 0;
            }
        }
        const vr::TrackedDeviceIndex_t right_controller =
            g_vr_system->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_RightHand);
        if (right_controller != vr::k_unTrackedDeviceIndexInvalid) {
            vr::VRControllerState_t state{};
            if (g_vr_system->GetControllerState(right_controller, &state, sizeof(state))) {
                right_button_mask = state.ulButtonPressed;
                legacy_right_grip =
                    (right_button_mask & vr::ButtonMaskFromId(vr::k_EButton_Grip)) != 0;
            }
        }
    }

    const bool active_input = move_active || turn_active || trigger_active ||
        left_trigger_active || left_grip_active || legacy_left_grip ||
        right_grip_active || legacy_right_grip ||
        x_active || legacy_left_x || y_active || b_active || right_stick_click_active ||
        left_pose_active || pose_active;
    g_shared->controller_active = active_input ? 1u : 0u;
    g_shared->controller_move_x = move_active ? move.x : 0.0f;
    g_shared->controller_move_y = move_active ? move.y : 0.0f;
    g_shared->controller_turn_x = turn_active ? turn.x : 0.0f;
    g_shared->controller_turn_y = turn_active ? turn.y : 0.0f;
    g_shared->controller_jump =
        jump_error == vr::VRInputError_None && jump.bActive && jump.bState ? 1u : 0u;
    g_shared->controller_sprint =
        sprint_error == vr::VRInputError_None && sprint.bActive && sprint.bState ? 1u : 0u;
    const float trigger_value = trigger_active ? std::clamp(trigger.x, 0.0f, 1.0f) : 0.0f;
    const float left_trigger_value =
        left_trigger_active ? std::clamp(left_trigger.x, 0.0f, 1.0f) : 0.0f;
    const float left_grip_value =
        left_grip_active ? std::clamp(left_grip.x, 0.0f, 1.0f) : 0.0f;
    const float right_grip_value =
        right_grip_active ? std::clamp(right_grip.x, 0.0f, 1.0f) : 0.0f;
    const bool right_trigger_was_pressed = g_right_trigger_pressed;
    const bool left_trigger_was_pressed = g_left_trigger_pressed;
    if (g_right_trigger_pressed) {
        if (trigger_value < 0.45f) g_right_trigger_pressed = false;
    } else if (trigger_value > 0.65f) {
        g_right_trigger_pressed = true;
    }
    if (g_left_trigger_pressed) {
        if (left_trigger_value < 0.45f) g_left_trigger_pressed = false;
    } else if (left_trigger_value > 0.65f) {
        g_left_trigger_pressed = true;
    }
    constexpr ULONGLONG kHeldItemActionHoldMs = 1000;
    const ULONGLONG held_item_hold_until =
        GetTickCount64() + kHeldItemActionHoldMs;
    const LONG stable_presence =
        g_stable_held_item_presence_mask.load(std::memory_order_relaxed);
    if (g_right_trigger_pressed && !right_trigger_was_pressed) {
        g_right_attack_suppresses_left_item =
            !hytalevr::held_item_was_present_before_attack(
                static_cast<uint32_t>(stable_presence),
                hytalevr::kHeldItemLeft);
        if (g_right_attack_suppresses_left_item) {
            g_pending_item_detections[hytalevr::kHeldItemLeft] = {};
        }
    }
    if (g_left_trigger_pressed && !left_trigger_was_pressed) {
        g_left_attack_suppresses_right_item =
            !hytalevr::held_item_was_present_before_attack(
                static_cast<uint32_t>(stable_presence),
                hytalevr::kHeldItemRight);
        const LoadedVrItemState& right_item =
            g_loaded_items[hytalevr::kHeldItemRight];
        g_left_use_locks_placeable_to_right =
            !g_left_attack_suppresses_right_item && right_item.asset_ready &&
            hytalevr::vr_item_definition_is_block_or_decor(
                right_item.asset.definition);
        if (g_left_attack_suppresses_right_item) {
            g_pending_item_detections[hytalevr::kHeldItemRight] = {};
        }
        if (g_left_use_locks_placeable_to_right) {
            g_pending_item_detections[hytalevr::kHeldItemLeft] = {};
        }
    }
    if (g_right_trigger_pressed) {
        g_held_item_detection_hold_until[hytalevr::kHeldItemRight].store(
            held_item_hold_until, std::memory_order_relaxed);
        if (g_right_attack_suppresses_left_item) {
            g_held_item_suppressed_until[hytalevr::kHeldItemLeft].store(
                held_item_hold_until, std::memory_order_relaxed);
        }
    } else {
        g_right_attack_suppresses_left_item = false;
    }
    if (g_left_trigger_pressed) {
        // Left trigger injects Hytale's right mouse action. A block normally
        // lives in the right hand, but some usable items can occupy the
        // off-hand, so preserve every item that was visible when use started.
        for (int side = hytalevr::kHeldItemLeft;
             side <= hytalevr::kHeldItemRight; ++side) {
            if (hytalevr::held_item_was_present_before_attack(
                    static_cast<uint32_t>(stable_presence), side)) {
                g_held_item_detection_hold_until[side].store(
                    held_item_hold_until, std::memory_order_relaxed);
            }
        }
        if (g_left_attack_suppresses_right_item) {
            g_held_item_suppressed_until[hytalevr::kHeldItemRight].store(
                held_item_hold_until, std::memory_order_relaxed);
        }
        if (g_left_use_locks_placeable_to_right &&
            !hytalevr::held_item_was_present_before_attack(
                static_cast<uint32_t>(stable_presence),
                hytalevr::kHeldItemLeft)) {
            // Hytale briefly submits a placed block as an off-hand draw.
            // Ignore that transient only when the off-hand was empty before
            // the action; a genuine torch/shield remains untouched.
            g_held_item_suppressed_until[hytalevr::kHeldItemLeft].store(
                held_item_hold_until, std::memory_order_relaxed);
        }
    } else {
        g_left_attack_suppresses_right_item = false;
        g_left_use_locks_placeable_to_right = false;
    }
    if (g_left_grip_pressed) {
        if (left_grip_value < 0.45f) g_left_grip_pressed = false;
    } else if (left_grip_value > 0.65f) {
        g_left_grip_pressed = true;
    }
    if (g_right_grip_pressed) {
        if (right_grip_value < 0.45f) g_right_grip_pressed = false;
    } else if (right_grip_value > 0.65f) {
        g_right_grip_pressed = true;
    }
    g_shared->controller_right_trigger = trigger_value;
    g_shared->controller_right_trigger_pressed = g_right_trigger_pressed ? 1u : 0u;
    g_shared->controller_left_trigger = left_trigger_value;
    g_shared->controller_left_trigger_pressed = g_left_trigger_pressed ? 1u : 0u;
    g_shared->controller_button_x =
        ((x_active && button_x.bState) || legacy_left_x) ? 1u : 0u;
    g_shared->controller_button_y = y_active && button_y.bState ? 1u : 0u;
    g_shared->controller_button_b = b_active && button_b.bState ? 1u : 0u;
    g_shared->controller_left_grip =
        (g_left_grip_pressed || legacy_left_grip) ? 1u : 0u;
    g_shared->controller_right_grip =
        (g_right_grip_pressed || legacy_right_grip) ? 1u : 0u;
    g_shared->controller_right_stick_click =
        right_stick_click_active && right_stick_click.bState ? 1u : 0u;
    g_shared->controller_left_button_mask = left_button_mask;
    g_shared->controller_right_button_mask = right_button_mask;
    g_shared->controller_right_pose_active = pose_active ? 1u : 0u;
    AcquireSRWLockExclusive(&g_pose_lock);
    g_left_controller_valid = left_pose_active;
    if (left_pose_active) {
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 4; ++column) {
                g_left_controller_pose[row * 4 + column] =
                    left_pose.pose.mDeviceToAbsoluteTracking.m[row][column];
            }
        }
    }
    g_right_controller_valid = pose_active;
    if (pose_active) {
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 4; ++column) {
                g_right_controller_pose[row * 4 + column] =
                    right_pose.pose.mDeviceToAbsoluteTracking.m[row][column];
            }
        }
    }
    if constexpr (kPhysicalAttacksEnabled) {
        const ULONGLONG physical_now = GetTickCount64();
        update_physical_controller_locked(0, left_pose.pose, left_pose_active, physical_now);
        update_physical_controller_locked(1, right_pose.pose, pose_active, physical_now);
    }
    ReleaseSRWLockExclusive(&g_pose_lock);
    g_shared->controller_input_error = static_cast<int32_t>(
        move_error != vr::VRInputError_None ? move_error :
        (turn_error != vr::VRInputError_None ? turn_error :
        (jump_error != vr::VRInputError_None ? jump_error :
        (sprint_error != vr::VRInputError_None ? sprint_error :
        (trigger_error != vr::VRInputError_None ? trigger_error :
        (left_trigger_error != vr::VRInputError_None ? left_trigger_error :
        (left_grip_error != vr::VRInputError_None ? left_grip_error :
        (right_grip_error != vr::VRInputError_None ? right_grip_error :
        (button_x_error != vr::VRInputError_None ? button_x_error :
        (button_y_error != vr::VRInputError_None ? button_y_error :
        (button_b_error != vr::VRInputError_None ? button_b_error :
        (right_stick_click_error != vr::VRInputError_None ? right_stick_click_error :
         pose_error))))))))))));
}

bool synchronize_openvr(const VrCameraControls& controls) {
    auto* compositor = vr::VRCompositor();
    if (!compositor) return false;

    LARGE_INTEGER frequency{};
    LARGE_INTEGER start{};
    LARGE_INTEGER end{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    vr::TrackedDevicePose_t render_poses[vr::k_unMaxTrackedDeviceCount]{};
    const auto error = compositor->WaitGetPoses(
        render_poses, vr::k_unMaxTrackedDeviceCount, nullptr, 0);
    QueryPerformanceCounter(&end);
    if (error == vr::VRCompositorError_None) {
        const auto& hmd_pose = render_poses[vr::k_unTrackedDeviceIndex_Hmd];
        if (hmd_pose.bDeviceIsConnected && hmd_pose.bPoseIsValid) {
            update_hmd_pose(hmd_pose, controls);
        } else if (g_shared) {
            g_shared->physical_jump_active = 0;
            g_shared->physical_sneak_active = 0;
        }
    }

    if (g_shared) {
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->wait_pose_calls));
        if (frequency.QuadPart > 0) {
            g_shared->wait_pose_us = static_cast<uint32_t>(
                ((end.QuadPart - start.QuadPart) * 1000000LL) / frequency.QuadPart);
        }
        g_shared->compositor_focus_pid = compositor->GetCurrentSceneFocusProcess();
    }
    return error == vr::VRCompositorError_None;
}

bool initialize_stereo() {
    if (g_vr_system) return vr::VRCompositor() != nullptr;
    vr::EVRInitError error = vr::VRInitError_None;
    g_vr_system = vr::VR_Init(&error, vr::VRApplication_Scene);
    if (error != vr::VRInitError_None || !g_vr_system || !vr::VRCompositor()) {
        if (g_shared) g_shared->stereo_error = 1000 + static_cast<int32_t>(error);
        g_vr_system = nullptr;
        return false;
    }
    vr::VRCompositor()->SetTrackingSpace(vr::TrackingUniverseStanding);
    if (auto* applications = vr::VRApplications()) {
        wchar_t module_path[MAX_PATH]{};
        GetModuleFileNameW(g_module, module_path, MAX_PATH);
        wchar_t* slash = wcsrchr(module_path, L'\\');
        if (slash) {
            wcscpy_s(slash + 1, MAX_PATH - static_cast<size_t>(slash + 1 - module_path),
                     L"hytalevr.vrmanifest");
            char manifest_path[MAX_PATH * 3]{};
            WideCharToMultiByte(CP_UTF8, 0, module_path, -1, manifest_path,
                                static_cast<int>(sizeof(manifest_path)), nullptr, nullptr);
            applications->AddApplicationManifest(manifest_path, false);
        }
        g_shared->identify_error = static_cast<int32_t>(applications->IdentifyApplication(
            GetCurrentProcessId(), "com.hytalevr.injected"));
        g_shared->scene_focus_pid = applications->GetCurrentSceneProcessId();
    }
    VrCameraControls controls{};
    if (read_controls(controls)) synchronize_openvr(controls);
    uint32_t width = 0;
    uint32_t height = 0;
    g_vr_system->GetRecommendedRenderTargetSize(&width, &height);
    if (g_shared) {
        g_shared->recommended_width = width;
        g_shared->recommended_height = height;
    }
    if (g_shared) g_shared->stereo_error = 0;
    return true;
}

void restore_source_supersampling() {
    if (g_supersampled_window && g_sdl_set_window_size &&
        g_supersample_base_width > 0 && g_supersample_base_height > 0) {
        g_sdl_set_window_size(g_supersampled_window,
                              g_supersample_base_width,
                              g_supersample_base_height);
        if (g_sdl_sync_window) g_sdl_sync_window(g_supersampled_window);
    }
    g_supersampled_window = nullptr;
    g_supersample_base_width = 0;
    g_supersample_base_height = 0;
    g_supersample_target_width = 0;
    g_supersample_target_height = 0;
    g_source_supersampling_active = false;
}

uint32_t effective_render_options(const VrCameraControls& controls) {
    uint32_t options = controls.reserved_render_option;
    if ((options & kVrEffectQualityControlsPresent) == 0u) {
        // Older v122 dashboards did not publish the quality-option marker.
        options |= kVrEffectProjectionCorrection;
    }
    return options;
}

bool prepare_render_resolution(void* window, const VrCameraControls& controls) {
    if (!g_shared || !g_vr_system || !window) {
        if (g_shared) g_shared->resolution_error = 1;
        return false;
    }

    constexpr uint64_t kSdlWindowFullscreen = 0x0000000000000001ull;
    constexpr uint64_t kSdlWindowMaximized = 0x0000000000000080ull;
    const bool supersampling_enabled =
        (effective_render_options(controls) & kVrEffectSupersampling) != 0u;
    const float requested_scale =
        std::clamp(controls.vr_resolution_scale, 1.0f, 2.0f);

    if (!supersampling_enabled || requested_scale <= 1.001f) {
        restore_source_supersampling();
    } else if (g_sdl_get_window_size && g_sdl_set_window_size &&
               g_sdl_get_window_flags) {
        int logical_width = 0;
        int logical_height = 0;
        const uint64_t flags = g_sdl_get_window_flags(window);
        const bool can_resize_source =
            (flags & (kSdlWindowFullscreen | kSdlWindowMaximized)) == 0 &&
            g_sdl_get_window_size(window, &logical_width, &logical_height) &&
            logical_width > 0 && logical_height > 0;
        if (can_resize_source) {
            if (g_supersampled_window != window ||
                g_supersample_base_width <= 0 || g_supersample_base_height <= 0) {
                if (g_supersampled_window && g_supersampled_window != window) {
                    restore_source_supersampling();
                }
                g_supersampled_window = window;
                g_supersample_base_width = logical_width;
                g_supersample_base_height = logical_height;
            }
            const int target_width = (std::max)(
                1, static_cast<int>(std::lround(g_supersample_base_width *
                                                requested_scale)));
            const int target_height = (std::max)(
                1, static_cast<int>(std::lround(g_supersample_base_height *
                                                requested_scale)));
            if (!g_source_supersampling_active ||
                target_width != g_supersample_target_width ||
                target_height != g_supersample_target_height) {
                if (g_sdl_set_window_size(window, target_width, target_height)) {
                    if (g_sdl_sync_window) g_sdl_sync_window(window);
                    g_supersample_target_width = target_width;
                    g_supersample_target_height = target_height;
                    g_source_supersampling_active = true;
                    std::ostringstream out;
                    out << "source supersampling enabled logical="
                        << g_supersample_base_width << "x"
                        << g_supersample_base_height << " target="
                        << target_width << "x" << target_height;
                    render_log(out.str());
                } else {
                    g_source_supersampling_active = false;
                    render_log("source supersampling resize failed; using output scaling");
                }
            }
        } else {
            // Fullscreen and maximized windows cannot be resized reliably at
            // swap time. Keep the base size so windowed mode can resume it.
            g_source_supersampling_active = false;
        }
    } else {
        g_source_supersampling_active = false;
    }

    uint32_t recommended_width = 0;
    uint32_t recommended_height = 0;
    g_vr_system->GetRecommendedRenderTargetSize(&recommended_width, &recommended_height);
    int pixel_width = 0;
    int pixel_height = 0;
    if (!g_sdl_get_window_size_in_pixels ||
        !g_sdl_get_window_size_in_pixels(window, &pixel_width, &pixel_height)) {
        g_shared->resolution_error = 2;
        return false;
    }

    // AFW scales both output dimensions together. Projection matrices remain
    // untouched, so resolution changes cannot stretch either eye.
    g_shared->recommended_width = recommended_width;
    g_shared->recommended_height = recommended_height;
    g_shared->backbuffer_width = pixel_width;
    g_shared->backbuffer_height = pixel_height;
    g_shared->resolution_error = 0;
    static int last_logged_width = 0;
    static int last_logged_height = 0;
    if (last_logged_width != pixel_width || last_logged_height != pixel_height) {
        std::ostringstream out;
        out << "resolution recommendedSteamVR=" << recommended_width << "x"
            << recommended_height << " backbuffer=" << pixel_width << "x"
            << pixel_height << " requestedScale=" << controls.vr_resolution_scale
            << " sourceSupersampling=" << (g_source_supersampling_active ? 1 : 0);
        render_log(out.str());
        last_logged_width = pixel_width;
        last_logged_height = pixel_height;
    }
    return true;
}

float active_vr_output_scale(const VrCameraControls& controls) {
    if ((effective_render_options(controls) & kVrEffectSupersampling) == 0u) {
        return 1.0f;
    }
    return g_source_supersampling_active
        ? 1.0f
        : std::clamp(controls.vr_resolution_scale, 1.0f, 2.0f);
}

bool initialize_gl_capture() {
    if (!g_gl_blit_framebuffer) {
        g_gl_gen_framebuffers = reinterpret_cast<GlGenFramebuffersFn>(wglGetProcAddress("glGenFramebuffers"));
        g_gl_delete_framebuffers = reinterpret_cast<GlDeleteFramebuffersFn>(
            wglGetProcAddress("glDeleteFramebuffers"));
        g_gl_bind_framebuffer = reinterpret_cast<GlBindFramebufferFn>(wglGetProcAddress("glBindFramebuffer"));
        g_gl_framebuffer_texture_2d = reinterpret_cast<GlFramebufferTexture2DFn>(
            wglGetProcAddress("glFramebufferTexture2D"));
        g_gl_blit_framebuffer = reinterpret_cast<GlBlitFramebufferFn>(wglGetProcAddress("glBlitFramebuffer"));
        g_gl_check_framebuffer_status = reinterpret_cast<GlCheckFramebufferStatusFn>(
            wglGetProcAddress("glCheckFramebufferStatus"));
    }
    if (!g_gl_gen_framebuffers || !g_gl_bind_framebuffer ||
        !g_gl_framebuffer_texture_2d || !g_gl_blit_framebuffer ||
        !g_gl_check_framebuffer_status) {
        return false;
    }
    if (g_capture_fbo) return true;
    g_gl_gen_framebuffers(1, &g_capture_fbo);
    return g_capture_fbo != 0;
}

bool ensure_ui_overlay(float width_meters, float distance_meters,
                       uint32_t recenter_sequence) {
    auto* overlay = vr::VROverlay();
    if (!overlay) {
        if (g_shared) g_shared->ui_overlay_error = 1;
        return false;
    }
    if (g_ui_overlay == vr::k_ulOverlayHandleInvalid) {
        vr::EVROverlayError error = overlay->FindOverlay("hytalevr.opengl.ui", &g_ui_overlay);
        if (error != vr::VROverlayError_None) {
            error = overlay->CreateOverlay("hytalevr.opengl.ui", "Hytale VR UI", &g_ui_overlay);
        }
        if (error != vr::VROverlayError_None) {
            if (g_shared) g_shared->ui_overlay_error = static_cast<int32_t>(error);
            g_ui_overlay = vr::k_ulOverlayHandleInvalid;
            return false;
        }
        overlay->SetOverlayRenderingPid(g_ui_overlay, GetCurrentProcessId());
        overlay->SetOverlayFlag(g_ui_overlay, vr::VROverlayFlags_IsPremultiplied, true);
        overlay->SetOverlayInputMethod(g_ui_overlay, vr::VROverlayInputMethod_Mouse);
        overlay->SetOverlayAlpha(g_ui_overlay, 1.0f);
    }
    overlay->SetOverlayWidthInMeters(g_ui_overlay, std::clamp(width_meters, 0.35f, 4.0f));

    const float distance = std::clamp(distance_meters, 0.35f, 6.0f);
    const bool needs_anchor =
        !g_ui_overlay_world_transform_valid ||
        g_ui_overlay_recenter_sequence != recenter_sequence ||
        std::fabs(g_ui_overlay_anchor_distance - distance) > 0.001f;
    if (!needs_anchor) return true;

    float hmd[12]{};
    bool hmd_valid = false;
    AcquireSRWLockShared(&g_pose_lock);
    hmd_valid = g_hmd_current_valid;
    if (hmd_valid) {
        std::copy(std::begin(g_hmd_current_pose), std::end(g_hmd_current_pose),
                  std::begin(hmd));
    }
    ReleaseSRWLockShared(&g_pose_lock);
    float absolute_pose[12]{};
    if (!hmd_valid ||
        !hytalevr::make_world_locked_ui_pose(hmd, distance, absolute_pose)) {
        if (g_shared) g_shared->ui_overlay_error = -30;
        return false;
    }
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 4; ++column) {
            g_ui_overlay_world_transform.m[row][column] =
                absolute_pose[row * 4 + column];
        }
    }
    const vr::EVROverlayError transform_error =
        overlay->SetOverlayTransformAbsolute(
            g_ui_overlay, vr::TrackingUniverseStanding,
            &g_ui_overlay_world_transform);
    if (transform_error != vr::VROverlayError_None) {
        if (g_shared) {
            g_shared->ui_overlay_error =
                static_cast<int32_t>(transform_error);
        }
        return false;
    }
    g_ui_overlay_world_transform_valid = true;
    g_ui_overlay_recenter_sequence = recenter_sequence;
    g_ui_overlay_anchor_distance = distance;
    if (render_logging_enabled()) {
        std::ostringstream out;
        out << "menu overlay anchored in Standing space seq="
            << recenter_sequence << " distance=" << distance
            << " position=" << absolute_pose[3] << ","
            << absolute_pose[7] << "," << absolute_pose[11];
        render_log(out.str());
    }
    return true;
}

void clear_ui_overlay_cursor() {
    auto* overlay = vr::VROverlay();
    if (overlay && g_ui_overlay != vr::k_ulOverlayHandleInvalid) {
        overlay->ClearOverlayCursorPositionOverride(g_ui_overlay);
    }
}

void hide_ui_overlay(int32_t error_code) {
    auto* overlay = vr::VROverlay();
    if (overlay && g_ui_overlay != vr::k_ulOverlayHandleInvalid) {
        overlay->ClearOverlayCursorPositionOverride(g_ui_overlay);
        overlay->HideOverlay(g_ui_overlay);
    }
    if (g_shared) {
        g_shared->ui_overlay_active = 0;
        g_shared->ui_overlay_error = error_code;
        g_shared->pointer_visible = 0;
        g_shared->pointer_menu_mode = 0;
        g_shared->pointer_surface_width = 0;
        g_shared->pointer_surface_height = 0;
    }
    g_ui_overlay_world_transform_valid = false;
}

bool menu_texture_fresh(LONG menu_frame, ULONGLONG now) {
    static LONG last_menu_frame = 0;
    static ULONGLONG last_menu_frame_tick = 0;
    if (menu_frame != last_menu_frame) {
        last_menu_frame = menu_frame;
        last_menu_frame_tick = now;
    }
    return last_menu_frame_tick != 0 && now - last_menu_frame_tick <= 350;
}

void publish_menu_overlay_texture(GLuint texture_id, const VrCameraControls& controls) {
    (void)texture_id;
    const GLuint menu_texture = g_ui_scale_shared
        ? static_cast<GLuint>(InterlockedCompareExchange(&g_ui_scale_shared->menuTextureId, 0, 0))
        : 0;
    const LONG menu_width = g_ui_scale_shared
        ? InterlockedCompareExchange(&g_ui_scale_shared->menuTextureWidth, 0, 0)
        : 0;
    const LONG menu_height = g_ui_scale_shared
        ? InterlockedCompareExchange(&g_ui_scale_shared->menuTextureHeight, 0, 0)
        : 0;
    const LONG capture_error = g_ui_scale_shared
        ? InterlockedCompareExchange(&g_ui_scale_shared->menuCaptureError, 0, 0)
        : 0;
    const LONG menu_frame = g_ui_scale_shared
        ? InterlockedCompareExchange(&g_ui_scale_shared->menuTextureFrame, 0, 0)
        : 0;
    const ULONGLONG now = GetTickCount64();
    const bool menu_texture_is_recent = menu_texture_fresh(menu_frame, now);
    const bool menu_ui_mode =
        controls.enabled && controls.stereo_enabled && controls.ui_overlay_enabled &&
        menu_visible_recently();

    auto* overlay = vr::VROverlay();
    if (!menu_ui_mode || menu_texture == 0 || menu_width <= 0 || menu_height <= 0 || !overlay) {
        hide_ui_overlay(static_cast<int32_t>(capture_error));
        return;
    }

    if (!ensure_ui_overlay(
            controls.ui_overlay_width, controls.ui_overlay_distance,
            controls.recenter_sequence)) {
        return;
    }
    vr::HmdVector2_t mouse_scale{};
    mouse_scale.v[0] = static_cast<float>(menu_width);
    mouse_scale.v[1] = static_cast<float>(menu_height);
    overlay->SetOverlayMouseScale(g_ui_overlay, &mouse_scale);

    vr::Texture_t ui_texture{
        reinterpret_cast<void*>(static_cast<uintptr_t>(menu_texture)),
        vr::TextureType_OpenGL,
        vr::ColorSpace_Gamma,
    };
    const vr::EVROverlayError error = overlay->SetOverlayTexture(g_ui_overlay, &ui_texture);
    if (error == vr::VROverlayError_None) {
        overlay->ShowOverlay(g_ui_overlay);
        if (g_shared) {
            g_shared->ui_overlay_active = 1;
            g_shared->ui_overlay_error = 0;
            InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->ui_overlay_frames));
        }
        static ULONGLONG last_overlay_log = 0;
        if (now - last_overlay_log >= 1000) {
            std::ostringstream out;
            out << "menu overlay texture submitted tex=" << menu_texture
                << " size=" << menu_width << "x" << menu_height
                << " frame=" << menu_frame
                << " fresh=" << (menu_texture_is_recent ? 1 : 0);
            render_log(out.str());
            last_overlay_log = now;
        }
    } else {
        hide_ui_overlay(static_cast<int32_t>(error));
    }
}

void submit_menu_overlay_texture(GLuint texture_id, const VrCameraControls& controls) {
    publish_menu_overlay_texture(texture_id, controls);
}

bool ensure_ui_target() {
    if (!initialize_gl_capture()) {
        if (g_shared) g_shared->ui_overlay_error = 20;
        return false;
    }
    int width = g_shared ? static_cast<int>(g_shared->backbuffer_width) : 0;
    int height = g_shared ? static_cast<int>(g_shared->backbuffer_height) : 0;
    if (width <= 0 || height <= 0) {
        width = g_texture_width > 0 ? g_texture_width : 1280;
        height = g_texture_height > 0 ? g_texture_height : 720;
    }
    if (g_ui_texture && g_ui_width == width && g_ui_height == height && g_ui_fbo) {
        return true;
    }
    if (!g_ui_fbo) {
        g_gl_gen_framebuffers(1, &g_ui_fbo);
    }
    if (!g_ui_texture) {
        glGenTextures(1, &g_ui_texture);
    }
    if (!g_ui_fbo || !g_ui_texture) {
        if (g_shared) g_shared->ui_overlay_error = 21;
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, g_ui_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE_VALUE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE_VALUE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    GLint draw = 0;
    GLint read = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_VALUE, &draw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_VALUE, &read);
    g_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER_VALUE, g_ui_fbo);
    g_gl_framebuffer_texture_2d(GL_DRAW_FRAMEBUFFER_VALUE, GL_COLOR_ATTACHMENT0_VALUE,
                                GL_TEXTURE_2D, g_ui_texture, 0);
    const bool complete =
        g_gl_check_framebuffer_status(GL_DRAW_FRAMEBUFFER_VALUE) == GL_FRAMEBUFFER_COMPLETE_VALUE;
    g_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER_VALUE, static_cast<GLuint>(draw));
    g_gl_bind_framebuffer(GL_READ_FRAMEBUFFER_VALUE, static_cast<GLuint>(read));
    if (!complete) {
        if (g_shared) g_shared->ui_overlay_error = 22;
        return false;
    }
    g_ui_width = width;
    g_ui_height = height;
    return true;
}

bool ui_overlay_requested() {
    return false;
}

void __fastcall hook_noesis_begin_onscreen(void* device) {
    HookCallbackScope callback_scope;
    if (g_noesis_begin_trampoline) g_noesis_begin_trampoline(device);
}

void __fastcall hook_noesis_end_onscreen(void* device) {
    HookCallbackScope callback_scope;
    const bool redirected = g_ui_redirect_active;
    if (g_noesis_end_trampoline) g_noesis_end_trampoline(device);
    if (!redirected) return;

    g_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER_VALUE, static_cast<GLuint>(g_ui_saved_draw_fbo));
    g_gl_bind_framebuffer(GL_READ_FRAMEBUFFER_VALUE, static_cast<GLuint>(g_ui_saved_read_fbo));
    glViewport(g_ui_saved_viewport[0], g_ui_saved_viewport[1],
               g_ui_saved_viewport[2], g_ui_saved_viewport[3]);
    g_ui_redirect_active = false;

    if (g_ui_overlay != vr::k_ulOverlayHandleInvalid) {
        vr::Texture_t texture{
            reinterpret_cast<void*>(static_cast<uintptr_t>(g_ui_texture)),
            vr::TextureType_OpenGL,
            vr::ColorSpace_Gamma,
        };
        auto* overlay = vr::VROverlay();
        if (overlay) {
            const vr::EVROverlayError error = overlay->SetOverlayTexture(g_ui_overlay, &texture);
            if (error == vr::VROverlayError_None) {
                overlay->ShowOverlay(g_ui_overlay);
                if (g_shared) {
                    InterlockedIncrement(reinterpret_cast<volatile LONG*>(
                        &g_shared->ui_overlay_frames));
                    g_shared->ui_overlay_error = 0;
                }
            } else if (g_shared) {
                g_shared->ui_overlay_error = static_cast<int32_t>(error);
            }
        }
    }
}

void __fastcall hook_noesis_renderer_render(void* renderer, void* a, void* b, void* c) {
    HookCallbackScope callback_scope;
    if (g_noesis_renderer_trampoline) {
        g_noesis_renderer_trampoline(renderer, a, b, c);
    }
}

void publish_pointer(bool valid, int x, int y, int surface_width,
                     int surface_height, bool menu_mode) {
    if (!g_shared) return;
    g_shared->pointer_visible = valid ? 1u : 0u;
    g_shared->pointer_menu_mode = menu_mode ? 1u : 0u;
    g_shared->pointer_surface_width = valid && surface_width > 0
        ? static_cast<uint32_t>(surface_width)
        : 0u;
    g_shared->pointer_surface_height = valid && surface_height > 0
        ? static_cast<uint32_t>(surface_height)
        : 0u;
    if (!valid) return;
    g_shared->pointer_x = x;
    g_shared->pointer_y = y;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->pointer_draws));
}

bool project_head_point_using_render_eye(int eye, const float head[3], float clip[4]) {
    if (!g_vr_system || eye < 0 || eye > 1) return false;

    Matrix4 eye_view{};
    Matrix4 projection{};
    AcquireSRWLockShared(&g_camera_lock);
    const bool camera_valid = g_afw_camera_valid;
    if (camera_valid) {
        eye_view = g_afw_source_to_eye_view[eye];
        projection = g_afw_eye_projection[eye];
    }
    ReleaseSRWLockShared(&g_camera_lock);
    if (!camera_valid) {
        const vr::EVREye vr_eye = eye == 0 ? vr::Eye_Left : vr::Eye_Right;
        eye_view = inverse_eye_to_head(g_vr_system->GetEyeToHeadTransform(vr_eye), 1.0f);
        projection = from_openvr(g_vr_system->GetProjectionMatrix(vr_eye, 0.05f, 100.0f));
    }

    float eye_point[4]{eye_view.value[12], eye_view.value[13],
                       eye_view.value[14], 1.0f};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            eye_point[row] += eye_view.value[column * 4 + row] * head[column];
        }
    }
    std::fill(clip, clip + 4, 0.0f);
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            clip[row] += projection.value[column * 4 + row] * eye_point[column];
        }
    }
    return std::isfinite(clip[3]) && clip[3] > 0.001f;
}

bool controller_pointer_pixel(int eye, int width, int height, float distance,
                              int& pixel_x, int& pixel_y,
                              int& surface_width, int& surface_height,
                              bool& menu_mode,
                              bool allow_menu_overlay = true,
                              float world_scale = 1.0f) {
    if (!g_vr_system || width <= 0 || height <= 0) return false;
    surface_width = width;
    surface_height = height;
    menu_mode = false;
    const bool menu_overlay_active = allow_menu_overlay &&
        g_shared && g_shared->ui_overlay_active != 0 &&
        g_ui_overlay != vr::k_ulOverlayHandleInvalid;
    if (menu_overlay_active) {
        const int overlay_width = g_ui_scale_shared
            ? static_cast<int>(InterlockedCompareExchange(&g_ui_scale_shared->menuTextureWidth, 0, 0))
            : static_cast<int>(g_shared->capture_width);
        const int overlay_height = g_ui_scale_shared
            ? static_cast<int>(InterlockedCompareExchange(&g_ui_scale_shared->menuTextureHeight, 0, 0))
            : static_cast<int>(g_shared->capture_height);
        if (overlay_width <= 0 || overlay_height <= 0) {
            clear_ui_overlay_cursor();
            return false;
        }
        float controller_pose[12]{};
        AcquireSRWLockShared(&g_pose_lock);
        const bool overlay_pose_valid = g_right_controller_valid;
        if (overlay_pose_valid) {
            std::copy(std::begin(g_right_controller_pose), std::end(g_right_controller_pose),
                      std::begin(controller_pose));
        }
        ReleaseSRWLockShared(&g_pose_lock);
        auto* overlay = vr::VROverlay();
        if (overlay_pose_valid && overlay) {
            vr::VROverlayIntersectionParams_t params{};
            params.eOrigin = vr::TrackingUniverseStanding;
            for (int row = 0; row < 3; ++row) {
                params.vSource.v[row] = controller_pose[row * 4 + 3];
                params.vDirection.v[row] = -controller_pose[row * 4 + 2];
            }
            vr::VROverlayIntersectionResults_t results{};
            if (overlay->ComputeOverlayIntersection(g_ui_overlay, &params, &results) &&
                std::isfinite(results.vUVs.v[0]) && std::isfinite(results.vUVs.v[1]) &&
                results.vUVs.v[0] >= 0.0f && results.vUVs.v[0] <= 1.0f &&
                results.vUVs.v[1] >= 0.0f && results.vUVs.v[1] <= 1.0f) {
                pixel_x = static_cast<int>(std::lround(results.vUVs.v[0] * overlay_width));
                pixel_y = static_cast<int>(std::lround((1.0f - results.vUVs.v[1]) * overlay_height));
                vr::HmdVector2_t cursor{};
                cursor.v[0] = static_cast<float>(std::clamp(pixel_x, 0, overlay_width));
                cursor.v[1] = static_cast<float>(std::clamp(pixel_y, 0, overlay_height));
                overlay->SetOverlayCursorPositionOverride(g_ui_overlay, &cursor);
                surface_width = overlay_width;
                surface_height = overlay_height;
                menu_mode = true;
                return true;
            }
        }
        clear_ui_overlay_cursor();
    }
    float hmd[12]{};
    float controller[12]{};
    AcquireSRWLockShared(&g_pose_lock);
    const bool valid = g_hmd_current_valid && g_right_controller_valid;
    if (valid) {
        std::copy(std::begin(g_hmd_current_pose), std::end(g_hmd_current_pose), std::begin(hmd));
        std::copy(std::begin(g_right_controller_pose), std::end(g_right_controller_pose),
                  std::begin(controller));
    }
    ReleaseSRWLockShared(&g_pose_lock);
    if (!valid) return false;

    const float pointer_distance = std::clamp(distance, 0.5f, 10.0f);
    world_scale = std::clamp(std::isfinite(world_scale) ? world_scale : 1.0f,
                             0.50f, 2.00f);
    float absolute[3]{};
    for (int row = 0; row < 3; ++row) {
        const float scaled_origin = hmd[row * 4 + 3] +
            (controller[row * 4 + 3] - hmd[row * 4 + 3]) * world_scale;
        absolute[row] = scaled_origin -
                        controller[row * 4 + 2] * pointer_distance * world_scale;
    }

    float head[3]{};
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            head[column] += hmd[row * 4 + column] *
                (absolute[row] - hmd[row * 4 + 3]);
        }
    }

    float clip[4]{};
    if (!project_head_point_using_render_eye(eye, head, clip)) return false;
    const float ndc_x = clip[0] / clip[3];
    const float ndc_y = clip[1] / clip[3];
    if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y) ||
        ndc_x < -1.1f || ndc_x > 1.1f || ndc_y < -1.1f || ndc_y > 1.1f) {
        return false;
    }
    pixel_x = static_cast<int>(std::lround((ndc_x * 0.5f + 0.5f) * width));
    pixel_y = static_cast<int>(std::lround((ndc_y * 0.5f + 0.5f) * height));
    return true;
}

bool project_tracking_point_to_eye(int eye, int width, int height,
                                   const float hmd[12],
                                   const float absolute[3],
                                   int& pixel_x, int& pixel_y) {
    if (!g_vr_system || width <= 0 || height <= 0) return false;

    float head[3]{};
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            head[column] += hmd[row * 4 + column] *
                (absolute[row] - hmd[row * 4 + 3]);
        }
    }

    float clip[4]{};
    if (!project_head_point_using_render_eye(eye, head, clip)) return false;
    const float ndc_x = clip[0] / clip[3];
    const float ndc_y = clip[1] / clip[3];
    if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y) ||
        ndc_x < -1.3f || ndc_x > 1.3f || ndc_y < -1.3f || ndc_y > 1.3f) {
        return false;
    }
    pixel_x = static_cast<int>(std::lround((ndc_x * 0.5f + 0.5f) * width));
    pixel_y = static_cast<int>(std::lround((ndc_y * 0.5f + 0.5f) * height));
    return true;
}

bool project_tracking_point_to_eye_ndc(int eye, const float hmd[12],
                                       const float absolute[3],
                                       float& ndc_x, float& ndc_y,
                                       float& depth) {
    if (!g_vr_system) return false;

    float head[3]{};
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            head[column] += hmd[row * 4 + column] *
                (absolute[row] - hmd[row * 4 + 3]);
        }
    }

    float clip[4]{};
    if (!project_head_point_using_render_eye(eye, head, clip)) return false;
    ndc_x = clip[0] / clip[3];
    ndc_y = clip[1] / clip[3];
    depth = clip[3];
    return std::isfinite(ndc_x) && std::isfinite(ndc_y) &&
           std::isfinite(depth) && depth > 0.001f &&
           ndc_x >= -64.0f && ndc_x <= 64.0f &&
           ndc_y >= -64.0f && ndc_y <= 64.0f;
}

bool controller_hand_ndc_for_eye(vr::EVREye eye, float& ndc_x,
                                 float& ndc_y, float& depth) {
    float hmd[12]{};
    float controller[12]{};
    AcquireSRWLockShared(&g_pose_lock);
    const bool valid = g_hmd_current_valid && g_right_controller_valid;
    if (valid) {
        std::copy(std::begin(g_hmd_current_pose), std::end(g_hmd_current_pose),
                  std::begin(hmd));
        std::copy(std::begin(g_right_controller_pose), std::end(g_right_controller_pose),
                  std::begin(controller));
    }
    ReleaseSRWLockShared(&g_pose_lock);
    if (!valid) return false;

    float absolute[3]{};
    for (int row = 0; row < 3; ++row) {
        const float origin = controller[row * 4 + 3];
        const float up = controller[row * 4 + 1];
        const float forward = -controller[row * 4 + 2];
        absolute[row] = origin + forward * 0.12f - up * 0.025f;
    }
    return project_tracking_point_to_eye_ndc(eye == vr::Eye_Left ? 0 : 1,
                                             hmd, absolute,
                                             ndc_x, ndc_y, depth);
}

void suppress_center_reticle(int source_x, int source_y, int source_width,
                             int source_height, int width, int height,
                             bool multisampled) {
    const int source_half = 14;
    const int source_offset = 38;
    const int source_center_x = source_x + source_width / 2;
    const int source_center_y = source_y + source_height / 2;
    int sample_center_x = source_center_x + source_offset;
    if (sample_center_x + source_half >= source_x + source_width) {
        sample_center_x = source_center_x - source_offset;
    }
    const int destination_half_x = std::max(4, source_half * width / source_width);
    const int destination_half_y = std::max(4, source_half * height / source_height);
    g_gl_blit_framebuffer(
        sample_center_x - source_half, source_center_y - source_half,
        sample_center_x + source_half, source_center_y + source_half,
        width / 2 - destination_half_x, height / 2 - destination_half_y,
        width / 2 + destination_half_x, height / 2 + destination_half_y,
        GL_COLOR_BUFFER_BIT, multisampled ? GL_NEAREST : GL_LINEAR);
}

void clear_scissor_rect(int x, int y, int width, int height,
                        int target_width, int target_height) {
    const int left = std::clamp(x, 0, target_width);
    const int bottom = std::clamp(y, 0, target_height);
    const int right = std::clamp(x + width, 0, target_width);
    const int top = std::clamp(y + height, 0, target_height);
    if (right > left && top > bottom) {
        glScissor(left, bottom, right - left, top - bottom);
        glClear(GL_COLOR_BUFFER_BIT);
    }
}

void draw_scissor_dot(int x, int y, int radius, int target_width, int target_height) {
    const int diameter = radius * 2 + 1;
    clear_scissor_rect(x - radius, y - radius, diameter, diameter,
                       target_width, target_height);
}

void draw_scissor_segment(int x0, int y0, int x1, int y1, int radius,
                          int target_width, int target_height) {
    const int dx = x1 - x0;
    const int dy = y1 - y0;
    const int steps = std::max(1, std::max(std::abs(dx), std::abs(dy)) / 3);
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const int x = static_cast<int>(std::lround(x0 + dx * t));
        const int y = static_cast<int>(std::lround(y0 + dy * t));
        draw_scissor_dot(x, y, radius, target_width, target_height);
    }
}

void draw_controller_hand_proxy(int eye, int width, int height,
                                const VrCameraControls& controls) {
    if (!controls.hand_pointer_enabled || !controls.first_person_hand_hidden ||
        !g_vr_system || width <= 0 || height <= 0) {
        return;
    }

    float hmd[12]{};
    float controller[12]{};
    AcquireSRWLockShared(&g_pose_lock);
    const bool valid = g_hmd_current_valid && g_right_controller_valid;
    if (valid) {
        std::copy(std::begin(g_hmd_current_pose), std::end(g_hmd_current_pose),
                  std::begin(hmd));
        std::copy(std::begin(g_right_controller_pose), std::end(g_right_controller_pose),
                  std::begin(controller));
    }
    ReleaseSRWLockShared(&g_pose_lock);
    if (!valid) return;

    float origin[3]{};
    float right[3]{};
    float up[3]{};
    float forward[3]{};
    float hmd_right[3]{};
    float hmd_up[3]{};
    float hmd_forward[3]{};
    for (int row = 0; row < 3; ++row) {
        origin[row] = controller[row * 4 + 3];
        right[row] = controller[row * 4 + 0];
        up[row] = controller[row * 4 + 1];
        forward[row] = -controller[row * 4 + 2];
        hmd_right[row] = hmd[row * 4 + 0];
        hmd_up[row] = hmd[row * 4 + 1];
        hmd_forward[row] = -hmd[row * 4 + 2];
    }

    float shoulder[3]{};
    float elbow[3]{};
    float wrist[3]{};
    float palm[3]{};
    float tip[3]{};
    float left_guard[3]{};
    float right_guard[3]{};
    for (int row = 0; row < 3; ++row) {
        shoulder[row] = hmd[row * 4 + 3] + hmd_right[row] * 0.18f -
            hmd_up[row] * 0.32f + hmd_forward[row] * 0.03f;
        wrist[row] = origin[row] - forward[row] * 0.055f - up[row] * 0.035f;
        elbow[row] = shoulder[row] * 0.45f + wrist[row] * 0.55f -
            hmd_up[row] * 0.10f + right[row] * 0.035f;
        palm[row] = origin[row] + forward[row] * 0.075f - up[row] * 0.030f;
        tip[row] = origin[row] + forward[row] * 0.205f - up[row] * 0.020f;
        left_guard[row] = palm[row] - right[row] * 0.060f + up[row] * 0.020f;
        right_guard[row] = palm[row] + right[row] * 0.060f + up[row] * 0.020f;
    }

    int shoulder_x = 0, shoulder_y = 0;
    int elbow_x = 0, elbow_y = 0;
    int wrist_x = 0, wrist_y = 0;
    int palm_x = 0, palm_y = 0;
    int tip_x = 0, tip_y = 0;
    int left_x = 0, left_y = 0;
    int right_x = 0, right_y = 0;
    const bool arm_projected =
        project_tracking_point_to_eye(eye, width, height, hmd, shoulder, shoulder_x, shoulder_y) &&
        project_tracking_point_to_eye(eye, width, height, hmd, elbow, elbow_x, elbow_y);
    const bool hand_projected =
        project_tracking_point_to_eye(eye, width, height, hmd, wrist, wrist_x, wrist_y) &&
        project_tracking_point_to_eye(eye, width, height, hmd, palm, palm_x, palm_y) &&
        project_tracking_point_to_eye(eye, width, height, hmd, tip, tip_x, tip_y) &&
        project_tracking_point_to_eye(eye, width, height, hmd, left_guard, left_x, left_y) &&
        project_tracking_point_to_eye(eye, width, height, hmd, right_guard, right_x, right_y);
    if (!hand_projected) return;

    const GLboolean scissor_enabled = glIsEnabled(GL_SCISSOR_TEST);
    GLint old_scissor[4]{};
    GLfloat old_clear[4]{};
    GLboolean old_mask[4]{};
    glGetIntegerv(GL_SCISSOR_BOX, old_scissor);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, old_clear);
    glGetBooleanv(GL_COLOR_WRITEMASK, old_mask);
    glEnable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    const int outline_radius = std::max(3, width / 900);
    const int arm_radius = std::max(3, width / 1050);
    const int hand_radius = std::max(3, width / 1150);
    const int palm_radius = std::max(5, width / 320);

    glClearColor(0.015f, 0.012f, 0.010f, 1.0f);
    if (arm_projected) {
        draw_scissor_segment(shoulder_x, shoulder_y, elbow_x, elbow_y, outline_radius, width, height);
        draw_scissor_segment(elbow_x, elbow_y, wrist_x, wrist_y, outline_radius, width, height);
    }
    draw_scissor_segment(wrist_x, wrist_y, tip_x, tip_y, outline_radius, width, height);
    draw_scissor_segment(left_x, left_y, right_x, right_y, outline_radius, width, height);
    draw_scissor_dot(palm_x, palm_y, palm_radius + 2, width, height);

    glClearColor(0.88f, 0.56f, 0.34f, 1.0f);
    if (arm_projected) {
        draw_scissor_segment(shoulder_x, shoulder_y, elbow_x, elbow_y, arm_radius, width, height);
        draw_scissor_segment(elbow_x, elbow_y, wrist_x, wrist_y, arm_radius, width, height);
    }
    draw_scissor_segment(wrist_x, wrist_y, palm_x, palm_y, hand_radius, width, height);
    draw_scissor_dot(palm_x, palm_y, palm_radius, width, height);

    glClearColor(0.22f, 0.95f, 1.0f, 1.0f);
    draw_scissor_segment(palm_x, palm_y, tip_x, tip_y, hand_radius, width, height);
    draw_scissor_dot(tip_x, tip_y, hand_radius + 1, width, height);

    glColorMask(old_mask[0], old_mask[1], old_mask[2], old_mask[3]);
    glClearColor(old_clear[0], old_clear[1], old_clear[2], old_clear[3]);
    glScissor(old_scissor[0], old_scissor[1], old_scissor[2], old_scissor[3]);
    if (!scissor_enabled) glDisable(GL_SCISSOR_TEST);
}

void draw_vr_hand_model(int eye, int width, int height,
                        const VrCameraControls& controls) {
    if (!controls.first_person_hand_hidden || !g_vr_system ||
        width <= 0 || height <= 0 || !load_hand_model()) {
        return;
    }
    const bool hardware_hand_renderer = ensure_hand_gl_renderer();

    float hmd[12]{};
    float controller_poses[2][12]{};
    bool controller_valid[2]{};
    AcquireSRWLockShared(&g_pose_lock);
    const bool hmd_valid = g_hmd_current_valid;
    if (hmd_valid) {
        std::copy(std::begin(g_hmd_current_pose), std::end(g_hmd_current_pose),
                  std::begin(hmd));
        controller_valid[0] = g_left_controller_valid;
        controller_valid[1] = g_right_controller_valid;
        if (controller_valid[0]) {
            std::copy(std::begin(g_left_controller_pose), std::end(g_left_controller_pose),
                      std::begin(controller_poses[0]));
        }
        if (controller_valid[1]) {
            std::copy(std::begin(g_right_controller_pose), std::end(g_right_controller_pose),
                      std::begin(controller_poses[1]));
        }
    }
    ReleaseSRWLockShared(&g_pose_lock);
    if (!hmd_valid || (!controller_valid[0] && !controller_valid[1])) return;
    const float world_scale = validated_world_scale(controls);
    for (int hand = 0; hand < 2; ++hand) {
        if (!controller_valid[hand]) continue;
        for (int row = 0; row < 3; ++row) {
            const int translation_index = row * 4 + 3;
            controller_poses[hand][translation_index] = hmd[translation_index] +
                (controller_poses[hand][translation_index] - hmd[translation_index]) *
                    world_scale;
        }
    }
    const Vec3 eye_position{hmd[3], hmd[7], hmd[11]};
    const bool shared_hand_item_depth =
        hardware_hand_renderer && begin_hand_item_depth(width, height);

    static thread_local std::vector<ProjectedHandTriangle> triangles;
    triangles.clear();
    const size_t required_triangles = (g_hand_vertices.size() / 3) * 2;
    if (triangles.capacity() < required_triangles) {
        triangles.reserve(required_triangles);
    }
    const Vec3 light{-0.25f, 0.55f, -0.80f};
    for (int hand = 0; hand < 2; ++hand) {
        if (!controller_valid[hand]) continue;
        const float* controller = controller_poses[hand];

        for (size_t index = 0; index + 2 < g_hand_vertices.size(); index += 3) {
            ProjectedHandTriangle triangle{};
            bool valid = true;
            float shade_sum = 0.0f;
            float u_sum = 0.0f;
            float v_sum = 0.0f;
            Vec3 world_center{};
            for (int corner = 0; corner < 3; ++corner) {
                const HandVertex& source = g_hand_vertices[index + corner];
                const Vec3 world = transform_hand_vertex(source, controller, hand == 1, controls);
                world_center.x += world.x;
                world_center.y += world.y;
                world_center.z += world.z;
                float ndc_x = 0.0f;
                float ndc_y = 0.0f;
                float depth = 0.0f;
                if (!project_tracking_point_to_eye_ndc_raw(eye, hmd, world,
                                                           ndc_x, ndc_y, depth)) {
                    valid = false;
                    break;
                }
                const Vec3 normal = transform_hand_normal(source.normal, controller, hand == 1, controls);
                const float lit = std::clamp(
                    normal.x * light.x + normal.y * light.y + normal.z * light.z,
                    -1.0f, 1.0f);
                const float shade = std::clamp(lit * 0.28f + 0.74f, 0.48f, 1.0f);
                triangle.v[corner].x = (ndc_x * 0.5f + 0.5f) * static_cast<float>(width);
                triangle.v[corner].y = (ndc_y * 0.5f + 0.5f) * static_cast<float>(height);
                triangle.v[corner].depth = depth;
                triangle.v[corner].shade = shade;
                triangle.v[corner].u = source.u;
                triangle.v[corner].v = source.v;
                triangle.depth += depth;
                shade_sum += shade;
                u_sum += source.u;
                v_sum += source.v;
            }
            if (!valid) continue;

            world_center.x /= 3.0f;
            world_center.y /= 3.0f;
            world_center.z /= 3.0f;
            const Vec3 eye_to_center{
                world_center.x - eye_position.x,
                world_center.y - eye_position.y,
                world_center.z - eye_position.z};

            const float area =
                (triangle.v[1].x - triangle.v[0].x) * (triangle.v[2].y - triangle.v[0].y) -
                (triangle.v[1].y - triangle.v[0].y) * (triangle.v[2].x - triangle.v[0].x);
            if (!std::isfinite(area) || std::fabs(area) < 1.0f) continue;
            triangle.depth =
                eye_to_center.x * eye_to_center.x +
                eye_to_center.y * eye_to_center.y +
                eye_to_center.z * eye_to_center.z;
            triangle.shade = shade_sum / 3.0f;
            if (!hardware_hand_renderer) {
                sample_hand_texture(u_sum / 3.0f, v_sum / 3.0f, triangle.shade,
                                    triangle.red, triangle.green, triangle.blue);
            }
            triangles.push_back(triangle);
        }
    }

    std::sort(triangles.begin(), triangles.end(),
              [](const ProjectedHandTriangle& a, const ProjectedHandTriangle& b) {
                  return a.depth > b.depth;
              });
    if (hardware_hand_renderer &&
        draw_projected_textured_triangles_gl(
            triangles, width, height, eye, g_hand_texture_gl,
            controls.hand_depth_tolerance)) {
        draw_vr_item_models(
            eye, width, height, hmd, controller_poses, controller_valid,
            controls);
        if (shared_hand_item_depth) end_hand_item_depth();
        return;
    }

    if (hardware_hand_renderer) {
        for (ProjectedHandTriangle& triangle : triangles) {
            const float u = (triangle.v[0].u + triangle.v[1].u + triangle.v[2].u) / 3.0f;
            const float v = (triangle.v[0].v + triangle.v[1].v + triangle.v[2].v) / 3.0f;
            sample_hand_texture(u, v, triangle.shade,
                                triangle.red, triangle.green, triangle.blue);
        }
    }

    const GLboolean scissor_enabled = glIsEnabled(GL_SCISSOR_TEST);
    GLint old_scissor[4]{};
    GLfloat old_clear[4]{};
    GLboolean old_mask[4]{};
    glGetIntegerv(GL_SCISSOR_BOX, old_scissor);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, old_clear);
    glGetBooleanv(GL_COLOR_WRITEMASK, old_mask);
    glEnable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    for (const ProjectedHandTriangle& triangle : triangles) {
        fill_projected_triangle(triangle, width, height);
    }

    glColorMask(old_mask[0], old_mask[1], old_mask[2], old_mask[3]);
    glClearColor(old_clear[0], old_clear[1], old_clear[2], old_clear[3]);
    glScissor(old_scissor[0], old_scissor[1], old_scissor[2], old_scissor[3]);
    if (!scissor_enabled) glDisable(GL_SCISSOR_TEST);
    if (hardware_hand_renderer) {
        draw_vr_item_models(
            eye, width, height, hmd, controller_poses, controller_valid,
            controls);
    }
    if (shared_hand_item_depth) end_hand_item_depth();
}

void draw_controller_pointer(int eye, int width, int height, float distance,
                             float world_scale,
                             bool publish_state = true) {
    int x = 0;
    int y = 0;
    int surface_width = 0;
    int surface_height = 0;
    bool menu_mode = false;
    if (!controller_pointer_pixel(eye, width, height, distance, x, y,
                                  surface_width, surface_height, menu_mode,
                                  false, world_scale)) {
        if (publish_state && eye == 0) publish_pointer(false, 0, 0, 0, 0, false);
        return;
    }

    const GLboolean scissor_enabled = glIsEnabled(GL_SCISSOR_TEST);
    GLint old_scissor[4]{};
    GLfloat old_clear[4]{};
    GLboolean old_mask[4]{};
    glGetIntegerv(GL_SCISSOR_BOX, old_scissor);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, old_clear);
    glGetBooleanv(GL_COLOR_WRITEMASK, old_mask);
    glEnable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glClearColor(0.02f, 0.02f, 0.02f, 1.0f);
    clear_scissor_rect(x - 6, y - 2, 13, 5, width, height);
    clear_scissor_rect(x - 2, y - 6, 5, 13, width, height);
    glClearColor(0.15f, 0.95f, 1.0f, 1.0f);
    clear_scissor_rect(x - 4, y, 9, 1, width, height);
    clear_scissor_rect(x, y - 4, 1, 9, width, height);

    glColorMask(old_mask[0], old_mask[1], old_mask[2], old_mask[3]);
    glClearColor(old_clear[0], old_clear[1], old_clear[2], old_clear[3]);
    glScissor(old_scissor[0], old_scissor[1], old_scissor[2], old_scissor[3]);
    if (!scissor_enabled) glDisable(GL_SCISSOR_TEST);
    if (publish_state && eye == 0) {
        publish_pointer(true, x, y, surface_width, surface_height, menu_mode);
    }
}

void draw_vr_eye_overlays(int eye, int width, int height,
                          const VrCameraControls& controls,
                          int source_x, int source_y,
                          int source_width, int source_height,
                          bool source_multisampled) {
    draw_vr_hand_model(eye, width, height, controls);
    if (controls.hand_pointer_enabled) {
        const bool menu_overlay_active = g_shared && g_shared->ui_overlay_active != 0;
        if (!menu_overlay_active && controls.hide_center_reticle) {
            suppress_center_reticle(source_x, source_y, source_width, source_height,
                                     width, height, source_multisampled);
        }
        const float pointer_distance = menu_overlay_active
            ? controls.ui_overlay_distance
            : controls.hand_pointer_distance;
        const float world_scale = validated_world_scale(controls);
        if (menu_overlay_active) {
            int pointer_x = 0;
            int pointer_y = 0;
            int pointer_surface_width = 0;
            int pointer_surface_height = 0;
            bool pointer_menu_mode = false;
            const bool pointer_valid =
                controller_pointer_pixel(eye, width, height, pointer_distance,
                                         pointer_x, pointer_y,
                                         pointer_surface_width,
                                         pointer_surface_height,
                                         pointer_menu_mode, true, world_scale);
            if (pointer_valid && pointer_menu_mode && eye == 0) {
                publish_pointer(true, pointer_x, pointer_y,
                                pointer_surface_width, pointer_surface_height, true);
            }
            draw_controller_pointer(eye, width, height,
                                    controls.hand_pointer_distance,
                                    world_scale, false);
        } else {
            draw_controller_pointer(eye, width, height, pointer_distance,
                                    world_scale);
        }
    } else if (g_shared) {
        publish_pointer(false, 0, 0, 0, 0, false);
    }
}

bool capture_eye(int eye, const VrCameraControls& controls, bool include_vr_overlays) {
    const bool check_gl_errors = render_logging_enabled();
    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    GLint samples = 0;
    glGetIntegerv(GL_SAMPLES_VALUE, &samples);
    const int source_width = viewport[2];
    const int source_height = viewport[3];
    if (source_width <= 0 || source_height <= 0) return false;
    if (g_shared) {
        g_shared->capture_width = source_width;
        g_shared->capture_height = source_height;
    }
    GLint max_texture_size = 4096;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    const float output_scale = active_vr_output_scale(controls);
    const hytalevr::RenderResolution output_resolution =
        hytalevr::scaled_render_resolution(source_width, source_height,
                                           output_scale, max_texture_size);
    if (output_resolution.width <= 0 || output_resolution.height <= 0) return false;

    if (source_width != g_source_texture_width ||
        source_height != g_source_texture_height ||
        output_resolution.width != g_texture_width ||
        output_resolution.height != g_texture_height) {
        if (g_eye_textures[0] || g_eye_textures[1]) {
            glDeleteTextures(2, g_eye_textures);
            g_eye_textures[0] = g_eye_textures[1] = 0;
        }
        if (g_afw_source_texture) glDeleteTextures(1, &g_afw_source_texture);
        g_afw_source_texture = 0;
        if (g_eye_depth_textures[0] || g_eye_depth_textures[1]) {
            glDeleteTextures(2, g_eye_depth_textures);
            g_eye_depth_textures[0] = g_eye_depth_textures[1] = 0;
        }
        g_eye_valid[0] = g_eye_valid[1] = false;
        g_eye_depth_valid[0] = g_eye_depth_valid[1] = false;
        g_pre_ui_captured[0] = g_pre_ui_captured[1] = false;
        g_source_texture_width = source_width;
        g_source_texture_height = source_height;
        g_texture_width = output_resolution.width;
        g_texture_height = output_resolution.height;
    }

    GLint previous_texture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
    if (!g_afw_source_texture) {
        glGenTextures(1, &g_afw_source_texture);
        glBindTexture(GL_TEXTURE_2D, g_afw_source_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE_VALUE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE_VALUE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, source_width, source_height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    const ULONGLONG capture_log_now = GetTickCount64();
    if (render_logging_enabled() && capture_log_now - g_last_capture_diag_tick >= 1500) {
        GLint read_fbo_for_log = 0;
        GLint draw_fbo_for_log = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_VALUE, &read_fbo_for_log);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_VALUE, &draw_fbo_for_log);
        std::ostringstream out;
        out << "capture begin eye=" << eye
            << " viewport=(" << viewport[0] << "," << viewport[1] << ","
            << source_width << "x" << source_height << ")"
            << " sourceTexture=" << source_width << "x" << source_height
            << " vrOutput=" << g_texture_width << "x" << g_texture_height
            << " resolutionScale=" << output_resolution.scale
            << " samples=" << samples
            << " readFbo=" << read_fbo_for_log
            << " drawFbo=" << draw_fbo_for_log
            << " captureFbo=" << g_capture_fbo
            << " sourceTex=" << g_afw_source_texture
            << " currentEye=" << (g_shared ? g_shared->current_eye : 0)
            << " stereoFrames=" << (g_shared ? g_shared->stereo_frames : 0);
        render_log(out.str());
        g_last_capture_diag_tick = capture_log_now;
    }

    glBindTexture(GL_TEXTURE_2D, g_afw_source_texture);
    if (initialize_gl_capture()) {
        GLint previous_read_fbo = 0;
        GLint previous_draw_fbo = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_VALUE, &previous_read_fbo);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_VALUE, &previous_draw_fbo);
        g_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER_VALUE, g_capture_fbo);
        g_gl_framebuffer_texture_2d(GL_DRAW_FRAMEBUFFER_VALUE, GL_COLOR_ATTACHMENT0_VALUE,
                                    GL_TEXTURE_2D, g_afw_source_texture, 0);
        if (g_gl_check_framebuffer_status(GL_DRAW_FRAMEBUFFER_VALUE) !=
            GL_FRAMEBUFFER_COMPLETE_VALUE) {
            g_gl_bind_framebuffer(GL_READ_FRAMEBUFFER_VALUE, static_cast<GLuint>(previous_read_fbo));
            g_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER_VALUE, static_cast<GLuint>(previous_draw_fbo));
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture));
            if (g_shared) g_shared->capture_error = 2;
            render_log("capture error: capture framebuffer incomplete");
            g_eye_valid[eye] = false;
            return false;
        }
        g_gl_bind_framebuffer(GL_READ_FRAMEBUFFER_VALUE, static_cast<GLuint>(previous_read_fbo));
        if (check_gl_errors) {
            while (glGetError() != GL_NO_ERROR) {}
        }
        g_gl_blit_framebuffer(viewport[0], viewport[1], viewport[0] + source_width,
                              viewport[1] + source_height, 0, 0,
                              source_width, source_height,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
        if (include_vr_overlays) {
            draw_vr_eye_overlays(eye, source_width, source_height, controls,
                                 viewport[0], viewport[1],
                                 source_width, source_height,
                                 samples > 1);
        }
        const GLenum blit_error = check_gl_errors ? glGetError() : GL_NO_ERROR;
        g_gl_bind_framebuffer(GL_READ_FRAMEBUFFER_VALUE, static_cast<GLuint>(previous_read_fbo));
        g_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER_VALUE, static_cast<GLuint>(previous_draw_fbo));
        if (blit_error != GL_NO_ERROR) {
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture));
            if (g_shared) g_shared->capture_error = static_cast<int32_t>(blit_error);
            {
                std::ostringstream out;
                out << "capture error: glBlit/copy GL error=0x" << std::hex << blit_error
                    << std::dec << " eye=" << eye << " source=" << source_width << "x"
                    << source_height << " target=" << source_width << "x"
                    << source_height;
                render_log(out.str());
            }
            g_eye_valid[eye] = false;
            return false;
        }
    } else {
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, viewport[0], viewport[1],
                            source_width, source_height);
    }
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture));
    g_eye_valid[eye] = true;
    if (g_shared) {
        g_shared->capture_error = 0;
    }
    return g_eye_valid[eye];
}

bool capture_native_eye(int eye, const VrCameraControls& controls) {
    if (eye < 0 || eye > 1 || !initialize_gl_capture()) return false;

    GLint viewport[4]{};
    GLint samples = 0;
    GLint max_texture_size = 4096;
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_SAMPLES_VALUE, &samples);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    const int source_width = viewport[2];
    const int source_height = viewport[3];
    if (source_width <= 0 || source_height <= 0) return false;

    const float output_scale = active_vr_output_scale(controls);
    const hytalevr::RenderResolution output_resolution =
        hytalevr::scaled_render_resolution(source_width, source_height,
                                           output_scale, max_texture_size);
    if (output_resolution.width <= 0 || output_resolution.height <= 0) return false;

    if (source_width != g_source_texture_width ||
        source_height != g_source_texture_height ||
        output_resolution.width != g_texture_width ||
        output_resolution.height != g_texture_height) {
        if (g_eye_textures[0] || g_eye_textures[1]) {
            glDeleteTextures(2, g_eye_textures);
            g_eye_textures[0] = g_eye_textures[1] = 0;
        }
        g_eye_valid[0] = g_eye_valid[1] = false;
        g_source_texture_width = source_width;
        g_source_texture_height = source_height;
        g_texture_width = output_resolution.width;
        g_texture_height = output_resolution.height;
    }

    GLint previous_texture = 0;
    GLint previous_read_fbo = 0;
    GLint previous_draw_fbo = 0;
    GLint previous_viewport[4]{};
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_VALUE, &previous_read_fbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_VALUE, &previous_draw_fbo);
    glGetIntegerv(GL_VIEWPORT, previous_viewport);

    if (!g_eye_textures[eye]) {
        glGenTextures(1, &g_eye_textures[eye]);
        glBindTexture(GL_TEXTURE_2D, g_eye_textures[eye]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE_VALUE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE_VALUE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_texture_width, g_texture_height,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    g_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER_VALUE, g_capture_fbo);
    g_gl_framebuffer_texture_2d(GL_DRAW_FRAMEBUFFER_VALUE, GL_COLOR_ATTACHMENT0_VALUE,
                                GL_TEXTURE_2D, g_eye_textures[eye], 0);
    bool success = g_gl_check_framebuffer_status(GL_DRAW_FRAMEBUFFER_VALUE) ==
        GL_FRAMEBUFFER_COMPLETE_VALUE;
    if (success) {
        while (glGetError() != GL_NO_ERROR) {}
        g_gl_bind_framebuffer(GL_READ_FRAMEBUFFER_VALUE,
                              static_cast<GLuint>(previous_read_fbo));
        g_gl_blit_framebuffer(
            viewport[0], viewport[1], viewport[0] + source_width,
            viewport[1] + source_height, 0, 0, g_texture_width, g_texture_height,
            GL_COLOR_BUFFER_BIT, samples > 1 ? GL_NEAREST : GL_LINEAR);
        glViewport(0, 0, g_texture_width, g_texture_height);
        draw_vr_eye_overlays(eye, g_texture_width, g_texture_height, controls,
                             viewport[0], viewport[1], source_width, source_height,
                             samples > 1);
        success = glGetError() == GL_NO_ERROR;
    }

    g_gl_bind_framebuffer(GL_READ_FRAMEBUFFER_VALUE,
                          static_cast<GLuint>(previous_read_fbo));
    g_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER_VALUE,
                          static_cast<GLuint>(previous_draw_fbo));
    glViewport(previous_viewport[0], previous_viewport[1],
               previous_viewport[2], previous_viewport[3]);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture));
    g_eye_valid[eye] = success;
    if (g_shared) {
        g_shared->capture_width = source_width;
        g_shared->capture_height = source_height;
        g_shared->capture_error = success ? 0 : 7;
    }
    return success;
}

int engine_render_pass_complete(void*, int render_world) {
    HookCallbackScope callback_scope;
    VrCameraControls controls{};
    if (!g_engine_render_hook_active ||
        !g_runtime_active.load(std::memory_order_acquire) ||
        !render_world ||
        !read_controls(controls) || !controls.enabled ||
        !controls.stereo_enabled || controls.non_vr_mode) {
        InterlockedExchange(&g_engine_render_pass, 0);
        InterlockedExchange(&g_render_eye, 0);
        return 0;
    }

    const LONG pass = InterlockedCompareExchange(&g_engine_render_pass, 0, 0);
    if (pass == 1) {
        const bool right_captured = capture_native_eye(1, controls);
        const bool pair_ready =
            right_captured && g_eye_valid[0] && g_eye_valid[1];
        InterlockedExchange(&g_engine_stereo_pair_ready, pair_ready ? 1 : 0);
        InterlockedExchange(&g_engine_render_pass, 0);
        InterlockedExchange(&g_render_eye, 0);
        if (render_logging_enabled()) {
            static ULONGLONG last_log_tick = 0;
            const ULONGLONG now = GetTickCount64();
            if (now - last_log_tick >= 1500) {
                std::ostringstream out;
                out << "engine stereo pair complete ready="
                    << (pair_ready ? 1 : 0)
                    << " source=" << g_source_texture_width << "x"
                    << g_source_texture_height << " output="
                    << g_texture_width << "x" << g_texture_height;
                render_log(out.str());
                last_log_tick = now;
            }
        }
        return 0;
    }
    if (pass != 0) {
        InterlockedExchange(&g_engine_render_pass, 0);
        InterlockedExchange(&g_engine_stereo_pair_ready, 0);
        InterlockedExchange(&g_render_eye, 0);
        return 0;
    }

    InterlockedExchange(&g_engine_stereo_pair_ready, 0);
    g_eye_valid[0] = g_eye_valid[1] = false;
    if (!capture_native_eye(0, controls)) {
        InterlockedExchange(&g_render_eye, 0);
        return 0;
    }
    InterlockedExchange(&g_engine_render_pass, 1);
    InterlockedExchange(&g_render_eye, 1);
    if (!apply_cached_engine_eye(1, controls)) {
        InterlockedExchange(&g_engine_render_pass, 0);
        InterlockedExchange(&g_render_eye, 0);
        g_eye_valid[0] = false;
        return 0;
    }
    return 1;
}

bool preserve_afw_source_texture(int captured_eye) {
    return captured_eye >= 0 && captured_eye <= 1 && g_eye_valid[captured_eye] &&
           g_afw_source_texture != 0 && g_source_texture_width > 0 &&
           g_source_texture_height > 0;
}

bool warp_source_to_eye(GLuint source_texture, int target_eye,
                        const VrCameraControls& controls) {
    if (!source_texture || target_eye < 0 || target_eye > 1 ||
        g_texture_width <= 0 || g_texture_height <= 0) {
        return false;
    }
    const SceneDepthTextureSnapshot depth = scene_depth_texture_snapshot();
    const DistortionTextureSnapshot distortion = distortion_texture_snapshot();
    if (!depth.valid || !depth.texture || !initialize_gl_capture() ||
        !ensure_afw_gl_renderer()) {
        return false;
    }
    const int source_width = g_source_texture_width;
    const int source_height = g_source_texture_height;
    constexpr bool use_held_item_depth = false;
    constexpr bool remove_held_item_background = false;
    const int depthToleranceX = (std::max)(8, source_width / 32);
    const int depthToleranceY = (std::max)(8, source_height / 32);
    if (std::abs(depth.width * 2 - source_width) > depthToleranceX ||
        std::abs(depth.height * 2 - source_height) > depthToleranceY) {
        return false;
    }

    Matrix4 source_projection{};
    Matrix4 target_projection{};
    Matrix4 source_to_target_view{};
    AcquireSRWLockShared(&g_camera_lock);
    const bool camera_valid = g_afw_camera_valid;
    source_projection = g_afw_source_projection;
    target_projection = g_afw_eye_projection[target_eye];
    source_to_target_view = g_afw_source_to_eye_view[target_eye];
    ReleaseSRWLockShared(&g_camera_lock);
    if (!camera_valid) return false;
    const uint32_t render_options = effective_render_options(controls);

    Matrix4 inverse_source_projection{};
    Matrix4 inverse_target_projection{};
    if (!hytalevr::inverse(source_projection, inverse_source_projection) ||
        !hytalevr::inverse(target_projection, inverse_target_projection)) {
        return false;
    }

    GLint previous_program = 0;
    GLint previous_vao = 0;
    GLint previous_array_buffer = 0;
    GLint previous_active_texture = 0;
    GLint previous_texture_0 = 0;
    GLint previous_texture_1 = 0;
    GLint previous_texture_2 = 0;
    GLint previous_texture_3 = 0;
    GLint previous_read_fbo = 0;
    GLint previous_draw_fbo = 0;
    GLint previous_viewport[4]{};
    GLboolean previous_color_mask[4]{};
    glGetIntegerv(GL_CURRENT_PROGRAM_VALUE, &previous_program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING_VALUE, &previous_vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING_VALUE, &previous_array_buffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE_VALUE, &previous_active_texture);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_VALUE, &previous_read_fbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_VALUE, &previous_draw_fbo);
    glGetIntegerv(GL_VIEWPORT, previous_viewport);
    glGetBooleanv(GL_COLOR_WRITEMASK, previous_color_mask);
    const GLboolean blend_enabled = glIsEnabled(GL_BLEND);
    const GLboolean depth_enabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean scissor_enabled = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean cull_enabled = glIsEnabled(GL_CULL_FACE);
    g_gl_active_texture(GL_TEXTURE0_VALUE);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture_0);
    g_gl_active_texture(GL_TEXTURE1_VALUE);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture_1);
    g_gl_active_texture(GL_TEXTURE2_VALUE);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture_2);
    g_gl_active_texture(GL_TEXTURE3_VALUE);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture_3);

    g_gl_active_texture(GL_TEXTURE0_VALUE);
    if (!g_eye_textures[target_eye]) {
        glGenTextures(1, &g_eye_textures[target_eye]);
        glBindTexture(GL_TEXTURE_2D, g_eye_textures[target_eye]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE_VALUE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE_VALUE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_texture_width, g_texture_height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    if (!g_eye_depth_textures[target_eye]) {
        glGenTextures(1, &g_eye_depth_textures[target_eye]);
        glBindTexture(GL_TEXTURE_2D, g_eye_depth_textures[target_eye]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE_VALUE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE_VALUE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F_VALUE,
                     g_texture_width, g_texture_height, 0,
                     GL_RED_VALUE, GL_FLOAT, nullptr);
    }
    g_eye_valid[target_eye] = false;
    g_eye_depth_valid[target_eye] = false;

    g_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER_VALUE, g_capture_fbo);
    g_gl_framebuffer_texture_2d(GL_DRAW_FRAMEBUFFER_VALUE, GL_COLOR_ATTACHMENT0_VALUE,
                                GL_TEXTURE_2D, g_eye_textures[target_eye], 0);
    bool success = g_gl_check_framebuffer_status(GL_DRAW_FRAMEBUFFER_VALUE) ==
        GL_FRAMEBUFFER_COMPLETE_VALUE;
    if (success) {
        while (glGetError() != GL_NO_ERROR) {}
        glViewport(0, 0, g_texture_width, g_texture_height);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        g_gl_use_program(g_afw_program);
        g_gl_bind_vertex_array(g_afw_vao);
        g_gl_uniform_1i(g_afw_source_color_uniform, 0);
        g_gl_uniform_1i(g_afw_source_depth_uniform, 1);
        g_gl_uniform_1i(g_afw_distortion_field_uniform, 2);
        g_gl_uniform_1i(g_afw_held_item_depth_uniform, 3);
        g_gl_uniform_1i(g_afw_use_held_item_depth_uniform,
                        use_held_item_depth ? 1 : 0);
        g_gl_uniform_1i(g_afw_remove_held_item_background_uniform,
                        remove_held_item_background ? 1 : 0);
        g_gl_uniform_2f(g_afw_source_texel_size_uniform,
                        1.0f / static_cast<float>(source_width),
                        1.0f / static_cast<float>(source_height));
        g_gl_uniform_1i(g_afw_composite_held_item_uniform, 0);
        g_gl_uniform_1i(g_afw_apply_distortion_uniform, distortion.valid ? 1 : 0);
        g_gl_uniform_1i(
            g_afw_sharpen_enabled_uniform,
            (render_options & kVrEffectSharpening) != 0u ? 1 : 0);
        g_gl_uniform_1f(g_afw_sharpen_strength_uniform,
                        std::clamp(controls.reserved_render_value, 0.0f, 1.0f));
        g_gl_uniform_1i(
            g_afw_edge_aa_enabled_uniform,
            (render_options & kVrEffectEdgeAa) != 0u ? 1 : 0);
        g_gl_uniform_matrix_4fv(g_afw_inverse_source_projection_uniform, 1, GL_FALSE,
                                inverse_source_projection.value);
        g_gl_uniform_matrix_4fv(g_afw_source_projection_uniform, 1, GL_FALSE,
                                source_projection.value);
        g_gl_uniform_matrix_4fv(g_afw_inverse_target_projection_uniform, 1, GL_FALSE,
                                inverse_target_projection.value);
        g_gl_uniform_matrix_4fv(g_afw_source_to_target_view_uniform, 1, GL_FALSE,
                                source_to_target_view.value);
        g_gl_uniform_matrix_4fv(g_afw_target_projection_uniform, 1, GL_FALSE,
                                target_projection.value);
        g_gl_uniform_1f(g_afw_depth_far_clip_uniform, depth.far_clip);
        g_gl_uniform_1i(
            g_afw_enable_warp_uniform,
            (render_options & kVrEffectProjectionCorrection) != 0u ? 2 : 0);
        g_gl_uniform_1i(g_afw_output_depth_uniform, 0);
        g_gl_active_texture(GL_TEXTURE0_VALUE);
        glBindTexture(GL_TEXTURE_2D, source_texture);
        g_gl_active_texture(GL_TEXTURE1_VALUE);
        glBindTexture(GL_TEXTURE_2D, depth.texture);
        g_gl_active_texture(GL_TEXTURE2_VALUE);
        glBindTexture(GL_TEXTURE_2D, distortion.valid ? distortion.texture : 0);
        g_gl_active_texture(GL_TEXTURE3_VALUE);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        success = glGetError() == GL_NO_ERROR;
        if (success) {
            g_gl_framebuffer_texture_2d(GL_DRAW_FRAMEBUFFER_VALUE,
                                        GL_COLOR_ATTACHMENT0_VALUE,
                                        GL_TEXTURE_2D,
                                        g_eye_depth_textures[target_eye], 0);
            const bool depth_complete =
                g_gl_check_framebuffer_status(GL_DRAW_FRAMEBUFFER_VALUE) ==
                GL_FRAMEBUFFER_COMPLETE_VALUE;
            if (depth_complete) {
                g_gl_uniform_1i(g_afw_output_depth_uniform, 1);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                g_eye_depth_valid[target_eye] = glGetError() == GL_NO_ERROR;
                g_gl_uniform_1i(g_afw_output_depth_uniform, 0);
            }
        }
    }

    g_gl_active_texture(GL_TEXTURE3_VALUE);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture_3));
    g_gl_active_texture(GL_TEXTURE2_VALUE);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture_2));
    g_gl_active_texture(GL_TEXTURE1_VALUE);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture_1));
    g_gl_active_texture(GL_TEXTURE0_VALUE);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture_0));
    g_gl_active_texture(static_cast<GLenum>(previous_active_texture));
    g_gl_bind_vertex_array(static_cast<GLuint>(previous_vao));
    g_gl_bind_buffer(GL_ARRAY_BUFFER_VALUE, static_cast<GLuint>(previous_array_buffer));
    g_gl_use_program(static_cast<GLuint>(previous_program));
    g_gl_bind_framebuffer(GL_READ_FRAMEBUFFER_VALUE, static_cast<GLuint>(previous_read_fbo));
    g_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER_VALUE, static_cast<GLuint>(previous_draw_fbo));
    glViewport(previous_viewport[0], previous_viewport[1],
               previous_viewport[2], previous_viewport[3]);
    glColorMask(previous_color_mask[0], previous_color_mask[1],
                previous_color_mask[2], previous_color_mask[3]);
    if (blend_enabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (depth_enabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (scissor_enabled) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (cull_enabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);

    if (success) {
        g_eye_valid[target_eye] = true;
        ++g_afw_warped_frames;
        if (render_logging_enabled() && (g_afw_warped_frames % 90u) == 1u) {
            std::ostringstream out;
            out << "AFW: warped centered source target=" << target_eye
                << " source=" << source_width << "x" << source_height
                << " output=" << g_texture_width << "x" << g_texture_height
                << " depth=" << depth.width << "x" << depth.height
                << " depthFrame=" << depth.frame
                << " distortion=" << (distortion.valid ? 1 : 0)
                << " distortionFrame=" << distortion.frame
                << " itemMask=" << (remove_held_item_background ? 1 : 0);
            render_log(out.str());
        }
    }
    return success;
}

bool project_view_position_to_uv(const Matrix4& projection,
                                 const float position[3],
                                 float uv[2]) {
    float clip[4]{};
    const float point[4]{position[0], position[1], position[2], 1.0f};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            clip[row] += projection.value[column * 4 + row] * point[column];
        }
    }
    if (!std::isfinite(clip[3]) || clip[3] <= 0.0001f) return false;
    uv[0] = clip[0] / clip[3] * 0.5f + 0.5f;
    uv[1] = clip[1] / clip[3] * 0.5f + 0.5f;
    return std::isfinite(uv[0]) && std::isfinite(uv[1]);
}

bool composite_held_item_to_eye(int eye,
                                const VrCameraControls& controls) {
    if (eye < 0 || eye > 1 || !g_eye_valid[eye] ||
        !g_eye_depth_valid[eye] || !g_eye_textures[eye] ||
        !g_eye_depth_textures[eye] || !g_afw_source_texture ||
        !initialize_gl_capture() ||
        !ensure_afw_gl_renderer()) {
        return false;
    }
    const HeldItemDepthTextureSnapshot layer = held_item_depth_texture_snapshot();
    if (!layer.valid ||
        std::abs(layer.width - g_source_texture_width) > 8 ||
        std::abs(layer.height - g_source_texture_height) > 8) {
        return false;
    }

    Matrix4 source_projection{};
    Matrix4 source_to_target_view{};
    Matrix4 target_projection{};
    AcquireSRWLockShared(&g_camera_lock);
    const bool camera_valid = g_afw_camera_valid;
    source_projection = g_afw_source_projection;
    source_to_target_view = g_afw_source_to_eye_view[eye];
    target_projection = g_afw_eye_projection[eye];
    ReleaseSRWLockShared(&g_camera_lock);
    if (!camera_valid) return false;

    Matrix4 inverse_source_projection{};
    if (!hytalevr::inverse(source_projection, inverse_source_projection)) return false;
    const SceneDepthTextureSnapshot scene_depth = scene_depth_texture_snapshot();
    const DistortionTextureSnapshot distortion = distortion_texture_snapshot();
    const float far_clip = scene_depth.valid ? scene_depth.far_clip : 1024.0f;

    GLint previous_program = 0;
    GLint previous_vao = 0;
    GLint previous_array_buffer = 0;
    GLint previous_active_texture = 0;
    GLint previous_texture_0 = 0;
    GLint previous_texture_2 = 0;
    GLint previous_texture_3 = 0;
    GLint previous_texture_4 = 0;
    GLint previous_texture_5 = 0;
    GLint previous_read_fbo = 0;
    GLint previous_draw_fbo = 0;
    GLint previous_viewport[4]{};
    GLint previous_blend_src = GL_ONE;
    GLint previous_blend_dst = GL_ZERO;
    GLboolean previous_color_mask[4]{};
    glGetIntegerv(GL_CURRENT_PROGRAM_VALUE, &previous_program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING_VALUE, &previous_vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING_VALUE, &previous_array_buffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE_VALUE, &previous_active_texture);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_VALUE, &previous_read_fbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_VALUE, &previous_draw_fbo);
    glGetIntegerv(GL_VIEWPORT, previous_viewport);
    glGetIntegerv(GL_BLEND_SRC, &previous_blend_src);
    glGetIntegerv(GL_BLEND_DST, &previous_blend_dst);
    glGetBooleanv(GL_COLOR_WRITEMASK, previous_color_mask);
    const GLboolean blend_enabled = glIsEnabled(GL_BLEND);
    const GLboolean depth_enabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean scissor_enabled = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean cull_enabled = glIsEnabled(GL_CULL_FACE);
    g_gl_active_texture(GL_TEXTURE0_VALUE);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture_0);
    g_gl_active_texture(GL_TEXTURE2_VALUE);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture_2);
    g_gl_active_texture(GL_TEXTURE3_VALUE);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture_3);
    g_gl_active_texture(GL_TEXTURE4_VALUE);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture_4);
    g_gl_active_texture(GL_TEXTURE5_VALUE);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture_5);

    g_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER_VALUE, g_capture_fbo);
    g_gl_framebuffer_texture_2d(GL_DRAW_FRAMEBUFFER_VALUE,
                                GL_COLOR_ATTACHMENT0_VALUE,
                                GL_TEXTURE_2D, g_eye_textures[eye], 0);
    bool success = g_gl_check_framebuffer_status(GL_DRAW_FRAMEBUFFER_VALUE) ==
        GL_FRAMEBUFFER_COMPLETE_VALUE;
    bool drew_item = false;
    if (success) {
        while (glGetError() != GL_NO_ERROR) {}
        glViewport(0, 0, g_texture_width, g_texture_height);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        g_gl_use_program(g_afw_program);
        g_gl_bind_vertex_array(g_afw_vao);
        g_gl_uniform_1i(g_afw_source_color_uniform, 0);
        g_gl_uniform_1i(g_afw_distortion_field_uniform, 2);
        g_gl_uniform_1i(g_afw_held_item_depth_uniform, 3);
        g_gl_uniform_1i(g_afw_eye_scene_depth_uniform, 4);
        g_gl_uniform_1i(g_afw_held_item_color_uniform, 5);
        g_gl_uniform_1i(g_afw_composite_held_item_uniform, 1);
        g_gl_uniform_1i(g_afw_use_held_item_depth_uniform, 0);
        g_gl_uniform_1i(g_afw_remove_held_item_background_uniform, 0);
        g_gl_uniform_2f(g_afw_source_texel_size_uniform,
                        1.0f / static_cast<float>(g_source_texture_width),
                        1.0f / static_cast<float>(g_source_texture_height));
        g_gl_uniform_1i(g_afw_apply_distortion_uniform,
                        distortion.valid ? 1 : 0);
        g_gl_uniform_1i(g_afw_output_depth_uniform, 0);
        g_gl_uniform_1f(g_afw_depth_far_clip_uniform, far_clip);
        g_gl_uniform_1f(g_afw_held_item_depth_bias_uniform,
                        std::clamp(controls.hand_depth_tolerance, 0.0f, 1.0f));
        g_gl_uniform_matrix_4fv(g_afw_inverse_source_projection_uniform, 1,
                                GL_FALSE, inverse_source_projection.value);
        g_gl_uniform_matrix_4fv(g_afw_source_to_target_view_uniform, 1,
                                GL_FALSE, source_to_target_view.value);
        g_gl_uniform_matrix_4fv(g_afw_target_projection_uniform, 1,
                                GL_FALSE, target_projection.value);
        g_gl_active_texture(GL_TEXTURE0_VALUE);
        glBindTexture(GL_TEXTURE_2D, g_afw_source_texture);
        g_gl_active_texture(GL_TEXTURE2_VALUE);
        glBindTexture(GL_TEXTURE_2D, distortion.valid ? distortion.texture : 0);
        g_gl_active_texture(GL_TEXTURE3_VALUE);
        glBindTexture(GL_TEXTURE_2D, layer.depth_texture);
        g_gl_active_texture(GL_TEXTURE4_VALUE);
        glBindTexture(GL_TEXTURE_2D, g_eye_depth_textures[eye]);
        g_gl_active_texture(GL_TEXTURE5_VALUE);
        glBindTexture(GL_TEXTURE_2D, layer.color_texture);

        for (int side = 0; side < 2; ++side) {
            if ((layer.visible_mask & (1L << side)) == 0 ||
                !hytalevr::finite_values(layer.view_positions[side], 3)) {
                continue;
            }
            float source_uv[2]{};
            if (!project_view_position_to_uv(source_projection,
                                             layer.view_positions[side], source_uv)) {
                continue;
            }
            float target_position[3]{
                source_to_target_view.value[12],
                source_to_target_view.value[13],
                source_to_target_view.value[14],
            };
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < 3; ++column) {
                    target_position[row] +=
                        source_to_target_view.value[column * 4 + row] *
                        layer.view_positions[side][column];
                }
            }
            float target_uv[2]{};
            if (!project_view_position_to_uv(target_projection,
                                             target_position, target_uv)) {
                continue;
            }
            const float radius = std::clamp(
                0.24f / std::max(-layer.view_positions[side][2], 0.35f),
                0.20f, 0.45f);
            g_gl_uniform_2f(g_afw_held_item_uv_shift_uniform,
                            target_uv[0] - source_uv[0],
                            target_uv[1] - source_uv[1]);
            g_gl_uniform_2f(g_afw_held_item_source_center_uniform,
                            source_uv[0], source_uv[1]);
            g_gl_uniform_1f(g_afw_held_item_radius_uniform, radius);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            drew_item = true;
        }
        success = glGetError() == GL_NO_ERROR;
        g_gl_uniform_1i(g_afw_composite_held_item_uniform, 0);
    }

    g_gl_active_texture(GL_TEXTURE5_VALUE);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture_5));
    g_gl_active_texture(GL_TEXTURE4_VALUE);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture_4));
    g_gl_active_texture(GL_TEXTURE3_VALUE);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture_3));
    g_gl_active_texture(GL_TEXTURE2_VALUE);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture_2));
    g_gl_active_texture(GL_TEXTURE0_VALUE);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture_0));
    g_gl_active_texture(static_cast<GLenum>(previous_active_texture));
    g_gl_bind_vertex_array(static_cast<GLuint>(previous_vao));
    g_gl_bind_buffer(GL_ARRAY_BUFFER_VALUE, static_cast<GLuint>(previous_array_buffer));
    g_gl_use_program(static_cast<GLuint>(previous_program));
    g_gl_bind_framebuffer(GL_READ_FRAMEBUFFER_VALUE,
                          static_cast<GLuint>(previous_read_fbo));
    g_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER_VALUE,
                          static_cast<GLuint>(previous_draw_fbo));
    glViewport(previous_viewport[0], previous_viewport[1],
               previous_viewport[2], previous_viewport[3]);
    glColorMask(previous_color_mask[0], previous_color_mask[1],
                previous_color_mask[2], previous_color_mask[3]);
    glBlendFunc(static_cast<GLenum>(previous_blend_src),
                static_cast<GLenum>(previous_blend_dst));
    if (blend_enabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (depth_enabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (scissor_enabled) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (cull_enabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    return success && drew_item;
}

bool draw_overlays_to_eye_texture(int eye, const VrCameraControls& controls) {
    if (eye < 0 || eye > 1 || !g_eye_valid[eye] || !g_eye_textures[eye] ||
        !initialize_gl_capture()) {
        return false;
    }
    GLint previous_read_fbo = 0;
    GLint previous_draw_fbo = 0;
    GLint previous_viewport[4]{};
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_VALUE, &previous_read_fbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_VALUE, &previous_draw_fbo);
    glGetIntegerv(GL_VIEWPORT, previous_viewport);
    g_gl_bind_framebuffer(GL_READ_FRAMEBUFFER_VALUE, g_capture_fbo);
    g_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER_VALUE, g_capture_fbo);
    g_gl_framebuffer_texture_2d(GL_DRAW_FRAMEBUFFER_VALUE, GL_COLOR_ATTACHMENT0_VALUE,
                                GL_TEXTURE_2D, g_eye_textures[eye], 0);
    const bool complete = g_gl_check_framebuffer_status(GL_DRAW_FRAMEBUFFER_VALUE) ==
        GL_FRAMEBUFFER_COMPLETE_VALUE;
    if (complete) {
        glViewport(0, 0, g_texture_width, g_texture_height);
        draw_vr_eye_overlays(eye, g_texture_width, g_texture_height, controls,
                             0, 0, g_texture_width, g_texture_height, false);
    }
    g_gl_bind_framebuffer(GL_READ_FRAMEBUFFER_VALUE, static_cast<GLuint>(previous_read_fbo));
    g_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER_VALUE, static_cast<GLuint>(previous_draw_fbo));
    glViewport(previous_viewport[0], previous_viewport[1],
               previous_viewport[2], previous_viewport[3]);
    return complete;
}

vr::EVRCompositorError submit_eye_texture_to_eye(int target_eye_index, int texture_eye_index) {
    if (!g_eye_valid[texture_eye_index]) return vr::VRCompositorError_RequestFailed;
    vr::VRTextureBounds_t bounds{0.0f, 0.0f, 1.0f, 1.0f};
    vr::Texture_t texture{
        reinterpret_cast<void*>(static_cast<uintptr_t>(g_eye_textures[texture_eye_index])),
        vr::TextureType_OpenGL, vr::ColorSpace_Gamma};
    auto* compositor = vr::VRCompositor();
    if (!compositor) return vr::VRCompositorError_RequestFailed;
    const vr::EVREye eye = target_eye_index == 0 ? vr::Eye_Left : vr::Eye_Right;
    const auto error = compositor->Submit(eye, &texture, &bounds);
    if (error != vr::VRCompositorError_None) {
        std::ostringstream out;
        out << "submit eye=" << target_eye_index
            << " texEye=" << texture_eye_index
            << " tex=" << g_eye_textures[texture_eye_index]
            << " error=" << static_cast<int32_t>(error)
            << " validL=" << g_eye_valid[0] << " validR=" << g_eye_valid[1];
        render_log(out.str());
    }
    if (eye == vr::Eye_Left) {
        g_shared->submit_left_error = static_cast<int32_t>(error);
    } else {
        g_shared->submit_right_error = static_cast<int32_t>(error);
    }
    return error;
}

vr::EVRCompositorError submit_eye_texture(int eye_index) {
    return submit_eye_texture_to_eye(eye_index, eye_index);
}

bool menu_visible_recently() {
    if (!ensure_ui_scale_mapping() || !g_ui_scale_shared) {
        return false;
    }

    static LONG last_menu_counter = 0;
    static ULONGLONG last_menu_tick = 0;
    const LONG menu_counter = InterlockedCompareExchange(
        &g_ui_scale_shared->menuLargeDrawCounter, 0, 0);
    const ULONGLONG now = GetTickCount64();

    if (menu_counter != last_menu_counter) {
        last_menu_counter = menu_counter;
        last_menu_tick = now;
    }

    return last_menu_tick != 0 && now - last_menu_tick <= 300;
}

bool submit_eye_pair() {
    if (!g_eye_valid[0] || !g_eye_valid[1]) return false;
    auto* compositor = vr::VRCompositor();
    if (!compositor) {
        ++g_consecutive_submit_failures;
        if (g_consecutive_submit_failures >= 2) {
            reset_eye_capture_resources("missing compositor");
        }
        return false;
    }

    const auto left_error = submit_eye_texture(0);
    const auto right_error = left_error == vr::VRCompositorError_None
        ? submit_eye_texture(1)
        : vr::VRCompositorError_RequestFailed;
    if (left_error != vr::VRCompositorError_None) {
        g_shared->submit_right_error =
            static_cast<int32_t>(vr::VRCompositorError_RequestFailed);
    }
    const bool submitted = left_error == vr::VRCompositorError_None &&
                           right_error == vr::VRCompositorError_None;
    if (submitted) {
        g_consecutive_submit_failures = 0;
        g_last_successful_submit_tick = GetTickCount64();
        compositor->PostPresentHandoff();
        g_shared->stereo_active = 1;
        g_shared->stereo_error = 0;
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->stereo_frames));
        static ULONGLONG last_submit_pair_log = 0;
        const ULONGLONG now = GetTickCount64();
        if (render_logging_enabled() && now - last_submit_pair_log >= 1500) {
            std::ostringstream out;
            const LONG menu_counter = g_ui_scale_shared
                ? InterlockedCompareExchange(&g_ui_scale_shared->menuVisibleCounter, 0, 0)
                : 0;
            const LONG menu_large_counter = g_ui_scale_shared
                ? InterlockedCompareExchange(&g_ui_scale_shared->menuLargeDrawCounter, 0, 0)
                : 0;
            out << "submit pair ok frames=" << g_shared->stereo_frames
                << " texL=" << g_eye_textures[0] << " texR=" << g_eye_textures[1]
                << " menuUiMode=" << (g_shared ? g_shared->ui_overlay_active : 0)
                << " menuCounter=" << menu_counter
                << " menuLarge=" << menu_large_counter
                << " completed=" << g_shared->completed_pairs
                << " currentEye=" << g_shared->current_eye;
            render_log(out.str());
            last_submit_pair_log = now;
        }
    } else {
        ++g_consecutive_submit_failures;
        g_shared->stereo_active = 0;
        g_shared->stereo_error = 3000;
        std::ostringstream out;
        out << "submit pair failed left=" << static_cast<int32_t>(left_error)
            << " right=" << static_cast<int32_t>(right_error)
            << " validL=" << g_eye_valid[0] << " validR=" << g_eye_valid[1]
            << " failures=" << g_consecutive_submit_failures;
        render_log(out.str());
        if (g_consecutive_submit_failures >= 2) {
            reset_eye_capture_resources("submit failure");
        }
    }
    // Never submit one texture again as part of a later pair. This prevents a
    // fresh eye from being displayed beside a stale frame after an error.
    g_eye_valid[0] = false;
    g_eye_valid[1] = false;
    g_pre_ui_captured[0] = g_pre_ui_captured[1] = false;
    return submitted;
}

void restore_neutral_camera(bool force_view = true) {
    if (!force_view) return;
    AcquireSRWLockExclusive(&g_camera_lock);
    constexpr ULONGLONG kBetweenFrameRestoreMaxAgeMs = 250;
    const ULONGLONG now = GetTickCount64();
    auto* camera = static_cast<unsigned char*>(g_primary_camera);
    Matrix4 observed_view{};
    Matrix4 observed_projection{};
    const bool camera_layout_valid =
        read_validated_camera_layout(
            camera, observed_view, observed_projection);
    if (camera_layout_valid && g_center_view_projection_valid &&
        now - g_last_primary_camera_tick < kBetweenFrameRestoreMaxAgeMs) {
        if (g_culling_view_valid) {
            std::memcpy(
                camera + kCameraViewOffset, &g_culling_view,
                sizeof(g_culling_view));
            g_last_written_culling_view = g_culling_view;
            g_last_written_culling_object = g_primary_camera;
            g_last_written_culling_tick = now;
            g_last_written_culling_view_valid = true;
        } else if (g_neutral_view_valid) {
            std::memcpy(
                camera + kCameraViewOffset, &g_neutral_view,
                sizeof(g_neutral_view));
        }
        const Matrix4& between_frame_view_projection =
            g_culling_view_projection_valid ? g_culling_view_projection : g_center_view_projection;
        if (g_culling_projection_valid) {
            std::memcpy(
                camera + kCameraProjectionOffset, &g_culling_projection,
                sizeof(g_culling_projection));
        }
        std::memcpy(
            camera + kCameraViewProjectionOffset,
            &between_frame_view_projection,
            sizeof(between_frame_view_projection));
    }
    ReleaseSRWLockExclusive(&g_camera_lock);
}

void shutdown_stereo() {
    restore_source_supersampling();
    if (g_ui_overlay != vr::k_ulOverlayHandleInvalid) {
        if (auto* overlay = vr::VROverlay()) overlay->HideOverlay(g_ui_overlay);
    }
    g_ui_overlay_world_transform_valid = false;
    g_ui_overlay_recenter_sequence = ~0u;
    g_ui_overlay_anchor_distance = 0.0f;
    if (g_ui_texture) {
        glDeleteTextures(1, &g_ui_texture);
        g_ui_texture = 0;
    }
    if (g_ui_fbo && g_gl_delete_framebuffers) {
        const GLuint framebuffer = g_ui_fbo;
        g_gl_delete_framebuffers(1, &framebuffer);
        g_ui_fbo = 0;
    }
    g_ui_width = g_ui_height = 0;
    g_ui_redirect_active = false;
    if (g_eye_textures[0] || g_eye_textures[1]) {
        glDeleteTextures(2, g_eye_textures);
        g_eye_textures[0] = g_eye_textures[1] = 0;
    }
    if (g_afw_source_texture) {
        glDeleteTextures(1, &g_afw_source_texture);
        g_afw_source_texture = 0;
    }
    if (g_eye_depth_textures[0] || g_eye_depth_textures[1]) {
        glDeleteTextures(2, g_eye_depth_textures);
        g_eye_depth_textures[0] = g_eye_depth_textures[1] = 0;
    }
    if (g_capture_fbo && g_gl_delete_framebuffers) {
        g_gl_delete_framebuffers(1, &g_capture_fbo);
        g_capture_fbo = 0;
    }
    g_eye_valid[0] = g_eye_valid[1] = false;
    g_eye_depth_valid[0] = g_eye_depth_valid[1] = false;
    g_pre_ui_captured[0] = g_pre_ui_captured[1] = false;
    g_texture_width = g_texture_height = 0;
    g_source_texture_width = g_source_texture_height = 0;
    g_consecutive_submit_failures = 0;
    g_last_successful_submit_tick = 0;
    g_last_swap_recenter_sequence = 0;
    if (g_hand_item_depth_texture) {
        glDeleteTextures(1, &g_hand_item_depth_texture);
        g_hand_item_depth_texture = 0;
    }
    if (g_torch_flame_texture_gl) {
        glDeleteTextures(1, &g_torch_flame_texture_gl);
        g_torch_flame_texture_gl = 0;
    }
    g_torch_flame_texture_width = 0;
    g_torch_flame_texture_height = 0;
    g_torch_flame_texture_attempted = false;
    g_hand_item_depth_width = 0;
    g_hand_item_depth_height = 0;
    g_hand_item_depth_active = false;
    for (LoadedVrItemState& state : g_loaded_items) {
        if (state.texture_gl) glDeleteTextures(1, &state.texture_gl);
        state = {};
    }
    for (PendingHeldItemDetection& pending : g_pending_item_detections) {
        pending = {};
    }
    g_held_item_asset_cache.clear();
    g_held_item_cache_sequence = 0;
    g_last_item_detection_frame = -1;
    for (int side = 0; side < 2; ++side) {
        g_held_item_detection_hold_until[side].store(
            0, std::memory_order_relaxed);
        g_held_item_suppressed_until[side].store(
            0, std::memory_order_relaxed);
    }
    g_stable_held_item_presence_mask.store(0, std::memory_order_relaxed);
    InterlockedExchange(&g_engine_render_pass, 0);
    InterlockedExchange(&g_engine_stereo_pair_ready, 0);
    InterlockedExchange(&g_render_eye, 0);
    if (g_vr_system) {
        vr::VR_Shutdown();
        g_vr_system = nullptr;
    }
    AcquireSRWLockExclusive(&g_pose_lock);
    g_hmd_view_delta = identity_matrix();
    g_hmd_origin_valid = false;
    g_hmd_current_valid = false;
    g_floor_height_alignment_sequence = ~0u;
    g_floor_height_offset = 0.0f;
    g_floor_height_world_scale = 1.30f;
    g_native_standing_camera_y = 0.0f;
    g_native_standing_camera_y_valid = false;
    g_last_sneak_height_compensation = 0.0f;
    g_left_controller_valid = false;
    g_right_controller_valid = false;
    reset_physical_tracking_locked();
    ReleaseSRWLockExclusive(&g_pose_lock);
    AcquireSRWLockExclusive(&g_camera_lock);
    g_neutral_view_valid = false;
    g_center_view_projection_valid = false;
    g_culling_view_projection_valid = false;
    g_culling_projection_valid = false;
    g_culling_view_valid = false;
    g_last_native_camera_view_valid = false;
    g_last_written_culling_view_valid = false;
    g_last_native_camera_object = nullptr;
    g_last_written_culling_object = nullptr;
    g_afw_camera_valid = false;
    ReleaseSRWLockExclusive(&g_camera_lock);
    g_aggressive_native_view_valid = false;
    g_last_observed_native_yaw_valid = false;
    g_aggressive_suppressed_snaps = 0;
    g_last_aggressive_native_delta = 0.0f;
    g_runtime_active.store(false, std::memory_order_release);
    g_actions_ready = false;
    g_action_set = vr::k_ulInvalidActionSetHandle;
    g_action_move = vr::k_ulInvalidActionHandle;
    g_action_turn = vr::k_ulInvalidActionHandle;
    g_action_jump = vr::k_ulInvalidActionHandle;
    g_action_sprint = vr::k_ulInvalidActionHandle;
    g_action_left_pose = vr::k_ulInvalidActionHandle;
    g_action_right_pose = vr::k_ulInvalidActionHandle;
    g_action_right_trigger = vr::k_ulInvalidActionHandle;
    g_action_left_trigger = vr::k_ulInvalidActionHandle;
    g_action_button_x = vr::k_ulInvalidActionHandle;
    g_action_button_y = vr::k_ulInvalidActionHandle;
    g_action_button_b = vr::k_ulInvalidActionHandle;
    g_action_left_grip = vr::k_ulInvalidActionHandle;
    g_action_right_grip = vr::k_ulInvalidActionHandle;
    g_action_right_stick_click = vr::k_ulInvalidActionHandle;
    g_right_trigger_pressed = false;
    g_left_trigger_pressed = false;
    g_right_attack_suppresses_left_item = false;
    g_left_attack_suppresses_right_item = false;
    g_left_use_locks_placeable_to_right = false;
    g_left_grip_pressed = false;
    g_right_grip_pressed = false;
    AcquireSRWLockExclusive(&g_game_ray_lock);
    g_controller_game_ray_valid = false;
    g_controller_game_ray_physical_active = false;
    ReleaseSRWLockExclusive(&g_game_ray_lock);
    if (g_shared) {
        g_shared->stereo_active = 0;
        g_shared->current_eye = 0;
        g_shared->controller_active = 0;
        g_shared->controller_move_x = 0.0f;
        g_shared->controller_move_y = 0.0f;
        g_shared->controller_turn_x = 0.0f;
        g_shared->controller_turn_y = 0.0f;
        g_shared->controller_jump = 0;
        g_shared->controller_sprint = 0;
        g_shared->controller_right_pose_active = 0;
        g_shared->controller_right_trigger = 0.0f;
        g_shared->controller_right_trigger_pressed = 0;
        g_shared->controller_left_trigger = 0.0f;
        g_shared->controller_left_trigger_pressed = 0;
        g_shared->controller_button_x = 0;
        g_shared->controller_button_y = 0;
        g_shared->controller_button_b = 0;
        g_shared->controller_left_grip = 0;
        g_shared->controller_right_grip = 0;
        g_shared->controller_right_stick_click = 0;
        g_shared->controller_left_button_mask = 0;
        g_shared->controller_right_button_mask = 0;
        g_shared->controller_ray_active = 0;
        publish_pointer(false, 0, 0, 0, 0, false);
        g_shared->effects_stabilized = 0;
        g_shared->ui_overlay_active = 0;
    }
}

void __cdecl hook_sdl_gl_swap_window(void* window) {
    HookCallbackScope callback_scope;
    if (g_shared) InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->swap_calls));
    if (ensure_ui_scale_mapping() && g_ui_scale_shared) {
        InterlockedIncrement(&g_ui_scale_shared->renderFrameSequence);
    }
    VrCameraControls controls{};
    const bool have_controls = read_controls(controls);
    const ULONGLONG swap_log_now = GetTickCount64();
    if (render_logging_enabled() && swap_log_now - g_last_swap_diag_tick >= 1500) {
        GLint viewport[4]{};
        GLint read_fbo = 0;
        GLint draw_fbo = 0;
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_VALUE, &read_fbo);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_VALUE, &draw_fbo);
        std::ostringstream out;
        out << "swap controls=" << (have_controls ? 1 : 0)
            << " enabled=" << (have_controls ? controls.enabled : 0)
            << " stereo=" << (have_controls ? controls.stereo_enabled : 0)
            << " runtime="
            << (g_runtime_active.load(std::memory_order_acquire) ? 1 : 0)
            << " renderEye=" << InterlockedCompareExchange(&g_render_eye, 0, 0)
            << " swapCalls=" << (g_shared ? g_shared->swap_calls : 0)
            << " stereoFrames=" << (g_shared ? g_shared->stereo_frames : 0)
            << " completed=" << (g_shared ? g_shared->completed_pairs : 0)
            << " primary=0x" << std::hex
            << reinterpret_cast<uintptr_t>(g_primary_camera)
            << std::dec
            << " viewport=(" << viewport[0] << "," << viewport[1] << ","
            << viewport[2] << "x" << viewport[3] << ")"
            << " readFbo=" << read_fbo << " drawFbo=" << draw_fbo
            << " eyeValid=" << g_eye_valid[0] << "/" << g_eye_valid[1]
            << " preUi=" << g_pre_ui_captured[0] << "/" << g_pre_ui_captured[1]
            << " capErr=" << (g_shared ? g_shared->capture_error : 0)
            << " submit=" << (g_shared ? g_shared->submit_left_error : 0)
            << "/" << (g_shared ? g_shared->submit_right_error : 0);
        render_log(out.str());
        g_last_swap_diag_tick = swap_log_now;
    }
    if (have_controls && controls.enabled &&
        controls.recenter_sequence != g_last_swap_recenter_sequence) {
        g_last_swap_recenter_sequence = controls.recenter_sequence;
        g_aggressive_native_view_valid = false;
        g_last_observed_native_yaw_valid = false;
        g_last_native_camera_view_valid = false;
        g_last_written_culling_view_valid = false;
        g_last_native_camera_object = nullptr;
        g_last_written_culling_object = nullptr;
        g_last_diag_valid = false;
        reset_eye_capture_resources("recenter sequence");
    }
    if (have_controls && controls.unload_requested) {
        restore_all_hooks(true);
        if (g_swap_trampoline) g_swap_trampoline(window);
        return;
    }
    if (have_controls && (!controls.enabled || controls.shutdown_requested)) {
        publish_ui_scale_neutral();
        restore_neutral_camera(true);
        if (g_runtime_active.load(std::memory_order_acquire)) shutdown_stereo();
    } else if (have_controls && controls.enabled && controls.non_vr_mode) {
        publish_render_filters_only(controls);
        if (g_runtime_active.load(std::memory_order_acquire)) shutdown_stereo();
        if (g_shared) {
            g_shared->stereo_active = 0;
            g_shared->stereo_error = 0;
        }
        if (g_swap_trampoline) g_swap_trampoline(window);
        return;
    } else if (have_controls && controls.enabled && initialize_stereo()) {
        g_runtime_active.store(true, std::memory_order_release);
        update_vr_actions();
        if (!controls.stereo_enabled) {
            synchronize_openvr(controls);
            g_shared->stereo_active = 0;
            if (g_swap_trampoline) g_swap_trampoline(window);
            return;
        }
        const ULONGLONG now = GetTickCount64();
        if (g_last_successful_submit_tick != 0 &&
            now - g_last_successful_submit_tick > 3000 &&
            (g_eye_valid[0] || g_eye_valid[1] ||
             InterlockedCompareExchange(&g_render_eye, 0, 0) != 0)) {
            reset_eye_capture_resources("submit watchdog");
            g_consecutive_submit_failures = 0;
            g_last_successful_submit_tick = now;
        }
        if (!prepare_render_resolution(window, controls)) {
            restore_neutral_camera();
            if (g_swap_trampoline) g_swap_trampoline(window);
            return;
        }
        const bool engine_pair_ready =
            g_engine_render_hook_active &&
            InterlockedExchange(&g_engine_stereo_pair_ready, 0) != 0 &&
            g_eye_valid[0] && g_eye_valid[1];
        InterlockedExchange(&g_render_eye, 0);
        g_shared->current_eye = 0;
        restore_neutral_camera();
        if (engine_pair_ready) {
            submit_menu_overlay_texture(g_eye_textures[0], controls);
            if (submit_eye_pair()) {
                synchronize_openvr(controls);
                InterlockedIncrement(
                    reinterpret_cast<volatile LONG*>(&g_shared->completed_pairs));
            } else {
                synchronize_openvr(controls);
            }
        } else {
            constexpr int source_eye = 0;
            const bool captured = g_pre_ui_captured[source_eye] ||
                                  capture_eye(source_eye, controls, false);
            g_pre_ui_captured[source_eye] = false;
            if (captured && preserve_afw_source_texture(source_eye)) {
                const bool left_warped =
                    warp_source_to_eye(g_afw_source_texture, 0, controls);
                const bool right_warped =
                    warp_source_to_eye(g_afw_source_texture, 1, controls);
                if (left_warped && right_warped) {
                    draw_overlays_to_eye_texture(0, controls);
                    draw_overlays_to_eye_texture(1, controls);
                    submit_menu_overlay_texture(g_eye_textures[0], controls);
                }
                if (left_warped && right_warped && submit_eye_pair()) {
                    synchronize_openvr(controls);
                    InterlockedIncrement(
                        reinterpret_cast<volatile LONG*>(&g_shared->completed_pairs));
                } else {
                    g_eye_valid[0] = g_eye_valid[1] = false;
                    g_eye_depth_valid[0] = g_eye_depth_valid[1] = false;
                    synchronize_openvr(controls);
                }
            } else {
                g_eye_valid[0] = false;
                g_eye_valid[1] = false;
                g_eye_depth_valid[0] = g_eye_depth_valid[1] = false;
                g_pre_ui_captured[0] = g_pre_ui_captured[1] = false;
                synchronize_openvr(controls);
            }
        }
    } else if (g_shared) {
        g_shared->stereo_active = 0;
    }
    if (g_swap_trampoline) g_swap_trampoline(window);
}

bool install_swap_hook() {
    HMODULE sdl = GetModuleHandleW(L"SDL3.dll");
    if (!sdl) return false;
    g_swap_target = reinterpret_cast<unsigned char*>(GetProcAddress(sdl, "SDL_GL_SwapWindow"));
    if (!g_swap_target) return false;
    g_sdl_get_window_size_in_pixels = reinterpret_cast<SdlGetWindowSizeFn>(
        GetProcAddress(sdl, "SDL_GetWindowSizeInPixels"));
    g_sdl_get_window_size = reinterpret_cast<SdlGetWindowSizeFn>(
        GetProcAddress(sdl, "SDL_GetWindowSize"));
    g_sdl_set_window_size = reinterpret_cast<SdlSetWindowSizeFn>(
        GetProcAddress(sdl, "SDL_SetWindowSize"));
    g_sdl_get_window_flags = reinterpret_cast<SdlGetWindowFlagsFn>(
        GetProcAddress(sdl, "SDL_GetWindowFlags"));
    g_sdl_sync_window = reinterpret_cast<SdlSyncWindowFn>(
        GetProcAddress(sdl, "SDL_SyncWindow"));

    // SDL3's exported wrapper is: mov rax,[rip+disp32]; jmp [rip+disp32].
    if (g_swap_target[0] != 0x48 || g_swap_target[1] != 0x8B || g_swap_target[2] != 0x05 ||
        g_swap_target[7] != 0x48 || g_swap_target[8] != 0xFF || g_swap_target[9] != 0x25) {
        g_swap_target = nullptr;
        return false;
    }
    std::memcpy(g_swap_original.data(), g_swap_target, g_swap_original.size());
    int32_t data_disp = 0;
    int32_t dispatcher_disp = 0;
    std::memcpy(&data_disp, g_swap_target + 3, sizeof(data_disp));
    std::memcpy(&dispatcher_disp, g_swap_target + 10, sizeof(dispatcher_disp));
    void* data_slot = g_swap_target + 7 + data_disp;
    void* dispatcher_slot = g_swap_target + 14 + dispatcher_disp;

    auto* trampoline = reinterpret_cast<unsigned char*>(g_swap_trampoline);
    const bool allocated_trampoline = trampoline == nullptr;
    if (allocated_trampoline) {
        trampoline = static_cast<unsigned char*>(
            VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!trampoline) return false;
        unsigned char* p = trampoline;
        *p++ = 0x49; *p++ = 0xBB;
        emit8(p, reinterpret_cast<uint64_t>(data_slot));
        *p++ = 0x49; *p++ = 0x8B; *p++ = 0x03;
        *p++ = 0x49; *p++ = 0xBB;
        emit8(p, reinterpret_cast<uint64_t>(dispatcher_slot));
        *p++ = 0x41; *p++ = 0xFF; *p++ = 0x23;
        g_swap_trampoline = reinterpret_cast<SdlGlSwapWindowFn>(trampoline);
    }

    DWORD old_protect = 0;
    SuspendedProcessThreads suspended_threads;
    if (!VirtualProtect(g_swap_target, 14, PAGE_EXECUTE_READWRITE, &old_protect)) {
        if (allocated_trampoline) {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            g_swap_trampoline = nullptr;
        }
        g_swap_target = nullptr;
        return false;
    }
    if (std::memcmp(g_swap_target, g_swap_original.data(), g_swap_original.size()) != 0) {
        DWORD unused = 0;
        VirtualProtect(g_swap_target, g_swap_original.size(), old_protect, &unused);
        render_log("SDL swap hook site changed before patching; installation aborted");
        if (allocated_trampoline) {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            g_swap_trampoline = nullptr;
        }
        g_swap_target = nullptr;
        return false;
    }
    g_swap_target[0] = 0x48;
    g_swap_target[1] = 0xB8;
    *reinterpret_cast<void**>(g_swap_target + 2) = reinterpret_cast<void*>(&hook_sdl_gl_swap_window);
    g_swap_target[10] = 0xFF;
    g_swap_target[11] = 0xE0;
    g_swap_target[12] = 0x90;
    g_swap_target[13] = 0x90;
    DWORD unused = 0;
    VirtualProtect(g_swap_target, 14, old_protect, &unused);
    FlushInstructionCache(GetCurrentProcess(), g_swap_target, 14);
    return true;
}

bool install_noesis_export_hook(unsigned char* target, std::array<unsigned char, 12>& original,
                                void*& memory, NoesisRenderFn& trampoline,
                                void* hook, size_t original_size) {
    if (!target || original_size > original.size()) return false;
    if (target[0] != 0x48 || target[1] != 0x8B || target[2] != 0x01) return false;
    std::memcpy(original.data(), target, original.size());

    auto* code = static_cast<unsigned char*>(memory);
    if (!code) {
        code = static_cast<unsigned char*>(
            VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!code) return false;
        memory = code;
        std::memcpy(code, target, original_size);
        std::memset(code + original_size, 0xCC, 32 - original_size);
        trampoline = reinterpret_cast<NoesisRenderFn>(code);
    }

    DWORD old_protect = 0;
    SuspendedProcessThreads suspended_threads;
    if (!VirtualProtect(target, original.size(), PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }
    target[0] = 0x48;
    target[1] = 0xB8;
    *reinterpret_cast<void**>(target + 2) = hook;
    target[10] = 0xFF;
    target[11] = 0xE0;
    DWORD unused = 0;
    VirtualProtect(target, original.size(), old_protect, &unused);
    FlushInstructionCache(GetCurrentProcess(), target, original.size());
    return true;
}

bool install_noesis_any_hook(unsigned char* target, std::array<unsigned char, 16>& original,
                             void*& memory, NoesisAnyFn& trampoline,
                             void* hook, size_t original_size) {
    if (!target || original_size > original.size() || original_size < 12) return false;
    std::memcpy(original.data(), target, original.size());

    auto* code = static_cast<unsigned char*>(memory);
    if (!code) {
        code = static_cast<unsigned char*>(
            VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!code) return false;
        memory = code;
        unsigned char* p = code;
        std::memcpy(p, target, original_size);
        p += original_size;
        *p++ = 0x48;
        *p++ = 0xB8;
        emit8(p, reinterpret_cast<uint64_t>(target + original_size));
        *p++ = 0xFF;
        *p++ = 0xE0;
        std::memset(p, 0xCC, static_cast<size_t>(64 - (p - code)));
        trampoline = reinterpret_cast<NoesisAnyFn>(code);
    }

    DWORD old_protect = 0;
    SuspendedProcessThreads suspended_threads;
    if (!VirtualProtect(target, original.size(), PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }
    target[0] = 0x48;
    target[1] = 0xB8;
    *reinterpret_cast<void**>(target + 2) = hook;
    target[10] = 0xFF;
    target[11] = 0xE0;
    for (size_t i = 12; i < original_size; ++i) target[i] = 0x90;
    DWORD unused = 0;
    VirtualProtect(target, original.size(), old_protect, &unused);
    FlushInstructionCache(GetCurrentProcess(), target, original.size());
    return true;
}

bool install_noesis_hooks() {
    HMODULE noesis = GetModuleHandleW(L"Noesis.dll");
    if (!noesis) return false;
    g_noesis_renderer_target = reinterpret_cast<unsigned char*>(
        GetProcAddress(noesis, "Noesis_Renderer_Render"));
    if (!g_noesis_renderer_target) return false;
    if (!install_noesis_any_hook(g_noesis_renderer_target, g_noesis_renderer_original,
                                 g_noesis_renderer_memory, g_noesis_renderer_trampoline,
                                 reinterpret_cast<void*>(&hook_noesis_renderer_render), 15)) {
        g_noesis_renderer_target = nullptr;
        return false;
    }
    g_noesis_hooks_active = true;
    return true;
}

void emit8(unsigned char*& cursor, uint64_t value) {
    std::memcpy(cursor, &value, sizeof(value));
    cursor += sizeof(value);
}

void* allocate_executable_near(void* target, size_t size) {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const uintptr_t granularity = info.dwAllocationGranularity;
    const uintptr_t center =
        reinterpret_cast<uintptr_t>(target) & ~(granularity - 1);
    constexpr uintptr_t kMaximumDistance = 0x70000000ull;
    const uintptr_t minimum = reinterpret_cast<uintptr_t>(
        info.lpMinimumApplicationAddress);
    const uintptr_t maximum = reinterpret_cast<uintptr_t>(
        info.lpMaximumApplicationAddress);

    for (uintptr_t distance = 0; distance <= kMaximumDistance;
         distance += granularity) {
        const uintptr_t candidates[2]{
            center >= distance ? center - distance : 0,
            center <= maximum - distance ? center + distance : 0,
        };
        for (const uintptr_t candidate : candidates) {
            if (candidate < minimum || candidate > maximum - size) continue;
            if (void* memory = VirtualAlloc(
                    reinterpret_cast<void*>(candidate), size,
                    MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)) {
                const intptr_t delta =
                    reinterpret_cast<unsigned char*>(memory) -
                    (static_cast<unsigned char*>(target) + 5);
                if (delta >= INT32_MIN && delta <= INT32_MAX) return memory;
                VirtualFree(memory, 0, MEM_RELEASE);
            }
        }
    }
    return nullptr;
}

bool engine_render_patch_matches() {
    if (!g_engine_render_hook_target || !g_engine_render_hook_memory ||
        !g_engine_render_final_stage_target ||
        !g_engine_render_final_stage_stub) {
        return false;
    }
    if (g_engine_render_hook_target[0] != 0xE9 ||
        g_engine_render_hook_target[5] != 0x90 ||
        g_engine_render_hook_target[6] != 0x90) {
        return false;
    }
    int32_t displacement = 0;
    std::memcpy(&displacement, g_engine_render_hook_target + 1,
                sizeof(displacement));
    if (g_engine_render_hook_target + 5 + displacement !=
        g_engine_render_hook_memory) {
        return false;
    }
    if (g_engine_render_final_stage_target[0] != 0xE9) return false;
    std::memcpy(&displacement, g_engine_render_final_stage_target + 1,
                sizeof(displacement));
    return g_engine_render_final_stage_target + 5 + displacement ==
        g_engine_render_final_stage_stub;
}

void clear_engine_render_hook_sites() {
    g_engine_render_hook_target = nullptr;
    g_engine_render_loop_target = nullptr;
    g_engine_render_final_stage_target = nullptr;
    g_engine_render_final_stage_stub = nullptr;
}

bool install_engine_render_hook() {
    if (g_engine_render_hook_target) {
        const bool installed = engine_render_patch_matches();
        g_engine_render_hook_active = installed;
        return installed;
    }
    constexpr const char* completion_patterns[] = {
        "C6 83 CE 02 00 00 01 48 8B 4B 28 39 09 E8 ?? ?? ?? ?? 90",
        "C6 83 ?? ?? 00 00 01 48 8B 4B 28 39 09 E8 ?? ?? ?? ?? 90",
    };
    constexpr const char* loop_patterns[] = {
        "48 8B 4B 20 48 8B 49 38 BA 0E 00 00 00 39 09 "
        "E8 ?? ?? ?? ?? 48 8B CB E8 ?? ?? ?? ??",
    };
    g_engine_render_hook_target = find_hook_target_by_patterns(
        "engine render completion", 0x3E65F4, completion_patterns,
        sizeof(completion_patterns) / sizeof(completion_patterns[0]), nullptr);
    g_engine_render_loop_target = find_hook_target_by_patterns(
        "engine render loop", 0x3E6548, loop_patterns,
        sizeof(loop_patterns) / sizeof(loop_patterns[0]), nullptr);
    if (!g_engine_render_hook_target || !g_engine_render_loop_target) {
        clear_engine_render_hook_sites();
        return false;
    }
    g_engine_render_final_stage_target = g_engine_render_hook_target - 0x3D;

    const unsigned char expected_original[7]{
        0xC6, 0x83, 0xCE, 0x02, 0x00, 0x00, 0x01};
    if (std::memcmp(g_engine_render_hook_target, expected_original,
                    sizeof(expected_original)) != 0) {
        render_log("engine render hook rejected unexpected completion bytes");
        clear_engine_render_hook_sites();
        return false;
    }
    const unsigned char expected_final_stage_branch[5]{
        0x40, 0x84, 0xF6, 0x74, 0x4A,
    };
    if (std::memcmp(g_engine_render_final_stage_target,
                    expected_final_stage_branch,
                    sizeof(expected_final_stage_branch)) != 0) {
        render_log("engine render hook rejected unexpected final-stage branch");
        clear_engine_render_hook_sites();
        return false;
    }
    const unsigned char expected_epilogue[15]{
        0x90,
        0x0F, 0x28, 0x74, 0x24, 0x40,
        0x48, 0x83, 0xC4, 0x58,
        0x5B, 0x5D, 0x5E, 0x5F, 0xC3,
    };
    if (std::memcmp(g_engine_render_hook_target + 0x12,
                    expected_epilogue, sizeof(expected_epilogue)) != 0) {
        render_log("engine render hook rejected unexpected stack layout");
        clear_engine_render_hook_sites();
        return false;
    }
    std::memcpy(g_engine_render_original.data(), g_engine_render_hook_target,
                g_engine_render_original.size());
    std::memcpy(g_engine_render_final_stage_original.data(),
                g_engine_render_final_stage_target,
                g_engine_render_final_stage_original.size());

    auto* code = static_cast<unsigned char*>(g_engine_render_hook_memory);
    if (!code) {
        code = static_cast<unsigned char*>(
            allocate_executable_near(g_engine_render_hook_target, 512));
        if (!code) {
            render_log("engine render hook could not allocate a near trampoline");
            clear_engine_render_hook_sites();
            return false;
        }
        g_engine_render_hook_memory = code;
    }

    const auto emit_relative_jump =
        [](unsigned char*& cursor, const unsigned char* target) {
            unsigned char* instruction = cursor;
            *cursor++ = 0xE9;
            const intptr_t delta = target - (instruction + 5);
            const int32_t delta32 = static_cast<int32_t>(delta);
            std::memcpy(cursor, &delta32, sizeof(delta32));
            cursor += sizeof(delta32);
        };

    unsigned char* p = code;
    const unsigned char save_prefix[] = {
        0x9C, 0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53,
        0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00,
        0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x20,
        0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x30,
        0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x40,
        0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x50,
        0xF3, 0x0F, 0x7F, 0x64, 0x24, 0x60,
        0xF3, 0x0F, 0x7F, 0x6C, 0x24, 0x70,
        0x48, 0x8B, 0xCB,
        0x8B, 0xD6,
        0x48, 0xB8,
    };
    std::memcpy(p, save_prefix, sizeof(save_prefix));
    p += sizeof(save_prefix);
    emit8(p, reinterpret_cast<uint64_t>(&engine_render_pass_complete));
    const unsigned char call_and_restore[] = {
        0xFF, 0xD0,
        0xF3, 0x0F, 0x6F, 0x44, 0x24, 0x20,
        0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x30,
        0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x40,
        0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x50,
        0xF3, 0x0F, 0x6F, 0x64, 0x24, 0x60,
        0xF3, 0x0F, 0x6F, 0x6C, 0x24, 0x70,
        0x48, 0x81, 0xC4, 0x80, 0x00, 0x00, 0x00,
        0x85, 0xC0, 0x0F, 0x85,
    };
    std::memcpy(p, call_and_restore, sizeof(call_and_restore));
    p += sizeof(call_and_restore);
    unsigned char* loop_branch_displacement = p;
    p += sizeof(int32_t);

    const unsigned char restore_registers[] = {
        0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58,
        0x5A, 0x59, 0x58, 0x9D,
    };
    std::memcpy(p, restore_registers, sizeof(restore_registers));
    p += sizeof(restore_registers);
    std::memcpy(p, g_engine_render_original.data(),
                g_engine_render_original.size());
    p += g_engine_render_original.size();
    emit_relative_jump(p, g_engine_render_hook_target + 7);

    unsigned char* loop_path = p;
    std::memcpy(p, restore_registers, sizeof(restore_registers));
    p += sizeof(restore_registers);
    emit_relative_jump(p, g_engine_render_loop_target);

    g_engine_render_final_stage_stub = p;
    const unsigned char final_stage_prefix[] = {
        0x9C,
        0x50,
        0x48, 0xB8,
    };
    std::memcpy(p, final_stage_prefix, sizeof(final_stage_prefix));
    p += sizeof(final_stage_prefix);
    emit8(p, reinterpret_cast<uint64_t>(&g_engine_render_pass));
    const unsigned char compare_second_pass[] = {
        0x83, 0x38, 0x01,
        0x0F, 0x84,
    };
    std::memcpy(p, compare_second_pass, sizeof(compare_second_pass));
    p += sizeof(compare_second_pass);
    unsigned char* second_pass_displacement = p;
    p += sizeof(int32_t);
    const unsigned char restore_and_test_world[] = {
        0x58,
        0x9D,
        0x40, 0x84, 0xF6,
        0x0F, 0x84,
    };
    std::memcpy(p, restore_and_test_world, sizeof(restore_and_test_world));
    p += sizeof(restore_and_test_world);
    unsigned char* skip_world_displacement = p;
    p += sizeof(int32_t);
    emit_relative_jump(p, g_engine_render_final_stage_target + 5);

    unsigned char* second_pass_path = p;
    *p++ = 0x58;
    *p++ = 0x9D;
    emit_relative_jump(p, g_engine_render_hook_target);

    unsigned char* skip_world_path = p;
    emit_relative_jump(p, g_engine_render_hook_target + 0x12);

    const intptr_t second_pass_delta =
        second_pass_path -
        (second_pass_displacement + sizeof(int32_t));
    const int32_t second_pass_delta32 =
        static_cast<int32_t>(second_pass_delta);
    std::memcpy(second_pass_displacement, &second_pass_delta32,
                sizeof(second_pass_delta32));
    const intptr_t skip_world_delta =
        skip_world_path -
        (skip_world_displacement + sizeof(int32_t));
    const int32_t skip_world_delta32 =
        static_cast<int32_t>(skip_world_delta);
    std::memcpy(skip_world_displacement, &skip_world_delta32,
                sizeof(skip_world_delta32));

    const intptr_t loop_delta =
        loop_path - (loop_branch_displacement + sizeof(int32_t));
    const int32_t loop_delta32 = static_cast<int32_t>(loop_delta);
    std::memcpy(loop_branch_displacement, &loop_delta32,
                sizeof(loop_delta32));
    if (static_cast<size_t>(p - code) > 512) {
        render_log("engine render hook trampoline overflow");
        clear_engine_render_hook_sites();
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), code,
                          static_cast<SIZE_T>(p - code));

    const intptr_t patch_delta =
        code - (g_engine_render_hook_target + 5);
    const intptr_t final_stage_patch_delta =
        g_engine_render_final_stage_stub -
        (g_engine_render_final_stage_target + 5);
    if (patch_delta < INT32_MIN || patch_delta > INT32_MAX ||
        final_stage_patch_delta < INT32_MIN ||
        final_stage_patch_delta > INT32_MAX) {
        render_log("engine render hook trampoline is outside rel32 range");
        clear_engine_render_hook_sites();
        return false;
    }
    const int32_t patch_delta32 = static_cast<int32_t>(patch_delta);
    const int32_t final_stage_patch_delta32 =
        static_cast<int32_t>(final_stage_patch_delta);
    unsigned char* patch_start = g_engine_render_final_stage_target;
    const size_t patch_size = static_cast<size_t>(
        g_engine_render_hook_target +
        g_engine_render_original.size() - patch_start);
    DWORD old_protect = 0;
    SuspendedProcessThreads suspended_threads;
    if (!VirtualProtect(patch_start, patch_size,
                        PAGE_EXECUTE_READWRITE, &old_protect)) {
        clear_engine_render_hook_sites();
        return false;
    }
    if (std::memcmp(g_engine_render_hook_target,
                    g_engine_render_original.data(),
                    g_engine_render_original.size()) != 0 ||
        std::memcmp(g_engine_render_final_stage_target,
                    g_engine_render_final_stage_original.data(),
                    g_engine_render_final_stage_original.size()) != 0) {
        DWORD unused = 0;
        VirtualProtect(patch_start, patch_size, old_protect, &unused);
        render_log("engine render hook sites changed before patching");
        clear_engine_render_hook_sites();
        return false;
    }
    g_engine_render_final_stage_target[0] = 0xE9;
    std::memcpy(g_engine_render_final_stage_target + 1,
                &final_stage_patch_delta32,
                sizeof(final_stage_patch_delta32));
    g_engine_render_hook_target[0] = 0xE9;
    std::memcpy(g_engine_render_hook_target + 1, &patch_delta32,
                sizeof(patch_delta32));
    g_engine_render_hook_target[5] = 0x90;
    g_engine_render_hook_target[6] = 0x90;
    DWORD unused = 0;
    VirtualProtect(patch_start, patch_size, old_protect, &unused);
    FlushInstructionCache(GetCurrentProcess(), patch_start, patch_size);
    InterlockedExchange(&g_engine_render_pass, 0);
    InterlockedExchange(&g_engine_stereo_pair_ready, 0);
    g_engine_render_hook_active = true;
    render_log("engine render hook installed: two-pass stereo active");
    return true;
}

bool install_interaction_hook() {
    constexpr const char* patterns[] = {
        "0F 29 BD 20 FE FF FF 44 0F 29 85 10 FE FF FF "
        "48 8D 95 00 FF FF FF 48 89 54 24 20 "
        "48 8D 95 20 FE FF FF 4C 8D 85 10 FE FF FF 4C 8B CF",
        "0F 29 BD 20 FE FF FF 44 0F 29 85 10 FE FF FF",
        "0F 29 BD ?? ?? ?? ?? 44 0F 29 85 ?? ?? ?? ??",
    };
    g_interaction_hook_target = find_hook_target_by_patterns(
        "interaction-ray hook", 0x4BF5F1, patterns,
        sizeof(patterns) / sizeof(patterns[0]),
        &hytalevr::interaction_hook_site_valid);
    if (!g_interaction_hook_target) return false;

    std::memcpy(g_interaction_original.data(), g_interaction_hook_target,
                g_interaction_original.size());
    int32_t ray_origin_stack_offset = 0;
    int32_t ray_direction_stack_offset = 0;
    std::memcpy(&ray_origin_stack_offset, g_interaction_original.data() + 3,
                sizeof(ray_origin_stack_offset));
    std::memcpy(&ray_direction_stack_offset, g_interaction_original.data() + 11,
                sizeof(ray_direction_stack_offset));

    auto* code = static_cast<unsigned char*>(g_interaction_hook_memory);
    const bool allocated_code = code == nullptr;
    if (allocated_code) {
        code = static_cast<unsigned char*>(
            VirtualAlloc(nullptr, 512, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!code) return false;
        g_interaction_hook_memory = code;
        unsigned char* p = code;

        // Preserve Hytale's two aligned stores before calling the C++ bridge.
        std::memcpy(p, g_interaction_original.data(), 15);
        p += 15;
        const unsigned char prefix[] = {
            0x9C, 0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53,
            0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00,
            0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x20,
            0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x30,
            0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x40,
            0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x50,
            0xF3, 0x0F, 0x7F, 0x64, 0x24, 0x60,
            0xF3, 0x0F, 0x7F, 0x6C, 0x24, 0x70,
        };
        std::memcpy(p, prefix, sizeof(prefix));
        p += sizeof(prefix);
        const unsigned char origin_lea[] = {0x48, 0x8D, 0x8D};
        std::memcpy(p, origin_lea, sizeof(origin_lea));
        p += sizeof(origin_lea);
        std::memcpy(p, &ray_origin_stack_offset, sizeof(ray_origin_stack_offset));
        p += sizeof(ray_origin_stack_offset);
        const unsigned char direction_lea[] = {0x48, 0x8D, 0x95};
        std::memcpy(p, direction_lea, sizeof(direction_lea));
        p += sizeof(direction_lea);
        std::memcpy(p, &ray_direction_stack_offset, sizeof(ray_direction_stack_offset));
        p += sizeof(ray_direction_stack_offset);
        *p++ = 0x48;
        *p++ = 0xB8;
        emit8(p, reinterpret_cast<uint64_t>(&apply_controller_gameplay_ray));
        const unsigned char suffix[] = {
            0xFF, 0xD0,
            0xF3, 0x0F, 0x6F, 0x44, 0x24, 0x20,
            0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x30,
            0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x40,
            0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x50,
            0xF3, 0x0F, 0x6F, 0x64, 0x24, 0x60,
            0xF3, 0x0F, 0x6F, 0x6C, 0x24, 0x70,
            0x48, 0x81, 0xC4, 0x80, 0x00, 0x00, 0x00,
            0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x58, 0x9D,
            0x48, 0xB8,
        };
        std::memcpy(p, suffix, sizeof(suffix));
        p += sizeof(suffix);
        emit8(p, reinterpret_cast<uint64_t>(g_interaction_hook_target + 15));
        *p++ = 0xFF;
        *p++ = 0xE0;
        if (static_cast<size_t>(p - code) > 512) {
            VirtualFree(code, 0, MEM_RELEASE);
            g_interaction_hook_memory = nullptr;
            g_interaction_hook_target = nullptr;
            return false;
        }
    }

    DWORD old_protect = 0;
    SuspendedProcessThreads suspended_threads;
    if (!VirtualProtect(g_interaction_hook_target, g_interaction_original.size(),
                        PAGE_EXECUTE_READWRITE, &old_protect)) {
        if (allocated_code) {
            VirtualFree(code, 0, MEM_RELEASE);
            g_interaction_hook_memory = nullptr;
        }
        g_interaction_hook_target = nullptr;
        return false;
    }
    if (std::memcmp(g_interaction_hook_target, g_interaction_original.data(),
                    g_interaction_original.size()) != 0) {
        DWORD unused = 0;
        VirtualProtect(g_interaction_hook_target, g_interaction_original.size(),
                       old_protect, &unused);
        render_log("interaction hook site changed before patching; installation aborted");
        if (allocated_code) {
            VirtualFree(code, 0, MEM_RELEASE);
            g_interaction_hook_memory = nullptr;
        }
        g_interaction_hook_target = nullptr;
        return false;
    }
    g_interaction_hook_target[0] = 0x48;
    g_interaction_hook_target[1] = 0xB8;
    *reinterpret_cast<void**>(g_interaction_hook_target + 2) = code;
    g_interaction_hook_target[10] = 0xFF;
    g_interaction_hook_target[11] = 0xE0;
    std::memset(g_interaction_hook_target + 12, 0x90, 3);
    DWORD unused = 0;
    VirtualProtect(g_interaction_hook_target, g_interaction_original.size(),
                   old_protect, &unused);
    FlushInstructionCache(GetCurrentProcess(), g_interaction_hook_target,
                          g_interaction_original.size());
    if (g_shared) g_shared->interaction_hook_active = 1;
    return true;
}

bool install_hook() {
    constexpr const char* patterns[] = {
        "0F 28 B4 24 80 03 00 00 0F 28 BC 24 70 03 00 00 "
        "44 0F 28 84 24 60 03 00 00 44 0F 28 8C 24 50 03 00 00 "
        "44 0F 28 94 24 40 03 00 00 44 0F 28 9C 24 30 03 00 00 "
        "44 0F 28 A4 24 20 03 00 00 44 0F 28 AC 24 10 03 00 00 "
        "48 81 C4 90 03 00 00 5B 5D 5E 5F 41 5D 41 5E 41 5F C3",
        "0F 28 B4 24 80 03 00 00 0F 28 BC 24 70 03 00 00 "
        "44 0F 28 84 24 60 03 00 00 44 0F 28 8C 24 50 03 00 00",
        "0F 28 B4 24 ?? ?? ?? ?? 0F 28 BC 24 ?? ?? ?? ?? "
        "44 0F 28 84 24 ?? ?? ?? ?? 44 0F 28 8C 24 ?? ?? ?? ??",
    };
    g_hook_target = find_hook_target_by_patterns(
        "camera hook", 0x5EC7F3, patterns,
        sizeof(patterns) / sizeof(patterns[0]),
        &hytalevr::camera_hook_site_valid);
    if (!g_hook_target) return false;

    std::memcpy(g_original.data(), g_hook_target, g_original.size());

    auto* code = static_cast<unsigned char*>(g_hook_memory);
    const bool allocated_code = code == nullptr;
    if (allocated_code) {
        code = static_cast<unsigned char*>(
            VirtualAlloc(nullptr, 512, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!code) return false;
        g_hook_memory = code;
        unsigned char* p = code;

        const unsigned char prefix[] = {
            0x9C, 0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53,
            0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00,
            0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x20,
            0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x30,
            0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x40,
            0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x50,
            0xF3, 0x0F, 0x7F, 0x64, 0x24, 0x60,
            0xF3, 0x0F, 0x7F, 0x6C, 0x24, 0x70,
            0x48, 0x8B, 0xCB,
            0x48, 0xB8,
        };
        std::memcpy(p, prefix, sizeof(prefix));
        p += sizeof(prefix);
        emit8(p, reinterpret_cast<uint64_t>(&apply_vr_camera));
        const unsigned char suffix[] = {
            0xFF, 0xD0,
            0xF3, 0x0F, 0x6F, 0x44, 0x24, 0x20,
            0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x30,
            0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x40,
            0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x50,
            0xF3, 0x0F, 0x6F, 0x64, 0x24, 0x60,
            0xF3, 0x0F, 0x6F, 0x6C, 0x24, 0x70,
            0x48, 0x81, 0xC4, 0x80, 0x00, 0x00, 0x00,
            0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x58, 0x9D,
        };
        std::memcpy(p, suffix, sizeof(suffix));
        p += sizeof(suffix);
        std::memcpy(p, g_original.data(), 16);
        p += 16;
        *p++ = 0x48;
        *p++ = 0xB8;
        emit8(p, reinterpret_cast<uint64_t>(g_hook_target + 16));
        *p++ = 0xFF;
        *p++ = 0xE0;
        if (static_cast<size_t>(p - code) > 512) {
            VirtualFree(code, 0, MEM_RELEASE);
            g_hook_memory = nullptr;
            g_hook_target = nullptr;
            return false;
        }
    }

    DWORD old_protect = 0;
    SuspendedProcessThreads suspended_threads;
    if (!VirtualProtect(g_hook_target, 16, PAGE_EXECUTE_READWRITE, &old_protect)) {
        if (allocated_code) {
            VirtualFree(code, 0, MEM_RELEASE);
            g_hook_memory = nullptr;
        }
        g_hook_target = nullptr;
        return false;
    }
    if (std::memcmp(g_hook_target, g_original.data(), g_original.size()) != 0) {
        DWORD unused = 0;
        VirtualProtect(g_hook_target, g_original.size(), old_protect, &unused);
        render_log("camera hook site changed before patching; installation aborted");
        if (allocated_code) {
            VirtualFree(code, 0, MEM_RELEASE);
            g_hook_memory = nullptr;
        }
        g_hook_target = nullptr;
        return false;
    }
    g_hook_target[0] = 0x48;
    g_hook_target[1] = 0xB8;
    *reinterpret_cast<void**>(g_hook_target + 2) = code;
    g_hook_target[10] = 0xFF;
    g_hook_target[11] = 0xE0;
    std::memset(g_hook_target + 12, 0x90, 4);
    DWORD unused = 0;
    VirtualProtect(g_hook_target, 16, old_protect, &unused);
    FlushInstructionCache(GetCurrentProcess(), g_hook_target, 16);
    return true;
}

bool restore_camera_hook() {
    if (!g_hook_target) return true;
    if (!hytalevr::absolute_jump_patch_matches(
            g_hook_target, g_original.size(), g_hook_memory)) {
        render_log("camera hook restore refused: patch ownership was lost");
        return false;
    }
    DWORD old_protect = 0;
    SuspendedProcessThreads suspended_threads;
    if (!VirtualProtect(g_hook_target, g_original.size(), PAGE_EXECUTE_READWRITE,
                        &old_protect)) return false;
    if (!hytalevr::absolute_jump_patch_matches(
            g_hook_target, g_original.size(), g_hook_memory)) {
        DWORD unused = 0;
        VirtualProtect(g_hook_target, g_original.size(), old_protect, &unused);
        render_log("camera hook restore refused: patch changed while suspending threads");
        return false;
    }
    std::memcpy(g_hook_target, g_original.data(), g_original.size());
    DWORD unused = 0;
    VirtualProtect(g_hook_target, g_original.size(), old_protect, &unused);
    FlushInstructionCache(GetCurrentProcess(), g_hook_target, g_original.size());
    g_hook_target = nullptr;
    g_primary_camera = nullptr;
    g_center_view_projection_valid = false;
    g_culling_view_projection_valid = false;
    g_culling_projection_valid = false;
    g_culling_view_valid = false;
    g_last_native_camera_view_valid = false;
    g_last_written_culling_view_valid = false;
    g_last_native_camera_object = nullptr;
    g_last_written_culling_object = nullptr;
    g_last_observed_native_yaw_valid = false;
    if (g_shared) g_shared->primary_camera = 0;
    // Keep the trampoline allocated. A thread that entered just before the
    // patch restoration may still be returning through it.
    return true;
}

bool restore_interaction_hook() {
    if (!g_interaction_hook_target) {
        if (g_shared) {
            g_shared->interaction_hook_active = 0;
            g_shared->controller_ray_active = 0;
        }
        return true;
    }
    if (!hytalevr::absolute_jump_patch_matches(
            g_interaction_hook_target, g_interaction_original.size(),
            g_interaction_hook_memory)) {
        render_log("interaction hook restore refused: patch ownership was lost");
        return false;
    }
    DWORD old_protect = 0;
    SuspendedProcessThreads suspended_threads;
    if (!VirtualProtect(g_interaction_hook_target, g_interaction_original.size(),
                        PAGE_EXECUTE_READWRITE, &old_protect)) return false;
    if (!hytalevr::absolute_jump_patch_matches(
            g_interaction_hook_target, g_interaction_original.size(),
            g_interaction_hook_memory)) {
        DWORD unused = 0;
        VirtualProtect(g_interaction_hook_target, g_interaction_original.size(),
                       old_protect, &unused);
        render_log(
            "interaction hook restore refused: patch changed while suspending threads");
        return false;
    }
    std::memcpy(g_interaction_hook_target, g_interaction_original.data(),
                g_interaction_original.size());
    DWORD unused = 0;
    VirtualProtect(g_interaction_hook_target, g_interaction_original.size(),
                   old_protect, &unused);
    FlushInstructionCache(GetCurrentProcess(), g_interaction_hook_target,
                          g_interaction_original.size());
    g_interaction_hook_target = nullptr;
    AcquireSRWLockExclusive(&g_game_ray_lock);
    g_controller_game_ray_valid = false;
    g_controller_game_ray_physical_active = false;
    ReleaseSRWLockExclusive(&g_game_ray_lock);
    if (g_shared) {
        g_shared->interaction_hook_active = 0;
        g_shared->controller_ray_active = 0;
    }
    return true;
}

bool restore_engine_render_hook() {
    if (!g_engine_render_hook_target) {
        clear_engine_render_hook_sites();
        g_engine_render_hook_active = false;
        InterlockedExchange(&g_engine_render_pass, 0);
        InterlockedExchange(&g_engine_stereo_pair_ready, 0);
        InterlockedExchange(&g_render_eye, 0);
        return true;
    }
    if (!engine_render_patch_matches()) {
        render_log("engine render hook restore refused: patch ownership was lost");
        return false;
    }

    unsigned char* patch_start = g_engine_render_final_stage_target;
    const size_t patch_size = static_cast<size_t>(
        g_engine_render_hook_target +
        g_engine_render_original.size() - patch_start);
    DWORD old_protect = 0;
    SuspendedProcessThreads suspended_threads;
    if (!VirtualProtect(patch_start, patch_size,
                        PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }
    if (!engine_render_patch_matches()) {
        DWORD unused = 0;
        VirtualProtect(patch_start, patch_size, old_protect, &unused);
        render_log(
            "engine render hook restore refused: patch changed while suspending threads");
        return false;
    }

    std::memcpy(g_engine_render_final_stage_target,
                g_engine_render_final_stage_original.data(),
                g_engine_render_final_stage_original.size());
    std::memcpy(g_engine_render_hook_target, g_engine_render_original.data(),
                g_engine_render_original.size());
    DWORD unused = 0;
    VirtualProtect(patch_start, patch_size, old_protect, &unused);
    FlushInstructionCache(GetCurrentProcess(), patch_start, patch_size);
    clear_engine_render_hook_sites();
    g_engine_render_hook_active = false;
    InterlockedExchange(&g_engine_render_pass, 0);
    InterlockedExchange(&g_engine_stereo_pair_ready, 0);
    InterlockedExchange(&g_render_eye, 0);
    render_log("engine render hook restored");
    // Keep the trampoline allocated because a thread that entered immediately
    // before restoration can still be returning through it.
    return true;
}

bool restore_noesis_hooks() {
    bool ok = true;
    if (g_noesis_renderer_target) {
        DWORD old_protect = 0;
        SuspendedProcessThreads suspended_threads;
        if (VirtualProtect(g_noesis_renderer_target, g_noesis_renderer_original.size(),
                           PAGE_EXECUTE_READWRITE, &old_protect)) {
            std::memcpy(g_noesis_renderer_target, g_noesis_renderer_original.data(),
                        g_noesis_renderer_original.size());
            DWORD unused = 0;
            VirtualProtect(g_noesis_renderer_target, g_noesis_renderer_original.size(),
                           old_protect, &unused);
            FlushInstructionCache(GetCurrentProcess(), g_noesis_renderer_target,
                                  g_noesis_renderer_original.size());
        } else {
            ok = false;
        }
        g_noesis_renderer_target = nullptr;
    }
    if (g_noesis_begin_target) {
        DWORD old_protect = 0;
        SuspendedProcessThreads suspended_threads;
        if (VirtualProtect(g_noesis_begin_target, g_noesis_begin_original.size(),
                           PAGE_EXECUTE_READWRITE, &old_protect)) {
            std::memcpy(g_noesis_begin_target, g_noesis_begin_original.data(),
                        g_noesis_begin_original.size());
            DWORD unused = 0;
            VirtualProtect(g_noesis_begin_target, g_noesis_begin_original.size(),
                           old_protect, &unused);
            FlushInstructionCache(GetCurrentProcess(), g_noesis_begin_target,
                                  g_noesis_begin_original.size());
        } else {
            ok = false;
        }
        g_noesis_begin_target = nullptr;
    }
    if (g_noesis_end_target) {
        DWORD old_protect = 0;
        SuspendedProcessThreads suspended_threads;
        if (VirtualProtect(g_noesis_end_target, g_noesis_end_original.size(),
                           PAGE_EXECUTE_READWRITE, &old_protect)) {
            std::memcpy(g_noesis_end_target, g_noesis_end_original.data(),
                        g_noesis_end_original.size());
            DWORD unused = 0;
            VirtualProtect(g_noesis_end_target, g_noesis_end_original.size(),
                           old_protect, &unused);
            FlushInstructionCache(GetCurrentProcess(), g_noesis_end_target,
                                  g_noesis_end_original.size());
        } else {
            ok = false;
        }
        g_noesis_end_target = nullptr;
    }
    if (g_shared) g_shared->ui_overlay_active = 0;
    g_noesis_hooks_active = false;
    return ok;
}

bool restore_swap_hook() {
    if (!g_swap_target) return true;
    if (!hytalevr::absolute_jump_patch_matches(
            g_swap_target, g_swap_original.size(),
            reinterpret_cast<const void*>(&hook_sdl_gl_swap_window))) {
        render_log("SDL swap hook restore refused: patch ownership was lost");
        return false;
    }
    DWORD old_protect = 0;
    SuspendedProcessThreads suspended_threads;
    if (!VirtualProtect(g_swap_target, g_swap_original.size(), PAGE_EXECUTE_READWRITE,
                        &old_protect)) return false;
    if (!hytalevr::absolute_jump_patch_matches(
            g_swap_target, g_swap_original.size(),
            reinterpret_cast<const void*>(&hook_sdl_gl_swap_window))) {
        DWORD unused = 0;
        VirtualProtect(g_swap_target, g_swap_original.size(), old_protect, &unused);
        render_log("SDL swap hook restore refused: patch changed while suspending threads");
        return false;
    }
    std::memcpy(g_swap_target, g_swap_original.data(), g_swap_original.size());
    DWORD unused = 0;
    VirtualProtect(g_swap_target, g_swap_original.size(), old_protect, &unused);
    FlushInstructionCache(GetCurrentProcess(), g_swap_target, g_swap_original.size());
    g_swap_target = nullptr;
    // The trampoline remains allocated because this callback still returns
    // through it after restoring SDL's original export wrapper.
    return true;
}

bool restore_all_hooks(bool render_thread_cleanup) {
    AcquireSRWLockExclusive(&g_hook_lifecycle_lock);
    if (!render_thread_cleanup &&
        g_runtime_active.load(std::memory_order_acquire)) {
        // OpenGL resources and source supersampling must be restored by the
        // swap/render thread while its context is current. Keep every hook in
        // place so the next swap can finish the unload safely.
        ReleaseSRWLockExclusive(&g_hook_lifecycle_lock);
        return false;
    }
    publish_ui_scale_neutral();
    restore_neutral_camera(true);
    if (render_thread_cleanup &&
        g_runtime_active.load(std::memory_order_acquire)) {
        shutdown_stereo();
    }

    const bool engine_render_restored = restore_engine_render_hook();
    const bool noesis_restored =
        engine_render_restored && restore_noesis_hooks();
    const bool interaction_restored =
        noesis_restored && restore_interaction_hook();
    const bool camera_restored = interaction_restored && restore_camera_hook();
    // Keep the swap callback installed when another hook could not be restored;
    // it gives the render thread a safe place to retry without unloading code.
    const bool swap_restored = camera_restored && restore_swap_hook();
    const bool restored = engine_render_restored && noesis_restored &&
                          interaction_restored && camera_restored &&
                          swap_restored;
    if (g_shared) {
        g_shared->swap_hook_active = swap_restored ? 0u : 1u;
        g_shared->hook_active = restored ? 0u : 1u;
        g_shared->hook_error = restored ? 0 : 3;
    }
    ReleaseSRWLockExclusive(&g_hook_lifecycle_lock);
    return restored;
}

bool wait_for_hook_callbacks_to_quiesce(DWORD timeout_ms) {
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    ULONGLONG idle_since = 0;
    while (GetTickCount64() < deadline) {
        if (InterlockedCompareExchange(&g_hook_callbacks_in_flight, 0, 0) == 0) {
            if (idle_since == 0) idle_since = GetTickCount64();
            if (GetTickCount64() - idle_since >= 250) return true;
        } else {
            idle_since = 0;
        }
        Sleep(5);
    }
    return false;
}

void close_worker_resources() {
    publish_ui_scale_neutral();
    if (g_ui_scale_shared) {
        UnmapViewOfFile(g_ui_scale_shared);
        g_ui_scale_shared = nullptr;
    }
    if (g_ui_scale_mapping) {
        CloseHandle(g_ui_scale_mapping);
        g_ui_scale_mapping = nullptr;
    }

    VrCameraShared* shared = g_shared;
    g_shared = nullptr;
    if (shared) UnmapViewOfFile(shared);
    if (g_mapping) {
        CloseHandle(g_mapping);
        g_mapping = nullptr;
    }
}

[[noreturn]] void unload_worker_module() {
    join_item_catalog_loader();
    if (g_vr_system) {
        vr::VR_Shutdown();
        g_vr_system = nullptr;
    }
    if (g_hand_gdiplus_token != 0) {
        Gdiplus::GdiplusShutdown(g_hand_gdiplus_token);
        g_hand_gdiplus_token = 0;
    }
    close_worker_resources();
    HMODULE module = g_module;
    g_module = nullptr;
    if (module) FreeLibraryAndExitThread(module, 0);
    ExitThread(0);
}

DWORD WINAPI worker(void*) {
    if (render_logging_enabled()) {
        std::lock_guard<std::mutex> lock(g_render_log_mutex);
        std::ofstream clear_log(render_log_path(), std::ios_base::trunc);
        if (clear_log.is_open()) {
            clear_log << GetTickCount64()
                      << " worker start v122 floor aligned\n";
        }
    }
    render_log("worker start: opening shared mapping");
    g_mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, kVrCameraMappingName);
    if (!g_mapping) {
        render_log("worker abort: OpenFileMapping failed");
        unload_worker_module();
    }
    g_shared = static_cast<VrCameraShared*>(
        MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(VrCameraShared)));
    if (!g_shared || g_shared->magic != kVrCameraMagic ||
        g_shared->version != kVrCameraVersion) {
        render_log("worker abort: shared mapping magic/version mismatch");
        if (g_shared) g_shared->hook_error = 6;
        unload_worker_module();
    }
    render_log("worker mapped shared memory");
    auto install_all_hooks = []() {
        render_log("install_all_hooks begin");
        g_shared->hook_active = 0;
        g_shared->swap_hook_active = 0;
        g_shared->interaction_hook_active = 0;
        if (!install_hook()) {
            g_shared->hook_error = 1;
            render_log("install_all_hooks failed: camera signature/hook");
            return false;
        }
        if (!install_interaction_hook()) {
            const bool camera_restored = restore_camera_hook();
            g_shared->hook_active = camera_restored ? 0u : 1u;
            g_shared->hook_error = camera_restored ? 4 : 3;
            render_log("install_all_hooks failed: interaction hook");
            return false;
        }
        // The renderer export hook only forwarded the call and depended on an
        // unvalidated Noesis prologue. UI handling is owned by HytaleUIScaleHook.
        g_noesis_hooks_active = false;
        // Replaying Hytale's render stages consumes internal frame queues and
        // either crashes or skips the final colour/UI composition. Keep the
        // experimental hook dormant until those queues can be cloned safely.
        g_engine_render_hook_active = false;
        InterlockedExchange(&g_engine_render_pass, 0);
        InterlockedExchange(&g_engine_stereo_pair_ready, 0);
        if (!install_swap_hook()) {
            const bool engine_render_restored = restore_engine_render_hook();
            const bool noesis_restored =
                engine_render_restored && restore_noesis_hooks();
            const bool interaction_restored =
                noesis_restored && restore_interaction_hook();
            const bool camera_restored = interaction_restored && restore_camera_hook();
            g_shared->hook_active = camera_restored ? 0u : 1u;
            g_shared->hook_error = camera_restored ? 2 : 3;
            render_log("install_all_hooks failed: SDL swap hook");
            return false;
        }
        g_shared->swap_hook_active = 1;
        g_shared->hook_active = 1;
        g_shared->hook_error = 0;
        render_log("install_all_hooks success");
        return true;
    };

    uint32_t observed_install_sequence = g_shared->install_sequence;
    ULONGLONG unload_requested_at = 0;
    ensure_item_catalog_loading();
    install_all_hooks();
    for (;;) {
        VrCameraControls controls{};
        if (!read_controls(controls)) {
            Sleep(20);
            continue;
        }
        if (controls.unload_requested) {
            const ULONGLONG now = GetTickCount64();
            if (unload_requested_at == 0) unload_requested_at = now;

            bool restored = g_shared->hook_active == 0;
            // Prefer render-thread cleanup. If rendering is paused, restore the
            // code hooks from the worker after a bounded grace period.
            if (!restored &&
                (!g_shared->swap_hook_active || now - unload_requested_at >= 750)) {
                restored = restore_all_hooks(false);
            }
            if (restored) {
                if (wait_for_hook_callbacks_to_quiesce(3000)) {
                    render_log("worker unload: hooks restored and callbacks quiescent");
                    unload_worker_module();
                }
                g_shared->hook_error = 7;
                render_log("worker unload deferred: callbacks are still active");
            }
            Sleep(20);
            continue;
        }
        unload_requested_at = 0;
        if (!g_shared->hook_active &&
                   controls.install_sequence != observed_install_sequence) {
            observed_install_sequence = controls.install_sequence;
            render_log("install sequence changed: reinstall requested");
            if (!controls.unload_requested) install_all_hooks();
        }
        Sleep(20);
    }
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
