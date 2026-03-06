#pragma once
#include <cstdint>
class InputManager;
struct BindingStore;

void handleDJOut(uint16_t port, uint8_t value, const BindingStore& bs);
void handleDancerOut(uint16_t port, uint8_t value, const BindingStore& bs);

// Start background thread that flushes buffered light state to InputManager.
void startLightFlushThread(const BindingStore& bs, InputManager& mgr);
