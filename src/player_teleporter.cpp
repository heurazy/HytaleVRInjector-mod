#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <tlhelp32.h>
#include <commctrl.h>
#include <dwmapi.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

#ifdef HYTALE_CAMERA_MODE
#include <openvr.h>
#include "vr_camera_shared.h"
#include "vr_locomotion.h"
#endif

namespace {

constexpr wchar_t kWindowClass[] = L"HytalePlayerTeleporterWindow";
constexpr uintptr_t kCoordinateOffsets[] = {0x270, 0x27C, 0x288, 0x294};
constexpr size_t kScanChunk = 2 * 1024 * 1024;
constexpr UINT_PTR kWriteTimer = 1;
#ifdef HYTALE_CAMERA_MODE
constexpr UINT_PTR kVrTimer = 2;
constexpr UINT kScanCompleteMessage = WM_APP + 1;
#endif

enum ControlId {
    IDC_CURRENT_X = 101,
    IDC_CURRENT_Y,
    IDC_CURRENT_Z,
    IDC_TARGET_X,
    IDC_TARGET_Y,
    IDC_TARGET_Z,
    IDC_TOLERANCE,
    IDC_ATTACH,
    IDC_SCAN,
    IDC_TELEPORT,
    IDC_RESTORE,
    IDC_HOLD,
    IDC_CANDIDATES,
    IDC_STATUS,
#ifdef HYTALE_CAMERA_MODE
    IDC_VR_SCALE,
    IDC_VR_NON_VR,
    IDC_VR_Y_SCALE,
    IDC_VR_INVERT_Z,
    IDC_VR_CENTER,
    IDC_VR_STOP,
    IDC_VR_POSE,
    IDC_VR_DISABLE_SHADOWS,
    IDC_VR_DISABLE_PARTICLES,
    IDC_VR_DISABLE_DISTORTION,
    IDC_VR_STEREO,
    IDC_VR_IPD,
    IDC_VR_SEPARATION,
    IDC_VR_SWAP_EYES,
    IDC_VR_RENDER_SCALE,
    IDC_VR_QUEST_LOCOMOTION,
    IDC_VR_QUEST_AZERTY,
    IDC_VR_QUEST_DEADZONE,
    IDC_VR_HAND_POINTER,
    IDC_VR_HIDE_RETICLE,
    IDC_VR_POINTER_DISTANCE,
    IDC_VR_TURN_SPEED,
    IDC_VR_DISABLE_FOREGROUND_EFFECTS,
    IDC_VR_MENU_DISTANCE,
    IDC_VR_MENU_WIDTH,
    IDC_VR_UI_SCALE,
    IDC_VR_UI_EYE_OFFSET,
    IDC_VR_UI_Y_OFFSET,
    IDC_VR_MENU_MOUSE,
    IDC_VR_NO_UBO_UI,
    IDC_VR_MENU_IGNORE_DRAW,
    IDC_VR_FLOOR_TILT,
    IDC_VR_CALIBRATE_FLOOR,
    IDC_VR_HIDE_FIRST_PERSON_HAND,
    IDC_VR_ADVANCED_OPTIONS,
    IDC_VR_KEY_FORWARD,
    IDC_VR_KEY_BACKWARD,
    IDC_VR_KEY_LEFT,
    IDC_VR_KEY_RIGHT,
    IDC_VR_KEY_JUMP,
    IDC_VR_KEY_SPRINT,
    IDC_VR_KEY_USE,
    IDC_VR_KEY_INVENTORY,
    IDC_TAB_DASHBOARD = 301,
    IDC_TAB_SETTINGS,
    IDC_TAB_RENDERING,
    IDC_TAB_KEYBINDINGS,
    IDC_TAB_UI,
#endif
};

struct Vec3 {
    float x;
    float y;
    float z;
};

struct Candidate {
    uintptr_t base;
    Vec3 original[4];
};

struct FeetReading {
    Vec3 value{};
    size_t copies = 0;
    uintptr_t latest_address = 0;
};

#ifdef HYTALE_CAMERA_MODE
struct ScanResult {
    bool feet_found = false;
    Vec3 feet{};
    uintptr_t f7_allocation_base = 0;
    std::vector<Candidate> candidates;
    std::wstring status;
};

struct ScanWork {
    HANDLE process = nullptr;
    float tolerance = 0.10f;
    uintptr_t f7_allocation_base = 0;
    std::vector<Candidate> known_candidates;
};


struct InjectedKey {
    WORD virtual_key = 0;
    WORD scan_code = 0;
    bool down = false;
};

struct InjectedKeys {
    InjectedKey left{};
    InjectedKey right{};
    InjectedKey forward{};
    InjectedKey backward{};
    InjectedKey jump{};
    InjectedKey sprint{};
};
#endif

HWND g_window = nullptr;
HANDLE g_process = nullptr;
DWORD g_pid = 0;
std::vector<Candidate> g_candidates;
Vec3 g_target{};
ULONGLONG g_burst_until = 0;
uintptr_t g_f7_allocation_base = 0;
int g_current_tab = 0;
ULONG_PTR g_gdiplusToken = 0;
HBRUSH g_background_brush = nullptr;
HBRUSH g_edit_background_brush = nullptr;
HBRUSH g_list_background_brush = nullptr;

constexpr COLORREF kLuaToolsBgRef = RGB(11, 11, 18);
constexpr COLORREF kLuaToolsSidebarRef = RGB(18, 18, 26);
constexpr COLORREF kLuaToolsPanelRef = RGB(20, 20, 28);
constexpr COLORREF kLuaToolsInputRef = RGB(15, 15, 22);
constexpr COLORREF kLuaToolsTextRef = RGB(229, 231, 235);
constexpr COLORREF kLuaToolsMutedRef = RGB(156, 163, 175);
constexpr COLORREF kLuaToolsHairlineRef = RGB(41, 41, 53);
constexpr COLORREF kLuaToolsAccentRef = RGB(167, 139, 250);
constexpr COLORREF kLuaToolsGreenRef = RGB(34, 197, 94);
constexpr COLORREF kLuaToolsOrangeRef = RGB(249, 115, 22);

inline Gdiplus::Color gdip_rgb(BYTE r, BYTE g, BYTE b) {
    return Gdiplus::Color(255, r, g, b);
}

inline Gdiplus::Color gdip_argb(BYTE a, BYTE r, BYTE g, BYTE b) {
    return Gdiplus::Color(a, r, g, b);
}

typedef enum _WINDOWCOMPOSITIONATTRIB {
    WCA_ACCENT_POLICY = 19,
    WCA_USEDARKMODECOLORS = 26
} WINDOWCOMPOSITIONATTRIB;

typedef struct _WINDOWCOMPOSITIONATTRIBDATA {
    WINDOWCOMPOSITIONATTRIB Attrib;
    PVOID pvData;
    SIZE_T cbData;
} WINDOWCOMPOSITIONATTRIBDATA;

typedef enum _ACCENT_STATE {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
    ACCENT_ENABLE_HOSTBACKDROP = 5
} ACCENT_STATE;

typedef struct _ACCENT_POLICY {
    ACCENT_STATE AccentState;
    DWORD AccentFlags;
    DWORD GradientColor;
    DWORD AnimationId;
} ACCENT_POLICY;

using SetWindowCompositionAttributePtr = BOOL(WINAPI*)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);
using RtlGetVersionPtr = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);

RTL_OSVERSIONINFOW get_windows_version() {
    RTL_OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return version;
    auto rtl_get_version = reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(ntdll, "RtlGetVersion"));
    if (rtl_get_version) rtl_get_version(&version);
    return version;
}

DWORD accent_color_argb(BYTE a, BYTE r, BYTE g, BYTE b) {
    return (static_cast<DWORD>(a) << 24) |
           (static_cast<DWORD>(b) << 16) |
           (static_cast<DWORD>(g) << 8) |
           static_cast<DWORD>(r);
}

bool apply_acrylic_fallback(HWND hwnd) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return false;
    auto set_window_composition_attribute =
        reinterpret_cast<SetWindowCompositionAttributePtr>(GetProcAddress(user32, "SetWindowCompositionAttribute"));
    if (!set_window_composition_attribute) return false;

    ACCENT_POLICY accent{
        ACCENT_ENABLE_ACRYLICBLURBEHIND,
        2,
        accent_color_argb(0x92, 0x18, 0x14, 0x1E),
        0
    };
    WINDOWCOMPOSITIONATTRIBDATA data{
        WCA_ACCENT_POLICY,
        &accent,
        sizeof(accent)
    };
    return set_window_composition_attribute(hwnd, &data) != FALSE;
}

void enable_mica_material(HWND hwnd) {
    if (!hwnd) return;

    constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
    constexpr DWORD kDwmwaWindowCornerPreference = 33;
    constexpr DWORD kDwmwaCaptionColor = 35;
    constexpr DWORD kDwmwaSystemBackdropType = 38;
    constexpr DWORD kDwmwaMicaEffect = 1029;
    constexpr int kDwmwcpRound = 2;
    constexpr int kDwmsbtMica = 2;
    constexpr int kDwmsbtTabbed = 4;
    constexpr COLORREF kColorNone = 0xFFFFFFFE;

    RTL_OSVERSIONINFOW version = get_windows_version();
    BOOL dark = TRUE;
    int rounded = kDwmwcpRound;
    MARGINS sheet_of_glass{-1, -1, -1, -1};

    DwmExtendFrameIntoClientArea(hwnd, &sheet_of_glass);
    DwmSetWindowAttribute(hwnd, kDwmwaUseImmersiveDarkMode, &dark, sizeof(dark));
    DwmSetWindowAttribute(hwnd, kDwmwaWindowCornerPreference, &rounded, sizeof(rounded));
    DwmSetWindowAttribute(hwnd, kDwmwaCaptionColor, &kColorNone, sizeof(kColorNone));

    LONG_PTR ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex_style | WS_EX_LAYERED);
    SetLayeredWindowAttributes(hwnd, 0, 238, LWA_ALPHA);

    bool dwm_backdrop_applied = false;
    if (version.dwBuildNumber >= 22523) {
        int backdrop = kDwmsbtTabbed;
        if (SUCCEEDED(DwmSetWindowAttribute(hwnd, kDwmwaSystemBackdropType, &backdrop, sizeof(backdrop)))) {
            dwm_backdrop_applied = true;
        }
        if (!dwm_backdrop_applied) {
            backdrop = kDwmsbtMica;
            if (SUCCEEDED(DwmSetWindowAttribute(hwnd, kDwmwaSystemBackdropType, &backdrop, sizeof(backdrop)))) {
                dwm_backdrop_applied = true;
            }
        }
    }

    if (!dwm_backdrop_applied && version.dwBuildNumber >= 22000) {
        BOOL enable = TRUE;
        if (SUCCEEDED(DwmSetWindowAttribute(hwnd, kDwmwaMicaEffect, &enable, sizeof(enable)))) {
            dwm_backdrop_applied = true;
        }
    }

    // flutter_acrylic's Acrylic path is the one that visibly blurs/tints through
    // the window. Keep it active even when Mica is available so the Win32/GDI
    // dashboard does not look like a fully opaque painted surface.
    apply_acrylic_fallback(hwnd);
}

#ifdef HYTALE_CAMERA_MODE
std::atomic_bool g_f7_scan_busy = false;
std::atomic_bool g_scan_cancel = false;
std::atomic<ScanResult*> g_pending_scan_result = nullptr;
HANDLE g_scan_thread = nullptr;
std::vector<HWND> g_advanced_controls;

vr::IVRSystem* g_vr_system = nullptr;
bool g_vr_tracking = false;
HANDLE g_vr_camera_mapping = nullptr;
VrCameraShared* g_vr_camera_shared = nullptr;
DWORD g_vr_camera_mapping_error = ERROR_SUCCESS;
bool g_vr_camera_mapping_existed = false;
uint32_t g_vr_recenter_sequence = 0;
hytalevr::DigitalStickState g_quest_stick_state{};
InjectedKeys g_injected_keys{};
float g_quest_stick_x = 0.0f;
float g_quest_stick_y = 0.0f;
bool g_quest_controller_connected = false;
float g_vr_head_origin_yaw = 0.0f;
bool g_vr_head_origin_yaw_valid = false;
float g_vr_last_head_sync_yaw = 0.0f;
bool g_vr_last_head_sync_yaw_valid = false;
float g_head_turn_mouse_remainder = 0.0f;
ULONGLONG g_last_stick_turn_tick = 0;
bool g_left_mouse_down = false;
bool g_right_mouse_down = false;
float g_turn_mouse_remainder = 0.0f;
ULONGLONG g_last_turn_tick = 0;
bool g_menu_mouse_active = false;
POINT g_menu_mouse_client{};
float g_menu_mouse_filtered_x = 0.0f;
float g_menu_mouse_filtered_y = 0.0f;
bool g_menu_mouse_filter_valid = false;
bool g_prev_button_x = false;
bool g_prev_button_y = false;
bool g_prev_button_b = false;
bool g_prev_left_grip = false;
bool g_prev_right_grip = false;
bool g_prev_right_stick_click = false;
bool g_sneak_toggled = false;
InjectedKey g_button_f_key{};
InjectedKey g_button_tab_key{};
InjectedKey g_button_a_key{};
InjectedKey g_sneak_key{};
ULONGLONG g_button_f_release_at = 0;
ULONGLONG g_button_tab_release_at = 0;
ULONGLONG g_button_a_release_at = 0;
#endif

HWND control(int id) {
    return GetDlgItem(g_window, id);
}

#ifdef HYTALE_CAMERA_MODE
constexpr UINT_PTR SUBCLASS_EDIT = 1;
constexpr UINT_PTR SUBCLASS_BUTTON = 2;
constexpr UINT_PTR SUBCLASS_TOGGLE = 3;
constexpr UINT_PTR SUBCLASS_STATIC = 4;
constexpr UINT_PTR SUBCLASS_LISTBOX = 5;

