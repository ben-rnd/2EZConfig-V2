#pragma once
#include <cstdint>
class InputManager;
struct BindingStore;

void handleDJOut(uint16_t port, uint8_t value, const BindingStore& bs, InputManager& mgr);
void handleDancerOut(uint16_t port, uint8_t value, const BindingStore& bs, InputManager& mgr);
