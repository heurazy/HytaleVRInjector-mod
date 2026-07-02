#include <windows.h>
#include <tlhelp32.h>
#include <GL/gl.h>
#include <openvr.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

#include "vr_camera_shared.h"
#include "vr_math.h"

namespace {

using hytalevr::Matrix4;
using hytalevr::identity_matrix;
using hytalevr::multiply;

HMODULE g_module = nullptr;
HANDLE g_mapping = nullptr;
VrCameraShared* g_shared = nullptr;
std::mutex g_render_log_mutex;
ULONGLONG g_last_camera_diag_tick = 0;
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
struct UiScaleShared {
    float uiScale;
    float offsetX;
    float offsetY;
    int disableUboScaling;
    int disableShadows;
    int disableParticles;
    int disableDistortion;
    int hideFirstPerson;
    volatile LONG menuVisibleCounter;
    volatile LONG menuLargeDrawCounter;
    volatile LONG menuTextureId;
    volatile LONG menuTextureWidth;
    volatile LONG menuTextureHeight;
    volatile LONG menuTextureFrame;
    volatile LONG menuCaptureError;
    volatile LONG currentEye;
    volatile LONG suppressMenuInGame;
    volatile LONG menuIgnoreDrawThreshold;
    int firstPersonControllerReanchor;
    int firstPersonControllerPoseValid;
    float firstPersonHandNdcX;
    float firstPersonHandNdcY;
    float firstPersonHandDepth;
    volatile LONG firstPersonMatrixPatches;
};
HANDLE g_ui_scale_mapping = nullptr;
UiScaleShared* g_ui_scale_shared = nullptr;
bool menu_visible_recently();
bool controller_hand_ndc_for_eye(vr::EVREye eye, float& ndc_x, float& ndc_y, float& depth);
void* g_hook_memory = nullptr;
unsigned char* g_hook_target = nullptr;
std::array<unsigned char, 16> g_original{};
void* g_interaction_hook_memory = nullptr;
unsigned char* g_interaction_hook_target = nullptr;
std::array<unsigned char, 15> g_interaction_original{};
using SdlGlSwapWindowFn = void(__cdecl*)(void*);
using SdlGetWindowSizeFn = bool(__cdecl*)(void*, int*, int*);
using NoesisRenderFn = void(__fastcall*)(void*);
using NoesisAnyFn = void(__fastcall*)(void*, void*, void*, void*);
using GlGenFramebuffersFn = void(APIENTRY*)(GLsizei, GLuint*);
using GlDeleteFramebuffersFn = void(APIENTRY*)(GLsizei, const GLuint*);
using GlBindFramebufferFn = void(APIENTRY*)(GLenum, GLuint);
using GlFramebufferTexture2DFn = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
using GlBlitFramebufferFn = void(APIENTRY*)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint,
                                             GLbitfield, GLenum);
using GlCheckFramebufferStatusFn = GLenum(APIENTRY*)(GLenum);
SdlGlSwapWindowFn g_swap_trampoline = nullptr;
SdlGetWindowSizeFn g_sdl_get_window_size_in_pixels = nullptr;
unsigned char* g_swap_target = nullptr;
std::array<unsigned char, 14> g_swap_original{};
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
GLuint g_eye_textures[2]{};
bool g_eye_valid[2]{};
bool g_pre_ui_captured[2]{};
int g_texture_width = 0;
int g_texture_height = 0;
int g_eye_target_width = 0;
int g_eye_target_height = 0;
volatile LONG g_render_eye = 0;
uint32_t g_last_swap_recenter_sequence = 0;
uint32_t g_consecutive_submit_failures = 0;
ULONGLONG g_last_successful_submit_tick = 0;
SRWLOCK g_pose_lock = SRWLOCK_INIT;
Matrix4 g_hmd_view_delta = identity_matrix();
float g_hmd_origin_pose[12]{};
float g_hmd_current_pose[12]{};
float g_right_controller_pose[12]{};
bool g_hmd_origin_valid = false;
bool g_hmd_current_valid = false;
bool g_right_controller_valid = false;
uint32_t g_recenter_sequence = 0;
bool g_runtime_active = false;
GLuint g_capture_fbo = 0;
GLuint g_resolve_fbo = 0;
GLuint g_resolve_texture = 0;
GLuint g_ui_fbo = 0;
GLuint g_ui_texture = 0;
int g_ui_width = 0;
int g_ui_height = 0;
bool g_ui_redirect_active = false;
GLint g_ui_saved_draw_fbo = 0;
GLint g_ui_saved_read_fbo = 0;
GLint g_ui_saved_viewport[4]{};
int g_resolve_width = 0;
int g_resolve_height = 0;
GlGenFramebuffersFn g_gl_gen_framebuffers = nullptr;
GlDeleteFramebuffersFn g_gl_delete_framebuffers = nullptr;
GlBindFramebufferFn g_gl_bind_framebuffer = nullptr;
GlFramebufferTexture2DFn g_gl_framebuffer_texture_2d = nullptr;
GlBlitFramebufferFn g_gl_blit_framebuffer = nullptr;
GlCheckFramebufferStatusFn g_gl_check_framebuffer_status = nullptr;
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
bool g_center_view_projection_valid = false;
bool g_culling_view_projection_valid = false;
bool g_culling_projection_valid = false;
bool g_culling_view_valid = false;
bool g_neutral_view_valid = false;
vr::VRActionSetHandle_t g_action_set = vr::k_ulInvalidActionSetHandle;
vr::VRActionHandle_t g_action_move = vr::k_ulInvalidActionHandle;
vr::VRActionHandle_t g_action_turn = vr::k_ulInvalidActionHandle;
vr::VRActionHandle_t g_action_jump = vr::k_ulInvalidActionHandle;
vr::VRActionHandle_t g_action_sprint = vr::k_ulInvalidActionHandle;
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
bool g_left_grip_pressed = false;
bool g_right_grip_pressed = false;
SRWLOCK g_game_ray_lock = SRWLOCK_INIT;
float g_controller_ray_offset[3]{};
float g_controller_ray_direction[3]{};
bool g_controller_game_ray_valid = false;

constexpr GLenum GL_READ_FRAMEBUFFER_VALUE = 0x8CA8;
constexpr GLenum GL_DRAW_FRAMEBUFFER_VALUE = 0x8CA9;
constexpr GLenum GL_READ_FRAMEBUFFER_BINDING_VALUE = 0x8CAA;
constexpr GLenum GL_DRAW_FRAMEBUFFER_BINDING_VALUE = 0x8CA6;
constexpr GLenum GL_COLOR_ATTACHMENT0_VALUE = 0x8CE0;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE_VALUE = 0x8CD5;
constexpr GLenum GL_CLAMP_TO_EDGE_VALUE = 0x812F;
constexpr GLenum GL_SAMPLES_VALUE = 0x80A9;
constexpr GLenum GL_VIEWPORT_VALUE = 0x0BA2;

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

