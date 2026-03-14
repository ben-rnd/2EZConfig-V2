#include "dll_input.h"
#include "bindings.h"
#include "input_manager.h"
#include "game_defs.h"
#include "logger.h"
#include "utilities.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <functional>

static std::atomic<uint8_t> s_djPortCache[7] = { 0xFF, 0xFF, 0xFF, 0x80, 0x80, 0xFF, 0xFF };
static std::atomic<uint16_t> s_dancerPortCache[4] = { 0xF000, 0xF000, 0x0000, 0x00FF };

struct BoundDevice {
    std::string path;
    std::string name;
};

static std::vector<BoundDevice> s_boundDevices;

enum DjPort {
    DJ_SYSTEM   = 1,  // 0x101: test, service, effectors, start buttons
    DJ_P1_KEYS  = 2,  // 0x102: P1 keys + pedal
    DJ_P1_TT    = 3,  // 0x103: P1 turntable position (analog)
    DJ_P2_TT    = 4,  // 0x104: P2 turntable position (analog)
    DJ_P2_KEYS  = 6,  // 0x106: P2 keys + pedal
};

enum DancerPort {
    DANCER_P1_PADS   = 0,  // 0x300: P1 left/centre/right
    DANCER_P2_PADS   = 1,  // 0x302: P2 left/centre/right
    DANCER_SENSORS   = 3,  // 0x306: hand sensors + test/service
};

// Each table maps a button enum to a bitmask within a specific I/O port.
// The button field stores the enum as int so both DJButton and DancerButton
// tables can share the same struct.

struct ButtonBit {
    int button;
    uint16_t mask;
};

// Port 0x101: system buttons
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

// Port 0x102: P1 keys + pedal
static const ButtonBit djP1Buttons[] = {
    { (int)DJButton::P1_1,     0x01 },
    { (int)DJButton::P1_2,     0x02 },
    { (int)DJButton::P1_3,     0x04 },
    { (int)DJButton::P1_4,     0x08 },
    { (int)DJButton::P1_5,     0x10 },
    // bits 5-6 unused
    { (int)DJButton::P1_PEDAL, 0x80 },
};

// Port 0x106: P2 keys + pedal
static const ButtonBit djP2Buttons[] = {
    { (int)DJButton::P2_1,     0x01 },
    { (int)DJButton::P2_2,     0x02 },
    { (int)DJButton::P2_3,     0x04 },
    { (int)DJButton::P2_4,     0x08 },
    { (int)DJButton::P2_5,     0x10 },
    // bits 5-6 unused
    { (int)DJButton::P2_PEDAL, 0x80 },
};

// Port 0x300: P1 dance pads 
static const ButtonBit dancerP1Pads[] = {
    { (int)DancerButton::P1_LEFT,   0x00F },
    { (int)DancerButton::P1_CENTRE, 0x0F0 },
    { (int)DancerButton::P1_RIGHT,  0xF00 },
};

// Port 0x302: P2 dance pads
static const ButtonBit dancerP2Pads[] = {
    { (int)DancerButton::P2_LEFT,   0x00F },
    { (int)DancerButton::P2_CENTRE, 0x0F0 },
    { (int)DancerButton::P2_RIGHT,  0xF00 },
};

// Port 0x306 high byte: dancer sensor switches (active-low, inverted on output)
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

// Builds a port value by clearing bits for any held buttons in the table.
static uint16_t computePort(uint16_t initialValue, const ButtonBit* table, size_t count, IsHeldFn isHeld) {
    uint16_t portValue = initialValue;
    for (size_t i = 0; i < count; i++) {
        if (isHeld(table[i].button)) {
            portValue &= ~table[i].mask;
        }
    }
    return portValue;
}

static uint16_t computeDancerSensorPort(IsHeldFn isHeld) {
    uint16_t portValue = computePort(0xFFFF, dancerSensors, std::size(dancerSensors), isHeld);

    // Test and service clear the low byte to their single active bit
    if (isHeld((int)DancerButton::TEST)) {
        portValue &= 0xFF00 | (1 << 5);
    }
    if (isHeld((int)DancerButton::SERVICE)) {
        portValue &= 0xFF00 | (1 << 4);
    }
    
    // game expects inverted sensors for some reason.
    return portValue ^ 0xFF00;
}

bool handleDJIn(uint16_t port, uint8_t& out) {
    switch (port) {
        case 0x101: out = s_djPortCache[DJ_SYSTEM].load();  return true;
        case 0x102: out = s_djPortCache[DJ_P1_KEYS].load(); return true;
        case 0x103: out = s_djPortCache[DJ_P1_TT].load();   return true;
        case 0x104: out = s_djPortCache[DJ_P2_TT].load();   return true;
        case 0x106: out = s_djPortCache[DJ_P2_KEYS].load(); return true;
        default:
            Logger::warnOnce("[IO] Unexpected DJ port read: 0x" + toHexString(port));
            out = 0xFF;
            return false;
    }
}

