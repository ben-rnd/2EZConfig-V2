#include "patch_store.h"

#include <windows.h>
#include <psapi.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// PatchStore::parseSinglePatch
// ---------------------------------------------------------------------------

Patch PatchStore::parseSinglePatch(const json& j) {
    Patch p;
    p.id          = j.value("id",          "");
    p.name        = j.value("name",        "");
    p.description = j.value("description", "");

    // Type
    std::string typeStr = j.value("type", "toggle");
    if (typeStr == "value")        p.type = PatchType::Value;
    else if (typeStr == "pattern") p.type = PatchType::Pattern;
    else                           p.type = PatchType::Toggle;

    // Apply timing
    std::string applyStr = j.value("apply", "normal");
    p.apply = (applyStr == "early") ? PatchApply::Early : PatchApply::Normal;

    // Enabled defaults false
    p.enabled = false;

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
    } else if (p.type == PatchType::Pattern) {
        p.pattern     = j.value("pattern",     "");
        p.replacement = j.value("replacement", "");
    }

    // Children (recursive)
    if (j.contains("children") && j["children"].is_array()) {
        for (const auto& child : j["children"]) {
            p.children.push_back(parseSinglePatch(child));
        }
    }

    return p;
}

// ---------------------------------------------------------------------------
// PatchStore::parseGamePatches
// ---------------------------------------------------------------------------

void PatchStore::parseGamePatches(const json& gameObj, std::vector<Patch>& out) {
    if (!gameObj.contains("patches") || !gameObj["patches"].is_array()) return;
    for (const auto& patchJson : gameObj["patches"]) {
        out.push_back(parseSinglePatch(patchJson));
    }
}

// ---------------------------------------------------------------------------
// PatchStore::load
// ---------------------------------------------------------------------------

void PatchStore::load(const std::string& dir) {
    m_patches.clear();

    // Load bundled patches.json
    std::string bundledPath = dir + "/patches.json";
    if (std::filesystem::exists(bundledPath)) {
        std::ifstream f(bundledPath);
        if (f.is_open()) {
            json j;
            try {
                f >> j;
                for (auto& [gameId, gameObj] : j.items()) {
                    parseGamePatches(gameObj, m_patches[gameId]);
                }
            } catch (...) {
                // Malformed JSON — skip silently
            }
        }
    }

    // Load user-patches.json and merge (user patch wins on id collision)
    std::string userPath = dir + "/user-patches.json";
    if (std::filesystem::exists(userPath)) {
        std::ifstream f(userPath);
        if (f.is_open()) {
            json j;
            try {
                f >> j;
                for (auto& [gameId, gameObj] : j.items()) {
                    std::vector<Patch> userPatches;
                    parseGamePatches(gameObj, userPatches);
                    auto& bundled = m_patches[gameId];
                    for (auto& up : userPatches) {
                        auto it = std::find_if(bundled.begin(), bundled.end(),
                            [&](const Patch& bp) { return bp.id == up.id; });
                        if (it != bundled.end()) *it = up;
                        else bundled.push_back(up);
                    }
                }
            } catch (...) {
                // Malformed user-patches.json — skip silently
            }
        }
    }
}

// ---------------------------------------------------------------------------
// patchesForGame
// ---------------------------------------------------------------------------

const std::vector<Patch>& PatchStore::patchesForGame(const std::string& gameId) const {
    static const std::vector<Patch> empty;
    auto it = m_patches.find(gameId);
    if (it != m_patches.end()) return it->second;
    return empty;
}

std::vector<Patch>& PatchStore::patchesForGame(const std::string& gameId) {
    return m_patches[gameId];  // creates empty vector if key absent
}

// ---------------------------------------------------------------------------
// gameIds
// ---------------------------------------------------------------------------

std::vector<std::string> PatchStore::gameIds() const {
    std::vector<std::string> ids;
    ids.reserve(m_patches.size());
    for (const auto& kv : m_patches) {
        ids.push_back(kv.first);
    }
    return ids;
}

// ---------------------------------------------------------------------------
// loadState / saveState helpers
// ---------------------------------------------------------------------------

void PatchStore::loadStateHelper(std::vector<Patch>& patches, const json& state) {
    for (auto& p : patches) {
        if (state.contains(p.id)) {
            const auto& s = state[p.id];
            p.enabled = s.value("enabled", false);
            if (p.type == PatchType::Value) {
                p.value = s.value("value", p.value);
            }
        }
        // Recurse into children
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
        // Recurse into children
        if (!p.children.empty()) {
            saveStateHelper(p.children, out);
        }
    }
}