void reset_eye_capture_resources(const char* reason) {
    if (g_eye_textures[0] || g_eye_textures[1]) {
        glDeleteTextures(2, g_eye_textures);
        g_eye_textures[0] = 0;
        g_eye_textures[1] = 0;
    }
    if (g_resolve_texture) {
        glDeleteTextures(1, &g_resolve_texture);
        g_resolve_texture = 0;
    }
    g_eye_valid[0] = false;
    g_eye_valid[1] = false;
    g_pre_ui_captured[0] = false;
    g_pre_ui_captured[1] = false;
    g_texture_width = 0;
    g_texture_height = 0;
    g_resolve_width = 0;
    g_resolve_height = 0;
    g_eye_target_width = 0;
    g_eye_target_height = 0;
    InterlockedExchange(&g_render_eye, 0);
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
bool restore_noesis_hooks();
bool restore_swap_hook();
bool capture_eye(int eye, const VrCameraControls& controls);

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
        controls.render_scale = g_shared->render_scale;
        controls.translation_scale = g_shared->translation_scale;
        controls.translation_y_scale = g_shared->translation_y_scale;
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
        controls.floor_tilt_degrees = g_shared->floor_tilt_degrees;
        controls.first_person_hand_hidden = g_shared->first_person_hand_hidden;
        controls.wide_culling_enabled = g_shared->wide_culling_enabled;
        controls.wide_culling_scale = g_shared->wide_culling_scale;
        controls.hmd_culling_view_enabled = g_shared->hmd_culling_view_enabled;
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
        L"Local\\HytaleUIScaleSharedMemory");
    if (!g_ui_scale_mapping) return false;
    g_ui_scale_shared = static_cast<UiScaleShared*>(
        MapViewOfFile(g_ui_scale_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(UiScaleShared)));
    if (!g_ui_scale_shared) {
        CloseHandle(g_ui_scale_mapping);
        g_ui_scale_mapping = nullptr;
        return false;
    }
    g_ui_scale_shared->uiScale = 1.0f;
    g_ui_scale_shared->offsetX = 0.0f;
    g_ui_scale_shared->offsetY = 0.0f;
    g_ui_scale_shared->disableUboScaling = 0;
    g_ui_scale_shared->disableShadows = 0;
    g_ui_scale_shared->disableParticles = 0;
    g_ui_scale_shared->disableDistortion = 0;
    g_ui_scale_shared->hideFirstPerson = 0;
    g_ui_scale_shared->menuVisibleCounter = 0;
    g_ui_scale_shared->menuLargeDrawCounter = 0;
    g_ui_scale_shared->menuTextureId = 0;
    g_ui_scale_shared->menuTextureWidth = 0;
    g_ui_scale_shared->menuTextureHeight = 0;
    g_ui_scale_shared->menuTextureFrame = 0;
    g_ui_scale_shared->menuCaptureError = 0;
    g_ui_scale_shared->currentEye = 0;
    g_ui_scale_shared->suppressMenuInGame = 0;
    g_ui_scale_shared->menuIgnoreDrawThreshold = 1;
    g_ui_scale_shared->firstPersonControllerReanchor = 0;
    g_ui_scale_shared->firstPersonControllerPoseValid = 0;
    g_ui_scale_shared->firstPersonHandNdcX = 0.0f;
    g_ui_scale_shared->firstPersonHandNdcY = 0.0f;
    g_ui_scale_shared->firstPersonHandDepth = 0.0f;
    g_ui_scale_shared->firstPersonMatrixPatches = 0;
    return true;
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
    g_ui_scale_shared->disableDistortion = controls.distortion_disabled ? 1 : 0;
    g_ui_scale_shared->currentEye = eye == vr::Eye_Left ? 0 : 1;
    g_ui_scale_shared->suppressMenuInGame = menu_visible ? 1 : 0;
    g_ui_scale_shared->menuIgnoreDrawThreshold =
        static_cast<LONG>(std::clamp(controls.menu_ignore_draw_threshold, 0u, 20000u));

    // Keep Hytale's camera-anchored first-person hand/item hidden in VR. The
    // controller ray/actions remain active, but no replacement hand is drawn.
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
    g_ui_scale_shared->hideFirstPerson = 0;
    g_ui_scale_shared->suppressMenuInGame = 0;
    g_ui_scale_shared->menuIgnoreDrawThreshold = 1;
    g_ui_scale_shared->firstPersonControllerReanchor = 0;
    g_ui_scale_shared->firstPersonControllerPoseValid = 0;
    g_ui_scale_shared->firstPersonHandNdcX = 0.0f;
    g_ui_scale_shared->firstPersonHandNdcY = 0.0f;
    g_ui_scale_shared->firstPersonHandDepth = 0.0f;
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
    g_ui_scale_shared->hideFirstPerson = 0;
    g_ui_scale_shared->suppressMenuInGame = 0;
    g_ui_scale_shared->menuIgnoreDrawThreshold =
        static_cast<LONG>(std::clamp(controls.menu_ignore_draw_threshold, 0u, 20000u));
    g_ui_scale_shared->firstPersonControllerReanchor = 0;
    g_ui_scale_shared->firstPersonControllerPoseValid = 0;
    g_ui_scale_shared->firstPersonHandNdcX = 0.0f;
    g_ui_scale_shared->firstPersonHandNdcY = 0.0f;
    g_ui_scale_shared->firstPersonHandDepth = 0.0f;
}

vr::EVREye current_camera_eye(const VrCameraControls& controls) {
    const bool left = (InterlockedCompareExchange(&g_render_eye, 0, 0) == 0) ^
                      (controls.swap_eyes != 0);
    return left ? vr::Eye_Left : vr::Eye_Right;
}

Matrix4 floor_tilt_correction(float degrees);
Matrix4 apply_floor_tilt_correction(const Matrix4& delta,
                                    const VrCameraControls& controls);

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

Matrix4 widen_projection_for_culling(Matrix4 projection, const VrCameraControls& controls) {
    if (!controls.wide_culling_enabled) return projection;
    const float scale = std::clamp(controls.wide_culling_scale, 0.05f, 1.0f);
    projection.value[0] *= scale;
    projection.value[5] *= scale;
    return projection;
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
    Matrix4 held = g_aggressive_native_view;
    held.value[12] = horizontal_native.value[12];
    held.value[13] = horizontal_native.value[13];
    held.value[14] = horizontal_native.value[14];
    g_aggressive_native_view = held;
    g_last_observed_native_yaw = native_yaw;
    g_last_observed_native_yaw_valid = true;
    snap_suppressed = true;
    ++g_aggressive_suppressed_snaps;
    return held;
}

