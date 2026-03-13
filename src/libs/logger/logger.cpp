#include "logger.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <set>

static bool s_enabled = false;
static LogLevel s_minLevel = LogLevel::INFO;
static bool s_console = false;
static TimestampMode s_tsMode = TimestampMode::Elapsed;
static std::ofstream s_file;
static std::atomic_flag s_lock = ATOMIC_FLAG_INIT;
static std::set<std::string> s_seenOnce;
static std::chrono::steady_clock::time_point s_startTime;

static void acquireLock() { while (s_lock.test_and_set(std::memory_order_acquire)) {} }
static void releaseLock() { s_lock.clear(std::memory_order_release); }

static std::string formatElapsed() {
    using namespace std::chrono;
    auto ms = duration_cast<milliseconds>(steady_clock::now() - s_startTime).count();
    long long h   = ms / 3600000; ms %= 3600000;
    long long m   = ms / 60000;   ms %= 60000;
    long long s   = ms / 1000;    ms %= 1000;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld.%03lld", h, m, s, ms);
    return buf;
}

static std::string formatWallClock() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    std::tm* tm_info = std::localtime(&t);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03lld",
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, (long long)ms);
    return buf;
}

static const char* levelStr(LogLevel level) {
    switch (level) {
        case LogLevel::INFO: return "INFO ";
        case LogLevel::WARN: return "WARN ";
        case LogLevel::ERR:  return "ERR  ";
    }
    return "INFO ";
}

static void log(LogLevel level, const std::string& msg) {
    if (!s_enabled || level < s_minLevel) {
        return;
    }

    std::string ts = (s_tsMode == TimestampMode::Elapsed) ? formatElapsed() : formatWallClock();
    std::string line = std::string("[") + levelStr(level) + "][" + ts + "] " + msg + "\n";

    acquireLock();
    if (s_file.is_open()) {
        s_file << line;
        s_file.flush();
    }
    if (s_console) {
        fputs(line.c_str(), stdout);
    }
    releaseLock();
}

void Logger::init(
    const std::string& directory,
    bool               enabled,
    const std::string& filename,
    LogLevel           minLevel,
    bool               console,
    TimestampMode      timestamp)
{
    s_startTime = std::chrono::steady_clock::now();
    s_enabled   = enabled;
    s_minLevel  = minLevel;
    s_console   = console;
    s_tsMode    = timestamp;

    if (enabled) {
        std::string path = directory + "/" + filename;
        s_file.open(path, std::ios::out | std::ios::trunc);
        log(LogLevel::INFO, "Logger initialised");
    }
}

bool Logger::isEnabled() { return s_enabled; }

void Logger::info (const std::string& msg) { log(LogLevel::INFO, msg); }
void Logger::warn (const std::string& msg) { log(LogLevel::WARN, msg); }
void Logger::error(const std::string& msg) { log(LogLevel::ERR,  msg); }

void Logger::warnOnce(const std::string& msg) {
    acquireLock();
    bool inserted = s_seenOnce.insert(msg).second;
    releaseLock();
    if (inserted) {
        log(LogLevel::WARN, msg);
    }
}
