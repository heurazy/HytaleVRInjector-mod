#include <windows.h>
#include <atomic>
#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <MinHook.h>

#include "../ui_scene_depth_policy.h"
#include "../ui_scale_shared.h"
#include "../vr_held_item.h"

// Basic OpenGL Type Definitions
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef char GLchar;
typedef int GLint;
typedef unsigned char GLboolean;
typedef float GLfloat;
typedef ptrdiff_t GLintptr;
typedef ptrdiff_t GLsizeiptr;
typedef unsigned int GLbitfield;

#define GL_PROGRAM 0x82E2

// Function Signatures
typedef void (WINAPI *glObjectLabel_t)(GLenum identifier, GLuint name, GLsizei length, const GLchar *label);
typedef void (WINAPI *glLinkProgram_t)(GLuint program);
typedef void (WINAPI *glUseProgram_t)(GLuint program);
typedef void (WINAPI *glDrawArrays_t)(GLenum mode, GLint first, GLsizei count);
typedef void (WINAPI *glDrawElements_t)(GLenum mode, GLsizei count, GLenum type, const void* indices);
typedef void (WINAPI *glUniformMatrix4fv_t)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
typedef void (WINAPI *glProgramUniformMatrix4fv_t)(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
typedef GLint (WINAPI *glGetUniformLocation_t)(GLuint program, const GLchar *name);
typedef void (WINAPI *glGetUniformiv_t)(GLuint program, GLint location, GLint *params);
typedef void (WINAPI *glGetUniformfv_t)(GLuint program, GLint location, GLfloat *params);
typedef GLint (WINAPI *glGetAttribLocation_t)(GLuint program, const GLchar *name);
typedef void (WINAPI *glGetVertexAttribiv_t)(GLuint index, GLenum pname, GLint *params);
typedef void (WINAPI *glGetVertexAttribPointerv_t)(GLuint index, GLenum pname, void **pointer);
typedef void (WINAPI *glGetNamedBufferSubData_t)(GLuint buffer, GLintptr offset, GLsizeiptr size, void *data);
typedef void (WINAPI *glGetNamedBufferParameteriv_t)(GLuint buffer, GLenum pname, GLint *params);
typedef void (WINAPI *glViewport_t)(GLint x, GLint y, GLsizei width, GLsizei height);
typedef GLboolean (WINAPI *glIsProgram_t)(GLuint program);
typedef void (WINAPI *glGetObjectLabel_t)(GLenum identifier, GLuint name, GLsizei bufSize, GLsizei *length, GLchar *label);
typedef void (WINAPI *glGetIntegerv_t)(GLenum pname, GLint *data);
typedef void (WINAPI *glGetFloatv_t)(GLenum pname, GLfloat *data);
typedef void (WINAPI *glGetTexLevelParameteriv_t)(GLenum target, GLint level, GLenum pname, GLint *params);
typedef void (WINAPI *glActiveTexture_t)(GLenum texture);
typedef void (WINAPI *glBindTexture_t)(GLenum target, GLuint texture);
typedef void (WINAPI *glGenTextures_t)(GLsizei n, GLuint *textures);
typedef void (WINAPI *glTexImage2D_t)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels);
typedef void (WINAPI *glTexParameteri_t)(GLenum target, GLenum pname, GLint param);
typedef void (WINAPI *glClearColor_t)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
typedef void (WINAPI *glClear_t)(GLbitfield mask);
typedef void (WINAPI *glBindFramebuffer_t)(GLenum target, GLuint framebuffer);
typedef void (WINAPI *glGenFramebuffers_t)(GLsizei n, GLuint *framebuffers);
typedef void (WINAPI *glFramebufferTexture2D_t)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef GLenum (WINAPI *glCheckFramebufferStatus_t)(GLenum target);
typedef void (WINAPI *glDrawBuffer_t)(GLenum mode);
typedef void (WINAPI *glReadBuffer_t)(GLenum mode);
typedef void (WINAPI *glDepthMask_t)(GLboolean flag);
typedef void (WINAPI *glColorMask_t)(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
typedef void (WINAPI *glEnable_t)(GLenum cap);
typedef void (WINAPI *glDisable_t)(GLenum cap);
typedef void (WINAPI *glClearDepth_t)(double depth);
typedef void (WINAPI *glDepthFunc_t)(GLenum function);
typedef void (WINAPI *glGetBooleanv_t)(GLenum pname, GLboolean *data);
typedef GLboolean (WINAPI *glIsEnabled_t)(GLenum cap);
typedef BOOL (WINAPI *GetCursorPos_t)(LPPOINT lpPoint);
typedef BOOL (WINAPI *GetClientRect_t)(HWND hWnd, LPRECT lpRect);
typedef BOOL (WINAPI *SetCursorPos_t)(int X, int Y);

// Buffer related function signatures
typedef void (WINAPI *glBindBuffer_t)(GLenum target, GLuint buffer);
typedef void (WINAPI *glBindBufferRange_t)(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size);
typedef void (WINAPI *glBindBufferBase_t)(GLenum target, GLuint index, GLuint buffer);
typedef void (WINAPI *glBufferSubData_t)(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
typedef void (WINAPI *glBufferData_t)(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
typedef void (WINAPI *glBufferStorage_t)(GLenum target, GLsizeiptr size, const void *data, GLbitfield flags);
typedef void* (WINAPI *glMapBuffer_t)(GLenum target, GLenum access);
typedef void* (WINAPI *glMapBufferRange_t)(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);
typedef GLboolean (WINAPI *glUnmapBuffer_t)(GLenum target);

void InitializeHooks();

#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_VIEWPORT 0x0BA2

// Real Function Pointers
glObjectLabel_t real_glObjectLabel = nullptr;
glLinkProgram_t real_glLinkProgram = nullptr;
glUseProgram_t real_glUseProgram = nullptr;
glDrawArrays_t real_glDrawArrays = nullptr;
glDrawElements_t real_glDrawElements = nullptr;
glUniformMatrix4fv_t real_glUniformMatrix4fv = nullptr;
glProgramUniformMatrix4fv_t real_glProgramUniformMatrix4fv = nullptr;
glGetUniformLocation_t real_glGetUniformLocation = nullptr;
glGetUniformiv_t real_glGetUniformiv = nullptr;
glGetUniformfv_t real_glGetUniformfv = nullptr;
glGetAttribLocation_t real_glGetAttribLocation = nullptr;
glGetVertexAttribiv_t real_glGetVertexAttribiv = nullptr;
glGetVertexAttribPointerv_t real_glGetVertexAttribPointerv = nullptr;
glGetNamedBufferSubData_t real_glGetNamedBufferSubData = nullptr;
glGetNamedBufferParameteriv_t real_glGetNamedBufferParameteriv = nullptr;
glViewport_t real_glViewport = nullptr;
glIsProgram_t real_glIsProgram = nullptr;
glGetObjectLabel_t real_glGetObjectLabel = nullptr;
glGetIntegerv_t real_glGetIntegerv = nullptr;
glGetFloatv_t real_glGetFloatv = nullptr;
glGetTexLevelParameteriv_t real_glGetTexLevelParameteriv = nullptr;
glActiveTexture_t real_glActiveTexture = nullptr;
glBindTexture_t real_glBindTexture = nullptr;
glGenTextures_t real_glGenTextures = nullptr;
glTexImage2D_t real_glTexImage2D = nullptr;
glTexParameteri_t real_glTexParameteri = nullptr;
glClearColor_t real_glClearColor = nullptr;
glClear_t real_glClear = nullptr;
glBindFramebuffer_t real_glBindFramebuffer = nullptr;
glGenFramebuffers_t real_glGenFramebuffers = nullptr;
glFramebufferTexture2D_t real_glFramebufferTexture2D = nullptr;
glCheckFramebufferStatus_t real_glCheckFramebufferStatus = nullptr;
glDrawBuffer_t raw_glDrawBuffer = nullptr;
glReadBuffer_t raw_glReadBuffer = nullptr;
glDepthMask_t raw_glDepthMask = nullptr;
glColorMask_t raw_glColorMask = nullptr;
glEnable_t raw_glEnable = nullptr;
glDisable_t raw_glDisable = nullptr;
glClearDepth_t raw_glClearDepth = nullptr;
glDepthFunc_t raw_glDepthFunc = nullptr;
glGetBooleanv_t raw_glGetBooleanv = nullptr;
glIsEnabled_t raw_glIsEnabled = nullptr;
GetCursorPos_t real_GetCursorPos = nullptr;
GetClientRect_t real_GetClientRect = nullptr;
SetCursorPos_t real_SetCursorPos = nullptr;

// Buffer function pointers
glBindBuffer_t real_glBindBuffer = nullptr;
glBindBufferRange_t real_glBindBufferRange = nullptr;
glBindBufferBase_t real_glBindBufferBase = nullptr;
glBufferSubData_t real_glBufferSubData = nullptr;
glBufferData_t real_glBufferData = nullptr;
glBufferStorage_t real_glBufferStorage = nullptr;
glMapBuffer_t real_glMapBuffer = nullptr;
glMapBufferRange_t real_glMapBufferRange = nullptr;
glUnmapBuffer_t real_glUnmapBuffer = nullptr;

WNDPROC real_WndProc = nullptr;
HWND g_gameHWND = NULL;
HWND GetGameWindow();

// Tracking State
GLuint g_currentProgram = 0;

// Thread-local state to track mapped buffers
struct MappedBufferState {
    GLuint bufferID;
    void* ptr;
    GLintptr offset;
    GLsizeiptr length;
    bool readable;
    bool writable;
};
thread_local std::vector<MappedBufferState> g_mappedBuffers;
thread_local std::vector<unsigned char> g_bufferUploadScratch;
thread_local GLuint g_sceneDataBuffer = 0;
thread_local GLintptr g_sceneDataBufferOffset = 0;
thread_local unsigned long long g_programUseSerial = 0;
thread_local unsigned long long g_lastScenePatchProgramUseSerial = 0;

// Shared Memory Structure
using SharedData = hytalevr::UiScaleSharedData;
SharedData* g_sharedData = nullptr;
HANDLE g_hMapFile = NULL;
thread_local bool g_heldItemMatrixReadyForDraw = false;
thread_local int g_currentHeldItemSide = -1;
thread_local bool g_pendingHeldItemMatrixReady = false;
thread_local GLuint g_pendingHeldItemProgram = 0;
thread_local int g_pendingHeldItemSide = -1;

struct HeldItemDrawDescriptor {
    bool indexed = false;
    GLenum mode = 0;
    GLint first = 0;
    GLsizei count = 0;
    GLenum indexType = 0;
    uintptr_t indicesOffset = 0;
};

struct HeldItemUvCacheKey {
    GLuint program = 0;
    GLuint vertexArray = 0;
    GLuint vertexBuffer = 0;
    GLuint elementBuffer = 0;
    uint64_t vertexBufferRevision = 0;
    uint64_t elementBufferRevision = 0;
    GLenum mode = 0;
    GLint first = 0;
    GLsizei count = 0;
    GLenum indexType = 0;
    uintptr_t indicesOffset = 0;
    bool indexed = false;

    bool operator==(const HeldItemUvCacheKey& other) const {
        return program == other.program &&
               vertexArray == other.vertexArray &&
               vertexBuffer == other.vertexBuffer &&
               elementBuffer == other.elementBuffer &&
               vertexBufferRevision == other.vertexBufferRevision &&
               elementBufferRevision == other.elementBufferRevision &&
               mode == other.mode &&
               first == other.first &&
               count == other.count &&
               indexType == other.indexType &&
               indicesOffset == other.indicesOffset &&
               indexed == other.indexed;
    }
};

struct HeldItemUvCacheKeyHash {
    size_t operator()(const HeldItemUvCacheKey& key) const {
        size_t value = static_cast<size_t>(key.program);
        const auto mix = [&](size_t component) {
            value ^= component + static_cast<size_t>(0x9e3779b9u) +
                     (value << 6u) + (value >> 2u);
        };
        mix(static_cast<size_t>(key.vertexArray));
        mix(static_cast<size_t>(key.vertexBuffer));
        mix(static_cast<size_t>(key.elementBuffer));
        mix(static_cast<size_t>(key.vertexBufferRevision));
        mix(static_cast<size_t>(key.vertexBufferRevision >> 32u));
        mix(static_cast<size_t>(key.elementBufferRevision));
        mix(static_cast<size_t>(key.elementBufferRevision >> 32u));
        mix(static_cast<size_t>(key.mode));
        mix(static_cast<size_t>(key.first));
        mix(static_cast<size_t>(key.count));
        mix(static_cast<size_t>(key.indexType));
        mix(static_cast<size_t>(key.indicesOffset));
        mix(static_cast<size_t>(key.indexed));
        return value;
    }
};

thread_local std::unordered_map<HeldItemUvCacheKey,
                                hytalevr::HeldItemUvBounds,
                                HeldItemUvCacheKeyHash>
    g_heldItemUvCache;
thread_local std::unordered_map<GLuint, uint64_t> g_heldItemBufferRevisions;
thread_local uint64_t g_heldItemBufferRevisionSequence = 0;

uint64_t HeldItemBufferRevision(GLuint buffer) {
    if (buffer == 0) return 0;
    const auto found = g_heldItemBufferRevisions.find(buffer);
    return found == g_heldItemBufferRevisions.end() ? 0 : found->second;
}

void NoteHeldItemBufferMutation(GLenum target, GLuint buffer) {
    constexpr GLenum kArrayBuffer = 0x8892;
    constexpr GLenum kElementArrayBuffer = 0x8893;
    if ((target != kArrayBuffer && target != kElementArrayBuffer) || buffer == 0) {
        return;
    }
    if (g_heldItemBufferRevisions.size() >= 4096) {
        g_heldItemBufferRevisions.clear();
        g_heldItemUvCache.clear();
    }
    g_heldItemBufferRevisions[buffer] = ++g_heldItemBufferRevisionSequence;
}

enum class HookInitializationState : int {
    Uninitialized = 0,
    Initializing = 1,
    Initialized = 2,
};
std::atomic<int> g_hookInitializationState{
    static_cast<int>(HookInitializationState::Uninitialized)};
std::atomic<ULONGLONG> g_nextHookInitializationRetryTick{0};
std::mutex g_logMutex;

bool DebugLoggingEnabled() {
    static int enabled = -1;
    if (enabled < 0) {
        char value[8]{};
        enabled = GetEnvironmentVariableA("HYTALEVR_DEBUG_LOGS", value, sizeof(value)) > 0 &&
                  value[0] != '\0' && value[0] != '0';
    }
    return enabled != 0;
}

std::string DebugLogPath() {
    char tempPath[MAX_PATH]{};
    const DWORD len = GetTempPathA(MAX_PATH, tempPath);
    std::string dir = len > 0 ? std::string(tempPath, len) : std::string(".\\");
    if (!dir.empty() && dir.back() != '\\' && dir.back() != '/') dir += "\\";
    dir += "HytaleVR";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir + "\\hytale_scaler_debug.log";
}

void LogMessage(const std::string& message) {
    if (!DebugLoggingEnabled()) return;
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::ofstream logFile(DebugLogPath(), std::ios_base::app);
    if (logFile.is_open()) {
        logFile << message << std::endl;
    }
    OutputDebugStringA(message.c_str());
}

void LogHeldItemRenderStackOnce() {
    static std::atomic<bool> logged{false};
    if (logged.exchange(true, std::memory_order_relaxed)) return;

    void* frames[32]{};
    const USHORT frameCount = CaptureStackBackTrace(1, 32, frames, nullptr);
    std::string message = "HytaleUIScaleHook: held-item render stack";
    for (USHORT index = 0; index < frameCount; ++index) {
        HMODULE module = nullptr;
        char modulePath[MAX_PATH]{};
        if (GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(frames[index]), &module) &&
            GetModuleFileNameA(module, modulePath, MAX_PATH) != 0) {
            const char* name = std::strrchr(modulePath, '\\');
            if (!name) name = std::strrchr(modulePath, '/');
            name = name ? name + 1 : modulePath;
            char line[192]{};
            sprintf_s(line, "\n  #%u %s+0x%llX", static_cast<unsigned>(index),
                      name,
                      static_cast<unsigned long long>(
                          reinterpret_cast<uintptr_t>(frames[index]) -
                          reinterpret_cast<uintptr_t>(module)));
            message += line;
        } else {
            char line[96]{};
            sprintf_s(line, "\n  #%u 0x%llX", static_cast<unsigned>(index),
                      static_cast<unsigned long long>(
                          reinterpret_cast<uintptr_t>(frames[index])));
            message += line;
        }
    }

    // This diagnostic is deliberately written once even when verbose logging is off.
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::ofstream logFile(DebugLogPath(), std::ios_base::app);
    if (logFile.is_open()) logFile << message << std::endl;
    OutputDebugStringA(message.c_str());
}

void LogHeldItemDiagnosticOnce(uint32_t bit, const std::string& message) {
    static std::atomic<uint32_t> loggedBits{0};
    const uint32_t previous = loggedBits.fetch_or(bit, std::memory_order_relaxed);
    if ((previous & bit) != 0) return;
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::ofstream logFile(DebugLogPath(), std::ios_base::app);
    if (logFile.is_open()) {
        logFile << "HytaleUIScaleHook: held-item diagnostic: "
                << message << std::endl;
    }
    OutputDebugStringA(message.c_str());
}

// UI Programs and Buffers Tracking
struct UIProgram {
    GLuint id;
    GLint uMVPMatrixLocation;
    std::string name;
};
std::vector<UIProgram> g_uiPrograms;
std::vector<GLuint> g_batcher2DPrograms;
std::vector<GLuint> g_shadowPrograms;
std::vector<GLuint> g_sunOcclusionPrograms;
std::vector<GLuint> g_ssaoPrograms;
std::vector<GLuint> g_cloudShadowPrograms;
std::vector<GLuint> g_particlePrograms;
std::vector<GLuint> g_postEffectPrograms;
std::vector<GLuint> g_mapChunkAlphaPrograms;
std::vector<GLuint> g_celestialBillboardPrograms;
std::vector<GLuint> g_basicPrograms;
std::vector<GLuint> g_skyPrograms;
std::vector<GLuint> g_hizReprojectPrograms;
std::vector<GLuint> g_possibleFirstPersonPrograms;
struct ProgramMatrixUniform {
    GLuint program = 0;
    GLint location = -1;
};
std::vector<ProgramMatrixUniform> g_ssaoReprojectionUniforms;
std::vector<ProgramMatrixUniform> g_heldItemModelUniforms;
std::vector<GLuint> g_uiBufferIDs;
std::unordered_set<GLuint> g_scannedPrograms;
std::mutex g_programsMutex;
struct CurrentProgramFlags {
    bool ui = false;
    GLint uiMvpLocation = -1;
    bool batcher2D = false;
    bool shadow = false;
    bool sunOcclusion = false;
    bool ssao = false;
    bool cloudShadow = false;
    bool particle = false;
    bool postEffect = false;
    bool mapChunkAlpha = false;
    bool celestialBillboard = false;
    bool basic = false;
    bool sky = false;
    bool hizReproject = false;
    bool possibleFirstPerson = false;
    bool heldItem = false;
    GLint heldItemModelLocation = -1;
};
CurrentProgramFlags g_currentProgramFlags;
GLuint g_neutralDistortionTexture = 0;
GLuint g_blackTexture = 0;
GLuint g_flatNormalTexture = 0;
GLuint g_whiteDepthTexture = 0;
bool g_neutralDistortionLogged = false;
GLuint g_menuCaptureFbo = 0;
GLuint g_menuCaptureTexture = 0;
int g_menuCaptureWidth = 0;
int g_menuCaptureHeight = 0;
DWORD g_menuCaptureUntilTick = 0;
int g_lastMenuCaptureEye = -1;
LONG g_lastMenuCaptureFrameSequence = -1;
bool g_menuCaptureClearedThisEye = false;

constexpr GLenum GL_TEXTURE_2D_VALUE = 0x0DE1;
constexpr GLenum GL_UNIFORM_BUFFER_VALUE = 0x8A11;
constexpr GLenum GL_UNIFORM_BUFFER_BINDING_VALUE = 0x8A28;
constexpr GLenum GL_TEXTURE0_VALUE = 0x84C0;
constexpr GLenum GL_TEXTURE8_VALUE = 0x84C8;
constexpr GLenum GL_ACTIVE_TEXTURE_VALUE = 0x84E0;
constexpr GLenum GL_TEXTURE_BINDING_2D_VALUE = 0x8069;
constexpr GLenum GL_VERTEX_ARRAY_BINDING_VALUE = 0x85B5;
constexpr GLenum GL_TEXTURE_MIN_FILTER_VALUE = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER_VALUE = 0x2800;
constexpr GLenum GL_TEXTURE_WRAP_S_VALUE = 0x2802;
constexpr GLenum GL_TEXTURE_WRAP_T_VALUE = 0x2803;
constexpr GLenum GL_NEAREST_VALUE = 0x2600;
constexpr GLenum GL_LINEAR_VALUE = 0x2601;
constexpr GLenum GL_CLAMP_TO_EDGE_VALUE = 0x812F;
constexpr GLenum GL_RGBA8_VALUE = 0x8058;
constexpr GLenum GL_RGBA_VALUE = 0x1908;
constexpr GLenum GL_UNSIGNED_BYTE_VALUE = 0x1401;
constexpr GLenum GL_RG16F_VALUE = 0x822F;
constexpr GLenum GL_R16F_VALUE = 0x822D;
constexpr GLenum GL_RG_VALUE = 0x8227;
constexpr GLenum GL_RED_VALUE = 0x1903;
constexpr GLenum GL_FLOAT_VALUE = 0x1406;
constexpr GLenum GL_COLOR_CLEAR_VALUE = 0x0C22;
constexpr GLenum GL_TEXTURE_WIDTH_VALUE = 0x1000;
constexpr GLenum GL_TEXTURE_HEIGHT_VALUE = 0x1001;
constexpr GLenum GL_TEXTURE_INTERNAL_FORMAT_VALUE = 0x1003;
constexpr GLenum GL_BUFFER_SIZE_VALUE = 0x8764;
constexpr GLenum GL_READ_ONLY_VALUE = 0x88B8;
constexpr GLenum GL_READ_WRITE_VALUE = 0x88BA;
constexpr GLbitfield GL_MAP_READ_BIT_VALUE = 0x0001;
constexpr GLbitfield GL_MAP_WRITE_BIT_VALUE = 0x0002;
constexpr GLenum GL_TRIANGLES_VALUE = 0x0004;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_VALUE = 0x00004000;
constexpr GLbitfield GL_DEPTH_BUFFER_BIT_VALUE = 0x00000100;
constexpr GLenum GL_FRAMEBUFFER_VALUE = 0x8D40;
constexpr GLenum GL_DEPTH_ATTACHMENT_VALUE = 0x8D00;
constexpr GLenum GL_DEPTH_COMPONENT32F_VALUE = 0x8CAC;
constexpr GLenum GL_DEPTH_COMPONENT_VALUE = 0x1902;
constexpr GLenum GL_TEXTURE_COMPARE_MODE_VALUE = 0x884C;
constexpr GLenum GL_NONE_VALUE = 0;
constexpr GLenum GL_DRAW_BUFFER_VALUE = 0x0C01;
constexpr GLenum GL_READ_BUFFER_VALUE = 0x0C02;
constexpr GLenum GL_COLOR_WRITEMASK_VALUE = 0x0C23;
constexpr GLenum GL_DEPTH_WRITEMASK_VALUE = 0x0B72;
constexpr GLenum GL_DEPTH_CLEAR_VALUE = 0x0B73;
constexpr GLenum GL_DEPTH_FUNC_VALUE = 0x0B74;
constexpr GLenum GL_DEPTH_TEST_VALUE = 0x0B71;
constexpr GLenum GL_LEQUAL_VALUE = 0x0203;
DWORD g_lastSkyProgramUseTick = 0;
GLuint g_cachedSceneDepthTexture = 0;
GLsizei g_cachedSceneDepthWidth = 0;
GLsizei g_cachedSceneDepthHeight = 0;
int g_cachedSceneDepthScore = 0;
GLuint g_heldItemDepthFbo = 0;
GLuint g_heldItemColorTexture = 0;
GLuint g_heldItemDepthTexture = 0;
int g_heldItemDepthWidth = 0;
int g_heldItemDepthHeight = 0;
LONG g_heldItemDepthClearedFrame = -1;
LONG g_heldItemLayerPositionFrame = -1;
LONG g_heldItemAtlasPublishFrame = -1;

std::vector<GLuint> g_loggedBufferIDs;
std::mutex g_loggedBuffersMutex;

bool ProgramListContains(const std::vector<GLuint>& programs, GLuint program) {
    return std::find(programs.begin(), programs.end(), program) != programs.end();
}

void RefreshCurrentProgramFlags(GLuint program) {
    CurrentProgramFlags flags{};
    if (program != 0) {
        std::lock_guard<std::mutex> lock(g_programsMutex);
        for (const auto& candidate : g_uiPrograms) {
            if (candidate.id == program) {
                flags.ui = true;
                flags.uiMvpLocation = candidate.uMVPMatrixLocation;
                break;
            }
        }
        flags.batcher2D = ProgramListContains(g_batcher2DPrograms, program);
        flags.shadow = ProgramListContains(g_shadowPrograms, program);
        flags.sunOcclusion = ProgramListContains(g_sunOcclusionPrograms, program);
        flags.ssao = ProgramListContains(g_ssaoPrograms, program);
        flags.cloudShadow = ProgramListContains(g_cloudShadowPrograms, program);
        flags.particle = ProgramListContains(g_particlePrograms, program);
        flags.postEffect = ProgramListContains(g_postEffectPrograms, program);
        flags.mapChunkAlpha = ProgramListContains(g_mapChunkAlphaPrograms, program);
        flags.celestialBillboard = ProgramListContains(g_celestialBillboardPrograms, program);
        flags.basic = ProgramListContains(g_basicPrograms, program);
        flags.sky = ProgramListContains(g_skyPrograms, program);
        flags.hizReproject = ProgramListContains(g_hizReprojectPrograms, program);
        flags.possibleFirstPerson = ProgramListContains(g_possibleFirstPersonPrograms, program);
        for (const ProgramMatrixUniform& uniform : g_heldItemModelUniforms) {
            if (uniform.program == program) {
                flags.heldItem = true;
                flags.heldItemModelLocation = uniform.location;
                break;
            }
        }
    }
    g_currentProgramFlags = flags;
}

bool IsUIProgram(GLuint program, GLint location, GLint& outLoc) {
    if (program == g_currentProgram) {
        outLoc = g_currentProgramFlags.uiMvpLocation;
        return g_currentProgramFlags.ui &&
               (location == -1 || location == g_currentProgramFlags.uiMvpLocation);
    }
    std::lock_guard<std::mutex> lock(g_programsMutex);
    for (const auto& p : g_uiPrograms) {
        if (p.id == program) {
            if (location == -1 || location == p.uMVPMatrixLocation) {
                outLoc = p.uMVPMatrixLocation;
                return true;
            }
        }
    }
    return false;
}

bool IsBatcher2DProgram(GLuint program) {
    if (program == g_currentProgram) return g_currentProgramFlags.batcher2D;
    std::lock_guard<std::mutex> lock(g_programsMutex);
    return std::find(g_batcher2DPrograms.begin(), g_batcher2DPrograms.end(), program) !=
           g_batcher2DPrograms.end();
}

bool Batcher2DMenuCaptureActive() {
    return g_sharedData != nullptr &&
           g_sharedData->suppressMenuInGame != 0 &&
           GetTickCount() <= g_menuCaptureUntilTick;
}

GLsizei MenuIgnoreDrawThreshold() {
    if (!g_sharedData) return 1;
    const LONG raw = InterlockedCompareExchange(
        &g_sharedData->menuIgnoreDrawThreshold, 0, 0);
    if (raw < 0) return 0;
    if (raw > 20000) return 20000;
    return static_cast<GLsizei>(raw);
}

void NoteBatcher2DMenuDraw(GLsizei count) {
    if (!g_sharedData || g_currentProgram == 0 || count < 500) {
        return;
    }
    const GLsizei ignoreThreshold = MenuIgnoreDrawThreshold();
    // Counts below the dashboard-controlled threshold cover the normal in-game HUD/hotbar, weapon
    // reticles and small gauge widgets, so they cannot be used as a "menu is
    // open" signal. Real inventory/menu captures observed so far are larger
    // batches, typically around 3132+.
    if (count <= ignoreThreshold) {
        return;
    }
    if (!IsBatcher2DProgram(g_currentProgram)) {
        return;
    }

    // Counts above the normal HUD/hotbar batch are the least noisy signal we
    // currently have for a real screen-space menu. Both counters are kept for
    // compatibility and calibration in logs.
    InterlockedIncrement(&g_sharedData->menuVisibleCounter);
    InterlockedIncrement(&g_sharedData->menuLargeDrawCounter);
    g_menuCaptureUntilTick = GetTickCount() + 350;

    static DWORD lastLogTick = 0;
    const DWORD now = GetTickCount();
    if (now - lastLogTick >= 1000) {
        LogMessage("HytaleUIScaleHook: large Batcher2D draw observed. Count=" +
                   std::to_string(count) + ", Counter=" +
                   std::to_string(g_sharedData->menuLargeDrawCounter) +
                   ", IgnoreDraw<=" + std::to_string(ignoreThreshold));
        lastLogTick = now;
    }
}

bool EnsureMenuCaptureTarget(int width, int height) {
    if (!real_glGetIntegerv || !real_glViewport ||
        !real_glGenTextures || !real_glBindTexture || !real_glTexImage2D ||
        !real_glTexParameteri || !real_glGenFramebuffers ||
        !real_glBindFramebuffer || !real_glFramebufferTexture2D ||
        !real_glCheckFramebufferStatus) {
        if (g_sharedData) g_sharedData->menuCaptureError = 1;
        return false;
    }

    width = (std::max)(320, (std::min)(width, 4096));
    height = (std::max)(240, (std::min)(height, 4096));
    if (g_menuCaptureTexture != 0 && g_menuCaptureFbo != 0 &&
        g_menuCaptureWidth == width && g_menuCaptureHeight == height) {
        return true;
    }

    if (g_menuCaptureTexture == 0) {
        real_glGenTextures(1, &g_menuCaptureTexture);
    }
    if (g_menuCaptureFbo == 0) {
        real_glGenFramebuffers(1, &g_menuCaptureFbo);
    }
    if (g_menuCaptureTexture == 0 || g_menuCaptureFbo == 0) {
        if (g_sharedData) g_sharedData->menuCaptureError = 2;
        return false;
    }

    GLint previousTexture = 0;
    real_glGetIntegerv(GL_TEXTURE_BINDING_2D_VALUE, &previousTexture);
    real_glBindTexture(GL_TEXTURE_2D_VALUE, g_menuCaptureTexture);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_MIN_FILTER_VALUE, GL_NEAREST_VALUE);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_MAG_FILTER_VALUE, GL_NEAREST_VALUE);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_WRAP_S_VALUE, GL_CLAMP_TO_EDGE_VALUE);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_WRAP_T_VALUE, GL_CLAMP_TO_EDGE_VALUE);
    real_glTexImage2D(GL_TEXTURE_2D_VALUE, 0, GL_RGBA8_VALUE, width, height, 0,
                      GL_RGBA_VALUE, GL_UNSIGNED_BYTE_VALUE, nullptr);
    real_glBindTexture(GL_TEXTURE_2D_VALUE, static_cast<GLuint>(previousTexture));

    GLint previousDraw = 0;
    real_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
    real_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_menuCaptureFbo);
    real_glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D_VALUE, g_menuCaptureTexture, 0);
    const GLenum status = real_glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    real_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDraw));
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        if (g_sharedData) g_sharedData->menuCaptureError = static_cast<LONG>(status);
        return false;
    }

    g_menuCaptureWidth = width;
    g_menuCaptureHeight = height;
    g_menuCaptureClearedThisEye = false;
    if (g_sharedData) {
        g_sharedData->menuTextureId = static_cast<LONG>(g_menuCaptureTexture);
        g_sharedData->menuTextureWidth = width;
        g_sharedData->menuTextureHeight = height;
        g_sharedData->menuCaptureError = 0;
    }
    LogMessage("HytaleUIScaleHook: menu capture target ready texture=" +
               std::to_string(g_menuCaptureTexture) + " size=" +
               std::to_string(width) + "x" + std::to_string(height));
    return true;
}