void apply_vr_camera(void* object) {
    if (g_shared) {
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->camera_hook_entries));
    }
    VrCameraControls controls{};
    if (!object || !read_controls(controls) || !controls.enabled) return;
    if (controls.non_vr_mode) {
        publish_render_filters_only(controls);
        return;
    }

    auto* camera = static_cast<unsigned char*>(object);
    Matrix4 raw_view{};
    Matrix4 projection{};
    std::memcpy(&raw_view, camera + 0x2E0, sizeof(raw_view));
    std::memcpy(&projection, camera + 0x4E0, sizeof(projection));

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
    AcquireSRWLockShared(&g_pose_lock);
    delta = g_hmd_view_delta;
    ReleaseSRWLockShared(&g_pose_lock);
    float aggressive_native_delta = 0.0f;
    bool aggressive_snap_suppressed = false;
    const Matrix4 leveled_view =
        aggressive_game_view(view, controls, aggressive_native_delta,
                             aggressive_snap_suppressed);
    const Matrix4 floor_corrected_view =
        multiply(floor_tilt_correction(controls.floor_tilt_degrees), leveled_view);
    g_last_aggressive_native_delta = aggressive_native_delta;
    if (g_shared) {
        g_shared->native_camera_yaw = yaw_from_view(view);
        g_shared->body_camera_yaw = yaw_from_view(leveled_view);
        g_shared->camera_yaw_valid = 1;
    }

    const Matrix4 center_view = multiply(delta, floor_corrected_view);
    const Matrix4 culling_view =
        controls.hmd_culling_view_enabled ? hytalevr::horizontal_view(center_view) : floor_corrected_view;
    const Matrix4 neutral_view_projection = multiply(projection, floor_corrected_view);
    const Matrix4 culling_projection = widen_projection_for_culling(projection, controls);
    const Matrix4 culling_view_projection = multiply(culling_projection, culling_view);
    AcquireSRWLockExclusive(&g_camera_lock);
    g_neutral_view = floor_corrected_view;
    g_neutral_view_valid = true;
    g_culling_view = culling_view;
    g_culling_view_valid = controls.hmd_culling_view_enabled != 0;
    g_center_view_projection = neutral_view_projection;
    g_center_view_projection_valid = true;
    g_culling_projection = culling_projection;
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

    Matrix4 eye_view_matrix = center_view;
    if (controls.stereo_enabled && g_vr_system) {
        const vr::EVREye eye = current_camera_eye(controls);
        const auto left_eye = g_vr_system->GetEyeToHeadTransform(vr::Eye_Left);
        const auto right_eye = g_vr_system->GetEyeToHeadTransform(vr::Eye_Right);
        const float dx = right_eye.m[0][3] - left_eye.m[0][3];
        const float dy = right_eye.m[1][3] - left_eye.m[1][3];
        const float dz = right_eye.m[2][3] - left_eye.m[2][3];
        const float runtime_ipd = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float requested_ipd = std::clamp(controls.ipd_meters, 0.04f, 0.08f);
        const float ipd_scale = runtime_ipd > 0.001f ? requested_ipd / runtime_ipd : 1.0f;
        const float separation = std::clamp(controls.stereo_separation, 0.0f, 2.0f) *
                                 std::clamp(ipd_scale, 0.5f, 1.5f);
        const auto eye_to_head = eye == vr::Eye_Left ? left_eye : right_eye;
        const Matrix4 eye_view = inverse_eye_to_head(eye_to_head, separation);
        eye_view_matrix = multiply(eye_view, center_view);

        float near_z = 0.05f;
        float far_z = 10000.0f;
        projection_planes(projection, near_z, far_z);
        projection = from_openvr(g_vr_system->GetProjectionMatrix(eye, near_z, far_z));
    }
    const Matrix4 view_projection = multiply(projection, eye_view_matrix);
    // Keep the final VR transform in +0x320. When HMD culling is enabled,
    // +0x2E0 stays on flattened headset yaw so world loading/culling follows
    // where the player looks, not only where Hytale's mouse camera points.
    if (controls.hmd_culling_view_enabled) {
        std::memcpy(camera + 0x2E0, &culling_view, sizeof(culling_view));
    }
    if (controls.wide_culling_enabled) {
        std::memcpy(camera + 0x4E0, &culling_projection, sizeof(culling_projection));
    }
    std::memcpy(camera + 0x320, &view_projection, sizeof(view_projection));
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
            << " wideCull=" << controls.wide_culling_enabled
            << " cullScale=" << controls.wide_culling_scale
            << " hmdCull=" << controls.hmd_culling_view_enabled
            << " floorTilt=" << controls.floor_tilt_degrees
            << " hmdOrigin=" << (g_hmd_origin_valid ? 1 : 0)
            << " hmdCurrent=" << (g_hmd_current_valid ? 1 : 0)
            << " selfCullView=" << (reused_cached_native_view ? 1 : 0)
            << " aggressiveDelta=" << aggressive_native_delta
            << " aggressiveSuppressed=" << (aggressive_snap_suppressed ? 1 : 0)
            << " aggressiveTotal=" << g_aggressive_suppressed_snaps
            << " nativeView " << matrix_summary(view)
            << " heldView " << matrix_summary(leveled_view)
            << " projection " << matrix_summary(projection)
            << " hmdDelta " << matrix_summary(delta)
            << " finalVP " << matrix_summary(view_projection);
        render_log(out.str());
        g_last_camera_diag_tick = camera_log_now;
        g_last_diag_view = view;
        g_last_diag_view_projection = view_projection;
        g_last_diag_valid = true;
    }

    bool right_pose_valid = false;
    Matrix4 right_view_delta = identity_matrix();
    AcquireSRWLockShared(&g_pose_lock);
    if (g_hmd_origin_valid && g_right_controller_valid) {
        right_view_delta = hytalevr::relative_view_pose(
            g_hmd_origin_pose, g_right_controller_pose,
            std::clamp(controls.translation_scale, 0.0f, 10.0f),
            std::clamp(controls.translation_y_scale, 0.0f, 10.0f),
            controls.invert_translation_xz != 0);
        right_pose_valid = true;
    }
    ReleaseSRWLockShared(&g_pose_lock);

    float ray_offset[3]{};
    float ray_direction[3]{};
    const bool ray_valid = controls.hand_pointer_enabled && right_pose_valid;
    if (ray_valid) {
        const Matrix4 controller_view = multiply(right_view_delta, floor_corrected_view);
        hytalevr::view_pose(controller_view, ray_offset, ray_direction);
    }
    AcquireSRWLockExclusive(&g_game_ray_lock);
    std::copy(std::begin(ray_offset), std::end(ray_offset),
              std::begin(g_controller_ray_offset));
    std::copy(std::begin(ray_direction), std::end(ray_direction),
              std::begin(g_controller_ray_direction));
    g_controller_game_ray_valid = ray_valid;
    ReleaseSRWLockExclusive(&g_game_ray_lock);

    g_shared->controller_ray_origin_x = ray_offset[0];
    g_shared->controller_ray_origin_y = ray_offset[1];
    g_shared->controller_ray_origin_z = ray_offset[2];
    g_shared->controller_ray_direction_x = ray_direction[0];
    g_shared->controller_ray_direction_y = ray_direction[1];
    g_shared->controller_ray_direction_z = ray_direction[2];
    g_shared->controller_ray_active =
        ray_valid && g_shared->interaction_hook_active ? 1u : 0u;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->effects_stabilized));
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->updates));
}

