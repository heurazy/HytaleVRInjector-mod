#include <windows.h>
#include <tlhelp32.h>

#include <array>
#include <cstring>

#include "noesis_probe_shared.h"

namespace {

using NoesisRenderFn = void(__fastcall*)(void*);
using NoesisAnyFn = void(__fastcall*)(void*, void*, void*, void*);

HMODULE g_module = nullptr;
HANDLE g_mapping = nullptr;
NoesisProbeShared* g_shared = nullptr;
unsigned char* g_begin_target = nullptr;
unsigned char* g_end_target = nullptr;
unsigned char* g_begin_offscreen_target = nullptr;
unsigned char* g_end_offscreen_target = nullptr;
unsigned char* g_renderer_render_target = nullptr;
unsigned char* g_renderer_offscreen_target = nullptr;
unsigned char* g_update_render_tree_target = nullptr;
unsigned char* g_draw_batch_target = nullptr;
unsigned char* g_set_render_target_target = nullptr;
void* g_begin_memory = nullptr;
void* g_end_memory = nullptr;
void* g_begin_offscreen_memory = nullptr;
void* g_end_offscreen_memory = nullptr;
void* g_renderer_render_memory = nullptr;
void* g_renderer_offscreen_memory = nullptr;
void* g_update_render_tree_memory = nullptr;
void* g_draw_batch_memory = nullptr;
void* g_set_render_target_memory = nullptr;
std::array<unsigned char, 12> g_begin_original{};
std::array<unsigned char, 12> g_end_original{};
std::array<unsigned char, 16> g_begin_offscreen_original{};
std::array<unsigned char, 16> g_end_offscreen_original{};
std::array<unsigned char, 16> g_renderer_render_original{};
std::array<unsigned char, 16> g_renderer_offscreen_original{};
std::array<unsigned char, 16> g_update_render_tree_original{};
std::array<unsigned char, 16> g_draw_batch_original{};
std::array<unsigned char, 16> g_set_render_target_original{};
NoesisRenderFn g_begin_trampoline = nullptr;
NoesisRenderFn g_end_trampoline = nullptr;
NoesisAnyFn g_begin_offscreen_trampoline = nullptr;
NoesisAnyFn g_end_offscreen_trampoline = nullptr;
NoesisAnyFn g_renderer_render_trampoline = nullptr;
NoesisAnyFn g_renderer_offscreen_trampoline = nullptr;
NoesisAnyFn g_update_render_tree_trampoline = nullptr;
NoesisAnyFn g_draw_batch_trampoline = nullptr;
NoesisAnyFn g_set_render_target_trampoline = nullptr;

class SuspendedProcessThreads {
public:
    SuspendedProcessThreads() {
        const DWORD current_process = GetCurrentProcessId();
        const DWORD current_thread = GetCurrentThreadId();
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return;
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        if (Thread32First(snapshot, &entry)) {
            do {
                if (entry.th32OwnerProcessID != current_process ||
                    entry.th32ThreadID == current_thread ||
                    thread_count_ >= threads_.size()) {
                    continue;
                }
                HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID);
                if (thread && SuspendThread(thread) != DWORD(-1)) {
                    threads_[thread_count_++] = thread;
                } else if (thread) {
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

private:
    std::array<HANDLE, 256> threads_{};
    size_t thread_count_ = 0;
};

void emit8(unsigned char*& cursor, uint64_t value) {
    std::memcpy(cursor, &value, sizeof(value));
    cursor += sizeof(value);
}

void __fastcall hook_begin(void* device) {
    if (g_begin_trampoline) g_begin_trampoline(device);
    if (g_shared) InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->begin_calls));
}

void __fastcall hook_end(void* device) {
    if (g_end_trampoline) g_end_trampoline(device);
    if (g_shared) InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->end_calls));
}

void __fastcall hook_begin_offscreen(void* a, void* b, void* c, void* d) {
    if (g_begin_offscreen_trampoline) g_begin_offscreen_trampoline(a, b, c, d);
    if (g_shared) InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->begin_offscreen_calls));
}

