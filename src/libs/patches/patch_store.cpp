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

static std::vector<int16_t> parseScan(const std::string& hexStr) {
    std::vector<int16_t> result;
    std::istringstream ss(hexStr);
    std::string token;
    while (ss >> token) {
        if (token == "??") {
            result.push_back(-1);
        } else {
            result.push_back(static_cast<int16_t>(std::stoul(token, nullptr, 16)));
        }
    }
    return result;
}

// Collect readable ranges from the process image once, then scan them.
static std::vector<uint8_t*> scanForPattern(const std::vector<int16_t>& scan) {
    HMODULE base = GetModuleHandle(NULL);
    if (scan.empty()) {
        return { reinterpret_cast<uint8_t*>(base) };
    }

    const uint8_t* imageStart = reinterpret_cast<const uint8_t*>(base);
    const SIZE_T   imageSize  = getImageSize(base);
    const size_t   scanLen    = scan.size();

    // Build list of readable byte ranges upfront
    struct Range { size_t start; size_t end; };
    std::vector<Range> ranges;
    for (size_t i = 0; i < imageSize; ) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(imageStart + i, &mbi, sizeof(mbi))) break;
        size_t regionEnd = (reinterpret_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize) - imageStart;
        if (regionEnd > imageSize) regionEnd = imageSize;
        if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
            (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY | PAGE_WRITECOPY))) {
            ranges.push_back({ i, regionEnd });
        }
        i = regionEnd;
    }

    // Scan only within readable ranges
    std::vector<uint8_t*> matches;
    for (const auto& r : ranges) {
        for (size_t i = r.start; i + scanLen <= r.end; ++i) {
            bool match = true;
            for (size_t k = 0; k < scanLen; ++k) {
                if (scan[k] != -1 && imageStart[i + k] != static_cast<uint8_t>(scan[k])) {
                    match = false;
                    break;
                }
            }
            if (match) {
                matches.push_back(const_cast<uint8_t*>(imageStart + i));
            }
        }
    }
    return matches;
}

Patch PatchStore::parseSinglePatch(const json& j) {
    return parseSinglePatch("", j);
}

Patch PatchStore::parseSinglePatch(const std::string& key, const json& j) {
    Patch patch;
    patch.id          = key.empty() ? j.value("id", "") : key;
    patch.name        = j.value("name",        "");
    patch.description = j.value("description", "");

    std::string typeStr = j.value("type", "toggle");
    if (typeStr == "value") {
        patch.type = PatchType::Value;
    } else {
        patch.type = PatchType::Toggle;
    }

    std::string applyStr = j.value("apply", "normal");
    if (applyStr == "super_early") {
        patch.apply = PatchApply::SuperEarly;
    } else if (applyStr == "early") {
        patch.apply = PatchApply::Early;
    } else {
        patch.apply = PatchApply::Normal;
    }

    patch.enabled = false;

    if (patch.type == PatchType::Toggle) {
        if (j.contains("scan_group") && j["scan_group"].is_array()) {
            // Scan-based patch: each entry has its own scan + writes
            for (const auto& scanEntry : j["scan_group"]) {
                ScanGroup group;
                if (scanEntry.contains("scan"))
                    group.scan = parseScan(scanEntry.value("scan", ""));
                if (scanEntry.contains("writes") && scanEntry["writes"].is_array()) {
                    for (const auto& writeEntry : scanEntry["writes"]) {
                        PatchWrite pw;
                        pw.offset = parseHexOffset(writeEntry.value("offset", "0x0"));
                        pw.bytes  = parseBytes(writeEntry.value("bytes", ""));
                        if (!pw.bytes.empty())
                            group.writes.push_back(pw);
                    }
                }
                patch.scan_group.push_back(group);
            }
        } else if (j.contains("writes") && j["writes"].is_array()) {
            // RVA-based patch: top-level writes, no scan
            ScanGroup group;
            for (const auto& writeEntry : j["writes"]) {
                PatchWrite pw;
                pw.offset = parseHexOffset(writeEntry.value("offset", "0x0"));
                pw.bytes  = parseBytes(writeEntry.value("bytes", ""));
                if (!pw.bytes.empty())
                    group.writes.push_back(pw);
            }
            patch.scan_group.push_back(group);
        }
    } else if (patch.type == PatchType::Value) {
        patch.offset = parseHexOffset(j.value("offset", "0x0"));
        if (j.contains("options") && j["options"].is_array()) {
            for (const auto& opt : j["options"]) {
                patch.options.push_back(opt.get<std::string>());
            }
        }
        patch.value = j.value("default", 0);
    }

    // Children — object keyed by id, or legacy array
    if (j.contains("children")) {
        if (j["children"].is_object()) {
            for (auto it = j["children"].begin(); it != j["children"].end(); ++it) {
                patch.children.push_back(parseSinglePatch(it.key(), it.value()));
            }
        } else if (j["children"].is_array()) {
            for (const auto& child : j["children"]) {
                patch.children.push_back(parseSinglePatch(child));
            }
        }
    }

    return patch;
}