bool handleDancerIn(uint16_t port, uint16_t& out) {
    switch (port) {
        case 0x300: out = s_dancerPortCache[DANCER_P1_PADS].load(); return true;
        case 0x302: out = s_dancerPortCache[DANCER_P2_PADS].load(); return true;
        case 0x304: out = s_dancerPortCache[2].load();              return true;
        case 0x306: out = s_dancerPortCache[DANCER_SENSORS].load(); return true;
        default:
            Logger::warnOnce("[IO] Unexpected Dancer port read: 0x" + toHexString(port));
            out = 0xFFFF;
            return false;
    }
}

void updatePortCache(const BindingStore& bindings) {
    // Snapshot all bound devices once per update cycle
    BindingStore::DeviceSnapshotMap deviceSnapshots;
    for (const auto& device : s_boundDevices) {
        DeviceSnapshot snapshot;
        if (bindings.mgr->snapshotDevice(device.path, snapshot)) {
            deviceSnapshots[device.path] = std::move(snapshot);
        }
    }

    // Helpers that close over the snapshot for DJ and Dancer button lookups
    auto djIsHeld = [&](int button) {
        return bindings.isHeldSnapshot(bindings.buttons[button], deviceSnapshots);
    };
    auto dancerIsHeld = [&](int button) {
        return bindings.isHeldSnapshot(bindings.dancerButtons[button], deviceSnapshots);
    };

    // DJ button ports
    s_djPortCache[DJ_SYSTEM].store(computePort(0xFF, djSystemButtons, std::size(djSystemButtons), djIsHeld));
    s_djPortCache[DJ_P1_KEYS].store(computePort(0xFF, djP1Buttons, std::size(djP1Buttons), djIsHeld));
    s_djPortCache[DJ_P2_KEYS].store(computePort(0xFF, djP2Buttons, std::size(djP2Buttons), djIsHeld));

    // DJ analog ports (turntable positions)
    s_djPortCache[DJ_P1_TT].store(bindings.getPositionSnapshot(bindings.analogs[(int)Analog::P1_TURNTABLE], bindings.mgr->getVttPosition((int)Analog::P1_TURNTABLE), deviceSnapshots));
    s_djPortCache[DJ_P2_TT].store(bindings.getPositionSnapshot(bindings.analogs[(int)Analog::P2_TURNTABLE], bindings.mgr->getVttPosition((int)Analog::P2_TURNTABLE), deviceSnapshots));

    // Dancer pad ports (inverted: hardware active-low -> cache active-high)
    s_dancerPortCache[DANCER_P1_PADS].store(static_cast<uint16_t>(~computePort(0x0FFF, dancerP1Pads, std::size(dancerP1Pads), dancerIsHeld)));
    s_dancerPortCache[DANCER_P2_PADS].store(static_cast<uint16_t>(~computePort(0x0FFF, dancerP2Pads, std::size(dancerP2Pads), dancerIsHeld)));

    // Dancer sensor + test/service port
    s_dancerPortCache[DANCER_SENSORS].store(computeDancerSensorPort(dancerIsHeld));
}

static void addUnique(const std::string& devicePath, const std::string& deviceName) {
    if (devicePath.empty()) {
        return;
    }
    for (const auto& device : s_boundDevices) {
        if (device.path == devicePath) {
            return;
        }
    }
    s_boundDevices.push_back({ devicePath, deviceName });
}

void initPortCache(const BindingStore& bindings) {
    // Bindings never change during DLL lifetime, so this runs once at startup.
    for (auto& binding : bindings.buttons) {
        if (binding.isSet() && !binding.isKeyboard()) {
            addUnique(binding.devicePath, binding.deviceName);
        }
    }
    for (auto& binding : bindings.dancerButtons) {
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

    Logger::info("[Input] " + std::to_string(s_boundDevices.size()) + " bound device(s)");
    for (const auto& device : s_boundDevices) {
        Logger::info("[Input]   " + device.name + " (" + device.path + ")");
    }

    bindings.mgr->setInputCallback([](void* userData) {
        updatePortCache(*static_cast<const BindingStore*>(userData));
    }, const_cast<BindingStore*>(&bindings));
}

static DWORD WINAPI inputPollThread(void* bindingsPtr) {
    const BindingStore& bindings = *static_cast<const BindingStore*>(bindingsPtr);
    initPortCache(bindings);
    while (true) {
        Sleep(1);
        updatePortCache(bindings);
    }
    return 0;
}

void startInputPollThread(const BindingStore& bindings) {
    Logger::info("[Input] Poll thread started");
    CreateThread(nullptr, 0, inputPollThread, const_cast<BindingStore*>(&bindings), 0, nullptr);
}
