#pragma once

#include <windows.h>

#include <string>

class HotkeyManager {
public:
    static constexpr int kHotkeyId = 1;

    bool Register(HWND hwnd, UINT modifiers, UINT virtualKey);
    void Unregister(HWND hwnd);
    static std::wstring FormatHotkey(UINT modifiers, UINT virtualKey);
};
