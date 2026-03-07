// HardLock Functionality (Mostly Ripped Off from the MultiKey Emulator)
#include <string.h>
#include "io.hardlock.internal.h"

#ifndef ROL8
#define ROL8(n, r)  (((unsigned char)(n) << (r)) | ((unsigned char)(n) >> (8 - (r)))) /* only works for uint8_t */
#endif

#ifndef HIBYTE
#define HIBYTE(w) (unsigned char)((w & 0xFF00u) >> 8)
#endif

void InitgSeeds(PKEYDATA pKeyData, PHL_CODE_STRUCT TempData) {
    TempData->gSeedArray[0] = (pKeyData->HdkSeed3 & 0xF000) >> 12;
    TempData->gSeedArray[1] = (pKeyData->HdkSeed3 & 0xF00) >> 8;
    TempData->gSeedArray[2] = (pKeyData->HdkSeed3 & 0xF0) >> 4;
    TempData->gSeedArray[3] = pKeyData->HdkSeed3 & 0xF;

    int i;
    for (i = 0; i < 16; i++) {
        TempData->gPtrArray[i] = ((pKeyData->HdkSeed1 >> i) & 1) << 1;
        TempData->gPtrArray[i] |= (pKeyData->HdkSeed2 >> i) & 1;
    }
}

void InitgVar12(PHL_CODE_STRUCT TempData) {
    TempData->gVar1 = 0xf;
    TempData->gVar2 = 0x0;
}

void SetDongleData(BYTE Data, PHL_CODE_STRUCT TempData, PKEYDATA pKeyData) {
    if ((pKeyData->password & 0x1F0000) == 0x1F0000)
    {
        TempData->gVar1 ^= (Data & 0xF) ^ TempData->gSeedArray[TempData->gPtrArray[TempData->gVar1]]; //old
    }
    else
    {
        Data &= 0xF;
        BYTE tmpPtr = TempData->gPtrArray[TempData->gVar1];
        TempData->gVar4 = (TempData->gVar4 << 4) | tmpPtr;
        TempData->gVar1 ^= Data;
        TempData->gVar1 ^= TempData->gVar2;
        TempData->gVar1 ^= TempData->gSeedArray[tmpPtr];
        TempData->gVar2 = (((TempData->gVar2 << 2) + tmpPtr) >> 1) & 0xF;
    }
}

BYTE GetBitFromDongleData(PHL_CODE_STRUCT TempData, PKEYDATA pKeyData) {
    if ((pKeyData->password & 0x1F0000) == 0x1F0000)
    {
        return (TempData->gPtrArray[TempData->gVar1] & 1); //old
    }
    else
    {

        return (HIBYTE(TempData->gVar4) >> 4u) & 1u;
    }
}

BYTE CipherFunction(DWORD* R, DWORD* tmpR, PHL_CODE_STRUCT TempData, PKEYDATA pKeyData) {
    BYTE* Data = (BYTE*)tmpR;

    BYTE SumOfBitFromDongle = 0;
    int OuterLoopCounter;
    int InnerLoopCounter;
    BYTE BitFromDongle;

    BitFromDongle = 1;
    OuterLoopCounter = 9;
    InitgVar12(TempData);
    while (OuterLoopCounter > 0) {
        InnerLoopCounter = 0;

        BYTE tmp1;
        BYTE tmp2;
        BYTE tmp3;
        while (InnerLoopCounter < 4) {
            tmp1 = Data[InnerLoopCounter];
            tmp1 = ROL8(tmp1, 1);

            SetDongleData(Data[InnerLoopCounter], TempData, pKeyData);
            tmp2 = BitFromDongle + InnerLoopCounter + 1;
            tmp2 &= 3;

            tmp3 = Data[tmp2] + tmp2 + tmp1;
            tmp3 = ROL8(tmp3, 1);

            Data[InnerLoopCounter] = tmp3;

            InnerLoopCounter++;
        }

        BitFromDongle = GetBitFromDongleData(TempData, pKeyData);
        SumOfBitFromDongle += BitFromDongle;

        OuterLoopCounter--;
    }


    *R ^= *tmpR;
    return SumOfBitFromDongle;
}

