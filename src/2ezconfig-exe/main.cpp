#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
#include "injector.h"
#include "settings.h"
#include "game_defs.h"
#include "game_detect.h"
#include "input_manager.h"
#include "bindings.h"
#include "patch_store.h"
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <filesystem>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Per-family theme colours
struct Theme {
    ImVec4 frame, border, background;
    ImVec4 text, disabledText;
    ImVec4 accent, accentDim, accentActive;
};

// EZ2DJ/AC — warm orange
static constexpr Theme THEME_EZ2DJ = {
    { 0.16f, 0.15f, 0.13f, 1.00f },
    { 0.28f, 0.26f, 0.21f, 1.00f },
    { 0.07f, 0.07f, 0.07f, 1.00f },
    { 1.00f, 1.00f, 1.00f, 1.00f },
    { 0.42f, 0.41f, 0.38f, 1.00f },
    { 0.83f, 0.44f, 0.10f, 1.00f },
    { 0.40f, 0.22f, 0.05f, 1.00f },
    { 1.00f, 0.58f, 0.15f, 1.00f },
};

// EZ2Dancer — blue/pink//green 
static constexpr Theme THEME_DANCER = {
    { 0.10f, 0.18f, 0.30f, 1.00f }, 
    { 0.20f, 0.40f, 0.65f, 1.00f }, 
    { 0.06f, 0.10f, 0.18f, 1.00f }, 
    { 1.00f, 1.00f, 1.00f, 1.00f }, 
    { 0.35f, 0.50f, 0.65f, 1.00f }, 
    { 0.30f, 0.58f, 0.95f, 1.00f }, 
    { 0.85f, 0.20f, 0.50f, 1.00f }, 
    { 0.55f, 0.92f, 0.45f, 1.00f }, 
};

// Sabin Sound Star — green/teal
static constexpr Theme THEME_SABIN = {
    { 0.10f, 0.12f, 0.10f, 1.00f },
    { 0.15f, 0.28f, 0.28f, 1.00f },
    { 0.05f, 0.06f, 0.05f, 1.00f },
    { 1.00f, 1.00f, 1.00f, 1.00f },
    { 0.30f, 0.45f, 0.48f, 1.00f },
    { 0.18f, 0.80f, 0.30f, 1.00f },
    { 0.08f, 0.28f, 0.28f, 1.00f },
    { 0.10f, 0.70f, 0.90f, 1.00f },
};

static const Theme& themeForFamily(GameFamily family) {
    switch (family) {
        case GameFamily::EZ2Dancer: return THEME_DANCER;
        case GameFamily::SabinSS:   return THEME_SABIN;
        default:                    return THEME_EZ2DJ;
    }
}

// Active theme (mutable — switched on family change)
static ImVec4 FRAME         = THEME_EZ2DJ.frame;
static ImVec4 BORDER        = THEME_EZ2DJ.border;
static ImVec4 BACKGROUND    = THEME_EZ2DJ.background;
static ImVec4 TEXT          = THEME_EZ2DJ.text;
static ImVec4 DISABLED_TEXT = THEME_EZ2DJ.disabledText;
static ImVec4 ACCENT        = THEME_EZ2DJ.accent;
static ImVec4 ACCENT_DIM    = THEME_EZ2DJ.accentDim;
static ImVec4 ACCENT_ACTIVE = THEME_EZ2DJ.accentActive;

static void applyTheme(GameFamily family) {
    const Theme& t = themeForFamily(family);
    FRAME         = t.frame;
    BORDER        = t.border;
    BACKGROUND    = t.background;
    TEXT          = t.text;
    DISABLED_TEXT = t.disabledText;
    ACCENT        = t.accent;
    ACCENT_DIM    = t.accentDim;
    ACCENT_ACTIVE = t.accentActive;
    // Re-apply to ImGui style
    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text]                 = TEXT;
    colors[ImGuiCol_TextDisabled]         = DISABLED_TEXT;
    colors[ImGuiCol_WindowBg]             = BACKGROUND;
    colors[ImGuiCol_PopupBg]              = BACKGROUND;
    colors[ImGuiCol_TitleBg]              = BACKGROUND;
    colors[ImGuiCol_TitleBgActive]        = FRAME;
    colors[ImGuiCol_TitleBgCollapsed]     = BACKGROUND;
    colors[ImGuiCol_MenuBarBg]            = BACKGROUND;
    colors[ImGuiCol_Border]               = BORDER;
    colors[ImGuiCol_Separator]            = BORDER;
    colors[ImGuiCol_SeparatorHovered]     = ACCENT;
    colors[ImGuiCol_SeparatorActive]      = ACCENT_ACTIVE;
    colors[ImGuiCol_FrameBg]              = FRAME;
    colors[ImGuiCol_FrameBgHovered]       = BORDER;
    colors[ImGuiCol_FrameBgActive]        = BORDER;
    colors[ImGuiCol_ScrollbarBg]          = BACKGROUND;
    colors[ImGuiCol_ScrollbarGrab]        = BORDER;
    colors[ImGuiCol_ScrollbarGrabHovered] = DISABLED_TEXT;
    colors[ImGuiCol_ScrollbarGrabActive]  = ACCENT;
    colors[ImGuiCol_Button]               = ACCENT_DIM;
    colors[ImGuiCol_ButtonHovered]        = ACCENT;
    colors[ImGuiCol_ButtonActive]         = ACCENT_ACTIVE;
    colors[ImGuiCol_Header]               = ACCENT_DIM;
    colors[ImGuiCol_HeaderHovered]        = ACCENT;
    colors[ImGuiCol_HeaderActive]         = ACCENT_ACTIVE;
    colors[ImGuiCol_CheckMark]            = ACCENT;
    colors[ImGuiCol_SliderGrab]           = ACCENT;
    colors[ImGuiCol_SliderGrabActive]     = ACCENT_ACTIVE;
    colors[ImGuiCol_ResizeGrip]           = ACCENT_DIM;
    colors[ImGuiCol_ResizeGripHovered]    = ACCENT;
    colors[ImGuiCol_ResizeGripActive]     = ACCENT_ACTIVE;
    colors[ImGuiCol_Tab]                  = ACCENT_DIM;
    colors[ImGuiCol_TabHovered]           = ACCENT_ACTIVE;
    colors[ImGuiCol_TabActive]            = ACCENT;
    colors[ImGuiCol_TabUnfocused]         = BACKGROUND;
    colors[ImGuiCol_TabUnfocusedActive]   = FRAME;
    colors[ImGuiCol_PlotLines]            = DISABLED_TEXT;
    colors[ImGuiCol_PlotLinesHovered]     = ACCENT_ACTIVE;
    colors[ImGuiCol_PlotHistogram]        = ACCENT;
    colors[ImGuiCol_PlotHistogramHovered] = ACCENT_ACTIVE;
}

