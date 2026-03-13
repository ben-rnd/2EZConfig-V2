#include "dll_input.h"
#include "bindings.h"
#include "input_manager.h"
#include "game_defs.h"
#include "logger.h"

std::atomic<uint8_t> s_djPortCache[7] = { 0xFF, 0xFF, 0xFF, 0x80, 0x80, 0xFF, 0xFF };
std::atomic<uint16_t> s_dancerPortCache[4] = { 0xF000, 0xF000, 0x0000, 0x00FF };

static std::vector<std::string> s_boundDevicePaths;

static uint8_t computePort0x101(const BindingStore& bindings, const BindingStore::DeviceSnapshotMap& deviceSnapshots) {
    uint8_t portValue = 0xFF;
    if (bindings.isHeldSnapshot(bindings.buttons[static_cast<int>(DJButton::P1_START)], deviceSnapshots)) {
        portValue &= ~0x01;
    }
    if (bindings.isHeldSnapshot(bindings.buttons[static_cast<int>(DJButton::P2_START)], deviceSnapshots)) {
        portValue &= ~0x02;
    }
    if (bindings.isHeldSnapshot(bindings.buttons[static_cast<int>(DJButton::EFFECTOR_1)], deviceSnapshots)) {
        portValue &= ~0x04;
    }
    if (bindings.isHeldSnapshot(bindings.buttons[static_cast<int>(DJButton::EFFECTOR_2)], deviceSnapshots)) {
        portValue &= ~0x08;
    }
    if (bindings.isHeldSnapshot(bindings.buttons[static_cast<int>(DJButton::EFFECTOR_3)], deviceSnapshots)) {
        portValue &= ~0x10;
    }
    if (bindings.isHeldSnapshot(bindings.buttons[static_cast<int>(DJButton::EFFECTOR_4)], deviceSnapshots)) {
        portValue &= ~0x20;
    }
    if (bindings.isHeldSnapshot(bindings.buttons[static_cast<int>(DJButton::SERVICE)], deviceSnapshots)) {
        portValue &= ~0x40;
    }
    if (bindings.isHeldSnapshot(bindings.buttons[static_cast<int>(DJButton::TEST)], deviceSnapshots)) {
        portValue &= ~0x80;
    }
    return portValue;
}

static uint8_t computePort0x102(const BindingStore& bindings, const BindingStore::DeviceSnapshotMap& deviceSnapshots) {
    uint8_t portValue = 0xFF;
    if (bindings.isHeldSnapshot(bindings.buttons[static_cast<int>(DJButton::P1_1)], deviceSnapshots)) {
        portValue &= ~0x01;
    }
    if (bindings.isHeldSnapshot(bindings.buttons[static_cast<int>(DJButton::P1_2)], deviceSnapshots)) {
        portValue &= ~0x02;
    }
    if (bindings.isHeldSnapshot(bindings.buttons[static_cast<int>(DJButton::P1_3)], deviceSnapshots)) {
        portValue &= ~0x04;
    }
    if (bindings.isHeldSnapshot(bindings.buttons[static_cast<int>(DJButton::P1_4)], deviceSnapshots)) {
        portValue &= ~0x08;
    }
    if (bindings.isHeldSnapshot(bindings.buttons[static_cast<int>(DJButton::P1_5)], deviceSnapshots)) {
        portValue &= ~0x10;
    }
    // bits 5-6 unused
    if (bindings.isHeldSnapshot(bindings.buttons[static_cast<int>(DJButton::P1_PEDAL)], deviceSnapshots)) {
        portValue &= ~0x80;
    }
    return portValue;
}

