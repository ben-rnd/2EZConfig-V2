#pragma once
#include <processthreadsapi.h>
// DBG
//Uncomment this to turn on debug printing
#define DEBUG_MODE
void DBG_print_buffer(unsigned char* data, size_t size);
void DBG_printfA(const char* fmt, ...);
void DBG_printfW(const wchar_t* format, ...);

// Utils
int GetDirectoryPath(char* lpFilename, unsigned long nSize);
void ResolveFullCurrentPath(char* file_name, char* out_path);
int HexToBin(const char* s, unsigned char* buff, int length);
void BinToHex(const unsigned char* buff, int length, char* output, int outLength);
void AsciiToWide(unsigned char* in, unsigned int in_length, unsigned char* out, unsigned int start_offset);
void print_hex(unsigned char* data, unsigned int len);

// ProcessInject
BOOL InjectProcessCtxSwitch(LPPROCESS_INFORMATION lpProcessInformation, char* path_to_dll, int jiggle_handle, int leave_suspended);

// Misc
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
