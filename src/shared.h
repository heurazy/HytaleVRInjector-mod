#pragma once

#include <cstdint>

constexpr wchar_t kMappingName[] = L"Local\\HytaleCameraProbe_v1";
constexpr uint32_t kSharedMagic = 0x48594350; // HYCP
constexpr uint32_t kSharedVersion = 1;
constexpr uint32_t kMaxUniforms = 256;
constexpr uint32_t kFlagPatchGlTables = 1u << 0;
constexpr uint32_t kFlagPatchKnownGlTable = 1u << 1;

struct UniformRecord {
    uint32_t program = 0;
    int32_t location = -1;
    char name[64]{};
};

struct CameraProbeShared {
    uint32_t magic = kSharedMagic;
    uint32_t version = kSharedVersion;
    uint32_t probe_pid = 0;
    uint32_t flags = 0;

    uint32_t sdl_hooked = 0;
    uint32_t get_uniform_location_seen = 0;
    uint32_t matrix_uploads = 0;
    uint32_t camera_position_uploads = 0;
    uint32_t override_uploads = 0;
    uint32_t uniform_count = 0;
    uint32_t patched_gl_pointers = 0;

    uint32_t current_program = 0;
    int32_t last_location = -1;
    char last_uniform_name[64]{};
    char last_gl_name[64]{};
    char status[256]{};

    float override_position[3]{};
    float override_rotation[3]{};
    float override_scale = 1.0f;
    uint32_t override_enabled = 0;
    uint32_t override_position_enabled = 1;
    uint32_t override_matrix_enabled = 0;

    float last_camera_position[3]{};
    float last_matrix[16]{};

    UniformRecord uniforms[kMaxUniforms]{};
};
