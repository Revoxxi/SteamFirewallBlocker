#include "config.h"

#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace {

std::wstring Trim(const std::wstring& value) {
    const auto start = value.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) {
        return L"";
    }
    const auto end = value.find_last_not_of(L" \t\r\n");
    return value.substr(start, end - start + 1);
}

bool ParseBool(const std::wstring& value, bool& out) {
    if (value == L"1" || value == L"true" || value == L"True") {
        out = true;
        return true;
    }
    if (value == L"0" || value == L"false" || value == L"False") {
        out = false;
        return true;
    }
    return false;
}

void KeepOnlyAllowedSteamPrograms(std::vector<ProgramEntry>& programs) {
    programs.erase(
        std::remove_if(
            programs.begin(),
            programs.end(),
            [](const ProgramEntry& entry) { return !IsAllowedSteamProgram(entry.path); }),
        programs.end());
}

}  // namespace

bool IsAllowedSteamProgram(const std::wstring& path) {
    const wchar_t* name = PathFindFileNameW(path.c_str());
    std::wstring fileName = name ? std::wstring(name) : path;
    std::transform(fileName.begin(), fileName.end(), fileName.begin(), ::towlower);
    return fileName == L"steam.exe" || fileName == L"steamservice.exe";
}

std::wstring Config::GetConfigPath() {
    wchar_t appData[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData))) {
        return L"config.ini";
    }

    std::wstring dir = appData;
    dir += L"\\SteamFirewallBlocker";
    CreateDirectoryW(dir.c_str(), nullptr);
    dir += L"\\config.ini";
    return dir;
}

bool Config::Load() {
    programs.clear();
    hotkeyModifiers = MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT;
    hotkeyVirtualKey = 'B';

    std::wifstream file(GetConfigPath());
    if (!file) {
        return false;
    }

    std::wstring line;
    enum class Section { None, Hotkey, Programs };
    Section section = Section::None;

    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == L';' || line[0] == L'#') {
            continue;
        }

        if (line == L"[hotkey]") {
            section = Section::Hotkey;
            continue;
        }
        if (line == L"[programs]") {
            section = Section::Programs;
            continue;
        }

        const auto eq = line.find(L'=');
        if (eq == std::wstring::npos) {
            continue;
        }

        const std::wstring key = Trim(line.substr(0, eq));
        const std::wstring value = Trim(line.substr(eq + 1));

        if (section == Section::Hotkey) {
            if (key == L"modifiers") {
                hotkeyModifiers = static_cast<UINT>(std::stoul(value));
            } else if (key == L"vk") {
                hotkeyVirtualKey = static_cast<UINT>(std::stoul(value));
            }
        } else if (section == Section::Programs) {
            ProgramEntry entry;
            entry.path = key;
            if (!ParseBool(value, entry.blocked)) {
                entry.blocked = false;
            }
            programs.push_back(std::move(entry));
        }
    }

    const size_t countBeforeFilter = programs.size();
    KeepOnlyAllowedSteamPrograms(programs);
    if (programs.size() != countBeforeFilter) {
        Save();
    }
    return true;
}

bool Config::Save() const {
    std::wofstream file(GetConfigPath());
    if (!file) {
        return false;
    }

    file << L"[hotkey]\n";
    file << L"modifiers=" << hotkeyModifiers << L"\n";
    file << L"vk=" << hotkeyVirtualKey << L"\n\n";
    file << L"[programs]\n";
    for (const auto& program : programs) {
        file << program.path << L"=" << (program.blocked ? L"1" : L"0") << L"\n";
    }

    return true;
}
