#include "patch_store.h"
#include "logger.h"
#include "utilities.h"

#include <windows.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static SIZE_T getImageSize(HMODULE base) {
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS*>(
                    reinterpret_cast<const uint8_t*>(base) + dos->e_lfanew);
    return nt->OptionalHeader.SizeOfImage;
}

static std::vector<uint8_t> parseBytes(const std::string& hexStr) {
    std::vector<uint8_t> result;
    std::istringstream ss(hexStr);
    std::string token;
    while (ss >> token) {
        result.push_back(static_cast<uint8_t>(std::stoul(token, nullptr, 16)));
    }
    return result;
}

static uint32_t parseHexOffset(const std::string& hexStr) {
    return static_cast<uint32_t>(std::stoul(hexStr, nullptr, 16));
}

static std::vector<int16_t> parseScan(const std::string& hexStr) {
    std::vector<int16_t> result;
    std::istringstream ss(hexStr);
    std::string token;
    while (ss >> token) {
        if (token == "??")
            result.push_back(-1);
        else
            result.push_back(static_cast<int16_t>(std::stoul(token, nullptr, 16)));
    }
    return result;
}

// Returns pointer to pattern match, or module base if scan is empty, or nullptr if not found.
static uint8_t* scanForPattern(const std::vector<int16_t>& scan) {
    HMODULE base = GetModuleHandle(NULL);
    if (scan.empty())
        return reinterpret_cast<uint8_t*>(base);

    const uint8_t* imageStart = reinterpret_cast<const uint8_t*>(base);
    const SIZE_T   imageSize  = getImageSize(base);
    const size_t   scanLen    = scan.size();

    for (size_t i = 0; i + scanLen <= imageSize; ++i) {
        bool match = true;
        for (size_t k = 0; k < scanLen; ++k) {
            if (scan[k] != -1 && imageStart[i + k] != static_cast<uint8_t>(scan[k])) {
                match = false;
                break;
            }
        }
        if (match)
            return const_cast<uint8_t*>(imageStart + i);
    }
    return nullptr;
}

Patch PatchStore::parseSinglePatch(const json& j) {
    return parseSinglePatch("", j);
}

Patch PatchStore::parseSinglePatch(const std::string& key, const json& j) {
    Patch p;
    p.id          = key.empty() ? j.value("id", "") : key;
    p.name        = j.value("name",        "");
    p.description = j.value("description", "");

    std::string typeStr = j.value("type", "toggle");
    if (typeStr == "value")        p.type = PatchType::Value;
    else                           p.type = PatchType::Toggle;

    std::string applyStr = j.value("apply", "normal");
    if (applyStr == "super_early")    p.apply = PatchApply::SuperEarly;
    else if (applyStr == "early")     p.apply = PatchApply::Early;
    else                              p.apply = PatchApply::Normal;

    p.enabled = false;

    if (j.contains("scan"))
        p.scan = parseScan(j.value("scan", ""));

    if (p.type == PatchType::Toggle) {
        if (j.contains("writes") && j["writes"].is_array()) {
            for (const auto& w : j["writes"]) {
                PatchWrite pw;
                pw.offset = parseHexOffset(w.value("offset", "0x0"));
                pw.bytes  = parseBytes(w.value("bytes", ""));
                if (!pw.bytes.empty()) {
                    p.writes.push_back(pw);
                }
            }
        }
    } else if (p.type == PatchType::Value) {
        p.offset = parseHexOffset(j.value("offset", "0x0"));
        if (j.contains("options") && j["options"].is_array()) {
            for (const auto& opt : j["options"]) {
                p.options.push_back(opt.get<std::string>());
            }
        }
        p.value = j.value("default", 0);
    }

    // Children — object keyed by id, or legacy array
    if (j.contains("children")) {
        if (j["children"].is_object()) {
            for (auto it = j["children"].begin(); it != j["children"].end(); ++it) {
                p.children.push_back(parseSinglePatch(it.key(), it.value()));
            }
        } else if (j["children"].is_array()) {
            for (const auto& child : j["children"]) {
                p.children.push_back(parseSinglePatch(child));
            }
        }
    }

    return p;
}

void PatchStore::parseGamePatches(const json& gameObj, std::vector<Patch>& out) {
    if (!gameObj.is_object()) return;
    for (auto it = gameObj.begin(); it != gameObj.end(); ++it) {
        if (it.value().is_object()) {
            out.push_back(parseSinglePatch(it.key(), it.value()));
        }
    }
}

