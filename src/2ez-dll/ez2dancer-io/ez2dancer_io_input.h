#pragma once
#include <cstdint>
struct BindingStore;

bool handleDancerIn(uint16_t port, uint16_t& out);
void startDancerInputPollThread(BindingStore& bs);
