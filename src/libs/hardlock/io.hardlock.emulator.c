#include <windows.h>
#include <stdlib.h>
#include "io.hardlock.internal.h"
#include "io.hardlock.protocol.h"
#include "io.hardlock.emulator.h"

//#include "nativecore/debug.h"


PEMULATED_HARDLOCK pEmulatedHardLock = NULL;

static EHardlockEntry HardLockTable[] = { 
// Uncomment this if you have some preloaded tables to include.
// #include "zdqUxer9.inl"
    {
        "",
        0,
        0,
        0,
        0
    }
};

// Because of how inexpensive this is, we're just gonna burn through the hardlock entries and find the right dongle.
int FindHardLock(PEMULATED_HARDLOCK pEmulatedHardLock, unsigned char* id_ref, unsigned char* id_verify) {

    int num_entries = sizeof(HardLockTable) / sizeof(EHardlockEntry);
    unsigned char crypt_buffer[0x50] = { 0x00 };
    for (int i = 0; i < num_entries; i++) {
        if (pEmulatedHardLock->Device.HdkID == HardLockTable[i].ModAd) {
            pEmulatedHardLock->Device.HdkSeed1 = HardLockTable[i].Seed1;
            pEmulatedHardLock->Device.HdkSeed2 = HardLockTable[i].Seed2;
            pEmulatedHardLock->Device.HdkSeed3 = HardLockTable[i].Seed3;
            memcpy(crypt_buffer, id_ref, 8);
            HL_CRYPT(&pEmulatedHardLock->Device, crypt_buffer);
            if (!memcmp(crypt_buffer, id_verify, 8)) {
                DBG_printfA("[io.hardlock]: Loaded HardLock for  %s",HardLockTable[i].Name);
                return TRUE;
            }
        }
    }
    return FALSE;
}


void ProcessHardLockCommand(PEMULATED_HARDLOCK pEHardLock, unsigned char* data, unsigned int length) {
    // A packet should never be smaller than 256 bytes.
    if (length < 0x100) { return; }

    int offset;
    int i;
    unsigned char crypt_buffer[0x50] = { 0x00 };
    DBG_printfW(L"Process HardLock Packet:");
    DBG_printfW(L"Before Decrypt [Outer]");
    DBG_print_buffer(data, length);
    DecryptPacket((HL_API*)data);
    DBG_printfW(L"Before Decrypt [Param]");

    DBG_print_buffer(data, length);
    DecryptParams((HL_API*)data);
    DBG_printfW(L"After Decrypt");
    DBG_print_buffer(data, length);


    // Process Packet
    HL_API* packet = (HL_API*)data;

    DBG_printfW(L"[io.hardlock]: HL API CALL: %04X", packet->Function);
    switch (packet->Function) {
        case API_INIT: // Login
            if(packet->ModID == EYE_DONGLE){
                DBG_printfW(L"[io.hardlock]: HL_LOGIN");
                // Find Dongle and Verify
                if (!pEHardLock->Device.HdkID) {
                    pEHardLock->Device.HdkID = packet->Module.Eye.ModAd;
                }
                if (!pEHardLock->Device.HdkSeed1 && !FindHardLock(pEHardLock, packet->ID_Ref, packet->ID_Verify)) {
                    DBG_printfW(L"[io.hardlock]: WARNING: Failed Dongle Crypt Test - Possibly Wrong Seeds");
                    packet->Status = UNKNOWN_DONGLE;
                } else{
                    packet->Status = STATUS_OK;
                    packet->Port = pEHardLock->Port;
                    packet->Remote = pEHardLock->Remote;
                }

                packet->Tail[0] = 0x01;
                packet->Tail[1] = 0x01;
            }else if(packet->ModID == HASP_DONGLE){
                DBG_printfW(L"[io.hardlock]: HASP_LOGIN");
                DBG_printfW(L"P1: %04X PW 1: %04X PW 2: %04X P2: %04X P3: %04X",packet->Module.Hasp.P1,packet->Module.Hasp.PW1,packet->Module.Hasp.PW2,packet->Module2.Hasp2.P2,packet->Module2.Hasp2.P3);
                packet->Status = STATUS_OK;
                packet->Port = pEHardLock->Port;
                packet->Remote = pEHardLock->Remote;
                packet->Tail[0] = 0x01;
                packet->Tail[1] = 0x01;
            }

            break;
        case 0x12C:
            DBG_printfA("[io.hardlock]: HASP Function 0x12C");
            packet->Tail[0] = 0x01;
            break;
        case 0x12E:
            DBG_printfA("[io.hardlock]: HASP Function 0x12E");
            packet->Tail[0] = 0x01;
            DBG_printfA("HASP SOMETHING: Blksize: %04X DataPtr: %p",packet->Bcnt,packet->Data);
            DBG_print_buffer((unsigned char*)packet->Data,packet->Bcnt*8);

            break;
        case API_DOWN: // Logout
            DBG_printfW(L"[io.hardlock]: HL_LOGOUT");
            break;
        case API_AVAIL: // We don't need to do anything for this.
            DBG_printfW(L"[io.hardlock]: HL_AVAIL");
            break;
        case API_CRYPT:
            DBG_printfW(L"[io.hardlock]: HL_CRYPT");
            offset = 0x100;
            for (i = 0; i < packet->Bcnt; i++) {
                DBG_printfW(L"[io.hardlock]: HL_CRYPT [Request]:");
                memcpy(crypt_buffer, data + offset, 8);
                DBG_print_buffer(crypt_buffer, 8);
                HL_CRYPT(&pEHardLock->Device, crypt_buffer);
                DBG_printfW(L"[io.hardlock]: HL_CRYPT [Response]:");
                DBG_print_buffer(crypt_buffer, 8);
                memcpy(data + offset, crypt_buffer, 8);
                offset += 8;
            }
            break;
        case API_CODE:
            offset = 0x100;

            DBG_printfW(L"[io.hardlock]: HL_CODE %d Blocks [BEFORE]", packet->Bcnt);
            DBG_print_buffer(data + offset, packet->Bcnt * 8);

            for (i = 0; i < packet->Bcnt - 1; i++) {
                memcpy(pEHardLock->Device.HdkTempMem, data + offset, 8);
                HL_CODE(&pEHardLock->Device, crypt_buffer, packet->Bcnt);
                offset += 8;
            }
            DBG_printfW(L"[io.hardlock]: HL_CODE [Response]:");
            DBG_print_buffer(crypt_buffer, sizeof(crypt_buffer));
            memcpy(data + 0x100, crypt_buffer, 8);
            memcpy(data + 0x108, crypt_buffer + 8, 8);
            memcpy(data + 0x110, crypt_buffer + 0x10, 8);
            *(DWORD*)(data + 0x118) += *(DWORD*)(crypt_buffer + 0x18);
            *(DWORD*)(data + 0x11C) += *(DWORD*)(crypt_buffer + 0x1C);
            memcpy(data + 0x120, crypt_buffer + 0x20, 8);
            memcpy(data + 0x128, crypt_buffer + 0x28, 8);

            DBG_printfW(L"[io.hardlock]: HL_CODE [AFTER]");
            offset = 0x100;
            DBG_print_buffer(data + offset, packet->Bcnt * 8);


            packet->Status = STATUS_OK;
            break;
        default:
            DBG_printfW(L"[io.hardlock]: WARNING: Unhandled Function: %04X", packet->Function);
            break;
    }

    // Process Response
    EncryptParams((HL_API*)data);
    DBG_printfW(L"[io.hardlock]: After Encrypt [Param]");
    DBG_print_buffer(data, length);
    EncryptPacket((HL_API*)data);

    DBG_printfW(L"[io.hardlock]: After Encrypt [Outer]");
    DBG_print_buffer(data, length);
}