static uint8_t computePort0x106(const BindingStore& bindings, const BindingStore::DeviceSnapshotMap& deviceSnapshots) {
    uint8_t portValue = 0xFF;
    if (bindings.isHeldSnapshot(bindings.buttons[static_cast<int>(DJButton::P2_1)], deviceSnapshots)) {
        portValue &= ~0x01;
    }
    if (bindings.isHeldSnapshot(bindings.buttons[static_cast<int>(DJButton::P2_2)], deviceSnapshots)) {
        portValue &= ~0x02;
    }
    if (bindings.isHeldSnapshot(bindings.buttons[static_cast<int>(DJButton::P2_3)], deviceSnapshots)) {
        portValue &= ~0x04;
    }
    if (bindings.isHeldSnapshot(bindings.buttons[static_cast<int>(DJButton::P2_4)], deviceSnapshots)) {
        portValue &= ~0x08;
    }
    if (bindings.isHeldSnapshot(bindings.buttons[static_cast<int>(DJButton::P2_5)], deviceSnapshots)) {
        portValue &= ~0x10;
    }
    // bits 5-6 unused
    if (bindings.isHeldSnapshot(bindings.buttons[static_cast<int>(DJButton::P2_PEDAL)], deviceSnapshots)) {
        portValue &= ~0x80;
    }
    return portValue;
}

static uint16_t computePort0x300(const BindingStore& bindings, const BindingStore::DeviceSnapshotMap& deviceSnapshots) {
    uint16_t portValue = 0x0FFF;
    if (bindings.isHeldSnapshot(bindings.dancerButtons[static_cast<int>(DancerButton::P1_LEFT)], deviceSnapshots)) {
        portValue &= ~0x00F;
    }
    if (bindings.isHeldSnapshot(bindings.dancerButtons[static_cast<int>(DancerButton::P1_CENTRE)], deviceSnapshots)) {
        portValue &= ~0x0F0;
    }
    if (bindings.isHeldSnapshot(bindings.dancerButtons[static_cast<int>(DancerButton::P1_RIGHT)], deviceSnapshots)) {
        portValue &= ~0xF00;
    }
    return static_cast<uint16_t>(~portValue);
}

static uint16_t computePort0x302(const BindingStore& bindings, const BindingStore::DeviceSnapshotMap& deviceSnapshots) {
    uint16_t portValue = 0x0FFF;
    if (bindings.isHeldSnapshot(bindings.dancerButtons[static_cast<int>(DancerButton::P2_LEFT)], deviceSnapshots)) {
        portValue &= ~0x00F;
    }
    if (bindings.isHeldSnapshot(bindings.dancerButtons[static_cast<int>(DancerButton::P2_CENTRE)], deviceSnapshots)) {
        portValue &= ~0x0F0;
    }
    if (bindings.isHeldSnapshot(bindings.dancerButtons[static_cast<int>(DancerButton::P2_RIGHT)], deviceSnapshots)) {
        portValue &= ~0xF00;
    }
    return static_cast<uint16_t>(~portValue);
}

static uint16_t computePort0x306(const BindingStore& bindings, const BindingStore::DeviceSnapshotMap& deviceSnapshots) {
    uint16_t portValue = 0xFFFF;
    if (bindings.isHeldSnapshot(bindings.dancerButtons[static_cast<int>(DancerButton::P1_L_SENSOR_TOP)], deviceSnapshots)) {
        portValue &= ~(1 << 11);
    }
    if (bindings.isHeldSnapshot(bindings.dancerButtons[static_cast<int>(DancerButton::P1_L_SENSOR_BOT)], deviceSnapshots)) {
        portValue &= ~(1 << 12);
    }
    if (bindings.isHeldSnapshot(bindings.dancerButtons[static_cast<int>(DancerButton::P1_R_SENSOR_TOP)], deviceSnapshots)) {
        portValue &= ~(1 << 10);
    }
    if (bindings.isHeldSnapshot(bindings.dancerButtons[static_cast<int>(DancerButton::P1_R_SENSOR_BOT)], deviceSnapshots)) {
        portValue &= ~(1 << 13);
    }
    if (bindings.isHeldSnapshot(bindings.dancerButtons[static_cast<int>(DancerButton::P2_L_SENSOR_TOP)], deviceSnapshots)) {
        portValue &= ~(1 << 9);
    }
    if (bindings.isHeldSnapshot(bindings.dancerButtons[static_cast<int>(DancerButton::P2_L_SENSOR_BOT)], deviceSnapshots)) {
        portValue &= ~(1 << 14);
    }
    if (bindings.isHeldSnapshot(bindings.dancerButtons[static_cast<int>(DancerButton::P2_R_SENSOR_TOP)], deviceSnapshots)) {
        portValue &= ~(1 << 8);
    }
    if (bindings.isHeldSnapshot(bindings.dancerButtons[static_cast<int>(DancerButton::P2_R_SENSOR_BOT)], deviceSnapshots)) {
        portValue &= ~(1 << 15);
    }
    if (bindings.isHeldSnapshot(bindings.dancerButtons[static_cast<int>(DancerButton::TEST)], deviceSnapshots)) {
        portValue &= 0xFF00 | (1 << 5);
    }
    if (bindings.isHeldSnapshot(bindings.dancerButtons[static_cast<int>(DancerButton::SERVICE)], deviceSnapshots)) {
        portValue &= 0xFF00 | (1 << 4);
    }
    return portValue ^ 0xFF00;
}

