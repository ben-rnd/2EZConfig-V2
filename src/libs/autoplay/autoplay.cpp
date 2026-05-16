#include "autoplay.h"
#include "logger.h"

#include <windows.h>
#include <atomic>

namespace {

struct GameOffset {
    const char* gameId;
    uintptr_t   offset;
};

// Offsets ported verbatim from V1 2EZ.cpp alternateInputThread.
constexpr GameOffset GAME_OFFSETS[] = {
    { "ez2ac_fn",    0x175E290 },   // Final
    { "ez2ac_fn_ex", 0x175F2E0 },   // Final:EX
    { "ez2ac_nt",    0x1360EA4 },   // Night Traveller
    { "ez2ac_ec",    0x00EF606C },  // Endless Circulation
};

// Default hotkey matches V1 default (VK_F11).
constexpr int DEFAULT_HOTKEY = VK_F11;

uintptr_t g_apAddr   = 0;
int       g_hotkey   = DEFAULT_HOTKEY;
std::atomic<bool> g_running{false};

void toggleByte() {
    uint8_t* target = reinterpret_cast<uint8_t*>(g_apAddr);
    DWORD oldProtect = 0;
    if (!VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        Logger::warn("[Autoplay] VirtualProtect failed");
        return;
    }
    *target = (*target == 0) ? 1 : 0;
    VirtualProtect(target, 1, oldProtect, &oldProtect);
}

DWORD WINAPI hotkeyThread(LPVOID) {
    bool pressed = false;
    while (g_running.load()) {
        bool down = (GetAsyncKeyState(g_hotkey) & 0x8000) != 0;
        if (down && !pressed) {
            toggleByte();
            uint8_t state = *reinterpret_cast<uint8_t*>(g_apAddr);
            Logger::info(std::string("[Autoplay] Toggled -> ") + (state ? "ON" : "OFF"));
        }
        pressed = down;
        Sleep(16);
    }
    return 0;
}

uintptr_t resolveOffset(const std::string& gameId) {
    for (const auto& entry : GAME_OFFSETS) {
        if (gameId == entry.gameId) return entry.offset;
    }
    return 0;
}

}  // namespace

namespace Autoplay {

void init(const std::string& gameId) {
    uintptr_t offset = resolveOffset(gameId);
    if (offset == 0) {
        return;  // Game not in the supported list.
    }

    uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    if (base == 0) {
        Logger::error("[Autoplay] Could not resolve module base");
        return;
    }

    g_apAddr = base + offset;
    g_running.store(true);
    CreateThread(nullptr, 0, hotkeyThread, nullptr, 0, nullptr);

    char keyName[32] = {};
    int sc = MapVirtualKeyA(g_hotkey, MAPVK_VK_TO_VSC);
    GetKeyNameTextA(sc << 16, keyName, sizeof(keyName));
    Logger::info(std::string("[Autoplay] Armed for ") + gameId +
                 " (hotkey: " + (keyName[0] ? keyName : "F11") + ")");
}

}  // namespace Autoplay
