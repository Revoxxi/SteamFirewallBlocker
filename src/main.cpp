#include "config.h"
#include "firewall.h"
#include "hotkey.h"
#include "resource.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "advapi32.lib")

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace {

constexpr wchar_t kWindowClassName[] = L"SteamFirewallBlockerMainWindow";
constexpr wchar_t kAppTitle[] = L"Steam Firewall Blocker";
constexpr wchar_t kWindowCaption[] = L"Steam Firewall Blocker \u00b7 Revoxxi";
constexpr wchar_t kHeaderTitle[] = L"Steam Firewall Blocker";
constexpr wchar_t kSupplierText[] = L"Supplied by Revoxxi";

struct AppState {
    Config config;
    FirewallManager firewall;
    HotkeyManager hotkey;
    HWND hwnd = nullptr;
    HWND listView = nullptr;
    HWND hotkeyDisplay = nullptr;
    HWND statusBar = nullptr;
    HWND titleLabel = nullptr;
    HWND supplierLabel = nullptr;
    HFONT titleFont = nullptr;
    HFONT supplierFont = nullptr;
    bool capturingHotkey = false;
    UINT pendingModifiers = 0;
    UINT pendingVirtualKey = 0;
};

AppState* g_app = nullptr;

std::wstring FileNameFromPath(const std::wstring& path) {
    const wchar_t* name = PathFindFileNameW(path.c_str());
    return name ? std::wstring(name) : path;
}

std::wstring ToLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), ::towlower);
    return value;
}

std::wstring SteamExecutableName(const std::wstring& path) {
    const wchar_t* name = PathFindFileNameW(path.c_str());
    return name ? ToLower(std::wstring(name)) : ToLower(path);
}

bool IsProgramNameListed(const std::vector<ProgramEntry>& programs, const std::wstring& path) {
    const std::wstring targetName = SteamExecutableName(path);
    for (const auto& program : programs) {
        if (SteamExecutableName(program.path) == targetName) {
            return true;
        }
    }
    return false;
}

bool TryReadRegistryString(HKEY root, const wchar_t* subkey, const wchar_t* valueName, std::wstring& out) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }

    wchar_t buffer[MAX_PATH] = {};
    DWORD bufferBytes = sizeof(buffer);
    DWORD type = 0;
    const LSTATUS status =
        RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &bufferBytes);
    RegCloseKey(key);

    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return false;
    }

    out = buffer;
    return !out.empty();
}