void PatchStore::parseGamePatches(const json& gameObj, std::vector<Patch>& out) {
    if (!gameObj.is_object()) {
        return;
    }
    for (auto it = gameObj.begin(); it != gameObj.end(); ++it) {
        if (it.value().is_object()) {
            out.push_back(parseSinglePatch(it.key(), it.value()));
        }
    }
}

void PatchStore::load(const std::string& dir, const std::string& gameDir) {
    m_patches.clear();

    std::string bundledPath = dir + "/patches.json";
    if (!std::filesystem::exists(bundledPath)) {
        Logger::warn("[PatchStore] patches.json not found at " + bundledPath);
    }
    if (std::filesystem::exists(bundledPath)) {
        std::ifstream file(bundledPath);
        if (file.is_open()) {
            json patchesJson;
            try {
                file >> patchesJson;
                for (auto it = patchesJson.begin(); it != patchesJson.end(); ++it) {
                    if (it.key() == "ver" || it.key() == "shared") {
                        continue;
                    }
                    parseGamePatches(it.value(), m_patches[it.key()]);
                }
                // Distribute shared patches into each game's vector
                if (patchesJson.contains("shared") && patchesJson["shared"].is_object()) {
                    for (auto it = patchesJson["shared"].begin(); it != patchesJson["shared"].end(); ++it) {
                        if (!it.value().is_object()) {
                            continue;
                        }
                        if (!it.value().contains("games") || !it.value()["games"].is_array()) {
                            continue;
                        }

                        Patch sharedPatch = parseSinglePatch(it.key(), it.value());
                        for (const auto& gameId : it.value()["games"]) {
                            auto& gamePatchList = m_patches[gameId.get<std::string>()];
                            bool exists = false;
                            for (const auto& existing : gamePatchList) {
                                if (existing.id == sharedPatch.id) {
                                exists = true;
                                break;
                            }
                            }
                            if (!exists) {
                                gamePatchList.push_back(sharedPatch);
                            }
                        }
                    }
                }
            } catch (...) {
                // Malformed JSON — skip silently
            }
        }
    }

    // Load user-patches.json and merge (user patch wins on id collision)
    std::string userPath = gameDir + "/user-patches.json";
    if (std::filesystem::exists(userPath)) {
        Logger::info("[PatchStore] Loading user-patches.json");
        std::ifstream file(userPath);
        if (file.is_open()) {
            json patchesJson;
            try {
                file >> patchesJson;
                for (auto jit = patchesJson.begin(); jit != patchesJson.end(); ++jit) {
                    const std::string& gameId = jit.key();
                    std::vector<Patch> userPatches;
                    parseGamePatches(jit.value(), userPatches);
                    auto& existingPatches = m_patches[gameId];
                    for (auto& userPatch : userPatches) {
                        Patch* found = nullptr;
                        for (int findIdx = 0; findIdx < (int)existingPatches.size(); ++findIdx) {
                            if (existingPatches[findIdx].id == userPatch.id) {
                            found = &existingPatches[findIdx];
                            break;
                        }
                        }
                        if (found) {
                            *found = userPatch;
                        } else {
                            existingPatches.push_back(userPatch);
                        }
                    }
                }
            } catch (...) {
                // Malformed user-patches.json — skip silently
            }
        }
    }

    for (const auto& gameEntry : m_patches) {
        Logger::info("[PatchStore] Loaded " + std::to_string(gameEntry.second.size()) + " patches for " + gameEntry.first);
    }
}

const std::vector<Patch>& PatchStore::patchesForGame(const std::string& gameId) const {
    static const std::vector<Patch> empty;
    auto it = m_patches.find(gameId);
    if (it != m_patches.end()) {
        return it->second;
    }
    return empty;
}

std::vector<Patch>& PatchStore::patchesForGame(const std::string& gameId) {
    return m_patches[gameId];
}

std::vector<std::string> PatchStore::gameIds() const {
    std::vector<std::string> ids;
    ids.reserve(m_patches.size());
    for (const auto& gameEntry : m_patches) {
        ids.push_back(gameEntry.first);
    }
    return ids;
}