// Subclass procedures:
LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                  UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    UNREFERENCED_PARAMETER(uIdSubclass);
    UNREFERENCED_PARAMETER(dwRefData);
    switch (uMsg) {
    case WM_PAINT: {
        LRESULT res = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        HDC hdc = GetWindowDC(hwnd);
        RECT rect;
        GetWindowRect(hwnd, &rect);
        int w = rect.right - rect.left;
        int h = rect.bottom - rect.top;
        
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        
        Gdiplus::Color glowColor;
        if (GetFocus() == hwnd) {
            glowColor = Gdiplus::Color(255, 167, 139, 250);
        } else {
            glowColor = Gdiplus::Color(255, 41, 41, 53);
        }
        
        Gdiplus::Pen pen(glowColor, 1.2f);
        Gdiplus::GraphicsPath path;
        float r = 8.0f;
        path.AddArc(0.5f, 0.5f, r, r, 180, 90);
        path.AddArc(w - r - 1.5f, 0.5f, r, r, 270, 90);
        path.AddArc(w - r - 1.5f, h - r - 1.5f, r, r, 0, 90);
        path.AddArc(0.5f, h - r - 1.5f, r, r, 90, 90);
        path.CloseFigure();
        g.DrawPath(&pen, &path);
        
        ReleaseDC(hwnd, hdc);
        return res;
    }
    case WM_NCPAINT:
        return 0;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                    UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    UNREFERENCED_PARAMETER(uIdSubclass);
    UNREFERENCED_PARAMETER(dwRefData);
    static HWND s_hoveredBtn = nullptr;
    switch (uMsg) {
    case WM_MOUSEMOVE: {
        if (s_hoveredBtn != hwnd) {
            s_hoveredBtn = hwnd;
            InvalidateRect(hwnd, nullptr, FALSE);
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
        }
        break;
    }
    case WM_MOUSELEAVE: {
        if (s_hoveredBtn == hwnd) {
            s_hoveredBtn = nullptr;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    }
    
    if (uMsg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rect;
        GetClientRect(hwnd, &rect);
        int w = rect.right - rect.left;
        int h = rect.bottom - rect.top;
        
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
        
        {
            Gdiplus::Graphics g(memDC);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            
            bool isPressed = (SendMessageW(hwnd, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0;
            bool isHovered = (s_hoveredBtn == hwnd);
            int id = GetDlgCtrlID(hwnd);
            bool isSidebarBtn = (id >= IDC_TAB_DASHBOARD && id <= IDC_TAB_UI);
            bool isSecondaryAction = (id == IDC_VR_CENTER || id == IDC_ATTACH ||
                                      id == IDC_RESTORE || id == IDC_VR_CALIBRATE_FLOOR);

            g.Clear(isSidebarBtn ? gdip_rgb(12, 12, 16) : gdip_rgb(13, 13, 17));
            
            Gdiplus::Color bkColor;
            Gdiplus::Color borderColor;
            
            bool isActiveTab = false;
            if (isSidebarBtn) {
                if (id == IDC_TAB_DASHBOARD && g_current_tab == 0) isActiveTab = true;
                else if (id == IDC_TAB_SETTINGS && g_current_tab == 1) isActiveTab = true;
                else if (id == IDC_TAB_RENDERING && g_current_tab == 2) isActiveTab = true;
                else if (id == IDC_TAB_KEYBINDINGS && g_current_tab == 3) isActiveTab = true;
                else if (id == IDC_TAB_UI && g_current_tab == 4) isActiveTab = true;
            }
            
            if (id == IDC_VR_STOP) {
                if (isPressed) {
                    bkColor = Gdiplus::Color(255, 153, 27, 27);
                    borderColor = Gdiplus::Color(150, 248, 113, 113);
                } else if (isHovered) {
                    bkColor = Gdiplus::Color(255, 185, 28, 28);
                    borderColor = Gdiplus::Color(170, 248, 113, 113);
                } else {
                    bkColor = Gdiplus::Color(230, 127, 29, 29);
                    borderColor = Gdiplus::Color(80, 248, 113, 113);
                }
            } else if (isSidebarBtn) {
                if (isActiveTab) {
                    bkColor = Gdiplus::Color(180, 39, 39, 45);
                    borderColor = Gdiplus::Color(58, 255, 255, 255);
                } else if (isHovered) {
                    bkColor = Gdiplus::Color(120, 32, 32, 38);
                    borderColor = Gdiplus::Color(42, 255, 255, 255);
                } else {
                    bkColor = Gdiplus::Color(0, 0, 0, 0);
                    borderColor = Gdiplus::Color(0, 0, 0, 0);
                }
            } else if (isSecondaryAction) {
                if (isPressed) {
                    bkColor = Gdiplus::Color(210, 24, 24, 30);
                    borderColor = Gdiplus::Color(90, 255, 255, 255);
                } else if (isHovered) {
                    bkColor = Gdiplus::Color(220, 32, 32, 38);
                    borderColor = Gdiplus::Color(120, 255, 255, 255);
                } else {
                    bkColor = Gdiplus::Color(185, 13, 13, 17);
                    borderColor = Gdiplus::Color(64, 255, 255, 255);
                }
            } else {
                if (isPressed) {
                    bkColor = Gdiplus::Color(255, 109, 40, 217);
                    borderColor = Gdiplus::Color(130, 196, 181, 253);
                } else if (isHovered) {
                    bkColor = Gdiplus::Color(255, 124, 58, 237);
                    borderColor = Gdiplus::Color(160, 196, 181, 253);
                } else {
                    bkColor = Gdiplus::Color(245, 109, 40, 217);
                    borderColor = Gdiplus::Color(80, 196, 181, 253);
                }
            }
            
            if (isSidebarBtn) {
                if (isActiveTab || isHovered) {
                    Gdiplus::SolidBrush bkBrush(bkColor);
                    g.FillRectangle(&bkBrush, 0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h));
                }
                if (isActiveTab) {
                    Gdiplus::SolidBrush indicatorBrush(Gdiplus::Color(255, 167, 139, 250));
                    g.FillRectangle(&indicatorBrush, 0.0f, 5.0f, 3.0f, static_cast<float>(h - 10));
                }
            } else {
                Gdiplus::SolidBrush bkBrush(bkColor);
                Gdiplus::GraphicsPath path;
                float r = 8.0f;
                path.AddArc(0.5f, 0.5f, r, r, 180, 90);
                path.AddArc(w - r - 1.5f, 0.5f, r, r, 270, 90);
                path.AddArc(w - r - 1.5f, h - r - 1.5f, r, r, 0, 90);
                path.AddArc(0.5f, h - r - 1.5f, r, r, 90, 90);
                path.CloseFigure();
                
                g.FillPath(&bkBrush, &path);
                Gdiplus::Pen borderPen(borderColor, 1.2f);
                g.DrawPath(&borderPen, &path);
            }
            
            wchar_t text[128]{};
            GetWindowTextW(hwnd, text, 128);
            
            Gdiplus::FontFamily fontFamily(L"Segoe UI");
            Gdiplus::Font font(&fontFamily, isSidebarBtn ? 10.0f : 9.5f, 
                               (isSidebarBtn && isActiveTab) ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular, 
                               Gdiplus::UnitPoint);
            Gdiplus::StringFormat format;
            format.SetAlignment(isSidebarBtn ? Gdiplus::StringAlignmentNear : Gdiplus::StringAlignmentCenter);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            
            Gdiplus::Color textColor = (isSidebarBtn && isActiveTab)
                ? Gdiplus::Color(255, 229, 231, 235)
                : Gdiplus::Color(255, 229, 231, 235);
            Gdiplus::SolidBrush textBrush(textColor);
            float leftOffset = isSidebarBtn ? 15.0f : 0.0f;
            Gdiplus::RectF textRect(leftOffset, 0.0f, static_cast<float>(w - leftOffset), static_cast<float>(h));
            g.DrawString(text, -1, &font, textRect, &format, &textBrush);
        }
        
        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);
        
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK ToggleSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                    UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    UNREFERENCED_PARAMETER(uIdSubclass);
    UNREFERENCED_PARAMETER(dwRefData);
    static HWND s_hoveredToggle = nullptr;
    switch (uMsg) {
    case WM_MOUSEMOVE: {
        if (s_hoveredToggle != hwnd) {
            s_hoveredToggle = hwnd;
            InvalidateRect(hwnd, nullptr, FALSE);
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
        }
        break;
    }
    case WM_MOUSELEAVE: {
        if (s_hoveredToggle == hwnd) {
            s_hoveredToggle = nullptr;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        SendMessageW(hwnd, BM_SETCHECK, !SendMessageW(hwnd, BM_GETCHECK, 0, 0), 0);
        SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), (LPARAM)hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    
    if (uMsg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rect;
        GetClientRect(hwnd, &rect);
        int w = rect.right - rect.left;
        int h = rect.bottom - rect.top;
        
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
        
        {
            Gdiplus::Graphics g(memDC);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            const int id = GetDlgCtrlID(hwnd);
            const bool isSidebarToggle = (id == IDC_VR_ADVANCED_OPTIONS);
            const bool isHovered = (s_hoveredToggle == hwnd);
            g.Clear(isSidebarToggle ? gdip_rgb(12, 12, 16) : gdip_rgb(13, 13, 17));
            
            bool isChecked = (SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED);
            
            wchar_t text[128]{};
            GetWindowTextW(hwnd, text, 128);
            
            Gdiplus::FontFamily fontFamily(L"Segoe UI");
            Gdiplus::Font font(&fontFamily, 9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentNear);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            
            Gdiplus::SolidBrush textBrush(gdip_rgb(229, 231, 235));
            Gdiplus::RectF textRect(42.0f, 0.0f, static_cast<float>(w - 44), static_cast<float>(h));
            g.DrawString(text, -1, &font, textRect, &format, &textBrush);
            
            float switchW = 32.0f;
            float switchH = 16.0f;
            float switchX = 2.0f;
            float switchY = (h - switchH) / 2.0f;
            
            Gdiplus::GraphicsPath pillPath;
            float r = switchH;
            pillPath.AddArc(switchX, switchY, r, r, 180, 90);
            pillPath.AddArc(switchX + switchW - r, switchY, r, r, 270, 90);
            pillPath.AddArc(switchX + switchW - r, switchY + switchH - r, r, r, 0, 90);
            pillPath.AddArc(switchX, switchY + switchH - r, r, r, 90, 90);
            pillPath.CloseFigure();
            
            Gdiplus::Color pillBkColor;
            Gdiplus::Color pillBorderColor;
            float knobX;
            
            if (isChecked) {
                pillBkColor = Gdiplus::Color(245, 109, 40, 217);
                pillBorderColor = Gdiplus::Color(110, 196, 181, 253);
                knobX = switchX + switchW - switchH + 1.0f;
            } else {
                pillBkColor = isHovered ? Gdiplus::Color(205, 39, 39, 45) : Gdiplus::Color(175, 24, 24, 27);
                pillBorderColor = Gdiplus::Color(54, 255, 255, 255);
                knobX = switchX + 1.0f;
            }
            
            Gdiplus::SolidBrush pillBkBrush(pillBkColor);
            g.FillPath(&pillBkBrush, &pillPath);
            
            Gdiplus::Pen pillBorderPen(pillBorderColor, 1.0f);
            g.DrawPath(&pillBorderPen, &pillPath);
            
            float knobD = switchH - 2.0f;
            float knobY = switchY + 1.0f;
            Gdiplus::SolidBrush knobBrush(isChecked ? gdip_rgb(255, 255, 255) : gdip_rgb(156, 163, 175));
            g.FillEllipse(&knobBrush, knobX, knobY, knobD, knobD);
        }
        
        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);
        
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK StaticSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                    UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    UNREFERENCED_PARAMETER(uIdSubclass);
    UNREFERENCED_PARAMETER(dwRefData);
    if (uMsg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rect;
        GetClientRect(hwnd, &rect);
        int w = rect.right - rect.left;
        int h = rect.bottom - rect.top;
        
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
        
        const int id = GetDlgCtrlID(hwnd);
        if (id == IDC_VR_POSE || id == IDC_STATUS) {
            HBRUSH solid = CreateSolidBrush(kLuaToolsInputRef);
            RECT fullRect{0, 0, w, h};
            FillRect(memDC, &fullRect, solid);
            DeleteObject(solid);
        } else {
            HDC parentDC = GetDC(GetParent(hwnd));
            POINT pt{0, 0};
            MapWindowPoints(hwnd, GetParent(hwnd), &pt, 1);
            BitBlt(memDC, 0, 0, w, h, parentDC, pt.x, pt.y, SRCCOPY);
            ReleaseDC(GetParent(hwnd), parentDC);
        }
        
        {
            Gdiplus::Graphics g(memDC);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            
            wchar_t text[512]{};
            GetWindowTextW(hwnd, text, 512);
            
            if (id == IDC_VR_POSE || id == IDC_STATUS) {
                Gdiplus::Color bkColor(246, 13, 13, 17);
                Gdiplus::Color borderColor(54, 255, 255, 255);
                Gdiplus::SolidBrush bkBrush(bkColor);
                
                Gdiplus::GraphicsPath path;
                float r = 8.0f;
                path.AddArc(0.5f, 0.5f, r, r, 180, 90);
                path.AddArc(w - r - 1.5f, 0.5f, r, r, 270, 90);
                path.AddArc(w - r - 1.5f, h - r - 1.5f, r, r, 0, 90);
                path.AddArc(0.5f, h - r - 1.5f, r, r, 90, 90);
                path.CloseFigure();
                
                g.FillPath(&bkBrush, &path);
                Gdiplus::Pen pen(borderColor, 1.0f);
                g.DrawPath(&pen, &path);
                
                Gdiplus::FontFamily fontFamily(L"Segoe UI");
                Gdiplus::Font font(&fontFamily, id == IDC_VR_POSE ? 8.5f : 9.0f,
                                   Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                Gdiplus::StringFormat format;
                format.SetAlignment(id == IDC_STATUS ? Gdiplus::StringAlignmentNear : Gdiplus::StringAlignmentCenter);
                format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
                
                Gdiplus::SolidBrush textBrush(gdip_rgb(244, 244, 245));
                Gdiplus::RectF textRect(12.0f, 0.0f, static_cast<float>(w - 24), static_cast<float>(h));
                g.DrawString(text, -1, &font, textRect, &format, &textBrush);
            } else {
                Gdiplus::FontFamily fontFamily(L"Segoe UI");
                Gdiplus::Font font(&fontFamily, 10.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                Gdiplus::StringFormat format;
                format.SetAlignment(Gdiplus::StringAlignmentNear);
                format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                
                Gdiplus::SolidBrush textBrush(gdip_rgb(229, 231, 235));
                Gdiplus::RectF textRect(10.0f, 0.0f, static_cast<float>(w - 20), static_cast<float>(h));
                g.DrawString(text, -1, &font, textRect, &format, &textBrush);
            }
        }
        
        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);
        
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK ListboxSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                     UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    UNREFERENCED_PARAMETER(uIdSubclass);
    UNREFERENCED_PARAMETER(dwRefData);
    if (uMsg == WM_PAINT) {
        LRESULT res = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        
        HDC hdc = GetWindowDC(hwnd);
        RECT rect;
        GetWindowRect(hwnd, &rect);
        int w = rect.right - rect.left;
        int h = rect.bottom - rect.top;
        
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        
        Gdiplus::Pen borderPen(gdip_argb(55, 255, 255, 255), 1.2f);
        Gdiplus::GraphicsPath path;
        float r = 8.0f;
        path.AddArc(0.5f, 0.5f, r, r, 180, 90);
        path.AddArc(w - r - 1.5f, 0.5f, r, r, 270, 90);
        path.AddArc(w - r - 1.5f, h - r - 1.5f, r, r, 0, 90);
        path.AddArc(0.5f, h - r - 1.5f, r, r, 90, 90);
        path.CloseFigure();
        g.DrawPath(&borderPen, &path);

        if (SendMessageW(hwnd, LB_GETCOUNT, 0, 0) == 0) {
            Gdiplus::FontFamily fontFamily(L"Segoe UI");
            Gdiplus::Font font(&fontFamily, 9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentCenter);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::SolidBrush textBrush(gdip_argb(190, 156, 163, 175));
            Gdiplus::RectF textRect(8.0f, 0.0f, static_cast<float>(w - 16), static_cast<float>(h));
            g.DrawString(L"No player block detected. Run Scan player block.", -1, &font, textRect, &format, &textBrush);
        }
        
        ReleaseDC(hwnd, hdc);
        return res;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

bool advanced_options_enabled();

void update_tab_visibility() {
    const bool advanced = advanced_options_enabled();
    
    // Column 1 IDs
    int showTab1 = ((g_current_tab == 0 && advanced) || g_current_tab == 1) ? SW_SHOW : SW_HIDE;
    int col1_ids[] = {
        IDC_CURRENT_X, IDC_CURRENT_Y, IDC_CURRENT_Z, IDC_TOLERANCE,
        IDC_VR_FLOOR_TILT, IDC_VR_CALIBRATE_FLOOR
    };
    for (int id : col1_ids) ShowWindow(control(id), showTab1);
    
    // Candidates listbox only visible on Dashboard (0) and Player Settings (1)
    ShowWindow(control(IDC_CANDIDATES), (g_current_tab == 0 || g_current_tab == 1) ? SW_SHOW : SW_HIDE);
    
    // Column 2 IDs
    int showTab2 = ((g_current_tab == 0 && advanced) || g_current_tab == 2) ? SW_SHOW : SW_HIDE;
    int col2_ids[] = {
        IDC_VR_STEREO, IDC_VR_DISABLE_FOREGROUND_EFFECTS, IDC_VR_DISABLE_SHADOWS,
        IDC_VR_DISABLE_PARTICLES, IDC_VR_DISABLE_DISTORTION, IDC_VR_QUEST_LOCOMOTION,
        IDC_VR_HIDE_FIRST_PERSON_HAND, IDC_VR_IPD, IDC_VR_SEPARATION,
        IDC_VR_RENDER_SCALE
    };
    for (int id : col2_ids) ShowWindow(control(id), showTab2);
    
    // Column 3 IDs
    int showTab3 = ((g_current_tab == 0 && advanced) || g_current_tab == 3) ? SW_SHOW : SW_HIDE;
    int col3_ids[] = {
        IDC_VR_KEY_FORWARD, IDC_VR_KEY_BACKWARD, IDC_VR_KEY_LEFT, IDC_VR_KEY_RIGHT,
        IDC_VR_KEY_JUMP, IDC_VR_KEY_SPRINT, IDC_VR_KEY_USE, IDC_VR_KEY_INVENTORY
    };
    for (int id : col3_ids) ShowWindow(control(id), showTab3);
    
    // Column 4 IDs
    int showTab4 = ((g_current_tab == 0 && advanced) || g_current_tab == 4) ? SW_SHOW : SW_HIDE;
    int col4_ids[] = {
        IDC_VR_HAND_POINTER, IDC_VR_HIDE_RETICLE, IDC_VR_MENU_MOUSE, IDC_VR_NO_UBO_UI,
        IDC_VR_QUEST_DEADZONE, IDC_VR_TURN_SPEED, IDC_VR_POINTER_DISTANCE,
        IDC_VR_MENU_DISTANCE, IDC_VR_MENU_WIDTH, IDC_VR_UI_SCALE,
        IDC_VR_UI_EYE_OFFSET, IDC_VR_UI_Y_OFFSET, IDC_VR_MENU_IGNORE_DRAW
    };
    for (int id : col4_ids) ShowWindow(control(id), showTab4);
    
    // Always visible dashboard top bar / status bar
    ShowWindow(control(IDC_SCAN), SW_SHOW);
    ShowWindow(control(IDC_VR_CENTER), SW_SHOW);
    ShowWindow(control(IDC_VR_STOP), SW_SHOW);
    ShowWindow(control(IDC_VR_POSE), SW_SHOW);
    ShowWindow(control(IDC_STATUS), SW_SHOW);
    ShowWindow(control(IDC_VR_ADVANCED_OPTIONS), SW_SHOW);
    
    // Force repaint of the main window to update panels
    InvalidateRect(g_window, nullptr, TRUE);
}

void layout_controls_for_current_tab() {
    auto setPos = [](int id, int x, int y, int w, int h) {
        HWND hwnd = control(id);
        if (hwnd) SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    };

    if (g_current_tab == 0) {
        // Dashboard layout
        // Column 1
        setPos(IDC_CURRENT_X, 235, 304, 135, 24);
        setPos(IDC_CURRENT_Y, 235, 334, 135, 24);
        setPos(IDC_CURRENT_Z, 235, 364, 135, 24);
        setPos(IDC_TOLERANCE, 295, 394, 75, 24);
        setPos(IDC_VR_FLOOR_TILT, 295, 424, 75, 24);
        setPos(IDC_VR_CALIBRATE_FLOOR, 235, 456, 135, 24);
        
        // Candidates list box
        setPos(IDC_CANDIDATES, 215, 166, 845, 46);
        
        // Column 2
        setPos(IDC_VR_STEREO, 405, 304, 180, 24);
        setPos(IDC_VR_DISABLE_FOREGROUND_EFFECTS, 405, 330, 180, 24);
        setPos(IDC_VR_DISABLE_SHADOWS, 405, 356, 180, 24);
        setPos(IDC_VR_DISABLE_PARTICLES, 405, 382, 180, 24);
        setPos(IDC_VR_DISABLE_DISTORTION, 405, 408, 180, 24);
        setPos(IDC_VR_QUEST_LOCOMOTION, 405, 434, 180, 24);
        setPos(IDC_VR_HIDE_FIRST_PERSON_HAND, 405, 460, 180, 24);
        setPos(IDC_VR_IPD, 515, 500, 70, 24);
        setPos(IDC_VR_SEPARATION, 515, 528, 70, 24);
        setPos(IDC_VR_RENDER_SCALE, 515, 556, 70, 24);
        
        // Column 3
        setPos(IDC_VR_KEY_FORWARD, 685, 328, 75, 24);
        setPos(IDC_VR_KEY_BACKWARD, 685, 354, 75, 24);
        setPos(IDC_VR_KEY_LEFT, 685, 380, 75, 24);
        setPos(IDC_VR_KEY_RIGHT, 685, 406, 75, 24);
        setPos(IDC_VR_KEY_JUMP, 685, 432, 75, 24);
        setPos(IDC_VR_KEY_SPRINT, 685, 458, 75, 24);
        setPos(IDC_VR_KEY_USE, 685, 484, 75, 24);
        setPos(IDC_VR_KEY_INVENTORY, 685, 510, 75, 24);
        
        // Column 4
        setPos(IDC_VR_HAND_POINTER, 815, 304, 220, 24);
        setPos(IDC_VR_HIDE_RETICLE, 815, 330, 220, 24);
        setPos(IDC_VR_MENU_MOUSE, 815, 356, 220, 24);
        setPos(IDC_VR_NO_UBO_UI, 815, 382, 220, 24);
        
        setPos(IDC_VR_QUEST_DEADZONE, 815, 424, 95, 24);
        setPos(IDC_VR_TURN_SPEED, 925, 424, 95, 24);
        setPos(IDC_VR_POINTER_DISTANCE, 815, 476, 95, 24);
        
        setPos(IDC_VR_MENU_DISTANCE, 925, 476, 95, 24);
        setPos(IDC_VR_MENU_WIDTH, 815, 528, 95, 24);
        setPos(IDC_VR_UI_SCALE, 925, 528, 95, 24);
        
        setPos(IDC_VR_UI_EYE_OFFSET, 815, 580, 95, 24);
        setPos(IDC_VR_UI_Y_OFFSET, 925, 580, 95, 24);
        
        setPos(IDC_VR_MENU_IGNORE_DRAW, 815, 632, 95, 24);
    } 
    else if (g_current_tab == 1) {
        // Player Settings layout
        setPos(IDC_CANDIDATES, 215, 50, 765, 60);
        
        setPos(IDC_CURRENT_X, 380, 175, 200, 24);
        setPos(IDC_CURRENT_Y, 380, 205, 200, 24);
        setPos(IDC_CURRENT_Z, 380, 235, 200, 24);
        setPos(IDC_TOLERANCE, 380, 265, 200, 24);
        setPos(IDC_VR_FLOOR_TILT, 380, 295, 90, 24);
        setPos(IDC_VR_CALIBRATE_FLOOR, 480, 295, 100, 24);
    } 
    else if (g_current_tab == 2) {
        // Rendering layout
        setPos(IDC_VR_STEREO, 250, 180, 220, 20);
        setPos(IDC_VR_DISABLE_FOREGROUND_EFFECTS, 250, 210, 220, 20);
        setPos(IDC_VR_DISABLE_SHADOWS, 250, 240, 220, 20);
        setPos(IDC_VR_DISABLE_PARTICLES, 250, 270, 220, 20);
        setPos(IDC_VR_DISABLE_DISTORTION, 250, 300, 220, 20);
        setPos(IDC_VR_QUEST_LOCOMOTION, 250, 330, 220, 20);
        setPos(IDC_VR_HIDE_FIRST_PERSON_HAND, 250, 360, 220, 20);
        setPos(IDC_VR_IPD, 380, 508, 150, 24);
        setPos(IDC_VR_SEPARATION, 380, 538, 150, 24);
        setPos(IDC_VR_RENDER_SCALE, 380, 568, 150, 24);
    } 
    else if (g_current_tab == 3) {
        // Keybindings layout
        setPos(IDC_VR_KEY_FORWARD, 380, 68, 150, 24);
        setPos(IDC_VR_KEY_BACKWARD, 380, 98, 150, 24);
        setPos(IDC_VR_KEY_LEFT, 380, 128, 150, 24);
        setPos(IDC_VR_KEY_RIGHT, 380, 158, 150, 24);
        setPos(IDC_VR_KEY_JUMP, 380, 188, 150, 24);
        setPos(IDC_VR_KEY_SPRINT, 380, 218, 150, 24);
        setPos(IDC_VR_KEY_USE, 380, 248, 150, 24);
        setPos(IDC_VR_KEY_INVENTORY, 380, 278, 150, 24);
    } 
    else if (g_current_tab == 4) {
        // UI/Pointer layout
        setPos(IDC_VR_HAND_POINTER, 250, 180, 220, 20);
        setPos(IDC_VR_HIDE_RETICLE, 250, 210, 220, 20);
        setPos(IDC_VR_MENU_MOUSE, 250, 240, 220, 20);
        setPos(IDC_VR_NO_UBO_UI, 250, 270, 220, 20);
        
        setPos(IDC_VR_QUEST_DEADZONE, 380, 372, 50, 24);
        setPos(IDC_VR_TURN_SPEED, 440, 372, 55, 24);
        setPos(IDC_VR_POINTER_DISTANCE, 505, 372, 50, 24);
        
        setPos(IDC_VR_MENU_DISTANCE, 380, 420, 50, 24);
        setPos(IDC_VR_MENU_WIDTH, 440, 420, 55, 24);
        setPos(IDC_VR_UI_SCALE, 505, 420, 50, 24);
        
        setPos(IDC_VR_UI_EYE_OFFSET, 380, 468, 80, 24);
        setPos(IDC_VR_UI_Y_OFFSET, 470, 468, 85, 24);
        
        setPos(IDC_VR_MENU_IGNORE_DRAW, 380, 516, 80, 24);
    }
}

bool advanced_options_enabled() {
    return IsDlgButtonChecked(g_window, IDC_VR_ADVANCED_OPTIONS) == BST_CHECKED;
}

void update_advanced_visibility() {
    update_tab_visibility();
}
#endif

void set_status(const std::wstring& text) {
    HWND status = control(IDC_STATUS);
    SetWindowTextW(status, text.c_str());
    if (status) {
        InvalidateRect(status, nullptr, TRUE);
        UpdateWindow(status);
    }
}

std::wstring get_text(int id) {
    wchar_t buffer[128]{};
    GetWindowTextW(control(id), buffer, static_cast<int>(std::size(buffer)));
    return buffer;
}

float get_float(int id, float fallback = 0.0f) {
    const std::wstring text = get_text(id);
    wchar_t* end = nullptr;
    const float value = static_cast<float>(wcstod(text.c_str(), &end));
    return end == text.c_str() ? fallback : value;
}

void set_float(int id, float value) {
    wchar_t buffer[64]{};
    swprintf_s(buffer, L"%.3f", value);
    SetWindowTextW(control(id), buffer);
}

#ifdef HYTALE_CAMERA_MODE
#endif

DWORD find_hytale() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD pid = 0;
    ULARGE_INTEGER newest_creation{};
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"HytaleClient.exe") == 0) {
                HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                             entry.th32ProcessID);
                FILETIME creation{}, exit{}, kernel{}, user{};
                if (process && GetProcessTimes(process, &creation, &exit, &kernel, &user)) {
                    ULARGE_INTEGER candidate{};
                    candidate.LowPart = creation.dwLowDateTime;
                    candidate.HighPart = creation.dwHighDateTime;
                    if (!pid || candidate.QuadPart > newest_creation.QuadPart) {
                        newest_creation = candidate;
                        pid = entry.th32ProcessID;
                    }
                }
                if (process) CloseHandle(process);
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return pid;
}

void detach() {
    if (g_process) CloseHandle(g_process);
    g_process = nullptr;
    g_pid = 0;
    g_candidates.clear();
    SendMessageW(control(IDC_CANDIDATES), LB_RESETCONTENT, 0, 0);
}

bool attach() {
    const DWORD pid = find_hytale();
    if (!pid) {
        detach();
        set_status(L"HytaleClient.exe not found. Start the game, then scan again.");
        return false;
    }
    if (g_process && pid == g_pid) return true;

    detach();
    g_process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ |
                            PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, pid);
    if (!g_process) {
        set_status(L"Process access denied. Try running the dashboard as administrator.");
        return false;
    }
    g_pid = pid;
    set_status(L"Attached to HytaleClient.exe, PID " + std::to_wstring(pid) + L".");
    return true;
}

bool readable_writable(const MEMORY_BASIC_INFORMATION& mbi) {
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const DWORD protection = mbi.Protect & 0xff;
    return protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
           protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

bool parse_feet_text(const wchar_t* text, Vec3& value) {
    if (!text) return false;
    const int parsed = swscanf_s(text, L"Feet (%f, %f, %f)",
                                 &value.x, &value.y, &value.z);
    return parsed == 3 &&
           std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
           std::fabs(value.x) < 1000000.0f && std::fabs(value.y) < 1000000.0f &&
           std::fabs(value.z) < 1000000.0f;
}

bool read_f7_feet(HANDLE process, Vec3& result, uintptr_t& allocation_base,
                  const std::atomic_bool* cancel = nullptr) {
    if (!process) return false;
    constexpr wchar_t prefix[] = L"Feet (";
    constexpr size_t prefix_chars = std::size(prefix) - 1;
    constexpr size_t max_text_chars = 64;
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    uintptr_t address = allocation_base != 0
        ? allocation_base
        : reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
    const uintptr_t maximum = reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
    std::vector<unsigned char> buffer;
    std::vector<FeetReading> readings;
    uintptr_t feet_allocation = allocation_base;

    while (address < maximum) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return false;
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQueryEx(process, reinterpret_cast<const void*>(address), &mbi, sizeof(mbi))) break;
        const uintptr_t region = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const uintptr_t region_end = region + mbi.RegionSize;
        const uintptr_t allocation = reinterpret_cast<uintptr_t>(mbi.AllocationBase);
        if (feet_allocation != 0 && allocation != feet_allocation) {
            if (region > feet_allocation) break;
            address = region_end;
            continue;
        }

        if (readable_writable(mbi) && mbi.Type == MEM_PRIVATE) {
            for (uintptr_t cursor = region; cursor < region_end;) {
                if (cancel && cancel->load(std::memory_order_relaxed)) return false;
                const size_t payload = static_cast<size_t>(
                    std::min<uintptr_t>(kScanChunk, region_end - cursor));
                const size_t overlap = max_text_chars * sizeof(wchar_t);
                const size_t read_size = static_cast<size_t>(
                    std::min<uintptr_t>(payload + overlap, region_end - cursor));
                buffer.resize(read_size);
                SIZE_T bytes_read = 0;
                if (ReadProcessMemory(process, reinterpret_cast<const void*>(cursor),
                                      buffer.data(), read_size, &bytes_read)) {
                    const size_t usable = std::min<size_t>(bytes_read, read_size);
                    const unsigned char* search = buffer.data();
                    const unsigned char* payload_end = buffer.data() + std::min(payload, usable);
                    while (search < payload_end) {
                        const auto* found = static_cast<const unsigned char*>(std::memchr(
                            search, static_cast<unsigned char>(L'F'),
                            static_cast<size_t>(payload_end - search)));
                        if (!found) break;
                        const size_t offset = static_cast<size_t>(found - buffer.data());
                        search = found + 1;
                        if (((cursor + offset) & 1) != 0 ||
                            offset + prefix_chars * sizeof(wchar_t) > usable ||
                            std::memcmp(buffer.data() + offset, prefix,
                                        prefix_chars * sizeof(wchar_t)) != 0) continue;

                        wchar_t text[max_text_chars]{};
                        const size_t available_chars = std::min(
                            max_text_chars - 1, (usable - offset) / sizeof(wchar_t));
                        std::memcpy(text, buffer.data() + offset,
                                    available_chars * sizeof(wchar_t));
                        Vec3 value{};
                        if (!parse_feet_text(text, value)) continue;

                        auto existing = std::find_if(readings.begin(), readings.end(),
                            [&](const FeetReading& reading) {
                                return std::fabs(reading.value.x - value.x) <= 0.0051f &&
                                       std::fabs(reading.value.y - value.y) <= 0.0051f &&
                                       std::fabs(reading.value.z - value.z) <= 0.0051f;
                            });
                        if (existing != readings.end()) {
                            ++existing->copies;
                            existing->latest_address = std::max(
                                existing->latest_address, cursor + offset);
                        } else {
                            readings.push_back({value, 1, cursor + offset});
                        }
                        if (feet_allocation == 0) feet_allocation = allocation;
                    }
                }
                cursor += payload;
            }
        }
        if (region_end <= address) break;
        address = region_end;
    }

    if (readings.empty()) return false;
    const auto best = std::max_element(readings.begin(), readings.end(),
        [](const FeetReading& left, const FeetReading& right) {
            if (left.copies != right.copies) return left.copies < right.copies;
            return left.latest_address < right.latest_address;
        });
    result = best->value;
    MEMORY_BASIC_INFORMATION feet_region{};
    if (VirtualQueryEx(process, reinterpret_cast<const void*>(best->latest_address),
                       &feet_region, sizeof(feet_region))) {
        allocation_base = reinterpret_cast<uintptr_t>(feet_region.AllocationBase);
    }
    return true;
}

bool close_vec(const Vec3& a, const Vec3& b, float tolerance) {
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z) &&
           std::fabs(a.x - b.x) <= tolerance &&
           std::fabs(a.y - b.y) <= tolerance &&
           std::fabs(a.z - b.z) <= tolerance;
}

bool read_exact(HANDLE process, uintptr_t address, void* output, size_t size) {
    SIZE_T read = 0;
    return process && ReadProcessMemory(process, reinterpret_cast<const void*>(address), output, size, &read) &&
           read == size;
}

bool read_exact(uintptr_t address, void* output, size_t size) {
    return read_exact(g_process, address, output, size);
}

bool write_exact(uintptr_t address, const void* input, size_t size) {
    SIZE_T written = 0;
    return WriteProcessMemory(g_process, reinterpret_cast<void*>(address), input, size, &written) &&
           written == size;
}

void add_candidate(std::vector<Candidate>& candidates, uintptr_t base, const Vec3 values[4]) {
    if (std::any_of(candidates.begin(), candidates.end(),
                    [base](const Candidate& item) { return item.base == base; })) return;

    Candidate candidate{};
    candidate.base = base;
    std::copy(values, values + 4, candidate.original);
    candidates.push_back(candidate);
}

void keep_latest_candidate(std::vector<Candidate>& candidates) {
    if (candidates.size() <= 1) return;
    Candidate latest = candidates.back();
    candidates.clear();
    candidates.push_back(latest);
}

void populate_candidate_list() {
    SendMessageW(control(IDC_CANDIDATES), LB_RESETCONTENT, 0, 0);
    for (const Candidate& candidate : g_candidates) {
        const Vec3& value = candidate.original[2];
        wchar_t label[192]{};
#ifdef HYTALE_CAMERA_MODE
        if (advanced_options_enabled()) {
            swprintf_s(label, L"Latest player block: 0x%llX    X %.3f    Y %.3f    Z %.3f",
                       static_cast<unsigned long long>(candidate.base), value.x, value.y, value.z);
        } else {
            swprintf_s(label, L"Latest player coordinate block");
        }
#else
        swprintf_s(label, L"0x%llX    X %.3f    Y %.3f    Z %.3f",
                   static_cast<unsigned long long>(candidate.base), value.x, value.y, value.z);
#endif
        SendMessageW(control(IDC_CANDIDATES), LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    }
}

std::vector<Candidate> find_candidates(HANDLE process, const Vec3& expected, float tolerance,
                                       uintptr_t f7_allocation_base,
                                       const std::atomic_bool* cancel = nullptr) {
    std::vector<Candidate> candidates;
    if (!process) return candidates;
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    uintptr_t address = reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
    const uintptr_t maximum = reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
    std::vector<unsigned char> buffer;

    unsigned char signature[sizeof(Vec3)]{};
    unsigned char signature_mask[sizeof(Vec3)]{};
    const Vec3 lower{expected.x - tolerance, expected.y - tolerance, expected.z - tolerance};
    const Vec3 upper{expected.x + tolerance, expected.y + tolerance, expected.z + tolerance};
    unsigned char lower_bytes[sizeof(Vec3)]{};
    unsigned char upper_bytes[sizeof(Vec3)]{};
    std::memcpy(signature, &expected, sizeof(signature));
    std::memcpy(lower_bytes, &lower, sizeof(lower_bytes));
    std::memcpy(upper_bytes, &upper, sizeof(upper_bytes));
    for (size_t i = 0; i < sizeof(signature); ++i) {
        signature_mask[i] = lower_bytes[i] == upper_bytes[i] ? 0xff : 0x00;
    }

    size_t anchor_offset = 0;
    size_t anchor_size = 0;
    for (size_t i = 0; i < sizeof(signature);) {
        if (!signature_mask[i]) {
            ++i;
            continue;
        }
        const size_t start = i;
        while (i < sizeof(signature) && signature_mask[i]) ++i;
        if (i - start >= anchor_size) {
            anchor_offset = start;
            anchor_size = i - start;
        }
    }
    if (anchor_size == 0) return candidates;

    // Hytale places the coordinate object near the start of a large committed
    // arena. Scan the first 2 MiB of large arenas first, then fall back to the
    // complete address space only if the fast pass found nothing.
    for (int pass = 0; pass < 2 && candidates.empty(); ++pass) {
        address = reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
        while (address < maximum && candidates.size() < 128) {
            if (cancel && cancel->load(std::memory_order_relaxed)) return {};
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQueryEx(process, reinterpret_cast<const void*>(address), &mbi, sizeof(mbi))) break;
            const uintptr_t region = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            const uintptr_t region_end = region + mbi.RegionSize;
            const bool fast_region = mbi.RegionSize >= 128 * 1024 * 1024;
            const uintptr_t scan_end = pass == 0
                ? std::min<uintptr_t>(region_end, region + 2 * 1024 * 1024)
                : region_end;

            const bool same_f7_arena = f7_allocation_base != 0 &&
                reinterpret_cast<uintptr_t>(mbi.AllocationBase) == f7_allocation_base;
            if (readable_writable(mbi) && mbi.Type == MEM_PRIVATE &&
                (pass != 0 || (fast_region && same_f7_arena))) {
                for (uintptr_t cursor = region; cursor < scan_end;) {
                    if (cancel && cancel->load(std::memory_order_relaxed)) return {};
                    const size_t payload = static_cast<size_t>(std::min<uintptr_t>(kScanChunk, scan_end - cursor));
                    const size_t read_size = static_cast<size_t>(std::min<uintptr_t>(
                        payload + kCoordinateOffsets[3] + sizeof(Vec3), scan_end - cursor));
                    buffer.resize(read_size);
                    SIZE_T bytes_read = 0;
                    if (ReadProcessMemory(process, reinterpret_cast<const void*>(cursor), buffer.data(),
                                          read_size, &bytes_read)) {
                    const size_t usable = std::min<size_t>(bytes_read, read_size);
                    const unsigned char* search = buffer.data();
                    const unsigned char* end = buffer.data() + usable;
                    while (search + anchor_size <= end) {
                        const auto* found = static_cast<const unsigned char*>(std::memchr(
                            search, signature[anchor_offset], static_cast<size_t>(end - search)));
                        if (!found) break;
                        if (static_cast<size_t>(end - found) < anchor_size ||
                            std::memcmp(found, signature + anchor_offset, anchor_size) != 0) {
                            search = found + 1;
                            continue;
                        }
                        if (found >= buffer.data() + anchor_offset) {
                            const size_t vector_offset = static_cast<size_t>(
                                found - buffer.data() - anchor_offset);
                            const uintptr_t vector_address = cursor + vector_offset;
                            bool signature_matches = vector_offset + sizeof(Vec3) <= usable &&
                                                     (vector_address & 3) == 0;
                            for (size_t i = 0; signature_matches && i < sizeof(Vec3); ++i) {
                                if (signature_mask[i] &&
                                    buffer[vector_offset + i] != signature[i]) {
                                    signature_matches = false;
                                }
                            }
                            Vec3 live{};
                            if (signature_matches) {
                                std::memcpy(&live, buffer.data() + vector_offset, sizeof(live));
                                signature_matches = close_vec(live, expected, tolerance);
                            }
                            if (signature_matches && vector_address >= kCoordinateOffsets[2]) {
                                const uintptr_t base = vector_address - kCoordinateOffsets[2];
                                Vec3 values[4]{};
                                size_t matching_copies = 0;
                                for (size_t i = 0; i < std::size(kCoordinateOffsets); ++i) {
                                    if (read_exact(process, base + kCoordinateOffsets[i],
                                                   &values[i], sizeof(Vec3)) &&
                                        close_vec(values[i], expected, tolerance)) {
                                        ++matching_copies;
                                    }
                                }
                                if (matching_copies >= 3) add_candidate(candidates, base, values);
                            }
                        }
                        search = found + 1;
                    }
                    }
                    cursor += payload;
                }
            }
            if (region_end <= address) break;
            address = region_end;
        }
    }

    return candidates;
}

void scan_candidates() {
    if (!attach()) return;
    const Vec3 expected{get_float(IDC_CURRENT_X), get_float(IDC_CURRENT_Y), get_float(IDC_CURRENT_Z)};
    const float tolerance = std::clamp(get_float(IDC_TOLERANCE, 0.10f), 0.001f, 10.0f);
    EnableWindow(control(IDC_SCAN), FALSE);
    set_status(L"Searching memory for the coordinate block...");
    UpdateWindow(g_window);
    g_candidates = find_candidates(g_process, expected, tolerance, g_f7_allocation_base);
    keep_latest_candidate(g_candidates);
    populate_candidate_list();
    EnableWindow(control(IDC_SCAN), TRUE);
    if (g_candidates.empty()) {
        set_status(L"No block found. Refresh the coordinates and increase tolerance slightly.");
    } else {
        SendMessageW(control(IDC_CANDIDATES), LB_SETCURSEL, 0, 0);
        set_status(L"Latest coordinate block found.");
    }
}

#ifdef HYTALE_CAMERA_MODE
DWORD WINAPI f7_scan_worker_impl(void* parameter) {
    auto* work = static_cast<ScanWork*>(parameter);
    auto* result = new ScanResult{};
    if (!g_scan_cancel.load(std::memory_order_relaxed)) {
        result->f7_allocation_base = work->f7_allocation_base;
        result->feet_found = read_f7_feet(work->process, result->feet,
                                          result->f7_allocation_base, &g_scan_cancel);
        if (!result->feet_found && work->f7_allocation_base != 0 &&
            !g_scan_cancel.load(std::memory_order_relaxed)) {
            result->f7_allocation_base = 0;
            result->feet_found = read_f7_feet(work->process, result->feet,
                                              result->f7_allocation_base, &g_scan_cancel);
        }
    }
    if (result->feet_found && !g_scan_cancel.load(std::memory_order_relaxed)) {
        for (const Candidate& known : work->known_candidates) {
            Vec3 values[4]{};
            size_t matches = 0;
            for (size_t i = 0; i < std::size(kCoordinateOffsets); ++i) {
                if (read_exact(work->process, known.base + kCoordinateOffsets[i],
                               &values[i], sizeof(Vec3)) &&
                    close_vec(values[i], result->feet, work->tolerance)) {
                    ++matches;
                }
            }
            if (matches >= 3) add_candidate(result->candidates, known.base, values);
        }
        if (result->candidates.empty()) {
            result->candidates = find_candidates(work->process, result->feet, work->tolerance,
                                                 result->f7_allocation_base, &g_scan_cancel);
        }
        keep_latest_candidate(result->candidates);
        result->status = result->candidates.empty()
            ? L"F7 feet were read, but no valid player block was found."
            : L"Latest player block found. Click Center VR to start.";
    } else if (!g_scan_cancel.load(std::memory_order_relaxed)) {
        result->status = L"F7 feet text not found. Show F7 in game, then scan again.";
    }
    CloseHandle(work->process);
    delete work;

    if (g_scan_cancel.load(std::memory_order_relaxed)) {
        delete result;
    } else {
        ScanResult* previous = g_pending_scan_result.exchange(result);
        delete previous;
        PostMessageW(g_window, kScanCompleteMessage, 0, 0);
    }
    g_f7_scan_busy.store(false, std::memory_order_release);
    return 0;
}

void start_f7_scan() {
    if (g_f7_scan_busy.exchange(true, std::memory_order_acq_rel)) {
        set_status(L"A scan is already running...");
        return;
    }
    if (!attach()) {
        g_f7_scan_busy.store(false, std::memory_order_release);
        return;
    }
    if (g_scan_thread) {
        CloseHandle(g_scan_thread);
        g_scan_thread = nullptr;
    }

    HANDLE process_copy = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), g_process, GetCurrentProcess(), &process_copy,
                         0, FALSE, DUPLICATE_SAME_ACCESS)) {
        g_f7_scan_busy.store(false, std::memory_order_release);
        set_status(L"Could not duplicate the Hytale process handle.");
        return;
    }
    auto* work = new ScanWork{process_copy,
        std::clamp(get_float(IDC_TOLERANCE, 0.10f), 0.001f, 10.0f),
        g_f7_allocation_base, g_candidates};
    g_scan_cancel.store(false, std::memory_order_release);
    EnableWindow(control(IDC_ATTACH), FALSE);
    EnableWindow(control(IDC_SCAN), FALSE);
    set_status(L"Reading F7, then searching memory for the player block...");
    g_scan_thread = CreateThread(nullptr, 0, f7_scan_worker_impl, work, 0, nullptr);
    if (!g_scan_thread) {
        CloseHandle(process_copy);
        delete work;
        g_f7_scan_busy.store(false, std::memory_order_release);
        EnableWindow(control(IDC_ATTACH), TRUE);
        EnableWindow(control(IDC_SCAN), TRUE);
        set_status(L"Could not start the scan.");
    }
}
#endif