static std::string getAppDataDir() {
    char pathBuffer[MAX_PATH] = {};
    if (GetEnvironmentVariableA("APPDATA", pathBuffer, MAX_PATH)) {
        return std::string(pathBuffer) + "\\2ezconfig";
    }
    return ".";
}

static void renderUI();
static void setTheme();
static void renderSettingsTab();
static void gameCheckbox(const char* label, const char* key, bool defaultVal);
static void renderButtonsTab();
static void renderAnalogsTab();
static void renderAnalogEditPopup(const std::vector<Device>& axisDevices);
static void renderLightsTab();
static void renderLightBindPopup(const std::vector<Device>& outputDevices);
static void renderPatchesTab();
static void renderPatchRow(Patch& patch);
static int pollKeyboardPress(bool* prevKeys);
static void renderVttKeyBind(const char* label, const char* bindId, const char* clearId,
                             ButtonBinding& binding, bool& capturing, bool& otherCapturing, bool* prevKeys);
static void globalCheckbox(const char* label, const char* key, bool defaultVal);
static void autoDetectGame();
static std::string resolveActiveExeName();
static std::vector<std::string> parseExtraDlls();
static void launchGame();
static void cleanupUI();
static int sixthBackgroundLoop(const char* launcherExe, const std::vector<std::string>& extraDlls);
static void renderRemember1stPatches();
static nlohmann::json saveSixthPatchState(const std::string& gameId);

static const char* s_buildDate = BUILD_DATE;

struct AppState {
    SettingsManager settings;
    InputManager* input = nullptr;
    BindingStore bindings;
    int gameIdx = 0;
    GameFamily family = GameFamily::EZ2DJ;
};
static AppState g_app;
static GLFWwindow* g_window = nullptr;
static bool s_sixthMode = false;
static std::string s_sixthLauncherExe;
static std::vector<std::string> s_sixthExtraDlls;

// Analog-edit popup state (persists across frames; opened by renderAnalogsTab)
static int s_editPort = -1;
static bool s_openPopup = false;
static int s_devIdx[2] = {0, 0};
static int s_axisIdx[2] = {0, 0};
static bool s_initialized[2] = {false, false};
static bool s_capturingVtt[2][2] = {};
static bool s_vttPrevKeys[256] = {};

// Light-bind popup state (persists across frames; opened by renderLightsTab)
static int s_bindLightIdx = -1;
static bool s_openLightPopup = false;
static int s_lightDevIdx = 0;
static int s_lightOutIdx = -1;
static float s_testTimer = 0.0f;
static std::string s_testPath;
static int s_testOutIdx = -1;


