#include <cstring>
#include <string>

#include "game_defs.h"
#include "sabin_io.h"
#include "logger.h"

namespace SabinIO {

static bool s_lightStates[static_cast<int>(SabinLight::COUNT)] = {};

static char s_parseBuf[64] = {};
static int s_parseLen = 0;

static void processCommand(const char* cmd, int len) {
    if (len < 4 || cmd[0] != '(' || cmd[len - 1] != ')') {
        return;
    }

    char state = cmd[len - 2];
    if (state != '+' && state != '-') {
        return;
    }
    bool on = (state == '-');

    // Extract the inner command without parens or +/-: "(L10+)" -> "L10"
    std::string base(cmd + 1, len - 3);

    for (int i = 0; i < static_cast<int>(SabinLight::COUNT); i++) {
        if (base == sabinLightCommands[i]) {
            s_lightStates[i] = on;
            return;
        }
    }

}

void initOutput() {
    memset(s_lightStates, 0, sizeof(s_lightStates));
    s_parseLen = 0;
    Logger::info("[IO] Output initialized");
}

void onSerialWrite(const uint8_t* data, uint32_t length) {
    for (uint32_t i = 0; i < length; i++) {
        char c = static_cast<char>(data[i]);

        if (c == '(') {
            s_parseLen = 0;
            s_parseBuf[s_parseLen++] = c;
        } else if (s_parseLen > 0) {
            if (s_parseLen < static_cast<int>(sizeof(s_parseBuf)) - 1) {
                s_parseBuf[s_parseLen++] = c;
            }
            if (c == ')') {
                s_parseBuf[s_parseLen] = '\0';
                processCommand(s_parseBuf, s_parseLen);
                s_parseLen = 0;
            }
        }
    }
}

bool getLightState(int lightIndex) {
    if (lightIndex < 0 || lightIndex >= static_cast<int>(SabinLight::COUNT)) {
        return false;
    }
    return s_lightStates[lightIndex];
}

}  // namespace SabinIO
