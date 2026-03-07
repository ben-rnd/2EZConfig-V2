#include <windows.h>
#include <stdio.h>
#include "kitchen_sink.h"

void print_hex(unsigned char* data, unsigned int len) {
	for (unsigned int i = 0; i < len; i++) {
		printf("%02X", data[i]);
	}
	printf("\n");
}

int GetDirectoryPath(char* lpFilename, unsigned long nSize) {
	GetModuleFileNameA(GetModuleHandleA(0), lpFilename, nSize);
	char* last_delimiter = strrchr(lpFilename, 0x5C);
	if (!last_delimiter) { return FALSE; }
	memset(last_delimiter + 1, 0x00, 1);
	return TRUE;
}

void ResolveFullCurrentPath(char* file_name, char* out_path) {
	if (!GetDirectoryPath(out_path, 1024)) { return; }
	strcat(out_path, file_name);
}

unsigned char HexChar(char c) {
	if ('0' <= c && c <= '9') return (unsigned char)(c - '0');
	if ('A' <= c && c <= 'F') return (unsigned char)(c - 'A' + 10);
	if ('a' <= c && c <= 'f') return (unsigned char)(c - 'a' + 10);
	return 0xFF;
}

int HexToBin(const char* s, unsigned char* buff, int length) {
	int result;
	if (!s || !buff || length <= 0) return -1;

	for (result = 0; *s; ++result)
	{
		unsigned char msn = HexChar(*s++);
		if (msn == 0xFF) return -1;
		unsigned char lsn = HexChar(*s++);
		if (lsn == 0xFF) return -1;
		unsigned char bin = (msn << 4) + lsn;

		if (length-- <= 0) return -1;
		*buff++ = bin;
	}
	return result;
}

void BinToHex(const unsigned char* buff, int length, char* output, int outLength) {
	char binHex[] = "0123456789ABCDEF";

	if (!output || outLength < 4) return;
	*output = '\0';

	if (!buff || length <= 0 || outLength <= 2 * length)
	{
		memcpy(output, "ERR", 4);
		return;
	}

	for (; length > 0; --length, outLength -= 2)
	{
		unsigned char byte = *buff++;

		*output++ = binHex[(byte >> 4) & 0x0F];
		*output++ = binHex[byte & 0x0F];
	}
	if (outLength-- <= 0) return;
	*output++ = '\0';
}

void AsciiToWide(unsigned char* in, unsigned int in_length, unsigned char* out, unsigned int start_offset) {
	for (unsigned int i = 0, j = start_offset; i < in_length; i++, j += 2) {
		memcpy(out + j, in + i, 1);
	}
}
