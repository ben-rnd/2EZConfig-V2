#pragma once

#include <windows.h>
#include <shlobj.h>
#include <string>

inline std::string getAppDataDir() {
    char pathBuffer[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, pathBuffer))) {
        return std::string(pathBuffer) + "\\2ezconfig";
    }
    return "";
}