void PatchStore::load(const std::string& dir) {
    m_patches.clear();

    std::string bundledPath = dir + "/patches.json";
    if (!std::filesystem::exists(bundledPath)) {
        Logger::warn("[PatchStore] patches.json not found at " + bundledPath);
    }
    if (std::filesystem::exists(bundledPath)) {
        std::ifstream f(bundledPath);
        if (f.is_open()) {
            json j;
            try {
                f >> j;
                for (auto it = j.begin(); it != j.end(); ++it) {
                    if (it.key() == "ver" || it.key() == "shared") continue;
                    parseGamePatches(it.value(), m_patches[it.key()]);
                }
                // Distribute shared patches into each game's vector
                if (j.contains("shared") && j["shared"].is_object()) {
                    for (auto it = j["shared"].begin(); it != j["shared"].end(); ++it) {
                        if (!it.value().is_object()) continue;
                        if (!it.value().contains("games") || !it.value()["games"].is_array()) continue;

                        Patch sp = parseSinglePatch(it.key(), it.value());
                        for (const auto& gid : it.value()["games"]) {
                            auto& gamePatchList = m_patches[gid.get<std::string>()];
                            bool exists = false;
                            for (const auto& existing : gamePatchList)
                                if (existing.id == sp.id) { exists = true; break; }
                            if (!exists)
                                gamePatchList.push_back(sp);
                        }
                    }
                }
            } catch (...) {
                // Malformed JSON — skip silently
            }
        }
    }

    // Load user-patches.json and merge (user patch wins on id collision)
    std::string userPath = dir + "/user-patches.json";
    if (std::filesystem::exists(userPath)) {
        Logger::info("[PatchStore] Loading user-patches.json");
        std::ifstream f(userPath);
        if (f.is_open()) {
            json j;
            try {
                f >> j;
                for (auto jit = j.begin(); jit != j.end(); ++jit) {
                    const std::string& gameId = jit.key();
                    std::vector<Patch> userPatches;
                    parseGamePatches(jit.value(), userPatches);
                    auto& bundled = m_patches[gameId];
                    for (auto& up : userPatches) {
                        Patch* found = nullptr;
                        for (int fi = 0; fi < (int)bundled.size(); ++fi)
                            if (bundled[fi].id == up.id) { found = &bundled[fi]; break; }
                        if (found) *found = up;
                        else bundled.push_back(up);
                    }
                }
            } catch (...) {
                // Malformed user-patches.json — skip silently
            }
        }
    }

    for (const auto& kv : m_patches)
        Logger::info("[PatchStore] Loaded " + std::to_string(kv.second.size()) + " patches for " + kv.first);
}

const std::vector<Patch>& PatchStore::patchesForGame(const std::string& gameId) const {
    static const std::vector<Patch> empty;
    auto it = m_patches.find(gameId);
    if (it != m_patches.end()) return it->second;
    return empty;
}

std::vector<Patch>& PatchStore::patchesForGame(const std::string& gameId) {
    return m_patches[gameId];
}

std::vector<std::string> PatchStore::gameIds() const {
    std::vector<std::string> ids;
    ids.reserve(m_patches.size());
    for (const auto& kv : m_patches) {
        ids.push_back(kv.first);
    }
    return ids;
}

void PatchStore::loadStateHelper(std::vector<Patch>& patches, const json& state) {
    for (auto& p : patches) {
        if (state.contains(p.id)) {
            const auto& s = state[p.id];
            p.enabled = s.value("enabled", false);
            if (p.type == PatchType::Value) {
                p.value = s.value("value", p.value);
            }
        }
        if (!p.children.empty()) {
            loadStateHelper(p.children, state);
        }
    }
}

void PatchStore::saveStateHelper(const std::vector<Patch>& patches, json& out) const {
    for (const auto& p : patches) {
        if (p.type == PatchType::Value) {
            out[p.id] = { {"enabled", p.enabled}, {"value", p.value} };
        } else {
            out[p.id] = { {"enabled", p.enabled} };
        }
        if (!p.children.empty()) {
            saveStateHelper(p.children, out);
        }
    }
}