bool CaptureOrSuppressBatcher2DMenuDraw(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    if (!g_sharedData || g_currentProgram == 0 || !IsBatcher2DProgram(g_currentProgram) ||
        !Batcher2DMenuCaptureActive()) {
        return false;
    }
    const GLsizei ignoreThreshold = MenuIgnoreDrawThreshold();
    // Keep HUD-only batches visible in the normal stereo pass. Capturing or
    // suppressing them makes the hand pointer enter a stale "menu" rectangle
    // around health/hotbar widgets in survival. The 1050/1164/1344/1404
    // batches observed with weapons and small HUD changes are also ignored.
    if (count <= ignoreThreshold) {
        return false;
    }

    const LONG currentEye = InterlockedCompareExchange(&g_sharedData->currentEye, 0, 0);
    const LONG frameSequence = InterlockedCompareExchange(
        &g_sharedData->renderFrameSequence, 0, 0);
    // AFW uses a fixed source eye, so eye changes cannot identify a new frame.
    // Use the swap sequence instead; timing gaps between UI batches are not
    // frame boundaries and used to clear partially rendered menus.
    if (frameSequence != g_lastMenuCaptureFrameSequence) {
        g_lastMenuCaptureFrameSequence = frameSequence;
        g_menuCaptureClearedThisEye = false;
    }
    if (currentEye != g_lastMenuCaptureEye) {
        g_lastMenuCaptureEye = static_cast<int>(currentEye);
        g_menuCaptureClearedThisEye = false;
    }

    if (currentEye != 0) {
        return g_sharedData->menuTextureId != 0;
    }

    GLint viewport[4] = {0, 0, 0, 0};
    real_glGetIntegerv(GL_VIEWPORT, viewport);
    const int width = viewport[2] > 0 ? viewport[2] : 1280;
    const int height = viewport[3] > 0 ? viewport[3] : 720;
    if (!EnsureMenuCaptureTarget(width, height)) {
        return false;
    }

    GLint previousDraw = 0;
    GLint previousRead = 0;
    GLfloat previousClear[4] = {};
    real_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
    real_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
    if (real_glGetFloatv) {
        real_glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClear);
    }

    real_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_menuCaptureFbo);
    real_glViewport(0, 0, g_menuCaptureWidth, g_menuCaptureHeight);
    if (!g_menuCaptureClearedThisEye && real_glClearColor && real_glClear) {
        real_glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        real_glClear(GL_COLOR_BUFFER_BIT_VALUE);
        g_menuCaptureClearedThisEye = true;
    }
    real_glDrawElements(mode, count, type, indices);

    real_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDraw));
    real_glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousRead));
    real_glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    if (real_glClearColor) {
        real_glClearColor(previousClear[0], previousClear[1], previousClear[2], previousClear[3]);
    }

    if (g_sharedData) {
        g_sharedData->menuTextureId = static_cast<LONG>(g_menuCaptureTexture);
        g_sharedData->menuTextureWidth = g_menuCaptureWidth;
        g_sharedData->menuTextureHeight = g_menuCaptureHeight;
        InterlockedIncrement(&g_sharedData->menuTextureFrame);
        g_sharedData->menuCaptureError = 0;
    }

    static DWORD lastLogTick = 0;
    const DWORD now = GetTickCount();
    if (now - lastLogTick >= 1000) {
        LogMessage("HytaleUIScaleHook: captured Batcher2D menu draw count=" +
                   std::to_string(count) + " texture=" +
                   std::to_string(g_menuCaptureTexture));
        lastLogTick = now;
    }
    return true;
}

bool IsShadowProgram(GLuint program) {
    if (program == g_currentProgram) return g_currentProgramFlags.shadow;
    std::lock_guard<std::mutex> lock(g_programsMutex);
    return std::find(g_shadowPrograms.begin(), g_shadowPrograms.end(), program) != g_shadowPrograms.end();
}

bool IsSunOcclusionProgram(GLuint program) {
    if (program == g_currentProgram) return g_currentProgramFlags.sunOcclusion;
    std::lock_guard<std::mutex> lock(g_programsMutex);
    return std::find(g_sunOcclusionPrograms.begin(), g_sunOcclusionPrograms.end(), program) !=
           g_sunOcclusionPrograms.end();
}

bool IsSsaoProgram(GLuint program) {
    if (program == g_currentProgram) return g_currentProgramFlags.ssao;
    std::lock_guard<std::mutex> lock(g_programsMutex);
    return std::find(g_ssaoPrograms.begin(), g_ssaoPrograms.end(), program) !=
           g_ssaoPrograms.end();
}

bool IsSsaoReprojectionUniform(GLuint program, GLint location) {
    if (program == 0 || location < 0) return false;
    std::lock_guard<std::mutex> lock(g_programsMutex);
    for (const auto& uniform : g_ssaoReprojectionUniforms) {
        if (uniform.program == program && uniform.location == location) return true;
    }
    return false;
}

bool IsCloudShadowProgram(GLuint program) {
    if (program == g_currentProgram) return g_currentProgramFlags.cloudShadow;
    std::lock_guard<std::mutex> lock(g_programsMutex);
    return std::find(g_cloudShadowPrograms.begin(), g_cloudShadowPrograms.end(), program) !=
           g_cloudShadowPrograms.end();
}

bool IsParticleProgram(GLuint program) {
    if (program == g_currentProgram) return g_currentProgramFlags.particle;
    std::lock_guard<std::mutex> lock(g_programsMutex);
    return std::find(g_particlePrograms.begin(), g_particlePrograms.end(), program) != g_particlePrograms.end();
}

bool IsPostEffectProgram(GLuint program) {
    if (program == g_currentProgram) return g_currentProgramFlags.postEffect;
    std::lock_guard<std::mutex> lock(g_programsMutex);
    return std::find(g_postEffectPrograms.begin(), g_postEffectPrograms.end(), program) != g_postEffectPrograms.end();
}

bool IsMapChunkAlphaProgram(GLuint program) {
    if (program == g_currentProgram) return g_currentProgramFlags.mapChunkAlpha;
    std::lock_guard<std::mutex> lock(g_programsMutex);
    return std::find(g_mapChunkAlphaPrograms.begin(), g_mapChunkAlphaPrograms.end(), program) !=
           g_mapChunkAlphaPrograms.end();
}

GLint HeldItemModelMatrixLocation(GLuint program) {
    if (program == g_currentProgram) {
        return g_currentProgramFlags.heldItem
            ? g_currentProgramFlags.heldItemModelLocation
            : -1;
    }
    std::lock_guard<std::mutex> lock(g_programsMutex);
    for (const ProgramMatrixUniform& uniform : g_heldItemModelUniforms) {
        if (uniform.program == program) return uniform.location;
    }
    return -1;
}

bool IsCelestialBillboardProgram(GLuint program) {
    if (program == g_currentProgram) return g_currentProgramFlags.celestialBillboard;
    std::lock_guard<std::mutex> lock(g_programsMutex);
    return std::find(g_celestialBillboardPrograms.begin(),
                     g_celestialBillboardPrograms.end(), program) !=
           g_celestialBillboardPrograms.end();
}

bool IsBasicProgram(GLuint program) {
    if (program == g_currentProgram) return g_currentProgramFlags.basic;
    std::lock_guard<std::mutex> lock(g_programsMutex);
    return std::find(g_basicPrograms.begin(), g_basicPrograms.end(), program) !=
           g_basicPrograms.end();
}

bool IsSkyProgram(GLuint program) {
    if (program == g_currentProgram) return g_currentProgramFlags.sky;
    std::lock_guard<std::mutex> lock(g_programsMutex);
    return std::find(g_skyPrograms.begin(), g_skyPrograms.end(), program) !=
           g_skyPrograms.end();
}

bool IsHiZReprojectProgram(GLuint program) {
    if (program == g_currentProgram) return g_currentProgramFlags.hizReproject;
    std::lock_guard<std::mutex> lock(g_programsMutex);
    return std::find(g_hizReprojectPrograms.begin(), g_hizReprojectPrograms.end(), program) !=
           g_hizReprojectPrograms.end();
}

bool ContainsAny(const std::string& text, std::initializer_list<const char*> needles) {
    for (const char* needle : needles) {
        if (text.find(needle) != std::string::npos) return true;
    }
    return false;
}

void RegisterProgramOnce(std::vector<GLuint>& programs,
                         GLuint program,
                         const std::string& label,
                         const char* kind) {
    if (std::find(programs.begin(), programs.end(), program) != programs.end()) return;
    programs.push_back(program);
    LogMessage(std::string("HytaleUIScaleHook: Registered ") + kind +
               " program! ID: " + std::to_string(program) + ", Label: " + label);
}

bool GetClientSize(int& width, int& height) {
    width = 0;
    height = 0;
    if (!real_GetClientRect) return false;
    HWND hwnd = GetGameWindow();
    if (!hwnd) return false;

    RECT rect{};
    if (!real_GetClientRect(hwnd, &rect)) return false;
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
    return width > 0 && height > 0;
}

bool IsLikelyLinearSceneDepthTexture(GLsizei width, GLsizei height) {
    int clientWidth = 0;
    int clientHeight = 0;
    GetClientSize(clientWidth, clientHeight);
    return hytalevr::is_likely_linear_scene_depth_texture(
        width, height, clientWidth, clientHeight);
}

int SceneDepthCandidateScore(GLsizei width, GLsizei height) {
    int clientWidth = 0;
    int clientHeight = 0;
    GetClientSize(clientWidth, clientHeight);
    return hytalevr::scene_depth_candidate_score(
        width, height, clientWidth, clientHeight);
}

void PublishSceneDepthTexture(GLuint texture, GLsizei width, GLsizei height, const char* source) {
    const int candidateScore = SceneDepthCandidateScore(width, height);
    if (!g_sharedData || texture == 0 || candidateScore == 0 ||
        candidateScore < g_cachedSceneDepthScore) {
        return;
    }

    const LONG previousTexture = InterlockedCompareExchange(
        &g_sharedData->sceneDepthTextureId, 0, 0);
    const LONG previousWidth = InterlockedCompareExchange(
        &g_sharedData->sceneDepthTextureWidth, 0, 0);
    const LONG previousHeight = InterlockedCompareExchange(
        &g_sharedData->sceneDepthTextureHeight, 0, 0);

    InterlockedExchange(&g_sharedData->sceneDepthTextureId, static_cast<LONG>(texture));
    InterlockedExchange(&g_sharedData->sceneDepthTextureWidth, static_cast<LONG>(width));
    InterlockedExchange(&g_sharedData->sceneDepthTextureHeight, static_cast<LONG>(height));
    g_sharedData->sceneDepthFarClip = 1024.0f;
    InterlockedIncrement(&g_sharedData->sceneDepthTextureFrame);
    g_cachedSceneDepthTexture = texture;
    g_cachedSceneDepthWidth = width;
    g_cachedSceneDepthHeight = height;
    g_cachedSceneDepthScore = candidateScore;

    if (previousTexture != static_cast<LONG>(texture) ||
        previousWidth != static_cast<LONG>(width) ||
        previousHeight != static_cast<LONG>(height)) {
        LogMessage(std::string("HytaleUIScaleHook: scene depth texture published from ") +
                   source + " texture=" + std::to_string(texture) +
                   " size=" + std::to_string(width) + "x" + std::to_string(height) +
                   " score=" + std::to_string(candidateScore));
    }
}