int main() {
    glfwSetErrorCallback([](int errorCode, const char* errorDescription) {
        fprintf(stderr, "GLFW error %d: %s\n", errorCode, errorDescription);
    });
    if (!glfwInit()) {
        return 1;
    }

    g_window = glfwCreateWindow(640, 480, "2EZConfig", nullptr, nullptr);
    if (!g_window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1);

    HWND hwnd = glfwGetWin32Window(g_window);
    HICON icon = LoadIconA(GetModuleHandleA(nullptr), "IDI_ICON1");
    SendMessage(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplGlfw_InitForOpenGL(g_window, true);
    ImGui_ImplOpenGL2_Init();

    setTheme();

    std::string appDataDir = getAppDataDir();
    std::filesystem::create_directories(appDataDir);

    // Extract patches.json from embedded resource to appdata dir if missing or outdated
    std::string patchesPath = appDataDir + "\\patches.json";
    HRSRC resourceHandle = FindResourceA(nullptr, "PATCHES_JSON", MAKEINTRESOURCEA(10));
    if (resourceHandle) {
        HGLOBAL loadedResource = LoadResource(nullptr, resourceHandle);
        if (loadedResource) {
            DWORD resourceSize = SizeofResource(nullptr, resourceHandle);
            const char* resourceData = static_cast<const char*>(LockResource(loadedResource));
            if (resourceData && resourceSize) {
                bool shouldWrite = (GetFileAttributesA(patchesPath.c_str()) == INVALID_FILE_ATTRIBUTES);
                if (!shouldWrite) {
                    try {
                        auto embedded = nlohmann::json::parse(resourceData, resourceData + resourceSize);
                        int embeddedVer = embedded.value("ver", 0);
                        std::ifstream diskFile(patchesPath);
                        if (diskFile.is_open()) {
                            auto diskJson = nlohmann::json::parse(diskFile);
                            if (!diskJson.contains("ver") || embeddedVer > diskJson.value("ver", 0)) {
                                shouldWrite = true;
                            }
                        }
                    } catch (...) {}
                }
                if (shouldWrite) {
                    HANDLE hFile = CreateFileA(patchesPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (hFile != INVALID_HANDLE_VALUE) {
                        DWORD bytesWritten = 0;
                        WriteFile(hFile, resourceData, resourceSize, &bytesWritten, nullptr);
                        CloseHandle(hFile);
                    }
                }
            }
        }
    }

    g_app.settings.load(".", appDataDir);

    g_app.input = new InputManager();
    g_app.bindings.load(g_app.settings, *g_app.input);

    autoDetectGame();
    applyTheme(g_app.family);

    if (g_app.settings.gameSettings().value("skip_ui", false)) {
        launchGame();
        cleanupUI();
        if (s_sixthMode) {
            return sixthBackgroundLoop(s_sixthLauncherExe.c_str(), s_sixthExtraDlls);
        }
        return 0;
    }

    while (!glfwWindowShouldClose(g_window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        renderUI();

        ImGui::Render();
        int frameWidth, frameHeight;
        glfwGetFramebufferSize(g_window, &frameWidth, &frameHeight);
        glViewport(0, 0, frameWidth, frameHeight);
        glClearColor(BACKGROUND.x, BACKGROUND.y, BACKGROUND.z, BACKGROUND.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(g_window);
    }

    cleanupUI();

    if (s_sixthMode) {
        return sixthBackgroundLoop(s_sixthLauncherExe.c_str(), s_sixthExtraDlls);
    }

    return 0;
}

static void autoDetectGame() {
    std::string gameId = g_app.settings.gameSettings().value("game_id", "");
    if (gameId.empty()) {
        WIN32_FIND_DATAA findData;
        HANDLE hFind = FindFirstFileA("*.exe", &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                DetectedGame detected = detectGameFromFile(findData.cFileName);
                if (!detected.id.empty()) {
                    gameId = detected.id;
                    g_app.settings.gameSettings()["game_id"] = gameId;
                    g_app.settings.save();
                    break;
                }
            } while (FindNextFileA(hFind, &findData));
            FindClose(hFind);
        }
    }
    if (gameId.empty()) {
        gameId = "ez2dj_1st";
    }

    g_app.gameIdx = gameIndexFromId(gameId);
    g_app.family  = familyFromGameId(gameId);
}

static void renderUI() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration   |
        ImGuiWindowFlags_NoMove         |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("##main", nullptr, flags);

    ImGui::TextUnformatted("2EZConfig ~ It rules once again ~ ");
    ImGui::SameLine();

    float playButtonWidth = 90.0f;
    float availableWidth = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + availableWidth - playButtonWidth);
    if (ImGui::Button("Play EZ2", ImVec2(playButtonWidth, 0))) {
        launchGame();
        glfwSetWindowShouldClose(g_window, GLFW_TRUE);
    }

    ImGui::Separator();

    if (ImGui::BeginTabBar("##tabs")) {

        if (ImGui::BeginTabItem("Settings")) {
            renderSettingsTab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Buttons")) {
            renderButtonsTab();
            ImGui::EndTabItem();
        }

        if (g_app.family == GameFamily::EZ2DJ) {
            if (ImGui::BeginTabItem("Analogs")) {
                renderAnalogsTab();
                ImGui::EndTabItem();
            }
        }

        if (ImGui::BeginTabItem("Lights")) {
            renderLightsTab();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    {
        char buf[256];
        snprintf(buf, sizeof(buf), "Made by kasaski (kissAss) - %s",  s_buildDate);
        float textW = ImGui::CalcTextSize(buf).x;
        ImGui::SetCursorPos({ ImGui::GetWindowWidth() - textW - 10.0f, ImGui::GetWindowHeight() - 20 });
        ImGui::TextDisabled("%s", buf);
    }
    
    ImGui::End();
}

static void renderPatchesTab() {
    std::string gameId = g_app.settings.gameSettings().value("game_id", "");
    auto& patches = g_app.settings.patchStore().patchesForGame(gameId);
    bool hasSixthR1st = (gameId == "ez2dj_6th");
    auto& r1stPatches = hasSixthR1st ? g_app.settings.patchStore().patchesForGame("rmbr_1st") : patches;

    if (patches.empty() && (!hasSixthR1st || r1stPatches.empty())) {
        const char* emptyMessage = "No patches available for this game.";
        float textW = ImGui::CalcTextSize(emptyMessage).x;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - textW) * 0.5f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 40.0f);
        ImGui::TextDisabled("%s", emptyMessage);
        return;
    }

    ImGui::BeginChild("##patchScroll", ImVec2(0, 0), false);
    for (auto& patch : patches) {
        ImGui::PushID(patch.id.c_str());
        renderPatchRow(patch);
        ImGui::PopID();
    }
    if (hasSixthR1st) renderRemember1stPatches();
    ImGui::EndChild();
}

static void renderPatchRow(Patch& patch) {
    bool changed = ImGui::Checkbox(patch.name.c_str(), &patch.enabled);

    if (patch.type == PatchType::Value) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        std::vector<const char*> optionLabels;
        for (const auto& optionName : patch.options) {
            optionLabels.push_back(optionName.c_str());
        }
        if (ImGui::Combo("##val", &patch.value, optionLabels.data(), static_cast<int>(optionLabels.size()))) {
            changed = true;
        }
    }

    if (!patch.description.empty() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", patch.description.c_str());
    }

    if (changed) {
        std::string gameId = g_app.settings.gameSettings().value("game_id", "");
        if (gameId == "ez2dj_6th")
            g_app.settings.gameSettings()["patches"] = saveSixthPatchState(gameId);
        else
            g_app.settings.gameSettings()["patches"] = g_app.settings.patchStore().saveState(gameId);
        g_app.settings.save();
    }

    // Children rendered only when parent is enabled
    if (!patch.children.empty() && patch.enabled) {
        ImGui::Indent(16.0f);
        for (auto& child : patch.children) {
            ImGui::PushID(child.id.c_str());
            renderPatchRow(child);
            ImGui::PopID();
        }
        ImGui::Unindent(16.0f);
    }
}

static void selectGame(int gameIdx) {
    GameFamily prevFamily = g_app.family;
    g_app.gameIdx  = gameIdx;
    g_app.family   = games[gameIdx].family;
    if (g_app.family != prevFamily) {
        applyTheme(g_app.family);
    }
    std::string gameId = games[gameIdx].id;
    g_app.settings.gameSettings()["game_id"] = gameId;
    g_app.settings.gameSettings().erase("exe_name");
    if (gameId == "ez2dj_6th")
        g_app.settings.gameSettings()["patches"] = saveSixthPatchState(gameId);
    else
        g_app.settings.gameSettings()["patches"] = g_app.settings.patchStore().saveState(gameId);
    g_app.settings.save();
}