void apply_controller_gameplay_ray(float* origin, float* direction) {
    if (g_shared) {
        InterlockedIncrement(
            reinterpret_cast<volatile LONG*>(&g_shared->interaction_ray_calls));
    }
    VrCameraControls controls{};
    if (!origin || !direction || !read_controls(controls) || !controls.enabled ||
        !controls.hand_pointer_enabled) return;

    float offset[3]{};
    float controller_direction[3]{};
    bool valid = false;
    AcquireSRWLockShared(&g_game_ray_lock);
    valid = g_controller_game_ray_valid;
    std::copy(std::begin(g_controller_ray_offset), std::end(g_controller_ray_offset),
              std::begin(offset));
    std::copy(std::begin(g_controller_ray_direction),
              std::end(g_controller_ray_direction), std::begin(controller_direction));
    ReleaseSRWLockShared(&g_game_ray_lock);
    if (!valid) return;

    for (int axis = 0; axis < 3; ++axis) {
        origin[axis] += offset[axis];
        direction[axis] = controller_direction[axis];
    }
    if (g_shared) {
        InterlockedIncrement(
            reinterpret_cast<volatile LONG*>(&g_shared->interaction_ray_overrides));
    }
}

Matrix4 floor_tilt_correction(float degrees) {
    const float clamped = std::clamp(degrees, -45.0f, 45.0f);
    const float radians = clamped * 0.01745329251994329576923690768489f;
    if (std::fabs(radians) < 0.00001f) return identity_matrix();

    Matrix4 result = identity_matrix();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    result.value[5] = c;
    result.value[6] = s;
    result.value[9] = -s;
    result.value[10] = c;
    return result;
}

Matrix4 apply_floor_tilt_correction(const Matrix4& delta, const VrCameraControls& controls) {
    const Matrix4 correction = floor_tilt_correction(controls.floor_tilt_degrees);
    return multiply(correction, delta);
}

void make_yaw_only_tracking_origin(const float current[12], float origin[12]) {
    std::copy(current, current + 12, origin);

    float forward[3]{
        -current[2],
        -current[6],
        -current[10],
    };
    forward[1] = 0.0f;
    float forward_length = std::sqrt(forward[0] * forward[0] +
                                     forward[2] * forward[2]);
    if (forward_length < 0.0001f) {
        forward[0] = 0.0f;
        forward[2] = -1.0f;
        forward_length = 1.0f;
    }
    forward[0] /= forward_length;
    forward[2] /= forward_length;

    const float right[3]{
        -forward[2],
        0.0f,
        forward[0],
    };

    origin[0] = right[0];
    origin[4] = right[1];
    origin[8] = right[2];
    origin[1] = 0.0f;
    origin[5] = 1.0f;
    origin[9] = 0.0f;
    origin[2] = -forward[0];
    origin[6] = -forward[1];
    origin[10] = -forward[2];
    origin[3] = current[3];
    origin[7] = current[7];
    origin[11] = current[11];
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

    AcquireSRWLockExclusive(&g_pose_lock);
    bool recentered = false;
    if (!g_hmd_origin_valid || controls.recenter_sequence != g_recenter_sequence) {
        std::copy(std::begin(current), std::end(current), std::begin(g_hmd_origin_pose));
        g_recenter_sequence = controls.recenter_sequence;
        g_hmd_origin_valid = true;
        recentered = true;
        std::ostringstream out;
        out << "hmd recenter: raw origin seq=" << controls.recenter_sequence
            << " poseY=" << std::fixed << std::setprecision(3) << current[7];
        render_log(out.str());
    }
    g_hmd_view_delta = hytalevr::relative_view_pose(
        g_hmd_origin_pose, current,
        std::clamp(controls.translation_scale, 0.0f, 10.0f),
        std::clamp(controls.translation_y_scale, 0.0f, 10.0f),
        controls.invert_translation_xz != 0);
    const ULONGLONG now = GetTickCount64();
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
    const bool pose_active = pose_error == vr::VRInputError_None && right_pose.bActive &&
        right_pose.pose.bDeviceIsConnected && right_pose.pose.bPoseIsValid;

    uint64_t left_button_mask = 0;
    uint64_t right_button_mask = 0;
    bool legacy_left_x = false;
    if (g_vr_system) {
        const vr::TrackedDeviceIndex_t left_controller =
            g_vr_system->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand);
        if (left_controller != vr::k_unTrackedDeviceIndexInvalid) {
            vr::VRControllerState_t state{};
            if (g_vr_system->GetControllerState(left_controller, &state, sizeof(state))) {
                left_button_mask = state.ulButtonPressed;
                legacy_left_x =
                    (left_button_mask & vr::ButtonMaskFromId(vr::k_EButton_A)) != 0 ||
                    (left_button_mask & vr::ButtonMaskFromId(vr::k_EButton_ApplicationMenu)) != 0;
            }
        }
        const vr::TrackedDeviceIndex_t right_controller =
            g_vr_system->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_RightHand);
        if (right_controller != vr::k_unTrackedDeviceIndexInvalid) {
            vr::VRControllerState_t state{};
            if (g_vr_system->GetControllerState(right_controller, &state, sizeof(state))) {
                right_button_mask = state.ulButtonPressed;
            }
        }
    }

    const bool active_input = move_active || turn_active || trigger_active ||
        left_trigger_active || left_grip_active || right_grip_active ||
        x_active || legacy_left_x || y_active || b_active || right_stick_click_active ||
        pose_active;
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
    g_shared->controller_left_grip = g_left_grip_pressed ? 1u : 0u;
    g_shared->controller_right_grip = g_right_grip_pressed ? 1u : 0u;
    g_shared->controller_right_stick_click =
        right_stick_click_active && right_stick_click.bState ? 1u : 0u;
    g_shared->controller_left_button_mask = left_button_mask;
    g_shared->controller_right_button_mask = right_button_mask;
    g_shared->controller_right_pose_active = pose_active ? 1u : 0u;
    AcquireSRWLockExclusive(&g_pose_lock);
    g_right_controller_valid = pose_active;
    if (pose_active) {
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 4; ++column) {
                g_right_controller_pose[row * 4 + column] =
                    right_pose.pose.mDeviceToAbsoluteTracking.m[row][column];
            }
        }
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
        update_hmd_pose(render_poses[vr::k_unTrackedDeviceIndex_Hmd], controls);
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