void RefreshCachedSceneDepthTexture() {
    if (!g_sharedData || g_cachedSceneDepthTexture == 0) return;
    InterlockedExchange(&g_sharedData->sceneDepthTextureId,
                        static_cast<LONG>(g_cachedSceneDepthTexture));
    InterlockedExchange(&g_sharedData->sceneDepthTextureWidth,
                        static_cast<LONG>(g_cachedSceneDepthWidth));
    InterlockedExchange(&g_sharedData->sceneDepthTextureHeight,
                        static_cast<LONG>(g_cachedSceneDepthHeight));
    InterlockedIncrement(&g_sharedData->sceneDepthTextureFrame);
}

void TryPublishBoundSceneDepthTexture(const char* source) {
    if (!real_glGetIntegerv || !real_glGetTexLevelParameteriv) return;

    GLint boundTexture = 0;
    GLint width = 0;
    GLint height = 0;
    GLint internalFormat = 0;
    real_glGetIntegerv(GL_TEXTURE_BINDING_2D_VALUE, &boundTexture);
    if (boundTexture <= 0) return;

    real_glGetTexLevelParameteriv(GL_TEXTURE_2D_VALUE, 0,
                                  GL_TEXTURE_INTERNAL_FORMAT_VALUE, &internalFormat);
    if (internalFormat != static_cast<GLint>(GL_R16F_VALUE)) return;

    real_glGetTexLevelParameteriv(GL_TEXTURE_2D_VALUE, 0, GL_TEXTURE_WIDTH_VALUE, &width);
    real_glGetTexLevelParameteriv(GL_TEXTURE_2D_VALUE, 0, GL_TEXTURE_HEIGHT_VALUE, &height);
    PublishSceneDepthTexture(static_cast<GLuint>(boundTexture), width, height, source);
}

void TryPublishTexture0SceneDepth(const char* source) {
    if (!real_glActiveTexture || !real_glGetIntegerv) return;

    GLint previousActiveTexture = GL_TEXTURE0_VALUE;
    real_glGetIntegerv(GL_ACTIVE_TEXTURE_VALUE, &previousActiveTexture);
    real_glActiveTexture(GL_TEXTURE0_VALUE);
    TryPublishBoundSceneDepthTexture(source);
    real_glActiveTexture(static_cast<GLenum>(previousActiveTexture));
}

void NotePossibleFirstPersonProgram(GLuint program, const std::string& label) {
    const bool looks_first_person =
        label.find("FirstPerson") != std::string::npos ||
        label.find("ViewModel") != std::string::npos ||
        label.find("Held") != std::string::npos ||
        label.find("Hand") != std::string::npos ||
        label.find("Arm") != std::string::npos;
    if (!looks_first_person) return;

    // Clipping/Distortion variants only build masks. Reanchoring those passes
    // creates an invisible silhouette around the visible held item.
    const bool looks_held_item =
        hytalevr::is_held_item_visual_program_label(label);
    const GLint model_matrix_location = looks_held_item && real_glGetUniformLocation
        ? real_glGetUniformLocation(program, "uModelMatrix")
        : -1;

    std::lock_guard<std::mutex> lock(g_programsMutex);
    const bool already_first_person =
        std::find(g_possibleFirstPersonPrograms.begin(),
                  g_possibleFirstPersonPrograms.end(), program) !=
        g_possibleFirstPersonPrograms.end();
    if (!already_first_person) {
        g_possibleFirstPersonPrograms.push_back(program);
        LogMessage("HytaleUIScaleHook: possible first-person/viewmodel program ID: " +
                   std::to_string(program) + ", Label: " + label);
    }
    if (model_matrix_location >= 0) {
        const bool already_held_item = std::any_of(
            g_heldItemModelUniforms.begin(), g_heldItemModelUniforms.end(),
            [&](const ProgramMatrixUniform& uniform) {
                return uniform.program == program;
            });
        if (!already_held_item) {
            g_heldItemModelUniforms.push_back({program, model_matrix_location});
            LogMessage("HytaleUIScaleHook: held-item program ID: " +
                       std::to_string(program) + ", uModelMatrix=" +
                       std::to_string(model_matrix_location) + ", Label: " + label);
        }
    }
}

bool ShouldSkipFirstPersonDraw() {
    if (!g_sharedData || g_sharedData->hideFirstPerson == 0 ||
        g_currentProgram == 0 || !g_currentProgramFlags.possibleFirstPerson) {
        return false;
    }
    if (g_currentProgramFlags.heldItem) {
        return !g_heldItemMatrixReadyForDraw;
    }
    return true;
}

struct HeldItemPoseSnapshot {
    LONG validMask = 0;
    float worldScale = 1.0f;
    float localOffset[3]{0.0f, 0.0f, -0.10f};
    float visualScale = 0.30f;
    float controllerPoses[2][12]{};
};

bool ReadHeldItemPoseSnapshot(HeldItemPoseSnapshot& snapshot) {
    if (!g_sharedData || g_sharedData->heldItemReanchorEnabled == 0) return false;
    for (int attempt = 0; attempt < 4; ++attempt) {
        const LONG begin = InterlockedCompareExchange(
            &g_sharedData->heldItemPoseSequence, 0, 0);
        if ((begin & 1) != 0) continue;
        snapshot.validMask = InterlockedCompareExchange(
            &g_sharedData->heldItemPoseValidMask, 0, 0);
        snapshot.worldScale = g_sharedData->heldItemWorldScale;
        std::memcpy(snapshot.localOffset, g_sharedData->heldItemLocalOffset,
                    sizeof(snapshot.localOffset));
        snapshot.visualScale = g_sharedData->heldItemVisualScale;
        std::memcpy(snapshot.controllerPoses,
                    g_sharedData->heldItemControllerPoses,
                    sizeof(snapshot.controllerPoses));
        MemoryBarrier();
        const LONG end = InterlockedCompareExchange(
            &g_sharedData->heldItemPoseSequence, 0, 0);
        if (begin == end && (end & 1) == 0) {
            return snapshot.validMask != 0 &&
                   std::isfinite(snapshot.worldScale) &&
                   hytalevr::finite_values(snapshot.localOffset, 3) &&
                   std::isfinite(snapshot.visualScale);
        }
    }
    return false;
}

bool TryPatchHeldItemModelMatrix(GLuint program,
                                 GLint location,
                                 GLsizei count,
                                 GLboolean transpose,
                                 const GLfloat* value,
                                 GLfloat (&modified)[16]) {
    const GLint model_location = HeldItemModelMatrixLocation(program);
    if (model_location < 0 || location != model_location) return false;
    if (program == g_currentProgram) {
        g_heldItemMatrixReadyForDraw = false;
        g_currentHeldItemSide = -1;
    } else if (program == g_pendingHeldItemProgram) {
        g_pendingHeldItemMatrixReady = false;
        g_pendingHeldItemSide = -1;
    }
    if (count != 1 || transpose != 0 || value == nullptr) {
        return false;
    }
    if (!hytalevr::held_item_model_matrix_has_anchor(value)) return false;

    HeldItemPoseSnapshot snapshot{};
    if (!ReadHeldItemPoseSnapshot(snapshot)) return false;
    const int side = hytalevr::held_item_side_from_model_matrix(value);
    if ((snapshot.validMask & (1L << side)) == 0) return false;
    if (!hytalevr::compose_held_item_model_matrix(
            value, snapshot.controllerPoses[side], snapshot.worldScale,
            snapshot.localOffset, snapshot.visualScale, modified)) {
        return false;
    }

    const LONG frame = InterlockedCompareExchange(
        &g_sharedData->renderFrameSequence, 0, 0);
    if (frame != g_heldItemLayerPositionFrame) {
        g_heldItemLayerPositionFrame = frame;
        InterlockedExchange(&g_sharedData->heldItemLayerVisibleMask, 0);
    }
    g_sharedData->heldItemLayerViewPositions[side][0] = modified[12];
    g_sharedData->heldItemLayerViewPositions[side][1] = modified[13];
    g_sharedData->heldItemLayerViewPositions[side][2] = modified[14];
    MemoryBarrier();
    InterlockedOr(&g_sharedData->heldItemLayerVisibleMask, 1L << side);
    InterlockedExchange(&g_sharedData->heldItemMatrixFrame, frame);

    if (program == g_currentProgram) {
        g_heldItemMatrixReadyForDraw = true;
        g_currentHeldItemSide = side;
    } else {
        g_pendingHeldItemMatrixReady = true;
        g_pendingHeldItemProgram = program;
        g_pendingHeldItemSide = side;
    }
    InterlockedIncrement(&g_sharedData->firstPersonMatrixPatches);
    InterlockedIncrement(&g_sharedData->heldItemMatrixPatches);

    static DWORD lastLogTick = 0;
    const DWORD now = DebugLoggingEnabled() ? GetTickCount() : 0;
    if (now != 0 && now - lastLogTick > 1000) {
        LogMessage("HytaleUIScaleHook: held item reanchored program=" +
                   std::to_string(program) +
                   " side=" + (side == hytalevr::kHeldItemLeft ? "left" : "right") +
                   " source=(" + std::to_string(value[12]) + "," +
                   std::to_string(value[13]) + "," + std::to_string(value[14]) +
                   ") tracked=(" + std::to_string(modified[12]) + "," +
                   std::to_string(modified[13]) + "," +
                   std::to_string(modified[14]) + ")");
        lastLogTick = now;
    }
    return true;
}

bool ReadNamedBufferRange(GLuint buffer,
                          size_t offset,
                          size_t size,
                          std::vector<uint8_t>& bytes) {
    if (buffer == 0 || size == 0 || !real_glGetNamedBufferSubData ||
        !real_glGetNamedBufferParameteriv ||
        offset > static_cast<size_t>((std::numeric_limits<GLint>::max)()) ||
        size > static_cast<size_t>((std::numeric_limits<GLint>::max)())) {
        return false;
    }
    GLint buffer_size = 0;
    real_glGetNamedBufferParameteriv(buffer, 0x8764, &buffer_size); // GL_BUFFER_SIZE
    if (buffer_size <= 0 || offset > static_cast<size_t>(buffer_size) ||
        size > static_cast<size_t>(buffer_size) - offset) {
        return false;
    }
    bytes.resize(size);
    real_glGetNamedBufferSubData(
        buffer, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size),
        bytes.data());
    return true;
}

bool DecodeHeldItemIndices(const HeldItemDrawDescriptor& draw,
                           GLuint element_buffer,
                           std::vector<uint32_t>& indices,
                           uint32_t& min_index,
                           uint32_t& max_index) {
    if (draw.count <= 0) return false;
    indices.clear();
    indices.reserve(static_cast<size_t>(draw.count));
    min_index = (std::numeric_limits<uint32_t>::max)();
    max_index = 0;
    if (!draw.indexed) {
        if (draw.first < 0) return false;
        const uint64_t last = static_cast<uint64_t>(draw.first) +
                              static_cast<uint64_t>(draw.count) - 1u;
        if (last > (std::numeric_limits<uint32_t>::max)()) return false;
        min_index = static_cast<uint32_t>(draw.first);
        max_index = static_cast<uint32_t>(last);
        return true;
    }

    size_t index_size = 0;
    if (draw.indexType == 0x1401) index_size = 1;      // GL_UNSIGNED_BYTE
    else if (draw.indexType == 0x1403) index_size = 2; // GL_UNSIGNED_SHORT
    else if (draw.indexType == 0x1405) index_size = 4; // GL_UNSIGNED_INT
    else return false;

    const size_t count = static_cast<size_t>(draw.count);
    if (count > (std::numeric_limits<size_t>::max)() / index_size) return false;
    std::vector<uint8_t> raw;
    if (!ReadNamedBufferRange(
            element_buffer, draw.indicesOffset, count * index_size, raw)) {
        return false;
    }
    for (size_t index = 0; index < count; ++index) {
        uint32_t value = 0;
        if (index_size == 1) {
            value = raw[index];
        } else if (index_size == 2) {
            uint16_t packed = 0;
            std::memcpy(&packed, raw.data() + index * index_size, sizeof(packed));
            value = packed;
        } else {
            std::memcpy(&value, raw.data() + index * index_size, sizeof(value));
        }
        indices.push_back(value);
        min_index = (std::min)(min_index, value);
        max_index = (std::max)(max_index, value);
    }
    return !indices.empty();
}

bool ResolveHeldItemUvBounds(const HeldItemDrawDescriptor& draw,
                             GLuint vertex_array,
                             hytalevr::HeldItemUvBounds& bounds) {
    if (g_currentProgram == 0 || vertex_array == 0 || draw.count <= 0 ||
        !real_glGetAttribLocation || !real_glGetVertexAttribiv ||
        !real_glGetVertexAttribPointerv || !real_glGetIntegerv) {
        return false;
    }
    const GLint location =
        real_glGetAttribLocation(g_currentProgram, "vertTexCoords");
    if (location < 0) return false;
    GLint enabled = 0;
    GLint size = 0;
    GLint type = 0;
    GLint stride = 0;
    GLint integer_attribute = 0;
    GLint vertex_buffer = 0;
    real_glGetVertexAttribiv(
        static_cast<GLuint>(location), 0x8622, &enabled);
    real_glGetVertexAttribiv(
        static_cast<GLuint>(location), 0x8623, &size);
    real_glGetVertexAttribiv(
        static_cast<GLuint>(location), 0x8625, &type);
    real_glGetVertexAttribiv(
        static_cast<GLuint>(location), 0x8624, &stride);
    real_glGetVertexAttribiv(
        static_cast<GLuint>(location), 0x88FD, &integer_attribute);
    real_glGetVertexAttribiv(
        static_cast<GLuint>(location), 0x889F, &vertex_buffer);
    void* pointer = nullptr;
    real_glGetVertexAttribPointerv(
        static_cast<GLuint>(location), 0x8645, &pointer);
    if (enabled == 0 || size != 1 || type != 0x1405 ||
        integer_attribute == 0 || stride <= 0 || vertex_buffer <= 0) {
        return false;
    }

    GLint element_buffer = 0;
    if (draw.indexed) {
        real_glGetIntegerv(0x8895, &element_buffer);
        if (element_buffer <= 0) return false;
    }
    const HeldItemUvCacheKey cache_key{
        g_currentProgram,
        vertex_array,
        static_cast<GLuint>(vertex_buffer),
        static_cast<GLuint>((std::max)(0, element_buffer)),
        HeldItemBufferRevision(static_cast<GLuint>(vertex_buffer)),
        HeldItemBufferRevision(static_cast<GLuint>((std::max)(0, element_buffer))),
        draw.mode,
        draw.first,
        draw.count,
        draw.indexType,
        draw.indicesOffset,
        draw.indexed};
    const auto cached = g_heldItemUvCache.find(cache_key);
    if (cached != g_heldItemUvCache.end()) {
        bounds = cached->second;
        return bounds.valid;
    }

    std::vector<uint32_t> indices;
    uint32_t min_index = 0;
    uint32_t max_index = 0;
    if (!DecodeHeldItemIndices(
            draw, static_cast<GLuint>(element_buffer), indices,
            min_index, max_index)) {
        return false;
    }

    const size_t attribute_offset = reinterpret_cast<uintptr_t>(pointer);
    const size_t vertex_stride = static_cast<size_t>(stride);
    if (max_index > ((std::numeric_limits<size_t>::max)() - attribute_offset) /
                        vertex_stride) {
        return false;
    }
    const size_t first_offset =
        attribute_offset + static_cast<size_t>(min_index) * vertex_stride;
    const size_t span =
        static_cast<size_t>(max_index - min_index) * vertex_stride +
        sizeof(uint32_t);
    std::vector<uint8_t> vertex_data;
    if (!ReadNamedBufferRange(
            static_cast<GLuint>(vertex_buffer), first_offset, span,
            vertex_data)) {
        return false;
    }

    bounds = {};
    const auto include_index = [&](uint32_t vertex_index) {
        const size_t local =
            static_cast<size_t>(vertex_index - min_index) * vertex_stride;
        if (local + sizeof(uint32_t) > vertex_data.size()) return false;
        uint32_t packed = 0;
        std::memcpy(&packed, vertex_data.data() + local, sizeof(packed));
        hytalevr::include_held_item_packed_uv(packed, bounds);
        return true;
    };
    if (draw.indexed) {
        for (uint32_t vertex_index : indices) {
            if (!include_index(vertex_index)) return false;
        }
    } else {
        for (uint32_t vertex_index = min_index; vertex_index <= max_index;
             ++vertex_index) {
            if (!include_index(vertex_index)) return false;
            if (vertex_index == (std::numeric_limits<uint32_t>::max)()) break;
        }
    }
    if (!bounds.valid) return false;
    if (g_heldItemUvCache.size() >= 2048) g_heldItemUvCache.clear();
    g_heldItemUvCache.emplace(cache_key, bounds);
    return true;
}

void PublishHeldItemAtlasState(const HeldItemDrawDescriptor& draw) {
    if (!g_sharedData || g_currentProgram == 0 ||
        g_currentHeldItemSide < hytalevr::kHeldItemLeft ||
        g_currentHeldItemSide > hytalevr::kHeldItemRight ||
        !real_glGetUniformLocation || !real_glGetUniformiv ||
        !real_glGetUniformfv || !real_glActiveTexture ||
        !real_glGetIntegerv || !real_glGetTexLevelParameteriv) {
        LogHeldItemDiagnosticOnce(
            1u, "atlas publish prerequisites are unavailable");
        return;
    }

    const GLint offset_location =
        real_glGetUniformLocation(g_currentProgram, "uGlobalOffset");
    const GLint inverse_size_location =
        real_glGetUniformLocation(g_currentProgram, "uAtlasSizeInv");
    const GLint atlas_index_location =
        real_glGetUniformLocation(g_currentProgram, "uAtlasIndex");
    if (offset_location < 0 || inverse_size_location < 0 ||
        atlas_index_location < 0) {
        LogHeldItemDiagnosticOnce(
            2u, "item program is missing atlas uniforms");
        return;
    }

    GLint offset[2]{};
    GLfloat inverse_size[2]{};
    GLint atlas_index = 0;
    real_glGetUniformiv(g_currentProgram, offset_location, offset);
    real_glGetUniformfv(g_currentProgram, inverse_size_location, inverse_size);
    real_glGetUniformiv(g_currentProgram, atlas_index_location, &atlas_index);
    if (atlas_index < 0 || atlas_index > 2 ||
        inverse_size[0] <= 0.0f || inverse_size[1] <= 0.0f) {
        LogHeldItemDiagnosticOnce(
            4u, "item atlas index or inverse size is invalid");
        return;
    }

    const char* sampler_names[3] = {"uTexture0", "uTexture1", "uTexture2"};
    const GLint sampler_location =
        real_glGetUniformLocation(g_currentProgram, sampler_names[atlas_index]);
    GLint texture_unit = atlas_index;
    if (sampler_location >= 0) {
        real_glGetUniformiv(g_currentProgram, sampler_location, &texture_unit);
    }
    if (texture_unit < 0 || texture_unit > 31) return;

    GLint previous_active_texture = GL_TEXTURE0_VALUE;
    GLint texture = 0;
    GLint texture_width = 0;
    GLint texture_height = 0;
    GLint vertex_array = 0;
    real_glGetIntegerv(GL_ACTIVE_TEXTURE_VALUE, &previous_active_texture);
    real_glActiveTexture(GL_TEXTURE0_VALUE + static_cast<GLenum>(texture_unit));
    real_glGetIntegerv(GL_TEXTURE_BINDING_2D_VALUE, &texture);
    if (texture > 0) {
        real_glGetTexLevelParameteriv(
            GL_TEXTURE_2D_VALUE, 0, GL_TEXTURE_WIDTH_VALUE, &texture_width);
        real_glGetTexLevelParameteriv(
            GL_TEXTURE_2D_VALUE, 0, GL_TEXTURE_HEIGHT_VALUE, &texture_height);
    }
    real_glActiveTexture(static_cast<GLenum>(previous_active_texture));
    real_glGetIntegerv(GL_VERTEX_ARRAY_BINDING_VALUE, &vertex_array);

    hytalevr::HeldItemUvBounds uv_bounds{};
    if (ResolveHeldItemUvBounds(
            draw, static_cast<GLuint>(vertex_array), uv_bounds)) {
        if (uv_bounds.min_u <=
                static_cast<uint32_t>((std::numeric_limits<int>::max)()) &&
            uv_bounds.min_v <=
                static_cast<uint32_t>((std::numeric_limits<int>::max)())) {
            offset[0] += static_cast<int>(uv_bounds.min_u);
            offset[1] += static_cast<int>(uv_bounds.min_v);
            LogHeldItemDiagnosticOnce(
                32u, "resolved packed item UV origin=" +
                         std::to_string(uv_bounds.min_u) + "," +
                         std::to_string(uv_bounds.min_v) +
                         " span=" +
                         std::to_string(uv_bounds.max_u - uv_bounds.min_u) +
                         "x" +
                         std::to_string(uv_bounds.max_v - uv_bounds.min_v));
        }
    } else {
        LogHeldItemDiagnosticOnce(
            64u, "could not resolve packed item UV coordinates");
    }
    if (texture <= 0 || texture_width <= 0 || texture_height <= 0 ||
        offset[0] < 0 || offset[1] < 0 ||
        offset[0] >= texture_width || offset[1] >= texture_height) {
        LogHeldItemDiagnosticOnce(
            8u, "item atlas texture binding or offset is invalid");
        return;
    }

    const int side = g_currentHeldItemSide;
    const LONG frame = InterlockedCompareExchange(
        &g_sharedData->renderFrameSequence, 0, 0);
    InterlockedIncrement(&g_sharedData->heldItemAtlasSequence);
    if (frame != g_heldItemAtlasPublishFrame) {
        g_heldItemAtlasPublishFrame = frame;
        InterlockedExchange(&g_sharedData->heldItemAtlasValidMask, 0);
    }
    g_sharedData->heldItemAtlasTextureIds[side] = texture;
    g_sharedData->heldItemAtlasOffsets[side][0] = offset[0];
    g_sharedData->heldItemAtlasOffsets[side][1] = offset[1];
    g_sharedData->heldItemAtlasSizes[side][0] = texture_width;
    g_sharedData->heldItemAtlasSizes[side][1] = texture_height;
    g_sharedData->heldItemVertexArrayIds[side] = vertex_array;
    g_sharedData->heldItemIndexCounts[side] = draw.count;
    g_sharedData->heldItemUvBounds[side][0] =
        uv_bounds.valid ? static_cast<LONG>(uv_bounds.min_u) : 0;
    g_sharedData->heldItemUvBounds[side][1] =
        uv_bounds.valid ? static_cast<LONG>(uv_bounds.min_v) : 0;
    g_sharedData->heldItemUvBounds[side][2] =
        uv_bounds.valid ? static_cast<LONG>(uv_bounds.max_u) : 0;
    g_sharedData->heldItemUvBounds[side][3] =
        uv_bounds.valid ? static_cast<LONG>(uv_bounds.max_v) : 0;
    g_sharedData->heldItemAtlasFrame = frame;
    MemoryBarrier();
    InterlockedOr(&g_sharedData->heldItemAtlasValidMask, 1L << side);
    InterlockedIncrement(&g_sharedData->heldItemAtlasSequence);
    LogHeldItemDiagnosticOnce(
        16u, "atlas publication succeeded program=" +
                 std::to_string(g_currentProgram) +
                 " texture=" + std::to_string(texture) +
                 " offset=" + std::to_string(offset[0]) + "," +
                 std::to_string(offset[1]) +
                 " size=" + std::to_string(texture_width) + "x" +
                 std::to_string(texture_height) +
                 " side=" + std::to_string(side));
}

