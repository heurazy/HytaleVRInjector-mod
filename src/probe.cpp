#include "shared.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>

using GLint = int;
using GLuint = unsigned int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLfloat = float;

using SDL_GL_GetProcAddressFn = void* (__cdecl*)(const char*);
using GlUseProgramFn = void(APIENTRY*)(GLuint);
using GlGetUniformLocationFn = GLint(APIENTRY*)(GLuint, const char*);
using GlUniformMatrix4fvFn = void(APIENTRY*)(GLint, GLsizei, GLboolean, const GLfloat*);
using GlUniform3fFn = void(APIENTRY*)(GLint, GLfloat, GLfloat, GLfloat);
using GlUniform3fvFn = void(APIENTRY*)(GLint, GLsizei, const GLfloat*);

namespace {
CameraProbeShared* g_shared = nullptr;
HANDLE g_mapping = nullptr;
HMODULE g_module = nullptr;

SDL_GL_GetProcAddressFn g_sdl_get_proc = nullptr;
SDL_GL_GetProcAddressFn g_sdl_get_proc_trampoline = nullptr;
GlUseProgramFn g_gl_use_program = nullptr;
GlGetUniformLocationFn g_gl_get_uniform_location = nullptr;
GlUniformMatrix4fvFn g_gl_uniform_matrix4fv = nullptr;
GlUniform3fFn g_gl_uniform3f = nullptr;
GlUniform3fvFn g_gl_uniform3fv = nullptr;

std::array<unsigned char, 12> g_original_sdl_bytes{};
void* g_sdl_hook_target = nullptr;

void APIENTRY hook_gl_use_program(GLuint program);
GLint APIENTRY hook_gl_get_uniform_location(GLuint program, const char* name);
void APIENTRY hook_gl_uniform_matrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
void APIENTRY hook_gl_uniform3f(GLint location, GLfloat x, GLfloat y, GLfloat z);
void APIENTRY hook_gl_uniform3fv(GLint location, GLsizei count, const GLfloat* value);

void copy_text(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    strncpy_s(dst, dst_size, src, _TRUNCATE);
}

void set_status(const char* text) {
    if (g_shared) copy_text(g_shared->status, sizeof(g_shared->status), text);
}

bool contains_ci(const char* haystack, const char* needle) {
    if (!haystack || !needle) return false;
    const size_t needle_len = std::strlen(needle);
    if (needle_len == 0) return true;
    for (const char* h = haystack; *h; ++h) {
        size_t i = 0;
        while (i < needle_len && h[i] &&
               std::tolower(static_cast<unsigned char>(h[i])) ==
                   std::tolower(static_cast<unsigned char>(needle[i]))) {
            ++i;
        }
        if (i == needle_len) return true;
    }
    return false;
}

bool is_camera_uniform(const char* name) {
    return contains_ci(name, "camera") || contains_ci(name, "view") ||
           contains_ci(name, "projection") || contains_ci(name, "mvp");
}

bool is_camera_position_uniform(const char* name) {
    return contains_ci(name, "cameraposition") || contains_ci(name, "camera_position") ||
           contains_ci(name, "uCameraPosition");
}

bool is_camera_matrix_uniform(const char* name) {
    return contains_ci(name, "viewmatrix") || contains_ci(name, "viewprojection") ||
           contains_ci(name, "projectionmatrix") || contains_ci(name, "mvpmatrix") ||
           contains_ci(name, "uViewMatrix") || contains_ci(name, "uViewProjectionMatrix");
}

const char* uniform_name_for(GLuint program, GLint location) {
    if (!g_shared) return "";
    const uint32_t count = std::min(g_shared->uniform_count, kMaxUniforms);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& u = g_shared->uniforms[i];
        if (u.program == program && u.location == location) return u.name;
    }
    for (uint32_t i = 0; i < count; ++i) {
        const auto& u = g_shared->uniforms[i];
        if (u.location == location) return u.name;
    }
    return "";
}

void record_uniform(GLuint program, GLint location, const char* name) {
    if (!g_shared || location < 0 || !is_camera_uniform(name)) return;
    g_shared->get_uniform_location_seen++;
    g_shared->last_location = location;
    copy_text(g_shared->last_uniform_name, sizeof(g_shared->last_uniform_name), name);

    uint32_t count = std::min(g_shared->uniform_count, kMaxUniforms);
    for (uint32_t i = 0; i < count; ++i) {
        if (g_shared->uniforms[i].program == program && g_shared->uniforms[i].location == location) {
            copy_text(g_shared->uniforms[i].name, sizeof(g_shared->uniforms[i].name), name);
            return;
        }
    }
    if (count < kMaxUniforms) {
        auto& slot = g_shared->uniforms[count];
        slot.program = program;
        slot.location = location;
        copy_text(slot.name, sizeof(slot.name), name);
        g_shared->uniform_count = count + 1;
    }
}

