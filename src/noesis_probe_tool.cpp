#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <iostream>
#include <string>

#include "noesis_probe_shared.h"

namespace {

DWORD find_hytale() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
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

} // namespace

int wmain(int argc, wchar_t** argv) {
    int seconds = 12;
    if (argc >= 2) seconds = std::clamp(_wtoi(argv[1]), 2, 120);
    const DWORD pid = find_hytale();
    if (!pid) {
        std::wcerr << L"HytaleClient.exe introuvable.\n";
        return 1;
    }

    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                        sizeof(NoesisProbeShared), kNoesisProbeMappingName);
    if (!mapping) {
        std::wcerr << L"Impossible de creer le mapping Noesis probe.\n";
        return 1;
    }
    auto* shared = static_cast<NoesisProbeShared*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(NoesisProbeShared)));
    if (!shared) {
        std::wcerr << L"Impossible de mapper Noesis probe.\n";
        CloseHandle(mapping);
        return 1;
    }
    *shared = NoesisProbeShared{};

    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                     PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                                 FALSE, pid);
    if (!process) {
        std::wcerr << L"OpenProcess refuse. Lance l'outil en administrateur si besoin.\n";
        return 1;
    }
    const std::wstring dll = executable_directory() + L"\\hytale_noesis_probe.dll";
    if (!remote_load_library(process, dll)) {
        std::wcerr << L"Injection hytale_noesis_probe.dll echouee.\n";
        CloseHandle(process);
        return 1;
    }
    CloseHandle(process);

    for (int i = 0; i < 200 && !shared->hook_active && shared->hook_error == 0; ++i) Sleep(10);
    if (!shared->hook_active) {
        std::wcerr << L"Hook Noesis inactif. error=" << shared->hook_error << L"\n";
        return 1;
    }

    std::wcout << L"Probe Noesis actif sur Hytale PID " << pid
               << L". Ouvre/ferme le menu maintenant.\n";
    NoesisProbeShared last = *shared;
    for (int i = 0; i < seconds; ++i) {
        Sleep(1000);
        NoesisProbeShared cur = *shared;
        std::wcout << L"t+" << (i + 1)
                   << L"s view=" << cur.update_render_tree_calls
                   << L"(+" << (cur.update_render_tree_calls - last.update_render_tree_calls) << L")"
                   << L" render=" << cur.renderer_render_calls
                   << L"(+" << (cur.renderer_render_calls - last.renderer_render_calls) << L")"
                   << L" off=" << cur.renderer_offscreen_calls
                   << L"(+" << (cur.renderer_offscreen_calls - last.renderer_offscreen_calls) << L")"
                   << L" batch=" << cur.draw_batch_calls
                   << L"(+" << (cur.draw_batch_calls - last.draw_batch_calls) << L")"
                   << L" rt=" << cur.set_render_target_calls
                   << L"(+" << (cur.set_render_target_calls - last.set_render_target_calls) << L")"
                   << L" on=" << cur.begin_calls << L"/" << cur.end_calls
                   << L"(+" << (cur.begin_calls - last.begin_calls) << L"/"
                   << (cur.end_calls - last.end_calls) << L")"
                   << L" offdev=" << cur.begin_offscreen_calls << L"/"
                   << cur.end_offscreen_calls
                   << L"(+" << (cur.begin_offscreen_calls - last.begin_offscreen_calls)
                   << L"/" << (cur.end_offscreen_calls - last.end_offscreen_calls) << L")\n";
        last = cur;
    }
    shared->unload_requested = 1;
    for (int i = 0; i < 200 && shared->hook_active; ++i) Sleep(10);
    std::wcout << L"Probe termine. hook_active=" << shared->hook_active
               << L" error=" << shared->hook_error << L"\n";
    return shared->hook_active ? 1 : 0;
}