void PatchStore::loadState(const json& patchState) {
    for (auto it = m_patches.begin(); it != m_patches.end(); ++it) {
        const std::string& gameId = it->first;
        if (patchState.contains(gameId) && patchState[gameId].is_object()) {
            loadStateHelper(it->second, patchState[gameId]);
        }
    }
}

json PatchStore::saveState() const {
    json out = json::object();
    for (auto it = m_patches.begin(); it != m_patches.end(); ++it) {
        const std::string& gameId = it->first;
        json gameOut = json::object();
        saveStateHelper(it->second, gameOut);
        out[gameId] = gameOut;
    }
    return out;
}

json PatchStore::saveState(const std::string& gameId) const {
    json out = json::object();
    auto it = m_patches.find(gameId);
    if (it != m_patches.end()) {
        json gameOut = json::object();
        saveStateHelper(it->second, gameOut);
        out[gameId] = gameOut;
    }
    return out;
}

void PatchStore::applyTogglePatch(const Patch& p) {
    uint8_t* writeBase = scanForPattern(p.scan);
    if (!writeBase) {
        Logger::warn("[PatchStore] Scan pattern not found for patch " + p.id);
        return;
    }
    if (!p.scan.empty())
        Logger::info("[PatchStore] Patch " + p.id + " scan matched at 0x" + toHexString(writeBase));

    for (const auto& pw : p.writes) {
        uint8_t* target = writeBase + pw.offset;
        DWORD    old    = 0;
        VirtualProtect(target, pw.bytes.size(), PAGE_EXECUTE_READWRITE, &old);
        memcpy(target, pw.bytes.data(), pw.bytes.size());
        VirtualProtect(target, pw.bytes.size(), old, &old);
    }
}

void PatchStore::applyValuePatch(const Patch& p) {
    uint8_t* writeBase = scanForPattern(p.scan);
    if (!writeBase) {
        Logger::warn("[PatchStore] Scan pattern not found for patch " + p.id);
        return;
    }
    if (!p.scan.empty())
        Logger::info("[PatchStore] Patch " + p.id + " scan matched at 0x" + toHexString(writeBase));

    uint8_t* target = writeBase + p.offset;
    DWORD    old    = 0;
    VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &old);
    *reinterpret_cast<uint8_t*>(target) = static_cast<uint8_t>(p.value);
    VirtualProtect(target, 1, old, &old);
}

void PatchStore::applyPatch(const Patch& p) {
    Logger::info("[PatchStore] Applying Patch " + p.id);
    switch (p.type) {
        case PatchType::Toggle:  applyTogglePatch(p);  break;
        case PatchType::Value:   applyValuePatch(p);   break;
    }
    for (const auto& child : p.children) {
        if (child.enabled) {
            applyPatch(child);
        }
    }
}

void PatchStore::applyVersionPatch(const std::string& replacement) {
    Logger::info("[PatchStore] Applying 2EZconfig Version patch");
    const std::string target = "Version %d.%02d";
    std::vector<int16_t> scan(target.begin(), target.end());

    uint8_t* match = scanForPattern(scan);
    if (!match) {
        Logger::warn("[PatchStore] Version string not found in process image");
        return;
    }

    size_t writeLen = replacement.size() + 1;
    DWORD old = 0;
    VirtualProtect(match, writeLen, PAGE_EXECUTE_READWRITE, &old);
    memcpy(match, replacement.c_str(), writeLen);
    VirtualProtect(match, writeLen, old, &old);
}

void PatchStore::applySuperEarlyPatches(const std::string& gameId) {
    Logger::info("[PatchStore] Applying 'super_early' patches");
    applyPatches(gameId, PatchApply::SuperEarly);
}

void PatchStore::applyEarlyPatches(const std::string& gameId) {
    Logger::info("[PatchStore] Applying 'early' patches");
    applyPatches(gameId, PatchApply::Early);
}

void PatchStore::applyPatches(const std::string& gameId) {
    Logger::info("[PatchStore] Applying patches");
    applyPatches(gameId, PatchApply::Normal);
}

void PatchStore::applyPatches(const std::string& gameId, PatchApply timing) {
    const auto& patches = patchesForGame(gameId);
    for (const auto& p : patches) {
        if (p.enabled && p.apply == timing) {
            applyPatch(p);
        } else if (!p.enabled && p.apply == timing) {
            Logger::info("[PatchStore] Skipping disabled patch " + p.id);
        }
    }
}
