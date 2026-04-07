#include "ez2dj_io_input.h"
#include "bindings.h"
#include "input_manager.h"
#include "game_defs.h"
#include "logger.h"
#include "utilities.h"

#include <windows.h>
#include <atomic>
#include <functional>
#include <vector>

static std::atomic<uint8_t> s_portCache[7] = { 0xFF, 0xFF, 0xFF, 0x80, 0x80, 0xFF, 0xFF };

static bool s_coinWasHeld = false;
static std::atomic<bool> s_coinPending{false};

struct BoundDevice {
    std::string path;
    std::string name;
};

static std::vector<BoundDevice> s_boundDevices;

enum DjPort {
    DJ_SYSTEM   = 1,  // 0x101
    DJ_P1_KEYS  = 2,  // 0x102
    DJ_P1_TT    = 3,  // 0x103
    DJ_P2_TT    = 4,  // 0x104
    DJ_COIN     = 5,  // 0x105
    DJ_P2_KEYS  = 6,  // 0x106
};

struct ButtonBit {
    int button;
    uint16_t mask;
};

static const ButtonBit djSystemButtons[] = {
    { (int)DJButton::P1_START,   0x01 },
    { (int)DJButton::P2_START,   0x02 },
    { (int)DJButton::EFFECTOR_1, 0x04 },
    { (int)DJButton::EFFECTOR_2, 0x08 },
    { (int)DJButton::EFFECTOR_3, 0x10 },
    { (int)DJButton::EFFECTOR_4, 0x20 },
    { (int)DJButton::SERVICE,    0x40 },
    { (int)DJButton::TEST,       0x80 },
};

static const ButtonBit djCoinButtons[] = {
    { (int)DJButton::COIN, 0x01 },
};

static const ButtonBit djP1Buttons[] = {
    { (int)DJButton::P1_1,     0x01 },
    { (int)DJButton::P1_2,     0x02 },
    { (int)DJButton::P1_3,     0x04 },
    { (int)DJButton::P1_4,     0x08 },
    { (int)DJButton::P1_5,     0x10 },
    { (int)DJButton::P1_PEDAL, 0x80 },
};

static const ButtonBit djP2Buttons[] = {
    { (int)DJButton::P2_1,     0x01 },
    { (int)DJButton::P2_2,     0x02 },
    { (int)DJButton::P2_3,     0x04 },
    { (int)DJButton::P2_4,     0x08 },
    { (int)DJButton::P2_5,     0x10 },
    { (int)DJButton::P2_PEDAL, 0x80 },
};

using IsHeldFn = std::function<bool(int button)>;

static uint16_t computePort(uint16_t initialValue, const ButtonBit* table, size_t count, IsHeldFn isHeld) {
    uint16_t portValue = initialValue;
    for (size_t i = 0; i < count; i++) {
        if (isHeld(table[i].button)) {
            portValue &= ~table[i].mask;
        }
    }
    return portValue;
}

bool handleDJIn(uint16_t port, uint8_t& out) {
    switch (port) {
        case 0x101: out = s_portCache[DJ_SYSTEM].load();  return true;
        case 0x102: out = s_portCache[DJ_P1_KEYS].load(); return true;
        case 0x103: out = s_portCache[DJ_P1_TT].load();   return true;
        case 0x104: out = s_portCache[DJ_P2_TT].load();   return true;
        case 0x105:
            if (s_coinPending.exchange(false)) {
                out = 0xFE; // bit 0 low = coin inserted
            } else {
                out = 0xFF;
            }
            return true;
        case 0x106: out = s_portCache[DJ_P2_KEYS].load(); return true;
        default:
            Logger::warnOnce("[IO] Unexpected DJ port read: 0x" + toHexString(port));
            out = 0xFF;
            return false;
    }
}

