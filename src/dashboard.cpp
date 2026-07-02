#include "shared.h"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cwchar>
#include <string>

namespace {
constexpr wchar_t kWindowClass[] = L"HytaleCameraDashboardWindow";

enum ControlId {
    IdInject = 100,
    IdOverride,
    IdPosition,
    IdMatrix,
    IdPatchKnownTable,
    IdPatchTables,
    IdApply,
    IdX,
    IdY,
    IdZ,
    IdPitch,
    IdYaw,
    IdRoll,
    IdStatus,
    IdUniforms,
};

CameraProbeShared* g_shared = nullptr;
HANDLE g_mapping = nullptr;
HWND g_status = nullptr;
HWND g_uniforms = nullptr;
HWND g_x = nullptr;
HWND g_y = nullptr;
HWND g_z = nullptr;
HWND g_pitch = nullptr;
HWND g_yaw = nullptr;
HWND g_roll = nullptr;
HWND g_override = nullptr;
HWND g_position = nullptr;
HWND g_matrix = nullptr;
HWND g_patch_known_table = nullptr;
HWND g_patch_tables = nullptr;

std::wstring exe_dir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* last = wcsrchr(path, L'\\');
    if (last) *last = 0;
    return path;
}

std::wstring probe_path() {
    return exe_dir() + L"\\hytale_camera_probe.dll";
}

void set_window_text(HWND hwnd, const std::wstring& text) {
    SetWindowTextW(hwnd, text.c_str());
}

float read_float(HWND hwnd) {
    wchar_t text[64]{};
    GetWindowTextW(hwnd, text, 64);
    return static_cast<float>(wcstod(text, nullptr));
}

void write_float(HWND hwnd, float value) {
    wchar_t text[64]{};
    swprintf_s(text, L"%.4f", value);
    SetWindowTextW(hwnd, text);
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
    return true;
}

DWORD find_process_id(const wchar_t* exe_name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD pid = 0;
    if (Process32FirstW(snap, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, exe_name) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return pid;
}

bool inject_probe_into(DWORD pid, std::wstring& message) {
    const std::wstring dll = probe_path();
    DWORD attrs = GetFileAttributesW(dll.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        message = L"DLL probe introuvable: " + dll;
        return false;
    }

    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                     PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                                 FALSE, pid);
    if (!process) {
        message = L"OpenProcess refuse. Lance le dashboard avec les memes droits que Hytale.";
        return false;
    }

    const size_t bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        CloseHandle(process);
        message = L"VirtualAllocEx impossible.";
        return false;
    }
    WriteProcessMemory(process, remote, dll.c_str(), bytes, nullptr);

    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    auto* load_library = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(kernel, "LoadLibraryW"));
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, load_library, remote, 0, nullptr);
    if (!thread) {
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        message = L"CreateRemoteThread impossible.";
        return false;
    }
    WaitForSingleObject(thread, 5000);
    DWORD exit_code = 0;
    GetExitCodeThread(thread, &exit_code);
    CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);

    message = exit_code ? L"Probe injecte." : L"LoadLibraryW a echoue dans Hytale.";
    return exit_code != 0;
}

bool inject_probe(std::wstring& message) {
    DWORD pid = find_process_id(L"HytaleClient.exe");
    if (!pid) {
        message = L"HytaleClient.exe introuvable. Lance Hytale avant d'injecter.";
        return false;
    }
    return inject_probe_into(pid, message);
}

void set_shared_status(const std::wstring& message) {
    if (!g_shared) return;
    char status[256]{};
    WideCharToMultiByte(CP_UTF8, 0, message.c_str(), -1, status, sizeof(status), nullptr, nullptr);
    strncpy_s(g_shared->status, status, _TRUNCATE);
}

HWND make_label(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, parent, nullptr, nullptr, nullptr);
}

HWND make_button(HWND parent, const wchar_t* text, int id, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                           x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           nullptr, nullptr);
}