bool prepare_render_resolution(void* window, const VrCameraControls& controls) {
    (void)controls;
    if (!g_shared || !g_vr_system || !window) {
        if (g_shared) g_shared->resolution_error = 1;
        return false;
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
    // Keep capture textures 1:1 with Hytale's real backbuffer. The old
    // SteamVR-sized rescale path could leave screen-space/UI content as a
    // transparent 2D layer that popped when the head pose changed.
    g_shared->recommended_width = static_cast<uint32_t>(pixel_width);
    g_shared->recommended_height = static_cast<uint32_t>(pixel_height);
    g_shared->backbuffer_width = pixel_width;
    g_shared->backbuffer_height = pixel_height;
    g_eye_target_width = pixel_width;
    g_eye_target_height = pixel_height;
    g_shared->resolution_error = 0;
    static int last_logged_width = 0;
    static int last_logged_height = 0;
    if (last_logged_width != pixel_width || last_logged_height != pixel_height) {
        std::ostringstream out;
        out << "resolution recommendedSteamVR=" << recommended_width << "x"
            << recommended_height << " backbuffer=" << pixel_width << "x"
            << pixel_height << " target=" << g_eye_target_width << "x"
            << g_eye_target_height << " renderScale=" << controls.render_scale;
        render_log(out.str());
        last_logged_width = pixel_width;
        last_logged_height = pixel_height;
    }
    return true;
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
    if (g_capture_fbo && g_resolve_fbo) return true;
    GLuint framebuffers[2]{};
    g_gl_gen_framebuffers(2, framebuffers);
    g_capture_fbo = framebuffers[0];
    g_resolve_fbo = framebuffers[1];
    return g_capture_fbo != 0 && g_resolve_fbo != 0;
}

bool ensure_ui_overlay(float width_meters = 1.50f, float distance_meters = 1.65f) {
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

    vr::HmdMatrix34_t transform{};
    transform.m[0][0] = 1.0f;
    transform.m[1][1] = 1.0f;
    transform.m[2][2] = 1.0f;
    transform.m[2][3] = -std::clamp(distance_meters, 0.35f, 6.0f);
    overlay->SetOverlayTransformTrackedDeviceRelative(
        g_ui_overlay, vr::k_unTrackedDeviceIndex_Hmd, &transform);
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

    if (!ensure_ui_overlay(controls.ui_overlay_width, controls.ui_overlay_distance)) {
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
    if (g_noesis_begin_trampoline) g_noesis_begin_trampoline(device);
}

void __fastcall hook_noesis_end_onscreen(void* device) {
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

bool controller_pointer_pixel(int eye, int width, int height, float distance,
                              int& pixel_x, int& pixel_y,
                              int& surface_width, int& surface_height,
                              bool& menu_mode,
                              bool allow_menu_overlay = true) {
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
    float absolute[3]{};
    for (int row = 0; row < 3; ++row) {
        absolute[row] = controller[row * 4 + 3] -
                        controller[row * 4 + 2] * pointer_distance;
    }

    float head[3]{};
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            head[column] += hmd[row * 4 + column] *
                (absolute[row] - hmd[row * 4 + 3]);
        }
    }

    const auto eye_to_head = g_vr_system->GetEyeToHeadTransform(
        eye == 0 ? vr::Eye_Left : vr::Eye_Right);
    float eye_point[4]{0.0f, 0.0f, 0.0f, 1.0f};
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            eye_point[column] += eye_to_head.m[row][column] *
                (head[row] - eye_to_head.m[row][3]);
        }
    }

    const auto projection = g_vr_system->GetProjectionMatrix(
        eye == 0 ? vr::Eye_Left : vr::Eye_Right, 0.05f, 100.0f);
    float clip[4]{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            clip[row] += projection.m[row][column] * eye_point[column];
        }
    }
    if (!std::isfinite(clip[3]) || clip[3] <= 0.001f) return false;
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

    const auto eye_to_head = g_vr_system->GetEyeToHeadTransform(
        eye == 0 ? vr::Eye_Left : vr::Eye_Right);
    float eye_point[4]{0.0f, 0.0f, 0.0f, 1.0f};
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            eye_point[column] += eye_to_head.m[row][column] *
                (head[row] - eye_to_head.m[row][3]);
        }
    }

    const auto projection = g_vr_system->GetProjectionMatrix(
        eye == 0 ? vr::Eye_Left : vr::Eye_Right, 0.05f, 100.0f);
    float clip[4]{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            clip[row] += projection.m[row][column] * eye_point[column];
        }
    }
    if (!std::isfinite(clip[3]) || clip[3] <= 0.001f) return false;
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

    const auto eye_to_head = g_vr_system->GetEyeToHeadTransform(
        eye == 0 ? vr::Eye_Left : vr::Eye_Right);
    float eye_point[4]{0.0f, 0.0f, 0.0f, 1.0f};
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            eye_point[column] += eye_to_head.m[row][column] *
                (head[row] - eye_to_head.m[row][3]);
        }
    }

    const auto projection = g_vr_system->GetProjectionMatrix(
        eye == 0 ? vr::Eye_Left : vr::Eye_Right, 0.05f, 100.0f);
    float clip[4]{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            clip[row] += projection.m[row][column] * eye_point[column];
        }
    }
    if (!std::isfinite(clip[3]) || clip[3] <= 0.001f) return false;
    ndc_x = clip[0] / clip[3];
    ndc_y = clip[1] / clip[3];
    depth = clip[3];
    return std::isfinite(ndc_x) && std::isfinite(ndc_y) &&
           std::isfinite(depth) &&
           ndc_x >= -1.3f && ndc_x <= 1.3f &&
           ndc_y >= -1.3f && ndc_y <= 1.3f;
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