std::wstring FindSteamInstallPath() {
    std::wstring path;
    if (TryReadRegistryString(
            HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath", path)) {
        return path;
    }
    if (TryReadRegistryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", path)) {
        return path;
    }
    return L"";
}

std::wstring JoinPath(const std::wstring& directory, const wchar_t* fileName) {
    wchar_t buffer[MAX_PATH] = {};
    wcscpy_s(buffer, directory.c_str());
    PathAppendW(buffer, fileName);
    return buffer;
}

std::wstring JoinPath(const std::wstring& directory, const std::wstring& fileName) {
    return JoinPath(directory, fileName.c_str());
}

std::wstring JoinRelativePath(const std::wstring& baseDirectory, const wchar_t* relativePath) {
    wchar_t buffer[MAX_PATH] = {};
    wcscpy_s(buffer, baseDirectory.c_str());

    wchar_t relativeCopy[MAX_PATH] = {};
    wcscpy_s(relativeCopy, relativePath);

    wchar_t* context = nullptr;
    for (wchar_t* part = wcstok_s(relativeCopy, L"\\", &context); part != nullptr;
         part = wcstok_s(nullptr, L"\\", &context)) {
        PathAppendW(buffer, part);
    }

    return buffer;
}

std::vector<std::wstring> RelativePathsForExecutable(const wchar_t* executableName) {
    if (_wcsicmp(executableName, L"steamwebhelper.exe") == 0) {
        return {
            L"steamwebhelper.exe",
            L"bin\\steamwebhelper.exe",
            L"bin\\cef\\cef.win64\\steamwebhelper.exe",
            L"bin\\cef\\cef.win7x64\\steamwebhelper.exe",
        };
    }
    if (_wcsicmp(executableName, L"steamservice.exe") == 0) {
        return {
            L"steamservice.exe",
            L"bin\\steamservice.exe",
        };
    }
    return {
        L"steam.exe",
        L"bin\\steam.exe",
    };
}

std::vector<std::wstring> CollectSteamExecutablePaths(const std::wstring& steamDirectory) {
    static const wchar_t* kExecutableNames[] = {
        L"steam.exe",
        L"steamservice.exe",
        L"steamwebhelper.exe",
    };

    std::vector<std::wstring> paths;
    for (const wchar_t* executableName : kExecutableNames) {
        for (const std::wstring& relativePath : RelativePathsForExecutable(executableName)) {
            const std::wstring candidate = JoinRelativePath(steamDirectory, relativePath.c_str());
            if (!PathFileExistsW(candidate.c_str()) || !IsAllowedSteamProgram(candidate)) {
                continue;
            }

            const std::wstring name = SteamExecutableName(candidate);
            const bool alreadyListed = std::any_of(
                paths.begin(),
                paths.end(),
                [&](const std::wstring& existing) {
                    return SteamExecutableName(existing) == name;
                });
            if (!alreadyListed) {
                paths.push_back(candidate);
            }
            break;
        }
    }

    return paths;
}

void SetStatusText(HWND statusBar, const std::wstring& text);
void RefreshProgramList(AppState* app);

bool PromptForSteamExecutable(HWND owner, std::wstring& steamExePath) {
    wchar_t filePath[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"steam.exe\0steam.exe\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = L"Locate steam.exe";

    if (!GetOpenFileNameW(&ofn)) {
        return false;
    }

    if (!IsAllowedSteamProgram(filePath) || SteamExecutableName(filePath) != L"steam.exe") {
        MessageBoxW(
            owner,
            L"Only steam.exe from your Steam installation can be selected.",
            kAppTitle,
            MB_ICONWARNING);
        return false;
    }

    steamExePath = filePath;
    return true;
}

bool AddSteamPrograms(AppState* app) {
    std::wstring steamDirectory;

    const std::wstring installPath = FindSteamInstallPath();
    if (!installPath.empty() && PathFileExistsW(JoinPath(installPath, L"steam.exe").c_str())) {
        steamDirectory = installPath;
    }

    if (steamDirectory.empty()) {
        std::wstring steamExePath;
        if (!PromptForSteamExecutable(app->hwnd, steamExePath)) {
            return false;
        }

        wchar_t directory[MAX_PATH] = {};
        wcscpy_s(directory, steamExePath.c_str());
        PathRemoveFileSpecW(directory);
        steamDirectory = directory;
    }

    const std::vector<std::wstring> candidates = CollectSteamExecutablePaths(steamDirectory);
    int addedCount = 0;
    for (const std::wstring& path : candidates) {
        if (IsProgramNameListed(app->config.programs, path)) {
            continue;
        }

        ProgramEntry entry;
        entry.path = path;
        entry.blocked = false;
        app->config.programs.push_back(entry);
        ++addedCount;
    }

    if (addedCount == 0) {
        MessageBoxW(
            app->hwnd,
            L"All Steam executables are already listed, or steamwebhelper.exe was not found.",
            kAppTitle,
            MB_ICONINFORMATION);
        return false;
    }

    app->config.Save();
    RefreshProgramList(app);
    SetStatusText(app->statusBar, L"Steam programs added.");
    return true;
}

bool AddProgramManual(AppState* app) {
    wchar_t filePath[MAX_PATH] = {};
    std::wstring initialDirectory;
    const std::wstring installPath = FindSteamInstallPath();
    if (!installPath.empty()) {
        const std::wstring cefDirectory =
            JoinRelativePath(installPath, L"bin\\cef\\cef.win64");
        if (PathFileExistsW(cefDirectory.c_str())) {
            initialDirectory = cefDirectory;
        } else {
            initialDirectory = installPath;
        }
    }

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app->hwnd;
    ofn.lpstrFilter =
        L"Steam executables\0steam.exe;steamservice.exe;steamwebhelper.exe\0"
        L"Executables (*.exe)\0*.exe\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = L"Select Steam executable";
    if (!initialDirectory.empty()) {
        ofn.lpstrInitialDir = initialDirectory.c_str();
    }

    if (!GetOpenFileNameW(&ofn)) {
        return false;
    }

    if (!IsAllowedSteamProgram(filePath)) {
        MessageBoxW(
            app->hwnd,
            L"Only steam.exe, steamservice.exe, and steamwebhelper.exe can be added.",
            kAppTitle,
            MB_ICONWARNING);
        return false;
    }

    const std::wstring selectedPath = filePath;
    const std::wstring selectedName = SteamExecutableName(selectedPath);
    for (auto& program : app->config.programs) {
        if (SteamExecutableName(program.path) == selectedName) {
            program.path = selectedPath;
            app->config.Save();
            RefreshProgramList(app);
            SetStatusText(app->statusBar, L"Updated path for " + FileNameFromPath(selectedPath) + L".");
            return true;
        }
    }

    ProgramEntry entry;
    entry.path = selectedPath;
    entry.blocked = false;
    app->config.programs.push_back(entry);
    app->config.Save();
    RefreshProgramList(app);
    SetStatusText(app->statusBar, L"Added " + FileNameFromPath(selectedPath) + L".");
    return true;
}

void SetStatusText(HWND statusBar, const std::wstring& text) {
    SendMessageW(statusBar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
}

void RefreshHotkeyDisplay(AppState* app) {
    const std::wstring text =
        HotkeyManager::FormatHotkey(app->config.hotkeyModifiers, app->config.hotkeyVirtualKey);
    SetWindowTextW(app->hotkeyDisplay, text.c_str());
}

void RefreshProgramList(AppState* app) {
    ListView_DeleteAllItems(app->listView);
    for (size_t i = 0; i < app->config.programs.size(); ++i) {
        const auto& program = app->config.programs[i];

        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<wchar_t*>(FileNameFromPath(program.path).c_str());
        ListView_InsertItem(app->listView, &item);

        ListView_SetItemText(
            app->listView,
            static_cast<int>(i),
            1,
            const_cast<wchar_t*>(program.path.c_str()));
        ListView_SetItemText(
            app->listView,
            static_cast<int>(i),
            2,
            const_cast<wchar_t*>(program.blocked ? L"Blocked" : L"Allowed"));
    }
}

bool ApplyFirewallState(AppState* app, ProgramEntry& program) {
    if (app->firewall.SetBlocked(program.path, program.blocked)) {
        return true;
    }

    const std::wstring details = app->firewall.LastErrorMessage();
    if (!details.empty()) {
        SetStatusText(app->statusBar, L"Firewall error: " + details);
    }
    return false;
}

void SyncFirewallFromRules(AppState* app) {
    for (auto& program : app->config.programs) {
        if (app->firewall.IsBlocked(program.path)) {
            program.blocked = true;
        }
    }
}

void ToggleAllPrograms(AppState* app) {
    if (app->config.programs.empty()) {
        SetStatusText(app->statusBar, L"No Steam programs configured. Click Add Steam.");
        return;
    }

    const bool anyAllowed =
        std::any_of(app->config.programs.begin(), app->config.programs.end(),
                    [](const ProgramEntry& entry) { return !entry.blocked; });
    const bool targetBlocked = anyAllowed;

    bool allSucceeded = true;
    for (auto& program : app->config.programs) {
        program.blocked = targetBlocked;
        if (!ApplyFirewallState(app, program)) {
            allSucceeded = false;
        }
    }

    app->config.Save();
    RefreshProgramList(app);

    if (allSucceeded) {
        SetStatusText(
            app->statusBar,
            targetBlocked
                ? L"Steam blocked. Teamkill now."
                : L"Steam unblocked. Click Reconnect in the menu.");
    } else {
        const std::wstring details = app->firewall.LastErrorMessage();
        if (!details.empty()) {
            SetStatusText(app->statusBar, L"Some firewall changes failed: " + details);
        } else {
            SetStatusText(app->statusBar, L"Some firewall changes failed.");
        }
    }
}

void ToggleSelectedProgram(AppState* app) {
    const int index = ListView_GetNextItem(app->listView, -1, LVNI_SELECTED);
    if (index < 0 || index >= static_cast<int>(app->config.programs.size())) {
        return;
    }

    auto& program = app->config.programs[static_cast<size_t>(index)];
    program.blocked = !program.blocked;
    if (!ApplyFirewallState(app, program)) {
        program.blocked = !program.blocked;
        return;
    }

    app->config.Save();
    RefreshProgramList(app);
    ListView_SetItemState(app->listView, index, LVIS_SELECTED, LVIS_SELECTED);
    SetStatusText(
        app->statusBar,
        program.blocked ? L"Program blocked." : L"Program allowed.");
}

void RemoveSelectedProgram(AppState* app) {
    const int index = ListView_GetNextItem(app->listView, -1, LVNI_SELECTED);
    if (index < 0 || index >= static_cast<int>(app->config.programs.size())) {
        return;
    }

    const std::wstring path = app->config.programs[static_cast<size_t>(index)].path;
    app->firewall.RemoveRules(path);
    app->config.programs.erase(app->config.programs.begin() + index);
    app->config.Save();
    RefreshProgramList(app);
    SetStatusText(app->statusBar, L"Program removed.");
}

UINT CollectModifiers() {
    UINT modifiers = MOD_NOREPEAT;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
        modifiers |= MOD_CONTROL;
    }
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
        modifiers |= MOD_SHIFT;
    }
    if (GetAsyncKeyState(VK_MENU) & 0x8000) {
        modifiers |= MOD_ALT;
    }
    if ((GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000)) {
        modifiers |= MOD_WIN;
    }
    return modifiers;
}

bool IsModifierKey(UINT vk) {
    switch (vk) {
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
        case VK_LWIN:
        case VK_RWIN:
            return true;
        default:
            return false;
    }
}

void BeginHotkeyCapture(AppState* app) {
    app->capturingHotkey = true;
    app->pendingModifiers = 0;
    app->pendingVirtualKey = 0;
    SetWindowTextW(app->hotkeyDisplay, L"Press a key combination...");
    SetStatusText(app->statusBar, L"Press the desired hotkey, then release.");
}

void CommitHotkey(AppState* app, UINT modifiers, UINT virtualKey) {
    app->config.hotkeyModifiers = modifiers | MOD_NOREPEAT;
    app->config.hotkeyVirtualKey = virtualKey;
    app->config.Save();

    if (!app->hotkey.Register(app->hwnd, app->config.hotkeyModifiers, app->config.hotkeyVirtualKey)) {
        MessageBoxW(
            app->hwnd,
            L"That hotkey is already in use. Choose another combination.",
            kAppTitle,
            MB_ICONWARNING);
    }

    RefreshHotkeyDisplay(app);
    SetStatusText(app->statusBar, L"Hotkey updated.");
}

void RemoveTitleBarIcon(HWND hwnd) {
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, 0);
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, 0);

    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style | WS_EX_DLGMODALFRAME);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

