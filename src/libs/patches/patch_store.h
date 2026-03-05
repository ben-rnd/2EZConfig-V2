#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <nlohmann/json.hpp>

enum class PatchType  { Toggle, Value, Pattern };
enum class PatchApply { Normal, Early };

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

    // Toggle fields
    std::vector<PatchWrite>    writes;

    // Value fields
    uint32_t                   offset  = 0;
    std::vector<std::string>   options;
    int                        value   = 0;

    // Pattern fields
    std::string          pattern;
    std::string          replacement;

    // Children (populated only for parent patches)
    std::vector<Patch>   children;
};

class PatchStore {
public:
    // Load patches.json and optionally user-patches.json from dir.
    // Merges: user patches override bundled patches by id within same game_id.
    void load(const std::string& dir);

    // Returns patches for a specific game_id (filtered). Empty vector if not found.
    const std::vector<Patch>& patchesForGame(const std::string& gameId) const;

    // Non-const overload — returns mutable reference so UI can modify enabled/value directly.
    std::vector<Patch>& patchesForGame(const std::string& gameId);

    // All game_ids that have patches.
    std::vector<std::string> gameIds() const;

    // Load patch state (enabled/value) from game-settings.json "patches" key.
    void loadState(const nlohmann::json& patchState);

    // Serialize current enabled/value state to JSON (for game-settings.json "patches" key).
    nlohmann::json saveState() const;

    // Apply all enabled patches for gameId to process memory.
    // applyEarly=true  -> apply only apply:"early" patches
    // applyEarly=false -> apply only apply:"normal" patches
    void applyPatches(const std::string& gameId, bool applyEarly);

    // Always applies the version string patch unconditionally (not controlled by patch state).
    // Replaces "Version %d.%02d" with replacement in the loaded module image.
    void applyVersionPatch(const std::string& replacement);

private:
    std::map<std::string, std::vector<Patch>> m_patches;

    void        parseGamePatches(const nlohmann::json& gameObj, std::vector<Patch>& out);
    Patch       parseSinglePatch(const nlohmann::json& j);
    void        applyPatch(const Patch& p);
    void        applyTogglePatch(const Patch& p);
    void        applyValuePatch(const Patch& p);
    void        applyPatternPatch(const Patch& p);
    void        saveStateHelper(const std::vector<Patch>& patches, nlohmann::json& out) const;
    void        loadStateHelper(std::vector<Patch>& patches, const nlohmann::json& state);
};
