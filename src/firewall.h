#pragma once

#include <netfw.h>
#include <windows.h>

#include <string>

class FirewallManager {
public:
    FirewallManager();
    ~FirewallManager();

    FirewallManager(const FirewallManager&) = delete;
    FirewallManager& operator=(const FirewallManager&) = delete;

    bool SetBlocked(const std::wstring& applicationPath, bool blocked);
    bool IsBlocked(const std::wstring& applicationPath) const;
    bool RemoveRules(const std::wstring& applicationPath);
    std::wstring LastErrorMessage() const;

private:
    bool EnsureComInitialized();
    std::wstring RuleName(const std::wstring& applicationPath, bool inbound) const;
    std::wstring LegacyRuleName(const std::wstring& applicationPath, bool inbound) const;
    bool GetPolicyAndRules(INetFwPolicy2** policy, INetFwRules** rules) const;
    bool FindRuleByName(const std::wstring& ruleName, INetFwRule** outRule) const;
    bool CreateBlockRule(const std::wstring& applicationPath, bool inbound);
    bool SetRuleEnabled(const std::wstring& ruleName, bool enabled);
    void RemoveRuleIfPresent(const std::wstring& ruleName);
    long ActiveProfiles() const;

    bool comInitialized_ = false;
    mutable HRESULT lastError_ = S_OK;
};
