#pragma once
#include "io.hardlock.internal.h"

typedef struct _EHardlockEntry{
    char*          Name;
    unsigned short ModAd;
    unsigned short Seed1;
    unsigned short Seed2;
    unsigned short Seed3;
}EHardlockEntry;

typedef struct _EMULATED_HARDLOCK {
    unsigned short DriverVersion;
    unsigned short DriverApiVersion;
    unsigned short Remote;
    unsigned short Port;
    KEY_DATA Device;
}EMULATED_HARDLOCK,* PEMULATED_HARDLOCK;

int LoadHardLockInfo(unsigned short modAd, unsigned short seed1, unsigned short seed2, unsigned short seed3);
int FindHardLock(PEMULATED_HARDLOCK pEmulatedHardLock, unsigned char* id_ref, unsigned char* id_verify);

void ProcessHardLockCommand(PEMULATED_HARDLOCK EHardLock, unsigned char* data, unsigned int length);
void ProcessHardlockIoctlWindows(unsigned int IoControlCode, unsigned char* InputBuffer,unsigned int InputBufferLength,unsigned char* OutputBuffer,unsigned int OutputBufferLength);
