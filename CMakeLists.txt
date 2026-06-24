cmake_minimum_required(VERSION 3.20)

# Allows CMake configure when rc.exe is missing (SDK not fully installed yet).
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

project(SteamFirewallBlocker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SteamFirewallBlocker WIN32
    src/main.cpp
    src/config.cpp
    src/firewall.cpp
    src/hotkey.cpp
)

target_include_directories(SteamFirewallBlocker PRIVATE src)

target_link_libraries(SteamFirewallBlocker PRIVATE
    ole32
    oleaut32
    uuid
    comctl32
    shell32
    shlwapi
    advapi32
)

if(MSVC)
    target_compile_definitions(SteamFirewallBlocker PRIVATE UNICODE _UNICODE)
    target_compile_options(SteamFirewallBlocker PRIVATE /W4 /utf-8)
    target_link_options(SteamFirewallBlocker PRIVATE
        "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
    )
endif()