void __fastcall HL_CODE(PKEYDATA pKeyData, BYTE* ResponseData, unsigned int num_blocks) { // 0x38 byte data that actual data is in 0x28

    HL_CODE_STRUCT TempData, * pTempData;
    pTempData = &TempData;

    InitgSeeds(pKeyData, pTempData);

    BYTE Data[0x50];
    //copy data to crypt
    memset(Data,0x00,0x38);
    memcpy(Data+0x28,pKeyData->HdkTempMem,8);


    BYTE* tmpData = Data;
    WORD* DongleBitCounter = (WORD*)(Data + 0x1C);
    int LoopCounter = 5;


    DWORD* L = (DWORD*)&Data[0x28];
    DWORD* R = (DWORD*)&Data[0x2C];
    DWORD* tmpR1 = (DWORD*)&Data[0x20];
    DWORD* tmpR2 = (DWORD*)&Data[0x24];

    while (LoopCounter >= 0) {
        *tmpR1 = *R;
        *tmpR2 = *R;

        *DongleBitCounter += CipherFunction(R, tmpR2, pTempData, pKeyData);
        *R ^= *L;

        *L = *tmpR1;

        tmpData += 4;
        *(DWORD*)(tmpData - 4) = *tmpR2;

        LoopCounter--;
    }

    if (num_blocks != 7) {
        memcpy(ResponseData + 0x00, Data + 0x04, 4); //copy back crypted data
        memcpy(ResponseData + 0x04, Data + 0x00, 4); //copy back crypted data
        memcpy(ResponseData + 0x08, Data + 0x0C, 4); //copy back crypted data
        memcpy(ResponseData + 0x0C, Data + 0x08, 4); //copy back crypted data
        memcpy(ResponseData + 0x10, Data + 0x14, 4); //copy back crypted data
        memcpy(ResponseData + 0x14, Data + 0x10, 4); //copy back crypted data
        memcpy(ResponseData + 0x18, Data + 0x1C, 2); //copy back crypted data
    }
    else {
        memcpy(ResponseData + 0x00, Data + 0x00, 4); //copy back crypted data
        memcpy(ResponseData + 0x04, Data + 0x04, 4); //copy back crypted data
        memcpy(ResponseData + 0x08, Data + 0x08, 4); //copy back crypted data
        memcpy(ResponseData + 0x0C, Data + 0x0C, 4); //copy back crypted data
        memcpy(ResponseData + 0x10, Data + 0x10, 4); //copy back crypted data
        memcpy(ResponseData + 0x14, Data + 0x14, 4); //copy back crypted data
        memcpy(ResponseData + 0x18, Data + 0x18, 4); //copy back crypted data

        memcpy(ResponseData + 0x1C, Data + 0x1C, 4); //copy back crypted data
        memcpy(ResponseData + 0x20, Data + 0x20, 4); //copy back crypted data
        memcpy(ResponseData + 0x22, Data + 0x22, 4); //copy back crypted data
        memcpy(ResponseData + 0x24, Data + 0x24, 4); //copy back crypted data
        memcpy(ResponseData + 0x28, Data + 0x28, 4); //copy back crypted data
        memcpy(ResponseData + 0x2C, Data + 0x2C, 4); //copy back crypted data
        memcpy(ResponseData + 0x30, Data + 0x30, 4); //copy back crypted data
    }
}

WORD Transform0_HW(WORD W0, WORD retW, PHL_CODE_STRUCT TempData, PKEYDATA pKeyData) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            SetDongleData((W0 & 0xFF) >> 2, TempData, pKeyData);
            if (W0 & 0x8000) {
                W0 <<= 1; W0++;
            }
            else {
                W0 <<= 1;
            }
        }
        retW >>= 1;
        DWORD nb = (TempData->gVar4 >> 12) & 1;
        if (nb == 0) {
            //tmpStatus++;
            retW |= 0x8000;
        }
    }

    return retW;
}

WORD transform0(WORD WORD3, WORD WORD4, PHL_CODE_STRUCT TempData, PKEYDATA pKeyData) {
    InitgVar12(TempData);
    WORD ax = WORD4 ^ Transform0_HW(WORD4, Transform0_HW(WORD3, 0, TempData, pKeyData), TempData, pKeyData);
    ax = (ax >> 15) + (ax << 1);
    ((BYTE*)&ax)[0] = (((BYTE*)&ax)[0] + ((BYTE*)&ax)[1]) & 0xFF;
    return ax;
}

