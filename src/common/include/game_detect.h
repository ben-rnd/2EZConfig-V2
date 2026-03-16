#pragma once

#include "md5.h"
#include "game_md5.h"
#include <string>
#include <cstring>

#ifdef _WIN32
#include <windows.h>

struct DetectedGame {
    std::string id;
    bool isDancer = false;
};

inline bool computeFileMD5(const std::string& filePath, uint8_t digest[MD5_DIGEST_LENGTH]) {
    HANDLE hFile = CreateFileA(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    MD5_CTX ctx;
    MD5_Init(&ctx);

    uint8_t buffer[8192];
    DWORD bytesRead = 0;
    while (ReadFile(hFile, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
        MD5_Update(&ctx, buffer, bytesRead);
    }
    CloseHandle(hFile);

    MD5_Final(digest, &ctx);
    return true;
}

inline DetectedGame detectGameFromMD5(const uint8_t digest[MD5_DIGEST_LENGTH]) {
    DetectedGame result;
    static const unsigned char zeroes[MD5_DIGEST_LENGTH] = {};

    for (const auto& entry : djGameMD5s) {
        if (memcmp(entry.md5, zeroes, MD5_DIGEST_LENGTH) == 0) {
            continue;
        }
        if (memcmp(digest, entry.md5, MD5_DIGEST_LENGTH) == 0) {
            result.id = entry.id;
            return result;
        }
    }

    for (const auto& entry : dancerGameMD5s) {
        if (memcmp(entry.md5, zeroes, MD5_DIGEST_LENGTH) == 0) {
            continue;
        }
        if (memcmp(digest, entry.md5, MD5_DIGEST_LENGTH) == 0) {
            result.id = entry.id;
            result.isDancer = true;
            return result;
        }
    }

    return result;
}

inline DetectedGame detectGameFromFile(const std::string& filePath) {
    uint8_t digest[MD5_DIGEST_LENGTH];
    if (!computeFileMD5(filePath, digest)) {
        return {};
    }
    return detectGameFromMD5(digest);
}

inline DetectedGame detectCurrentGame() {
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    return detectGameFromFile(exePath);
}

#endif
