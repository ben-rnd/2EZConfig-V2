// Processing For HardLock Packets

#include "io.hardlock.protocol.h"

#ifndef ROL8
#define ROL8(n, r)  (((unsigned char)(n) << (r)) | ((unsigned char)(n) >> (8 - (r)))) /* only works for uint8_t */
#endif
#ifndef ROR8
#define ROR8(n, r)  (((unsigned char)(n) >> (r)) | ((unsigned char)(n) << (8 - (r)))) /* only works for uint8_t */
#endif
#ifndef ROL16
#define ROL16(n, r) (((unsigned short)(n) << (r)) | ((unsigned short)(n) >> (16 - (r)))) /* only works for uint16_t */
#endif
#ifndef ROR16
#define ROR16(n, r) (((unsigned short)(n) >> (r)) | ((unsigned short)(n) << (16 - (r)))) /* only works for uint16_t */
#endif

// PACKET PARAMETER CODECS
void DecryptParams(HL_API* packet) {
    int i;
    switch (packet->CryptVersion) {
        case 1:
            packet->API_Version_ID[0] = ((ROL16(packet->CryptSeed, 3) ^ packet->API_Version_ID[0]) + 0x7C) ^ 0x14;
            packet->API_Version_ID[1] = ((packet->API_Version_ID[1] ^ (packet->CryptSeed & 0xFF) - 0x20) ^ 0x1C);
            packet->API_Options[0] = (((packet->API_Options[0] ^ 0xAA) - ROL16(packet->CryptSeed, 9)) ^ 0x5131) - 0x19;
            packet->API_Options[1] = (ROR16(packet->CryptSeed, 1) ^ packet->API_Options[1] ^ 0x8407) - 0x0F;
            packet->ModID = (((ROL16(packet->CryptSeed, 6) ^ packet->ModID ^ 0x8450) - 0x11) ^ 0x5131) - 0xE9;
            if (packet->ModID == HASP_DONGLE) {
                packet->Module.Hasp.PW1 = ((packet->Module.Hasp.PW1 ^ (packet->CryptSeed - 0x0B) ^ 0x35DB8414) - 0x4D2);
                packet->Module.Hasp.PW2 = ((packet->Module.Hasp.PW2 - ROR16(packet->CryptSeed, 1) + 0x0B) ^ 0x35131) - 0x4EAD;
                packet->Module.Hasp.P1 = ((packet->Module.Hasp.P1 ^ (ROL16(packet->CryptSeed, 7) - 0x0B)) + 0x5EDD);
                packet->Module2.Hasp2.P2 = (packet->Module2.Hasp2.P2 ^ (ROR16(packet->CryptSeed, 5) + 0x0B)) - 0x4E3;
                packet->Module2.Hasp2.P3 = ((packet->Module2.Hasp2.P3 - packet->CryptSeed) ^ 0xD557) - 0x4D2;
            }
            else {
                // Ignore the names, it's a union and these are what fits...
                packet->Module.Lt.LT_Reserved = (((ROR16(packet->CryptSeed, 4) - 0x0B) ^ packet->Module.Lt.LT_Reserved) - 0x11) ^ 0x84EF;
                packet->Module.Lt.Reg = ((packet->Module.Lt.Reg - packet->CryptSeed + 0x0B) ^ 0x5131) - 0xE7;
                packet->Module.Lt.Value = ((packet->CryptSeed - 0x0C) ^ packet->Module.Lt.Value) - 0x9C;
                packet->Module.Lt.Password[0] = ((ROL16(packet->CryptSeed, 5) - 0x0C) ^ packet->Module.Lt.Password[0]) - 0x88;
                packet->Module.Lt.Password[1] = ((ROL16(packet->CryptSeed, 5) - 0x0C) ^ packet->Module.Lt.Password[1]) - 0x88;
            }
            packet->Data = ((packet->CryptSeed + 1) ^ (unsigned int)packet->Data) - 0x39;
            packet->Bcnt = (ROR16(packet->CryptSeed, 3) ^ packet->Bcnt ^ 0xF5) - 0xB8;
            packet->Function = (((packet->Function - ROL16(packet->CryptSeed, 2)) ^ 0x8414) - 0xE9) ^ 0x5131;
            packet->Status = (((packet->Status - ROR16(packet->CryptSeed, 5)) ^ 0x8414) - 0xE9) ^ 0x5131;
            packet->Remote ^= ROL16(packet->CryptSeed, 3) ^ 0xFA ^ 0x8450;
            // Something fun to note: packet->Port is not encrypted.
            packet->Speed = ((packet->Speed - (ROL16(packet->CryptSeed, 5) ^ 0x85)) ^ 0x5120) - 0xAD;
            packet->NetUsers = (ROL16(packet->CryptSeed, 6) ^ packet->NetUsers ^ 0x380) - 0xCC6;


            for (i = 0; i < 8; i++) {
                packet->ID_Ref[i] = ((packet->ID_Ref[i] ^ (packet->CryptSeed - 2)) + 0x80) & 0xFF;
                packet->ID_Verify[i] = ((packet->ID_Verify[i] ^ (packet->CryptSeed - 0x34)) + 0x58) & 0xFF;
            }

            packet->Task_ID = (packet->Task_ID ^ packet->CryptSeed ^ 0x15) - 0x15E;
            packet->MaxUsers = (ROR16(packet->CryptSeed, 3) ^ packet->MaxUsers ^ 0x5752) - 0x33D6;
            packet->Timeout = (packet->Timeout ^ ROR16(packet->CryptSeed, 2) ^ 0xFDDE5) - 0x54;
            packet->ShortLife = (packet->ShortLife ^ ROR16(packet->CryptSeed, 3) ^ 0x15) - 0xFA;
            packet->Application = (((packet->Application ^ packet->CryptSeed) - 0x65) ^ 0x5131) - 0x7FFE;

            //Alternate Protocol: packet->Protocol = ((packet->Protocol ^ packet->CryptSeed) ^ 0x8492) + 0x4ED8;
            packet->Protocol ^= packet->CryptSeed ^ 0xB128 ^ 0x8492;
            packet->PM_Host = (packet->PM_Host ^ packet->CryptSeed) + 0x40A4;
            packet->OSspecific = (packet->OSspecific ^ packet->CryptSeed) - 0x9D;
            packet->PortMask = (((packet->PortMask ^ packet->CryptSeed) ^ 0x8405) - 0x70CA) ^ 0x5131;
            // packet->PortFlags is not Encoded
            packet->EnvMask = ((packet->EnvMask ^ packet->CryptSeed) - 0xCB) ^ 0xF204;
            packet->EnvFlags = (((packet->EnvFlags ^ packet->CryptSeed) - 0xCF) ^ 0x5131) - 0x7007;
            packet->EEFlags = ((packet->EEFlags ^ ROL16(packet->CryptSeed, 5) ^ 0x85) - 0x32);
            // packet->Prot4Info is not Encoded
            packet->FuncOptions = ((packet->FuncOptions - (ROL16(packet->CryptSeed, 7) ^ 0xA3)) ^ 0x3F) - 0x77;
            packet->Slot_ID = ((packet->Slot_ID ^ packet->CryptSeed) ^ 0x15) - 0xF8;
            packet->Slot_ID_HIGH = ((packet->Slot_ID_HIGH ^ packet->CryptSeed) ^ 0x2215) - 0x56E8;
            // Missing packet->RUS_ExpDate
            // Missing packet->DataHigh
            packet->VendorKey = ((unsigned int)packet->VendorKey ^ packet->CryptSeed) - 0x13D;
            // Missing packet->VendorKeyHigh
            // Missing packet->OsSpecificHigh
            // This Might Be Incorrect
            packet->RUS_MaxInfo = (packet->RUS_MaxInfo ^ (ROL16(packet->CryptSeed, 5) - 0x0B)) - 0xC885;
            // This Might Be Incorrect
            packet->RUS_CurInfo = (packet->RUS_CurInfo ^ (ROR16(packet->CryptSeed, 6) - 0x0B)) - 0xC87B;
            packet->Tail[0] = ((packet->Tail[0] - (ROR16(packet->CryptSeed, 5) ^ 0x55)) ^ 0x20) + 0x17;
            packet->Tail[1] = (ROL16(packet->CryptSeed, 1) ^ packet->Tail[1] ^ 0x85) + 6;

            break;
        case 2:
            packet->API_Version_ID[0] = ((ROL16(packet->CryptSeed, 2) ^ packet->API_Version_ID[0]) + 0x7C) ^ 0x17;
            packet->API_Version_ID[1] = ((packet->CryptSeed ^ packet->API_Version_ID[1]) - 0x1F) ^ 0xA;
            packet->API_Options[0] = ((packet->API_Options[0] ^ 0xBB) - ROR16(packet->CryptSeed, 7)) ^ 0x5128;
            packet->API_Options[1] = (ROR16(packet->CryptSeed, 1) ^ packet->API_Options[1] ^ 0x8400) - 0x73;
            packet->ModID = (((ROL16(packet->CryptSeed, 4) ^ packet->ModID ^ 0x8436) - 0x0E) ^ 0x5131) - 0xA6;
            if (packet->ModID == HASP_DONGLE) {
                packet->Module.Hasp.PW1 = ((packet->Module.Hasp.PW1 ^ (packet->CryptSeed - 0x2F) ^ 0x35DB84D2) - 0x2800);
                packet->Module.Hasp.PW2 = ((packet->Module.Hasp.PW2 - ROR16(packet->CryptSeed, 3) + 0x3D) ^ 0x35131) - 0x47FB;
                packet->Module.Hasp.P1 = ((packet->Module.Hasp.P1 ^ (ROL16(packet->CryptSeed, 2) - 0x36)) + 0x3FB2);
                packet->Module2.Hasp2.P2 = (packet->Module2.Hasp2.P2 ^ (ROR16(packet->CryptSeed, 4) + 0x21)) - 0x15DF;
                packet->Module2.Hasp2.P3 = ((packet->Module2.Hasp2.P3 - packet->CryptSeed) ^ 0xD555) - 0x5155;
            }
            else {
                // Ignore the names, it's a union and these are what fits...
                packet->Module.Lt.LT_Reserved = (((ROR16(packet->CryptSeed, 3) - 0x12) ^ packet->Module.Lt.LT_Reserved) - 0x11) ^ 0x843C;
                packet->Module.Lt.Reg = ((packet->Module.Lt.Reg + 0x20 - packet->CryptSeed) ^ 0x5131) - 0x121;
                packet->Module.Lt.Value = ((packet->CryptSeed - 0x15) ^ packet->Module.Lt.Value) - 0x53;
                packet->Module.Lt.Password[0] = ((ROL16(packet->CryptSeed, 4) - 0x16) ^ packet->Module.Lt.Password[0]) - 0x148;
                packet->Module.Lt.Password[1] = ((ROL16(packet->CryptSeed, 4) - 0x16) ^ packet->Module.Lt.Password[1]) - 0x148;
            }
            packet->Data = ((packet->CryptSeed + 1) ^ (unsigned int)packet->Data) - 0x42;
            packet->Bcnt = (ROR16(packet->CryptSeed, 5) ^ packet->Bcnt ^ 0x1A) - 266;
            packet->Function = (((packet->Function - ROL16(packet->CryptSeed, 4)) ^ 0x84A2) - 0xC7) ^ 0x5131;
            packet->Status = (((packet->Status - ROR16(packet->CryptSeed, 3)) ^ 0x8470) - 0x85) ^ 0x5131;
            packet->Remote = (ROL16(packet->CryptSeed, 3) ^ packet->Remote ^ 0x8450) - 0x8A;
            // Something fun to note: packet->Port is not encrypted.
            packet->Speed = ((packet->Speed - (ROL16(packet->CryptSeed, 2) ^ 0x7A)) ^ 0x5176) - 0x47;
            packet->NetUsers = (packet->NetUsers ^ (ROL16(packet->CryptSeed, 2)) ^ 0xE250) - 0x4EC;

            for (i = 0; i < 8; i++) {
                packet->ID_Ref[i] = ((packet->ID_Ref[i] ^ packet->CryptSeed - 0x14) - 0x1F) & 0xFF;
                packet->ID_Verify[i] = (packet->ID_Verify[i] ^ (packet->CryptSeed - 0x34)) + 0x58;
            }


            packet->Task_ID = (packet->Task_ID ^ packet->CryptSeed ^ 0x18) - 0x1C2;
            packet->MaxUsers = (ROR16(packet->CryptSeed, 1) ^ packet->MaxUsers ^ 0x6677) - 0x33D7;
            packet->Timeout = (packet->Timeout ^ ROR16(packet->CryptSeed, 3) ^ 0xFDDCC) - 0xC2;
            packet->ShortLife = (packet->ShortLife ^ ROR16(packet->CryptSeed, 2) ^ 0x19) - 0xDF;
            packet->Application = (((packet->Application ^ packet->CryptSeed) - 0x66) ^ 0x5131) - 0x31DE;

            packet->Protocol = ((packet->Protocol ^ packet->CryptSeed) ^ 0x8486) - 0x3BEE;
            packet->PM_Host = (packet->PM_Host ^ packet->CryptSeed) + 0xCBD;
            packet->OSspecific = (packet->OSspecific ^ packet->CryptSeed) - 0xE3;
            packet->PortMask = (((packet->PortMask ^ packet->CryptSeed) ^ 0x8405) + 0x7F35) ^ 0x5131;
            // packet->PortFlags is not Encoded
            packet->EnvMask = ((packet->EnvMask ^ packet->CryptSeed) - 0x17) ^ 0xF107;
            packet->EnvFlags = (((packet->EnvFlags ^ packet->CryptSeed) - 0xD9) ^ 0x5131) - 0x7701;
            packet->EEFlags = (packet->EEFlags ^ ROL16(packet->CryptSeed, 2) ^ 0x51) - 0x4D;
            // packet->Prot4Info is not Encoded
            packet->FuncOptions = ((packet->FuncOptions - (ROL16(packet->CryptSeed, 7) ^ 0xCD)) ^ 0x38) + 0x25;
            packet->Slot_ID = ((packet->Slot_ID ^ packet->CryptSeed) ^ 0x25) - 0xFD;
            packet->Slot_ID_HIGH = ((packet->Slot_ID_HIGH ^ packet->CryptSeed) ^ 0x231F) - 0x6F54;
            // Missing packet->RUS_ExpDate
            // Missing packet->DataHigh
            packet->VendorKey = ((unsigned int)packet->VendorKey ^ packet->CryptSeed) - 0x13E;
            // Missing packet->VendorKeyHigh
            // Missing packet->OsSpecificHigh
            // This Might Be Incorrect
            packet->RUS_MaxInfo = (packet->RUS_MaxInfo ^ ROL16(packet->CryptSeed, 2) - 0x6F) - 0xA175;
            // This Might Be Incorrect
            packet->RUS_CurInfo = (packet->RUS_CurInfo ^ (ROR16(packet->CryptSeed, 1) - 0x0B)) - 0xC8D7;

            packet->Tail[0] = ((packet->Tail[0] - (ROR16(packet->CryptSeed, 1) ^ 0x5A)) ^ 0x7F) - 0x63;
            packet->Tail[1] = (packet->Tail[1] ^ ROL16(packet->CryptSeed, 4) ^ 0xF1) - 0x52;
            break;
        default:
            break;
    }
}

