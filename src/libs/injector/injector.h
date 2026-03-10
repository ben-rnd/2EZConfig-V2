#pragma once
#include <vector>
#include <string>

namespace Injector {
    int LaunchAndInject(const char* exeName, const std::vector<std::string>& extraDlls = {});
    int InjectRunningProcess(const char* processName);
}
