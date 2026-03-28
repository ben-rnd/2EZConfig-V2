#include <cstring>
#include <string>

#include "game_defs.h"
#include "sabin_io.h"
#include "logger.h"

namespace SabinIO {

static IOBuffer* s_ioBuf = nullptr;
static bool s_prevState[static_cast<int>(SabinButton::COUNT)] = {};

static void sendButton(const char* str) {
    for (const char* p = str; *p; p++) {
        uint32_t available =
            (s_ioBuf->writePos - s_ioBuf->readPos + 0x1000) & 0xFFF;
        if (available == 0xFFF) {
            return;
        }
        s_ioBuf->buffer[s_ioBuf->writePos] = static_cast<uint8_t>(*p);
        s_ioBuf->writePos = (s_ioBuf->writePos + 1) & 0xFFF;
    }
}

void initInput(IOBuffer* ioBuf) {
    s_ioBuf = ioBuf;
    Logger::info("[IO] IO buffer initialized");
}

void processButton(int buttonIndex, bool pressed) {
    if (!s_ioBuf ||
        buttonIndex < 0 || buttonIndex >= static_cast<int>(SabinButton::COUNT) ||
        s_prevState[buttonIndex] == pressed) {
        return;
    }
    s_prevState[buttonIndex] = pressed;

    const char* base = sabinButtonCommands[buttonIndex];

    // service buttons only send press (no +/-)
    if (strcmp(base, "Tet") == 0 || strcmp(base, "Svce") == 0 ||
        strcmp(base, "Coin") == 0 || strcmp(base, "Bill") == 0) {
        if (pressed) {
            sendButton(("(" + std::string(base) + ")").c_str());
        }
        return;
    }

    // Build full command: "S10" -> "(S10-)" or "(S10+)"
    // - = pressed, + = released
    std::string cmd = "(" + std::string(base) + (pressed ? '-' : '+') + ")";
    sendButton(cmd.c_str());
}
bool hasNewData() {
    if (!s_ioBuf) {
        return false;
    }
    return s_ioBuf->writePos != s_ioBuf->readPos;
}

}  // namespace SabinIO