void* make_trampoline(void* target, const unsigned char* original, size_t original_size) {
    auto* code = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, original_size + 12, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!code) return nullptr;
    std::memcpy(code, original, original_size);
    code[original_size + 0] = 0x48;
    code[original_size + 1] = 0xB8;
    *reinterpret_cast<void**>(code + original_size + 2) =
        static_cast<unsigned char*>(target) + original_size;
    code[original_size + 10] = 0xFF;
    code[original_size + 11] = 0xE0;
    FlushInstructionCache(GetCurrentProcess(), code, original_size + 12);
    return code;
}

bool install_abs_jump(void* target, void* detour, std::array<unsigned char, 12>& original) {
    DWORD old_protect = 0;
    if (!VirtualProtect(target, original.size(), PAGE_EXECUTE_READWRITE, &old_protect)) return false;
    std::memcpy(original.data(), target, original.size());
    auto* patch = static_cast<unsigned char*>(target);
    patch[0] = 0x48;
    patch[1] = 0xB8;
    *reinterpret_cast<void**>(patch + 2) = detour;
    patch[10] = 0xFF;
    patch[11] = 0xE0;
    DWORD unused = 0;
    VirtualProtect(target, original.size(), old_protect, &unused);
    FlushInstructionCache(GetCurrentProcess(), target, original.size());
    return true;
}

bool read_original_bytes(void* target, std::array<unsigned char, 12>& original) {
    std::memcpy(original.data(), target, original.size());
    return true;
}

bool is_scannable_memory(const MEMORY_BASIC_INFORMATION& mbi) {
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    switch (mbi.Protect & 0xff) {
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return true;
    default:
        return false;
    }
}

bool replace_pointer(void** slot, void* expected, void* replacement) {
    if (*slot != expected) return false;
    DWORD old_protect = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old_protect)) return false;
    if (*slot == expected) {
        *slot = replacement;
        DWORD unused = 0;
        VirtualProtect(slot, sizeof(void*), old_protect, &unused);
        FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
        return true;
    }
    DWORD unused = 0;
    VirtualProtect(slot, sizeof(void*), old_protect, &unused);
    return false;
}

