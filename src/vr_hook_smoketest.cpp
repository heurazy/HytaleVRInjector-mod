#include <windows.h>
#include <tlhelp32.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <string>

#include "vr_camera_shared.h"

namespace {

constexpr uintptr_t kCameraHookOffset = 0x5EC7F3;
constexpr uintptr_t kInteractionHookOffset = 0x4BF5F1;
constexpr std::array<unsigned char, 16> kExpectedCameraBytes{
    0x0F, 0x28, 0xB4, 0x24, 0x80, 0x03, 0x00, 0x00,
    0x0F, 0x28, 0xBC, 0x24, 0x70, 0x03, 0x00, 0x00,
};
constexpr std::array<unsigned char, 15> kExpectedInteractionBytes{
    0x0F, 0x29, 0xBD, 0x20, 0xFE, 0xFF, 0xFF,
    0x44, 0x0F, 0x29, 0x85, 0x10, 0xFE, 0xFF, 0xFF,
};

struct ModuleInfo {
    uintptr_t base = 0;
    std::wstring path;
};

DWORD find_hytale() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return {};
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD result = 0;
    FILETIME newest{};
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"HytaleClient.exe") != 0) continue;
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                         entry.th32ProcessID);
            FILETIME creation{}, exit{}, kernel{}, user{};
            if (process && GetProcessTimes(process, &creation, &exit, &kernel, &user) &&
                CompareFileTime(&creation, &newest) > 0) {
                newest = creation;
                result = entry.th32ProcessID;
            }
            if (process) CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

ModuleInfo module_info(DWORD pid, const wchar_t* name) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return {};
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    ModuleInfo result{};
    if (Module32FirstW(snapshot, &module)) {
        do {
            if (_wcsicmp(module.szModule, name) == 0) {
                result.base = reinterpret_cast<uintptr_t>(module.modBaseAddr);
                result.path = module.szExePath;
                break;
            }
        } while (Module32NextW(snapshot, &module));
    }
    CloseHandle(snapshot);
    return result;
}

bool has_incompatible_hook(DWORD pid, const wchar_t* required_name) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    bool found = false;
    if (Module32FirstW(snapshot, &module)) {
        do {
            if (_wcsnicmp(module.szModule, L"hytale_vr_camera_hook_", 22) == 0 &&
                _wcsicmp(module.szModule, required_name) != 0) {
                found = true;
                break;
            }
        } while (Module32NextW(snapshot, &module));
    }
    CloseHandle(snapshot);
    return found;
}

uintptr_t remote_export_address(DWORD pid, const wchar_t* module_name,
                                const char* export_name) {
    const ModuleInfo remote = module_info(pid, module_name);
    if (!remote.base || remote.path.empty()) return 0;
    HMODULE local = LoadLibraryExW(remote.path.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!local) return 0;
    const FARPROC local_export = GetProcAddress(local, export_name);
    const uintptr_t result = local_export
        ? remote.base + (reinterpret_cast<uintptr_t>(local_export) -
                         reinterpret_cast<uintptr_t>(local))
        : 0;
    FreeLibrary(local);
    return result;
}

std::wstring executable_directory() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring result(path);
    const size_t slash = result.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : result.substr(0, slash);
}

bool remote_load_library(HANDLE process, const std::wstring& path) {
    const SIZE_T size = (path.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE,
                                  PAGE_READWRITE);
    if (!remote) return false;
    bool success = WriteProcessMemory(process, remote, path.c_str(), size, nullptr) != FALSE;
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    auto* load_library = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        kernel32 ? GetProcAddress(kernel32, "LoadLibraryW") : nullptr);
    HANDLE thread = success && load_library
        ? CreateRemoteThread(process, nullptr, 0, load_library, remote, 0, nullptr)
        : nullptr;
    if (thread) {
        success = WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0;
        DWORD result = 0;
        success = success && GetExitCodeThread(thread, &result) && result != 0;
        CloseHandle(thread);
    } else {
        success = false;
    }
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    return success;
}

void begin_control_write(VrCameraShared* shared) {
    if ((InterlockedCompareExchange(
            reinterpret_cast<volatile LONG*>(&shared->control_sequence), 0, 0) & 1) != 0) {
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&shared->control_sequence));
    }
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&shared->control_sequence));
    MemoryBarrier();
}

void end_control_write(VrCameraShared* shared) {
    MemoryBarrier();
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&shared->control_sequence));
}

void publish_unload(VrCameraShared* shared) {
    begin_control_write(shared);
    shared->enabled = 0;
    shared->stereo_enabled = 0;
    shared->shutdown_requested = 1;
    shared->unload_requested = 1;
    end_control_write(shared);
}

void publish_install(VrCameraShared* shared) {
    begin_control_write(shared);
    shared->enabled = 0;
    shared->stereo_enabled = 0;
    shared->shutdown_requested = 0;
    shared->unload_requested = 0;
    ++shared->install_sequence;
    shared->hook_error = 0;
    end_control_write(shared);
}