static void renderSettingsTab() {
    float availWidth = ImGui::GetContentRegionAvail().x;

    // Game Family dropdown
    ImGui::TextUnformatted("Game");
    int familyIdx = static_cast<int>(g_app.family);
    ImGui::SetNextItemWidth(availWidth * 0.35f);
    if (ImGui::Combo("##family", &familyIdx, gameFamilyNames, GAME_FAMILY_COUNT)) {
        GameFamily newFamily = static_cast<GameFamily>(familyIdx);
        if (newFamily != g_app.family) {
            // Switch to the first game in the new family
            selectGame(firstGameIndexForFamily(newFamily));
        }
    }

    // Version dropdown (only games from selected family)
    ImGui::SameLine();
    ImGui::SetNextItemWidth(availWidth * 0.35f);
    if (ImGui::BeginCombo("##version", games[g_app.gameIdx].name)) {
        for (int i = 0; i < GAME_COUNT; i++) {
            if (games[i].family != g_app.family) continue;
            bool isSelected = (g_app.gameIdx == i);
            if (ImGui::Selectable(games[i].name, isSelected)) {
                selectGame(i);
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    // Exe Name
    ImGui::TextUnformatted("Exe Name Override");
    ImGui::SetNextItemWidth(-1);
    {
        static char exeNameBuffer[MAX_PATH] = {};
        static int  previousGameIndex = -1;
        if (previousGameIndex != g_app.gameIdx) {
            previousGameIndex = g_app.gameIdx;
            std::string stored = g_app.settings.gameSettings().value("exe_name", "");
            strncpy(exeNameBuffer, stored.c_str(), MAX_PATH - 1);
            exeNameBuffer[MAX_PATH - 1] = '\0';
        }
        const char* defaultExeHint = games[g_app.gameIdx].defaultExeName;
        if (ImGui::InputTextWithHint("##exe_name", defaultExeHint, exeNameBuffer, MAX_PATH)) {
            if (exeNameBuffer[0]) {
                g_app.settings.gameSettings()["exe_name"] = std::string(exeNameBuffer);
            } else {
                g_app.settings.gameSettings().erase("exe_name");
            }
            g_app.settings.save();
        }
    }

    {
        static char extraDllsBuffer[2048] = {};
        static int  previousGameIndexDlls = -1;
        if (previousGameIndexDlls != g_app.gameIdx) {
            previousGameIndexDlls = g_app.gameIdx;
            std::string stored = g_app.settings.gameSettings().value("extra_dlls", "");
            strncpy(extraDllsBuffer, stored.c_str(), sizeof(extraDllsBuffer) - 1);
            extraDllsBuffer[sizeof(extraDllsBuffer) - 1] = '\0';
        }
        ImGui::TextUnformatted("Extra DLLs");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##extra_dlls", "eg: mod1.dll mod2.dll", extraDllsBuffer, sizeof(extraDllsBuffer))) {
            if (extraDllsBuffer[0]) {
                g_app.settings.gameSettings()["extra_dlls"] = std::string(extraDllsBuffer);
            } else {
                g_app.settings.gameSettings().erase("extra_dlls");
            }
            g_app.settings.save();
        }
        ImGui::TextDisabled("Space-separated DLLs injected with 2EZ.dll");
    }

    ImGui::SeparatorText("Global Settings");
    globalCheckbox("Enable IO Emulation",                "io_emu",       true);
    globalCheckbox("Force High Priority (experimental)", "high_priority", false);

    ImGui::SeparatorText("Debug Settings");
    gameCheckbox("Enable Logging", "logging_enabled", false);
    {
        bool loggingOn = g_app.settings.gameSettings().value("logging_enabled", false);
        if (loggingOn){
            ImGui::Indent();
            gameCheckbox("Verbose Output Port Logging", "verbose_output_logging", false);
            ImGui::Unindent();
        }
    }

    ImGui::SeparatorText("Game Patches");
    renderPatchesTab();
}

static void renderButtonsTab() {
    enum BindState { BindState_Normal, BindState_Listening };
    static BindState s_state      = BindState_Normal;
    static BindState s_prevState  = BindState_Normal;
    static int       s_listenIdx  = -1;
    static float     s_listenTimer = 0.0f;
    static bool      s_prevKeys[256] = {};

    if (s_state != s_prevState) {
        if (s_state == BindState_Listening) {
            g_app.input->startCapture();
        } else {
            g_app.input->stopCapture();
        }
        s_prevState = s_state;
    }

    const char** actionList;
    int          actionCount;
    switch (g_app.family) {
        case GameFamily::EZ2Dancer: actionList = dancerButtonNames; actionCount = BindingStore::DANCER_COUNT; break;
        case GameFamily::SabinSS:   actionList = sabinButtonNames;  actionCount = BindingStore::SABIN_BUTTON_COUNT; break;
        default:                    actionList = djButtonNames;     actionCount = BindingStore::BUTTON_COUNT; break;
    }

    ImGui::BeginChild("##buttonsScroll", ImVec2(0, ImGui::GetWindowHeight() - 85), false);
    if (ImGui::BeginTable("##buttonable", 3, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Button",  ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < actionCount; i++) {
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ButtonBinding& bnd = (g_app.family == GameFamily::EZ2Dancer) ? g_app.bindings.dancerButtons[i]
                               : (g_app.family == GameFamily::SabinSS) ? g_app.bindings.sabinButtons[i]
                               : g_app.bindings.buttons[i];

            if (s_state == BindState_Listening && s_listenIdx == i) {
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ACCENT_ACTIVE, "Press a button...");
                ImGui::TableSetColumnIndex(1);
                char timerDisplay[16]; snprintf(timerDisplay, sizeof(timerDisplay), "(%.1fs)", s_listenTimer);
                ImGui::TextUnformatted(timerDisplay);
                ImGui::TableSetColumnIndex(2);
                if (ImGui::Button("Cancel")) {
                    g_app.input->stopCapture();
                    s_state     = BindState_Normal;
                    s_listenIdx = -1;
                }

                CaptureResult capturedInput;
                if (g_app.input->pollCapture(capturedInput)) {
                    bnd = ButtonBinding::fromCapture(capturedInput);
                    g_app.bindings.save(g_app.settings);
                    s_state     = BindState_Normal;
                    s_listenIdx = -1;
                }

                if (s_state == BindState_Listening) {
                    int virtualKey = pollKeyboardPress(s_prevKeys);
                    if (virtualKey >= 0) {
                        ButtonBinding keyBinding;
                        keyBinding.vkCode = virtualKey;
                        bnd = keyBinding;
                        g_app.bindings.save(g_app.settings);
                        g_app.input->stopCapture();
                        s_state     = BindState_Normal;
                        s_listenIdx = -1;
                    }
                }

                s_listenTimer -= ImGui::GetIO().DeltaTime;
                if (s_listenTimer <= 0.0f) {
                    g_app.input->stopCapture();
                    s_state = BindState_Normal;
                }
            } else {
                ImGui::TableSetColumnIndex(0);

                bool isButtonHeld = g_app.bindings.isHeld(bnd);

                if (isButtonHeld) {
                    ImGui::TextColored(ACCENT_ACTIVE, "%s:", actionList[i]);
                } else {
                    ImGui::Text("%s:", actionList[i]);
                }

                ImGui::TableSetColumnIndex(1);

                if (isButtonHeld) {
                    ImGui::TextColored(ACCENT_ACTIVE, "%s", g_app.bindings.getDisplayString(bnd).c_str());
                } else {
                    ImGui::TextUnformatted(g_app.bindings.getDisplayString(bnd).c_str());
                }

                ImGui::TableSetColumnIndex(2);

                if (ImGui::Button("Bind")) {
                    s_state       = BindState_Listening;
                    s_listenIdx   = i;
                    s_listenTimer = 5.0f;
                    // Prime prev keys to avoid spurious immediate trigger
                    for (int virtualKey = 0; virtualKey < 256; virtualKey++) {
                        s_prevKeys[virtualKey] = (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
                    }
                }
                if (bnd.isSet()) {
                    ImGui::SameLine();
                    if (ImGui::Button("Clear")) {
                        bnd.clear();
                        g_app.bindings.save(g_app.settings);
                    }
                }
            }

            ImGui::PopID();
        }
    }
    ImGui::EndTable();
    ImGui::EndChild();
}

static void renderAnalogsTab() {
    // Build device list for combo: only Generic Desktop (page 0x01)
    // devices with axes. Excludes consumer control (0x0C), system
    // control, and other non-joystick HID collections.
    std::vector<Device> allDevices = g_app.input->getDevices();
    std::vector<Device> axisDevices;
    for (auto& device : allDevices) {
        if (!device.valueCapsNames.empty() && device.hid && device.hid->caps.UsagePage == 0x01) {
            axisDevices.push_back(device);
        }
    }

    if (ImGui::BeginTable("##analogTable", 3, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Turntable", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Binding",   ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Actions",   ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableHeadersRow();

        for (int port = 0; port < BindingStore::ANALOG_COUNT; port++) {
            ImGui::PushID(port);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("P%d", port+1);
            AnalogBinding& analogBinding = g_app.bindings.analogs[port];
            if (analogBinding.isSet() || analogBinding.hasVtt()) {
                ImGui::SameLine();
                float display = g_app.bindings.getAnalogPosition(analogBinding, g_app.bindings.getVttPosition(port), g_app.input->getMousePosition(port)) / 255.0f;
                ImGui::ProgressBar(display, ImVec2(40.0f, 0));
            }

            ImGui::TableSetColumnIndex(1);
            if (!analogBinding.isSet() && analogBinding.hasVtt()) {
                ImGui::TextUnformatted("Virtual TT keys Assigned");
            } else {
                ImGui::TextUnformatted(g_app.bindings.getDisplayString(analogBinding).c_str());
            }

            ImGui::TableSetColumnIndex(2);
            if (ImGui::Button("Edit")) {
                s_editPort       = port;
                s_openPopup      = true;
                s_initialized[port] = false;
            }
            if (analogBinding.isSet() || analogBinding.hasVtt()) {
                ImGui::SameLine();
                if (ImGui::Button("Clear")) {
                    analogBinding.clear();
                    g_app.input->setMouseBinding(port, "", -1, 5);
                    g_app.bindings.save(g_app.settings);
                }
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Open popup outside PushID scope
    if (s_openPopup) {
        ImGui::OpenPopup("EditAnalog");
        s_openPopup = false;
    }
    renderAnalogEditPopup(axisDevices);
}

static void renderAnalogEditPopup(const std::vector<Device>& axisDevices) {
    if (!ImGui::BeginPopupModal("EditAnalog", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    int port = s_editPort;
    if (port >= 0 && port < BindingStore::ANALOG_COUNT) {
        ImGui::PushID(port);
        ImGui::Text("Editing: %s", analogNames[port]);
        ImGui::Separator();
        AnalogBinding& analogBinding = g_app.bindings.analogs[port];

        // Get mouse devices for the combo.
        auto mouseDevices = g_app.input->getMouseDevices();
        std::vector<std::string> mouseLabels;
        for (auto& m : mouseDevices) {
            mouseLabels.push_back(m.name);
        }
        int firstMouseIdx = 1 + static_cast<int>(axisDevices.size());

        if (!s_initialized[port]) {
            s_initialized[port] = true;
            s_devIdx[port] = 0;
            s_axisIdx[port] = 0;
            if (analogBinding.hasMouse()) {
                for (int mi = 0; mi < static_cast<int>(mouseDevices.size()); mi++) {
                    if (mouseDevices[mi].path == analogBinding.mousePath) {
                        s_devIdx[port] = firstMouseIdx + mi;
                        s_axisIdx[port] = (analogBinding.mouseAxis >= 0) ? analogBinding.mouseAxis : 0;
                        break;
                    }
                }
            } else if (!analogBinding.devicePath.empty() && analogBinding.axisIdx >= 0) {
                for (int di = 0; di < static_cast<int>(axisDevices.size()); di++) {
                    if (axisDevices[di].path == analogBinding.devicePath) {
                        s_devIdx[port]  = di + 1;
                        s_axisIdx[port] = (analogBinding.axisIdx >= 0) ? analogBinding.axisIdx : 0;
                        break;
                    }
                }
            }
        }

        // Build combined device labels: (none), [HID devices...], [mice...]
        std::vector<const char*> deviceLabels;
        deviceLabels.push_back("(none)");
        for (auto& device : axisDevices) {
            deviceLabels.push_back(device.name.c_str());
        }
        for (auto& ml : mouseLabels) {
            deviceLabels.push_back(ml.c_str());
        }

        if (ImGui::Combo("Device", &s_devIdx[port], deviceLabels.data(), static_cast<int>(deviceLabels.size()))) {
            s_axisIdx[port] = 0;
            if (s_devIdx[port] == 0) {
                // (none) — clear both HID and mouse
                analogBinding.devicePath.clear();
                analogBinding.deviceName.clear();
                analogBinding.axisIdx = -1;
                analogBinding.mousePath.clear();
                analogBinding.mouseName.clear();
                analogBinding.mouseAxis = -1;
                g_app.input->setMouseBinding(port, "", -1, 5);
            } else if (s_devIdx[port] >= firstMouseIdx) {
                // Mouse selected — clear HID fields, set mouse
                analogBinding.devicePath.clear();
                analogBinding.deviceName.clear();
                analogBinding.axisIdx = -1;
                int mi = s_devIdx[port] - firstMouseIdx;
                analogBinding.mousePath = mouseDevices[mi].path;
                analogBinding.mouseName = mouseDevices[mi].name;
                analogBinding.mouseAxis = 0;
                g_app.input->setMouseBinding(port, analogBinding.mousePath, analogBinding.mouseAxis, analogBinding.mouseSensitivity);
            } else {
                // HID device selected — clear mouse fields
                const Device& device = axisDevices[static_cast<size_t>(s_devIdx[port] - 1)];
                analogBinding.devicePath = device.path;
                analogBinding.deviceName = device.name;
                analogBinding.axisIdx = 0;
                analogBinding.mousePath.clear();
                analogBinding.mouseName.clear();
                analogBinding.mouseAxis = -1;
                g_app.input->setMouseBinding(port, "", -1, 5);
            }
            g_app.bindings.save(g_app.settings);
        }

        bool isMouse = (s_devIdx[port] >= firstMouseIdx);
        bool isHid = (s_devIdx[port] > 0 && !isMouse);

        if (isMouse) {
            // Mouse axis combo: X / Y
            const char* mouseAxes[] = { "X", "Y" };
            if (ImGui::Combo("Axis", &s_axisIdx[port], mouseAxes, 2)) {
                analogBinding.mouseAxis = s_axisIdx[port];
                g_app.input->setMouseBinding(port, analogBinding.mousePath, analogBinding.mouseAxis, analogBinding.mouseSensitivity);
                g_app.bindings.save(g_app.settings);
            }
            if (ImGui::SliderInt("Sensitivity", &analogBinding.mouseSensitivity, 1, TT_MAX_SENSE)) {
                g_app.input->setMouseBinding(port, analogBinding.mousePath, analogBinding.mouseAxis, analogBinding.mouseSensitivity);
                g_app.bindings.save(g_app.settings);
            }
        } else if (isHid) {
            const Device& device = axisDevices[static_cast<size_t>(s_devIdx[port] - 1)];
            int axisCount = static_cast<int>(device.valueCapsNames.size());
            std::vector<const char*> axisLabels;
            for (auto& axisName : device.valueCapsNames) {
                axisLabels.push_back(axisName.c_str());
            }
            if (axisCount > 0 && ImGui::Combo("Axis", &s_axisIdx[port], axisLabels.data(), axisCount)) {
                analogBinding.axisIdx = s_axisIdx[port];
                g_app.bindings.save(g_app.settings);
            }
        } else {
            ImGui::TextDisabled("Axis: (select device first)");
        }

        if (ImGui::Checkbox("Reverse", &analogBinding.reverse)) {
            g_app.bindings.save(g_app.settings);
        }

        ImGui::Separator();

        renderVttKeyBind("Virtual TT-:", "Bind##vttm", "Clear##vttm",
                         analogBinding.vttMinus, s_capturingVtt[port][1], s_capturingVtt[port][0], s_vttPrevKeys);
        renderVttKeyBind("Virtual TT+:", "Bind##vttp", "Clear##vttp",
                         analogBinding.vttPlus,  s_capturingVtt[port][0], s_capturingVtt[port][1], s_vttPrevKeys);

        if (ImGui::SliderInt("Virtual TT Step Amount", &analogBinding.vttStep, 1, TT_MAX_SENSE)) {
            g_app.bindings.save(g_app.settings);
        }

        ImGui::Separator();

        {
            float normalizedPosition = 0.5f;
            char overlayText[32];
            if (analogBinding.isSet() || analogBinding.hasVtt()) {
                normalizedPosition = g_app.bindings.getAnalogPosition(analogBinding, g_app.bindings.getVttPosition(port), g_app.input->getMousePosition(port)) / 255.0f;
                snprintf(overlayText, sizeof(overlayText), "%.0f", normalizedPosition * 255.0f);
            } else {
                snprintf(overlayText, sizeof(overlayText), "(unbound)");
            }
            ImGui::ProgressBar(normalizedPosition, ImVec2(-0.5f, 0), overlayText);
        }

        ImGui::PopID();
    }
    ImGui::Separator();
    if (ImGui::Button("Close", ImVec2(120, 0))) {
        // Stop any VTT capture in progress
        for (int portIndex = 0; portIndex < 2; portIndex++) {
            for (int direction = 0; direction < 2; direction++) {
                if (s_capturingVtt[portIndex][direction]) {
                    g_app.input->stopCapture();
                    s_capturingVtt[portIndex][direction] = false;
                }
            }
        }
        ImGui::CloseCurrentPopup();
        s_editPort = -1;
    }
    ImGui::EndPopup();
}

static void renderLightsTab() {
    // Advance test timer — auto-turn-off after 0.5 seconds
    if (s_testTimer > 0.0f) {
        s_testTimer -= ImGui::GetIO().DeltaTime;
        if (s_testTimer <= 0.0f && !s_testPath.empty()) {
            g_app.input->setLight(s_testPath, s_testOutIdx, 0.0f);
            g_app.input->disableOutput(s_testPath);
        }
    }

    // Get output-capable devices for bind popup.
    std::vector<Device> allDevices = g_app.input->getDevices();
    std::vector<Device> outputDevices;
    for (auto& device : allDevices) {
        if (!device.buttonOutputCapsNames.empty() || !device.valueOutputCapsNames.empty()) {
            outputDevices.push_back(device);
        }
    }

    if(g_app.family == GameFamily::EZ2Dancer){
        ImGui::TextDisabled("EZ2Dancer player pads and hand sensor lights are still being researched.\nOnly simple cabinet lighting has been implemented for now.");
    }

    ImGui::BeginChild("##lightsScroll", ImVec2(0, ImGui::GetWindowHeight() - 85), false);
    if (ImGui::BeginTable("##lighttable", 3,
            ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Light",   ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableHeadersRow();

        const char** names;
        int count;
        LightBinding* lightArray;
        switch (g_app.family) {
            case GameFamily::EZ2Dancer:
                names = dancerLightNames; count = BindingStore::DANCER_LIGHT_COUNT; lightArray = g_app.bindings.dancerLights; break;
            case GameFamily::SabinSS:
                names = sabinLightNames;  count = BindingStore::SABIN_LIGHT_COUNT;  lightArray = g_app.bindings.sabinLights;  break;
            default:
                names = lightNames;       count = BindingStore::LIGHT_COUNT;        lightArray = g_app.bindings.lights;       break;
        }

        for (int i = 0; i < count; i++) {
            ImGui::PushID(i);
            ImGui::TableNextRow();

            LightBinding& lightBinding = lightArray[i];

            std::string lightLabel = g_app.bindings.getDisplayString(lightBinding);
            bool isTestActive = (s_testTimer > 0.0f && lightBinding.isSet()
                                && s_testPath == lightBinding.devicePath
                                && s_testOutIdx == lightBinding.outputIdx);

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(names[i]);

            ImGui::TableSetColumnIndex(1);
            if (isTestActive) {
                ImGui::TextColored(ACCENT_ACTIVE, "%s", lightLabel.c_str());
            } else {
                ImGui::TextUnformatted(lightLabel.c_str());
            }

            ImGui::TableSetColumnIndex(2);
            if (ImGui::Button("Bind")) {
                s_bindLightIdx = i;
                s_lightDevIdx  = 0;
                s_lightOutIdx  = -1;
                // Populate from current binding if set
                if (lightBinding.isSet()) {
                    for (int d = 0; d < static_cast<int>(outputDevices.size()); d++) {
                        if (outputDevices[d].path == lightBinding.devicePath) {
                            s_lightDevIdx = d + 1;  // +1 for "(none)" entry
                            s_lightOutIdx = lightBinding.outputIdx;
                            break;
                        }
                    }
                }
                s_openLightPopup = true;
            }

            if (lightBinding.isSet()) {
                ImGui::SameLine();
                if (ImGui::Button("Clear")) {
                    lightBinding.clear();
                    g_app.bindings.save(g_app.settings);
                }
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    if (s_openLightPopup) {
        ImGui::OpenPopup("BindLight");
        s_openLightPopup = false;
    }
    renderLightBindPopup(outputDevices);
}

static void renderLightBindPopup(const std::vector<Device>& outputDevices) {
    if (!ImGui::BeginPopupModal("BindLight", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const char** popupNames;
    switch (g_app.family) {
        case GameFamily::EZ2Dancer: popupNames = dancerLightNames; break;
        case GameFamily::SabinSS:   popupNames = sabinLightNames;  break;
        default:                    popupNames = lightNames;       break;
    }
    ImGui::Text("Bind Light: %s", (s_bindLightIdx >= 0 ? popupNames[s_bindLightIdx] : "?"));
    ImGui::Separator();

    std::vector<const char*> deviceLabels;
    deviceLabels.push_back("(none)");
    for (auto& device : outputDevices) {
        deviceLabels.push_back(device.name.c_str());
    }
    int previousDeviceIndex = s_lightDevIdx;
    ImGui::Combo("Device", &s_lightDevIdx, deviceLabels.data(), static_cast<int>(deviceLabels.size()));
    if (previousDeviceIndex != s_lightDevIdx) {
        s_lightOutIdx = -1;  // reset output on device change
    }

    // Output combo — flat: buttonOutputCapsNames then valueOutputCapsNames
    int previousOutputIndex = s_lightOutIdx;
    if (s_lightDevIdx > 0) {
        const Device& selectedDevice = outputDevices[s_lightDevIdx - 1];
        std::vector<const char*> outputLabels;
        for (auto& outputName : selectedDevice.buttonOutputCapsNames) {
            outputLabels.push_back(outputName.c_str());
        }
        for (auto& outputName : selectedDevice.valueOutputCapsNames) {
            outputLabels.push_back(outputName.c_str());
        }
        if (!outputLabels.empty()) {
            if (s_lightOutIdx < 0) {
                s_lightOutIdx = 0;  // auto-select first
            }
            ImGui::Combo("Output", &s_lightOutIdx, outputLabels.data(), static_cast<int>(outputLabels.size()));
        }
    } else {
        s_lightOutIdx = -1;
    }

    if ((previousDeviceIndex != s_lightDevIdx || previousOutputIndex != s_lightOutIdx)
        && s_lightDevIdx > 0 && s_lightOutIdx >= 0 && s_bindLightIdx >= 0) {
        LightBinding& lightBinding = (g_app.family == GameFamily::EZ2Dancer) ? g_app.bindings.dancerLights[s_bindLightIdx]
                                   : (g_app.family == GameFamily::SabinSS) ? g_app.bindings.sabinLights[s_bindLightIdx]
                                   : g_app.bindings.lights[s_bindLightIdx];
        lightBinding.devicePath = outputDevices[s_lightDevIdx - 1].path;
        lightBinding.deviceName = outputDevices[s_lightDevIdx - 1].name;
        lightBinding.outputIdx  = s_lightOutIdx;
        g_app.bindings.save(g_app.settings);
    }

    ImGui::Separator();

    // Test button — pulses light for 0.5 seconds then auto-off
    bool canTest = s_lightDevIdx > 0 && s_lightOutIdx >= 0;
    if (!canTest) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Test")) {
        s_testPath   = outputDevices[s_lightDevIdx - 1].path;
        s_testOutIdx = s_lightOutIdx;
        g_app.input->setLight(s_testPath, s_testOutIdx, 1.0f);
        s_testTimer  = 0.5f;
    }
    if (!canTest) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// Returns newly-pressed VK (ignoring mouse buttons), or -1 if none. Updates prevKeys[] in-place.
static int pollKeyboardPress(bool* prevKeys) {
    for (int virtualKey = 0x01; virtualKey < 0xFF; virtualKey++) {
        bool isPressed = (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
        if (isPressed && !prevKeys[virtualKey] && virtualKey != VK_LBUTTON && virtualKey != VK_RBUTTON && virtualKey != VK_MBUTTON) {
            prevKeys[virtualKey] = true;
            return virtualKey;
        }
        prevKeys[virtualKey] = isPressed;
    }
    return -1;
}

static void renderVttKeyBind(const char* label, const char* bindId, const char* clearId,
                             ButtonBinding& binding, bool& capturing, bool& otherCapturing, bool* prevKeys) {
    ImGui::Text("%s %s", label, g_app.bindings.getDisplayString(binding).c_str());
    ImGui::SameLine();
    if (capturing) {
        ImGui::TextColored(ACCENT_ACTIVE, "Press a button...");
        CaptureResult capturedInput;
        if (g_app.input->pollCapture(capturedInput)) {
            binding = ButtonBinding::fromCapture(capturedInput);
            g_app.bindings.save(g_app.settings);
            g_app.input->stopCapture();
            capturing = false;
        } else {
            int virtualKey = pollKeyboardPress(prevKeys);
            if (virtualKey >= 0) {
                binding.clear();
                binding.vkCode = virtualKey;
                g_app.bindings.save(g_app.settings);
                g_app.input->stopCapture();
                capturing = false;
            }
        }
    } else {
        if (ImGui::Button(bindId)) {
            otherCapturing = false;
            capturing = true;
            g_app.input->startCapture();
            for (int virtualKey = 0; virtualKey < 256; virtualKey++) {
                prevKeys[virtualKey] = (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
            }
        }
        if (binding.isSet()) {
            ImGui::SameLine();
            if (ImGui::Button(clearId)) {
                binding.clear();
                g_app.bindings.save(g_app.settings);
            }
        }
    }
}

static void globalCheckbox(const char* label, const char* key, bool defaultVal) {
    bool isEnabled = g_app.settings.globalSettings().value(key, defaultVal);
    if (ImGui::Checkbox(label, &isEnabled)) {
        g_app.settings.globalSettings()[key] = isEnabled;
        g_app.settings.save();
    }
}

static void gameCheckbox(const char* label, const char* key, bool defaultVal) {
    bool isEnabled = g_app.settings.gameSettings().value(key, defaultVal);
    if (ImGui::Checkbox(label, &isEnabled)) {
        g_app.settings.gameSettings()[key] = isEnabled;
        g_app.settings.save();
    }
}

// Launch / cleanup helpers

static std::string resolveActiveExeName() {
    const char* defaultExe = games[g_app.gameIdx].defaultExeName;
    std::string exeOverride = g_app.settings.gameSettings().value("exe_name", "");
    return exeOverride.empty() ? defaultExe : exeOverride;
}

static std::vector<std::string> parseExtraDlls() {
    std::vector<std::string> dlls;
    std::string str = g_app.settings.gameSettings().value("extra_dlls", "");
    std::istringstream iss(str);
    std::string dll;
    while (iss >> dll) dlls.push_back(dll);
    return dlls;
}

static void launchGame() {
    std::string exe = resolveActiveExeName();
    auto extraDlls = parseExtraDlls();
    std::string gameId = g_app.settings.gameSettings().value("game_id", "");
    if (gameId == "ez2dj_6th") {
        s_sixthMode = true;
        s_sixthLauncherExe = exe;
        s_sixthExtraDlls = extraDlls;
    } else {
        Injector::LaunchAndInject(exe.c_str(), extraDlls);
    }
}

static void cleanupUI() {
    g_app.bindings.stopVttThread();
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(g_window);
    glfwTerminate();
    delete g_app.input;
    g_app.input = nullptr;
}

static int sixthBackgroundLoop(const char* launcherExe, const std::vector<std::string>& extraDlls) {
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char* lastSlash = strrchr(exePath, '\\');
    std::string exeDir = lastSlash ? std::string(exePath, lastSlash) : ".";
    std::string dllFullPath = exeDir + "\\2EZ.dll";
    Injector::LaunchAndInject(launcherExe, extraDlls);

    DWORD lastInjected6th = 0;
    DWORD lastInjected1st = 0;
    DWORD lastSeenTime = GetTickCount();

    while (true) {
        Sleep(50);

        bool seen = false;
        DWORD pid6th = Injector::FindProcess("EZ2DJ6th.exe");
        if (pid6th && pid6th != lastInjected6th) {
            Injector::InjectRunningProcess("EZ2DJ6th.exe", dllFullPath.c_str());
            lastInjected6th = pid6th;
        }
        if (pid6th) seen = true;

        DWORD pid1st = Injector::FindProcess("EZ2DJ.exe");
        if (pid1st && pid1st != lastInjected1st) {
            Injector::InjectRunningProcess("EZ2DJ.exe", dllFullPath.c_str());
            lastInjected1st = pid1st;
        }
        if (pid1st) seen = true;

        if (seen) {
            lastSeenTime = GetTickCount();
        } else if (GetTickCount() - lastSeenTime > 2000) {
            break;
        }
    }
    return 0;
}

static void renderRemember1stPatches() {
    auto& r1stPatches = g_app.settings.patchStore().patchesForGame("rmbr_1st");
    if (r1stPatches.empty()){
        return;
    }

    ImGui::TextDisabled("Remember 1st Patches");
    for (auto& patch : r1stPatches) {
        ImGui::PushID(patch.id.c_str());
        renderPatchRow(patch);
        ImGui::PopID();
    }
}

static nlohmann::json saveSixthPatchState(const std::string& gameId) {
    auto patchState = g_app.settings.patchStore().saveState(gameId);
    auto r1st = g_app.settings.patchStore().saveState("rmbr_1st");
    if (r1st.contains("rmbr_1st")) patchState["rmbr_1st"] = r1st["rmbr_1st"];
    return patchState;
}

// End Launch / cleanup helpers


static void setTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding    = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.PopupBorderSize  = 1.0f;
    style.GrabRounding     = 4.0f;
    style.TabRounding      = 4.0f;

    // Fixed colours (not theme-dependent)
    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_ChildBg]              = { 0, 0, 0, 0 };
    colors[ImGuiCol_BorderShadow]         = { 0, 0, 0, 0 };

    // Apply initial theme colours
    applyTheme(g_app.family);
}