void __fastcall hook_end_offscreen(void* a, void* b, void* c, void* d) {
    if (g_end_offscreen_trampoline) g_end_offscreen_trampoline(a, b, c, d);
    if (g_shared) InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->end_offscreen_calls));
}

void __fastcall hook_renderer_render(void* a, void* b, void* c, void* d) {
    if (g_shared) InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->renderer_render_calls));
    if (g_renderer_render_trampoline) g_renderer_render_trampoline(a, b, c, d);
}

void __fastcall hook_renderer_offscreen(void* a, void* b, void* c, void* d) {
    if (g_shared) InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->renderer_offscreen_calls));
    if (g_renderer_offscreen_trampoline) g_renderer_offscreen_trampoline(a, b, c, d);
}

void __fastcall hook_update_render_tree(void* a, void* b, void* c, void* d) {
    if (g_shared) InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->update_render_tree_calls));
    if (g_update_render_tree_trampoline) g_update_render_tree_trampoline(a, b, c, d);
}

void __fastcall hook_draw_batch(void* a, void* b, void* c, void* d) {
    if (g_shared) InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->draw_batch_calls));
    if (g_draw_batch_trampoline) g_draw_batch_trampoline(a, b, c, d);
}

void __fastcall hook_set_render_target(void* a, void* b, void* c, void* d) {
    if (g_shared) InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shared->set_render_target_calls));
    if (g_set_render_target_trampoline) g_set_render_target_trampoline(a, b, c, d);
}