int selected_candidate() {
    return static_cast<int>(SendMessageW(control(IDC_CANDIDATES), LB_GETCURSEL, 0, 0));
}

bool write_target() {
    const int selected = selected_candidate();
    if (!g_process || selected < 0 || static_cast<size_t>(selected) >= g_candidates.size()) return false;
    const uintptr_t base = g_candidates[selected].base;
    bool success = true;
    for (uintptr_t offset : kCoordinateOffsets) {
        success = write_exact(base + offset, &g_target, sizeof(g_target)) && success;
    }
    return success;
}

#ifdef HYTALE_CAMERA_MODE
bool read_hmd_pose(Vec3& position, float rotation[9]) {
    if (!g_vr_system) return false;
    vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
    g_vr_system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0.0f,
                                                  poses, vr::k_unMaxTrackedDeviceCount);
    const auto& hmd = poses[vr::k_unTrackedDeviceIndex_Hmd];
    if (!hmd.bDeviceIsConnected || !hmd.bPoseIsValid) return false;
    position = {hmd.mDeviceToAbsoluteTracking.m[0][3],
                hmd.mDeviceToAbsoluteTracking.m[1][3],
                hmd.mDeviceToAbsoluteTracking.m[2][3]};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            rotation[row * 3 + column] = hmd.mDeviceToAbsoluteTracking.m[row][column];
        }
    }
    return true;
}