void PatchStore::loadState(const json& patchState) {
    for (auto& [gameId, patches] : m_patches) {
        // patchState is scoped by game_id: { "ez2ac_fn_ex": { "patch_id": {...} } }
        if (patchState.contains(gameId) && patchState[gameId].is_object()) {
            loadStateHelper(patches, patchState[gameId]);
        }
    }
}

json PatchStore::saveState() const {
    json out = json::object();
    for (const auto& [gameId, patches] : m_patches) {
        // Save per game_id so same-named patches across games don't collide.
        json gameOut = json::object();
        saveStateHelper(patches, gameOut);
        out[gameId] = gameOut;
    }
    return out;
}

// ---------------------------------------------------------------------------
// applyPatches
// ---------------------------------------------------------------------------

void PatchStore::applyTogglePatch(const Patch& p) {
    LPVOID base = GetModuleHandle(NULL);
    for (const auto& pw : p.writes) {
        LPVOID target = reinterpret_cast<uint8_t*>(base) + pw.offset;
        DWORD  old    = 0;
        VirtualProtect(target, pw.bytes.size(), PAGE_EXECUTE_READWRITE, &old);
        memcpy(target, pw.bytes.data(), pw.bytes.size());
        VirtualProtect(target, pw.bytes.size(), old, &old);
    }
}

void PatchStore::applyValuePatch(const Patch& p) {
    LPVOID base   = GetModuleHandle(NULL);
    LPVOID target = reinterpret_cast<uint8_t*>(base) + p.offset;
    DWORD  old    = 0;
    VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &old);
    *reinterpret_cast<uint8_t*>(target) = static_cast<uint8_t>(p.value);
    VirtualProtect(target, 1, old, &old);
}

void PatchStore::applyPatternPatch(const Patch& p) {
    if (p.pattern.empty() || p.replacement.empty()) return;

    HMODULE    base = GetModuleHandle(NULL);
    MODULEINFO mi   = {};
    if (!GetModuleInformation(GetCurrentProcess(), base, &mi, sizeof(mi))) return;

    const uint8_t* imageStart = reinterpret_cast<const uint8_t*>(base);
    const uint8_t* imageEnd   = imageStart + mi.SizeOfImage;
    const size_t   patternLen = p.pattern.size();

    MEMORY_BASIC_INFORMATION mbi = {};
    const uint8_t* addr = imageStart;

    while (addr < imageEnd) {
        if (!VirtualQuery(addr, &mbi, sizeof(mbi))) break;

        const uint8_t* regionStart = reinterpret_cast<const uint8_t*>(mbi.BaseAddress);
        const uint8_t* regionEnd   = regionStart + mbi.RegionSize;
        if (regionEnd > imageEnd) regionEnd = imageEnd;

        if (mbi.State == MEM_COMMIT &&
            !(mbi.Protect & PAGE_NOACCESS) &&
            !(mbi.Protect & PAGE_GUARD))
        {
            for (const uint8_t* scan = regionStart;
                 scan + patternLen <= regionEnd; ++scan)
            {
                if (memcmp(scan, p.pattern.c_str(), patternLen) == 0) {
                    uint8_t* target   = const_cast<uint8_t*>(scan);
                    size_t   writeLen = p.replacement.size() + 1;
                    DWORD old = 0;
                    VirtualProtect(target, writeLen, PAGE_EXECUTE_READWRITE, &old);
                    memcpy(target, p.replacement.c_str(), writeLen);
                    VirtualProtect(target, writeLen, old, &old);
                    return;  // first match wins
                }
            }
        }

        addr = regionStart + mbi.RegionSize;
    }
    // Not found — silent no-op
}

void PatchStore::applyPatch(const Patch& p) {
    switch (p.type) {
        case PatchType::Toggle:  applyTogglePatch(p);  break;
        case PatchType::Value:   applyValuePatch(p);   break;
        case PatchType::Pattern: applyPatternPatch(p); break;
    }
    // Recurse into children only when parent is enabled
    for (const auto& child : p.children) {
        if (child.enabled) {
            applyPatch(child);
        }
    }
}

void PatchStore::applyVersionPatch(const std::string& replacement) {
    Patch p;
    p.type        = PatchType::Pattern;
    p.pattern     = "Version %d.%02d";
    p.replacement = replacement;
    applyPatternPatch(p);
}

void PatchStore::applyEarlyPatches(const std::string& gameId) {
    applyPatches(gameId, true);
}

void PatchStore::applyPatches(const std::string& gameId) {
    applyPatches(gameId, false);
}


void PatchStore::applyPatches(const std::string& gameId, bool applyEarly) {
    const auto& patches = patchesForGame(gameId);
    PatchApply  target  = applyEarly ? PatchApply::Early : PatchApply::Normal;
    for (const auto& p : patches) {
        if (p.enabled && p.apply == target) {
            applyPatch(p);
        }
    }
}