void updatePortCache(const BindingStore& bindings) {
    BindingStore::DeviceSnapshotMap deviceSnapshots;
    for (const auto& path : s_boundDevicePaths) {
        DeviceSnapshot snapshot;
        if (bindings.mgr->snapshotDevice(path, snapshot)) {
            deviceSnapshots[path] = std::move(snapshot);
        }
    }
    // DJ button ports
    s_djPortCache[1].store(computePort0x101(bindings, deviceSnapshots));
    s_djPortCache[2].store(computePort0x102(bindings, deviceSnapshots));
    s_djPortCache[6].store(computePort0x106(bindings, deviceSnapshots));

    // DJ analog ports (turntables)
    s_djPortCache[3].store(bindings.getPositionSnapshot(bindings.analogs[static_cast<int>(Analog::P1_TURNTABLE)], bindings.mgr->getVttPosition(static_cast<int>(Analog::P1_TURNTABLE)), deviceSnapshots));
    s_djPortCache[4].store(bindings.getPositionSnapshot(bindings.analogs[static_cast<int>(Analog::P2_TURNTABLE)], bindings.mgr->getVttPosition(static_cast<int>(Analog::P2_TURNTABLE)), deviceSnapshots));

    // Dancer ports
    s_dancerPortCache[0].store(computePort0x300(bindings, deviceSnapshots));
    s_dancerPortCache[1].store(computePort0x302(bindings, deviceSnapshots));
    s_dancerPortCache[3].store(computePort0x306(bindings, deviceSnapshots));
}

static void addUnique(const std::string& devicePath) {
    if (devicePath.empty()) {
        return;
    }
    for (const std::string& existing : s_boundDevicePaths) {
        if (existing == devicePath) {
            return;
        }
    }
    s_boundDevicePaths.push_back(devicePath);
}

void initPortCache(const BindingStore& bindings) {
    // Bindings never change during DLL lifetime, so this runs once at startup.
    for (auto& binding : bindings.buttons) {
        if (binding.isSet() && !binding.isKeyboard()) {
            addUnique(binding.devicePath);
        }
    }
    for (auto& binding : bindings.dancerButtons) {
        if (binding.isSet() && !binding.isKeyboard()) {
            addUnique(binding.devicePath);
        }
    }
    for (auto& analogBinding : bindings.analogs) {
        if (analogBinding.isSet()) {
            addUnique(analogBinding.devicePath);
        }
        if (analogBinding.vttPlus.isSet() && !analogBinding.vttPlus.isKeyboard()) {
            addUnique(analogBinding.vttPlus.devicePath);
        }
        if (analogBinding.vttMinus.isSet() && !analogBinding.vttMinus.isKeyboard()) {
            addUnique(analogBinding.vttMinus.devicePath);
        }
    }

    Logger::info("[Input] " + std::to_string(s_boundDevicePaths.size()) + " bound device(s)");
    for (const auto& path : s_boundDevicePaths) {
        Logger::info("[Input]   " + path);
    }

    bindings.mgr->setInputCallback([](void* bindings) {
        updatePortCache(*static_cast<const BindingStore*>(bindings));
    }, const_cast<BindingStore*>(&bindings));
}
