#include "firewall.h"

#include <comdef.h>
#include <netfw.h>
#include <shlwapi.h>

#include <functional>

namespace {

std::wstring NormalizePath(const std::wstring& path) {
    wchar_t fullPath[MAX_PATH] = {};
    if (GetFullPathNameW(path.c_str(), MAX_PATH, fullPath, nullptr) > 0) {
        return fullPath;
    }
    return path;
}

std::wstring FileNameFromPath(const std::wstring& path) {
    const wchar_t* name = PathFindFileNameW(path.c_str());
    return name ? std::wstring(name) : path;
}

void FormatRuleName(wchar_t* buffer, size_t bufferCount, const std::wstring& normalizedPath, bool inbound) {
    const size_t pathHash = std::hash<std::wstring>{}(normalizedPath);
    swprintf_s(
        buffer,
        bufferCount,
        L"SFB-%016llx-%s",
        static_cast<unsigned long long>(pathHash),
        inbound ? L"IN" : L"OUT");
}

void FormatLegacyRuleName(wchar_t* buffer, size_t bufferCount, const std::wstring& normalizedPath, bool inbound) {
    const size_t pathHash = std::hash<std::wstring>{}(normalizedPath);
    swprintf_s(
        buffer,
        bufferCount,
        L"SFB|%016llx|%s",
        static_cast<unsigned long long>(pathHash),
        inbound ? L"IN" : L"OUT");
}

}  // namespace

FirewallManager::FirewallManager() = default;

FirewallManager::~FirewallManager() {
    if (comInitialized_) {
        CoUninitialize();
    }
}

bool FirewallManager::EnsureComInitialized() {
    if (comInitialized_) {
        return true;
    }

    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        lastError_ = hr;
        return false;
    }

    comInitialized_ = true;
    return true;
}

std::wstring FirewallManager::LastErrorMessage() const {
    if (lastError_ == S_OK) {
        return L"";
    }

    _com_error error(lastError_);
    if (error.ErrorMessage() && error.ErrorMessage()[0] != L'\0') {
        return error.ErrorMessage();
    }

    wchar_t buffer[64] = {};
    swprintf_s(buffer, L"Error code 0x%08lX", lastError_);
    return buffer;
}

std::wstring FirewallManager::RuleName(const std::wstring& applicationPath, bool inbound) const {
    const std::wstring normalized = NormalizePath(applicationPath);
    wchar_t buffer[128] = {};
    FormatRuleName(buffer, 128, normalized, inbound);
    return buffer;
}

std::wstring FirewallManager::LegacyRuleName(const std::wstring& applicationPath, bool inbound) const {
    const std::wstring normalized = NormalizePath(applicationPath);
    wchar_t buffer[128] = {};
    FormatLegacyRuleName(buffer, 128, normalized, inbound);
    return buffer;
}

bool FirewallManager::GetPolicyAndRules(INetFwPolicy2** policy, INetFwRules** rules) const {
    *policy = nullptr;
    *rules = nullptr;

    HRESULT hr = CoCreateInstance(
        __uuidof(NetFwPolicy2),
        nullptr,
        CLSCTX_INPROC_SERVER,
        __uuidof(INetFwPolicy2),
        reinterpret_cast<void**>(policy));
    if (FAILED(hr)) {
        lastError_ = hr;
        return false;
    }

    hr = (*policy)->get_Rules(rules);
    if (FAILED(hr)) {
        lastError_ = hr;
        (*policy)->Release();
        *policy = nullptr;
        return false;
    }

    return true;
}

bool FirewallManager::FindRuleByName(const std::wstring& ruleName, INetFwRule** outRule) const {
    *outRule = nullptr;

    INetFwPolicy2* policy = nullptr;
    INetFwRules* rules = nullptr;
    if (!GetPolicyAndRules(&policy, &rules)) {
        return false;
    }

    INetFwRule* rule = nullptr;
    const HRESULT hr = rules->Item(_bstr_t(ruleName.c_str()), &rule);
    rules->Release();
    policy->Release();

    if (FAILED(hr) || rule == nullptr) {
        lastError_ = FAILED(hr) ? hr : E_FAIL;
        return false;
    }

    *outRule = rule;
    lastError_ = S_OK;
    return true;
}

long FirewallManager::ActiveProfiles() const {
    INetFwPolicy2* policy = nullptr;
    HRESULT hr = CoCreateInstance(
        __uuidof(NetFwPolicy2),
        nullptr,
        CLSCTX_INPROC_SERVER,
        __uuidof(INetFwPolicy2),
        reinterpret_cast<void**>(&policy));
    if (FAILED(hr)) {
        return NET_FW_PROFILE2_ALL;
    }

    long profiles = NET_FW_PROFILE2_ALL;
    policy->get_CurrentProfileTypes(&profiles);
    policy->Release();
    return profiles != 0 ? profiles : NET_FW_PROFILE2_ALL;
}

