#pragma once

namespace Injector {
    int LaunchAndInject(const char* exeName);
    int InjectRunningProcess(const char* processName);
}