float hmd_yaw_from_rotation(const float rotation[9]) {
    const float forward_x = -rotation[2];
    const float forward_z = -rotation[8];
    return std::atan2(forward_x, -forward_z);
}

float hmd_pitch_degrees_from_rotation(const float rotation[9]) {
    const float forward_x = -rotation[2];
    const float forward_y = -rotation[5];
    const float forward_z = -rotation[8];
    const float horizontal = std::sqrt(forward_x * forward_x + forward_z * forward_z);
    constexpr float radians_to_degrees = 57.2957795130823208768f;
    return std::atan2(forward_y, horizontal) * radians_to_degrees;
}

float normalize_angle(float radians) {
    constexpr float pi = 3.14159265358979323846f;
    while (radians > pi) radians -= 2.0f * pi;
    while (radians < -pi) radians += 2.0f * pi;
    return radians;
}

bool update_head_origin_yaw() {
    Vec3 hmd{};
    float rotation[9]{};
    if (!read_hmd_pose(hmd, rotation)) return false;
    g_vr_head_origin_yaw = hmd_yaw_from_rotation(rotation);
    g_vr_head_origin_yaw_valid = true;
    g_vr_last_head_sync_yaw = g_vr_head_origin_yaw;
    g_vr_last_head_sync_yaw_valid = true;
    g_head_turn_mouse_remainder = 0.0f;
    return true;
}

bool head_based_locomotion_axes(float& x, float& y) {
    if (!g_vr_head_origin_yaw_valid) return false;
    Vec3 hmd{};
    float rotation[9]{};
    if (!read_hmd_pose(hmd, rotation)) return false;
    const float yaw = hmd_yaw_from_rotation(rotation) - g_vr_head_origin_yaw;
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    const float raw_x = x;
    const float raw_y = y;
    x = raw_x * c + raw_y * s;
    y = raw_y * c - raw_x * s;
    return true;
}

bool read_player_position(Vec3& position) {
    const int selected = selected_candidate();
    if (!g_process || selected < 0 ||
        static_cast<size_t>(selected) >= g_candidates.size()) return false;
    return read_exact(g_candidates[selected].base + 0x288, &position, sizeof(position));
}

bool hytale_has_input_focus() {
    DWORD foreground_pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground_pid);
    return foreground_pid != 0 && foreground_pid == g_pid;
}

struct HytaleWindowSearch {
    DWORD pid = 0;
    HWND window = nullptr;
    LONG64 area = 0;
};

BOOL CALLBACK enum_hytale_windows(HWND window, LPARAM data) {
    auto* search = reinterpret_cast<HytaleWindowSearch*>(data);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != search->pid || !IsWindowVisible(window) || GetWindow(window, GW_OWNER)) {
        return TRUE;
    }
    RECT client{};
    if (!GetClientRect(window, &client)) return TRUE;
    const LONG width = client.right - client.left;
    const LONG height = client.bottom - client.top;
    if (width <= 0 || height <= 0) return TRUE;
    const LONG64 area = static_cast<LONG64>(width) * height;
    if (area > search->area) {
        search->area = area;
        search->window = window;
    }
    return TRUE;
}

HWND hytale_window() {
    if (!g_pid) return nullptr;
    HytaleWindowSearch search{};
    search.pid = g_pid;
    EnumWindows(enum_hytale_windows, reinterpret_cast<LPARAM>(&search));
    return search.window;
}

HKL hytale_keyboard_layout() {
    HWND window = hytale_window();
    DWORD thread_id = 0;
    if (window) {
        thread_id = GetWindowThreadProcessId(window, nullptr);
    }
    HKL layout = GetKeyboardLayout(thread_id);
    return layout ? layout : GetKeyboardLayout(0);
}

WORD scan_code_for_virtual_key(WORD virtual_key) {
    UINT scan_code = MapVirtualKeyExW(virtual_key, MAPVK_VK_TO_VSC, hytale_keyboard_layout());
    if (scan_code == 0) {
        scan_code = MapVirtualKeyW(virtual_key, MAPVK_VK_TO_VSC);
    }
    return static_cast<WORD>(scan_code);
}

WORD virtual_key_for_physical_scan(UINT scan_code, WORD fallback) {
    const UINT virtual_key =
        MapVirtualKeyExW(scan_code, MAPVK_VSC_TO_VK_EX, hytale_keyboard_layout());
    return static_cast<WORD>(virtual_key != 0 ? virtual_key : fallback);
}

struct MovementKeys {
    WORD forward = 'W';
    WORD backward = 'S';
    WORD left = 'A';
    WORD right = 'D';
};

MovementKeys movement_keys_for_current_layout() {
    // Physical QWERTY WASD positions. On AZERTY these resolve to ZQSD.
    constexpr UINT kPhysicalW = 0x11;
    constexpr UINT kPhysicalA = 0x1E;
    constexpr UINT kPhysicalS = 0x1F;
    constexpr UINT kPhysicalD = 0x20;
    return {
        virtual_key_for_physical_scan(kPhysicalW, 'W'),
        virtual_key_for_physical_scan(kPhysicalS, 'S'),
        virtual_key_for_physical_scan(kPhysicalA, 'A'),
        virtual_key_for_physical_scan(kPhysicalD, 'D'),
    };
}

std::wstring trim_key_text(std::wstring text) {
    while (!text.empty() && iswspace(text.front())) {
        text.erase(text.begin());
    }
    while (!text.empty() && iswspace(text.back())) {
        text.pop_back();
    }
    return text;
}

std::wstring upper_key_text(std::wstring text) {
    for (wchar_t& ch : text) {
        ch = static_cast<wchar_t>(towupper(ch));
    }
    return text;
}