bool FirewallManager::CreateBlockRule(const std::wstring& applicationPath, bool inbound) {
    INetFwPolicy2* policy = nullptr;
    INetFwRules* rules = nullptr;
    if (!GetPolicyAndRules(&policy, &rules)) {
        return false;
    }

    INetFwRule* rule = nullptr;
    HRESULT hr = CoCreateInstance(
        __uuidof(NetFwRule),
        nullptr,
        CLSCTX_INPROC_SERVER,
        __uuidof(INetFwRule),
        reinterpret_cast<void**>(&rule));
    if (FAILED(hr)) {
        lastError_ = hr;
        rules->Release();
        policy->Release();
        return false;
    }

    const std::wstring normalizedPath = NormalizePath(applicationPath);
    if (!PathFileExistsW(normalizedPath.c_str())) {
        lastError_ = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        rule->Release();
        rules->Release();
        policy->Release();
        return false;
    }

    const std::wstring name = RuleName(normalizedPath, inbound);
    const std::wstring description =
        L"SteamFirewallBlocker rule for " + FileNameFromPath(normalizedPath);

    rule->put_Name(_bstr_t(name.c_str()));
    rule->put_Description(_bstr_t(description.c_str()));
    rule->put_ApplicationName(_bstr_t(normalizedPath.c_str()));
    rule->put_Protocol(NET_FW_IP_PROTOCOL_ANY);
    rule->put_Direction(inbound ? NET_FW_RULE_DIR_IN : NET_FW_RULE_DIR_OUT);
    rule->put_Profiles(ActiveProfiles());
    rule->put_InterfaceTypes(_bstr_t(L"All"));
    rule->put_Action(NET_FW_ACTION_BLOCK);
    rule->put_Grouping(_bstr_t(L"SteamFirewallBlocker"));
    rule->put_Enabled(VARIANT_TRUE);

    hr = rules->Add(rule);
    rule->Release();
    rules->Release();
    policy->Release();

    if (SUCCEEDED(hr)) {
        lastError_ = S_OK;
        return true;
    }

    lastError_ = hr;
    if (hr == E_UNEXPECTED || hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
        return SetRuleEnabled(name, true);
    }

    return false;
}

bool FirewallManager::SetRuleEnabled(const std::wstring& ruleName, bool enabled) {
    INetFwRule* rule = nullptr;
    if (!FindRuleByName(ruleName, &rule)) {
        return false;
    }

    const HRESULT hr = rule->put_Enabled(enabled ? VARIANT_TRUE : VARIANT_FALSE);
    rule->Release();
    lastError_ = hr;
    return SUCCEEDED(hr);
}

void FirewallManager::RemoveRuleIfPresent(const std::wstring& ruleName) {
    INetFwPolicy2* policy = nullptr;
    INetFwRules* rules = nullptr;
    if (!GetPolicyAndRules(&policy, &rules)) {
        return;
    }

    rules->Remove(_bstr_t(ruleName.c_str()));
    rules->Release();
    policy->Release();
}

bool FirewallManager::SetBlocked(const std::wstring& applicationPath, bool blocked) {
    if (!EnsureComInitialized()) {
        return false;
    }

    const std::wstring normalizedPath = NormalizePath(applicationPath);
    if (normalizedPath.empty()) {
        lastError_ = E_INVALIDARG;
        return false;
    }

    for (const bool inbound : {true, false}) {
        RemoveRuleIfPresent(LegacyRuleName(normalizedPath, inbound));
    }

    bool success = true;
    for (const bool inbound : {true, false}) {
        const std::wstring name = RuleName(normalizedPath, inbound);
        INetFwRule* existing = nullptr;
        if (!FindRuleByName(name, &existing)) {
            if (!blocked) {
                continue;
            }
            if (!CreateBlockRule(normalizedPath, inbound)) {
                success = false;
            }
            continue;
        }

        existing->Release();
        if (!SetRuleEnabled(name, blocked)) {
            success = false;
        }
    }

    return success;
}

bool FirewallManager::IsBlocked(const std::wstring& applicationPath) const {
    if (!const_cast<FirewallManager*>(this)->EnsureComInitialized()) {
        return false;
    }

    const std::wstring name = RuleName(applicationPath, false);
    INetFwRule* rule = nullptr;
    if (!FindRuleByName(name, &rule)) {
        return false;
    }

    VARIANT_BOOL enabled = VARIANT_FALSE;
    const HRESULT hr = rule->get_Enabled(&enabled);
    rule->Release();
    return SUCCEEDED(hr) && enabled == VARIANT_TRUE;
}

bool FirewallManager::RemoveRules(const std::wstring& applicationPath) {
    if (!EnsureComInitialized()) {
        return false;
    }

    INetFwPolicy2* policy = nullptr;
    INetFwRules* rules = nullptr;
    if (!GetPolicyAndRules(&policy, &rules)) {
        return false;
    }

    bool success = true;
    for (const bool inbound : {true, false}) {
        for (const std::wstring& name :
             {RuleName(applicationPath, inbound), LegacyRuleName(applicationPath, inbound)}) {
            const HRESULT hr = rules->Remove(_bstr_t(name.c_str()));
            if (FAILED(hr) && hr != E_INVALIDARG) {
                success = false;
                lastError_ = hr;
            }
        }
    }

    rules->Release();
    policy->Release();
    return success;
}
