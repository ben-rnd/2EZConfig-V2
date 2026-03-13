#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <nlohmann/json.hpp>

enum class PatchType  { Toggle, Value };
enum class PatchApply { Normal, Early, SuperEarly };

struct PatchWrite {
    uint32_t             offset;  // relative to GetModuleHandle(NULL), parsed from hex string
    std::vector<uint8_t> bytes;   // multi-byte write (e.g. "90 90 90" -> {0x90, 0x90, 0x90})
};

struct Patch {
    std::string          id;
    std::string          name;
    std::string          description;
    PatchType            type         = PatchType::Toggle;
    PatchApply           apply        = PatchApply::Normal;
    bool                 enabled      = false;

    std::vector<PatchWrite>    writes;

    uint32_t                   offset  = 0;
    std::vector<std::string>   options;
    int                        value   = 0;

    std::vector<int16_t> scan;        // byte scan pattern: -1 = wildcard, 0-255 = exact byte

    std::vector<Patch>   children;
};

class PatchStore {
public:
   
    void load(const std::string& dir);
    const std::vector<Patch>& patchesForGame(const std::string& gameId) const;
    // Non-const overload — returns mutable reference so UI can modify enabled/value directly.
    std::vector<Patch>& patchesForGame(const std::string& gameId);
    std::vector<std::string> gameIds() const;
    void loadState(const nlohmann::json& patchState);
    nlohmann::json saveState() const;
    nlohmann::json saveState(const std::string& gameId) const;
    void applySuperEarlyPatches(const std::string& gameId);
    void applyEarlyPatches(const std::string& gameId);
    void applyPatches(const std::string& gameId);

    // Replaces "Version %d.%02d" with replacement 2EZConfig version string.
    void applyVersionPatch(const std::string& replacement);

private:
    std::map<std::string, std::vector<Patch>> m_patches;

    void        parseGamePatches(const nlohmann::json& gameObj, std::vector<Patch>& out);
    Patch       parseSinglePatch(const nlohmann::json& j);
    Patch       parseSinglePatch(const std::string& key, const nlohmann::json& j);
    void        applyPatches(const std::string& gameId, PatchApply timing);
    void        applyPatch(const Patch& p);
    void        applyTogglePatch(const Patch& p);
    void        applyValuePatch(const Patch& p);
    void        saveStateHelper(const std::vector<Patch>& patches, nlohmann::json& out) const;
    void        loadStateHelper(std::vector<Patch>& patches, const nlohmann::json& state);
};