void EncryptParams(HL_API* packet) {
    int i;
    switch (packet->CryptVersion) {
        case 1:
            packet->API_Version_ID[0] = ((packet->API_Version_ID[0] ^ 0x14) - 0x7C) ^ ROL16(packet->CryptSeed, 3);
            packet->API_Version_ID[1] = ((packet->API_Version_ID[1] ^ (packet->CryptSeed & 0xFF)) - 0x20) ^ 0x1C;
            // (((packet->API_Options[0] ^ 0xAA) - ROR16(packet->CryptSeed, 7)) ^ 0x5131) - 0x19;
            packet->API_Options[0] = ((packet->API_Options[0] + 0x19) ^ 0x5131) + ROL16(packet->CryptSeed, 9) ^ 0xAA;
            packet->API_Options[1] = ((packet->API_Options[1] + 0x0F) ^ 0x8407) ^ ROR16(packet->CryptSeed, 1);
            if (packet->ModID == HASP_DONGLE) {
                // NOTE: I HAVE NEVER TESTED THESE - THEY MAY BE INCORRECT.
                packet->Module.Hasp.PW1 = ((packet->Module.Hasp.PW1 + 0x4D2) ^ 0x35DB8414) ^ (packet->CryptSeed - 0x0B);
                packet->Module.Hasp.PW2 = ((packet->Module.Hasp.PW2 + 0x4EAD) ^ 0x35131) - (ROR16(packet->CryptSeed, 1) + 0x0B);
                packet->Module.Hasp.P1 = (packet->Module.Hasp.P1 - 0x5EDD) ^ (ROL16(packet->CryptSeed, 7) - 0x0B);
                packet->Module2.Hasp2.P2 = (packet->Module2.Hasp2.P2 + 0x4E3) ^ (ROR16(packet->CryptSeed, 5) + 0x0B);
                packet->Module2.Hasp2.P3 = ((packet->Module2.Hasp2.P3 + 0x4D2) ^ 0xD557) - packet->CryptSeed;
            }
            else {
                // Ignore the names, it's a union and these are what fits...
                packet->Module.Lt.LT_Reserved = ((packet->Module.Lt.LT_Reserved ^ 0x84EF) + 0x11) ^ (ROR16(packet->CryptSeed, 4) - 0x0B);
                packet->Module.Lt.Reg = (packet->CryptSeed + ((packet->Module.Lt.Reg + 0xE7) ^ 0x5131) - 0x0B);
                packet->Module.Lt.Value = (packet->Module.Lt.Value + 0x9C) ^ (packet->CryptSeed - 0x0C);
                packet->Module.Lt.Password[0] = (packet->Module.Lt.Password[0] + 0x88) ^ (ROL16(packet->CryptSeed, 5) - 0x0C);
                packet->Module.Lt.Password[1] = (packet->Module.Lt.Password[1] + 0x88) ^ (ROL16(packet->CryptSeed, 5) - 0x0C);

            }

            packet->ModID = ((((packet->ModID + 0xE9) ^ 0x5131) + 0x11) ^ 0x8450) ^ ROL16(packet->CryptSeed, 6);
            packet->Data = ((packet->CryptSeed + 1) ^ ((unsigned int)packet->Data) + 0x39);
            packet->Bcnt = ((packet->Bcnt + 0xB8) ^ 0xF5) ^ ROR16(packet->CryptSeed, 3);
            packet->Function = (((packet->Function ^ 0x5131) + 0xE9) ^ 0x8414) + ROL16(packet->CryptSeed, 2);
            packet->Status = (((packet->Status ^ 0x5131) + 0xE9) ^ 0x8414) + ROR16(packet->CryptSeed, 5);
            packet->Remote = ROL16(packet->CryptSeed, 3) ^ (packet->Remote + 0xFA) ^ 0x8450;

            // packet->Port is not encoded
            packet->Speed = ((packet->Speed + 0xAD) ^ 0x5120) + (ROL16(packet->CryptSeed, 5) ^ 0x85);
            packet->NetUsers = ((packet->NetUsers + 0x0CC6)) ^ (ROL16(packet->CryptSeed, 6) ^ 0x380);


            for (i = 0; i < 8; i++) {
                packet->ID_Ref[i] = (packet->ID_Ref[i] + 0x80) ^ (packet->CryptSeed - 0x02);
                packet->ID_Verify[i] = (packet->ID_Verify[i] - 0x58) ^ (packet->CryptSeed - 0x34);
            }

            packet->Task_ID = (packet->Task_ID + 0x15E) ^ (packet->CryptSeed ^ 0x15);
            packet->MaxUsers = ((packet->MaxUsers + 0x33D6) ^ 0x5752) ^ ROR16(packet->CryptSeed, 3);
            packet->Timeout = ((packet->Timeout + 0x54) ^ 0xFDDE5) ^ ROR16(packet->CryptSeed, 2);
            packet->ShortLife = ((packet->ShortLife + 0xFA) ^ 0x15) ^ ROR16(packet->CryptSeed, 3);
            packet->Application = (((packet->Application + 0x7FFE) ^ 0x5131) + 0x65) ^ packet->CryptSeed;
            packet->Protocol = ((packet->Protocol - 0x4ED8) ^ 0x8492) ^ packet->CryptSeed;
            packet->PM_Host = (packet->PM_Host - 0x40A4) ^ packet->CryptSeed;
            packet->OSspecific = (packet->OSspecific + 0x9D) ^ packet->CryptSeed;
            packet->PortMask = (((packet->PortMask ^ 0x5131) + 0x70CA) ^ 0x8405) ^ packet->CryptSeed;
            // packet->PortFlags is not Encoded
            packet->EnvMask = ((packet->EnvMask ^ 0xF204) + 0xCB) ^ packet->CryptSeed;
            packet->EnvFlags = (((packet->EnvFlags + 0x7007) ^ 0x5131) + 0xCF) ^ packet->CryptSeed;
            packet->EEFlags = ((packet->EEFlags + 0x32) ^ 0x85) ^ ROL16(packet->CryptSeed, 5);
            // packet->Prot4Info is not Encoded
            packet->FuncOptions = ((packet->FuncOptions + 0x77) ^ 0x3F) + (ROL16(packet->CryptSeed, 7) ^ 0xA3);
            packet->Slot_ID = ((packet->Slot_ID + 0xF8) ^ 0x15) ^ packet->CryptSeed;
            packet->Slot_ID_HIGH = ((packet->Slot_ID_HIGH + 0x56E8) ^ 0x2215) ^ packet->CryptSeed;
            // Missing packet->RUS_ExpDate
            // Missing packet->DataHigh
            packet->VendorKey = ((unsigned int)packet->VendorKey + 0x13D) ^ packet->CryptSeed;
            // Missing packet->VendorKeyHigh
            // Missing packet->OsSpecificHigh
            // This Might Be Incorrect
            packet->RUS_MaxInfo = (packet->RUS_MaxInfo + 0xC885) ^ (ROL16(packet->CryptSeed, 5) - 0x0B);
            // This Might Be Incorrect
            packet->RUS_CurInfo = (packet->RUS_CurInfo + 0xC87B) ^ (ROR16(packet->CryptSeed, 6) - 0x0B);
            packet->Tail[0] = ((packet->Tail[0] - 0x17) ^ 0x20) + (ROR16(packet->CryptSeed, 5) ^ 0x55);
            packet->Tail[1] = ((packet->Tail[1] - 6) ^ 0x85) ^ ROL16(packet->CryptSeed, 1);
            break;
        case 2:
            packet->API_Version_ID[0] = ((packet->API_Version_ID[0] ^ 0x17) - 0x7C) ^ ROL16(packet->CryptSeed, 2);
            packet->API_Version_ID[1] = packet->CryptSeed ^ ((packet->API_Version_ID[1] ^ 0xA) + 0x1F);
            packet->API_Options[0] = (packet->API_Options[0] ^ 0x5128) + ROR16(packet->CryptSeed, 7) ^ 0xBB;
            packet->API_Options[1] = ROR16(packet->CryptSeed, 1) ^ (packet->API_Options[1] + 0x73) ^ 0x8400;


            if (packet->ModID == HASP_DONGLE) {
                // NOTE: I HAVE NEVER TESTED THESE - THEY MAY BE INCORRECT.
                packet->Module.Hasp.PW1 = ((packet->Module.Hasp.PW1 + 0x2800) ^ 0x35DB84D2) ^ (packet->CryptSeed - 0x2F);
                packet->Module.Hasp.PW2 = ((packet->Module.Hasp.PW2 + 0x47FB) ^ 0x35131) - (ROR16(packet->CryptSeed, 3) + 0x3D);
                packet->Module.Hasp.P1 = (packet->Module.Hasp.P1 - 0x3FB2) ^ (ROL16(packet->CryptSeed, 2) - 0x36);
                packet->Module2.Hasp2.P2 = (packet->Module2.Hasp2.P2 + 0x15DF) ^ (ROR16(packet->CryptSeed, 4) + 0x21);
                packet->Module2.Hasp2.P3 = ((packet->Module2.Hasp2.P3 + 0x5155) ^ 0xD555) - packet->CryptSeed;
            }
            else {
                // Ignore the names, it's a union and these are what fits...
                packet->Module.Lt.LT_Reserved = (ROR16(packet->CryptSeed, 3) - 0x12) ^ ((packet->Module.Lt.LT_Reserved ^ 0x843C) + 0x11);
                packet->Module.Lt.Reg = (((packet->Module.Lt.Reg + 0x121) ^ 0x5131) + packet->CryptSeed) - 0x20;
                packet->Module.Lt.Value = (packet->Module.Lt.Value + 0x53) ^ (packet->CryptSeed - 0x15);
                packet->Module.Lt.Password[0] = (packet->Module.Lt.Password[0] + 0x148) ^ ((ROL16(packet->CryptSeed, 4) - 0x16));
                packet->Module.Lt.Password[1] = (packet->Module.Lt.Password[1] + 0x148) ^ ((ROL16(packet->CryptSeed, 4) - 0x16));
            }

            packet->ModID = ROL16(packet->CryptSeed, 4) ^ (((packet->ModID + 0xA6) ^ 0x5131) + 0x0E) ^ 0x8436;

            packet->Data = ((packet->CryptSeed + 1) ^ (unsigned int)packet->Data + 0x42);
            packet->Bcnt = ((packet->Bcnt + 0x10A) ^ 0x1A) ^ ROR16(packet->CryptSeed, 5);
            packet->Function = (((packet->Function ^ 0x5131) + 0xC7) ^ 0x84A2) + ROL16(packet->CryptSeed, 4);
            packet->Status = (((packet->Status ^ 0x5131) + 0x85) ^ 0x8470) + ROR16(packet->CryptSeed, 3);
            packet->Remote = ((packet->Remote + 0x8A) ^ 0x8450) ^ ROL16(packet->CryptSeed, 3);
            // Something fun to note: packet->Port is not encrypted.
            packet->Speed = ((packet->Speed + 0x47) ^ 0x5176) + (ROL16(packet->CryptSeed, 2) ^ 0x7A);
            packet->NetUsers = (packet->NetUsers + 0x4EC) ^ 0xE250 ^ ROL16(packet->CryptSeed, 2);

            for (i = 0; i < 8; i++) {
                packet->ID_Ref[i] = (packet->ID_Ref[i] + 0x1F) ^ (packet->CryptSeed - 0x14);
                packet->ID_Verify[i] = (packet->ID_Verify[i] - 0x58) ^ (packet->CryptSeed - 0x34);
            }

            packet->Task_ID = (packet->Task_ID + 0x1C2) ^ (0x18 ^ packet->CryptSeed);
            packet->MaxUsers = ((packet->MaxUsers + 0x33D7) ^ 0x6677) ^ ROR16(packet->CryptSeed, 1);
            packet->Timeout = (packet->Timeout + 0xC2) ^ 0xFDDCC ^ ROR16(packet->CryptSeed, 3);
            packet->ShortLife = (packet->ShortLife + 0xDF) ^ 0x19 ^ ROR16(packet->CryptSeed, 2);
            packet->Application = (((packet->Application + 0x31DE) ^ 0x5131) + 0x66) ^ packet->CryptSeed;
            packet->Protocol = ((packet->Protocol + 0x3BEE) ^ 0x8486) ^ packet->CryptSeed;
            packet->PM_Host = (packet->PM_Host - 0xCBD) ^ packet->CryptSeed;
            packet->OSspecific = (packet->OSspecific + 0xE3) ^ packet->CryptSeed;
            packet->PortMask = ((((packet->PortMask ^ 0x5131) - 0x7F35) ^ 0x8405) ^ packet->CryptSeed);
            // packet->PortFlags is not Encoded
            packet->EnvMask = ((packet->EnvMask ^ 0xF107) + 0x17) ^ packet->CryptSeed;
            packet->EnvFlags = (((packet->EnvFlags + 0x7701) ^ 0x5131) + 0xD9) ^ packet->CryptSeed;
            packet->EEFlags = ((packet->EEFlags + 0x4D) ^ 0x51) ^ ROL16(packet->CryptSeed, 2);
            // packet->Prot4Info is not Encoded
            packet->FuncOptions = ((packet->FuncOptions - 0x25) ^ 0x38) + (ROL16(packet->CryptSeed, 7) ^ 0xCD);
            packet->Slot_ID = ((packet->Slot_ID + 0xFD) ^ 0x25) ^ packet->CryptSeed;
            packet->Slot_ID_HIGH = ((packet->Slot_ID_HIGH + 0x6F54) ^ 0x231F) ^ packet->CryptSeed;
            // Missing packet->RUS_ExpDate
            // Missing packet->DataHigh
            packet->VendorKey = ((unsigned int)packet->VendorKey + 0x13E) ^ packet->CryptSeed;
            // Missing packet->VendorKeyHigh
            // Missing packet->OsSpecificHigh
            // This Might Be Incorrect
            packet->RUS_MaxInfo = (packet->RUS_MaxInfo + 0xA175) ^ (ROL16(packet->CryptSeed, 2) - 0x6F);
            // This Might Be Incorrect
            packet->RUS_CurInfo = (packet->RUS_CurInfo + 0xC8D7) ^ (ROR16(packet->CryptSeed, 1) - 0x0B);
            packet->Tail[0] = ((packet->Tail[0] + 0x63) ^ 0x7F) + (ROR16(packet->CryptSeed, 1) ^ 0x5A);
            packet->Tail[1] = (packet->Tail[1] + 0x52) ^ (ROL16(packet->CryptSeed, 4) ^ 0xF1);
            break;
        default:
            break;
    }
}