void RegisterProgramIfUI(GLuint program) {
    if (program == 0) return;

    {
        std::lock_guard<std::mutex> lock(g_programsMutex);
        if (!g_scannedPrograms.insert(program).second) return;
    }
    
    if (real_glGetObjectLabel && real_glGetUniformLocation) {
        GLchar label[256] = {0};
        GLsizei length = 0;
        real_glGetObjectLabel(GL_PROGRAM, program, sizeof(label), &length, label);
        if (length > 0) {
            std::string labelStr(label, length);
            NotePossibleFirstPersonProgram(program, labelStr);
            if (labelStr.find("Batcher2DProgram") != std::string::npos ||
                labelStr.find("Batcher2D") != std::string::npos) {
                std::lock_guard<std::mutex> lock(g_programsMutex);
                RegisterProgramOnce(g_batcher2DPrograms, program, labelStr, "Batcher2D");
            }
            if (ContainsAny(labelStr, {"DeferredShadowProgram", "DeferredShadow", "ShadowProgram",
                                       "ShadowMap", "Shadow"})) {
                std::lock_guard<std::mutex> lock(g_programsMutex);
                RegisterProgramOnce(g_shadowPrograms, program, labelStr, "shadow");
            }
            if (ContainsAny(labelStr, {"SunOcclusionDownsampleProgram", "SunOcclusion",
                                       "ScreenSpaceOcclusion", "AmbientOcclusion"})) {
                std::lock_guard<std::mutex> lock(g_programsMutex);
                RegisterProgramOnce(g_sunOcclusionPrograms, program, labelStr, "sun occlusion");
            }
            if (ContainsAny(labelStr, {"SSAOProgram", "SSAOFs", "SSAO"})) {
                const GLint reprojectionLocation =
                    real_glGetUniformLocation(program, "uReprojectMatrix");
                std::lock_guard<std::mutex> lock(g_programsMutex);
                RegisterProgramOnce(g_ssaoPrograms, program, labelStr, "SSAO");
                if (reprojectionLocation >= 0) {
                    const bool alreadyRegistered = std::any_of(
                        g_ssaoReprojectionUniforms.begin(),
                        g_ssaoReprojectionUniforms.end(),
                        [&](const ProgramMatrixUniform& uniform) {
                            return uniform.program == program;
                        });
                    if (!alreadyRegistered) {
                        g_ssaoReprojectionUniforms.push_back(
                            {program, reprojectionLocation});
                    }
                }
            }
            if (labelStr.find("CloudShadow") != std::string::npos ||
                labelStr.find("CloudOcclusion") != std::string::npos ||
                labelStr.find("CloudsShadow") != std::string::npos ||
                ((labelStr.find("Cloud") != std::string::npos ||
                  labelStr.find("Clouds") != std::string::npos) &&
                 (labelStr.find("Shadow") != std::string::npos ||
                  labelStr.find("Occlusion") != std::string::npos))) {
                std::lock_guard<std::mutex> lock(g_programsMutex);
                RegisterProgramOnce(g_cloudShadowPrograms, program, labelStr, "cloud shadow");
            }
            if (labelStr.find("ParticleErosionProgram") != std::string::npos ||
                labelStr.find("ParticleProgram") != std::string::npos) {
                std::lock_guard<std::mutex> lock(g_programsMutex);
                RegisterProgramOnce(g_particlePrograms, program, labelStr, "particle");
            }
            if (labelStr.find("PostEffectProgram") != std::string::npos) {
                std::lock_guard<std::mutex> lock(g_programsMutex);
                RegisterProgramOnce(g_postEffectPrograms, program, labelStr, "post effect");
            }
            if (labelStr.find("MapChunkAlphaBlendedProgram") != std::string::npos) {
                std::lock_guard<std::mutex> lock(g_programsMutex);
                RegisterProgramOnce(g_mapChunkAlphaPrograms, program, labelStr, "map chunk alpha");
            }
            if (ContainsAny(labelStr, {"Moon", "Celestial", "SunMoon", "SunAndMoon",
                                       "Lunar", "StarField", "Stars"})) {
                std::lock_guard<std::mutex> lock(g_programsMutex);
                RegisterProgramOnce(g_celestialBillboardPrograms, program, labelStr,
                                    "celestial billboard");
            }
            if (labelStr.find("BasicProgram") != std::string::npos) {
                std::lock_guard<std::mutex> lock(g_programsMutex);
                RegisterProgramOnce(g_basicPrograms, program, labelStr, "basic");
            }
            if (labelStr.find("SkyProgram") != std::string::npos) {
                std::lock_guard<std::mutex> lock(g_programsMutex);
                RegisterProgramOnce(g_skyPrograms, program, labelStr, "sky");
            }
            if (labelStr.find("HiZReprojectProgram") != std::string::npos ||
                labelStr.find("HiZReproject") != std::string::npos) {
                std::lock_guard<std::mutex> lock(g_programsMutex);
                RegisterProgramOnce(g_hizReprojectPrograms, program, labelStr, "HiZ reprojection");
            }
            bool isUI = (labelStr.find("Batcher") != std::string::npos) || 
                        (labelStr.find("Text") != std::string::npos) ||
                        (labelStr.find("UI") != std::string::npos);
                        
            if (isUI) {
                GLint loc = real_glGetUniformLocation(program, "uMVPMatrix");
                UIProgram p;
                p.id = program;
                p.uMVPMatrixLocation = loc;
                p.name = labelStr;
                
                std::lock_guard<std::mutex> lock(g_programsMutex);
                const bool alreadyRegistered = std::any_of(
                    g_uiPrograms.begin(), g_uiPrograms.end(),
                    [program](const UIProgram& candidate) {
                        return candidate.id == program;
                    });
                if (!alreadyRegistered) {
                    g_uiPrograms.push_back(p);
                    LogMessage("HytaleUIScaleHook: Registered UI program! ID: " + std::to_string(program) + ", Label: " + labelStr + ", uMVPMatrix Location: " + std::to_string(loc));
                }
            }
        }
    }
}

void RegisterBufferIfUI(GLenum target, GLuint buffer) {
    if (buffer == 0) return;
    
    GLint mvpLoc = -1;
    if (g_currentProgram != 0 && IsUIProgram(g_currentProgram, -1, mvpLoc)) {
        std::lock_guard<std::mutex> lock(g_programsMutex);
        if (std::find(g_uiBufferIDs.begin(), g_uiBufferIDs.end(), buffer) == g_uiBufferIDs.end()) {
            g_uiBufferIDs.push_back(buffer);
            LogMessage("HytaleUIScaleHook: Registered UI buffer ID: " + std::to_string(buffer) + " for target: " + std::to_string(target));
        }
    }
}

bool IsUIBufferBound(GLenum target, GLuint& outBufferID) {
    GLenum query = 0;
    if (target == 0x8A11) query = 0x8A28; // GL_UNIFORM_BUFFER -> GL_UNIFORM_BUFFER_BINDING
    else if (target == 0x8892) query = 0x8894; // GL_ARRAY_BUFFER -> GL_ARRAY_BUFFER_BINDING
    else if (target == 0x8893) query = 0x8895; // GL_ELEMENT_ARRAY_BUFFER -> GL_ELEMENT_ARRAY_BUFFER_BINDING
    
    if (query != 0 && real_glGetIntegerv) {
        GLint bound = 0;
        real_glGetIntegerv(query, &bound);
        if (bound > 0) {
            std::lock_guard<std::mutex> lock(g_programsMutex);
            if (std::find(g_uiBufferIDs.begin(), g_uiBufferIDs.end(), (GLuint)bound) != g_uiBufferIDs.end()) {
                outBufferID = (GLuint)bound;
                return true;
            }
        }
    }
    return false;
}

// Window Enumeration to find Hytale Window
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId == GetCurrentProcessId()) {
        char className[256];
        GetClassNameA(hwnd, className, sizeof(className));
        if (strcmp(className, "SDL_app") == 0 || strstr(className, "Hytale") != nullptr) {
            g_gameHWND = hwnd;
            return FALSE; // Stop enumerating
        }
        if (g_gameHWND == NULL) {
            g_gameHWND = hwnd;
        }
    }
    return TRUE;
}

HWND GetGameWindow() {
    if (g_gameHWND == NULL) {
        EnumWindows(EnumWindowsProc, 0);
    }
    return g_gameHWND;
}

// WndProc hook to translate mouse coordinates
LRESULT CALLBACK hook_WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    g_gameHWND = hwnd; // Always keep game HWND updated dynamically
    
    float scale = (g_sharedData != nullptr) ? g_sharedData->uiScale : 1.0f;
    // Mouse hit testing must stay mono/stable. The render path receives a
    // per-eye X offset, but using it directly makes the cursor jump between
    // eyes. Use the left/reference-eye offset derived from the current stereo
    // separation so the cursor matches the pointer source eye.
    const float centerOffsetX = (scale != 0.0f) ? (1.0f - scale) : 0.0f;
    const float renderOffsetX = (g_sharedData != nullptr) ? g_sharedData->offsetX : centerOffsetX;
    float offsetX = centerOffsetX + std::fabs(renderOffsetX - centerOffsetX);
    float offsetY = (g_sharedData != nullptr) ? g_sharedData->offsetY : 0.0f;
    
    if (scale != 0.0f && scale != 1.0f) {
        switch (uMsg) {
            case WM_MOUSEMOVE:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP: {
                short x = (short)(lParam & 0xFFFF);
                short y = (short)((lParam >> 16) & 0xFFFF);
                
                RECT rect;
                if (real_GetClientRect && real_GetClientRect(hwnd, &rect)) {
                    float physWidth = (rect.right - rect.left);
                    float physHeight = (rect.bottom - rect.top);
                    
                    short unscaledX = (short)((x - offsetX * (physWidth / 2.0f)) / scale);
                    short unscaledY = (short)((y + offsetY * (physHeight / 2.0f)) / scale);
                    
                    lParam = (LPARAM)(((WORD)(((WORD)unscaledX) & 0xffff)) | ((DWORD)(((WORD)unscaledY)) << 16));
                }
                break;
            }
        }
    }
    
    return CallWindowProcW(real_WndProc, hwnd, uMsg, wParam, lParam);
}

// GetCursorPos hook to scale coordinates (Top-Left scale + Offsets)
BOOL WINAPI hook_GetCursorPos(LPPOINT lpPoint) {
    BOOL result = real_GetCursorPos(lpPoint);
    if (result && lpPoint != nullptr) {
        float scale = (g_sharedData != nullptr) ? g_sharedData->uiScale : 1.0f;
        // Use the stable reference-eye X offset for mouse coordinates.
        const float centerOffsetX = (scale != 0.0f) ? (1.0f - scale) : 0.0f;
        const float renderOffsetX = (g_sharedData != nullptr) ? g_sharedData->offsetX : centerOffsetX;
        float offsetX = centerOffsetX + std::fabs(renderOffsetX - centerOffsetX);
        float offsetY = (g_sharedData != nullptr) ? g_sharedData->offsetY : 0.0f;
        
        if (scale != 0.0f && scale != 1.0f) {
            HWND hwnd = GetGameWindow();
            if (hwnd != NULL && real_GetClientRect) {
                POINT clientPoint = *lpPoint;
                if (ScreenToClient(hwnd, &clientPoint)) {
                    RECT rect;
                    real_GetClientRect(hwnd, &rect);
                    float physWidth = (rect.right - rect.left);
                    float physHeight = (rect.bottom - rect.top);
                    
                    clientPoint.x = (LONG)((clientPoint.x - offsetX * (physWidth / 2.0f)) / scale);
                    clientPoint.y = (LONG)((clientPoint.y + offsetY * (physHeight / 2.0f)) / scale);
                    
                    *lpPoint = clientPoint;
                    ClientToScreen(hwnd, lpPoint);
                }
            }
        }
    }
    return result;
}

// SetCursorPos hook to scale coordinates back (Top-Left scale + Offsets)
BOOL WINAPI hook_SetCursorPos(int X, int Y) {
    float scale = (g_sharedData != nullptr) ? g_sharedData->uiScale : 1.0f;
    // Use the stable reference-eye X offset for mouse coordinates.
    const float centerOffsetX = (scale != 0.0f) ? (1.0f - scale) : 0.0f;
    const float renderOffsetX = (g_sharedData != nullptr) ? g_sharedData->offsetX : centerOffsetX;
    float offsetX = centerOffsetX + std::fabs(renderOffsetX - centerOffsetX);
    float offsetY = (g_sharedData != nullptr) ? g_sharedData->offsetY : 0.0f;
    
    if (scale != 0.0f && scale != 1.0f) {
        HWND hwnd = GetGameWindow();
        if (hwnd != NULL && real_GetClientRect) {
            POINT pt = { X, Y };
            if (ScreenToClient(hwnd, &pt)) {
                RECT rect;
                real_GetClientRect(hwnd, &rect);
                float physWidth = (rect.right - rect.left);
                float physHeight = (rect.bottom - rect.top);
                
                pt.x = (LONG)(pt.x * scale + offsetX * (physWidth / 2.0f));
                pt.y = (LONG)(pt.y * scale - offsetY * (physHeight / 2.0f));
                
                ClientToScreen(hwnd, &pt);
                X = pt.x;
                Y = pt.y;
            }
        }
    }
    return real_SetCursorPos(X, Y);
}

void HookWndProc() {
    if (real_WndProc != nullptr) return;
    HWND hwnd = GetGameWindow();
    if (hwnd != NULL) {
        real_WndProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)hook_WndProc);
        if (real_WndProc != nullptr) {
            LogMessage("HytaleUIScaleHook: WndProc subclassed successfully.");
        } else {
            LogMessage("HytaleUIScaleHook: SetWindowLongPtrW failed. Error: " + std::to_string(GetLastError()));
        }
    }
}

// Shared Memory Functions
void InitializeSharedMemory() {
    g_hMapFile = OpenFileMappingA(
        FILE_MAP_ALL_ACCESS, FALSE, hytalevr::kUiScaleMappingNameA);
    if (g_hMapFile == NULL) {
        g_hMapFile = CreateFileMappingA(
            INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(SharedData),
            hytalevr::kUiScaleMappingNameA);
        LogMessage("HytaleUIScaleHook: Shared memory mapping created.");
        if (g_hMapFile != NULL) {
            g_sharedData = (SharedData*)MapViewOfFile(g_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedData));
        }
    } else {
        LogMessage("HytaleUIScaleHook: Shared memory mapping opened.");
        g_sharedData = (SharedData*)MapViewOfFile(g_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedData));
    }
    
    if (g_hMapFile != NULL && g_sharedData != nullptr) {
        if (!hytalevr::ui_scale_shared_data_compatible(*g_sharedData)) {
            hytalevr::initialize_ui_scale_shared_data(*g_sharedData);
        }
        g_sharedData->firstPersonMatrixPatches = 0;
        g_sharedData->heldItemMatrixPatches = 0;
        if (g_sharedData->sceneDepthFarClip <= 0.0f) {
            g_sharedData->sceneDepthFarClip = 1024.0f;
        }
        LogMessage("HytaleUIScaleHook: Mapped shared memory. Scale: " + std::to_string(g_sharedData->uiScale) + ", OffsetX: " + std::to_string(g_sharedData->offsetX) + ", OffsetY: " + std::to_string(g_sharedData->offsetY) + ", DisableUboScaling: " + std::to_string(g_sharedData->disableUboScaling) + ", DisableShadows: " + std::to_string(g_sharedData->disableShadows) + ", DisableParticles: " + std::to_string(g_sharedData->disableParticles) + ", DisableDistortion: " + std::to_string(g_sharedData->disableDistortion) + ", HideFirstPerson: " + std::to_string(g_sharedData->hideFirstPerson) + ", MenuVisibleCounter: " + std::to_string(g_sharedData->menuVisibleCounter));
    } else {
        LogMessage("HytaleUIScaleHook: Shared memory mapping or view creation failed.");
    }
}

void CleanupSharedMemory() {
    if (g_sharedData != nullptr) {
        UnmapViewOfFile(g_sharedData);
        g_sharedData = nullptr;
    }
    if (g_hMapFile != NULL) {
        CloseHandle(g_hMapFile);
        g_hMapFile = NULL;
    }
    LogMessage("HytaleUIScaleHook: Shared memory cleaned up.");
}

void ScanExistingPrograms() {
    if (!real_glIsProgram || !real_glGetObjectLabel || !real_glGetUniformLocation) {
        LogMessage("HytaleUIScaleHook: Scan skipped. OpenGL pointers not resolved.");
        return;
    }
    LogMessage("HytaleUIScaleHook: Scanning existing shader programs (1 to 1000)...");
    int foundCount = 0;
    for (GLuint i = 1; i < 1000; ++i) {
        if (real_glIsProgram(i)) {
            foundCount++;
            RegisterProgramIfUI(i);
        }
    }
    LogMessage("HytaleUIScaleHook: Scan complete. Total active programs found in range: " + std::to_string(foundCount));
}

// OpenGL Hook Functions
void WINAPI hook_glObjectLabel(GLenum identifier, GLuint name, GLsizei length, const GLchar *label) {
    if (identifier == GL_PROGRAM && label != nullptr) {
        std::string labelStr;
        if (length < 0) {
            labelStr = label;
        } else {
            labelStr = std::string(label, length);
        }
        
        LogMessage("HytaleUIScaleHook: glObjectLabel called on GL_PROGRAM " + std::to_string(name) + " - Label: " + labelStr);
        real_glObjectLabel(identifier, name, length, label);
        {
            std::lock_guard<std::mutex> lock(g_programsMutex);
            g_scannedPrograms.erase(name);
        }
        RegisterProgramIfUI(name);
        if (name == g_currentProgram) RefreshCurrentProgramFlags(name);
        return;
    }
    real_glObjectLabel(identifier, name, length, label);
}

void WINAPI hook_glLinkProgram(GLuint program) {
    real_glLinkProgram(program);
    {
        std::lock_guard<std::mutex> lock(g_programsMutex);
        g_scannedPrograms.erase(program);
    }
    RegisterProgramIfUI(program);
    if (program == g_currentProgram) RefreshCurrentProgramFlags(program);
}

void WINAPI hook_glUseProgram(GLuint program) {
    const bool pending =
        g_pendingHeldItemMatrixReady && g_pendingHeldItemProgram == program;
    g_currentProgram = program;
    ++g_programUseSerial;
    g_heldItemMatrixReadyForDraw = pending;
    g_currentHeldItemSide = pending ? g_pendingHeldItemSide : -1;
    if (pending) {
        g_pendingHeldItemMatrixReady = false;
        g_pendingHeldItemProgram = 0;
        g_pendingHeldItemSide = -1;
    }
    real_glUseProgram(program);
    RegisterProgramIfUI(program);
    RefreshCurrentProgramFlags(program);
    if (g_currentProgramFlags.sky) {
        g_lastSkyProgramUseTick = GetTickCount();
    }
    if (g_currentProgramFlags.hizReproject) {
        TryPublishTexture0SceneDepth("HiZReprojectProgram use");
        RefreshCachedSceneDepthTexture();
    }
}

