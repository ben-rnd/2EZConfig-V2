#pragma once
#include "fastapi.h"
void EncryptPacket(HL_API* packet);
void DecryptPacket(HL_API* packet);
void EncryptParams(HL_API* packet);
void DecryptParams(HL_API* packet);