WORD parse_key_name(const std::wstring& raw_text, WORD fallback) {
    const std::wstring text = upper_key_text(trim_key_text(raw_text));
    if (text.empty() || text == L"AUTO" || text == L"-") return fallback;
    if (text == L"SPACE" || text == L"ESPACE") return VK_SPACE;
    if (text == L"SHIFT" || text == L"MAJ") return VK_SHIFT;
    if (text == L"CTRL" || text == L"CONTROL") return VK_CONTROL;
    if (text == L"ALT") return VK_MENU;
    if (text == L"TAB") return VK_TAB;
    if (text == L"ENTER" || text == L"RETURN") return VK_RETURN;
    if (text == L"ESC" || text == L"ESCAPE") return VK_ESCAPE;
    if (text == L"MOUSE3" || text == L"MIDDLE") return 0;
    if (text.size() >= 2 && text[0] == L'F') {
        wchar_t* end = nullptr;
        const long f_key = wcstol(text.c_str() + 1, &end, 10);
        if (end && *end == L'\0' && f_key >= 1 && f_key <= 24) {
            return static_cast<WORD>(VK_F1 + f_key - 1);
        }
    }
    if (text.size() == 1) {
        const wchar_t ch = text[0];
        if ((ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9')) {
            return static_cast<WORD>(ch);
        }
        const SHORT mapped = VkKeyScanExW(ch, hytale_keyboard_layout());
        if (mapped != -1) return static_cast<WORD>(mapped & 0xff);
    }
    return fallback;
}

WORD key_override(int control_id, WORD fallback) {
    return parse_key_name(get_text(control_id), fallback);
}

bool focus_hytale_window() {
    HWND window = hytale_window();
    if (!window) return false;

    if (IsIconic(window)) {
        ShowWindow(window, SW_RESTORE);
    } else {
        ShowWindow(window, SW_SHOW);
    }

    DWORD foreground_thread = 0;
    HWND foreground = GetForegroundWindow();
    if (foreground) {
        foreground_thread = GetWindowThreadProcessId(foreground, nullptr);
    }
    const DWORD current_thread = GetCurrentThreadId();
    const DWORD target_thread = GetWindowThreadProcessId(window, nullptr);
    const bool attached_foreground = foreground_thread != 0 &&
        foreground_thread != current_thread &&
        AttachThreadInput(current_thread, foreground_thread, TRUE) != FALSE;
    const bool attached_target = target_thread != 0 &&
        target_thread != current_thread &&
        AttachThreadInput(current_thread, target_thread, TRUE) != FALSE;

    AllowSetForegroundWindow(g_pid);
    BringWindowToTop(window);
    SetActiveWindow(window);
    const bool focused = SetForegroundWindow(window) != FALSE;
    SetFocus(window);

    if (attached_target) AttachThreadInput(current_thread, target_thread, FALSE);
    if (attached_foreground) AttachThreadInput(current_thread, foreground_thread, FALSE);
    return focused || hytale_has_input_focus();
}

void set_injected_key(InjectedKey& key, WORD virtual_key, bool down) {
    auto send = [](WORD scan_code, bool key_down) {
        if (scan_code == 0) return false;
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = scan_code;
        input.ki.dwFlags = KEYEVENTF_SCANCODE;
        if (!key_down) input.ki.dwFlags |= KEYEVENTF_KEYUP;
        return SendInput(1, &input, sizeof(input)) == 1;
    };

    const WORD scan_code = scan_code_for_virtual_key(virtual_key);
    if (key.down && (key.virtual_key != virtual_key || key.scan_code != scan_code)) {
        if (!send(key.scan_code, false)) return;
        key.down = false;
    }
    if (virtual_key == 0 || scan_code == 0) return;
    key.virtual_key = virtual_key;
    key.scan_code = scan_code;
    if (key.down == down) return;
    if (send(scan_code, down)) key.down = down;
}

void release_quest_keys() {
    set_injected_key(g_injected_keys.left, g_injected_keys.left.virtual_key, false);
    set_injected_key(g_injected_keys.right, g_injected_keys.right.virtual_key, false);
    set_injected_key(g_injected_keys.forward, g_injected_keys.forward.virtual_key, false);
    set_injected_key(g_injected_keys.backward, g_injected_keys.backward.virtual_key, false);
    set_injected_key(g_injected_keys.jump, g_injected_keys.jump.virtual_key, false);
    set_injected_key(g_injected_keys.sprint, g_injected_keys.sprint.virtual_key, false);
    set_injected_key(g_button_f_key, g_button_f_key.virtual_key, false);
    set_injected_key(g_button_tab_key, g_button_tab_key.virtual_key, false);
    set_injected_key(g_button_a_key, g_button_a_key.virtual_key, false);
    set_injected_key(g_sneak_key, g_sneak_key.virtual_key, false);
    g_button_f_release_at = 0;
    g_button_tab_release_at = 0;
    g_button_a_release_at = 0;
    g_sneak_toggled = false;
    g_quest_stick_state = {};
}

void set_injected_mouse_button(bool& state, DWORD down_flag, DWORD up_flag, bool down) {
    if (state == down) return;
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = down ? down_flag : up_flag;
    if (SendInput(1, &input, sizeof(input)) == 1) {
        state = down;
    }
}

void set_injected_left_mouse(bool down) {
    set_injected_mouse_button(g_left_mouse_down, MOUSEEVENTF_LEFTDOWN,
                              MOUSEEVENTF_LEFTUP, down);
}

void set_injected_right_mouse(bool down) {
    set_injected_mouse_button(g_right_mouse_down, MOUSEEVENTF_RIGHTDOWN,
                              MOUSEEVENTF_RIGHTUP, down);
}

void send_key_tap(WORD virtual_key) {
    const WORD scan_code = scan_code_for_virtual_key(virtual_key);
    if (scan_code == 0) return;
    INPUT inputs[2]{};
    for (auto& input : inputs) input.type = INPUT_KEYBOARD;
    inputs[0].ki.wScan = scan_code;
    inputs[1].ki.wScan = scan_code;
    inputs[0].ki.dwFlags = KEYEVENTF_SCANCODE;
    inputs[1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    SendInput(static_cast<UINT>(std::size(inputs)), inputs, sizeof(INPUT));
}

void focus_hytale_window_and_tap_f7() {
    focus_hytale_window();
    Sleep(80);
    if (hytale_has_input_focus()) {
        send_key_tap(VK_F7);
    }
}

void send_virtual_key_tap(WORD virtual_key) {
    send_key_tap(virtual_key);
}

void begin_timed_key_press(InjectedKey& key, ULONGLONG& release_at,
                           WORD virtual_key, DWORD hold_ms = 120) {
    set_injected_key(key, virtual_key, true);
    release_at = GetTickCount64() + hold_ms;
}

void update_timed_key_press(InjectedKey& key, ULONGLONG& release_at) {
    if (release_at == 0 || GetTickCount64() < release_at) return;
    set_injected_key(key, key.virtual_key, false);
    release_at = 0;
}

void send_mouse_wheel(int delta) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = static_cast<DWORD>(delta);
    SendInput(1, &input, sizeof(input));
}

void send_middle_click() {
    INPUT inputs[2]{};
    for (auto& input : inputs) input.type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
    inputs[1].mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
    SendInput(static_cast<UINT>(std::size(inputs)), inputs, sizeof(INPUT));
}

void update_right_hand_attack() {
    const bool enabled =
        IsDlgButtonChecked(g_window, IDC_VR_HAND_POINTER) == BST_CHECKED;
    const bool right_trigger = g_vr_camera_shared &&
        g_vr_camera_shared->controller_right_trigger_pressed != 0;
    const bool left_trigger = g_vr_camera_shared &&
        g_vr_camera_shared->controller_left_trigger_pressed != 0;
    const bool ray_ready = g_vr_camera_shared &&
        g_vr_camera_shared->controller_ray_active != 0;
    const bool pose_ready = g_vr_camera_shared &&
        g_vr_camera_shared->controller_right_pose_active != 0;
    const bool can_click = enabled && ray_ready && pose_ready && hytale_has_input_focus();
    set_injected_left_mouse(can_click && right_trigger);
    set_injected_right_mouse(can_click && left_trigger);
}

void update_quest_button_actions() {
    update_timed_key_press(g_button_f_key, g_button_f_release_at);
    update_timed_key_press(g_button_tab_key, g_button_tab_release_at);
    update_timed_key_press(g_button_a_key, g_button_a_release_at);
    if (!g_vr_camera_shared || !hytale_has_input_focus()) {
        g_prev_button_x = false;
        g_prev_button_y = false;
        g_prev_button_b = false;
        g_prev_left_grip = false;
        g_prev_right_grip = false;
        g_prev_right_stick_click = false;
        set_injected_key(g_button_f_key, g_button_f_key.virtual_key, false);
        set_injected_key(g_button_tab_key, g_button_tab_key.virtual_key, false);
        set_injected_key(g_button_a_key, g_button_a_key.virtual_key, false);
        set_injected_key(g_sneak_key, g_sneak_key.virtual_key, false);
        g_button_f_release_at = 0;
        g_button_tab_release_at = 0;
        g_button_a_release_at = 0;
        g_sneak_toggled = false;
        return;
    }

    const bool button_x = g_vr_camera_shared->controller_button_x != 0;
    const bool button_y = g_vr_camera_shared->controller_button_y != 0;
    const bool button_b = g_vr_camera_shared->controller_button_b != 0;
    const bool left_grip = g_vr_camera_shared->controller_left_grip != 0;
    const bool right_grip = g_vr_camera_shared->controller_right_grip != 0;
    const bool right_stick_click = g_vr_camera_shared->controller_right_stick_click != 0;

    if (button_x && !g_prev_button_x) {
        const WORD use_key = key_override(IDC_VR_KEY_USE, 'F');
        send_virtual_key_tap(use_key);
        begin_timed_key_press(g_button_f_key, g_button_f_release_at, use_key);
    }
    if (button_y && !g_prev_button_y) {
        begin_timed_key_press(g_button_tab_key, g_button_tab_release_at,
                              key_override(IDC_VR_KEY_INVENTORY, VK_TAB));
    }
    if (button_b && !g_prev_button_b) {
        begin_timed_key_press(g_button_a_key, g_button_a_release_at, 'A');
    }
    if (left_grip && !g_prev_left_grip) send_mouse_wheel(-WHEEL_DELTA);
    if (right_grip && !g_prev_right_grip) send_mouse_wheel(WHEEL_DELTA);
    if (right_stick_click && !g_prev_right_stick_click) {
        g_sneak_toggled = !g_sneak_toggled;
        set_injected_key(g_sneak_key, VK_CONTROL, g_sneak_toggled);
    }

    g_prev_button_x = button_x;
    g_prev_button_y = button_y;
    g_prev_button_b = button_b;
    g_prev_left_grip = left_grip;
    g_prev_right_grip = right_grip;
    g_prev_right_stick_click = right_stick_click;
}


void update_vr_menu_mouse() {
    g_menu_mouse_active = false;
    if (!g_vr_camera_shared ||
        IsDlgButtonChecked(g_window, IDC_VR_MENU_MOUSE) != BST_CHECKED ||
        IsDlgButtonChecked(g_window, IDC_VR_HAND_POINTER) != BST_CHECKED ||
        !hytale_has_input_focus() ||
        g_vr_camera_shared->ui_overlay_active == 0 ||
        g_vr_camera_shared->pointer_visible == 0 ||
        g_vr_camera_shared->pointer_menu_mode == 0) {
        g_menu_mouse_filter_valid = false;
        return;
    }

    HWND game_window = hytale_window();
    if (!game_window) {
        g_menu_mouse_filter_valid = false;
        return;
    }
    RECT client{};
    if (!GetClientRect(game_window, &client)) {
        g_menu_mouse_filter_valid = false;
        return;
    }
    const int client_width = client.right - client.left;
    const int client_height = client.bottom - client.top;
    const int texture_width = static_cast<int>(
        g_vr_camera_shared->pointer_surface_width != 0
            ? g_vr_camera_shared->pointer_surface_width
            : (g_vr_camera_shared->capture_width != 0
                ? g_vr_camera_shared->capture_width
                : g_vr_camera_shared->recommended_width));
    const int texture_height = static_cast<int>(
        g_vr_camera_shared->pointer_surface_height != 0
            ? g_vr_camera_shared->pointer_surface_height
            : (g_vr_camera_shared->capture_height != 0
                ? g_vr_camera_shared->capture_height
                : g_vr_camera_shared->recommended_height));
    if (client_width <= 0 || client_height <= 0 ||
        texture_width <= 0 || texture_height <= 0) {
        g_menu_mouse_filter_valid = false;
        return;
    }

    const float normalized_x = std::clamp(
        static_cast<float>(g_vr_camera_shared->pointer_x) / texture_width, 0.0f, 1.0f);
    const float normalized_y = std::clamp(
        static_cast<float>(g_vr_camera_shared->pointer_y) / texture_height, 0.0f, 1.0f);
    const float target_x = normalized_x * (client_width - 1);
    const float target_y = (1.0f - normalized_y) * (client_height - 1);
    if (!g_menu_mouse_filter_valid) {
        g_menu_mouse_filtered_x = target_x;
        g_menu_mouse_filtered_y = target_y;
        g_menu_mouse_filter_valid = true;
    } else {
        constexpr float follow = 0.30f;
        g_menu_mouse_filtered_x += (target_x - g_menu_mouse_filtered_x) * follow;
        g_menu_mouse_filtered_y += (target_y - g_menu_mouse_filtered_y) * follow;
    }
    POINT point{};
    point.x = static_cast<LONG>(std::lround(g_menu_mouse_filtered_x));
    point.y = static_cast<LONG>(std::lround(g_menu_mouse_filtered_y));
    g_menu_mouse_client = point;
    ClientToScreen(game_window, &point);
    SetCursorPos(point.x, point.y);
    g_menu_mouse_active = true;
}

float apply_stick_deadzone(float value, float deadzone) {
    const float magnitude = std::fabs(value);
    if (magnitude <= deadzone) return 0.0f;
    const float normalized = (magnitude - deadzone) / (1.0f - deadzone);
    return std::copysign(std::clamp(normalized, 0.0f, 1.0f), value);
}

void update_quest_turning() {
    const ULONGLONG now = GetTickCount64();
    const float elapsed = g_last_turn_tick == 0
        ? 0.0f
        : std::clamp(static_cast<float>(now - g_last_turn_tick) * 0.001f,
                     0.0f, 0.05f);
    g_last_turn_tick = now;
    if (IsDlgButtonChecked(g_window, IDC_VR_QUEST_LOCOMOTION) != BST_CHECKED ||
        !g_vr_camera_shared || !g_vr_camera_shared->controller_active ||
        !hytale_has_input_focus()) {
        g_turn_mouse_remainder = 0.0f;
        if (g_vr_camera_shared) g_vr_camera_shared->native_head_sync_active = 0;
        return;
    }

    const float deadzone = std::clamp(get_float(IDC_VR_QUEST_DEADZONE, 0.35f),
                                      0.10f, 0.90f);
    const float axis = apply_stick_deadzone(
        g_vr_camera_shared->controller_turn_x, deadzone);
    const float speed = std::clamp(get_float(IDC_VR_TURN_SPEED, 450.0f),
                                   50.0f, 2000.0f);
    g_turn_mouse_remainder += axis * speed * elapsed;

    const bool stick_turning = std::fabs(axis) > 0.001f;
    if (stick_turning) {
        g_last_stick_turn_tick = now;
    }
    const bool stick_turn_recent = g_last_stick_turn_tick != 0 &&
        now - g_last_stick_turn_tick < 160;
    bool head_sync_active = false;

    Vec3 hmd{};
    float rotation[9]{};
    if (!stick_turning && !stick_turn_recent &&
        g_vr_head_origin_yaw_valid &&
        g_vr_camera_shared->camera_yaw_valid != 0 &&
        read_hmd_pose(hmd, rotation)) {
        const float hmd_relative_yaw = normalize_angle(
            hmd_yaw_from_rotation(rotation) - g_vr_head_origin_yaw);
        const float target_native_yaw = normalize_angle(
            g_vr_camera_shared->body_camera_yaw + hmd_relative_yaw);
        const float yaw_error = normalize_angle(
            target_native_yaw - g_vr_camera_shared->native_camera_yaw);
        if (std::fabs(yaw_error) > 0.003f) {
            constexpr float kHeadYawMousePixelsPerRadian = 650.0f;
            const float max_pixels = std::clamp(2600.0f * elapsed, 4.0f, 28.0f);
            const float pixels = std::clamp(
                yaw_error * kHeadYawMousePixelsPerRadian, -max_pixels, max_pixels);
            g_head_turn_mouse_remainder += pixels;
            head_sync_active = true;
        }
    }
    g_vr_camera_shared->native_head_sync_active = head_sync_active ? 1u : 0u;

    g_turn_mouse_remainder += g_head_turn_mouse_remainder;
    g_head_turn_mouse_remainder = 0.0f;
    const LONG mouse_x = static_cast<LONG>(std::lround(g_turn_mouse_remainder));
    if (mouse_x == 0) return;

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = mouse_x;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    if (SendInput(1, &input, sizeof(input)) == 1) {
        g_turn_mouse_remainder -= static_cast<float>(mouse_x);
    }
}

bool read_quest_controls(float& x, float& y, bool& jump, bool& sprint) {
    if (!g_vr_camera_shared || !g_vr_camera_shared->controller_active) return false;
    x = g_vr_camera_shared->controller_move_x;
    y = g_vr_camera_shared->controller_move_y;
    jump = g_vr_camera_shared->controller_jump != 0;
    sprint = g_vr_camera_shared->controller_sprint != 0;
    return true;
}

void update_quest_locomotion() {
    const bool enabled = IsDlgButtonChecked(g_window, IDC_VR_QUEST_LOCOMOTION) == BST_CHECKED;
    bool jump = false;
    bool sprint = false;
    g_quest_controller_connected = enabled &&
        read_quest_controls(g_quest_stick_x, g_quest_stick_y, jump, sprint);
    if (!g_quest_controller_connected || !hytale_has_input_focus()) {
        release_quest_keys();
        if (!g_quest_controller_connected) {
            g_quest_stick_x = 0.0f;
            g_quest_stick_y = 0.0f;
        }
        return;
    }

    const float deadzone = std::clamp(get_float(IDC_VR_QUEST_DEADZONE, 0.35f),
                                      0.10f, 0.90f);
    g_quest_stick_state = hytalevr::digital_stick(
        g_quest_stick_x, g_quest_stick_y, g_quest_stick_state, deadzone);
    const MovementKeys movement_keys = movement_keys_for_current_layout();
    set_injected_key(g_injected_keys.forward,
                     key_override(IDC_VR_KEY_FORWARD, movement_keys.forward),
                     g_quest_stick_state.forward);
    set_injected_key(g_injected_keys.backward,
                     key_override(IDC_VR_KEY_BACKWARD, movement_keys.backward),
                     g_quest_stick_state.backward);
    set_injected_key(g_injected_keys.left,
                     key_override(IDC_VR_KEY_LEFT, movement_keys.left),
                     g_quest_stick_state.left);
    set_injected_key(g_injected_keys.right,
                     key_override(IDC_VR_KEY_RIGHT, movement_keys.right),
                     g_quest_stick_state.right);
    set_injected_key(g_injected_keys.jump, key_override(IDC_VR_KEY_JUMP, VK_SPACE), jump);
    set_injected_key(g_injected_keys.sprint, key_override(IDC_VR_KEY_SPRINT, VK_SHIFT), sprint);
}

std::wstring executable_directory() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring result(path);
    const size_t slash = result.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : result.substr(0, slash);
}

bool initialize_vr_camera_mapping() {
    if (g_vr_camera_shared) return true;
    g_vr_camera_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                               sizeof(VrCameraShared), kVrCameraMappingName);
    g_vr_camera_mapping_error = GetLastError();
    g_vr_camera_mapping_existed = g_vr_camera_mapping_error == ERROR_ALREADY_EXISTS;
    if (!g_vr_camera_mapping) return false;
    g_vr_camera_shared = static_cast<VrCameraShared*>(
        MapViewOfFile(g_vr_camera_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(VrCameraShared)));
    if (!g_vr_camera_shared) {
        g_vr_camera_mapping_error = GetLastError();
        CloseHandle(g_vr_camera_mapping);
        g_vr_camera_mapping = nullptr;
        return false;
    }
    if (g_vr_camera_shared->magic != kVrCameraMagic ||
        g_vr_camera_shared->version != kVrCameraVersion) {
        *g_vr_camera_shared = VrCameraShared{};
    }
    return true;
}

bool remote_load_library(HANDLE process, const std::wstring& path) {
    const SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) return false;

    bool success = WriteProcessMemory(process, remote, path.c_str(), bytes, nullptr) != FALSE;
    if (success) {
        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        auto* load_library = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            kernel32 ? GetProcAddress(kernel32, "LoadLibraryW") : nullptr);
        HANDLE thread = load_library
            ? CreateRemoteThread(process, nullptr, 0, load_library, remote, 0, nullptr)
            : nullptr;
        if (thread) {
            success = WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0;
            DWORD module_result = 0;
            success = success && GetExitCodeThread(thread, &module_result) && module_result != 0;
            CloseHandle(thread);
        } else {
            success = false;
        }
    }
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    return success;
}

bool has_loaded_vr_camera_hook(DWORD pid, const wchar_t* required_module) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    bool found_required = false;
    bool found_other = false;
    if (Module32FirstW(snapshot, &module)) {
        do {
            if (_wcsnicmp(module.szModule, L"hytale_vr_camera_hook_", 22) == 0) {
                if (_wcsicmp(module.szModule, required_module) == 0) {
                    found_required = true;
                } else {
                    found_other = true;
                }
            }
        } while (Module32NextW(snapshot, &module));
    }
    CloseHandle(snapshot);
    if (found_other) return false;
    return found_required;
}