void WINAPI hook_glBindTexture(GLenum target, GLuint texture) {
    real_glBindTexture(target, texture);
    if (target != GL_TEXTURE_2D_VALUE || texture == 0 || g_currentProgram == 0 ||
        !g_currentProgramFlags.hizReproject || !real_glGetIntegerv) {
        return;
    }

    GLint activeTexture = GL_TEXTURE0_VALUE;
    real_glGetIntegerv(GL_ACTIVE_TEXTURE_VALUE, &activeTexture);
    if (activeTexture == GL_TEXTURE0_VALUE) {
        TryPublishBoundSceneDepthTexture("HiZReproject texture bind");
    }
}

void WINAPI hook_glTexImage2D(GLenum target, GLint level, GLint internalformat,
                              GLsizei width, GLsizei height, GLint border,
                              GLenum format, GLenum type, const void* pixels) {
    real_glTexImage2D(target, level, internalformat, width, height, border,
                      format, type, pixels);
    if (target == GL_TEXTURE_2D_VALUE && level == 0 && border == 0 &&
        internalformat == static_cast<GLint>(GL_R16F_VALUE) &&
        format == GL_RED_VALUE && type == GL_FLOAT_VALUE &&
        IsLikelyLinearSceneDepthTexture(width, height)) {
        TryPublishBoundSceneDepthTexture("R16F TexImage2D");
    }
}

bool ShouldSkipShadowDraw() {
    return g_sharedData != nullptr &&
           g_sharedData->disableShadows != 0 &&
           g_currentProgram != 0 &&
           IsShadowProgram(g_currentProgram);
}

bool ShouldNeutralizeSsaoDraw() {
    return g_sharedData != nullptr &&
           g_sharedData->disableShadows != 0 &&
           g_currentProgram != 0 &&
           IsSsaoProgram(g_currentProgram);
}

bool ShouldNeutralizeSunOcclusionDraw() {
    return g_sharedData != nullptr &&
           g_sharedData->disableShadows != 0 &&
           g_currentProgram != 0 &&
           IsSunOcclusionProgram(g_currentProgram);
}

bool ShouldNeutralizeCloudShadowDraw() {
    return g_sharedData != nullptr &&
           g_sharedData->disableShadows != 0 &&
           g_currentProgram != 0 &&
           IsCloudShadowProgram(g_currentProgram);
}

bool ShouldSkipParticleDraw() {
    return g_sharedData != nullptr &&
           g_sharedData->disableParticles != 0 &&
           g_currentProgram != 0 &&
           IsParticleProgram(g_currentProgram);
}

bool ShouldSkipCelestialBillboardDraw() {
    return g_sharedData != nullptr &&
           g_currentProgram != 0 &&
           IsCelestialBillboardProgram(g_currentProgram);
}

bool CurrentTexture2DIsSquareMoonCandidate() {
    if (!real_glGetIntegerv || !real_glGetTexLevelParameteriv) return false;

    GLint texture = 0;
    real_glGetIntegerv(GL_TEXTURE_BINDING_2D_VALUE, &texture);
    if (texture <= 0) return false;

    GLint width = 0;
    GLint height = 0;
    real_glGetTexLevelParameteriv(GL_TEXTURE_2D_VALUE, 0, GL_TEXTURE_WIDTH_VALUE, &width);
    real_glGetTexLevelParameteriv(GL_TEXTURE_2D_VALUE, 0, GL_TEXTURE_HEIGHT_VALUE, &height);
    return width == 256 && height == 256;
}

bool ShouldSkipCameraAnchoredMoonDraw(GLenum mode, GLsizei count) {
    if (g_sharedData == nullptr || g_currentProgram == 0) return false;
    if (ShouldSkipCelestialBillboardDraw()) return true;
    if (mode != GL_TRIANGLES_VALUE || count != 6 || !IsBasicProgram(g_currentProgram)) {
        return false;
    }
    const DWORD now = GetTickCount();
    if (g_lastSkyProgramUseTick == 0 || now - g_lastSkyProgramUseTick > 250) {
        return false;
    }
    return CurrentTexture2DIsSquareMoonCandidate();
}

bool ShouldNeutralizeDistortion() {
    return g_sharedData != nullptr &&
           g_sharedData->disableDistortion != 0 &&
           g_currentProgram != 0 &&
           IsPostEffectProgram(g_currentProgram);
}

bool VrDistortionReprojectionEnabled() {
    return g_sharedData != nullptr && g_sharedData->disableDistortion == 2;
}

void PublishDistortionTexture(GLuint texture) {
    if (!g_sharedData || texture == 0 || !real_glGetTexLevelParameteriv) return;

    GLint width = 0;
    GLint height = 0;
    real_glGetTexLevelParameteriv(GL_TEXTURE_2D_VALUE, 0, GL_TEXTURE_WIDTH_VALUE, &width);
    real_glGetTexLevelParameteriv(GL_TEXTURE_2D_VALUE, 0, GL_TEXTURE_HEIGHT_VALUE, &height);
    if (width <= 0 || height <= 0) return;

    g_sharedData->distortionTextureId = static_cast<LONG>(texture);
    g_sharedData->distortionTextureWidth = width;
    g_sharedData->distortionTextureHeight = height;
    g_sharedData->distortionTextureFrame = InterlockedCompareExchange(
        &g_sharedData->renderFrameSequence, 0, 0);
}

bool EnsureNeutralDistortionTexture() {
    if (g_neutralDistortionTexture != 0) return true;
    if (!real_glActiveTexture || !real_glBindTexture || !real_glGenTextures ||
        !real_glTexImage2D || !real_glTexParameteri || !real_glGetIntegerv) {
        return false;
    }

    GLint savedActiveTexture = 0;
    GLint savedTexture2D = 0;
    real_glGetIntegerv(GL_ACTIVE_TEXTURE_VALUE, &savedActiveTexture);
    real_glActiveTexture(GL_TEXTURE8_VALUE);
    real_glGetIntegerv(GL_TEXTURE_BINDING_2D_VALUE, &savedTexture2D);

    real_glGenTextures(1, &g_neutralDistortionTexture);
    real_glBindTexture(GL_TEXTURE_2D_VALUE, g_neutralDistortionTexture);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_MIN_FILTER_VALUE, GL_NEAREST_VALUE);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_MAG_FILTER_VALUE, GL_NEAREST_VALUE);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_WRAP_S_VALUE, GL_CLAMP_TO_EDGE_VALUE);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_WRAP_T_VALUE, GL_CLAMP_TO_EDGE_VALUE);
    const float neutralPixels[2] = {0.0f, 0.0f};
    real_glTexImage2D(GL_TEXTURE_2D_VALUE, 0, GL_RG16F_VALUE, 1, 1, 0,
                      GL_RG_VALUE, GL_FLOAT_VALUE, neutralPixels);

    real_glBindTexture(GL_TEXTURE_2D_VALUE, static_cast<GLuint>(savedTexture2D));
    real_glActiveTexture(static_cast<GLenum>(savedActiveTexture));

    LogMessage("HytaleUIScaleHook: Created neutral R16G16 distortion texture.");
    return g_neutralDistortionTexture != 0;
}

bool EnsureBlackTexture() {
    if (g_blackTexture != 0) return true;
    if (!real_glActiveTexture || !real_glBindTexture || !real_glGenTextures ||
        !real_glTexImage2D || !real_glTexParameteri || !real_glGetIntegerv) {
        return false;
    }

    GLint savedActiveTexture = 0;
    GLint savedTexture2D = 0;
    real_glGetIntegerv(GL_ACTIVE_TEXTURE_VALUE, &savedActiveTexture);
    real_glActiveTexture(GL_TEXTURE0_VALUE);
    real_glGetIntegerv(GL_TEXTURE_BINDING_2D_VALUE, &savedTexture2D);

    real_glGenTextures(1, &g_blackTexture);
    real_glBindTexture(GL_TEXTURE_2D_VALUE, g_blackTexture);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_MIN_FILTER_VALUE, GL_NEAREST_VALUE);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_MAG_FILTER_VALUE, GL_NEAREST_VALUE);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_WRAP_S_VALUE, GL_CLAMP_TO_EDGE_VALUE);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_WRAP_T_VALUE, GL_CLAMP_TO_EDGE_VALUE);
    const unsigned char blackPixels[4] = {0, 0, 0, 0};
    real_glTexImage2D(GL_TEXTURE_2D_VALUE, 0, 0x8058, 1, 1, 0,
                      0x1908, 0x1401, blackPixels);

    real_glBindTexture(GL_TEXTURE_2D_VALUE, static_cast<GLuint>(savedTexture2D));
    real_glActiveTexture(static_cast<GLenum>(savedActiveTexture));

    LogMessage("HytaleUIScaleHook: Created transparent black RGBA8 effect texture.");
    return g_blackTexture != 0;
}

bool EnsureFlatNormalTexture() {
    if (g_flatNormalTexture != 0) return true;
    if (!real_glActiveTexture || !real_glBindTexture || !real_glGenTextures ||
        !real_glTexImage2D || !real_glTexParameteri || !real_glGetIntegerv) {
        return false;
    }

    GLint savedActiveTexture = 0;
    GLint savedTexture2D = 0;
    real_glGetIntegerv(GL_ACTIVE_TEXTURE_VALUE, &savedActiveTexture);
    real_glActiveTexture(GL_TEXTURE0_VALUE);
    real_glGetIntegerv(GL_TEXTURE_BINDING_2D_VALUE, &savedTexture2D);

    real_glGenTextures(1, &g_flatNormalTexture);
    real_glBindTexture(GL_TEXTURE_2D_VALUE, g_flatNormalTexture);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_MIN_FILTER_VALUE, GL_NEAREST_VALUE);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_MAG_FILTER_VALUE, GL_NEAREST_VALUE);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_WRAP_S_VALUE, GL_CLAMP_TO_EDGE_VALUE);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_WRAP_T_VALUE, GL_CLAMP_TO_EDGE_VALUE);
    const unsigned char normalPixels[4] = {128, 128, 255, 255};
    real_glTexImage2D(GL_TEXTURE_2D_VALUE, 0, 0x8058, 1, 1, 0,
                      0x1908, 0x1401, normalPixels);

    real_glBindTexture(GL_TEXTURE_2D_VALUE, static_cast<GLuint>(savedTexture2D));
    real_glActiveTexture(static_cast<GLenum>(savedActiveTexture));

    LogMessage("HytaleUIScaleHook: Created flat normal RGBA8 texture.");
    return g_flatNormalTexture != 0;
}

bool EnsureWhiteDepthTexture() {
    if (g_whiteDepthTexture != 0) return true;
    if (!real_glActiveTexture || !real_glBindTexture || !real_glGenTextures ||
        !real_glTexImage2D || !real_glTexParameteri || !real_glGetIntegerv) {
        return false;
    }

    GLint savedActiveTexture = 0;
    GLint savedTexture2D = 0;
    real_glGetIntegerv(GL_ACTIVE_TEXTURE_VALUE, &savedActiveTexture);
    real_glActiveTexture(GL_TEXTURE0_VALUE);
    real_glGetIntegerv(GL_TEXTURE_BINDING_2D_VALUE, &savedTexture2D);

    real_glGenTextures(1, &g_whiteDepthTexture);
    real_glBindTexture(GL_TEXTURE_2D_VALUE, g_whiteDepthTexture);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_MIN_FILTER_VALUE, GL_NEAREST_VALUE);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_MAG_FILTER_VALUE, GL_NEAREST_VALUE);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_WRAP_S_VALUE, GL_CLAMP_TO_EDGE_VALUE);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_WRAP_T_VALUE, GL_CLAMP_TO_EDGE_VALUE);
    const float depthPixel = 1.0f;
    real_glTexImage2D(GL_TEXTURE_2D_VALUE, 0, GL_R16F_VALUE, 1, 1, 0,
                      GL_RED_VALUE, GL_FLOAT_VALUE, &depthPixel);

    real_glBindTexture(GL_TEXTURE_2D_VALUE, static_cast<GLuint>(savedTexture2D));
    real_glActiveTexture(static_cast<GLenum>(savedActiveTexture));

    LogMessage("HytaleUIScaleHook: Created white R16F depth texture.");
    return g_whiteDepthTexture != 0;
}

template <typename DrawCall>
void DrawWithOptionalNeutralDistortion(DrawCall drawCall) {
    if (!ShouldNeutralizeDistortion()) {
        drawCall();
        return;
    }
    if (!EnsureNeutralDistortionTexture()) {
        static bool loggedMissingGl = false;
        if (!loggedMissingGl) {
            LogMessage("HytaleUIScaleHook: cannot neutralize distortion, missing texture GL functions.");
            loggedMissingGl = true;
        }
        drawCall();
        return;
    }

    GLint savedActiveTexture = 0;
    GLint savedTexture2D = 0;
    real_glGetIntegerv(GL_ACTIVE_TEXTURE_VALUE, &savedActiveTexture);
    real_glActiveTexture(GL_TEXTURE8_VALUE);
    real_glGetIntegerv(GL_TEXTURE_BINDING_2D_VALUE, &savedTexture2D);
    if (VrDistortionReprojectionEnabled()) {
        PublishDistortionTexture(static_cast<GLuint>(savedTexture2D));
    } else if (g_sharedData) {
        g_sharedData->distortionTextureId = 0;
        g_sharedData->distortionTextureWidth = 0;
        g_sharedData->distortionTextureHeight = 0;
    }
    real_glBindTexture(GL_TEXTURE_2D_VALUE, g_neutralDistortionTexture);

    if (!g_neutralDistortionLogged) {
        LogMessage("HytaleUIScaleHook: isolating PostEffectProgram distortion on GL_TEXTURE8 for VR.");
        g_neutralDistortionLogged = true;
    }

    drawCall();

    real_glBindTexture(GL_TEXTURE_2D_VALUE, static_cast<GLuint>(savedTexture2D));
    real_glActiveTexture(static_cast<GLenum>(savedActiveTexture));
}

bool ShouldNeutralizeWaterEffects() {
    return g_sharedData != nullptr &&
           g_sharedData->disableParticles != 0 &&
           g_currentProgram != 0 &&
           IsMapChunkAlphaProgram(g_currentProgram);
}

template <typename DrawCall>
void DrawWithOptionalNeutralWaterEffects(DrawCall drawCall) {
    if (!ShouldNeutralizeWaterEffects()) {
        DrawWithOptionalNeutralDistortion(drawCall);
        return;
    }
    if (!EnsureBlackTexture() || !EnsureFlatNormalTexture() || !EnsureWhiteDepthTexture()) {
        static bool loggedMissingGl = false;
        if (!loggedMissingGl) {
            LogMessage("HytaleUIScaleHook: cannot neutralize water effects, missing neutral textures or GL functions.");
            loggedMissingGl = true;
        }
        DrawWithOptionalNeutralDistortion(drawCall);
        return;
    }

    constexpr GLenum textureUnits[8] = {
        0x84C1, 0x84C2, 0x84C3, 0x84C4, 0x84C5, 0x84C6, 0x84C7, 0x84CC
    }; // GL_TEXTURE1/2/3/4/5/6/7/12.
    GLint savedActiveTexture = 0;
    GLint savedTexture2D[8] = {};
    real_glGetIntegerv(GL_ACTIVE_TEXTURE_VALUE, &savedActiveTexture);
    for (int i = 0; i < 8; ++i) {
        real_glActiveTexture(textureUnits[i]);
        real_glGetIntegerv(GL_TEXTURE_BINDING_2D_VALUE, &savedTexture2D[i]);
        GLuint replacement = g_blackTexture;
        if (i == 0 || i == 7) replacement = g_whiteDepthTexture;
        if (i == 1) replacement = g_flatNormalTexture;
        real_glBindTexture(GL_TEXTURE_2D_VALUE, replacement);
    }

    static bool logged = false;
    if (!logged) {
        LogMessage("HytaleUIScaleHook: neutralizing MapChunkAlphaBlendedProgram slots 1/2/3/4/5/6/7/12. Previous textures: " +
                   std::to_string(savedTexture2D[0]) + "/" +
                   std::to_string(savedTexture2D[1]) + "/" +
                   std::to_string(savedTexture2D[2]) + "/" +
                   std::to_string(savedTexture2D[3]) + "/" +
                   std::to_string(savedTexture2D[4]) + "/" +
                   std::to_string(savedTexture2D[5]) + "/" +
                   std::to_string(savedTexture2D[6]) + "/" +
                   std::to_string(savedTexture2D[7]));
        logged = true;
    }

    DrawWithOptionalNeutralDistortion(drawCall);

    for (int i = 0; i < 8; ++i) {
        real_glActiveTexture(textureUnits[i]);
        real_glBindTexture(GL_TEXTURE_2D_VALUE, static_cast<GLuint>(savedTexture2D[i]));
    }
    real_glActiveTexture(static_cast<GLenum>(savedActiveTexture));
}

struct VrSceneMatrixSnapshot {
    float view[16];
    float projection[16];
    float viewProjection[16];
    float inverseView[16];
    float inverseViewProjection[16];
    float reprojection[16];
    float projectionInfo[4];
};

bool ReadVrSceneMatrixSnapshot(VrSceneMatrixSnapshot& snapshot) {
    if (!g_sharedData ||
        InterlockedCompareExchange(&g_sharedData->vrSceneMatricesValid, 0, 0) == 0) {
        return false;
    }

    for (int attempt = 0; attempt < 4; ++attempt) {
        const LONG begin = InterlockedCompareExchange(
            &g_sharedData->vrSceneMatrixSequence, 0, 0);
        if ((begin & 1) != 0) continue;

        memcpy(snapshot.view, g_sharedData->vrSceneView, sizeof(snapshot.view));
        memcpy(snapshot.projection, g_sharedData->vrSceneProjection,
               sizeof(snapshot.projection));
        memcpy(snapshot.viewProjection, g_sharedData->vrSceneViewProjection,
               sizeof(snapshot.viewProjection));
        memcpy(snapshot.inverseView, g_sharedData->vrSceneInvView,
               sizeof(snapshot.inverseView));
        memcpy(snapshot.inverseViewProjection,
               g_sharedData->vrSceneInvViewProjection,
               sizeof(snapshot.inverseViewProjection));
        memcpy(snapshot.reprojection, g_sharedData->vrSceneReprojection,
               sizeof(snapshot.reprojection));
        memcpy(snapshot.projectionInfo, g_sharedData->vrSceneProjectionInfo,
               sizeof(snapshot.projectionInfo));
        MemoryBarrier();

        const LONG end = InterlockedCompareExchange(
            &g_sharedData->vrSceneMatrixSequence, 0, 0);
        if (begin != end || (end & 1) != 0) continue;

        const float* values = reinterpret_cast<const float*>(&snapshot);
        for (size_t i = 0; i < sizeof(snapshot) / sizeof(float); ++i) {
            if (!std::isfinite(values[i])) return false;
        }
        return true;
    }
    return false;
}

bool CurrentProgramNeedsVrSceneMatrices() {
    if (!g_sharedData || g_currentProgram == 0) return false;
    const bool shadowPass =
        (g_currentProgramFlags.shadow || g_currentProgramFlags.sunOcclusion ||
         g_currentProgramFlags.ssao || g_currentProgramFlags.cloudShadow) &&
        g_sharedData->disableShadows == 0;
    const bool particlePass =
        g_currentProgramFlags.particle && g_sharedData->disableParticles == 0;
    const bool distortionPass =
        (g_currentProgramFlags.postEffect || g_currentProgramFlags.mapChunkAlpha) &&
        g_sharedData->disableDistortion == 2;
    return shadowPass || particlePass || distortionPass;
}

void PatchVrSceneDataForCurrentProgram() {
    if (!CurrentProgramNeedsVrSceneMatrices() || !real_glBindBuffer ||
        !real_glBufferSubData || !real_glGetIntegerv || g_sceneDataBuffer == 0 ||
        g_lastScenePatchProgramUseSerial == g_programUseSerial) {
        return;
    }

    VrSceneMatrixSnapshot snapshot{};
    if (!ReadVrSceneMatrixSnapshot(snapshot)) return;

    // SceneData_inc.glsl uses std140. The camera section starts after the two
    // first-person matrices at byte 128 and contains six matrices followed by
    // ProjInfos, ending at byte 528.
    float cameraBlock[100]{};
    size_t cursor = 0;
    const auto append = [&](const float* source, size_t count) {
        memcpy(cameraBlock + cursor, source, count * sizeof(float));
        cursor += count;
    };
    append(snapshot.view, 16);
    append(snapshot.projection, 16);
    append(snapshot.viewProjection, 16);
    append(snapshot.inverseView, 16);
    append(snapshot.inverseViewProjection, 16);
    append(snapshot.reprojection, 16);
    append(snapshot.projectionInfo, 4);

    GLint previousBuffer = 0;
    real_glGetIntegerv(GL_UNIFORM_BUFFER_BINDING_VALUE, &previousBuffer);
    real_glBindBuffer(GL_UNIFORM_BUFFER_VALUE, g_sceneDataBuffer);
    real_glBufferSubData(GL_UNIFORM_BUFFER_VALUE,
                         g_sceneDataBufferOffset + 128,
                         static_cast<GLsizeiptr>(sizeof(cameraBlock)), cameraBlock);
    real_glBindBuffer(GL_UNIFORM_BUFFER_VALUE, static_cast<GLuint>(previousBuffer));
    g_lastScenePatchProgramUseSerial = g_programUseSerial;

    static DWORD lastLogTick = 0;
    const DWORD now = GetTickCount();
    if (now - lastLogTick >= 1500) {
        LogMessage("HytaleUIScaleHook: synchronized VR SceneData matrices for effect program=" +
                   std::to_string(g_currentProgram) + " buffer=" +
                   std::to_string(g_sceneDataBuffer));
        lastLogTick = now;
    }
}