void EncryptPacket(HL_API* packet) {
    int i;
    unsigned char* raw_data = (unsigned char*)packet;

    if (packet->CryptVersion) {
        for (i = 0; i < 0xB9; ++i) {
            unsigned short w = *(unsigned short*)(raw_data + i);
            w = (w + packet->CryptSeed) & 0xFFFF;
            *(unsigned short*)(raw_data + i) = w;
            packet->CryptSeed = (ROR16(packet->CryptSeed, 15) - (w ^ i)) & 0xFFFF;
        }
        for (i = 0xBE; i < 0xFF; ++i) {
            unsigned short w = *(unsigned short*)(raw_data + i);
            w = (w - packet->CryptSeed) & 0xFFFF;
            *(unsigned short*)(raw_data + i) = w;
            packet->CryptSeed = (ROR16(packet->CryptSeed, 15) + (w ^ i)) & 0xFFFF;
        }
    }
}

void DecryptPacket(HL_API* packet) {
    int i;
    unsigned short sd;
    unsigned char* raw_data = (unsigned char*)packet;
    if (packet->CryptVersion) {
        for (i = 0xFE; i > 0xBD; i--) {
            sd = *(unsigned short*)(raw_data + i);
            packet->CryptSeed = ROR16(packet->CryptSeed - (sd ^ i), 1);
            *(unsigned short*)(raw_data + i) = sd + packet->CryptSeed;
        }
        for (i = 0xB8; i >= 0; i--) {
            sd = *(unsigned short*)(raw_data + i);
            packet->CryptSeed = ROR16(packet->CryptSeed + (sd ^ i), 1);
            *(unsigned short*)(raw_data + i) = sd - packet->CryptSeed;
        }
    }
}