HWND make_check(HWND parent, const wchar_t* text, int id, int x, int y, int w, int h, bool checked) {
    HWND hwnd = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                x, y, w, h, parent,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(hwnd, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return hwnd;
}

HWND make_edit(HWND parent, int id, int x, int y, int w, int h, const wchar_t* value) {
    HWND hwnd = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", value,
                                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                x, y, w, h, parent,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(hwnd, EM_SETLIMITTEXT, 32, 0);
    return hwnd;
}

void apply_overrides() {
    if (!g_shared) return;
    g_shared->override_position[0] = read_float(g_x);
    g_shared->override_position[1] = read_float(g_y);
    g_shared->override_position[2] = read_float(g_z);
    g_shared->override_rotation[0] = read_float(g_pitch);
    g_shared->override_rotation[1] = read_float(g_yaw);
    g_shared->override_rotation[2] = read_float(g_roll);
    g_shared->override_enabled = SendMessageW(g_override, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_shared->override_position_enabled = SendMessageW(g_position, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_shared->override_matrix_enabled = SendMessageW(g_matrix, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (SendMessageW(g_patch_tables, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        g_shared->flags |= kFlagPatchGlTables;
    } else {
        g_shared->flags &= ~kFlagPatchGlTables;
    }
    if (SendMessageW(g_patch_known_table, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        g_shared->flags |= kFlagPatchKnownGlTable;
    } else {
        g_shared->flags &= ~kFlagPatchKnownGlTable;
    }
}

void refresh_dashboard() {
    if (!g_shared) return;

    wchar_t text[2048]{};
    swprintf_s(text,
               L"PID probe: %u\r\n"
               L"SDL hook: %s\r\n"
               L"Programme GL courant: %u\r\n"
               L"Dernier GL charge: %S\r\n"
               L"Dernier uniform: %S (loc %d)\r\n"
               L"Uniforms camera vus: %u\r\n"
               L"Pointeurs GL patches: %u\r\n"
               L"glGetUniformLocation camera: %u\r\n"
               L"Matrices camera uploadees: %u\r\n"
               L"Positions camera uploadees: %u\r\n"
               L"Overrides appliques: %u\r\n"
               L"Derniere position camera: %.4f / %.4f / %.4f\r\n"
               L"Status: %S",
               g_shared->probe_pid,
               g_shared->sdl_hooked ? L"oui" : L"non",
               g_shared->current_program,
               g_shared->last_gl_name,
               g_shared->last_uniform_name,
               g_shared->last_location,
               g_shared->uniform_count,
               g_shared->patched_gl_pointers,
               g_shared->get_uniform_location_seen,
               g_shared->matrix_uploads,
               g_shared->camera_position_uploads,
               g_shared->override_uploads,
               g_shared->last_camera_position[0],
               g_shared->last_camera_position[1],
               g_shared->last_camera_position[2],
               g_shared->status);
    set_window_text(g_status, text);

    std::wstring uniforms;
    const uint32_t count = std::min(g_shared->uniform_count, kMaxUniforms);
    for (uint32_t i = 0; i < count; ++i) {
        wchar_t line[160]{};
        swprintf_s(line, L"%03u  program=%u  loc=%d  %S\r\n",
                   i,
                   g_shared->uniforms[i].program,
                   g_shared->uniforms[i].location,
                   g_shared->uniforms[i].name);
        uniforms += line;
    }
    set_window_text(g_uniforms, uniforms);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_CREATE: {
        InitCommonControls();
        make_button(hwnd, L"Inject probe", IdInject, 14, 14, 118, 28);
        make_label(hwnd, L"Lance Hytale via le launcher officiel, puis injecte une fois au menu/en jeu.",
                   148, 18, 430, 22);
        g_override = make_check(hwnd, L"Override", IdOverride, 590, 18, 86, 22, false);
        g_position = make_check(hwnd, L"Position", IdPosition, 682, 18, 82, 22, true);
        g_matrix = make_check(hwnd, L"Matrix", IdMatrix, 14, 48, 76, 22, false);
        g_patch_known_table = make_check(hwnd, L"Patch cible GL", IdPatchKnownTable, 104, 48, 120, 22, false);
        g_patch_tables = make_check(hwnd, L"Scan large (risque)", IdPatchTables, 238, 48, 150, 22, false);

        make_label(hwnd, L"X", 16, 62, 16, 20);
        g_x = make_edit(hwnd, IdX, 34, 58, 80, 24, L"0");
        make_label(hwnd, L"Y", 122, 62, 16, 20);
        g_y = make_edit(hwnd, IdY, 140, 58, 80, 24, L"0");
        make_label(hwnd, L"Z", 228, 62, 16, 20);
        g_z = make_edit(hwnd, IdZ, 246, 58, 80, 24, L"0");

        make_label(hwnd, L"Pitch", 344, 62, 42, 20);
        g_pitch = make_edit(hwnd, IdPitch, 390, 58, 70, 24, L"0");
        make_label(hwnd, L"Yaw", 470, 62, 34, 20);
        g_yaw = make_edit(hwnd, IdYaw, 506, 58, 70, 24, L"0");
        make_label(hwnd, L"Roll", 586, 62, 34, 20);
        g_roll = make_edit(hwnd, IdRoll, 622, 58, 70, 24, L"0");
        make_button(hwnd, L"Apply", IdApply, 710, 56, 80, 28);

        g_status = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                   WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
                                   14, 100, 776, 230, hwnd, reinterpret_cast<HMENU>(IdStatus), nullptr, nullptr);
        make_label(hwnd, L"Uniforms camera detectes", 14, 344, 260, 20);
        g_uniforms = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                     WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY |
                                         WS_VSCROLL | WS_HSCROLL,
                                     14, 368, 776, 250, hwnd, reinterpret_cast<HMENU>(IdUniforms), nullptr, nullptr);

        if (g_shared) {
            write_float(g_x, g_shared->override_position[0]);
            write_float(g_y, g_shared->override_position[1]);
            write_float(g_z, g_shared->override_position[2]);
        }
        SetTimer(hwnd, 1, 250, nullptr);
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        if (id == IdInject) {
            apply_overrides();
            std::wstring message;
            inject_probe(message);
            set_shared_status(message);
        } else if (id == IdApply || id == IdOverride || id == IdPosition ||
                   id == IdMatrix || id == IdPatchKnownTable || id == IdPatchTables) {
            apply_overrides();
        }
        refresh_dashboard();
        return 0;
    }
    case WM_TIMER:
        refresh_dashboard();
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}
} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    init_shared();
    WNDCLASSW wc{};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, kWindowClass, L"Hytale Camera Probe Dashboard",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 820, 680,
                                nullptr, nullptr, instance, nullptr);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
