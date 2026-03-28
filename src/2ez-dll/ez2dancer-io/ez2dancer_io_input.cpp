#include "ez2dancer_io_input.h"
#include "bindings.h"
#include "input_manager.h"
#include "game_defs.h"
#include "logger.h"
#include "utilities.h"

#include <windows.h>
#include <atomic>
#include <functional>
#include <vector>

static std::atomic<uint16_t> s_portCache[4] = { 0xF000, 0xF000, 0x0000, 0x00FF };

struct BoundDevice {
    std::string path;
    std::string name;
};

static std::vector<BoundDevice> s_boundDevices;

enum DancerPort {
    DANCER_P1_PADS   = 0,  // 0x300
    DANCER_P2_PADS   = 1,  // 0x302
    DANCER_SENSORS   = 3,  // 0x306
};

struct ButtonBit {
    int button;
    uint16_t mask;
};

static const ButtonBit dancerP1Pads[] = {
    { (int)DancerButton::P1_LEFT,   0x00F },
    { (int)DancerButton::P1_CENTRE, 0x0F0 },
    { (int)DancerButton::P1_RIGHT,  0xF00 },
};

static const ButtonBit dancerP2Pads[] = {
    { (int)DancerButton::P2_LEFT,   0x00F },
    { (int)DancerButton::P2_CENTRE, 0x0F0 },
    { (int)DancerButton::P2_RIGHT,  0xF00 },
};

static const ButtonBit dancerSensors[] = {
    { (int)DancerButton::P2_R_SENSOR_TOP, 0x0100 },
    { (int)DancerButton::P2_L_SENSOR_TOP, 0x0200 },
    { (int)DancerButton::P1_R_SENSOR_TOP, 0x0400 },
    { (int)DancerButton::P1_L_SENSOR_TOP, 0x0800 },
    { (int)DancerButton::P1_L_SENSOR_BOT, 0x1000 },
    { (int)DancerButton::P1_R_SENSOR_BOT, 0x2000 },
    { (int)DancerButton::P2_L_SENSOR_BOT, 0x4000 },
    { (int)DancerButton::P2_R_SENSOR_BOT, 0x8000 },
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

static uint16_t computeSensorPort(IsHeldFn isHeld) {
    uint16_t portValue = computePort(0xFFFF, dancerSensors, std::size(dancerSensors), isHeld);

    if (isHeld((int)DancerButton::TEST)) {
        portValue &= 0xFF00 | (1 << 5);
    }
    if (isHeld((int)DancerButton::SERVICE)) {
        portValue &= 0xFF00 | (1 << 4);
    }

    // game expects inverted sensors for some reason.
    return portValue ^ 0xFF00;
}

bool handleDancerIn(uint16_t port, uint16_t& out) {
    switch (port) {
        case 0x300: out = s_portCache[DANCER_P1_PADS].load(); return true;
        case 0x302: out = s_portCache[DANCER_P2_PADS].load(); return true;
        case 0x304: out = s_portCache[2].load();              return true;
        case 0x306: out = s_portCache[DANCER_SENSORS].load(); return true;
        default:
            Logger::warnOnce("[IO] Unexpected Dancer port read: 0x" + toHexString(port));
            out = 0xFFFF;
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
        return bindings.isHeldSnapshot(bindings.dancerButtons[button], deviceSnapshots);
    };

    s_portCache[DANCER_P1_PADS].store(static_cast<uint16_t>(~computePort(0x0FFF, dancerP1Pads, std::size(dancerP1Pads), isHeld)));
    s_portCache[DANCER_P2_PADS].store(static_cast<uint16_t>(~computePort(0x0FFF, dancerP2Pads, std::size(dancerP2Pads), isHeld)));
    s_portCache[DANCER_SENSORS].store(computeSensorPort(isHeld));
}

static void addUnique(const std::string& devicePath, const std::string& deviceName) {
    if (devicePath.empty()) return;
    for (const auto& device : s_boundDevices) {
        if (device.path == devicePath) return;
    }
    s_boundDevices.push_back({ devicePath, deviceName });
}

static void initPortCache(BindingStore& bindings) {
    for (auto& binding : bindings.dancerButtons) {
        if (binding.isSet() && !binding.isKeyboard()) {
            addUnique(binding.devicePath, binding.deviceName);
        }
    }

    std::vector<Device> connectedDevices = bindings.mgr->getDevices();
    Logger::info("[Dancer Input] " + std::to_string(s_boundDevices.size()) + " bound device(s)");
    for (const auto& device : s_boundDevices) {
        bool connected = false;
        for (const auto& cd : connectedDevices) {
            if (cd.path == device.path) { connected = true; break; }
        }
        if (connected) {
            Logger::info("[Dancer Input]   " + device.name + " (" + device.path + ")");
        } else {
            Logger::warn("[Dancer Input] (Disconnected) " + device.name);
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

void startDancerInputPollThread(BindingStore& bindings) {
    Logger::info("[Dancer Input] Poll thread started");
    CreateThread(nullptr, 0, inputPollThread, &bindings, 0, nullptr);
}