uint32_t patch_known_gl_api_tables() {
    if (!g_sdl_get_proc_trampoline) return 0;

    if (!g_gl_use_program) {
        g_gl_use_program = reinterpret_cast<GlUseProgramFn>(g_sdl_get_proc_trampoline("glUseProgram"));
    }
    if (!g_gl_get_uniform_location) {
        g_gl_get_uniform_location =
            reinterpret_cast<GlGetUniformLocationFn>(g_sdl_get_proc_trampoline("glGetUniformLocation"));
    }
    if (!g_gl_uniform_matrix4fv) {
        g_gl_uniform_matrix4fv =
            reinterpret_cast<GlUniformMatrix4fvFn>(g_sdl_get_proc_trampoline("glUniformMatrix4fv"));
    }
    if (!g_gl_uniform3f) {
        g_gl_uniform3f = reinterpret_cast<GlUniform3fFn>(g_sdl_get_proc_trampoline("glUniform3f"));
    }
    if (!g_gl_uniform3fv) {
        g_gl_uniform3fv = reinterpret_cast<GlUniform3fvFn>(g_sdl_get_proc_trampoline("glUniform3fv"));
    }

    if (!g_gl_use_program || !g_gl_get_uniform_location || !g_gl_uniform_matrix4fv) return 0;

    constexpr size_t kUseProgram = 0x310;
    constexpr size_t kGetUniformLocation = 0x338;
    constexpr size_t kUniform3f = 0x388;
    constexpr size_t kUniform3fv = 0x3A8;
    constexpr size_t kUniformMatrix4fv = 0x3B8;
    constexpr size_t kRequiredSize = kUniformMatrix4fv + sizeof(void*);

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    MEMORY_BASIC_INFORMATION stack_mbi{};
    VirtualQuery(&si, &stack_mbi, sizeof(stack_mbi));

    auto* cursor = static_cast<unsigned char*>(si.lpMinimumApplicationAddress);
    auto* end = static_cast<unsigned char*>(si.lpMaximumApplicationAddress);
    uint32_t replaced = 0;
    uint32_t candidates = 0;

    while (cursor < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(cursor, &mbi, sizeof(mbi))) break;
        auto* base = static_cast<unsigned char*>(mbi.BaseAddress);
        auto* next = base + mbi.RegionSize;

        if (is_scannable_memory(mbi) && mbi.AllocationBase != g_module &&
            mbi.AllocationBase != stack_mbi.AllocationBase && mbi.RegionSize >= kRequiredSize) {
            for (auto* p = base; p + kRequiredSize <= next; p += sizeof(void*)) {
                auto** use_program = reinterpret_cast<void**>(p + kUseProgram);
                auto** get_uniform_location = reinterpret_cast<void**>(p + kGetUniformLocation);
                auto** uniform_matrix = reinterpret_cast<void**>(p + kUniformMatrix4fv);

                if (*use_program == reinterpret_cast<void*>(g_gl_use_program) &&
                    *get_uniform_location == reinterpret_cast<void*>(g_gl_get_uniform_location) &&
                    *uniform_matrix == reinterpret_cast<void*>(g_gl_uniform_matrix4fv)) {
                    ++candidates;
                    replaced += replace_pointer(use_program, reinterpret_cast<void*>(g_gl_use_program),
                                                reinterpret_cast<void*>(&hook_gl_use_program))
                                    ? 1
                                    : 0;
                    replaced += replace_pointer(get_uniform_location,
                                                reinterpret_cast<void*>(g_gl_get_uniform_location),
                                                reinterpret_cast<void*>(&hook_gl_get_uniform_location))
                                    ? 1
                                    : 0;
                    replaced += replace_pointer(reinterpret_cast<void**>(p + kUniform3f),
                                                reinterpret_cast<void*>(g_gl_uniform3f),
                                                reinterpret_cast<void*>(&hook_gl_uniform3f))
                                    ? 1
                                    : 0;
                    replaced += replace_pointer(reinterpret_cast<void**>(p + kUniform3fv),
                                                reinterpret_cast<void*>(g_gl_uniform3fv),
                                                reinterpret_cast<void*>(&hook_gl_uniform3fv))
                                    ? 1
                                    : 0;
                    replaced += replace_pointer(uniform_matrix,
                                                reinterpret_cast<void*>(g_gl_uniform_matrix4fv),
                                                reinterpret_cast<void*>(&hook_gl_uniform_matrix4fv))
                                    ? 1
                                    : 0;
                }
            }
        }
        cursor = next;
    }

    if (g_shared) {
        g_shared->patched_gl_pointers = replaced;
        char status[256]{};
        std::snprintf(status, sizeof(status),
                      "Patch cible table GL: %u candidats, %u pointeurs remplaces",
                      candidates, replaced);
        set_status(status);
    }
    return replaced;
}

uint32_t patch_existing_gl_tables() {
    if (!g_sdl_get_proc_trampoline) return 0;

    if (!g_gl_use_program) {
        g_gl_use_program = reinterpret_cast<GlUseProgramFn>(g_sdl_get_proc_trampoline("glUseProgram"));
    }
    if (!g_gl_get_uniform_location) {
        g_gl_get_uniform_location =
            reinterpret_cast<GlGetUniformLocationFn>(g_sdl_get_proc_trampoline("glGetUniformLocation"));
    }
    if (!g_gl_uniform_matrix4fv) {
        g_gl_uniform_matrix4fv =
            reinterpret_cast<GlUniformMatrix4fvFn>(g_sdl_get_proc_trampoline("glUniformMatrix4fv"));
    }
    if (!g_gl_uniform3f) {
        g_gl_uniform3f = reinterpret_cast<GlUniform3fFn>(g_sdl_get_proc_trampoline("glUniform3f"));
    }
    if (!g_gl_uniform3fv) {
        g_gl_uniform3fv = reinterpret_cast<GlUniform3fvFn>(g_sdl_get_proc_trampoline("glUniform3fv"));
    }

    struct PatchPair {
        void* original;
        void* hook;
    };
    const PatchPair patches[] = {
        {reinterpret_cast<void*>(g_gl_use_program), reinterpret_cast<void*>(&hook_gl_use_program)},
        {reinterpret_cast<void*>(g_gl_get_uniform_location), reinterpret_cast<void*>(&hook_gl_get_uniform_location)},
        {reinterpret_cast<void*>(g_gl_uniform_matrix4fv), reinterpret_cast<void*>(&hook_gl_uniform_matrix4fv)},
        {reinterpret_cast<void*>(g_gl_uniform3f), reinterpret_cast<void*>(&hook_gl_uniform3f)},
        {reinterpret_cast<void*>(g_gl_uniform3fv), reinterpret_cast<void*>(&hook_gl_uniform3fv)},
    };

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    MEMORY_BASIC_INFORMATION stack_mbi{};
    VirtualQuery(&si, &stack_mbi, sizeof(stack_mbi));
    auto* cursor = static_cast<unsigned char*>(si.lpMinimumApplicationAddress);
    auto* end = static_cast<unsigned char*>(si.lpMaximumApplicationAddress);
    uint32_t replaced = 0;

    while (cursor < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(cursor, &mbi, sizeof(mbi))) break;
        auto* base = static_cast<unsigned char*>(mbi.BaseAddress);
        auto* next = base + mbi.RegionSize;

        if (is_scannable_memory(mbi) && mbi.AllocationBase != g_module &&
            mbi.AllocationBase != stack_mbi.AllocationBase) {
            auto* first = reinterpret_cast<void**>(base);
            auto* last = reinterpret_cast<void**>(next - sizeof(void*));
            for (void** slot = first; slot <= last; ++slot) {
                for (const auto& patch : patches) {
                    if (patch.original && replace_pointer(slot, patch.original, patch.hook)) {
                        ++replaced;
                        break;
                    }
                }
            }
        }
        cursor = next;
    }

    if (g_shared) g_shared->patched_gl_pointers = replaced;
    return replaced;
}

