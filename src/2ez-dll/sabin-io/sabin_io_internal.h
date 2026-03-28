#pragma once
#include "sabin_io.h"

// Internal functions shared between io_input.cpp, io_output.cpp, and io_hooks.cpp.
// Not part of the public API.
namespace SabinIO {

void initInput(IOBuffer* ioBuf);
bool hasNewData();

void initOutput();
void onSerialWrite(const uint8_t* data, uint32_t length);
bool getLightState(int lightIndex);
void processButton(int buttonIndex, bool pressed);

}  // namespace SabinIO