bool EnsureHeldItemDepthTarget(int width, int height) {
    if (width <= 0 || height <= 0 || !real_glGetIntegerv || !real_glGenTextures ||
        !real_glBindTexture || !real_glTexImage2D || !real_glTexParameteri ||
        !real_glGenFramebuffers || !real_glBindFramebuffer ||
        !real_glFramebufferTexture2D || !real_glCheckFramebufferStatus ||
        !raw_glDrawBuffer || !raw_glReadBuffer) {
        return false;
    }
    if (g_heldItemColorTexture != 0 && g_heldItemDepthTexture != 0 &&
        g_heldItemDepthFbo != 0 &&
        g_heldItemDepthWidth == width && g_heldItemDepthHeight == height) {
        return true;
    }

    GLint previous_texture = 0;
    GLint previous_read_fbo = 0;
    GLint previous_draw_fbo = 0;
    GLint previous_read_buffer = 0;
    GLint previous_draw_buffer = 0;
    real_glGetIntegerv(GL_TEXTURE_BINDING_2D_VALUE, &previous_texture);
    real_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read_fbo);
    real_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous_draw_fbo);
    real_glGetIntegerv(GL_READ_BUFFER_VALUE, &previous_read_buffer);
    real_glGetIntegerv(GL_DRAW_BUFFER_VALUE, &previous_draw_buffer);

    if (g_heldItemColorTexture == 0) {
        real_glGenTextures(1, &g_heldItemColorTexture);
    }
    if (g_heldItemDepthTexture == 0) {
        real_glGenTextures(1, &g_heldItemDepthTexture);
    }
    if (g_heldItemDepthFbo == 0) {
        real_glGenFramebuffers(1, &g_heldItemDepthFbo);
    }
    if (g_heldItemColorTexture == 0 || g_heldItemDepthTexture == 0 ||
        g_heldItemDepthFbo == 0) {
        return false;
    }

    real_glBindTexture(GL_TEXTURE_2D_VALUE, g_heldItemColorTexture);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_MIN_FILTER_VALUE, GL_LINEAR_VALUE);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_MAG_FILTER_VALUE, GL_LINEAR_VALUE);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_WRAP_S_VALUE, GL_CLAMP_TO_EDGE_VALUE);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_WRAP_T_VALUE, GL_CLAMP_TO_EDGE_VALUE);
    real_glTexImage2D(GL_TEXTURE_2D_VALUE, 0, GL_RGBA8_VALUE,
                      width, height, 0, GL_RGBA_VALUE,
                      GL_UNSIGNED_BYTE_VALUE, nullptr);

    real_glBindTexture(GL_TEXTURE_2D_VALUE, g_heldItemDepthTexture);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_MIN_FILTER_VALUE, GL_NEAREST_VALUE);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_MAG_FILTER_VALUE, GL_NEAREST_VALUE);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_WRAP_S_VALUE, GL_CLAMP_TO_EDGE_VALUE);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_WRAP_T_VALUE, GL_CLAMP_TO_EDGE_VALUE);
    real_glTexParameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_COMPARE_MODE_VALUE, GL_NONE_VALUE);
    real_glTexImage2D(GL_TEXTURE_2D_VALUE, 0, GL_DEPTH_COMPONENT32F_VALUE,
                      width, height, 0, GL_DEPTH_COMPONENT_VALUE,
                      GL_FLOAT_VALUE, nullptr);

    real_glBindFramebuffer(GL_FRAMEBUFFER_VALUE, g_heldItemDepthFbo);
    real_glFramebufferTexture2D(GL_FRAMEBUFFER_VALUE, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D_VALUE, g_heldItemColorTexture, 0);
    real_glFramebufferTexture2D(GL_FRAMEBUFFER_VALUE, GL_DEPTH_ATTACHMENT_VALUE,
                                GL_TEXTURE_2D_VALUE, g_heldItemDepthTexture, 0);
    raw_glDrawBuffer(GL_COLOR_ATTACHMENT0);
    raw_glReadBuffer(GL_COLOR_ATTACHMENT0);
    const bool complete =
        real_glCheckFramebufferStatus(GL_FRAMEBUFFER_VALUE) == GL_FRAMEBUFFER_COMPLETE;

    real_glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previous_read_fbo));
    real_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previous_draw_fbo));
    raw_glReadBuffer(static_cast<GLenum>(previous_read_buffer));
    raw_glDrawBuffer(static_cast<GLenum>(previous_draw_buffer));
    real_glBindTexture(GL_TEXTURE_2D_VALUE, static_cast<GLuint>(previous_texture));
    if (!complete) return false;

    g_heldItemDepthWidth = width;
    g_heldItemDepthHeight = height;
    g_heldItemDepthClearedFrame = -1;
    return true;
}

template <typename DrawCall>
bool CaptureHeldItemDepth(DrawCall&& draw_call) {
    if (!g_sharedData || !g_currentProgramFlags.heldItem ||
        !g_heldItemMatrixReadyForDraw || !real_glViewport || !real_glClear ||
        !real_glGetFloatv || !real_glClearColor ||
        !raw_glDepthMask || !raw_glColorMask || !raw_glEnable ||
        !raw_glDisable || !raw_glClearDepth || !raw_glDepthFunc ||
        !raw_glGetBooleanv || !raw_glIsEnabled) {
        return false;
    }

    GLint viewport[4]{};
    real_glGetIntegerv(GL_VIEWPORT, viewport);
    if (!EnsureHeldItemDepthTarget(viewport[2], viewport[3])) return false;

    GLint previous_read_fbo = 0;
    GLint previous_draw_fbo = 0;
    GLint previous_read_buffer = 0;
    GLint previous_draw_buffer = 0;
    GLint previous_depth_func = 0;
    GLboolean previous_color_mask[4]{};
    GLboolean previous_depth_mask = 0;
    GLfloat previous_clear_depth = 1.0f;
    GLfloat previous_clear_color[4]{};
    real_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read_fbo);
    real_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous_draw_fbo);
    real_glGetIntegerv(GL_READ_BUFFER_VALUE, &previous_read_buffer);
    real_glGetIntegerv(GL_DRAW_BUFFER_VALUE, &previous_draw_buffer);
    real_glGetIntegerv(GL_DEPTH_FUNC_VALUE, &previous_depth_func);
    raw_glGetBooleanv(GL_COLOR_WRITEMASK_VALUE, previous_color_mask);
    raw_glGetBooleanv(GL_DEPTH_WRITEMASK_VALUE, &previous_depth_mask);
    real_glGetFloatv(GL_DEPTH_CLEAR_VALUE, &previous_clear_depth);
    real_glGetFloatv(GL_COLOR_CLEAR_VALUE, previous_clear_color);
    const GLboolean depth_test_enabled = raw_glIsEnabled(GL_DEPTH_TEST_VALUE);

    real_glBindFramebuffer(GL_FRAMEBUFFER_VALUE, g_heldItemDepthFbo);
    raw_glDrawBuffer(GL_COLOR_ATTACHMENT0);
    raw_glReadBuffer(GL_COLOR_ATTACHMENT0);
    real_glViewport(0, 0, g_heldItemDepthWidth, g_heldItemDepthHeight);
    raw_glColorMask(1, 1, 1, 1);
    raw_glDepthMask(1);
    raw_glEnable(GL_DEPTH_TEST_VALUE);
    raw_glDepthFunc(GL_LEQUAL_VALUE);

    const LONG frame = InterlockedCompareExchange(
        &g_sharedData->renderFrameSequence, 0, 0);
    if (frame != g_heldItemDepthClearedFrame) {
        real_glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        raw_glClearDepth(1.0);
        real_glClear(GL_COLOR_BUFFER_BIT_VALUE | GL_DEPTH_BUFFER_BIT_VALUE);
        g_heldItemDepthClearedFrame = frame;
    }
    draw_call();

    InterlockedExchange(&g_sharedData->heldItemLayerTextureId,
                        static_cast<LONG>(g_heldItemColorTexture));
    InterlockedExchange(&g_sharedData->heldItemDepthTextureId,
                        static_cast<LONG>(g_heldItemDepthTexture));
    InterlockedExchange(&g_sharedData->heldItemDepthTextureWidth,
                        g_heldItemDepthWidth);
    InterlockedExchange(&g_sharedData->heldItemDepthTextureHeight,
                        g_heldItemDepthHeight);
    InterlockedExchange(&g_sharedData->heldItemDepthTextureFrame, frame);

    real_glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previous_read_fbo));
    real_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previous_draw_fbo));
    raw_glReadBuffer(static_cast<GLenum>(previous_read_buffer));
    raw_glDrawBuffer(static_cast<GLenum>(previous_draw_buffer));
    real_glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    raw_glColorMask(previous_color_mask[0], previous_color_mask[1],
                    previous_color_mask[2], previous_color_mask[3]);
    raw_glDepthMask(previous_depth_mask);
    raw_glDepthFunc(static_cast<GLenum>(previous_depth_func));
    raw_glClearDepth(previous_clear_depth);
    real_glClearColor(previous_clear_color[0], previous_clear_color[1],
                      previous_clear_color[2], previous_clear_color[3]);
    if (depth_test_enabled) raw_glEnable(GL_DEPTH_TEST_VALUE);
    else raw_glDisable(GL_DEPTH_TEST_VALUE);
    return true;
}

void WINAPI hook_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (ShouldSkipFirstPersonDraw()) {
        static bool logged = false;
        if (!logged) {
            LogMessage("HytaleUIScaleHook: skipping possible first-person/viewmodel glDrawArrays.");
            logged = true;
        }
        return;
    }
    PatchVrSceneDataForCurrentProgram();
    if (ShouldNeutralizeSsaoDraw() || ShouldNeutralizeSunOcclusionDraw() || ShouldNeutralizeCloudShadowDraw()) {
        if (real_glClearColor && real_glClear && real_glGetFloatv) {
            GLfloat previous[4] = {};
            real_glGetFloatv(GL_COLOR_CLEAR_VALUE, previous);
            real_glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
            real_glClear(GL_COLOR_BUFFER_BIT_VALUE);
            real_glClearColor(previous[0], previous[1], previous[2], previous[3]);
            static bool logged = false;
            if (!logged) {
                LogMessage("HytaleUIScaleHook: neutralized SSAO/sun occlusion/cloud shadow output to white on glDrawArrays.");
                logged = true;
            }
            return;
        }
    }
    if (ShouldSkipShadowDraw()) {
        static bool logged = false;
        if (!logged) {
            LogMessage("HytaleUIScaleHook: skipping shadow/sun occlusion glDrawArrays while shadows are disabled.");
            logged = true;
        }
        return;
    }
    if (ShouldSkipCameraAnchoredMoonDraw(mode, count)) {
        static bool logged = false;
        if (!logged) {
            LogMessage("HytaleUIScaleHook: skipping camera-anchored moon/celestial glDrawArrays.");
            logged = true;
        }
        return;
    }
    if (ShouldSkipParticleDraw()) {
        static bool logged = false;
        if (!logged) {
            LogMessage("HytaleUIScaleHook: skipping particle glDrawArrays while rain/effects are disabled.");
            logged = true;
        }
        return;
    }
    if (g_currentProgramFlags.heldItem && g_heldItemMatrixReadyForDraw) {
        LogHeldItemRenderStackOnce();
        PublishHeldItemAtlasState(
            {false, mode, first, count, 0, 0});
        return;
    }
    DrawWithOptionalNeutralWaterEffects([&]() {
        real_glDrawArrays(mode, first, count);
    });
}

void WINAPI hook_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    NoteBatcher2DMenuDraw(count);
    if (CaptureOrSuppressBatcher2DMenuDraw(mode, count, type, indices)) {
        return;
    }
    if (ShouldSkipFirstPersonDraw()) {
        static bool logged = false;
        if (!logged) {
            LogMessage("HytaleUIScaleHook: skipping possible first-person/viewmodel glDrawElements.");
            logged = true;
        }
        return;
    }
    PatchVrSceneDataForCurrentProgram();
    if (ShouldNeutralizeSsaoDraw() || ShouldNeutralizeSunOcclusionDraw() || ShouldNeutralizeCloudShadowDraw()) {
        if (real_glClearColor && real_glClear && real_glGetFloatv) {
            GLfloat previous[4] = {};
            real_glGetFloatv(GL_COLOR_CLEAR_VALUE, previous);
            real_glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
            real_glClear(GL_COLOR_BUFFER_BIT_VALUE);
            real_glClearColor(previous[0], previous[1], previous[2], previous[3]);
            static bool logged = false;
            if (!logged) {
                LogMessage("HytaleUIScaleHook: neutralized SSAO/sun occlusion/cloud shadow output to white on glDrawElements.");
                logged = true;
            }
            return;
        }
    }
    if (ShouldSkipShadowDraw()) {
        static bool logged = false;
        if (!logged) {
            LogMessage("HytaleUIScaleHook: skipping shadow/sun occlusion glDrawElements while shadows are disabled.");
            logged = true;
        }
        return;
    }
    if (ShouldSkipCameraAnchoredMoonDraw(mode, count)) {
        static bool logged = false;
        if (!logged) {
            LogMessage("HytaleUIScaleHook: skipping camera-anchored moon/celestial glDrawElements.");
            logged = true;
        }
        return;
    }
    if (ShouldSkipParticleDraw()) {
        static bool logged = false;
        if (!logged) {
            LogMessage("HytaleUIScaleHook: skipping particle glDrawElements while rain/effects are disabled.");
            logged = true;
        }
        return;
    }
    if (g_currentProgramFlags.heldItem && g_heldItemMatrixReadyForDraw) {
        LogHeldItemRenderStackOnce();
        PublishHeldItemAtlasState(
            {true, mode, 0, count, type, reinterpret_cast<uintptr_t>(indices)});
        return;
    }
    DrawWithOptionalNeutralWaterEffects([&]() {
        real_glDrawElements(mode, count, type, indices);
    });
}

bool TryPatchSsaoReprojectionMatrix(GLuint program,
                                    GLint location,
                                    GLsizei count,
                                    const GLfloat* value,
                                    GLfloat (&modified)[16]) {
    if (!g_sharedData || g_sharedData->disableShadows != 0 || count != 1 ||
        value == nullptr || !IsSsaoReprojectionUniform(program, location)) {
        return false;
    }

    VrSceneMatrixSnapshot snapshot{};
    if (!ReadVrSceneMatrixSnapshot(snapshot)) return false;
    memcpy(modified, snapshot.reprojection, sizeof(modified));

    static DWORD lastLogTick = 0;
    const DWORD now = GetTickCount();
    if (now - lastLogTick >= 1500) {
        LogMessage("HytaleUIScaleHook: replaced SSAO/shadow temporal uReprojectMatrix for program=" +
                   std::to_string(program));
        lastLogTick = now;
    }
    return true;
}

void WINAPI hook_glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    GLint mvpLoc = -1;
    float scale = (g_sharedData != nullptr) ? g_sharedData->uiScale : 1.0f;
    float offsetX = (g_sharedData != nullptr) ? g_sharedData->offsetX : 0.0f;
    float offsetY = (g_sharedData != nullptr) ? g_sharedData->offsetY : 0.0f;

    GLfloat reprojectionModified[16];
    if (TryPatchSsaoReprojectionMatrix(g_currentProgram, location, count, value,
                                       reprojectionModified)) {
        real_glUniformMatrix4fv(location, count, transpose, reprojectionModified);
        return;
    }

    GLfloat heldItemModified[16];
    if (TryPatchHeldItemModelMatrix(g_currentProgram, location, count, transpose,
                                    value, heldItemModified)) {
        real_glUniformMatrix4fv(location, count, transpose, heldItemModified);
        return;
    }
    
    if (g_currentProgram != 0 &&
        IsUIProgram(g_currentProgram, location, mvpLoc) && value != nullptr &&
        (scale != 1.0f || offsetX != 0.0f || offsetY != 0.0f)) {
        static float lastLoggedScale = -1.0f;
        static float lastLoggedX = 0.0f;
        static float lastLoggedY = 0.0f;
        static GLuint lastLoggedProgram = 0;
        if (scale != lastLoggedScale || offsetX != lastLoggedX || offsetY != lastLoggedY || g_currentProgram != lastLoggedProgram) {
            LogMessage("HytaleUIScaleHook: Adjusting glUniformMatrix4fv for program " + std::to_string(g_currentProgram) + ". Scale: " + std::to_string(scale) + ", X: " + std::to_string(offsetX) + ", Y: " + std::to_string(offsetY));
            lastLoggedScale = scale;
            lastLoggedX = offsetX;
            lastLoggedY = offsetY;
            lastLoggedProgram = g_currentProgram;
        }
        
        GLfloat modified[16];
        memcpy(modified, value, sizeof(modified));
        
        // Scale terms
        modified[0] *= scale;
        modified[5] *= scale;
        
        // Offset/Translation terms
        modified[12] += offsetX;
        modified[13] += offsetY;
        
        real_glUniformMatrix4fv(location, count, transpose, modified);
        return;
    }
    real_glUniformMatrix4fv(location, count, transpose, value);
}

void WINAPI hook_glProgramUniformMatrix4fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    RegisterProgramIfUI(program);
    
    GLint mvpLoc = -1;
    float scale = (g_sharedData != nullptr) ? g_sharedData->uiScale : 1.0f;
    float offsetX = (g_sharedData != nullptr) ? g_sharedData->offsetX : 0.0f;
    float offsetY = (g_sharedData != nullptr) ? g_sharedData->offsetY : 0.0f;

    GLfloat reprojectionModified[16];
    if (TryPatchSsaoReprojectionMatrix(program, location, count, value,
                                       reprojectionModified)) {
        real_glProgramUniformMatrix4fv(program, location, count, transpose,
                                       reprojectionModified);
        return;
    }

    GLfloat heldItemModified[16];
    if (TryPatchHeldItemModelMatrix(program, location, count, transpose,
                                    value, heldItemModified)) {
        real_glProgramUniformMatrix4fv(program, location, count, transpose,
                                       heldItemModified);
        return;
    }
    
    if (program != 0 &&
        IsUIProgram(program, location, mvpLoc) && value != nullptr &&
        (scale != 1.0f || offsetX != 0.0f || offsetY != 0.0f)) {
        static float lastLoggedScale = -1.0f;
        static float lastLoggedX = 0.0f;
        static float lastLoggedY = 0.0f;
        static GLuint lastLoggedProgram = 0;
        if (scale != lastLoggedScale || offsetX != lastLoggedX || offsetY != lastLoggedY || program != lastLoggedProgram) {
            LogMessage("HytaleUIScaleHook: Adjusting glProgramUniformMatrix4fv for program " + std::to_string(program) + ". Scale: " + std::to_string(scale) + ", X: " + std::to_string(offsetX) + ", Y: " + std::to_string(offsetY));
            lastLoggedScale = scale;
            lastLoggedX = offsetX;
            lastLoggedY = offsetY;
            lastLoggedProgram = program;
        }
        
        GLfloat modified[16];
        memcpy(modified, value, sizeof(modified));
        
        // Scale terms
        modified[0] *= scale;
        modified[5] *= scale;
        
        // Offset/Translation terms
        modified[12] += offsetX;
        modified[13] += offsetY;
        
        real_glProgramUniformMatrix4fv(program, location, count, transpose, modified);
        return;
    }
    real_glProgramUniformMatrix4fv(program, location, count, transpose, value);
}

// UBO tracking hooks and helpers
void WINAPI hook_glBindBuffer(GLenum target, GLuint buffer) {
    real_glBindBuffer(target, buffer);
    RegisterBufferIfUI(target, buffer);
}

void WINAPI hook_glBindBufferRange(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size) {
    real_glBindBufferRange(target, index, buffer, offset, size);
    if (target == GL_UNIFORM_BUFFER_VALUE && index == 0) {
        g_sceneDataBuffer = buffer;
        g_sceneDataBufferOffset = offset;
        g_lastScenePatchProgramUseSerial = 0;
    }
    RegisterBufferIfUI(target, buffer);
}

void WINAPI hook_glBindBufferBase(GLenum target, GLuint index, GLuint buffer) {
    real_glBindBufferBase(target, index, buffer);
    if (target == GL_UNIFORM_BUFFER_VALUE && index == 0) {
        g_sceneDataBuffer = buffer;
        g_sceneDataBufferOffset = 0;
        g_lastScenePatchProgramUseSerial = 0;
    }
    RegisterBufferIfUI(target, buffer);
}

GLuint GetBoundBufferID(GLenum target) {
    GLenum query = 0;
    if (target == 0x8A11) query = 0x8A28; // GL_UNIFORM_BUFFER -> GL_UNIFORM_BUFFER_BINDING
    else if (target == 0x8892) query = 0x8894; // GL_ARRAY_BUFFER -> GL_ARRAY_BUFFER_BINDING
    else if (target == 0x8893) query = 0x8895; // GL_ELEMENT_ARRAY_BUFFER -> GL_ELEMENT_ARRAY_BUFFER_BINDING
    
    if (query != 0 && real_glGetIntegerv) {
        GLint bound = 0;
        real_glGetIntegerv(query, &bound);
        return (GLuint)bound;
    }
    return 0;
}

