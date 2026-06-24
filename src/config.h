#pragma once

#include <windows.h>

#include <string>
#include <vector>

struct ProgramEntry {
    std::wstring path;
    bool blocked = false;
};

struct Config {
    UINT hotkeyModifiers = MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT;
    UINT hotkeyVirtualKey = 'B';
    std::vector<ProgramEntry> programs;

    static std::wstring GetConfigPath();
    bool Load();
    bool Save() const;
};

bool IsAllowedSteamProgram(const std::wstring& path);
