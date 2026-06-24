#include "hotkey.h"

#include <sstream>

bool HotkeyManager::Register(HWND hwnd, UINT modifiers, UINT virtualKey) {
    Unregister(hwnd);
    return RegisterHotKey(hwnd, kHotkeyId, modifiers, virtualKey) != FALSE;
}

void HotkeyManager::Unregister(HWND hwnd) {
    UnregisterHotKey(hwnd, kHotkeyId);
}

std::wstring HotkeyManager::FormatHotkey(UINT modifiers, UINT virtualKey) {
    std::wstringstream stream;

    if (modifiers & MOD_CONTROL) {
        stream << L"Ctrl+";
    }
    if (modifiers & MOD_SHIFT) {
        stream << L"Shift+";
    }
    if (modifiers & MOD_ALT) {
        stream << L"Alt+";
    }
    if (modifiers & MOD_WIN) {
        stream << L"Win+";
    }

    wchar_t keyName[64] = {};
    const UINT scanCode = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC);
    const LONG lParam = static_cast<LONG>(scanCode << 16);
    if (GetKeyNameTextW(lParam, keyName, 64) > 0) {
        stream << keyName;
    } else if (virtualKey >= 'A' && virtualKey <= 'Z') {
        stream << static_cast<wchar_t>(virtualKey);
    } else if (virtualKey >= '0' && virtualKey <= '9') {
        stream << static_cast<wchar_t>(virtualKey);
    } else {
        stream << L"VK" << virtualKey;
    }

    return stream.str();
}