bool install_export_hook(unsigned char* target, std::array<unsigned char, 12>& original,
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

bool install_any_hook(unsigned char* target, std::array<unsigned char, 16>& original,
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

bool restore_hook(unsigned char*& target, const std::array<unsigned char, 12>& original) {
    if (!target) return true;
    DWORD old_protect = 0;
    SuspendedProcessThreads suspended_threads;
    if (!VirtualProtect(target, original.size(), PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }
    std::memcpy(target, original.data(), original.size());
    DWORD unused = 0;
    VirtualProtect(target, original.size(), old_protect, &unused);
    FlushInstructionCache(GetCurrentProcess(), target, original.size());
    target = nullptr;
    return true;
}

bool restore_any_hook(unsigned char*& target, const std::array<unsigned char, 16>& original) {
    if (!target) return true;
    DWORD old_protect = 0;
    SuspendedProcessThreads suspended_threads;
    if (!VirtualProtect(target, original.size(), PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }
    std::memcpy(target, original.data(), original.size());
    DWORD unused = 0;
    VirtualProtect(target, original.size(), old_protect, &unused);
    FlushInstructionCache(GetCurrentProcess(), target, original.size());
    target = nullptr;
    return true;
}

bool install_hooks() {
    HMODULE noesis = GetModuleHandleW(L"Noesis.dll");
    if (!noesis) return false;
    g_begin_target = reinterpret_cast<unsigned char*>(
        GetProcAddress(noesis, "Noesis_RenderDevice_BeginOnscreenRender"));
    g_end_target = reinterpret_cast<unsigned char*>(
        GetProcAddress(noesis, "Noesis_RenderDevice_EndOnscreenRender"));
    g_begin_offscreen_target = reinterpret_cast<unsigned char*>(
        GetProcAddress(noesis, "Noesis_RenderDevice_BeginOffscreenRender"));
    g_end_offscreen_target = reinterpret_cast<unsigned char*>(
        GetProcAddress(noesis, "Noesis_RenderDevice_EndOffscreenRender"));
    g_renderer_render_target = reinterpret_cast<unsigned char*>(
        GetProcAddress(noesis, "Noesis_Renderer_Render"));
    g_renderer_offscreen_target = reinterpret_cast<unsigned char*>(
        GetProcAddress(noesis, "Noesis_Renderer_RenderOffscreen"));
    g_update_render_tree_target = reinterpret_cast<unsigned char*>(
        GetProcAddress(noesis, "Noesis_Renderer_UpdateRenderTree"));
    g_draw_batch_target = reinterpret_cast<unsigned char*>(
        GetProcAddress(noesis, "Noesis_RenderDevice_DrawBatch"));
    g_set_render_target_target = reinterpret_cast<unsigned char*>(
        GetProcAddress(noesis, "Noesis_RenderDevice_SetRenderTarget"));
    if (!g_begin_target || !g_end_target) return false;
    if (!install_export_hook(g_begin_target, g_begin_original, g_begin_memory,
                             g_begin_trampoline, reinterpret_cast<void*>(&hook_begin), 7)) {
        g_begin_target = nullptr;
        return false;
    }
    if (!install_export_hook(g_end_target, g_end_original, g_end_memory,
                             g_end_trampoline, reinterpret_cast<void*>(&hook_end), 10)) {
        restore_hook(g_begin_target, g_begin_original);
        return false;
    }
    install_any_hook(g_begin_offscreen_target, g_begin_offscreen_original,
                     g_begin_offscreen_memory, g_begin_offscreen_trampoline,
                     reinterpret_cast<void*>(&hook_begin_offscreen), 12);
    install_any_hook(g_end_offscreen_target, g_end_offscreen_original,
                     g_end_offscreen_memory, g_end_offscreen_trampoline,
                     reinterpret_cast<void*>(&hook_end_offscreen), 12);
    install_any_hook(g_renderer_render_target, g_renderer_render_original,
                     g_renderer_render_memory, g_renderer_render_trampoline,
                     reinterpret_cast<void*>(&hook_renderer_render), 15);
    install_any_hook(g_renderer_offscreen_target, g_renderer_offscreen_original,
                     g_renderer_offscreen_memory, g_renderer_offscreen_trampoline,
                     reinterpret_cast<void*>(&hook_renderer_offscreen), 12);
    install_any_hook(g_update_render_tree_target, g_update_render_tree_original,
                     g_update_render_tree_memory, g_update_render_tree_trampoline,
                     reinterpret_cast<void*>(&hook_update_render_tree), 12);
    install_any_hook(g_draw_batch_target, g_draw_batch_original,
                     g_draw_batch_memory, g_draw_batch_trampoline,
                     reinterpret_cast<void*>(&hook_draw_batch), 12);
    install_any_hook(g_set_render_target_target, g_set_render_target_original,
                     g_set_render_target_memory, g_set_render_target_trampoline,
                     reinterpret_cast<void*>(&hook_set_render_target), 12);
    return true;
}

DWORD WINAPI worker(void*) {
    g_mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, kNoesisProbeMappingName);
    if (!g_mapping) return 0;
    g_shared = static_cast<NoesisProbeShared*>(
        MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(NoesisProbeShared)));
    if (!g_shared || g_shared->magic != kNoesisProbeMagic ||
        g_shared->version != kNoesisProbeVersion) {
        return 0;
    }
    for (int i = 0; i < 200 && !GetModuleHandleW(L"Noesis.dll"); ++i) Sleep(10);
    if (!install_hooks()) {
        g_shared->hook_error = 1;
        return 0;
    }
    g_shared->hook_active = 1;
    g_shared->hook_error = 0;
    while (!g_shared->unload_requested) Sleep(20);
    restore_any_hook(g_set_render_target_target, g_set_render_target_original);
    restore_any_hook(g_draw_batch_target, g_draw_batch_original);
    restore_any_hook(g_update_render_tree_target, g_update_render_tree_original);
    restore_any_hook(g_renderer_offscreen_target, g_renderer_offscreen_original);
    restore_any_hook(g_renderer_render_target, g_renderer_render_original);
    restore_any_hook(g_end_offscreen_target, g_end_offscreen_original);
    restore_any_hook(g_begin_offscreen_target, g_begin_offscreen_original);
    const bool ok = restore_hook(g_end_target, g_end_original) &&
                    restore_hook(g_begin_target, g_begin_original);
    g_shared->hook_active = ok ? 0u : 1u;
    g_shared->hook_error = ok ? 0 : 2;
    return 0;
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
