#pragma once

#include <windows.h>

#include <cwchar>
#include <cwctype>
#include <string>

namespace hytalevr {

inline WORD scan_code_for_virtual_key(WORD virtual_key, HKL keyboard_layout) {
    UINT scan_code = MapVirtualKeyExW(virtual_key, MAPVK_VK_TO_VSC, keyboard_layout);
    if (scan_code == 0) {
        scan_code = MapVirtualKeyW(virtual_key, MAPVK_VK_TO_VSC);
    }
    return static_cast<WORD>(scan_code);
}

inline WORD virtual_key_for_physical_scan(UINT scan_code,
                                          WORD fallback,
                                          HKL keyboard_layout) {
    const UINT virtual_key =
        MapVirtualKeyExW(scan_code, MAPVK_VSC_TO_VK_EX, keyboard_layout);
    return static_cast<WORD>(virtual_key != 0 ? virtual_key : fallback);
}

struct MovementKeys {
    WORD forward = 'W';
    WORD backward = 'S';
    WORD left = 'A';
    WORD right = 'D';
};

inline MovementKeys movement_keys_for_layout(HKL keyboard_layout) {
    // Physical QWERTY WASD positions. On AZERTY these resolve to ZQSD.
    constexpr UINT kPhysicalW = 0x11;
    constexpr UINT kPhysicalA = 0x1E;
    constexpr UINT kPhysicalS = 0x1F;
    constexpr UINT kPhysicalD = 0x20;
    return {
        virtual_key_for_physical_scan(kPhysicalW, 'W', keyboard_layout),
        virtual_key_for_physical_scan(kPhysicalS, 'S', keyboard_layout),
        virtual_key_for_physical_scan(kPhysicalA, 'A', keyboard_layout),
        virtual_key_for_physical_scan(kPhysicalD, 'D', keyboard_layout),
    };
}

inline std::wstring trim_key_text(std::wstring text) {
    while (!text.empty() && std::iswspace(text.front())) {
        text.erase(text.begin());
    }
    while (!text.empty() && std::iswspace(text.back())) {
        text.pop_back();
    }
    return text;
}

inline std::wstring upper_key_text(std::wstring text) {
    for (wchar_t& ch : text) {
        ch = static_cast<wchar_t>(std::towupper(ch));
    }
    return text;
}

inline WORD parse_key_name(const std::wstring& raw_text,
                           WORD fallback,
                           HKL keyboard_layout) {
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
        const long function_key = std::wcstol(text.c_str() + 1, &end, 10);
        if (end && *end == L'\0' && function_key >= 1 && function_key <= 24) {
            return static_cast<WORD>(VK_F1 + function_key - 1);
        }
    }
    if (text.size() == 1) {
        const wchar_t ch = text[0];
        if ((ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9')) {
            return static_cast<WORD>(ch);
        }
        const SHORT mapped = VkKeyScanExW(ch, keyboard_layout);
        if (mapped != -1) return static_cast<WORD>(mapped & 0xff);
    }
    return fallback;
}

} // namespace hytalevr