void publish_runtime(VrCameraShared* shared) {
    begin_control_write(shared);
    shared->enabled = 1;
    shared->stereo_enabled = 0;
    shared->shutdown_requested = 0;
    shared->unload_requested = 0;
    shared->translation_scale = 1.0f;
    shared->translation_y_scale = 1.0f;
    shared->invert_translation_xz = 0;
    ++shared->recenter_sequence;
    end_control_write(shared);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const bool test_input = argc > 1 && _wcsicmp(argv[1], L"--input") == 0;
    const DWORD pid = find_hytale();
    if (!pid) {
        std::wcerr << L"HytaleClient.exe introuvable.\n";
        return 1;
    }
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                        sizeof(VrCameraShared), kVrCameraMappingName);
    if (!mapping) return 3;
    const bool mapping_existed = GetLastError() == ERROR_ALREADY_EXISTS;
    auto* shared = static_cast<VrCameraShared*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(VrCameraShared)));
    if (!shared) {
        CloseHandle(mapping);
        return 4;
    }
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                     PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                                 FALSE, pid);
    if (!process) return 5;
    if (has_incompatible_hook(pid, L"hytale_vr_camera_hook_v120_native_hand.dll")) {
        std::wcerr << L"Une ancienne version du hook est chargee; relance Hytale avant le test.\n";
        CloseHandle(process);
        return 2;
    }
    const ModuleInfo existing_hook = module_info(pid, L"hytale_vr_camera_hook_v120_native_hand.dll");
    bool loaded = false;
    bool rearm_existing = false;
    if (existing_hook.base) {
        if (!mapping_existed || shared->magic != kVrCameraMagic ||
            shared->version != kVrCameraVersion || shared->hook_active) {
            std::wcerr << L"Le hook v100 ignore draw est deja actif; utilise le dashboard pour l'arreter.\n";
            CloseHandle(process);
            return 2;
        }
        rearm_existing = true;
        loaded = true;
    } else {
        *shared = VrCameraShared{};
    }

    const uintptr_t sdl_swap = remote_export_address(pid, L"SDL3.dll", "SDL_GL_SwapWindow");
    std::array<unsigned char, 14> original_swap{};
    SIZE_T swap_read = 0;
    if (!sdl_swap || !ReadProcessMemory(process, reinterpret_cast<const void*>(sdl_swap),
                                        original_swap.data(), original_swap.size(), &swap_read) ||
        swap_read != original_swap.size()) {
        std::wcerr << L"Impossible de lire SDL_GL_SwapWindow.\n";
        CloseHandle(process);
        return 12;
    }
    if (!loaded) {
        const std::wstring directory = executable_directory();
        loaded = remote_load_library(process, directory + L"\\HytaleUIScaleHook.dll") &&
                 remote_load_library(process, directory + L"\\openvr_api.dll") &&
                 remote_load_library(process, directory + L"\\hytale_vr_camera_hook_v120_native_hand.dll");
    } else if (rearm_existing) {
        publish_install(shared);
    }
    if (!loaded) {
        std::wcerr << L"LoadLibrary distant a echoue.\n";
        CloseHandle(process);
        return 6;
    }

    for (int i = 0; i < 400 && !shared->hook_active && shared->hook_error == 0; ++i) Sleep(5);
    if (!shared->hook_active) {
        std::wcerr << L"Installation refusee, hook_error=" << shared->hook_error << L".\n";
        CloseHandle(process);
        return 7;
    }
    const uint32_t before = shared->camera_hook_entries;
    Sleep(500);
    const uint32_t after = shared->camera_hook_entries;
    std::wcout << L"Hook actif; appels camera=" << (after - before)
               << L", appels rayon=" << shared->interaction_ray_calls << L".\n";
    if (after == before || !shared->interaction_hook_active ||
        shared->interaction_ray_calls == 0) {
        publish_unload(shared);
        CloseHandle(process);
        return 8;
    }

    if (test_input) {
        publish_runtime(shared);
        Sleep(1500);
        std::wcout << L"Input SteamVR: actif=" << shared->controller_active
                   << L" stick=" << shared->controller_move_x << L","
                   << shared->controller_move_y
                   << L" rotation=" << shared->controller_turn_x << L","
                   << shared->controller_turn_y
                   << L" saut=" << shared->controller_jump
                   << L" sprint=" << shared->controller_sprint
                   << L" poseDroite=" << shared->controller_right_pose_active
                   << L" trigger=" << shared->controller_right_trigger
                   << L" erreur=" << shared->controller_input_error << L".\n";
    }

    publish_unload(shared);
    for (int i = 0; i < 400 && shared->hook_active; ++i) Sleep(5);
    if (shared->hook_active) {
        std::wcerr << L"Le hook n'a pas confirme sa restauration.\n";
        CloseHandle(process);
        return 9;
    }

    const uintptr_t base = module_info(pid, L"HytaleClient.exe").base;
    std::array<unsigned char, 16> bytes{};
    std::array<unsigned char, 15> interaction_bytes{};
    std::array<unsigned char, 14> restored_swap{};
    SIZE_T read = 0;
    const bool restored = base && ReadProcessMemory(
        process, reinterpret_cast<const void*>(base + kCameraHookOffset),
        bytes.data(), bytes.size(), &read) && read == bytes.size() &&
        bytes == kExpectedCameraBytes;
    SIZE_T interaction_read = 0;
    const bool interaction_restored = base && ReadProcessMemory(
        process, reinterpret_cast<const void*>(base + kInteractionHookOffset),
        interaction_bytes.data(), interaction_bytes.size(), &interaction_read) &&
        interaction_read == interaction_bytes.size() &&
        interaction_bytes == kExpectedInteractionBytes;
    SIZE_T restored_swap_read = 0;
    const bool swap_restored = ReadProcessMemory(
        process, reinterpret_cast<const void*>(sdl_swap), restored_swap.data(),
        restored_swap.size(), &restored_swap_read) &&
        restored_swap_read == restored_swap.size() && restored_swap == original_swap;
    CloseHandle(process);
    UnmapViewOfFile(shared);
    CloseHandle(mapping);
    if (!restored || !interaction_restored || !swap_restored) {
        std::wcerr << L"Les octets camera, interaction ou SDL originaux ne sont pas restaures.\n";
        return 10;
    }
    std::wcout << L"Restauration camera/interaction/SDL confirmee; module v100 ignore draw pret a etre rearme.\n";
    return 0;
}