void PatchStore::loadStateHelper(std::vector<Patch>& patches, const json& state) {
    for (auto& patch : patches) {
        if (state.contains(patch.id)) {
            const auto& savedState = state[patch.id];
            patch.enabled = savedState.value("enabled", false);
            if (patch.type == PatchType::Value) {
                patch.value = savedState.value("value", patch.value);
            }
        }
        if (!patch.children.empty()) {
            loadStateHelper(patch.children, state);
        }
    }
}

void PatchStore::saveStateHelper(const std::vector<Patch>& patches, json& out) const {
    for (const auto& patch : patches) {
        if (patch.type == PatchType::Value) {
            out[patch.id] = { {"enabled", patch.enabled}, {"value", patch.value} };
        } else {
            out[patch.id] = { {"enabled", patch.enabled} };
        }
        if (!patch.children.empty()) {
            saveStateHelper(patch.children, out);
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
    for (size_t gi = 0; gi < p.scan_group.size(); ++gi) {
        const auto& group = p.scan_group[gi];
        auto matches = scanForPattern(group.scan);
        if (matches.empty()) {
            Logger::warn("[PatchStore] Scan pattern not found for patch " + p.name +
                         " (scan " + std::to_string(gi + 1) + "/" + std::to_string(p.scan_group.size()) + ")");
            return;
        }
        if (!group.scan.empty()) {
            for (auto* writeBase : matches) {
                Logger::info("[PatchStore] Patch " + p.name + " scan " +
                             std::to_string(gi + 1) + " matched at 0x" + toHexString(writeBase));
            }
        }
        for (auto* writeBase : matches) {
            for (const auto& pw : group.writes) {
                uint8_t* target = writeBase + pw.offset;
                DWORD    previousProtection = 0;
                VirtualProtect(target, pw.bytes.size(), PAGE_EXECUTE_READWRITE, &previousProtection);
                memcpy(target, pw.bytes.data(), pw.bytes.size());
                VirtualProtect(target, pw.bytes.size(), previousProtection, &previousProtection);
            }
        }
    }
}

void PatchStore::applyValuePatch(const Patch& p) {
    std::vector<int16_t> scan;
    if (!p.scan_group.empty())
        scan = p.scan_group[0].scan;

    auto matches = scanForPattern(scan);
    if (matches.empty()) {
        Logger::warn("[PatchStore] Scan pattern not found for patch " + p.name);
        return;
    }
    if (!scan.empty()) {
        for (auto* writeBase : matches) {
            Logger::info("[PatchStore] Patch " + p.name + " scan matched at 0x" + toHexString(writeBase));
        }
    }

    for (auto* writeBase : matches) {
        uint8_t* target = writeBase + p.offset;
        DWORD    old    = 0;
        VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &old);
        *reinterpret_cast<uint8_t*>(target) = static_cast<uint8_t>(p.value);
        VirtualProtect(target, 1, old, &old);
    }
}

void PatchStore::applyPatch(const Patch& p) {
    Logger::info("[PatchStore] Applying Patch " + p.name);
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

    auto matches = scanForPattern(scan);
    if (matches.empty()) {
        Logger::warn("[PatchStore] Version string not found in process image");
        return;
    }

    for (auto* match : matches) {
        Logger::info("[PatchStore] Replacing string at 0x" + toHexString(match));
        size_t writeLen = replacement.size() + 1;
        DWORD old = 0;
        VirtualProtect(match, writeLen, PAGE_EXECUTE_READWRITE, &old);
        memcpy(match, replacement.c_str(), writeLen);
        VirtualProtect(match, writeLen, old, &old);
    }
}

void PatchStore::applySuperEarlyPatches(const std::string& gameId) {
    applyPatches(gameId, PatchApply::SuperEarly);
}

void PatchStore::applyEarlyPatches(const std::string& gameId) {
    applyPatches(gameId, PatchApply::Early);
}

void PatchStore::applyPatches(const std::string& gameId) {
    applyPatches(gameId, PatchApply::Normal);
}

void PatchStore::applyPatches(const std::string& gameId, PatchApply timing) {
    const auto& patches = patchesForGame(gameId);
    for (const auto& p : patches) {
        if (p.enabled && p.apply == timing) {
            applyPatch(p);
        } else if (!p.enabled && p.apply == timing) {
            Logger::info("[PatchStore] Skipping disabled patch " + p.name);
        }
    }
}
