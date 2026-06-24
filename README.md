# Steam Firewall Blocker

**Supplied by Revoxxi**

A Windows utility that toggles firewall blocks on `steam.exe`, `steamservice.exe`, and `steamwebhelper.exe` with a hotkey. It was built around a Counter-Strike reconnect/ban interaction and is intended for **educational use** — understanding how Steam connectivity affects in-game kick and cooldown behavior.

> **Disclaimer:** Using this tool to circumvent bans, grief other players, or exploit online games may violate game Terms of Service and result in account penalties. Use at your own risk, preferably in controlled environments where you have permission to test (private servers, offline, research, etc.).

---

## What problem does this address?

In Counter-Strike, team damage can trigger a kick or ban from the current session. There is a known interaction where **cutting Steam’s connection at the correct times** affects whether that penalty sticks when you return to the menu — leaving the **Reconnect** button available so you can rejoin the match.

This matches how the exploit is described publicly (e.g. Portmaster tutorials on Steam forums): block Steam’s outgoing connections **before** the kick, then reconnect after restoring connectivity.

The important part is **when** you block and unblock:

| Step | Action |
|------|--------|
| 1 | **Block Steam** (hotkey) **before** the teamkill |
| 2 | Teamkill (e.g. a cheater in spawn on your team) |
| 3 | You are sent **back to the menu** from the team-damage kick/ban |
| 4 | **Unblock Steam** (hotkey) while **in the menu** |
| 5 | Click **Reconnect** to rejoin the match |

Blocking **after** the teamkill is too late. You **unblock in the menu**, then **reconnect** — no special freezetime timing is required in the public write-ups; unblock and use the reconnect button.

If you hit **max team damage**, you may still receive a **cooldown after the match**, but you can often rejoin and continue that game. A **vote kick** from your team still applies normally.

This documents a **CS flaw / edge case** in how Steam connectivity and session state interact. The tool does not modify game files or inject into the client; it only blocks Steam executables at the Windows Firewall level.

---

## Who is this for?

- **Education & research** — learning how firewall-level Steam blocks affect reconnect and session state.
- **Understanding the exploit** — reproducing the team-damage + reconnect behavior in a controlled setting.
- **Team punishment scenarios** — studying team damage against cheaters on your own team while analyzing ban-dodge mechanics. This can violate ToS in public matchmaking. **Do it at your own risk.**

---

## What the program does (technically)

| Component | Behavior |
|-----------|----------|
| `steam.exe` | Blocked / unblocked via Windows Firewall rules |
| `steamservice.exe` | Blocked / unblocked via Windows Firewall rules |
| `steamwebhelper.exe` | Blocked / unblocked via Windows Firewall rules |
| Hotkey | Toggles all listed executables between **Blocked** and **Allowed** |
| UI | Add Steam, per-entry toggle, hotkey configuration |

For each executable, the app creates inbound and outbound **block** rules (group: `SteamFirewallBlocker`). Blocking **enables** the rules; allowing **disables** them. Rules persist across restarts; toggling is instant.

**Default hotkey:** `Ctrl+Shift+B`

Settings are stored at:

`%APPDATA%\SteamFirewallBlocker\config.ini`

---

## Requirements

- Windows 10 / 11
- **Administrator** privileges (UAC prompt on launch)
- Windows Firewall service running
- Visual Studio 2022 Build Tools (or full VS) to compile from source

---

## Build

```powershell
.\build.bat
```

Output: `build\SteamFirewallBlocker.exe`

Manual build (if needed):

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

---

## Setup

1. Run `SteamFirewallBlocker.exe` as Administrator.
2. Click **Add Steam** to auto-detect executables, or **Add Manual** to pick a path (e.g. `bin\cef\cef.win64\steamwebhelper.exe`).
3. Click **Set Hotkey** and press a combination (must include Ctrl, Shift, or Alt).

Only those three Steam executables can be added.

---

## In-game workflow (CS)

```
[BEFORE teamkill]  →  Hotkey: BLOCK Steam
        ↓
   Teamkill target
        ↓
[Back in menu]     →  Hotkey: UNBLOCK Steam
        ↓
                   →  Click Reconnect
```

1. **Block first** — press the hotkey to block Steam **before** you teamkill. If Steam is still connected when the kill happens, this will not work.
2. **Teamkill** — kill the target (e.g. cheater in spawn).
3. **Unblock in the menu** — once you are kicked back to the **menu**, press the hotkey again to restore Steam’s connection. Do not unblock mid-round.
4. **Reconnect** — click the **Reconnect** button to rejoin the match.

Exact outcomes depend on server settings, game version, and Valve-side changes. This tool only controls the firewall.

---

## License & attribution

Created by **Revoxxi**. For educational and research purposes.