void __fastcall HL_CRYPT(PKEYDATA pKeyData, BYTE* ResponseData) {  // 0x8 byte data that actual data is in 0x0

    WORD Word1, Word2, Word3, Word4;
    //BYTE i=0;
    //tmpStatus = 0;
    BYTE* Data = ResponseData;

    HL_CODE_STRUCT TempData, * pTempData;
    pTempData = &TempData;

    InitgSeeds(pKeyData, pTempData);

    Word1 = ((WORD*)Data)[0];
    Word2 = ((WORD*)Data)[1];
    Word3 = ((WORD*)Data)[2];
    Word4 = ((WORD*)Data)[3];
    for (int i = 0; i < 5; i++) {
        WORD transf = Word3 ^ Word4 ^ transform0(Word3, Word4, pTempData, pKeyData);
        BYTE tmp1 = ((BYTE*)&transf)[1] + ((BYTE*)&transf)[0];
        tmp1 = tmp1 * 2 + (tmp1 >> 7);
        tmp1++;
        tmp1 = tmp1 * 2 + (tmp1 >> 7);
        BYTE tmp2 = ((BYTE*)&transf)[0] + tmp1;
        tmp2 = tmp2 * 2 + (tmp2 >> 7);
        tmp2 = tmp2 * 2 + (tmp2 >> 7);
        transf = (tmp1 << 8) + tmp2;
        WORD _Word4 = transf + Word4;
        _Word4 = ((_Word4 + 1) & 0xFF) + (_Word4 & 0xFF00);
        _Word4 = (_Word4 >> 15) + (_Word4 * 2);
        _Word4 = (_Word4 >> 15) + (_Word4 * 2);
        transf ^= Word1;
        _Word4 ^= Word2;
        Word1 = Word3;
        Word2 = Word4;
        Word3 ^= transf;
        Word4 ^= _Word4;
    }
    ((WORD*)Data)[0] = Word3;
    ((WORD*)Data)[1] = Word4;
    ((WORD*)Data)[2] = Word1;
    ((WORD*)Data)[3] = Word2;
    //      if ((tmpStatus<=0x26)&&(tmpStatus>=2))
    //              return 1;
    //      else
    //              return 0;
}


unsigned char __fastcall HL_CALC(PKEYDATA pkeyData, unsigned short p1, unsigned short p2){
    unsigned int Input;
    unsigned short i, Var1, Var2, Var4, tmpPtr;
    BYTE  k, i1, i2, i3, i4, PtrArray[16], SeedArray[4], Output;

    if ((pkeyData->password&0x1F0000)==0x1F0000)   //old key
    {

        SeedArray[0] = (pkeyData->HdkSeed3 & 0xF000) >> 12;
        SeedArray[1] = (pkeyData->HdkSeed3 & 0xF00) >> 8;
        SeedArray[2] = (pkeyData->HdkSeed3 & 0xF0) >> 4;
        SeedArray[3] = pkeyData->HdkSeed3 & 0xF;

        for (i=0; i<16; i++)
        {
            PtrArray[i]  = ((pkeyData->HdkSeed1 >> i) & 1) << 1;
            PtrArray[i] |= (pkeyData->HdkSeed2 >> i) & 1;
        }

        Output = 0;


        i1 = (p2 >> 8) & 0xFF;
        i2 = p2 & 0xFF;
        i3 = (p1 >> 8) & 0xFF;
        i4 = p1 & 0xFF;
        i1 = ROL8(i1, 4);
        i2 = ROL8(i2, 4);
        i3 = ROL8(i3, 4);
        i4 = ROL8(i4, 4);

        Input = ((unsigned int)i1 << 24) | ((unsigned int)i2 << 16) | ((unsigned int)i3 << 8) | (unsigned int)i4;

        //    KdPrint(("   input to HL_CALC = 0x%8.8X\n", Input));

        Var1 = 0xf;
        Var2 = 0x0;


        for(i=0; i<8; i++)
        {

            tmpPtr = PtrArray[Var1];

            Var1 ^= Input & 0x0F;
            Var1 ^= SeedArray[tmpPtr];

            Var2 = PtrArray[Var1];
            Var4 = tmpPtr | (Var4 << 4);

            Output <<= 1;
            Output |= PtrArray[Var1] & 1;
            Input >>= 4;
        }
        k=Output;
        Output=~(((k<<7)&0x80)|((k<<5)&0x40)|((k<<3)&0x20)|((k<<1)&0x10)|((k>>7)&0x01)|((k>>5)&0x02)|((k>>3)&0x04)|((k>>1)&0x08));
    }
    else   { Output=0xFF; }

    return Output;
}