void LayoutControls(HWND hwnd, AppState* app) {
    RECT client = {};
    GetClientRect(hwnd, &client);

    const int margin = 10;
    const int buttonWidth = 120;
    const int buttonHeight = 28;
    const int statusHeight = 24;
    const int headerHeight = 44;
    const int hotkeyRowHeight = 30;

    MoveWindow(app->titleLabel, margin, 8, client.right - margin * 2, 22, TRUE);
    MoveWindow(app->supplierLabel, margin, 28, client.right - margin * 2, 18, TRUE);

    const int hotkeyTop = headerHeight + margin;
    const int contentBottom = client.bottom - statusHeight - margin;
    const int listTop = hotkeyTop + hotkeyRowHeight + margin;
    const int listRight = client.right - margin - buttonWidth - margin;
    const int listHeight = contentBottom - listTop;

    MoveWindow(
        app->hotkeyDisplay,
        margin + 70,
        hotkeyTop,
        client.right - margin * 2 - 70 - 120,
        24,
        TRUE);
    MoveWindow(
        GetDlgItem(hwnd, IDC_BTN_SET_HOTKEY),
        client.right - margin - 120,
        hotkeyTop - 2,
        120,
        28,
        TRUE);

    MoveWindow(GetDlgItem(hwnd, IDC_HOTKEY_LABEL), margin, hotkeyTop + 4, 60, 20, TRUE);
    MoveWindow(app->listView, margin, listTop, listRight - margin, listHeight, TRUE);

    const int buttonLeft = client.right - margin - buttonWidth;
    int buttonTop = listTop;
    MoveWindow(GetDlgItem(hwnd, IDC_BTN_ADD), buttonLeft, buttonTop, buttonWidth, buttonHeight, TRUE);
    buttonTop += buttonHeight + 8;
    MoveWindow(GetDlgItem(hwnd, IDC_BTN_ADD_MANUAL), buttonLeft, buttonTop, buttonWidth, buttonHeight, TRUE);
    buttonTop += buttonHeight + 8;
    MoveWindow(GetDlgItem(hwnd, IDC_BTN_REMOVE), buttonLeft, buttonTop, buttonWidth, buttonHeight, TRUE);
    buttonTop += buttonHeight + 8;
    MoveWindow(GetDlgItem(hwnd, IDC_BTN_TOGGLE), buttonLeft, buttonTop, buttonWidth, buttonHeight, TRUE);

    MoveWindow(app->statusBar, 0, client.bottom - statusHeight, client.right, statusHeight, TRUE);
}