bool inject_vr_camera_hook() {
    if (!initialize_vr_camera_mapping()) {
        if (g_vr_camera_mapping_existed) {
            set_status(L"Shared VR memory is held by an old injection. Close all dashboards and restart Hytale.");
        } else {
            set_status(L"Could not create shared VR memory. Run the dashboard as administrator if Hytale is elevated.");
        }
        return false;
    }
    constexpr wchar_t required_module[] = L"hytale_vr_camera_hook_v120_native_hand.dll";
    if (has_loaded_vr_camera_hook(g_pid, required_module)) {
        if (g_vr_camera_shared->hook_active) {
            focus_hytale_window_and_tap_f7();
            return true;
        }
        if ((InterlockedCompareExchange(
                reinterpret_cast<volatile LONG*>(&g_vr_camera_shared->control_sequence),
                0, 0) & 1) != 0) {
            InterlockedIncrement(
                reinterpret_cast<volatile LONG*>(&g_vr_camera_shared->control_sequence));
        }
        InterlockedIncrement(
            reinterpret_cast<volatile LONG*>(&g_vr_camera_shared->control_sequence));
        MemoryBarrier();
        g_vr_camera_shared->enabled = 0;
        g_vr_camera_shared->shutdown_requested = 0;
        g_vr_camera_shared->unload_requested = 0;
        ++g_vr_camera_shared->install_sequence;
        g_vr_camera_shared->hook_error = 0;
        MemoryBarrier();
        InterlockedIncrement(
            reinterpret_cast<volatile LONG*>(&g_vr_camera_shared->control_sequence));
        set_status(L"VR hook loaded. Waiting for auto-scan to finish...");
        for (int i = 0; i < 2000 && !g_vr_camera_shared->hook_active &&
             g_vr_camera_shared->hook_error == 0; ++i) {
            Sleep(5);
        }
        const bool active = g_vr_camera_shared->hook_active != 0;
        if (active) focus_hytale_window_and_tap_f7();
        return active;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, g_pid);
    if (snapshot != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W module{};
        module.dwSize = sizeof(module);
        if (Module32FirstW(snapshot, &module)) {
            do {
                if (_wcsnicmp(module.szModule, L"hytale_vr_camera_hook_", 22) == 0 &&
                    _wcsicmp(module.szModule, required_module) != 0) {
                    CloseHandle(snapshot);
                    set_status(L"An older VR hook is loaded in Hytale. Close and restart Hytale before Center VR.");
                    return false;
                }
            } while (Module32NextW(snapshot, &module));
        }
        CloseHandle(snapshot);
    }
    *g_vr_camera_shared = VrCameraShared{};

    const std::wstring directory = executable_directory();
    const std::wstring openvr = directory + L"\\openvr_api.dll";
    const std::wstring ui_hook = directory + L"\\HytaleUIScaleHook.dll";
    const std::wstring dll = directory + L"\\hytale_vr_camera_hook_v120_native_hand.dll";
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                     PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                                 FALSE, g_pid);
    if (!process) {
        set_status(L"Could not open Hytale for VR hook injection. Run the dashboard with the same privileges as the game.");
        return false;
    }

    const bool success = remote_load_library(process, ui_hook) &&
                         remote_load_library(process, openvr) &&
                         remote_load_library(process, dll);
    CloseHandle(process);
    if (!success) {
        set_status(L"VR hook v120 injection failed. Close Hytale if an old DLL is loaded, then restart the dashboard.");
        return false;
    }

    set_status(L"VR hook injected. Waiting for auto-scan to finish...");
    for (int i = 0; i < 2000 && !g_vr_camera_shared->hook_active &&
         g_vr_camera_shared->hook_error == 0; ++i) {
        Sleep(5);
    }
    if (g_vr_camera_shared->hook_error != 0) {
        switch (g_vr_camera_shared->hook_error) {
        case 1:
            set_status(L"Camera auto-scan failed. This Hytale version needs a new hook pattern.");
            break;
        case 2:
            set_status(L"SDL_GL_SwapWindow is incompatible. The camera hook was removed cleanly.");
            break;
        case 4:
            set_status(L"Interaction-ray auto-scan failed. Camera hook was removed cleanly.");
            break;
        case 5:
            set_status(L"2D UI hook unavailable on this version. Camera, stereo and interaction remain active.");
            break;
        case 6:
            set_status(L"VR hook worker could not read shared memory. Restart Hytale and the dashboard.");
            break;
        default:
            set_status(L"Hook restoration failed. Close the dashboard and restart Hytale.");
            break;
        }
    }
    if (!g_vr_camera_shared->hook_active) {
        set_status(L"VR hook worker did not report back. Check %TEMP%\\HytaleVR\\hytale_vr_render_debug.log.");
    }
    const bool active = g_vr_camera_shared->hook_active != 0;
    if (active) focus_hytale_window_and_tap_f7();
    return active;
}

void start_vr_tracking();

void publish_vr_controls(bool enabled, bool shutdown_requested, bool unload_requested = false) {
    if (!g_vr_camera_shared) return;
    const bool non_vr_mode = IsDlgButtonChecked(g_window, IDC_VR_NON_VR) == BST_CHECKED;
    if ((InterlockedCompareExchange(
            reinterpret_cast<volatile LONG*>(&g_vr_camera_shared->control_sequence), 0, 0) & 1) != 0) {
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_vr_camera_shared->control_sequence));
    }
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_vr_camera_shared->control_sequence));
    MemoryBarrier();
    g_vr_camera_shared->enabled = enabled ? 1u : 0u;
    g_vr_camera_shared->non_vr_mode = non_vr_mode ? 1u : 0u;
    g_vr_camera_shared->stereo_enabled =
        (!non_vr_mode && IsDlgButtonChecked(g_window, IDC_VR_STEREO) == BST_CHECKED) ? 1u : 0u;
    g_vr_camera_shared->ipd_meters =
        std::clamp(get_float(IDC_VR_IPD, 64.0f), 40.0f, 80.0f) * 0.001f;
    g_vr_camera_shared->stereo_separation =
        std::clamp(get_float(IDC_VR_SEPARATION, 100.0f), 0.0f, 200.0f) * 0.01f;
    g_vr_camera_shared->render_scale =
        std::clamp(get_float(IDC_VR_RENDER_SCALE, 100.0f), 35.0f, 100.0f) * 0.01f;
    g_vr_camera_shared->translation_scale =
        std::clamp(get_float(IDC_VR_SCALE, 1.0f), 0.0f, 10.0f);
    g_vr_camera_shared->translation_y_scale =
        std::clamp(get_float(IDC_VR_Y_SCALE, 1.0f), 0.0f, 10.0f);
    g_vr_camera_shared->invert_translation_xz =
        IsDlgButtonChecked(g_window, IDC_VR_INVERT_Z) == BST_CHECKED ? 1u : 0u;
    g_vr_camera_shared->hand_pointer_enabled =
        IsDlgButtonChecked(g_window, IDC_VR_HAND_POINTER) == BST_CHECKED ? 1u : 0u;
    g_vr_camera_shared->hide_center_reticle =
        IsDlgButtonChecked(g_window, IDC_VR_HIDE_RETICLE) == BST_CHECKED ? 1u : 0u;
    g_vr_camera_shared->ui_overlay_enabled =
        IsDlgButtonChecked(g_window, IDC_VR_DISABLE_FOREGROUND_EFFECTS) == BST_CHECKED ? 1u : 0u;
    g_vr_camera_shared->shadows_disabled =
        IsDlgButtonChecked(g_window, IDC_VR_DISABLE_SHADOWS) == BST_CHECKED ? 1u : 0u;
    g_vr_camera_shared->particles_disabled =
        IsDlgButtonChecked(g_window, IDC_VR_DISABLE_PARTICLES) == BST_CHECKED ? 1u : 0u;
    g_vr_camera_shared->distortion_disabled =
        IsDlgButtonChecked(g_window, IDC_VR_DISABLE_DISTORTION) == BST_CHECKED ? 1u : 0u;
    g_vr_camera_shared->ui_overlay_distance =
        std::clamp(get_float(IDC_VR_MENU_DISTANCE, 1.65f), 0.35f, 6.0f);
    g_vr_camera_shared->ui_overlay_width =
        std::clamp(get_float(IDC_VR_MENU_WIDTH, 1.50f), 0.35f, 4.0f);
    g_vr_camera_shared->ui_scale =
        std::clamp(get_float(IDC_VR_UI_SCALE, 1.00f), 0.10f, 2.0f);
    g_vr_camera_shared->ui_eye_offset =
        std::clamp(get_float(IDC_VR_UI_EYE_OFFSET, 0.245f), -0.25f, 0.25f);
    g_vr_camera_shared->ui_offset_y =
        std::clamp(get_float(IDC_VR_UI_Y_OFFSET, 0.0f), -0.75f, 0.75f);
    g_vr_camera_shared->ui_ubo_scaling_disabled =
        IsDlgButtonChecked(g_window, IDC_VR_NO_UBO_UI) == BST_CHECKED ? 1u : 0u;
    g_vr_camera_shared->menu_ignore_draw_threshold =
        static_cast<uint32_t>(std::clamp(get_float(IDC_VR_MENU_IGNORE_DRAW, 1.0f),
                                         0.0f, 20000.0f));
    g_vr_camera_shared->hand_pointer_distance =
        std::clamp(get_float(IDC_VR_POINTER_DISTANCE, 4.0f), 0.5f, 10.0f);
    g_vr_camera_shared->turn_speed =
        std::clamp(get_float(IDC_VR_TURN_SPEED, 450.0f), 50.0f, 2000.0f);
    g_vr_camera_shared->floor_tilt_degrees =
        std::clamp(get_float(IDC_VR_FLOOR_TILT, -15.0f), -45.0f, 45.0f);
    g_vr_camera_shared->first_person_hand_hidden =
        IsDlgButtonChecked(g_window, IDC_VR_HIDE_FIRST_PERSON_HAND) == BST_CHECKED ? 1u : 0u;
    g_vr_camera_shared->wide_culling_enabled = 0u;
    g_vr_camera_shared->wide_culling_scale = 0.50f;
    g_vr_camera_shared->hmd_culling_view_enabled = 1u;
    g_vr_camera_shared->swap_eyes =
        IsDlgButtonChecked(g_window, IDC_VR_SWAP_EYES) == BST_CHECKED ? 1u : 0u;
    g_vr_camera_shared->shutdown_requested = shutdown_requested ? 1u : 0u;
    g_vr_camera_shared->unload_requested = unload_requested ? 1u : 0u;
    g_vr_camera_shared->recenter_sequence = g_vr_recenter_sequence;
    MemoryBarrier();
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_vr_camera_shared->control_sequence));
}

bool ensure_openvr_for_calibration() {
    if (g_vr_system) return true;
    vr::EVRInitError error = vr::VRInitError_None;
    g_vr_system = vr::VR_Init(&error, vr::VRApplication_Background);
    if (error != vr::VRInitError_None || !g_vr_system) {
        g_vr_system = nullptr;
        set_status(L"SteamVR/OpenVR unavailable. Start SteamVR and wake the headset.");
        return false;
    }
    return true;
}

void calibrate_floor_tilt() {
    if (!ensure_openvr_for_calibration()) return;

    constexpr int kSamples = 24;
    constexpr float kMaxSampleSpreadDegrees = 3.5f;
    float sum = 0.0f;
    float minimum = 1000.0f;
    float maximum = -1000.0f;
    int samples = 0;

    set_status(L"Calibrating floor tilt... keep your head steady and look naturally forward.");
    for (int i = 0; i < kSamples; ++i) {
        Vec3 position{};
        float rotation[9]{};
        if (read_hmd_pose(position, rotation)) {
            const float pitch = std::clamp(hmd_pitch_degrees_from_rotation(rotation), -45.0f, 45.0f);
            sum += pitch;
            minimum = std::min(minimum, pitch);
            maximum = std::max(maximum, pitch);
            ++samples;
        }
        Sleep(10);
    }

    if (samples < 8) {
        set_status(L"Floor calibration failed: headset pose was not stable/valid.");
        return;
    }
    if (maximum - minimum > kMaxSampleSpreadDegrees) {
        set_status(L"Floor calibration cancelled: head moved too much. Try again while steady.");
        return;
    }

    const float calibrated = std::clamp(sum / static_cast<float>(samples), -45.0f, 45.0f);
    set_float(IDC_VR_FLOOR_TILT, calibrated);
    if (g_vr_tracking) {
        publish_vr_controls(true, false);
    }

    wchar_t status[180]{};
    swprintf_s(status, L"Floor tilt calibrated to %.2f degrees.", calibrated);
    set_status(status);
}

void recenter_vr_tracking() {
    if (!g_vr_tracking || !g_vr_camera_shared || !g_vr_camera_shared->hook_active) {
        start_vr_tracking();
        return;
    }
    ++g_vr_recenter_sequence;
    update_head_origin_yaw();
    publish_vr_controls(true, false);

    wchar_t status[160]{};
    swprintf_s(status, L"VR recenter sent to hook. Sequence=%u", g_vr_recenter_sequence);
    set_status(status);
}

void shutdown_vr() {
    KillTimer(g_window, kVrTimer);
    g_vr_tracking = false;
    release_quest_keys();
    set_injected_left_mouse(false);
    set_injected_right_mouse(false);
    g_menu_mouse_filter_valid = false;
    g_menu_mouse_active = false;
    g_prev_button_x = false;
    g_prev_button_y = false;
    g_prev_button_b = false;
    g_prev_left_grip = false;
    g_prev_right_grip = false;
    g_prev_right_stick_click = false;
    g_quest_controller_connected = false;
    g_vr_head_origin_yaw_valid = false;
    g_vr_last_head_sync_yaw_valid = false;
    g_last_turn_tick = 0;
    g_last_stick_turn_tick = 0;
    g_turn_mouse_remainder = 0.0f;
    g_head_turn_mouse_remainder = 0.0f;
    if (g_vr_camera_shared) {
        publish_vr_controls(false, true);
    }
    if (g_vr_system) {
        vr::VR_Shutdown();
        g_vr_system = nullptr;
    }
    SetWindowTextW(control(IDC_VR_POSE), L"Headset: stopped");
}

void unload_vr_hook() {
    if (!g_vr_camera_shared || !g_vr_camera_shared->hook_active) return;
    publish_vr_controls(false, true, true);
    for (int i = 0; i < 200 && g_vr_camera_shared->hook_active; ++i) Sleep(5);
}

void start_vr_tracking() {
    if (!attach()) return;
    const bool non_vr_mode = IsDlgButtonChecked(g_window, IDC_VR_NON_VR) == BST_CHECKED;
    if (non_vr_mode) {
        if (!inject_vr_camera_hook()) {
            return;
        }
        g_vr_tracking = true;
        SetTimer(g_window, kVrTimer, 50, nullptr);
        publish_vr_controls(true, false);
        SetWindowTextW(control(IDC_VR_POSE), L"Non-VR: active");
        set_status(L"Non-VR mode active. Only render filters are applied.");
        return;
    }
    const int selected = selected_candidate();
    if (selected < 0 || static_cast<size_t>(selected) >= g_candidates.size()) {
        set_status(L"Scan the player block first.");
        return;
    }

    if (!g_vr_system) {
        vr::EVRInitError error = vr::VRInitError_None;
        g_vr_system = vr::VR_Init(&error, vr::VRApplication_Background);
        if (error != vr::VRInitError_None || !g_vr_system) {
            g_vr_system = nullptr;
            set_status(L"SteamVR/OpenVR unavailable. Start SteamVR and wake the headset.");
            return;
        }
    }

    Vec3 hmd{};
    float hmd_rotation[9]{};
    Vec3 player{};
    if (!read_hmd_pose(hmd, hmd_rotation)) {
        set_status(L"Headset detected, but its pose is not valid yet.");
        return;
    }
    g_vr_head_origin_yaw = hmd_yaw_from_rotation(hmd_rotation);
    g_vr_head_origin_yaw_valid = true;
    g_vr_last_head_sync_yaw = g_vr_head_origin_yaw;
    g_vr_last_head_sync_yaw_valid = true;
    g_head_turn_mouse_remainder = 0.0f;
    if (!inject_vr_camera_hook()) {
        return;
    }
    if (!read_exact(g_candidates[selected].base + 0x288, &player, sizeof(player))) {
        set_status(L"The player block is no longer valid. Scan again.");
        return;
    }

    g_target = player;
    g_vr_tracking = true;
    ++g_vr_recenter_sequence;
    SetTimer(g_window, kVrTimer, 8, nullptr);
    publish_vr_controls(true, false);
    set_status(L"VR tracking active. Camera 6DoF is enabled; Hytale keeps locomotion and gravity.");
}

void update_vr_tracking() {
    if (!g_vr_tracking) return;
    if (IsDlgButtonChecked(g_window, IDC_VR_NON_VR) == BST_CHECKED) {
        publish_vr_controls(true, false);
        SetWindowTextW(control(IDC_VR_POSE), L"Non-VR: active");
        return;
    }
    update_quest_turning();
    update_quest_locomotion();
    update_vr_menu_mouse();
    update_quest_button_actions();
    update_right_hand_attack();
    publish_vr_controls(true, false);

    Vec3 player{};
    if (!read_player_position(player)) {
        shutdown_vr();
        set_status(L"Lost player-block reading. Scan again before recentering VR.");
        return;
    }

    g_target = player;

    set_float(IDC_TARGET_X, g_target.x);
    set_float(IDC_TARGET_Y, g_target.y);
    set_float(IDC_TARGET_Z, g_target.z);
    wchar_t pose_text[96]{};
    swprintf_s(pose_text, L"VR active | Hand %s | Ray %s",
               g_vr_camera_shared && g_vr_camera_shared->controller_right_pose_active ? L"OK" : L"--",
               g_vr_camera_shared && g_vr_camera_shared->controller_ray_active ? L"OK" : L"--");
    SetWindowTextW(control(IDC_VR_POSE), pose_text);
}
#endif

void teleport() {
#ifdef HYTALE_CAMERA_MODE
    set_status(L"Teleport tools are disabled in the VR dashboard.");
    return;
#else
    if (!attach()) return;
    if (selected_candidate() < 0) {
        set_status(L"Scan and select a candidate address first.");
        return;
    }
    g_target = {get_float(IDC_TARGET_X), get_float(IDC_TARGET_Y), get_float(IDC_TARGET_Z)};
    g_burst_until = GetTickCount64() + 1500;
    if (write_target()) {
        SetTimer(g_window, kWriteTimer, 10, nullptr);
        set_status(L"Teleport applied. Writing is held for 1.5 seconds.");
    } else {
        set_status(L"Write failed. The game may have restarted; attach and scan again.");
    }
#endif
}

void restore() {
#ifdef HYTALE_CAMERA_MODE
    set_status(L"Restore tools are disabled in the VR dashboard.");
    return;
#else
    const int selected = selected_candidate();
    if (!g_process || selected < 0 || static_cast<size_t>(selected) >= g_candidates.size()) {
        set_status(L"No candidate selected to restore.");
        return;
    }
    const Candidate& candidate = g_candidates[selected];
    bool success = true;
    for (size_t i = 0; i < std::size(kCoordinateOffsets); ++i) {
        success = write_exact(candidate.base + kCoordinateOffsets[i], &candidate.original[i], sizeof(Vec3)) && success;
    }
    g_burst_until = 0;
    CheckDlgButton(g_window, IDC_HOLD, BST_UNCHECKED);
    set_status(success ? L"Values captured during scan restored." : L"Restore failed.");
#endif
}