static void updatePortCache(BindingStore& bindings) {
    BindingStore::DeviceSnapshotMap deviceSnapshots;
    for (const auto& device : s_boundDevices) {
        DeviceSnapshot snapshot;
        if (bindings.mgr->snapshotDevice(device.path, snapshot)) {
            deviceSnapshots[device.path] = std::move(snapshot);
        }
    }

    auto isHeld = [&](int button) {
        return bindings.isHeldSnapshot(bindings.buttons[button], deviceSnapshots);
    };

    s_portCache[DJ_SYSTEM].store(computePort(0xFF, djSystemButtons, std::size(djSystemButtons), isHeld));

    // Coin is edge-triggered: set pending on press, consumed on first port read.
    bool coinHeld = isHeld((int)DJButton::COIN);
    if (coinHeld && !s_coinWasHeld) {
        s_coinPending.store(true);
    }
    s_coinWasHeld = coinHeld;

    s_portCache[DJ_P1_KEYS].store(computePort(0xFF, djP1Buttons, std::size(djP1Buttons), isHeld));
    s_portCache[DJ_P2_KEYS].store(computePort(0xFF, djP2Buttons, std::size(djP2Buttons), isHeld));

    s_portCache[DJ_P1_TT].store(bindings.getPositionSnapshot(bindings.analogs[(int)Analog::P1_TURNTABLE], bindings.getVttPosition((int)Analog::P1_TURNTABLE), bindings.mgr->getMousePosition((int)Analog::P1_TURNTABLE), deviceSnapshots));
    s_portCache[DJ_P2_TT].store(bindings.getPositionSnapshot(bindings.analogs[(int)Analog::P2_TURNTABLE], bindings.getVttPosition((int)Analog::P2_TURNTABLE), bindings.mgr->getMousePosition((int)Analog::P2_TURNTABLE), deviceSnapshots));
}

static void addUnique(const std::string& devicePath, const std::string& deviceName) {
    if (devicePath.empty()) return;
    for (const auto& device : s_boundDevices) {
        if (device.path == devicePath) return;
    }
    s_boundDevices.push_back({ devicePath, deviceName });
}

static void initPortCache(BindingStore& bindings) {
    for (auto& binding : bindings.buttons) {
        if (binding.isSet() && !binding.isKeyboard()) {
            addUnique(binding.devicePath, binding.deviceName);
        }
    }
    for (auto& analogBinding : bindings.analogs) {
        if (analogBinding.isSet()) {
            addUnique(analogBinding.devicePath, analogBinding.deviceName);
        }
        if (analogBinding.vttPlus.isSet() && !analogBinding.vttPlus.isKeyboard()) {
            addUnique(analogBinding.vttPlus.devicePath, analogBinding.vttPlus.deviceName);
        }
        if (analogBinding.vttMinus.isSet() && !analogBinding.vttMinus.isKeyboard()) {
            addUnique(analogBinding.vttMinus.devicePath, analogBinding.vttMinus.deviceName);
        }
    }

    std::vector<Device> connectedDevices = bindings.mgr->getDevices();
    Logger::info("[DJ Input] " + std::to_string(s_boundDevices.size()) + " bound device(s)");
    for (const auto& device : s_boundDevices) {
        bool connected = false;
        for (const auto& cd : connectedDevices) {
            if (cd.path == device.path) { connected = true; break; }
        }
        if (connected) {
            Logger::info("[DJ Input]   " + device.name + " (" + device.path + ")");
        } else {
            Logger::warn("[DJ Input] (Disconnected) " + device.name);
        }
    }

    bindings.mgr->setInputCallback([](void* userData) {
        updatePortCache(*static_cast<BindingStore*>(userData));
    }, &bindings);
}

static DWORD WINAPI inputPollThread(void* bindingsPtr) {
    BindingStore& bindings = *static_cast<BindingStore*>(bindingsPtr);
    initPortCache(bindings);
    while (true) {
        Sleep(1);
        updatePortCache(bindings);
    }
    return 0;
}

void startDJInputPollThread(BindingStore& bindings) {
    Logger::info("[DJ Input] Poll thread started");
    CreateThread(nullptr, 0, inputPollThread, &bindings, 0, nullptr);
}