HWND CreateMainWindow(HINSTANCE instance, AppState* app) {
    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kWindowClassName,
        kWindowCaption,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        900,
        580,
        nullptr,
        nullptr,
        instance,
        app);

    app->hwnd = hwnd;

    app->titleFont = CreateFontW(
        -18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    app->supplierFont = CreateFontW(
        -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    app->titleLabel = CreateWindowExW(
        0,
        L"STATIC",
        kHeaderTitle,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        10,
        8,
        860,
        22,
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TITLE_LABEL)),
        instance,
        nullptr);
    SendMessageW(app->titleLabel, WM_SETFONT, reinterpret_cast<WPARAM>(app->titleFont), TRUE);

    app->supplierLabel = CreateWindowExW(
        0,
        L"STATIC",
        kSupplierText,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        10,
        28,
        860,
        18,
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SUPPLIER_LABEL)),
        instance,
        nullptr);
    SendMessageW(app->supplierLabel, WM_SETFONT, reinterpret_cast<WPARAM>(app->supplierFont), TRUE);

    CreateWindowExW(
        0,
        L"STATIC",
        L"Hotkey:",
        WS_CHILD | WS_VISIBLE,
        10,
        58,
        60,
        20,
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_HOTKEY_LABEL)),
        instance,
        nullptr);

    app->hotkeyDisplay = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_READONLY | ES_CENTER,
        80,
        54,
        300,
        24,
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_HOTKEY_DISPLAY)),
        instance,
        nullptr);

    CreateWindowExW(
        0,
        L"BUTTON",
        L"Set Hotkey",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        390,
        52,
        120,
        28,
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_SET_HOTKEY)),
        instance,
        nullptr);

    app->listView = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEWW,
        L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        10,
        94,
        700,
        360,
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROGRAM_LIST)),
        instance,
        nullptr);

    ListView_SetExtendedListViewStyle(app->listView, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    LVCOLUMNW column = {};
    column.mask = LVCF_TEXT | LVCF_WIDTH;
    column.pszText = const_cast<wchar_t*>(L"Program");
    column.cx = 140;
    ListView_InsertColumn(app->listView, 0, &column);
    column.pszText = const_cast<wchar_t*>(L"Path");
    column.cx = 430;
    ListView_InsertColumn(app->listView, 1, &column);
    column.pszText = const_cast<wchar_t*>(L"Status");
    column.cx = 100;
    ListView_InsertColumn(app->listView, 2, &column);

    CreateWindowExW(
        0,
        L"BUTTON",
        L"Add Steam",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        720,
        94,
        120,
        28,
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_ADD)),
        instance,
        nullptr);

    CreateWindowExW(
        0,
        L"BUTTON",
        L"Add Manual",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        720,
        130,
        120,
        28,
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_ADD_MANUAL)),
        instance,
        nullptr);

    CreateWindowExW(
        0,
        L"BUTTON",
        L"Remove",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        720,
        166,
        120,
        28,
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_REMOVE)),
        instance,
        nullptr);

    CreateWindowExW(
        0,
        L"BUTTON",
        L"Toggle Selected",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        720,
        202,
        120,
        28,
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_TOGGLE)),
        instance,
        nullptr);

    app->statusBar = CreateWindowExW(
        0,
        STATUSCLASSNAMEW,
        L"Ready",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0,
        0,
        0,
        0,
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS_BAR)),
        instance,
        nullptr);

    return hwnd;
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    AppState* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message) {
        case WM_NCCREATE: {
            auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
            return TRUE;
        }

        case WM_CREATE:
            app = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            RemoveTitleBarIcon(hwnd);
            return 0;

        case WM_SIZE:
            if (app) {
                LayoutControls(hwnd, app);
            }
            return 0;

        case WM_COMMAND:
            if (!app) {
                break;
            }
            switch (LOWORD(wParam)) {
                case IDC_BTN_ADD:
                    AddSteamPrograms(app);
                    return 0;
                case IDC_BTN_ADD_MANUAL:
                    AddProgramManual(app);
                    return 0;
                case IDC_BTN_REMOVE:
                    RemoveSelectedProgram(app);
                    return 0;
                case IDC_BTN_TOGGLE:
                    ToggleSelectedProgram(app);
                    return 0;
                case IDC_BTN_SET_HOTKEY:
                    BeginHotkeyCapture(app);
                    SetFocus(app->hwnd);
                    return 0;
            }
            break;

        case WM_HOTKEY:
            if (app && static_cast<int>(wParam) == HotkeyManager::kHotkeyId) {
                ToggleAllPrograms(app);
            }
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (app && app->capturingHotkey) {
                const UINT vk = static_cast<UINT>(wParam);
                if (IsModifierKey(vk)) {
                    return 0;
                }

                UINT modifiers = CollectModifiers();
                modifiers &= ~(MOD_CONTROL | MOD_SHIFT | MOD_ALT | MOD_WIN);
                modifiers |= CollectModifiers() & (MOD_CONTROL | MOD_SHIFT | MOD_ALT | MOD_WIN);
                modifiers |= MOD_NOREPEAT;

                if (modifiers == MOD_NOREPEAT) {
                    SetStatusText(app->statusBar, L"Include Ctrl, Shift, or Alt with the hotkey.");
                    return 0;
                }

                app->capturingHotkey = false;
                CommitHotkey(app, modifiers, vk);
                return 0;
            }
            break;

        case WM_CLOSE:
            if (app) {
                app->hotkey.Unregister(hwnd);
                app->config.Save();
            }
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (app) {
                if (app->titleFont) {
                    DeleteObject(app->titleFont);
                }
                if (app->supplierFont) {
                    DeleteObject(app->supplierFont);
                }
            }
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool IsRunningAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(
            &ntAuthority,
            2,
            SECURITY_BUILTIN_DOMAIN_RID,
            DOMAIN_ALIAS_RID_ADMINS,
            0, 0, 0, 0, 0, 0,
            &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    if (!IsRunningAsAdmin()) {
        MessageBoxW(
            nullptr,
            L"This application must be run as Administrator to modify Windows Firewall rules.",
            kAppTitle,
            MB_ICONERROR);
        return 1;
    }

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    auto app = std::make_unique<AppState>();
    g_app = app.get();
    app->config.Load();
    SyncFirewallFromRules(app.get());

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = nullptr;
    wc.hIconSm = nullptr;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClassName;
    if (!RegisterClassExW(&wc)) {
        return 1;
    }

    HWND hwnd = CreateMainWindow(instance, app.get());
    if (!hwnd) {
        return 1;
    }

    RefreshProgramList(app.get());
    RefreshHotkeyDisplay(app.get());

    if (!app->hotkey.Register(hwnd, app->config.hotkeyModifiers, app->config.hotkeyVirtualKey)) {
        SetStatusText(app->statusBar, L"Default hotkey could not be registered. Set a new one.");
    } else {
        SetStatusText(
            app->statusBar,
            L"Ready. Block before teamkill → unblock in menu → click Reconnect.");
    }

    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_app = nullptr;
    return static_cast<int>(msg.wParam);
}
