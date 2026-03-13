#pragma once
#include <string>

enum class LogLevel     { INFO, WARN, ERR };
enum class TimestampMode { Elapsed, WallClock };

class Logger {
public:
    // directory    — where to write the log file
    // enabled      — master on/off switch
    // filename     — log file name (default: "app.log")
    // minLevel     — suppress messages below this level (default: INFO = log all)
    // console      — also write to stdout (default: false)
    // timestamp    — Elapsed (time since init) or WallClock (default: Elapsed)
    static void init(
        const std::string& directory,
        bool               enabled,
        const std::string& filename   = "app.log",
        LogLevel           minLevel   = LogLevel::INFO,
        bool               console    = false,
        TimestampMode      timestamp  = TimestampMode::Elapsed
    );

    static bool isEnabled();

    static void info (const std::string& msg);
    static void warn (const std::string& msg);
    static void warnOnce(const std::string& msg);
    static void error(const std::string& msg);
};