// Modified: accepts dongle values directly instead of reading from an INI file.
// Static values (DriverVersion, DriverApiVersion, Port, Remote, DongleType) are hardcoded.
int LoadHardLockInfo(unsigned short modAd, unsigned short seed1, unsigned short seed2, unsigned short seed3) {
    pEmulatedHardLock = (PEMULATED_HARDLOCK)calloc(1, sizeof(EMULATED_HARDLOCK));
    pEmulatedHardLock->Device.DongleType   = 2;
    pEmulatedHardLock->DriverVersion       = 0x305;
    pEmulatedHardLock->DriverApiVersion    = 0x356;
    pEmulatedHardLock->Port                = 0x378;
    pEmulatedHardLock->Remote              = 1;
    pEmulatedHardLock->Device.HdkID       = modAd;
    pEmulatedHardLock->Device.HdkSeed1    = seed1;
    pEmulatedHardLock->Device.HdkSeed2    = seed2;
    pEmulatedHardLock->Device.HdkSeed3    = seed3;
    return TRUE;
}


#define IOCTL_HARDLOCK_PACKET_2    0x9C40244C
#define IOCTL_HARDLOCK_API_VERSION 0x9C402450
#define IOCTL_HARDLOCK_PACKET      0x9C402458
#define IOCTL_HARDLOCK_CHALLENGE   0x9C4024A0
#define IOCTL_HARDLOCK_UNK         0x9C4024A8
#define IOCTL_HARDLOCK_UNK2        0x9C402468

void ProcessHardlockIoctlWindows(unsigned int IoControlCode, unsigned char* InputBuffer,unsigned int InputBufferLength,unsigned char* OutputBuffer,unsigned int OutputBufferLength){
    // HardLock ioctl Logic Goes Here
    unsigned char* in_data = (unsigned char*)InputBuffer;
    unsigned char* out_data = (unsigned char*)OutputBuffer;
    unsigned int challenge_request = 0;
    unsigned int challenge_response = 0;
    DBG_printfW(L"[io.hardlock]: HardLock ioctl: %04X", IoControlCode);
    DBG_print_buffer(in_data, InputBufferLength);
    switch (IoControlCode) {
        case IOCTL_HARDLOCK_UNK2:
            break;
        case IOCTL_HARDLOCK_UNK: // Curious as to what this means.
            memset(OutputBuffer, 0x05, 1);
            break;
        case IOCTL_HARDLOCK_API_VERSION:
            if (in_data != out_data) { memcpy(out_data, in_data, InputBufferLength); }
            out_data[2] = 0xFA;
            out_data[3] = 0xFA;
            switch (in_data[0]) {
                case 0:
                    *(WORD*)(out_data + 4) = (WORD)pEmulatedHardLock->DriverVersion;
                    break;
                case 1:
                    *(WORD*)(out_data + 4) = (WORD)pEmulatedHardLock->DriverApiVersion;
                    break;
                default:
                    break;
            }

            break;
        case IOCTL_HARDLOCK_CHALLENGE:
            challenge_request = *(unsigned int*)in_data;
            if (challenge_request == 0xBEEFBAB2) {
                challenge_response = 0x2AFEBABE;
            }
            else if (challenge_request == 0xBEEFBABE) {
                challenge_response = 0xCAFEBABE;
            }
            memcpy(out_data, &challenge_response, sizeof(challenge_response));
            break;
        case IOCTL_HARDLOCK_PACKET:
        case IOCTL_HARDLOCK_PACKET_2:
            ProcessHardLockCommand(pEmulatedHardLock, out_data, InputBufferLength);
            break;
        default:
            DBG_printfW(L"DeviceIoControl Warning: Unsupported OpCode: %04X", IoControlCode);
            break;
    }
}