void draw_controller_pointer(int eye, int width, int height, float distance,
                             bool publish_state = true) {
    int x = 0;
    int y = 0;
    int surface_width = 0;
    int surface_height = 0;
    bool menu_mode = false;
    if (!controller_pointer_pixel(eye, width, height, distance, x, y,
                                  surface_width, surface_height, menu_mode,
                                  false)) {
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

bool capture_eye(int eye, const VrCameraControls& controls) {
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
    // Keep the game window untouched. Each eye owns an independent SteamVR-sized
    // texture, so changing quality cannot alter the game's aspect ratio.
    const int width = g_eye_target_width > 0 ? g_eye_target_width : source_width;
    const int height = g_eye_target_height > 0 ? g_eye_target_height : source_height;

    if (width != g_texture_width || height != g_texture_height) {
        if (g_eye_textures[0] || g_eye_textures[1]) glDeleteTextures(2, g_eye_textures);
        g_eye_textures[0] = g_eye_textures[1] = 0;
        g_eye_valid[0] = g_eye_valid[1] = false;
        g_pre_ui_captured[0] = g_pre_ui_captured[1] = false;
        g_texture_width = width;
        g_texture_height = height;
    }

    GLint previous_texture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
    if (!g_eye_textures[eye]) {
        glGenTextures(1, &g_eye_textures[eye]);
        glBindTexture(GL_TEXTURE_2D, g_eye_textures[eye]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE_VALUE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE_VALUE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
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
            << " target=" << width << "x" << height
            << " samples=" << samples
            << " readFbo=" << read_fbo_for_log
            << " drawFbo=" << draw_fbo_for_log
            << " captureFbo=" << g_capture_fbo
            << " resolveFbo=" << g_resolve_fbo
            << " tex=" << g_eye_textures[eye]
            << " currentEye=" << (g_shared ? g_shared->current_eye : 0)
            << " stereoFrames=" << (g_shared ? g_shared->stereo_frames : 0);
        render_log(out.str());
        g_last_capture_diag_tick = capture_log_now;
    }

    glBindTexture(GL_TEXTURE_2D, g_eye_textures[eye]);
    if (initialize_gl_capture()) {
        GLint previous_read_fbo = 0;
        GLint previous_draw_fbo = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_VALUE, &previous_read_fbo);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_VALUE, &previous_draw_fbo);
        g_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER_VALUE, g_capture_fbo);
        g_gl_framebuffer_texture_2d(GL_DRAW_FRAMEBUFFER_VALUE, GL_COLOR_ATTACHMENT0_VALUE,
                                    GL_TEXTURE_2D, g_eye_textures[eye], 0);
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
        while (glGetError() != GL_NO_ERROR) {}
        int overlay_source_x = viewport[0];
        int overlay_source_y = viewport[1];
        bool overlay_source_multisampled = samples > 1;
        if (samples > 1 && (source_width != width || source_height != height)) {
            if (!g_resolve_texture || g_resolve_width != source_width ||
                g_resolve_height != source_height) {
                if (g_resolve_texture) glDeleteTextures(1, &g_resolve_texture);
                glGenTextures(1, &g_resolve_texture);
                glBindTexture(GL_TEXTURE_2D, g_resolve_texture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE_VALUE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE_VALUE);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, source_width, source_height, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                g_resolve_width = source_width;
                g_resolve_height = source_height;
            }
            g_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER_VALUE, g_resolve_fbo);
            g_gl_framebuffer_texture_2d(GL_DRAW_FRAMEBUFFER_VALUE, GL_COLOR_ATTACHMENT0_VALUE,
                                        GL_TEXTURE_2D, g_resolve_texture, 0);
            if (g_gl_check_framebuffer_status(GL_DRAW_FRAMEBUFFER_VALUE) ==
                GL_FRAMEBUFFER_COMPLETE_VALUE) {
                g_gl_blit_framebuffer(viewport[0], viewport[1], viewport[0] + source_width,
                                      viewport[1] + source_height, 0, 0,
                                      source_width, source_height,
                                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
                g_gl_bind_framebuffer(GL_READ_FRAMEBUFFER_VALUE, g_resolve_fbo);
                g_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER_VALUE, g_capture_fbo);
                g_gl_blit_framebuffer(0, 0, source_width, source_height,
                                      0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
                overlay_source_x = 0;
                overlay_source_y = 0;
                overlay_source_multisampled = false;
            } else {
                g_gl_bind_framebuffer(GL_READ_FRAMEBUFFER_VALUE,
                                      static_cast<GLuint>(previous_read_fbo));
                g_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER_VALUE,
                                      static_cast<GLuint>(previous_draw_fbo));
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture));
                if (g_shared) g_shared->capture_error = 3;
                render_log("capture error: resolve framebuffer incomplete");
                g_eye_valid[eye] = false;
                return false;
            }
        } else {
            g_gl_blit_framebuffer(viewport[0], viewport[1], viewport[0] + source_width,
                                  viewport[1] + source_height, 0, 0, width, height,
                                  GL_COLOR_BUFFER_BIT, samples > 1 ? GL_NEAREST : GL_LINEAR);
        }
        if (controls.hand_pointer_enabled) {
            const bool menu_overlay_active = g_shared && g_shared->ui_overlay_active != 0;
            if (!menu_overlay_active && controls.hide_center_reticle) {
                suppress_center_reticle(overlay_source_x, overlay_source_y,
                                         source_width, source_height, width, height,
                                         overlay_source_multisampled);
            }
            const float pointer_distance = menu_overlay_active
                ? controls.ui_overlay_distance
                : controls.hand_pointer_distance;
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
                                             pointer_menu_mode);
                if (pointer_valid && pointer_menu_mode) {
                    if (eye == 0) {
                        publish_pointer(true, pointer_x, pointer_y,
                                        pointer_surface_width, pointer_surface_height,
                                        true);
                    }
                }
                draw_controller_pointer(eye, width, height, controls.hand_pointer_distance, false);
            } else {
                draw_controller_pointer(eye, width, height, pointer_distance);
            }
        } else if (g_shared) {
            publish_pointer(false, 0, 0, 0, 0, false);
        }
        const GLenum blit_error = glGetError();
        g_gl_bind_framebuffer(GL_READ_FRAMEBUFFER_VALUE, static_cast<GLuint>(previous_read_fbo));
        g_gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER_VALUE, static_cast<GLuint>(previous_draw_fbo));
        if (blit_error != GL_NO_ERROR) {
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture));
            if (g_shared) g_shared->capture_error = static_cast<int32_t>(blit_error);
            {
                std::ostringstream out;
                out << "capture error: glBlit/copy GL error=0x" << std::hex << blit_error
                    << std::dec << " eye=" << eye << " source=" << source_width << "x"
                    << source_height << " target=" << width << "x" << height;
                render_log(out.str());
            }
            g_eye_valid[eye] = false;
            return false;
        }
    } else {
        if (width != source_width || height != source_height) {
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture));
            if (g_shared) g_shared->capture_error = 4;
            render_log("capture error: GL capture unavailable and source/target sizes differ");
            g_eye_valid[eye] = false;
            return false;
        }
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
    MEMORY_BASIC_INFORMATION memory{};
    const bool camera_is_writable = g_primary_camera &&
        VirtualQuery(static_cast<unsigned char*>(g_primary_camera) + 0x2E0,
                     &memory, sizeof(memory)) == sizeof(memory) &&
        memory.State == MEM_COMMIT && (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0;
    const DWORD camera_protection = memory.Protect & 0xff;
    const bool protection_is_writable = camera_protection == PAGE_READWRITE ||
        camera_protection == PAGE_WRITECOPY || camera_protection == PAGE_EXECUTE_READWRITE ||
        camera_protection == PAGE_EXECUTE_WRITECOPY;
    if (camera_is_writable && protection_is_writable && g_center_view_projection_valid &&
        now - g_last_primary_camera_tick < kBetweenFrameRestoreMaxAgeMs) {
        if (g_culling_view_valid) {
            std::memcpy(static_cast<unsigned char*>(g_primary_camera) + 0x2E0,
                        &g_culling_view, sizeof(g_culling_view));
            g_last_written_culling_view = g_culling_view;
            g_last_written_culling_object = g_primary_camera;
            g_last_written_culling_tick = now;
            g_last_written_culling_view_valid = true;
        } else if (g_neutral_view_valid) {
            std::memcpy(static_cast<unsigned char*>(g_primary_camera) + 0x2E0,
                        &g_neutral_view, sizeof(g_neutral_view));
        }
        const Matrix4& between_frame_view_projection =
            g_culling_view_projection_valid ? g_culling_view_projection : g_center_view_projection;
        if (g_culling_projection_valid) {
            std::memcpy(static_cast<unsigned char*>(g_primary_camera) + 0x4E0,
                        &g_culling_projection, sizeof(g_culling_projection));
        }
        std::memcpy(static_cast<unsigned char*>(g_primary_camera) + 0x320,
                    &between_frame_view_projection, sizeof(between_frame_view_projection));
    }
    ReleaseSRWLockExclusive(&g_camera_lock);
}

