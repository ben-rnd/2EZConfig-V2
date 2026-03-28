#pragma once
#include <cstdint>
struct BindingStore;

// Handle IN opcodes — returns the value to load into AL/AX.
// Returns true if the port was recognized, false otherwise.
bool handleDJIn(uint16_t port, uint8_t& out);
bool handleDancerIn(uint16_t port, uint16_t& out);

void startInputPollThread(BindingStore& bs);