HWND add_control(const wchar_t* klass, const wchar_t* text, DWORD style,
                 int x, int y, int width, int height, int id = 0) {
    DWORD normalStyle = style;
    if (_wcsicmp(klass, L"EDIT") == 0) {
        normalStyle &= ~WS_BORDER;
    }
    
    HWND hwnd = CreateWindowExW(0, klass, text, WS_CHILD | WS_VISIBLE | normalStyle,
                           x, y, width, height, g_window,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
                           
    if (hwnd) {
        if (_wcsicmp(klass, L"EDIT") == 0) {
            SendMessageW(hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
            SetWindowSubclass(hwnd, EditSubclassProc, SUBCLASS_EDIT, 0);
        } else if (_wcsicmp(klass, L"BUTTON") == 0) {
            DWORD btnStyle = GetWindowLongW(hwnd, GWL_STYLE) & 0xF;
            if (btnStyle == BS_AUTOCHECKBOX || btnStyle == BS_CHECKBOX) {
                SetWindowSubclass(hwnd, ToggleSubclassProc, SUBCLASS_TOGGLE, 0);
            } else {
                SetWindowSubclass(hwnd, ButtonSubclassProc, SUBCLASS_BUTTON, 0);
            }
        } else if (_wcsicmp(klass, L"STATIC") == 0) {
            SetWindowSubclass(hwnd, StaticSubclassProc, SUBCLASS_STATIC, 0);
        } else if (_wcsicmp(klass, L"LISTBOX") == 0) {
            SetWindowSubclass(hwnd, ListboxSubclassProc, SUBCLASS_LISTBOX, 0);
        }
    }
    return hwnd;
}

void create_ui(HWND window) {
    g_window = window;
#ifdef HYTALE_CAMERA_MODE
    g_advanced_controls.clear();
#endif
    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    std::vector<HWND> controls;
    auto add = [&](const wchar_t* klass, const wchar_t* text, DWORD style,
                   int x, int y, int width, int height, int id = 0) {
        HWND item = add_control(klass, text, style, x, y, width, height, id);
        controls.push_back(item);
        return item;
    };
#ifdef HYTALE_CAMERA_MODE
    auto add_advanced = [&](const wchar_t* klass, const wchar_t* text, DWORD style,
                            int x, int y, int width, int height, int id = 0) {
        HWND item = add(klass, text, style, x, y, width, height, id);
        g_advanced_controls.push_back(item);
        return item;
    };
    auto add_hidden = [&](const wchar_t* klass, const wchar_t* text, DWORD style,
                          int x, int y, int width, int height, int id = 0) {
        HWND item = add(klass, text, style, x, y, width, height, id);
        ShowWindow(item, SW_HIDE);
        return item;
    };

    add(L"BUTTON", L"Advanced options", BS_AUTOCHECKBOX, 20, 92, 145, 24, IDC_VR_ADVANCED_OPTIONS);

    // Main functional controls
    add(L"BUTTON", L"Scan player block", BS_DEFPUSHBUTTON, 215, 50, 160, 40, IDC_SCAN);
    add(L"BUTTON", L"Center VR", BS_DEFPUSHBUTTON, 390, 50, 160, 40, IDC_VR_CENTER);
    add(L"BUTTON", L"Stop VR", BS_PUSHBUTTON, 565, 50, 160, 40, IDC_VR_STOP);
    add(L"STATIC", L"Headset: stopped", WS_BORDER | SS_LEFT, 740, 50, 240, 40, IDC_VR_POSE);

    add(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        215, 166, 845, 46, IDC_CANDIDATES);

    // Column 1 inputs
    add(L"EDIT", L"158.110", WS_BORDER | ES_AUTOHSCROLL, 235, 304, 135, 24, IDC_CURRENT_X);
    add(L"EDIT", L"121.000", WS_BORDER | ES_AUTOHSCROLL, 235, 334, 135, 24, IDC_CURRENT_Y);
    add(L"EDIT", L"256.118", WS_BORDER | ES_AUTOHSCROLL, 235, 364, 135, 24, IDC_CURRENT_Z);
    add(L"EDIT", L"0.10", WS_BORDER | ES_AUTOHSCROLL, 295, 394, 75, 24, IDC_TOLERANCE);
    add(L"EDIT", L"-15", WS_BORDER | ES_AUTOHSCROLL, 295, 424, 75, 24, IDC_VR_FLOOR_TILT);
    add(L"BUTTON", L"Calibrate floor", BS_PUSHBUTTON, 235, 456, 135, 24, IDC_VR_CALIBRATE_FLOOR);

    // Column 2 inputs/toggles
    add(L"BUTTON", L"SteamVR stereo", BS_AUTOCHECKBOX, 405, 304, 175, 20, IDC_VR_STEREO);
    add(L"BUTTON", L"Stable VR menus", BS_AUTOCHECKBOX, 405, 328, 175, 20, IDC_VR_DISABLE_FOREGROUND_EFFECTS);
    add(L"BUTTON", L"Disable shadows", BS_AUTOCHECKBOX, 405, 352, 175, 20, IDC_VR_DISABLE_SHADOWS);
    add(L"BUTTON", L"Disable particles", BS_AUTOCHECKBOX, 405, 376, 175, 20, IDC_VR_DISABLE_PARTICLES);
    add(L"BUTTON", L"Disable distortion", BS_AUTOCHECKBOX, 405, 400, 175, 20, IDC_VR_DISABLE_DISTORTION);
    add(L"BUTTON", L"Quest stick", BS_AUTOCHECKBOX, 405, 424, 175, 20, IDC_VR_QUEST_LOCOMOTION);
    add(L"BUTTON", L"Hide 1P Hand", BS_AUTOCHECKBOX, 405, 448, 175, 20, IDC_VR_HIDE_FIRST_PERSON_HAND);
    add(L"EDIT", L"64.0", WS_BORDER | ES_AUTOHSCROLL, 515, 500, 65, 24, IDC_VR_IPD);
    add(L"EDIT", L"100", WS_BORDER | ES_AUTOHSCROLL, 515, 528, 65, 24, IDC_VR_SEPARATION);
    add(L"EDIT", L"100", WS_BORDER | ES_AUTOHSCROLL, 515, 556, 65, 24, IDC_VR_RENDER_SCALE);

    // Column 3 inputs
    add(L"EDIT", L"Auto", WS_BORDER | ES_AUTOHSCROLL, 685, 328, 75, 24, IDC_VR_KEY_FORWARD);
    add(L"EDIT", L"Auto", WS_BORDER | ES_AUTOHSCROLL, 685, 354, 75, 24, IDC_VR_KEY_BACKWARD);
    add(L"EDIT", L"Auto", WS_BORDER | ES_AUTOHSCROLL, 685, 380, 75, 24, IDC_VR_KEY_LEFT);
    add(L"EDIT", L"Auto", WS_BORDER | ES_AUTOHSCROLL, 685, 406, 75, 24, IDC_VR_KEY_RIGHT);
    add(L"EDIT", L"Auto", WS_BORDER | ES_AUTOHSCROLL, 685, 432, 75, 24, IDC_VR_KEY_JUMP);
    add(L"EDIT", L"Auto", WS_BORDER | ES_AUTOHSCROLL, 685, 458, 75, 24, IDC_VR_KEY_SPRINT);
    add(L"EDIT", L"Auto", WS_BORDER | ES_AUTOHSCROLL, 685, 484, 75, 24, IDC_VR_KEY_USE);
    add(L"EDIT", L"Auto", WS_BORDER | ES_AUTOHSCROLL, 685, 510, 75, 24, IDC_VR_KEY_INVENTORY);

    // Column 4 inputs/toggles
    add(L"BUTTON", L"Right-hand pointer", BS_AUTOCHECKBOX, 815, 304, 220, 24, IDC_VR_HAND_POINTER);
    add(L"BUTTON", L"Hide center reticle", BS_AUTOCHECKBOX, 815, 330, 220, 24, IDC_VR_HIDE_RETICLE);
    add(L"BUTTON", L"Menu mouse", BS_AUTOCHECKBOX, 815, 356, 220, 24, IDC_VR_MENU_MOUSE);
    add(L"BUTTON", L"No UBO", BS_AUTOCHECKBOX, 815, 382, 220, 24, IDC_VR_NO_UBO_UI);
    
    add(L"EDIT", L"0.35", WS_BORDER | ES_AUTOHSCROLL, 815, 424, 95, 24, IDC_VR_QUEST_DEADZONE);
    add(L"EDIT", L"450", WS_BORDER | ES_AUTOHSCROLL, 925, 424, 95, 24, IDC_VR_TURN_SPEED);
    add(L"EDIT", L"4.0", WS_BORDER | ES_AUTOHSCROLL, 815, 476, 95, 24, IDC_VR_POINTER_DISTANCE);
    
    add(L"EDIT", L"1.65", WS_BORDER | ES_AUTOHSCROLL, 925, 476, 95, 24, IDC_VR_MENU_DISTANCE);
    add(L"EDIT", L"1.50", WS_BORDER | ES_AUTOHSCROLL, 815, 528, 95, 24, IDC_VR_MENU_WIDTH);
    add(L"EDIT", L"1.00", WS_BORDER | ES_AUTOHSCROLL, 925, 528, 95, 24, IDC_VR_UI_SCALE);
    
    add(L"EDIT", L"0.245", WS_BORDER | ES_AUTOHSCROLL, 815, 580, 95, 24, IDC_VR_UI_EYE_OFFSET);
    add(L"EDIT", L"0", WS_BORDER | ES_AUTOHSCROLL, 925, 580, 95, 24, IDC_VR_UI_Y_OFFSET);
    
    add(L"EDIT", L"1", WS_BORDER | ES_AUTOHSCROLL, 815, 632, 95, 24, IDC_VR_MENU_IGNORE_DRAW);

    add_hidden(L"BUTTON", L"Attach", BS_PUSHBUTTON, 650, 39, 60, 25, IDC_ATTACH);
    add_hidden(L"EDIT", L"0", WS_BORDER | ES_AUTOHSCROLL, 36, 302, 80, 25, IDC_TARGET_X);
    add_hidden(L"EDIT", L"0", WS_BORDER | ES_AUTOHSCROLL, 126, 302, 80, 25, IDC_TARGET_Y);
    add_hidden(L"EDIT", L"0", WS_BORDER | ES_AUTOHSCROLL, 216, 302, 80, 25, IDC_TARGET_Z);
    add_hidden(L"BUTTON", L"Hold", BS_AUTOCHECKBOX, 306, 302, 60, 25, IDC_HOLD);
    add_hidden(L"BUTTON", L"Move", BS_DEFPUSHBUTTON, 376, 302, 70, 25, IDC_TELEPORT);
    add_hidden(L"BUTTON", L"Restore", BS_PUSHBUTTON, 456, 302, 70, 25, IDC_RESTORE);
    add_hidden(L"EDIT", L"1.00", WS_BORDER | ES_AUTOHSCROLL, 536, 302, 58, 25, IDC_VR_SCALE);
    add_hidden(L"EDIT", L"1.00", WS_BORDER | ES_AUTOHSCROLL, 604, 302, 58, 25, IDC_VR_Y_SCALE);
    add_hidden(L"BUTTON", L"Non VR", BS_AUTOCHECKBOX, 672, 302, 76, 25, IDC_VR_NON_VR);
    add_hidden(L"BUTTON", L"Invert XZ", BS_AUTOCHECKBOX, 672, 330, 96, 25, IDC_VR_INVERT_Z);
    add_hidden(L"BUTTON", L"Swap eyes", BS_AUTOCHECKBOX, 672, 358, 96, 25, IDC_VR_SWAP_EYES);

    add(L"STATIC", L"Ready. Scan the player block, then click Center VR.", WS_BORDER | SS_LEFT,
        200, 680, 860, 46, IDC_STATUS);
#else
    add(L"STATIC", L"Coordonnees actuelles (pour reperer le joueur)", 0, 18, 16, 330, 20);
    add(L"STATIC", L"X", 0, 18, 44, 18, 22);
    add(L"EDIT", L"158.110", WS_BORDER | ES_AUTOHSCROLL, 36, 40, 105, 25, IDC_CURRENT_X);
    add(L"STATIC", L"Y", 0, 154, 44, 18, 22);
    add(L"EDIT", L"121.000", WS_BORDER | ES_AUTOHSCROLL, 172, 40, 105, 25, IDC_CURRENT_Y);
    add(L"STATIC", L"Z", 0, 290, 44, 18, 22);
    add(L"EDIT", L"256.118", WS_BORDER | ES_AUTOHSCROLL, 308, 40, 105, 25, IDC_CURRENT_Z);
    add(L"STATIC", L"Tolerance", 0, 432, 44, 65, 22);
    add(L"EDIT", L"0.10", WS_BORDER | ES_AUTOHSCROLL, 498, 40, 72, 25, IDC_TOLERANCE);

    add(L"BUTTON", L"Attacher", BS_PUSHBUTTON, 590, 39, 92, 28, IDC_ATTACH);
    add(L"BUTTON", L"Scanner", BS_DEFPUSHBUTTON, 692, 39, 92, 28, IDC_SCAN);

    add(L"STATIC", L"Blocs de coordonnees detectes", 0, 18, 81, 300, 20);
    add(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        18, 104, 766, 155, IDC_CANDIDATES);

    add(L"STATIC", L"Destination", 0, 18, 278, 120, 20);
    add(L"STATIC", L"X", 0, 18, 306, 18, 22);
    add(L"EDIT", L"158.110", WS_BORDER | ES_AUTOHSCROLL, 36, 302, 105, 25, IDC_TARGET_X);
    add(L"STATIC", L"Y", 0, 154, 306, 18, 22);
    add(L"EDIT", L"121.000", WS_BORDER | ES_AUTOHSCROLL, 172, 302, 105, 25, IDC_TARGET_Y);
    add(L"STATIC", L"Z", 0, 290, 306, 18, 22);
    add(L"EDIT", L"256.118", WS_BORDER | ES_AUTOHSCROLL, 308, 302, 105, 25, IDC_TARGET_Z);

    add(L"BUTTON", L"Maintenir", BS_AUTOCHECKBOX, 432, 302, 100, 25, IDC_HOLD);
    add(L"BUTTON", L"Teleporter", BS_DEFPUSHBUTTON, 546, 299, 112, 31, IDC_TELEPORT);
    add(L"BUTTON", L"Restaurer", BS_PUSHBUTTON, 670, 299, 114, 31, IDC_RESTORE);

    add(L"STATIC", L"Lance Hytale, recopie les coordonnees affichees en jeu, puis Attacher > Scanner.",
        0, 18, 350, 766, 22);
    add(L"STATIC", L"Pret. Aucune injection n'est utilisee.", WS_BORDER | SS_LEFT,
        18, 378, 766, 46, IDC_STATUS);
#endif

    for (HWND item : controls) SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
#ifdef HYTALE_CAMERA_MODE
    CheckDlgButton(g_window, IDC_VR_QUEST_LOCOMOTION, BST_CHECKED);
    CheckDlgButton(g_window, IDC_VR_STEREO, BST_CHECKED);
    CheckDlgButton(g_window, IDC_VR_HAND_POINTER, BST_CHECKED);
    CheckDlgButton(g_window, IDC_VR_HIDE_RETICLE, BST_CHECKED);
    CheckDlgButton(g_window, IDC_VR_DISABLE_FOREGROUND_EFFECTS, BST_CHECKED);
    CheckDlgButton(g_window, IDC_VR_DISABLE_SHADOWS, BST_CHECKED);
    CheckDlgButton(g_window, IDC_VR_DISABLE_PARTICLES, BST_CHECKED);
    CheckDlgButton(g_window, IDC_VR_DISABLE_DISTORTION, BST_CHECKED);
    CheckDlgButton(g_window, IDC_VR_MENU_MOUSE, BST_CHECKED);
    CheckDlgButton(g_window, IDC_VR_NO_UBO_UI, BST_CHECKED);
    CheckDlgButton(g_window, IDC_VR_HIDE_FIRST_PERSON_HAND, BST_CHECKED);
    update_advanced_visibility();
#endif
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
#ifdef HYTALE_CAMERA_MODE
    case kScanCompleteMessage: {
        ScanResult* result = g_pending_scan_result.exchange(nullptr);
        if (!result) return 0;
        if (g_scan_thread) {
            CloseHandle(g_scan_thread);
            g_scan_thread = nullptr;
        }
        EnableWindow(control(IDC_ATTACH), TRUE);
        EnableWindow(control(IDC_SCAN), TRUE);
        if (result->feet_found) {
            g_f7_allocation_base = result->f7_allocation_base;
            set_float(IDC_CURRENT_X, result->feet.x);
            set_float(IDC_CURRENT_Y, result->feet.y);
            set_float(IDC_CURRENT_Z, result->feet.z);
            set_float(IDC_TARGET_X, result->feet.x);
            set_float(IDC_TARGET_Y, result->feet.y);
            set_float(IDC_TARGET_Z, result->feet.z);
            g_candidates = std::move(result->candidates);
            populate_candidate_list();
            if (!g_candidates.empty()) {
                SendMessageW(control(IDC_CANDIDATES), LB_SETCURSEL, 0, 0);
            }
        }
        set_status(result->status);
        delete result;
        return 0;
    }
#endif
    case WM_CREATE:
        enable_mica_material(window);
        g_background_brush = CreateSolidBrush(kLuaToolsBgRef);
        g_edit_background_brush = CreateSolidBrush(kLuaToolsInputRef);
        g_list_background_brush = CreateSolidBrush(kLuaToolsInputRef);
        create_ui(window);
#ifdef HYTALE_CAMERA_MODE
        layout_controls_for_current_tab();
        update_tab_visibility();
#endif
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wparam;
        if (message == WM_CTLCOLORSTATIC) {
            SetTextColor(hdc, kLuaToolsTextRef);
            SetBkMode(hdc, TRANSPARENT);
            return (INT_PTR)GetStockObject(NULL_BRUSH);
        } else if (message == WM_CTLCOLOREDIT) {
            SetTextColor(hdc, kLuaToolsTextRef);
            SetBkColor(hdc, kLuaToolsInputRef);
            return (INT_PTR)g_edit_background_brush;
        } else if (message == WM_CTLCOLORLISTBOX) {
            SetTextColor(hdc, kLuaToolsTextRef);
            SetBkColor(hdc, kLuaToolsInputRef);
            return (INT_PTR)g_list_background_brush;
        }
        break;
    }
#ifdef HYTALE_CAMERA_MODE
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(window, &ps);
        RECT rect;
        GetClientRect(window, &rect);
        int w = rect.right - rect.left;
        int h = rect.bottom - rect.top;
        
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
        
        {
            Gdiplus::Graphics g(memDC);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            
            // Let native Acrylic/Mica show through, then add a readability veil.
            g.Clear(gdip_argb(0, 0, 0, 0));
            Gdiplus::SolidBrush readabilityVeil(gdip_argb(142, 7, 7, 12));
            g.FillRectangle(&readabilityVeil, 0, 0, w, h);

            Gdiplus::GraphicsPath micaGlowPath;
            micaGlowPath.AddEllipse(-180.0f, -220.0f, 760.0f, 520.0f);
            Gdiplus::PathGradientBrush micaGlow(&micaGlowPath);
            micaGlow.SetCenterColor(gdip_argb(38, 118, 70, 160));
            Gdiplus::Color glowStops[] = { gdip_argb(0, 118, 70, 160) };
            int glowCount = 1;
            micaGlow.SetSurroundColors(glowStops, &glowCount);
            g.FillPath(&micaGlow, &micaGlowPath);
            
            // Sidebar
            Gdiplus::SolidBrush sidebarBrush(gdip_argb(220, 12, 12, 16));
            g.FillRectangle(&sidebarBrush, 0, 0, 180, h);
            
            Gdiplus::Pen divPen(gdip_argb(70, 255, 255, 255), 1.0f);
            g.DrawLine(&divPen, 180.0f, 0.0f, 180.0f, static_cast<float>(h));
            
            // Logo
            Gdiplus::FontFamily fontFamily(L"Segoe UI");
            Gdiplus::Font logoFont(&fontFamily, 16.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
            Gdiplus::SolidBrush logoBrush(gdip_rgb(229, 231, 235));
            g.DrawString(L"Hytale VR", -1, &logoFont, Gdiplus::PointF(20.0f, 25.0f), &logoBrush);
            g.DrawLine(&divPen, 20.0f, 65.0f, 160.0f, 65.0f);
            
            // Panels
            Gdiplus::Color panelBorderColor(44, 255, 255, 255);
            Gdiplus::Color panelBkColor(224, 13, 13, 17);
            Gdiplus::SolidBrush panelBrush(panelBkColor);
            Gdiplus::Pen panelPen(panelBorderColor, 1.2f);
            float r = 8.0f;
            
            auto drawPanel = [&](float x, float y, float width, float height, const wchar_t* title) {
                Gdiplus::GraphicsPath path;
                path.AddArc(x, y, r, r, 180, 90);
                path.AddArc(x + width - r, y, r, r, 270, 90);
                path.AddArc(x + width - r, y + height - r, r, r, 0, 90);
                path.AddArc(x, y + height - r, r, r, 90, 90);
                path.CloseFigure();
                
                g.FillPath(&panelBrush, &path);
                g.DrawPath(&panelPen, &path);
                
                if (title && wcslen(title) > 0) {
                    Gdiplus::Font panelTitleFont(&fontFamily, 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                    Gdiplus::SolidBrush titleBrush(gdip_rgb(229, 231, 235));
                    g.DrawString(title, -1, &panelTitleFont, Gdiplus::PointF(x + 15.0f, y + 12.0f), &titleBrush);
                }
            };
            
            if (g_current_tab == 0) {
                const bool advanced = advanced_options_enabled();
                drawPanel(200.0f, 20.0f, 860.0f, 95.0f, L"Quick Actions");
                drawPanel(200.0f, 130.0f, 860.0f, 95.0f, L"Player Coordinate Block");
                if (advanced) drawPanel(200.0f, 240.0f, 860.0f, 430.0f, L"Advanced Options");
                
                Gdiplus::Font colHeaderFont(&fontFamily, 9.5f, Gdiplus::FontStyleBold | Gdiplus::FontStyleUnderline, Gdiplus::UnitPoint);
                Gdiplus::SolidBrush colHeaderBrush(gdip_rgb(229, 231, 235));
                
                Gdiplus::Font labelFont(&fontFamily, 9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                Gdiplus::SolidBrush labelBrush(gdip_rgb(156, 163, 175));
                
                if (advanced) {
                    g.DrawString(L"Scan Tuning", -1, &colHeaderFont, Gdiplus::PointF(215.0f, 275.0f), &colHeaderBrush);
                    g.DrawString(L"Rendering Settings", -1, &colHeaderFont, Gdiplus::PointF(405.0f, 275.0f), &colHeaderBrush);
                    g.DrawString(L"Keybindings", -1, &colHeaderFont, Gdiplus::PointF(605.0f, 275.0f), &colHeaderBrush);
                    g.DrawString(L"UI/Pointer Settings", -1, &colHeaderFont, Gdiplus::PointF(815.0f, 275.0f), &colHeaderBrush);
                    g.DrawString(L"X", -1, &labelFont, Gdiplus::PointF(215.0f, 308.0f), &labelBrush);
                    g.DrawString(L"Y", -1, &labelFont, Gdiplus::PointF(215.0f, 338.0f), &labelBrush);
                    g.DrawString(L"Z", -1, &labelFont, Gdiplus::PointF(215.0f, 368.0f), &labelBrush);
                    g.DrawString(L"Tolerance", -1, &labelFont, Gdiplus::PointF(215.0f, 398.0f), &labelBrush);
                    g.DrawString(L"floor tilt", -1, &labelFont, Gdiplus::PointF(215.0f, 428.0f), &labelBrush);
                    g.DrawString(L"IPD mm", -1, &labelFont, Gdiplus::PointF(405.0f, 504.0f), &labelBrush);
                    g.DrawString(L"Separation %", -1, &labelFont, Gdiplus::PointF(405.0f, 532.0f), &labelBrush);
                    g.DrawString(L"VR Resolution", -1, &labelFont, Gdiplus::PointF(405.0f, 560.0f), &labelBrush);
                    g.DrawString(L"Custom Keys", -1, &labelFont, Gdiplus::PointF(605.0f, 308.0f), &labelBrush);
                    g.DrawString(L"Forward", -1, &labelFont, Gdiplus::PointF(605.0f, 332.0f), &labelBrush);
                    g.DrawString(L"Backward", -1, &labelFont, Gdiplus::PointF(605.0f, 358.0f), &labelBrush);
                    g.DrawString(L"Left", -1, &labelFont, Gdiplus::PointF(605.0f, 384.0f), &labelBrush);
                    g.DrawString(L"Right", -1, &labelFont, Gdiplus::PointF(605.0f, 410.0f), &labelBrush);
                    g.DrawString(L"Jump", -1, &labelFont, Gdiplus::PointF(605.0f, 436.0f), &labelBrush);
                    g.DrawString(L"Sprint", -1, &labelFont, Gdiplus::PointF(605.0f, 462.0f), &labelBrush);
                    g.DrawString(L"Use", -1, &labelFont, Gdiplus::PointF(605.0f, 488.0f), &labelBrush);
                    g.DrawString(L"Inventory", -1, &labelFont, Gdiplus::PointF(605.0f, 514.0f), &labelBrush);
                    g.DrawString(L"Deadzone", -1, &labelFont, Gdiplus::PointF(815.0f, 408.0f), &labelBrush);
                    g.DrawString(L"Turn speed", -1, &labelFont, Gdiplus::PointF(925.0f, 408.0f), &labelBrush);
                    g.DrawString(L"Pointer m", -1, &labelFont, Gdiplus::PointF(815.0f, 460.0f), &labelBrush);
                    g.DrawString(L"Menu m", -1, &labelFont, Gdiplus::PointF(925.0f, 460.0f), &labelBrush);
                    g.DrawString(L"Width", -1, &labelFont, Gdiplus::PointF(815.0f, 512.0f), &labelBrush);
                    g.DrawString(L"UI Scale", -1, &labelFont, Gdiplus::PointF(925.0f, 512.0f), &labelBrush);
                    g.DrawString(L"Eye offset", -1, &labelFont, Gdiplus::PointF(815.0f, 564.0f), &labelBrush);
                    g.DrawString(L"UI Y", -1, &labelFont, Gdiplus::PointF(925.0f, 564.0f), &labelBrush);
                    g.DrawString(L"Ignore draw", -1, &labelFont, Gdiplus::PointF(815.0f, 616.0f), &labelBrush);
                }
            } else if (g_current_tab == 1) {
                drawPanel(200.0f, 20.0f, 780.0f, 95.0f, L"Player Coordinate Block");
                drawPanel(200.0f, 130.0f, 780.0f, 300.0f, L"Scan Tuning");
                
                Gdiplus::Font labelFont(&fontFamily, 9.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                Gdiplus::SolidBrush labelBrush(gdip_rgb(156, 163, 175));
                
                g.DrawString(L"X Coordinate", -1, &labelFont, Gdiplus::PointF(220.0f, 178.0f), &labelBrush);
                g.DrawString(L"Y Coordinate", -1, &labelFont, Gdiplus::PointF(220.0f, 208.0f), &labelBrush);
                g.DrawString(L"Z Coordinate", -1, &labelFont, Gdiplus::PointF(220.0f, 238.0f), &labelBrush);
                g.DrawString(L"Tolerance", -1, &labelFont, Gdiplus::PointF(220.0f, 268.0f), &labelBrush);
                g.DrawString(L"floor tilt", -1, &labelFont, Gdiplus::PointF(220.0f, 298.0f), &labelBrush);
            } else if (g_current_tab == 2) {
                drawPanel(200.0f, 20.0f, 780.0f, 570.0f, L"Rendering Settings");
                
                Gdiplus::Font labelFont(&fontFamily, 9.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                Gdiplus::SolidBrush labelBrush(gdip_rgb(156, 163, 175));
                
                g.DrawString(L"IPD mm", -1, &labelFont, Gdiplus::PointF(220.0f, 508.0f), &labelBrush);
                g.DrawString(L"Separation %", -1, &labelFont, Gdiplus::PointF(220.0f, 538.0f), &labelBrush);
                g.DrawString(L"VR Resolution %", -1, &labelFont, Gdiplus::PointF(220.0f, 568.0f), &labelBrush);
            } else if (g_current_tab == 3) {
                drawPanel(200.0f, 20.0f, 780.0f, 570.0f, L"Keybindings");
                
                Gdiplus::Font labelFont(&fontFamily, 10.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                Gdiplus::SolidBrush labelBrush(gdip_rgb(156, 163, 175));
                
                g.DrawString(L"Forward Key Override", -1, &labelFont, Gdiplus::PointF(220.0f, 72.0f), &labelBrush);
                g.DrawString(L"Backward Key Override", -1, &labelFont, Gdiplus::PointF(220.0f, 102.0f), &labelBrush);
                g.DrawString(L"Left Key Override", -1, &labelFont, Gdiplus::PointF(220.0f, 132.0f), &labelBrush);
                g.DrawString(L"Right Key Override", -1, &labelFont, Gdiplus::PointF(220.0f, 162.0f), &labelBrush);
                g.DrawString(L"Jump Key Override", -1, &labelFont, Gdiplus::PointF(220.0f, 192.0f), &labelBrush);
                g.DrawString(L"Sprint Key Override", -1, &labelFont, Gdiplus::PointF(220.0f, 222.0f), &labelBrush);
                g.DrawString(L"Use Key Override", -1, &labelFont, Gdiplus::PointF(220.0f, 252.0f), &labelBrush);
                g.DrawString(L"Inventory Key Override", -1, &labelFont, Gdiplus::PointF(220.0f, 282.0f), &labelBrush);
            } else if (g_current_tab == 4) {
                drawPanel(200.0f, 20.0f, 780.0f, 570.0f, L"UI/Pointer Settings");
                
                Gdiplus::Font labelFont(&fontFamily, 9.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                Gdiplus::SolidBrush labelBrush(gdip_rgb(156, 163, 175));
                
                g.DrawString(L"Deadzone", -1, &labelFont, Gdiplus::PointF(220.0f, 376.0f), &labelBrush);
                g.DrawString(L"Turn speed", -1, &labelFont, Gdiplus::PointF(280.0f, 376.0f), &labelBrush);
                g.DrawString(L"Pointer m", -1, &labelFont, Gdiplus::PointF(345.0f, 376.0f), &labelBrush);
                
                g.DrawString(L"Menu m", -1, &labelFont, Gdiplus::PointF(220.0f, 424.0f), &labelBrush);
                g.DrawString(L"Width", -1, &labelFont, Gdiplus::PointF(280.0f, 424.0f), &labelBrush);
                g.DrawString(L"UI Scale", -1, &labelFont, Gdiplus::PointF(345.0f, 424.0f), &labelBrush);
                
                g.DrawString(L"Eye offset", -1, &labelFont, Gdiplus::PointF(220.0f, 472.0f), &labelBrush);
                g.DrawString(L"UI Y", -1, &labelFont, Gdiplus::PointF(310.0f, 472.0f), &labelBrush);
                
                g.DrawString(L"Ignore draw", -1, &labelFont, Gdiplus::PointF(220.0f, 520.0f), &labelBrush);
            }
        }
        
        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);
        
        EndPaint(window, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
#endif
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
#ifdef HYTALE_CAMERA_MODE
        case IDC_TAB_DASHBOARD:
            g_current_tab = 0;
            layout_controls_for_current_tab();
            update_tab_visibility();
            return 0;
        case IDC_TAB_SETTINGS:
            g_current_tab = 1;
            layout_controls_for_current_tab();
            update_tab_visibility();
            return 0;
        case IDC_TAB_RENDERING:
            g_current_tab = 2;
            layout_controls_for_current_tab();
            update_tab_visibility();
            return 0;
        case IDC_TAB_KEYBINDINGS:
            g_current_tab = 3;
            layout_controls_for_current_tab();
            update_tab_visibility();
            return 0;
        case IDC_TAB_UI:
            g_current_tab = 4;
            layout_controls_for_current_tab();
            update_tab_visibility();
            return 0;
#endif
#ifdef HYTALE_CAMERA_MODE
        case IDC_SCAN: start_f7_scan(); return 0;
#else
        case IDC_ATTACH: attach(); return 0;
        case IDC_SCAN: scan_candidates(); return 0;
#endif
#ifndef HYTALE_CAMERA_MODE
        case IDC_TELEPORT: teleport(); return 0;
        case IDC_RESTORE: restore(); return 0;
#endif
#ifdef HYTALE_CAMERA_MODE
        case IDC_VR_ADVANCED_OPTIONS:
            if (IsDlgButtonChecked(g_window, IDC_VR_ADVANCED_OPTIONS) == BST_CHECKED) {
                const int choice = MessageBoxW(
                    g_window,
                    L"Advanced options can affect VR stability, rendering, input bindings, and UI placement.\n\n"
                    L"Only change these settings if you know what they do.",
                    L"Advanced options warning",
                    MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON2);
                if (choice != IDOK) {
                    CheckDlgButton(g_window, IDC_VR_ADVANCED_OPTIONS, BST_UNCHECKED);
                    set_status(L"Advanced options were not enabled.");
                }
            }
            update_advanced_visibility();
            populate_candidate_list();
            return 0;
        case IDC_VR_CENTER: recenter_vr_tracking(); return 0;
        case IDC_VR_CALIBRATE_FLOOR: calibrate_floor_tilt(); return 0;
        case IDC_VR_STOP:
            shutdown_vr();
            set_status(L"VR tracking stopped.");
            return 0;
#endif
        case IDC_CANDIDATES:
            if (HIWORD(wparam) == LBN_SELCHANGE) {
                const int selected = selected_candidate();
                if (selected >= 0 && static_cast<size_t>(selected) < g_candidates.size()) {
                    Vec3 value{};
                    if (read_exact(g_candidates[selected].base + 0x288, &value, sizeof(value))) {
                        set_float(IDC_TARGET_X, value.x);
                        set_float(IDC_TARGET_Y, value.y);
                        set_float(IDC_TARGET_Z, value.z);
                    }
                }
            }
            return 0;
        }
        break;
    case WM_TIMER:
#ifdef HYTALE_CAMERA_MODE
        if (wparam == kVrTimer) {
            update_vr_tracking();
            return 0;
        }
#endif
#ifndef HYTALE_CAMERA_MODE
        if (wparam == kWriteTimer) {
            const bool hold = IsDlgButtonChecked(window, IDC_HOLD) == BST_CHECKED;
            if ((hold || GetTickCount64() < g_burst_until) && write_target()) return 0;
            KillTimer(window, kWriteTimer);
            if (!hold && GetTickCount64() >= g_burst_until) set_status(L"Write finished. Check the in-game position.");
        }
#endif
        return 0;
    case WM_DESTROY:
        if (g_background_brush) DeleteObject(g_background_brush);
        if (g_edit_background_brush) DeleteObject(g_edit_background_brush);
        if (g_list_background_brush) DeleteObject(g_list_background_brush);
#ifdef HYTALE_CAMERA_MODE
        g_scan_cancel.store(true, std::memory_order_release);
        if (g_scan_thread) {
            WaitForSingleObject(g_scan_thread, INFINITE);
            CloseHandle(g_scan_thread);
            g_scan_thread = nullptr;
        }
        delete g_pending_scan_result.exchange(nullptr);
        shutdown_vr();
        unload_vr_hook();
        if (g_vr_camera_shared) UnmapViewOfFile(g_vr_camera_shared);
        if (g_vr_camera_mapping) CloseHandle(g_vr_camera_mapping);
#endif
        detach();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE,
                    _In_ PWSTR, _In_ int show_command) {
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr);

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    window_class.lpszClassName = kWindowClass;
    RegisterClassExW(&window_class);

#ifdef HYTALE_CAMERA_MODE
    const wchar_t* title = L"Hytale VR Dashboard";
    const int window_width = 1100;
    const int window_height = 760;
#else
    const wchar_t* title = L"Hytale Player Teleporter";
    const int window_width = 820;
    const int window_height = 480;
#endif
    HWND window = CreateWindowExW(0, kWindowClass, title,
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                  CW_USEDEFAULT, CW_USEDEFAULT, window_width, window_height,
                                  nullptr, nullptr, instance, nullptr);
    if (!window) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 1;
    }
    ShowWindow(window, show_command);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    return static_cast<int>(message.wParam);
}
