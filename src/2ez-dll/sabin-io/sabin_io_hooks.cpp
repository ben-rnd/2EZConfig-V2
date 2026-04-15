#include <windows.h>
#include <timeapi.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "bindings.h"
#include "game_defs.h"
#include "input_manager.h"
#include "sabin_io.h"
#include "sabin_io_internal.h"
#include "logger.h"
#include "hooks.h"

static constexpr uintptr_t RVA_SERIAL_IO_OBJECT = 0x10E9DC;
static constexpr uintptr_t RVA_SERIAL_WRITE_FN  = 0x0471E0;

static uintptr_t gameBase() {
    return reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
}

static SabinIO::SerialIO* serialIO() {
    return reinterpret_cast<SabinIO::SerialIO*>(gameBase() + RVA_SERIAL_IO_OBJECT);
}

typedef void(__attribute__((thiscall)) *SerialWriteFn)(void* thisPtr, const char* data, int length);
static SerialWriteFn Real_SerialWrite = nullptr;

static void __attribute__((thiscall)) Hook_SerialWrite(void* thisPtr, const char* data, int length) {
    SabinIO::onSerialWrite(reinterpret_cast<const uint8_t*>(data), static_cast<uint32_t>(length));
    Real_SerialWrite(thisPtr, data, length);
}

static bool s_ioBufReady = false;

static void tryInitIOBuffer() {
    if (s_ioBufReady) {
        return;
    }
    auto* obj = serialIO();
    memset(&obj->ioBuffer, 0, sizeof(SabinIO::IOBuffer));
    SabinIO::initInput(&obj->ioBuffer);
    s_ioBufReady = true;
}

static BindingStore* s_bindings = nullptr;
static InputManager* s_input = nullptr;

struct BoundDevice {
    std::string path;
    std::string name;
};

static std::vector<BoundDevice> s_boundDevices;

static void addUnique(const std::string& devicePath, const std::string& deviceName) {
    if (devicePath.empty()) return;
    for (const auto& device : s_boundDevices) {
        if (device.path == devicePath) return;
    }
    s_boundDevices.push_back({ devicePath, deviceName });
}

static void initBoundDevices() {
    for (auto& binding : s_bindings->sabinButtons) {
        if (binding.isSet() && !binding.isKeyboard()) {
            addUnique(binding.devicePath, binding.deviceName);
        }
    }
    Logger::info("[Sabin Input] " + std::to_string(s_boundDevices.size()) + " bound device(s)");
}

static DWORD WINAPI inputThread(void*) {
    timeBeginPeriod(1);
    initBoundDevices();

    while (true) {
        tryInitIOBuffer();

        if (s_ioBufReady) {
            BindingStore::DeviceSnapshotMap deviceSnapshots;
            for (const auto& device : s_boundDevices) {
                DeviceSnapshot snapshot;
                if (s_bindings->mgr->snapshotDevice(device.path, snapshot)) {
                    deviceSnapshots[device.path] = std::move(snapshot);
                }
            }

            for (int i = 0; i < static_cast<int>(SabinButton::COUNT); i++) {
                bool held = s_bindings->isHeldSnapshot(s_bindings->sabinButtons[i], deviceSnapshots);
                SabinIO::processButton(i, held);
            }

            if (SabinIO::hasNewData()) {
                HWND hWnd = serialIO()->hWnd;
                if (hWnd) {
                    PostMessageA(hWnd, 0x0401, 0, 0);
                }
            }
        }

        Sleep(1);
    }
    return 0;
}

static DWORD WINAPI outputThread(void*) {
    timeBeginPeriod(1);

    while (true) {
        Sleep(1);
        if (!SabinIO::s_lightDirty.load()) continue;
        SabinIO::s_lightDirty.store(false);

        for (int i = 0; i < static_cast<int>(SabinLight::COUNT); i++) {
            const auto& lb = s_bindings->sabinLights[i];
            if (lb.isSet()) {
                float val = SabinIO::getLightState(i) ? 1.0f : 0.0f;
                s_input->setLight(lb.devicePath, lb.outputIdx, val);
            }
        }
    }
    return 0;
}

void SabinIO::installHooks() {
    void* serialWriteAddr = reinterpret_cast<void*>(gameBase() + RVA_SERIAL_WRITE_FN);
    if (hook_create(serialWriteAddr, reinterpret_cast<void*>(&Hook_SerialWrite), reinterpret_cast<void**>(&Real_SerialWrite))) {
        Logger::info("[IO] Serial_Write hooked (early)");
    } else {
        Logger::error("[IO] Failed to hook Serial_Write");
    }
    SabinIO::initOutput();
}

void SabinIO::initialiseIO(BindingStore* bindings, InputManager* input) {
    s_bindings = bindings;
    s_input = input;

    tryInitIOBuffer();

    CreateThread(nullptr, 0, inputThread, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, outputThread, nullptr, 0, nullptr);

    Logger::info("[IO] Installed");
}