void ScanAndModifyMatrix(void* data, GLsizeiptr size, const std::string& source, GLuint bufferID, GLintptr offset) {
    if (data == nullptr || size < 64) return;
    
    if (g_sharedData != nullptr && g_sharedData->disableUboScaling != 0) {
        return;
    }
    
    float scale = (g_sharedData != nullptr) ? g_sharedData->uiScale : 1.0f;
    float offsetX = (g_sharedData != nullptr) ? g_sharedData->offsetX : 0.0f;
    float offsetY = (g_sharedData != nullptr) ? g_sharedData->offsetY : 0.0f;
    
    bool hasChanges = (scale != 1.0f || offsetX != 0.0f || offsetY != 0.0f);
    
    static float lastLoggedScale = -1.0f;
    static float lastLoggedX = 0.0f;
    static float lastLoggedY = 0.0f;
    
    if (scale != lastLoggedScale || offsetX != lastLoggedX || offsetY != lastLoggedY) {
        std::lock_guard<std::mutex> lock(g_loggedBuffersMutex);
        g_loggedBufferIDs.clear();
        lastLoggedScale = scale;
        lastLoggedX = offsetX;
        lastLoggedY = offsetY;
        LogMessage("HytaleUIScaleHook: Scale/Offset changed. Scale=" + std::to_string(scale) + ", X=" + std::to_string(offsetX) + ", Y=" + std::to_string(offsetY) + ". Clearing logged buffers cache.");
    }
    
    int floatCount = (int)(size / sizeof(float));
    for (int i = 0; i <= floatCount - 16; i += 4) {
        float* M = (float*)data + i;
        
        if (M[15] == 1.0f &&
            M[1] == 0.0f && M[2] == 0.0f && M[3] == 0.0f &&
            M[4] == 0.0f && M[6] == 0.0f && M[7] == 0.0f &&
            M[8] == 0.0f && M[9] == 0.0f && M[11] == 0.0f &&
            M[0] != 0.0f && M[5] != 0.0f &&
            std::abs(M[0]) < 100.0f && std::abs(M[5]) < 100.0f) 
        {
            bool alreadyLogged = false;
            {
                std::lock_guard<std::mutex> lock(g_loggedBuffersMutex);
                if (std::find(g_loggedBufferIDs.begin(), g_loggedBufferIDs.end(), bufferID) != g_loggedBufferIDs.end()) {
                    alreadyLogged = true;
                } else {
                    g_loggedBufferIDs.push_back(bufferID);
                }
            }
            
            if (!alreadyLogged) {
                LogMessage("HytaleUIScaleHook: Found projection matrix in " + source + 
                           " (Buffer: " + std::to_string(bufferID) + 
                           ", Offset: " + std::to_string(offset + i * sizeof(float)) + 
                           "). M[0]=" + std::to_string(M[0]) + 
                           ", M[5]=" + std::to_string(M[5]) + 
                           ", M[12]=" + std::to_string(M[12]) + 
                           ", M[13]=" + std::to_string(M[13]) + 
                           ", M[15]=" + std::to_string(M[15]));
            }
            
            if (hasChanges) {
                M[0] *= scale;
                M[5] *= scale;
                M[12] += offsetX;
                M[13] += offsetY;
            }
        }
    }
}

bool ShouldModifyBatcher2DVertices() {
    constexpr bool kEnableBatcher2DVertexPatch = false;
    return kEnableBatcher2DVertexPatch &&
           g_sharedData != nullptr &&
           g_currentProgram != 0 &&
           IsBatcher2DProgram(g_currentProgram) &&
           g_sharedData->disableUboScaling != 0;
}

bool ShouldModifyUboUpload() {
    if (!g_sharedData || g_sharedData->disableUboScaling != 0) return false;
    return g_sharedData->uiScale != 1.0f ||
           g_sharedData->offsetX != 0.0f ||
           g_sharedData->offsetY != 0.0f;
}

bool PrepareBufferUploadCopy(const void* data, GLsizeiptr size) {
    if (!data || size <= 0) return false;
    constexpr size_t kMaxPatchedUploadBytes = 16u * 1024u * 1024u;
    const size_t byte_count = static_cast<size_t>(size);
    if (static_cast<GLsizeiptr>(byte_count) != size ||
        byte_count > kMaxPatchedUploadBytes) {
        return false;
    }
    try {
        g_bufferUploadScratch.resize(byte_count);
    } catch (...) {
        return false;
    }
    std::memcpy(g_bufferUploadScratch.data(), data, byte_count);
    return true;
}

GLsizeiptr BoundBufferSize(GLuint buffer) {
    if (buffer == 0 || !real_glGetNamedBufferParameteriv) return 0;
    GLint buffer_size = 0;
    real_glGetNamedBufferParameteriv(
        buffer, GL_BUFFER_SIZE_VALUE, &buffer_size);
    return buffer_size > 0 ? static_cast<GLsizeiptr>(buffer_size) : 0;
}

void ObserveBatcher2DMenuVertices(const void* data, GLsizeiptr size, const std::string& source, GLuint bufferID) {
    // Vertex uploads are too noisy for menu visibility: HUD bars and hotbar
    // uploads can look menu-like. Full menu capture is driven by large Batcher2D
    // draw counts only.
    return;

    if (g_sharedData == nullptr || data == nullptr || size < 64 || (size % 64) != 0) {
        return;
    }

    RECT rect{};
    float width = 2560.0f;
    float height = 1440.0f;
    const HWND hwnd = GetGameWindow();
    if (hwnd != NULL && real_GetClientRect && real_GetClientRect(hwnd, &rect)) {
        const float clientWidth = static_cast<float>(rect.right - rect.left);
        const float clientHeight = static_cast<float>(rect.bottom - rect.top);
        if (clientWidth > 0.0f && clientHeight > 0.0f) {
            width = clientWidth;
            height = clientHeight;
        }
    }

    constexpr GLsizeiptr kStride = 64;
    constexpr GLsizeiptr kPositionOffset = 0;
    constexpr GLsizeiptr kScissorOffset = 16;
    const auto* bytes = static_cast<const unsigned char*>(data);
    const int vertexCount = static_cast<int>(size / kStride);
    if (vertexCount < 48 || vertexCount > 200000) {
        return;
    }

    int screenVertices = 0;
    int centralMenuVertices = 0;
    int bottomHudVertices = 0;
    int centralScissorVertices = 0;
    float minX = width * 8.0f;
    float minY = height * 8.0f;
    float maxX = -width * 8.0f;
    float maxY = -height * 8.0f;

    for (int i = 0; i < vertexCount; ++i) {
        const unsigned char* vertex = bytes + static_cast<GLsizeiptr>(i) * kStride;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        memcpy(&x, vertex + kPositionOffset + 0, sizeof(float));
        memcpy(&y, vertex + kPositionOffset + 4, sizeof(float));
        memcpy(&z, vertex + kPositionOffset + 8, sizeof(float));
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || std::fabs(z) > 0.001f) {
            continue;
        }
        if (x < -width * 0.05f || x > width * 1.10f ||
            y < -height * 0.05f || y > height * 1.10f) {
            continue;
        }

        ++screenVertices;
        minX = (std::min)(minX, x);
        minY = (std::min)(minY, y);
        maxX = (std::max)(maxX, x);
        maxY = (std::max)(maxY, y);

        const bool central =
            x >= width * 0.08f && x <= width * 0.92f &&
            y >= height * 0.12f && y <= height * 0.78f;
        if (central) {
            ++centralMenuVertices;
        }
        if (y >= height * 0.76f) {
            ++bottomHudVertices;
        }

        uint16_t scissor[4]{};
        memcpy(scissor, vertex + kScissorOffset, sizeof(scissor));
        const bool hasScissor = scissor[0] != 0 || scissor[1] != 0 || scissor[2] != 0 || scissor[3] != 0;
        if (hasScissor &&
            scissor[0] < width * 1.10f && scissor[1] < height * 1.10f &&
            scissor[2] < width * 1.10f && scissor[3] < height * 1.10f &&
            scissor[1] < height * 0.78f && scissor[3] > height * 0.12f) {
            ++centralScissorVertices;
        }
    }

    if (screenVertices < 48) {
        return;
    }

    const float rangeX = maxX - minX;
    const float rangeY = maxY - minY;
    const bool mostlyBottomHud = bottomHudVertices > (screenVertices * 3) / 5;
    const bool menuLike =
        !mostlyBottomHud &&
        centralMenuVertices >= 48 &&
        centralScissorVertices >= 12 &&
        rangeX >= width * 0.08f &&
        rangeY >= height * 0.06f;

    if (!menuLike) {
        return;
    }

    InterlockedIncrement(&g_sharedData->menuVisibleCounter);
    g_menuCaptureUntilTick = GetTickCount() + 350;
    const LONG counter = InterlockedCompareExchange(&g_sharedData->menuVisibleCounter, 0, 0);
    static DWORD lastLogTick = 0;
    const DWORD now = GetTickCount();
    if (now - lastLogTick >= 500) {
        LogMessage("HytaleUIScaleHook: menu-like vertex upload observed from " + source +
                   " (Buffer: " + std::to_string(bufferID) +
                   ", Vertices: " + std::to_string(vertexCount) +
                   ", Screen: " + std::to_string(screenVertices) +
                   ", Central: " + std::to_string(centralMenuVertices) +
                   ", Scissor: " + std::to_string(centralScissorVertices) +
                   ", Bottom: " + std::to_string(bottomHudVertices) +
                   ", Range: " + std::to_string(rangeX) + "x" + std::to_string(rangeY) +
                   ", MenuCounter: " + std::to_string(counter) + ").");
        lastLogTick = now;
    }
}

void ModifyBatcher2DVertices(void* data, GLsizeiptr size, const std::string& source, GLuint bufferID, GLintptr bufferOffset) {
    static DWORD nextObservationTick = 0;
    const DWORD now = GetTickCount();
    if (g_sharedData != nullptr &&
        g_currentProgram != 0 &&
        IsBatcher2DProgram(g_currentProgram) &&
        now >= nextObservationTick) {
        nextObservationTick = now + 80;
        ObserveBatcher2DMenuVertices(data, size, source, bufferID);
    }

    if (g_sharedData != nullptr &&
        g_currentProgram != 0 &&
        IsBatcher2DProgram(g_currentProgram) &&
        g_sharedData->disableUboScaling != 0) {
        static bool loggedDisabled = false;
        if (!loggedDisabled) {
            LogMessage("HytaleUIScaleHook: Batcher2D vertex patch disabled; v75 visual behavior restored (" +
                       source + ", Buffer: " + std::to_string(bufferID) +
                       ", Offset: " + std::to_string(bufferOffset) +
                       ", Size: " + std::to_string(size) + ").");
            loggedDisabled = true;
        }
    }
    if (data == nullptr || size < 64 || !ShouldModifyBatcher2DVertices()) return;

    const float scale = g_sharedData->uiScale;
    const float offsetX = g_sharedData->offsetX;
    const float offsetY = g_sharedData->offsetY;
    if (scale == 0.0f || (scale == 1.0f && offsetX == 0.0f && offsetY == 0.0f)) return;

    constexpr GLsizeiptr kStride = 64;
    constexpr GLsizeiptr kPositionOffset = 0;
    constexpr GLsizeiptr kScissorOffset = 16;

    if ((bufferOffset % kStride) != 0 || (size % kStride) != 0) {
        static bool loggedUnaligned = false;
        if (!loggedUnaligned) {
            LogMessage("HytaleUIScaleHook: skipped unaligned Batcher2D upload from " + source +
                       " (Buffer: " + std::to_string(bufferID) +
                       ", Offset: " + std::to_string(bufferOffset) +
                       ", Size: " + std::to_string(size) + ").");
            loggedUnaligned = true;
        }
        return;
    }

    RECT rect{};
    const HWND hwnd = GetGameWindow();
    float width = 0.0f;
    float height = 0.0f;
    if (hwnd != NULL && real_GetClientRect && real_GetClientRect(hwnd, &rect)) {
        width = static_cast<float>(rect.right - rect.left);
        height = static_cast<float>(rect.bottom - rect.top);
    }
    if (width <= 0.0f || height <= 0.0f) return;

    const float pxOffsetX = offsetX * (width / 2.0f);
    const float pxOffsetY = -offsetY * (height / 2.0f);
    const int vertexCount = static_cast<int>(size / kStride);
    auto* bytes = static_cast<unsigned char*>(data);

    for (int i = 0; i < vertexCount; ++i) {
        unsigned char* vertex = bytes + static_cast<GLsizeiptr>(i) * kStride;
        auto* position = reinterpret_cast<float*>(vertex + kPositionOffset);
        if (!std::isfinite(position[0]) || !std::isfinite(position[1]) ||
            std::abs(position[0]) > width * 4.0f || std::abs(position[1]) > height * 4.0f) {
            continue;
        }

        position[0] += pxOffsetX;
        position[1] += pxOffsetY;

        auto* scissor = reinterpret_cast<uint16_t*>(vertex + kScissorOffset);
        const bool hasScissor = scissor[0] != 0 || scissor[1] != 0 || scissor[2] != 0 || scissor[3] != 0;
        if (hasScissor &&
            scissor[0] < width * 4.0f && scissor[1] < height * 4.0f &&
            scissor[2] < width * 4.0f && scissor[3] < height * 4.0f) {
            const int dx = static_cast<int>(std::lround(pxOffsetX));
            const int dy = static_cast<int>(std::lround(pxOffsetY));
            scissor[0] = static_cast<uint16_t>(std::clamp(static_cast<int>(scissor[0]) + dx, 0, 65535));
            scissor[1] = static_cast<uint16_t>(std::clamp(static_cast<int>(scissor[1]) + dy, 0, 65535));
            scissor[2] = static_cast<uint16_t>(std::clamp(static_cast<int>(scissor[2]) + dx, 0, 65535));
            scissor[3] = static_cast<uint16_t>(std::clamp(static_cast<int>(scissor[3]) + dy, 0, 65535));
        }
    }

    static bool logged = false;
    if (!logged) {
        LogMessage("HytaleUIScaleHook: modified Batcher2D vertices from " + source +
                   " (Buffer: " + std::to_string(bufferID) +
                   ", Offset: " + std::to_string(bufferOffset) +
                   ", Vertices: " + std::to_string(vertexCount) +
                   ", Scale: " + std::to_string(scale) +
                   ", PxOffset: " + std::to_string(pxOffsetX) + "/" +
                   std::to_string(pxOffsetY) + ", position+scissor offset).");
        logged = true;
    }
}

void WINAPI hook_glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data) {
    if (target == 0x8892 && data != nullptr && size >= 64 &&
        ShouldModifyBatcher2DVertices()) { // GL_ARRAY_BUFFER
        GLuint bufID = GetBoundBufferID(target);
        const void* upload_data = data;
        if (PrepareBufferUploadCopy(data, size)) {
            ModifyBatcher2DVertices(
                g_bufferUploadScratch.data(), size, "glBufferSubData", bufID,
                offset);
            upload_data = g_bufferUploadScratch.data();
        }
        real_glBufferSubData(target, offset, size, upload_data);
        NoteHeldItemBufferMutation(target, bufID);
        return;
    }
    if (target == 0x8A11 && data != nullptr && size >= 64 &&
        ShouldModifyUboUpload()) { // GL_UNIFORM_BUFFER
        GLuint bufID = GetBoundBufferID(target);
        const void* upload_data = data;
        if (PrepareBufferUploadCopy(data, size)) {
            ScanAndModifyMatrix(
                g_bufferUploadScratch.data(), size, "glBufferSubData", bufID,
                offset);
            upload_data = g_bufferUploadScratch.data();
        }
        real_glBufferSubData(target, offset, size, upload_data);
        return;
    }
    const GLuint buffer =
        (target == 0x8892 || target == 0x8893) ? GetBoundBufferID(target) : 0;
    real_glBufferSubData(target, offset, size, data);
    NoteHeldItemBufferMutation(target, buffer);
}

void WINAPI hook_glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
    if (target == 0x8892 && data != nullptr && size >= 64 &&
        ShouldModifyBatcher2DVertices()) { // GL_ARRAY_BUFFER
        GLuint bufID = GetBoundBufferID(target);
        const void* upload_data = data;
        if (PrepareBufferUploadCopy(data, size)) {
            ModifyBatcher2DVertices(
                g_bufferUploadScratch.data(), size, "glBufferData", bufID, 0);
            upload_data = g_bufferUploadScratch.data();
        }
        real_glBufferData(target, size, upload_data, usage);
        NoteHeldItemBufferMutation(target, bufID);
        return;
    }
    if (target == 0x8A11 && data != nullptr && size >= 64 &&
        ShouldModifyUboUpload()) { // GL_UNIFORM_BUFFER
        GLuint bufID = GetBoundBufferID(target);
        const void* upload_data = data;
        if (PrepareBufferUploadCopy(data, size)) {
            ScanAndModifyMatrix(
                g_bufferUploadScratch.data(), size, "glBufferData", bufID, 0);
            upload_data = g_bufferUploadScratch.data();
        }
        real_glBufferData(target, size, upload_data, usage);
        return;
    }
    const GLuint buffer =
        (target == 0x8892 || target == 0x8893) ? GetBoundBufferID(target) : 0;
    real_glBufferData(target, size, data, usage);
    NoteHeldItemBufferMutation(target, buffer);
}

void WINAPI hook_glBufferStorage(GLenum target, GLsizeiptr size, const void *data, GLbitfield flags) {
    if (target == 0x8892 && data != nullptr && size >= 64 &&
        ShouldModifyBatcher2DVertices()) { // GL_ARRAY_BUFFER
        GLuint bufID = GetBoundBufferID(target);
        const void* upload_data = data;
        if (PrepareBufferUploadCopy(data, size)) {
            ModifyBatcher2DVertices(
                g_bufferUploadScratch.data(), size, "glBufferStorage", bufID,
                0);
            upload_data = g_bufferUploadScratch.data();
        }
        real_glBufferStorage(target, size, upload_data, flags);
        NoteHeldItemBufferMutation(target, bufID);
        return;
    }
    if (target == 0x8A11 && data != nullptr && size >= 64 &&
        ShouldModifyUboUpload()) { // GL_UNIFORM_BUFFER
        GLuint bufID = GetBoundBufferID(target);
        const void* upload_data = data;
        if (PrepareBufferUploadCopy(data, size)) {
            ScanAndModifyMatrix(
                g_bufferUploadScratch.data(), size, "glBufferStorage", bufID,
                0);
            upload_data = g_bufferUploadScratch.data();
        }
        real_glBufferStorage(target, size, upload_data, flags);
        return;
    }
    const GLuint buffer =
        (target == 0x8892 || target == 0x8893) ? GetBoundBufferID(target) : 0;
    real_glBufferStorage(target, size, data, flags);
    NoteHeldItemBufferMutation(target, buffer);
}

void* WINAPI hook_glMapBuffer(GLenum target, GLenum access) {
    void* result = real_glMapBuffer(target, access);
    if (result != nullptr && target == 0x8A11) { // GL_UNIFORM_BUFFER
        GLuint bufID = GetBoundBufferID(target);
        const bool readable =
            access == GL_READ_ONLY_VALUE || access == GL_READ_WRITE_VALUE;
        const bool writable = access == GL_READ_WRITE_VALUE;
        const GLsizeiptr length = BoundBufferSize(bufID);
        if (!readable || !writable || length < 64) return result;
        MappedBufferState state = {
            bufID, result, 0, length, readable, writable};

        bool found = false;
        for (auto& b : g_mappedBuffers) {
            if (b.bufferID == bufID) {
                b = state;
                found = true;
                break;
            }
        }
        if (!found) {
            g_mappedBuffers.push_back(state);
        }
    }
    return result;
}

void* WINAPI hook_glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
    void* result = real_glMapBufferRange(target, offset, length, access);
    if (result != nullptr && target == 0x8A11) { // GL_UNIFORM_BUFFER
        GLuint bufID = GetBoundBufferID(target);
        const bool readable = (access & GL_MAP_READ_BIT_VALUE) != 0;
        const bool writable = (access & GL_MAP_WRITE_BIT_VALUE) != 0;
        const GLsizeiptr buffer_size = BoundBufferSize(bufID);
        if (!readable || !writable || length < 64 || offset < 0 ||
            buffer_size <= 0 || offset > buffer_size ||
            length > buffer_size - offset) {
            return result;
        }
        MappedBufferState state = {
            bufID, result, offset, length, readable, writable};

        bool found = false;
        for (auto& b : g_mappedBuffers) {
            if (b.bufferID == bufID) {
                b = state;
                found = true;
                break;
            }
        }
        if (!found) {
            g_mappedBuffers.push_back(state);
        }
    }
    return result;
}

GLboolean WINAPI hook_glUnmapBuffer(GLenum target) {
    const GLuint mutated_buffer =
        (target == 0x8892 || target == 0x8893) ? GetBoundBufferID(target) : 0;
    if (target == 0x8A11) { // GL_UNIFORM_BUFFER
        GLuint bufID = GetBoundBufferID(target);
        for (auto it = g_mappedBuffers.begin(); it != g_mappedBuffers.end(); ++it) {
            if (it->bufferID == bufID) {
                if (it->readable && it->writable && it->length >= 64) {
                    ScanAndModifyMatrix(
                        it->ptr, it->length, "glUnmapBuffer", it->bufferID,
                        it->offset);
                }
                g_mappedBuffers.erase(it);
                break;
            }
        }
    }
    const GLboolean result = real_glUnmapBuffer(target);
    if (result) NoteHeldItemBufferMutation(target, mutated_buffer);
    return result;
}