void APIENTRY hook_gl_use_program(GLuint program) {
    if (g_shared) g_shared->current_program = program;
    if (g_gl_use_program) g_gl_use_program(program);
}

GLint APIENTRY hook_gl_get_uniform_location(GLuint program, const char* name) {
    const GLint location = g_gl_get_uniform_location ? g_gl_get_uniform_location(program, name) : -1;
    record_uniform(program, location, name);
    return location;
}

void APIENTRY hook_gl_uniform_matrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) {
    const char* name = uniform_name_for(g_shared ? g_shared->current_program : 0, location);
    if (g_shared && value && is_camera_matrix_uniform(name)) {
        g_shared->matrix_uploads++;
        g_shared->last_location = location;
        copy_text(g_shared->last_uniform_name, sizeof(g_shared->last_uniform_name), name);
        std::memcpy(g_shared->last_matrix, value, sizeof(float) * 16);
        if (g_shared->override_enabled && g_shared->override_matrix_enabled && count > 0) {
            float edited[16]{};
            std::memcpy(edited, value, sizeof(edited));
            edited[12] += g_shared->override_position[0];
            edited[13] += g_shared->override_position[1];
            edited[14] += g_shared->override_position[2];
            g_shared->override_uploads++;
            if (g_gl_uniform_matrix4fv) g_gl_uniform_matrix4fv(location, count, transpose, edited);
            return;
        }
    }
    if (g_gl_uniform_matrix4fv) g_gl_uniform_matrix4fv(location, count, transpose, value);
}

void APIENTRY hook_gl_uniform3f(GLint location, GLfloat x, GLfloat y, GLfloat z) {
    const char* name = uniform_name_for(g_shared ? g_shared->current_program : 0, location);
    if (g_shared && is_camera_position_uniform(name)) {
        g_shared->camera_position_uploads++;
        g_shared->last_camera_position[0] = x;
        g_shared->last_camera_position[1] = y;
        g_shared->last_camera_position[2] = z;
        if (g_shared->override_enabled && g_shared->override_position_enabled) {
            x += g_shared->override_position[0];
            y += g_shared->override_position[1];
            z += g_shared->override_position[2];
            g_shared->override_uploads++;
        }
    }
    if (g_gl_uniform3f) g_gl_uniform3f(location, x, y, z);
}

void APIENTRY hook_gl_uniform3fv(GLint location, GLsizei count, const GLfloat* value) {
    const char* name = uniform_name_for(g_shared ? g_shared->current_program : 0, location);
    if (g_shared && value && is_camera_position_uniform(name)) {
        g_shared->camera_position_uploads++;
        g_shared->last_camera_position[0] = value[0];
        g_shared->last_camera_position[1] = value[1];
        g_shared->last_camera_position[2] = value[2];
        if (g_shared->override_enabled && g_shared->override_position_enabled && count > 0) {
            float edited[3] = {
                value[0] + g_shared->override_position[0],
                value[1] + g_shared->override_position[1],
                value[2] + g_shared->override_position[2],
            };
            g_shared->override_uploads++;
            if (g_gl_uniform3fv) g_gl_uniform3fv(location, count, edited);
            return;
        }
    }
    if (g_gl_uniform3fv) g_gl_uniform3fv(location, count, value);
}