void shutdown_stereo() {
    if (g_ui_overlay != vr::k_ulOverlayHandleInvalid) {
        if (auto* overlay = vr::VROverlay()) overlay->HideOverlay(g_ui_overlay);
    }
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
    if (g_resolve_texture) {
        glDeleteTextures(1, &g_resolve_texture);
        g_resolve_texture = 0;
    }
    if ((g_capture_fbo || g_resolve_fbo) && g_gl_delete_framebuffers) {
        const GLuint framebuffers[2]{g_capture_fbo, g_resolve_fbo};
        g_gl_delete_framebuffers(2, framebuffers);
        g_capture_fbo = g_resolve_fbo = 0;
    }
    g_eye_valid[0] = g_eye_valid[1] = false;
    g_pre_ui_captured[0] = g_pre_ui_captured[1] = false;
    g_texture_width = g_texture_height = 0;
    g_resolve_width = g_resolve_height = 0;
    g_eye_target_width = g_eye_target_height = 0;
    g_consecutive_submit_failures = 0;
    g_last_successful_submit_tick = 0;
    g_last_swap_recenter_sequence = 0;
    InterlockedExchange(&g_render_eye, 0);
    if (g_vr_system) {
        vr::VR_Shutdown();
        g_vr_system = nullptr;
    }
    AcquireSRWLockExclusive(&g_pose_lock);
    g_hmd_view_delta = identity_matrix();
    g_hmd_origin_valid = false;
    g_hmd_current_valid = false;
    g_right_controller_valid = false;
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
    ReleaseSRWLockExclusive(&g_camera_lock);
    g_aggressive_native_view_valid = false;
    g_last_observed_native_yaw_valid = false;
    g_aggressive_suppressed_snaps = 0;
    g_last_aggressive_native_delta = 0.0f;
    g_runtime_active = false;
    g_actions_ready = false;
    g_action_set = vr::k_ulInvalidActionSetHandle;
    g_action_move = vr::k_ulInvalidActionHandle;
    g_action_turn = vr::k_ulInvalidActionHandle;
    g_action_jump = vr::k_ulInvalidActionHandle;
    g_action_sprint = vr::k_ulInvalidActionHandle;
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
    g_left_grip_pressed = false;
    g_right_grip_pressed = false;
    AcquireSRWLockExclusive(&g_game_ray_lock);
    g_controller_game_ray_valid = false;
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
    if (g_shared) InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->swap_calls));
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
            << " runtime=" << (g_runtime_active ? 1 : 0)
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
        publish_ui_scale_neutral();
        restore_neutral_camera(true);
        if (g_runtime_active) shutdown_stereo();
        restore_noesis_hooks();
        const bool interaction_restored = restore_interaction_hook();
        const bool camera_restored = interaction_restored && restore_camera_hook();
        // Keep the swap callback installed if the camera patch could not be
        // restored yet; the next frame then provides a safe retry point.
        const bool swap_restored = camera_restored && restore_swap_hook();
        g_shared->swap_hook_active = swap_restored ? 0u : 1u;
        g_shared->hook_active = camera_restored && swap_restored ? 0u : 1u;
        g_shared->hook_error = camera_restored && swap_restored ? 0 : 3;
        if (g_swap_trampoline) g_swap_trampoline(window);
        return;
    }
    if (have_controls && (!controls.enabled || controls.shutdown_requested)) {
        publish_ui_scale_neutral();
        restore_neutral_camera(true);
        if (g_runtime_active) shutdown_stereo();
    } else if (have_controls && controls.enabled && controls.non_vr_mode) {
        publish_render_filters_only(controls);
        if (g_runtime_active) shutdown_stereo();
        if (g_shared) {
            g_shared->stereo_active = 0;
            g_shared->stereo_error = 0;
        }
        if (g_swap_trampoline) g_swap_trampoline(window);
        return;
    } else if (have_controls && controls.enabled && initialize_stereo()) {
        g_runtime_active = true;
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
        const int eye = static_cast<int>(InterlockedCompareExchange(&g_render_eye, 0, 0) & 1);
        if (eye == 0 && !prepare_render_resolution(window, controls)) {
            restore_neutral_camera();
            if (g_swap_trampoline) g_swap_trampoline(window);
            return;
        }
        const bool captured = g_pre_ui_captured[eye] || capture_eye(eye, controls);
        g_pre_ui_captured[eye] = false;
        restore_neutral_camera();
        if (captured) {
            if (eye == 0) {
                submit_menu_overlay_texture(g_eye_textures[0], controls);
                InterlockedExchange(&g_render_eye, 1);
                g_shared->current_eye = 1;
            } else if (submit_eye_pair()) {
                synchronize_openvr(controls);
                InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->completed_pairs));
                InterlockedExchange(&g_render_eye, 0);
                g_shared->current_eye = 0;
            } else {
                InterlockedExchange(&g_render_eye, 0);
                g_shared->current_eye = 0;
            }
        } else {
            g_eye_valid[0] = false;
            g_eye_valid[1] = false;
            g_pre_ui_captured[0] = g_pre_ui_captured[1] = false;
            InterlockedExchange(&g_render_eye, 0);
            g_shared->current_eye = 0;
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

bool install_interaction_hook() {
    auto* executable = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (!executable) return false;

    g_interaction_hook_target = executable + 0x4BF5F1;
    const unsigned char expected[15] = {
        0x0F, 0x29, 0xBD, 0x20, 0xFE, 0xFF, 0xFF,
        0x44, 0x0F, 0x29, 0x85, 0x10, 0xFE, 0xFF, 0xFF,
    };
    if (std::memcmp(g_interaction_hook_target, expected, sizeof(expected)) != 0) {
        g_interaction_hook_target = nullptr;
        return false;
    }
    std::memcpy(g_interaction_original.data(), g_interaction_hook_target,
                g_interaction_original.size());

    auto* code = static_cast<unsigned char*>(g_interaction_hook_memory);
    const bool allocated_code = code == nullptr;
    if (allocated_code) {
        code = static_cast<unsigned char*>(
            VirtualAlloc(nullptr, 512, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!code) return false;
        g_interaction_hook_memory = code;
        unsigned char* p = code;

        // Preserve Hytale's two aligned stores before calling the C++ bridge.
        std::memcpy(p, expected, sizeof(expected));
        p += sizeof(expected);
        const unsigned char prefix[] = {
            0x9C, 0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53,
            0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00,
            0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x20,
            0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x30,
            0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x40,
            0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x50,
            0xF3, 0x0F, 0x7F, 0x64, 0x24, 0x60,
            0xF3, 0x0F, 0x7F, 0x6C, 0x24, 0x70,
            0x48, 0x8D, 0x8D, 0x20, 0xFE, 0xFF, 0xFF,
            0x48, 0x8D, 0x95, 0x10, 0xFE, 0xFF, 0xFF,
            0x48, 0xB8,
        };
        std::memcpy(p, prefix, sizeof(prefix));
        p += sizeof(prefix);
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
    auto* executable = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (!executable) return false;

    g_hook_target = executable + 0x5EC7F3;
    const unsigned char expected[16] = {
        0x0F, 0x28, 0xB4, 0x24, 0x80, 0x03, 0x00, 0x00,
        0x0F, 0x28, 0xBC, 0x24, 0x70, 0x03, 0x00, 0x00,
    };
    if (std::memcmp(g_hook_target, expected, sizeof(expected)) != 0) {
        g_hook_target = nullptr;
        return false;
    }
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
            0x0F, 0x28, 0xB4, 0x24, 0x80, 0x03, 0x00, 0x00,
            0x0F, 0x28, 0xBC, 0x24, 0x70, 0x03, 0x00, 0x00,
            0x48, 0xB8,
        };
        std::memcpy(p, suffix, sizeof(suffix));
        p += sizeof(suffix);
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
    DWORD old_protect = 0;
    SuspendedProcessThreads suspended_threads;
    if (!VirtualProtect(g_hook_target, g_original.size(), PAGE_EXECUTE_READWRITE,
                        &old_protect)) return false;
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
    DWORD old_protect = 0;
    SuspendedProcessThreads suspended_threads;
    if (!VirtualProtect(g_interaction_hook_target, g_interaction_original.size(),
                        PAGE_EXECUTE_READWRITE, &old_protect)) return false;
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
    ReleaseSRWLockExclusive(&g_game_ray_lock);
    if (g_shared) {
        g_shared->interaction_hook_active = 0;
        g_shared->controller_ray_active = 0;
    }
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
    DWORD old_protect = 0;
    SuspendedProcessThreads suspended_threads;
    if (!VirtualProtect(g_swap_target, g_swap_original.size(), PAGE_EXECUTE_READWRITE,
                        &old_protect)) return false;
    std::memcpy(g_swap_target, g_swap_original.data(), g_swap_original.size());
    DWORD unused = 0;
    VirtualProtect(g_swap_target, g_swap_original.size(), old_protect, &unused);
    FlushInstructionCache(GetCurrentProcess(), g_swap_target, g_swap_original.size());
    g_swap_target = nullptr;
    // The trampoline remains allocated because this callback still returns
    // through it after restoring SDL's original export wrapper.
    return true;
}

DWORD WINAPI worker(void*) {
    render_log("worker start: opening shared mapping");
    g_mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, kVrCameraMappingName);
    if (!g_mapping) {
        render_log("worker abort: OpenFileMapping failed");
        return 0;
    }
    g_shared = static_cast<VrCameraShared*>(
        MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(VrCameraShared)));
    if (!g_shared || g_shared->magic != kVrCameraMagic ||
        g_shared->version != kVrCameraVersion) {
        render_log("worker abort: shared mapping magic/version mismatch");
        return 0;
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
        g_noesis_hooks_active = install_noesis_hooks();
        render_log(std::string("install_all_hooks noesis=") +
                   (g_noesis_hooks_active ? "1" : "0"));
        if (!install_swap_hook()) {
            restore_noesis_hooks();
            const bool interaction_restored = restore_interaction_hook();
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
    install_all_hooks();
    for (;;) {
        VrCameraControls controls{};
        if (!read_controls(controls)) {
            Sleep(20);
            continue;
        }
        if (controls.unload_requested && g_shared->hook_active &&
            !g_shared->swap_hook_active) {
            const bool interaction_restored = restore_interaction_hook();
            const bool restored = interaction_restored && restore_camera_hook();
            g_shared->hook_active = restored ? 0u : 1u;
            g_shared->hook_error = restored ? 0 : 3;
        } else if (!g_shared->hook_active &&
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
        if (render_logging_enabled()) {
            std::lock_guard<std::mutex> lock(g_render_log_mutex);
            std::ofstream clear_log(render_log_path(), std::ios_base::trunc);
            if (clear_log.is_open()) {
                clear_log << GetTickCount64() << " DLL_PROCESS_ATTACH v120 native hand\n";
            }
        }
        HANDLE thread = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        render_log("DLL_PROCESS_DETACH");
        publish_ui_scale_neutral();
        if (g_ui_scale_shared) {
            UnmapViewOfFile(g_ui_scale_shared);
            g_ui_scale_shared = nullptr;
        }
        if (g_ui_scale_mapping) {
            CloseHandle(g_ui_scale_mapping);
            g_ui_scale_mapping = nullptr;
        }
    }
    return TRUE;
}