void WINAPI hook_glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    const int initialized =
        static_cast<int>(HookInitializationState::Initialized);
    if (g_hookInitializationState.load(std::memory_order_acquire) != initialized &&
        GetTickCount64() >=
            g_nextHookInitializationRetryTick.load(std::memory_order_relaxed)) {
        InitializeHooks();
    }
    HookWndProc();
    real_glViewport(x, y, width, height);
}

// Hook Initialization
void InitializeHooks() {
    const int initialized =
        static_cast<int>(HookInitializationState::Initialized);
    if (g_hookInitializationState.load(std::memory_order_acquire) == initialized) {
        return;
    }
    int expected = static_cast<int>(HookInitializationState::Uninitialized);
    if (!g_hookInitializationState.compare_exchange_strong(
            expected, static_cast<int>(HookInitializationState::Initializing),
            std::memory_order_acq_rel)) {
        return;
    }

    HGLRC currentContext = wglGetCurrentContext();
    if (currentContext == NULL) {
        g_hookInitializationState.store(
            static_cast<int>(HookInitializationState::Uninitialized),
            std::memory_order_release);
        return; // No active context yet in this thread
    }
    
    LogMessage("HytaleUIScaleHook: Active OpenGL context detected in thread. Resolving functions...");
    
    real_glObjectLabel = (glObjectLabel_t)wglGetProcAddress("glObjectLabel");
    real_glLinkProgram = (glLinkProgram_t)wglGetProcAddress("glLinkProgram");
    real_glUseProgram = (glUseProgram_t)wglGetProcAddress("glUseProgram");
    real_glDrawArrays = (glDrawArrays_t)wglGetProcAddress("glDrawArrays");
    real_glDrawElements = (glDrawElements_t)wglGetProcAddress("glDrawElements");
    real_glUniformMatrix4fv = (glUniformMatrix4fv_t)wglGetProcAddress("glUniformMatrix4fv");
    real_glProgramUniformMatrix4fv = (glProgramUniformMatrix4fv_t)wglGetProcAddress("glProgramUniformMatrix4fv");
    real_glGetUniformLocation = (glGetUniformLocation_t)wglGetProcAddress("glGetUniformLocation");
    real_glGetUniformiv = (glGetUniformiv_t)wglGetProcAddress("glGetUniformiv");
    real_glGetUniformfv = (glGetUniformfv_t)wglGetProcAddress("glGetUniformfv");
    real_glGetAttribLocation = (glGetAttribLocation_t)wglGetProcAddress("glGetAttribLocation");
    real_glGetVertexAttribiv = (glGetVertexAttribiv_t)wglGetProcAddress("glGetVertexAttribiv");
    real_glGetVertexAttribPointerv = (glGetVertexAttribPointerv_t)wglGetProcAddress("glGetVertexAttribPointerv");
    real_glGetNamedBufferSubData = (glGetNamedBufferSubData_t)wglGetProcAddress("glGetNamedBufferSubData");
    real_glGetNamedBufferParameteriv = (glGetNamedBufferParameteriv_t)wglGetProcAddress("glGetNamedBufferParameteriv");
    real_glIsProgram = (glIsProgram_t)wglGetProcAddress("glIsProgram");
    real_glGetObjectLabel = (glGetObjectLabel_t)wglGetProcAddress("glGetObjectLabel");
    real_glGetTexLevelParameteriv = (glGetTexLevelParameteriv_t)wglGetProcAddress("glGetTexLevelParameteriv");
    real_glActiveTexture = (glActiveTexture_t)wglGetProcAddress("glActiveTexture");
    real_glBindTexture = (glBindTexture_t)wglGetProcAddress("glBindTexture");
    real_glGenTextures = (glGenTextures_t)wglGetProcAddress("glGenTextures");
    real_glTexImage2D = (glTexImage2D_t)wglGetProcAddress("glTexImage2D");
    real_glTexParameteri = (glTexParameteri_t)wglGetProcAddress("glTexParameteri");
    real_glClearColor = (glClearColor_t)wglGetProcAddress("glClearColor");
    real_glClear = (glClear_t)wglGetProcAddress("glClear");
    real_glBindFramebuffer = (glBindFramebuffer_t)wglGetProcAddress("glBindFramebuffer");
    real_glGenFramebuffers = (glGenFramebuffers_t)wglGetProcAddress("glGenFramebuffers");
    real_glFramebufferTexture2D = (glFramebufferTexture2D_t)wglGetProcAddress("glFramebufferTexture2D");
    real_glCheckFramebufferStatus = (glCheckFramebufferStatus_t)wglGetProcAddress("glCheckFramebufferStatus");
    HMODULE hGL = GetModuleHandleA("opengl32.dll");
    if (hGL) {
        raw_glDrawBuffer = reinterpret_cast<glDrawBuffer_t>(GetProcAddress(hGL, "glDrawBuffer"));
        raw_glReadBuffer = reinterpret_cast<glReadBuffer_t>(GetProcAddress(hGL, "glReadBuffer"));
        raw_glDepthMask = reinterpret_cast<glDepthMask_t>(GetProcAddress(hGL, "glDepthMask"));
        raw_glColorMask = reinterpret_cast<glColorMask_t>(GetProcAddress(hGL, "glColorMask"));
        raw_glEnable = reinterpret_cast<glEnable_t>(GetProcAddress(hGL, "glEnable"));
        raw_glDisable = reinterpret_cast<glDisable_t>(GetProcAddress(hGL, "glDisable"));
        raw_glClearDepth = reinterpret_cast<glClearDepth_t>(GetProcAddress(hGL, "glClearDepth"));
        raw_glDepthFunc = reinterpret_cast<glDepthFunc_t>(GetProcAddress(hGL, "glDepthFunc"));
        raw_glGetBooleanv = reinterpret_cast<glGetBooleanv_t>(GetProcAddress(hGL, "glGetBooleanv"));
        raw_glIsEnabled = reinterpret_cast<glIsEnabled_t>(GetProcAddress(hGL, "glIsEnabled"));
    }
    if (!real_glDrawArrays && hGL) {
        real_glDrawArrays = (glDrawArrays_t)GetProcAddress(hGL, "glDrawArrays");
    }
    if (!real_glDrawElements && hGL) {
        real_glDrawElements = (glDrawElements_t)GetProcAddress(hGL, "glDrawElements");
    }
    real_glGetIntegerv = hGL ? (glGetIntegerv_t)GetProcAddress(hGL, "glGetIntegerv") : nullptr;
    real_glGetFloatv = hGL ? (glGetFloatv_t)GetProcAddress(hGL, "glGetFloatv") : nullptr;
    if (!real_glGetTexLevelParameteriv && hGL) {
        real_glGetTexLevelParameteriv = (glGetTexLevelParameteriv_t)GetProcAddress(hGL, "glGetTexLevelParameteriv");
    }
    if (!real_glClearColor && hGL) {
        real_glClearColor = (glClearColor_t)GetProcAddress(hGL, "glClearColor");
    }
    if (!real_glClear && hGL) {
        real_glClear = (glClear_t)GetProcAddress(hGL, "glClear");
    }
    if (!real_glBindTexture && hGL) {
        real_glBindTexture = (glBindTexture_t)GetProcAddress(hGL, "glBindTexture");
    }
    if (!real_glGenTextures && hGL) {
        real_glGenTextures = (glGenTextures_t)GetProcAddress(hGL, "glGenTextures");
    }
    if (!real_glTexImage2D && hGL) {
        real_glTexImage2D = (glTexImage2D_t)GetProcAddress(hGL, "glTexImage2D");
    }
    if (!real_glTexParameteri && hGL) {
        real_glTexParameteri = (glTexParameteri_t)GetProcAddress(hGL, "glTexParameteri");
    }
    if (!real_glBindFramebuffer && hGL) {
        real_glBindFramebuffer = (glBindFramebuffer_t)GetProcAddress(hGL, "glBindFramebuffer");
    }
    if (!real_glGenFramebuffers && hGL) {
        real_glGenFramebuffers = (glGenFramebuffers_t)GetProcAddress(hGL, "glGenFramebuffers");
    }
    if (!real_glFramebufferTexture2D && hGL) {
        real_glFramebufferTexture2D = (glFramebufferTexture2D_t)GetProcAddress(hGL, "glFramebufferTexture2D");
    }
    if (!real_glCheckFramebufferStatus && hGL) {
        real_glCheckFramebufferStatus = (glCheckFramebufferStatus_t)GetProcAddress(hGL, "glCheckFramebufferStatus");
    }
    
    // Resolve UBO/Buffer functions
    real_glBindBuffer = (glBindBuffer_t)wglGetProcAddress("glBindBuffer");
    real_glBindBufferRange = (glBindBufferRange_t)wglGetProcAddress("glBindBufferRange");
    real_glBindBufferBase = (glBindBufferBase_t)wglGetProcAddress("glBindBufferBase");
    real_glBufferSubData = (glBufferSubData_t)wglGetProcAddress("glBufferSubData");
    real_glBufferData = (glBufferData_t)wglGetProcAddress("glBufferData");
    real_glMapBuffer = (glMapBuffer_t)wglGetProcAddress("glMapBuffer");
    real_glMapBufferRange = (glMapBufferRange_t)wglGetProcAddress("glMapBufferRange");
    real_glUnmapBuffer = (glUnmapBuffer_t)wglGetProcAddress("glUnmapBuffer");
    real_glBufferStorage = (glBufferStorage_t)wglGetProcAddress("glBufferStorage");
    
    LogMessage("HytaleUIScaleHook: Resolving glObjectLabel: " + std::string(real_glObjectLabel ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glLinkProgram: " + std::string(real_glLinkProgram ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glUseProgram: " + std::string(real_glUseProgram ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glDrawArrays: " + std::string(real_glDrawArrays ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glDrawElements: " + std::string(real_glDrawElements ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glUniformMatrix4fv: " + std::string(real_glUniformMatrix4fv ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glProgramUniformMatrix4fv: " + std::string(real_glProgramUniformMatrix4fv ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glGetUniformLocation: " + std::string(real_glGetUniformLocation ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glGetUniformiv: " + std::string(real_glGetUniformiv ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glGetUniformfv: " + std::string(real_glGetUniformfv ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glGetAttribLocation: " + std::string(real_glGetAttribLocation ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glGetVertexAttribiv: " + std::string(real_glGetVertexAttribiv ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glGetVertexAttribPointerv: " + std::string(real_glGetVertexAttribPointerv ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glGetNamedBufferSubData: " + std::string(real_glGetNamedBufferSubData ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glGetNamedBufferParameteriv: " + std::string(real_glGetNamedBufferParameteriv ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glIsProgram: " + std::string(real_glIsProgram ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glGetObjectLabel: " + std::string(real_glGetObjectLabel ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glGetIntegerv: " + std::string(real_glGetIntegerv ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glGetFloatv: " + std::string(real_glGetFloatv ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glGetTexLevelParameteriv: " + std::string(real_glGetTexLevelParameteriv ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glActiveTexture: " + std::string(real_glActiveTexture ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glBindTexture: " + std::string(real_glBindTexture ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glGenTextures: " + std::string(real_glGenTextures ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glTexImage2D: " + std::string(real_glTexImage2D ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glTexParameteri: " + std::string(real_glTexParameteri ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glClearColor: " + std::string(real_glClearColor ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glClear: " + std::string(real_glClear ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glBindFramebuffer: " + std::string(real_glBindFramebuffer ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glGenFramebuffers: " + std::string(real_glGenFramebuffers ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glFramebufferTexture2D: " + std::string(real_glFramebufferTexture2D ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glCheckFramebufferStatus: " + std::string(real_glCheckFramebufferStatus ? "SUCCESS" : "FAILED"));
    
    LogMessage("HytaleUIScaleHook: Resolving glBindBuffer: " + std::string(real_glBindBuffer ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glBindBufferRange: " + std::string(real_glBindBufferRange ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glBindBufferBase: " + std::string(real_glBindBufferBase ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glBufferSubData: " + std::string(real_glBufferSubData ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glBufferData: " + std::string(real_glBufferData ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glMapBuffer: " + std::string(real_glMapBuffer ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glMapBufferRange: " + std::string(real_glMapBufferRange ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glUnmapBuffer: " + std::string(real_glUnmapBuffer ? "SUCCESS" : "FAILED"));
    LogMessage("HytaleUIScaleHook: Resolving glBufferStorage: " + std::string(real_glBufferStorage ? "SUCCESS" : "FAILED"));

    const bool required_functions_ready =
        real_glUseProgram && real_glDrawArrays && real_glDrawElements &&
        real_glUniformMatrix4fv && real_glGetUniformLocation &&
        real_glGetIntegerv;
    if (!required_functions_ready) {
        LogMessage(
            "HytaleUIScaleHook: required OpenGL functions are incomplete; "
            "initialization will retry.");
        g_nextHookInitializationRetryTick.store(
            GetTickCount64() + 1000, std::memory_order_relaxed);
        g_hookInitializationState.store(
            static_cast<int>(HookInitializationState::Uninitialized),
            std::memory_order_release);
        return;
    }

    MH_STATUS status;
    bool hook_creation_ok = true;
    std::vector<LPVOID> created_hook_targets;
    const auto note_hook_status =
        [&](MH_STATUS hook_status, const char* hook_name) {
            const bool ok = hook_status == MH_OK ||
                            hook_status == MH_ERROR_ALREADY_CREATED;
            if (!ok) {
                hook_creation_ok = false;
                LogMessage(
                    std::string("HytaleUIScaleHook: required hook failed: ") +
                    hook_name + " status=" + MH_StatusToString(hook_status));
            }
        };
    const auto create_hook =
        [&](LPVOID target, LPVOID detour, LPVOID* original,
            const char* hook_name) {
            if (!target) return;
            const MH_STATUS hook_status =
                MH_CreateHook(target, detour, original);
            if (hook_status == MH_OK) {
                created_hook_targets.push_back(target);
            }
            LogMessage(
                std::string("HytaleUIScaleHook: Create ") + hook_name +
                " hook status: " + MH_StatusToString(hook_status));
            note_hook_status(hook_status, hook_name);
        };

    create_hook((LPVOID)real_glObjectLabel, (LPVOID)hook_glObjectLabel,
                (LPVOID*)&real_glObjectLabel, "glObjectLabel");
    create_hook((LPVOID)real_glLinkProgram, (LPVOID)hook_glLinkProgram,
                (LPVOID*)&real_glLinkProgram, "glLinkProgram");
    create_hook((LPVOID)real_glUseProgram, (LPVOID)hook_glUseProgram,
                (LPVOID*)&real_glUseProgram, "glUseProgram");
    create_hook((LPVOID)real_glBindTexture, (LPVOID)hook_glBindTexture,
                (LPVOID*)&real_glBindTexture, "glBindTexture");
    create_hook((LPVOID)real_glTexImage2D, (LPVOID)hook_glTexImage2D,
                (LPVOID*)&real_glTexImage2D, "glTexImage2D");
    create_hook((LPVOID)real_glDrawArrays, (LPVOID)hook_glDrawArrays,
                (LPVOID*)&real_glDrawArrays, "glDrawArrays");
    create_hook((LPVOID)real_glDrawElements, (LPVOID)hook_glDrawElements,
                (LPVOID*)&real_glDrawElements, "glDrawElements");
    create_hook((LPVOID)real_glUniformMatrix4fv,
                (LPVOID)hook_glUniformMatrix4fv,
                (LPVOID*)&real_glUniformMatrix4fv, "glUniformMatrix4fv");
    create_hook((LPVOID)real_glProgramUniformMatrix4fv,
                (LPVOID)hook_glProgramUniformMatrix4fv,
                (LPVOID*)&real_glProgramUniformMatrix4fv,
                "glProgramUniformMatrix4fv");
    create_hook((LPVOID)real_glBindBuffer, (LPVOID)hook_glBindBuffer,
                (LPVOID*)&real_glBindBuffer, "glBindBuffer");
    create_hook((LPVOID)real_glBindBufferRange,
                (LPVOID)hook_glBindBufferRange,
                (LPVOID*)&real_glBindBufferRange, "glBindBufferRange");
    create_hook((LPVOID)real_glBindBufferBase,
                (LPVOID)hook_glBindBufferBase,
                (LPVOID*)&real_glBindBufferBase, "glBindBufferBase");
    create_hook((LPVOID)real_glBufferSubData,
                (LPVOID)hook_glBufferSubData,
                (LPVOID*)&real_glBufferSubData, "glBufferSubData");
    create_hook((LPVOID)real_glBufferData, (LPVOID)hook_glBufferData,
                (LPVOID*)&real_glBufferData, "glBufferData");
    create_hook((LPVOID)real_glBufferStorage, (LPVOID)hook_glBufferStorage,
                (LPVOID*)&real_glBufferStorage, "glBufferStorage");
    create_hook((LPVOID)real_glMapBuffer, (LPVOID)hook_glMapBuffer,
                (LPVOID*)&real_glMapBuffer, "glMapBuffer");
    create_hook((LPVOID)real_glMapBufferRange,
                (LPVOID)hook_glMapBufferRange,
                (LPVOID*)&real_glMapBufferRange, "glMapBufferRange");
    create_hook((LPVOID)real_glUnmapBuffer, (LPVOID)hook_glUnmapBuffer,
                (LPVOID*)&real_glUnmapBuffer, "glUnmapBuffer");

    if (!hook_creation_ok) {
        for (LPVOID target : created_hook_targets) {
            MH_RemoveHook(target);
        }
        g_nextHookInitializationRetryTick.store(
            GetTickCount64() + 1000, std::memory_order_relaxed);
        g_hookInitializationState.store(
            static_cast<int>(HookInitializationState::Uninitialized),
            std::memory_order_release);
        return;
    }

    status = MH_EnableHook(MH_ALL_HOOKS);
    LogMessage("HytaleUIScaleHook: MH_EnableHook status: " + std::string(MH_StatusToString(status)));
    if (status != MH_OK && status != MH_ERROR_ENABLED) {
        for (LPVOID target : created_hook_targets) {
            MH_DisableHook(target);
            MH_RemoveHook(target);
        }
        g_nextHookInitializationRetryTick.store(
            GetTickCount64() + 1000, std::memory_order_relaxed);
        g_hookInitializationState.store(
            static_cast<int>(HookInitializationState::Uninitialized),
            std::memory_order_release);
        return;
    }

    ScanExistingPrograms();

    g_hookInitializationState.store(
        static_cast<int>(HookInitializationState::Initialized),
        std::memory_order_release);
}

DWORD WINAPI InitializeUiHookWorker(LPVOID) {
    if (DebugLoggingEnabled()) {
        std::ofstream logFile(DebugLogPath(), std::ios_base::trunc);
        if (logFile.is_open()) {
            logFile << "=== HytaleUIScaleHook Log Started ===" << std::endl;
        }
    }
    LogMessage("HytaleUIScaleHook: initialization worker started.");
    InitializeSharedMemory();

    const MH_STATUS initialize_status = MH_Initialize();
    LogMessage(
        "HytaleUIScaleHook: MinHook MH_Initialize status: " +
        std::string(MH_StatusToString(initialize_status)));
    if (initialize_status != MH_OK &&
        initialize_status != MH_ERROR_ALREADY_INITIALIZED) {
        return 0;
    }

    const auto create_and_enable =
        [](LPVOID target, LPVOID detour, LPVOID* original,
           const char* hook_name) {
            if (!target) return false;
            MH_STATUS status = MH_CreateHook(target, detour, original);
            LogMessage(
                std::string("HytaleUIScaleHook: Create ") + hook_name +
                " status: " + MH_StatusToString(status));
            if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED) {
                return false;
            }
            status = MH_EnableHook(target);
            LogMessage(
                std::string("HytaleUIScaleHook: Enable ") + hook_name +
                " status: " + MH_StatusToString(status));
            return status == MH_OK || status == MH_ERROR_ENABLED;
        };

    HMODULE hOpenGL = GetModuleHandleA("opengl32.dll");
    if (hOpenGL) {
        real_glViewport =
            (glViewport_t)GetProcAddress(hOpenGL, "glViewport");
        if (!create_and_enable(
                (LPVOID)real_glViewport, (LPVOID)hook_glViewport,
                (LPVOID*)&real_glViewport, "glViewport hook")) {
            LogMessage(
                "HytaleUIScaleHook: bootstrap glViewport hook unavailable.");
        }
    } else {
        LogMessage(
            "HytaleUIScaleHook: GetModuleHandle for opengl32.dll failed.");
    }

    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    if (hUser32) {
        real_GetCursorPos =
            (GetCursorPos_t)GetProcAddress(hUser32, "GetCursorPos");
        real_GetClientRect =
            (GetClientRect_t)GetProcAddress(hUser32, "GetClientRect");
        real_SetCursorPos =
            (SetCursorPos_t)GetProcAddress(hUser32, "SetCursorPos");
        create_and_enable(
            (LPVOID)real_GetCursorPos, (LPVOID)hook_GetCursorPos,
            (LPVOID*)&real_GetCursorPos, "GetCursorPos hook");
        create_and_enable(
            (LPVOID)real_SetCursorPos, (LPVOID)hook_SetCursorPos,
            (LPVOID*)&real_SetCursorPos, "SetCursorPos hook");
    }

    // This succeeds only if the worker happens to own an OpenGL context.
    // Otherwise the first hooked glViewport call completes initialization.
    InitializeHooks();
    return 0;
}

// Keep DllMain loader-lock safe. The UI hook remains loaded for the lifetime of
// Hytale, so process teardown lets Windows reclaim MinHook and OpenGL resources.
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        HANDLE thread = CreateThread(
            nullptr, 0, InitializeUiHookWorker, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
