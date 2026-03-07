#pragma once

#ifndef WORD
#define WORD unsigned short
#endif

#ifndef BYTE
#define BYTE unsigned char
#endif

#ifndef DWORD
#define DWORD unsigned int
#endif

typedef struct _HL_CODE_STRUCT {
    WORD gVar1;
    WORD gVar2;
    WORD gVar4;
    WORD tmpStatus;
    BYTE gPtrArray[16];
    BYTE gSeedArray[4];
} HL_CODE_STRUCT, * PHL_CODE_STRUCT;

typedef struct _HASPTIMEMEMORY
{
    BYTE curr_time[3]; // time, BCD, SS:MM:HH
    BYTE curr_date[4]; // date, BCD, DD:MM:WW:YY (WW - weekday ?)
    BYTE rezerv1[0x19]; // ???
    BYTE Id[8]; // Id of TIME HASP (4 bytes, but real is - 8?)
    BYTE FAS[0x10]; // FAS ? There is some infos about progs - ? ExpiryDate's, etc - ?
} tHASPTIMEMEMORY;

typedef struct {
    BYTE    columnMask;
    BYTE    cryptInitVect;
    BYTE    secTable[8];
    BYTE    isInvSecTab;
    DWORD   prepNotMask;
    DWORD   curLFSRState;
    BYTE    first5bit;
    DWORD   password;
} KEY_INFO;

typedef struct _KEY_DATA {
    //
    // Dongle type
    //
    BYTE   DongleType;     // Type of Dongle (1-HASP,2-HARDLOCK,3-SENTINEL)
    //
    // Current key state
    //
    BYTE   isInitDone;     // Is chiperkeys given to key
    BYTE   isKeyOpened;    // Is valid password is given to key
    BYTE   encodedStatus;  // Last encoded status
    BYTE	srm_f2x_param[8]; // AF-2F
    WORD  chiperKey1,     // Keys for chiper
    chiperKey2;

    //
    //Static information about HARDLOCK key
    //
    //UCHAR   HdkkeyType;   // Old or New Hardlock (0-old,1-new) NOT USED
    DWORD   HdkID;          // ID
    BYTE   HdkHasMem;      //
    BYTE   HdkMem[0x80];   // Memory content (0x60 ROM + 0x20 RAM)
    WORD  HdkSeed1,       // Seeds for crypting
    HdkSeed2,
            HdkSeed3;

    BYTE   HdkTempMem[8];    // Used for hl_code & hl_crypt calculation

    //Static information about SENTINEL key

    BYTE   SentkeyType;    // Sentinel Type (0-SuperPro,1-UltraPro,2... other types)
    WORD  CellMem[256];    // Memory content
    BYTE   CellType[256];   // Cells type
    WORD  ExtraCell[32];   //used for crypt algo 8 sets*4 bytes
    BYTE   ExtraOfs;       //offset in ExtraCell array
    BYTE   request;
    BYTE   SentTempMem[80];
    DWORD   SentBufLen;

    //
    // Static information about HASP key
    //
    BYTE   keyType;        // Type of key
    BYTE   memoryType;     // Memory size of key
    DWORD   password;       // Password for key
    BYTE   options[14];    // Options for key
    BYTE   secTable[8];    // ST for key
    BYTE   netMemory[16];  // NetMemory for key
    tHASPTIMEMEMORY HASPTIMEMEMORY;
    long long TimeShift;     // Time shift (relativly OS time)
    BYTE   memory[0x1000]; // Memory content
    KEY_INFO KEY_INFOdata;  // columnMask & prepNotMask
    int QisFound;
    long QA_Buff_Len;
    BYTE EncDecType;
    BYTE EncDecValue;
    BYTE lastQA_buff[48];
    WORD d_name[20];
    BYTE AesKey[16];       // AES key for encrypt-decrypt
} KEY_DATA, * PKEYDATA;

void __fastcall HL_CRYPT(PKEYDATA pKeyData, BYTE* ResponseData);
void __fastcall HL_CODE(PKEYDATA pKeyData, BYTE* ResponseData, unsigned int num_blocks);
unsigned char __fastcall HL_CALC(PKEYDATA pkeyData, unsigned short p1, unsigned short p2);
