#pragma once
#include <cstdint>
struct BindingStore;

bool handleDJIn(uint16_t port, uint8_t& out);
void startDJInputPollThread(BindingStore& bs);
