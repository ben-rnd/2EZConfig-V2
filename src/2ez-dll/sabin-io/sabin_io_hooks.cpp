#include <windows.h>
#include <timeapi.h>

#include <cstdint>
#include <cstring>

#include "bindings.h"
#include "game_defs.h"
#include "input_manager.h"
#include "sabin_io.h"
#include "sabin_io_internal.h"
#include "logger.h"

extern "C" {
#include "memutils.h"
}

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
    SabinIO::onSerialWrite(reinterpret_cast<const uint8_t*>(data),
                           static_cast<uint32_t>(length));
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

static DWORD WINAPI inputThread(void*) {
    timeBeginPeriod(1);

    while (true) {
        tryInitIOBuffer();

        if (s_ioBufReady) {
            for (int i = 0; i < static_cast<int>(SabinButton::COUNT); i++) {
                bool held = s_bindings->isHeld(s_bindings->sabinButtons[i]);
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
        for (int i = 0; i < static_cast<int>(SabinLight::COUNT); i++) {
            const auto& lb = s_bindings->sabinLights[i];
            if (lb.isSet()) {
                float val = SabinIO::getLightState(i) ? 1.0f : 0.0f;
                s_input->setLight(lb.devicePath, lb.outputIdx, val);
            }
        }

        Sleep(1);
    }
    return 0;
}

void SabinIO::installHooks(BindingStore* bindings, InputManager* input) {
    s_bindings = bindings;
    s_input = input;

    void* serialWriteAddr = reinterpret_cast<void*>(gameBase() + RVA_SERIAL_WRITE_FN);
    struct HotPatchInfo ctx;
    if (memutils_hotpatch(serialWriteAddr, reinterpret_cast<void*>(&Hook_SerialWrite),
                          9, &ctx, reinterpret_cast<void**>(&Real_SerialWrite))) {
        Logger::info("[IO] Serial_Write hooked");
    } else {
        Logger::error("[IO] Failed to hook Serial_Write");
    }

    SabinIO::initOutput();
    tryInitIOBuffer();

    CreateThread(nullptr, 0, inputThread, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, outputThread, nullptr, 0, nullptr);

    Logger::info("[IO] Installed");
}