void* __cdecl hook_sdl_gl_get_proc_address(const char* name) {
    void* proc = g_sdl_get_proc_trampoline ? g_sdl_get_proc_trampoline(name) : nullptr;
    if (g_shared) copy_text(g_shared->last_gl_name, sizeof(g_shared->last_gl_name), name);
    if (!name || !proc) return proc;

    if (std::strcmp(name, "glUseProgram") == 0) {
        g_gl_use_program = reinterpret_cast<GlUseProgramFn>(proc);
        return reinterpret_cast<void*>(&hook_gl_use_program);
    }
    if (std::strcmp(name, "glGetUniformLocation") == 0) {
        g_gl_get_uniform_location = reinterpret_cast<GlGetUniformLocationFn>(proc);
        return reinterpret_cast<void*>(&hook_gl_get_uniform_location);
    }
    if (std::strcmp(name, "glUniformMatrix4fv") == 0) {
        g_gl_uniform_matrix4fv = reinterpret_cast<GlUniformMatrix4fvFn>(proc);
        return reinterpret_cast<void*>(&hook_gl_uniform_matrix4fv);
    }
    if (std::strcmp(name, "glUniform3f") == 0) {
        g_gl_uniform3f = reinterpret_cast<GlUniform3fFn>(proc);
        return reinterpret_cast<void*>(&hook_gl_uniform3f);
    }
    if (std::strcmp(name, "glUniform3fv") == 0) {
        g_gl_uniform3fv = reinterpret_cast<GlUniform3fvFn>(proc);
        return reinterpret_cast<void*>(&hook_gl_uniform3fv);
    }
    return proc;
}

bool init_shared() {
    g_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                   sizeof(CameraProbeShared), kMappingName);
    if (!g_mapping) return false;
    g_shared = static_cast<CameraProbeShared*>(
        MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(CameraProbeShared)));
    if (!g_shared) return false;
    if (g_shared->magic != kSharedMagic || g_shared->version != kSharedVersion) {
        *g_shared = CameraProbeShared{};
    }
    g_shared->probe_pid = GetCurrentProcessId();
    set_status("Probe charge, attente de SDL3.dll");
    return true;
}

DWORD WINAPI worker_thread(void*) {
    if (!init_shared()) return 0;

    HMODULE sdl = nullptr;
    for (int i = 0; i < 120000 && !sdl; ++i) {
        sdl = GetModuleHandleW(L"SDL3.dll");
        if (!sdl) Sleep(1);
    }
    if (!sdl) {
        set_status("SDL3.dll introuvable");
        return 0;
    }

    g_sdl_hook_target = reinterpret_cast<void*>(GetProcAddress(sdl, "SDL_GL_GetProcAddress"));
    if (!g_sdl_hook_target) {
        set_status("SDL_GL_GetProcAddress introuvable");
        return 0;
    }

    read_original_bytes(g_sdl_hook_target, g_original_sdl_bytes);
    g_sdl_get_proc_trampoline = reinterpret_cast<SDL_GL_GetProcAddressFn>(
        make_trampoline(g_sdl_hook_target, g_original_sdl_bytes.data(), g_original_sdl_bytes.size()));
    if (!g_sdl_get_proc_trampoline) {
        set_status("Creation trampoline impossible");
        return 0;
    }
    if (!install_abs_jump(g_sdl_hook_target, reinterpret_cast<void*>(&hook_sdl_gl_get_proc_address),
                          g_original_sdl_bytes)) {
        set_status("Installation hook SDL impossible");
        return 0;
    }

    g_sdl_get_proc = reinterpret_cast<SDL_GL_GetProcAddressFn>(g_sdl_hook_target);
    g_shared->sdl_hooked = 1;
    uint32_t patched = 0;
    if (g_shared->flags & kFlagPatchKnownGlTable) {
        patched += patch_known_gl_api_tables();
    }
    if (g_shared->flags & kFlagPatchGlTables) {
        patched += patch_existing_gl_tables();
    }
    char status[256]{};
    std::snprintf(status, sizeof(status),
                  "Hook SDL actif, patch cible %s, scan large %s, pointeurs: %u",
                  (g_shared->flags & kFlagPatchKnownGlTable) ? "actif" : "desactive",
                  (g_shared->flags & kFlagPatchGlTables) ? "actif" : "desactive",
                  patched);
    set_status(status);
    return 0;
}
} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, worker_thread, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
