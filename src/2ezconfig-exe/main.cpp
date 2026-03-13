#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
#include "injector.h"
#include "settings.h"
#include "game_defs.h"
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

static const char* s_buildDate = BUILD_DATE;

struct AppState {
    SettingsManager settings;
    InputManager* input = nullptr;
    BindingStore bindings;
    int gameIdx = 0;
    bool isDancer = false;
    uint8_t vttPos[BindingStore::ANALOG_COUNT] = {128, 128};
};
static AppState g_app;
static GLFWwindow* g_window = nullptr;

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
    ImGui::GetIO().IniFilename = nullptr;

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

    std::string gid = g_app.settings.gameSettings().value("game_id", "ez2dj_1st");
    static const int DJ_COUNT_M     = static_cast<int>(sizeof(djGames)     / sizeof(djGames[0]));
    static const int DANCER_COUNT_M = static_cast<int>(sizeof(dancerGames) / sizeof(dancerGames[0]));
    g_app.gameIdx  = 0;
    g_app.isDancer = false;
    bool found = false;
    for (int i = 0; !found && i < DJ_COUNT_M; i++) {
        if (gid == djGames[i].id) {
            g_app.gameIdx = i;
            found = true;
        }
    }
    for (int i = 0; !found && i < DANCER_COUNT_M; i++) {
        if (gid == dancerGames[i].id) {
            g_app.gameIdx = DJ_COUNT_M + i;
            g_app.isDancer = true;
            found = true;
        }
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
        glClearColor(0.07f, 0.07f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(g_window);
    }

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(g_window);
    glfwTerminate();

    delete g_app.input;
    g_app.input = nullptr;

    return 0;
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
    // Get current exeName at frame time — use exe_name override if set in game-settings
    static const int DJ_COUNT_TB = static_cast<int>(sizeof(djGames) / sizeof(djGames[0]));
    const char* defaultExe = (g_app.gameIdx >= DJ_COUNT_TB) ? "EZ2Dancer.exe" : djGames[g_app.gameIdx].defaultExeName;
    std::string exeOverride = g_app.settings.gameSettings().value("exe_name", "");
    const char* activeExeName   = exeOverride.empty() ? defaultExe : exeOverride.c_str();
    if (ImGui::Button("Play EZ2", ImVec2(playButtonWidth, 0))) {
        std::vector<std::string> extraDlls;
        std::string extraDllsStr = g_app.settings.gameSettings().value("extra_dlls", "");
        std::istringstream iss(extraDllsStr);
        std::string dllPath;
        while (iss >> dllPath) {
            extraDlls.push_back(dllPath);
        }
        Injector::LaunchAndInject(activeExeName, extraDlls);
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

        if (!g_app.isDancer) {
            if (ImGui::BeginTabItem("Analogs")) {
                renderAnalogsTab();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Lights")) {
                renderLightsTab();
                ImGui::EndTabItem();
            }
        }

        ImGui::EndTabBar();
    }

    ImGui::SetCursorPos({ ImGui::GetWindowWidth() - 280, ImGui::GetWindowHeight() - 20 });
    ImGui::TextDisabled("Made by kasaski (kissAss) - %s",  s_buildDate);

    ImGui::End();
}

static void renderPatchesTab() {
    std::string gameId = g_app.settings.gameSettings().value("game_id", "");
    auto& patches = g_app.settings.patchStore().patchesForGame(gameId);

    if (patches.empty()) {
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

static void renderSettingsTab() {
    static const int DJ_COUNT     = static_cast<int>(sizeof(djGames)     / sizeof(djGames[0]));
    static const int DANCER_COUNT = static_cast<int>(sizeof(dancerGames) / sizeof(dancerGames[0]));
    static const int TOTAL_COUNT  = DJ_COUNT + DANCER_COUNT;

    static const char* gameComboItems[DJ_COUNT + DANCER_COUNT];
    static bool        gameComboBuilt = false;
    if (!gameComboBuilt) {
        for (int i = 0; i < DJ_COUNT; i++) {
            gameComboItems[i] = djGames[i].name;
        }
        for (int i = 0; i < DANCER_COUNT; i++) {
            gameComboItems[DJ_COUNT + i] = dancerGames[i].name;
        }
        gameComboBuilt = true;
    }

    if (ImGui::Combo("Game", &g_app.gameIdx, gameComboItems, TOTAL_COUNT)) {
        g_app.isDancer = (g_app.gameIdx >= DJ_COUNT);
        std::string gameId;
        if (g_app.isDancer) {
            int dancerIdx = g_app.gameIdx - DJ_COUNT;
            gameId = dancerGames[dancerIdx].id;
        } else {
            gameId = djGames[g_app.gameIdx].id;
        }
        g_app.settings.gameSettings()["game_id"] = gameId;
        g_app.settings.gameSettings().erase("exe_name");  // reset override when game changes
        g_app.settings.gameSettings()["patches"] = g_app.settings.patchStore().saveState(gameId);
        g_app.settings.save();
    }

    {
        static char exeNameBuffer[MAX_PATH] = {};
        static int  previousGameIndex = -1;
        if (previousGameIndex != g_app.gameIdx) {
            previousGameIndex = g_app.gameIdx;
            std::string stored = g_app.settings.gameSettings().value("exe_name", "");
            strncpy(exeNameBuffer, stored.c_str(), MAX_PATH - 1);
            exeNameBuffer[MAX_PATH - 1] = '\0';
        }
        if (ImGui::InputText("Exe Name", exeNameBuffer, MAX_PATH)) {
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
        if (ImGui::InputText("Extra DLLs", extraDllsBuffer, sizeof(extraDllsBuffer))) {
            if (extraDllsBuffer[0]) {
                g_app.settings.gameSettings()["extra_dlls"] = std::string(extraDllsBuffer);
            } else {
                g_app.settings.gameSettings().erase("extra_dlls");
            }
            g_app.settings.save();
        }
        ImGui::TextDisabled("Space-separated DLL paths injected after 2EZ.dll");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Global Settings");
    ImGui::Separator();
    globalCheckbox("Enable IO Emulation",                "io_emu",       true);
    globalCheckbox("Force High Priority (experimental)", "high_priority", false);

    ImGui::Separator();
    ImGui::TextUnformatted("Debug");
    ImGui::Separator();
    gameCheckbox("Enable Logging", "logging_enabled", false);

    ImGui::Separator();
    ImGui::TextUnformatted("Game Patches");
    ImGui::Separator();
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

    const char** actionList  = g_app.isDancer ? dancerButtonNames : djButtonNames;
    const int    actionCount = g_app.isDancer ? BindingStore::DANCER_COUNT : BindingStore::BUTTON_COUNT;

    ImGui::BeginChild("##buttonsScroll", ImVec2(0, ImGui::GetWindowHeight() - 85), false);
    if (ImGui::BeginTable("##buttonable", 3, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Button",  ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < actionCount; i++) {
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ButtonBinding& bnd = g_app.isDancer ? g_app.bindings.dancerButtons[i] : g_app.bindings.buttons[i];

            if (s_state == BindState_Listening && s_listenIdx == i) {
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImVec4(1,1,0,1), "Press a button...");
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
                    ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "%s:", actionList[i]);
                } else {
                    ImGui::Text("%s:", actionList[i]);
                }

                ImGui::TableSetColumnIndex(1);

                if (isButtonHeld) {
                    ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "%s", g_app.bindings.getDisplayString(bnd).c_str());
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
    // Poll VTT keys each frame to drive the live preview
    for (int port = 0; port < BindingStore::ANALOG_COUNT; port++) {
        const AnalogBinding& analogBinding = g_app.bindings.analogs[port];
        if (analogBinding.vttPlus.isSet()  && g_app.bindings.isHeld(analogBinding.vttPlus)) {
            g_app.vttPos[port] = static_cast<uint8_t>((static_cast<int>(g_app.vttPos[port]) + analogBinding.vttStep) & 0xFF);
        }
        if (analogBinding.vttMinus.isSet() && g_app.bindings.isHeld(analogBinding.vttMinus)) {
            g_app.vttPos[port] = static_cast<uint8_t>((static_cast<int>(g_app.vttPos[port]) - analogBinding.vttStep + 256) & 0xFF);
        }
    }

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
                float display = g_app.bindings.getAnalogPosition(analogBinding, g_app.vttPos[port]) / 255.0f;
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
                    g_app.vttPos[port] = 128;
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

        if (!s_initialized[port]) {
            s_initialized[port] = true;
            s_devIdx[port] = 0;
            s_axisIdx[port] = 0;
            if (analogBinding.isSet()) {
                for (int di = 0; di < static_cast<int>(axisDevices.size()); di++) {
                    if (axisDevices[di].path == analogBinding.devicePath) {
                        s_devIdx[port]  = di + 1;  // +1 for "(none)" at index 0
                        s_axisIdx[port] = (analogBinding.axisIdx >= 0) ? analogBinding.axisIdx : 0;
                        break;
                    }
                }
            }
        }

        std::vector<const char*> deviceLabels;
        deviceLabels.push_back("(none)");
        for (auto& device : axisDevices) {
            deviceLabels.push_back(device.name.c_str());
        }
        if (ImGui::Combo("Device", &s_devIdx[port], deviceLabels.data(), static_cast<int>(deviceLabels.size()))) {
            s_axisIdx[port] = 0;
            if (s_devIdx[port] > 0) {
                const Device& device = axisDevices[static_cast<size_t>(s_devIdx[port] - 1)];
                analogBinding.devicePath = device.path;
                analogBinding.deviceName = device.name;
                analogBinding.axisIdx = 0;
            } else {
                analogBinding.devicePath.clear();
                analogBinding.deviceName.clear();
                analogBinding.axisIdx = -1;
            }
            g_app.bindings.save(g_app.settings);
        }

        if (s_devIdx[port] > 0) {
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

        if (ImGui::SliderInt("Virtual TT Step Amount", &analogBinding.vttStep, 1, 10)) {
            g_app.bindings.save(g_app.settings);
        }

        ImGui::Separator();

        {
            float normalizedPosition = 0.5f;
            char overlayText[32];
            if (analogBinding.isSet() || analogBinding.hasVtt()) {
                normalizedPosition = g_app.bindings.getAnalogPosition(analogBinding, g_app.vttPos[port]) / 255.0f;
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

    ImGui::BeginChild("##lightsScroll", ImVec2(0, ImGui::GetWindowHeight() - 85), false);
    if (ImGui::BeginTable("##lighttable", 3,
            ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Light",   ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < BindingStore::LIGHT_COUNT; i++) {
            ImGui::PushID(i);
            ImGui::TableNextRow();

            LightBinding& lightBinding = g_app.bindings.lights[i];

            std::string lightLabel = g_app.bindings.getDisplayString(lightBinding);
            bool isTestActive = (s_testTimer > 0.0f && lightBinding.isSet()
                                && s_testPath == lightBinding.devicePath
                                && s_testOutIdx == lightBinding.outputIdx);

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(lightNames[i]);

            ImGui::TableSetColumnIndex(1);
            if (isTestActive) {
                ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "%s", lightLabel.c_str());
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

    ImGui::Text("Bind Light: %s", (s_bindLightIdx >= 0 ? lightNames[s_bindLightIdx] : "?"));
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
        LightBinding& lightBinding = g_app.bindings.lights[s_bindLightIdx];
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
        ImGui::TextColored(ImVec4(1,1,0,1), "[Press button or key...]");
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

static void setTheme() {
    // Night Traveller theme
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding    = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.PopupBorderSize  = 1.0f;
    style.GrabRounding     = 4.0f;
    style.TabRounding      = 4.0f;

    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text]                 = { 0.92f, 0.91f, 0.88f, 1.00f };  // warm off-white
    colors[ImGuiCol_TextDisabled]         = { 0.42f, 0.41f, 0.38f, 1.00f };  // mid grey
    colors[ImGuiCol_WindowBg]             = { 0.07f, 0.07f, 0.07f, 0.97f };  // near black
    colors[ImGuiCol_ChildBg]              = { 0.00f, 0.00f, 0.00f, 0.00f };
    colors[ImGuiCol_PopupBg]              = { 0.09f, 0.09f, 0.09f, 0.97f };
    colors[ImGuiCol_Border]               = { 0.25f, 0.23f, 0.20f, 0.60f };  // warm dark grey
    colors[ImGuiCol_BorderShadow]         = { 0.00f, 0.00f, 0.00f, 0.00f };
    colors[ImGuiCol_FrameBg]              = { 0.16f, 0.15f, 0.13f, 1.00f };  // dark charcoal
    colors[ImGuiCol_FrameBgHovered]       = { 0.24f, 0.22f, 0.18f, 1.00f };
    colors[ImGuiCol_FrameBgActive]        = { 0.30f, 0.27f, 0.22f, 1.00f };
    colors[ImGuiCol_TitleBg]              = { 0.10f, 0.07f, 0.02f, 1.00f };  // very dark orange
    colors[ImGuiCol_TitleBgActive]        = { 0.22f, 0.13f, 0.02f, 1.00f };
    colors[ImGuiCol_TitleBgCollapsed]     = { 0.10f, 0.07f, 0.02f, 0.80f };
    colors[ImGuiCol_MenuBarBg]            = { 0.10f, 0.07f, 0.02f, 1.00f };
    colors[ImGuiCol_ScrollbarBg]          = { 0.02f, 0.02f, 0.02f, 0.53f };
    colors[ImGuiCol_ScrollbarGrab]        = { 0.32f, 0.32f, 0.30f, 1.00f };
    colors[ImGuiCol_ScrollbarGrabHovered] = { 0.44f, 0.42f, 0.38f, 1.00f };
    colors[ImGuiCol_ScrollbarGrabActive]  = { 0.83f, 0.44f, 0.10f, 1.00f };  // orange on active
    colors[ImGuiCol_CheckMark]            = { 0.88f, 0.50f, 0.10f, 1.00f };  // orange
    colors[ImGuiCol_SliderGrab]           = { 0.83f, 0.44f, 0.10f, 1.00f };  // orange
    colors[ImGuiCol_SliderGrabActive]     = { 1.00f, 0.60f, 0.20f, 1.00f };
    colors[ImGuiCol_Button]               = { 0.83f, 0.44f, 0.10f, 0.30f };
    colors[ImGuiCol_ButtonHovered]        = { 0.88f, 0.50f, 0.12f, 0.75f };
    colors[ImGuiCol_ButtonActive]         = { 1.00f, 0.58f, 0.15f, 1.00f };
    colors[ImGuiCol_Header]               = { 0.55f, 0.28f, 0.04f, 0.75f };  // dark orange
    colors[ImGuiCol_HeaderHovered]        = { 0.83f, 0.44f, 0.10f, 0.75f };
    colors[ImGuiCol_HeaderActive]         = { 0.90f, 0.52f, 0.12f, 1.00f };
    colors[ImGuiCol_Separator]            = { 0.28f, 0.26f, 0.22f, 0.70f };
    colors[ImGuiCol_SeparatorHovered]     = { 0.83f, 0.44f, 0.10f, 0.80f };
    colors[ImGuiCol_SeparatorActive]      = { 0.90f, 0.52f, 0.12f, 1.00f };
    colors[ImGuiCol_ResizeGrip]           = { 0.83f, 0.44f, 0.10f, 0.20f };
    colors[ImGuiCol_ResizeGripHovered]    = { 0.83f, 0.44f, 0.10f, 0.65f };
    colors[ImGuiCol_ResizeGripActive]     = { 1.00f, 0.58f, 0.15f, 0.95f };
    colors[ImGuiCol_Tab]                  = { 0.40f, 0.22f, 0.05f, 1.00f };  // visible dark orange
    colors[ImGuiCol_TabHovered]           = { 0.90f, 0.50f, 0.12f, 1.00f };
    colors[ImGuiCol_TabActive]            = { 0.78f, 0.42f, 0.09f, 1.00f };  // bright active orange
    colors[ImGuiCol_TabUnfocused]         = { 0.25f, 0.14f, 0.03f, 1.00f };
    colors[ImGuiCol_TabUnfocusedActive]   = { 0.48f, 0.26f, 0.06f, 1.00f };
    colors[ImGuiCol_PlotLines]            = { 0.55f, 0.55f, 0.52f, 1.00f };
    colors[ImGuiCol_PlotLinesHovered]     = { 1.00f, 0.58f, 0.15f, 1.00f };
    colors[ImGuiCol_PlotHistogram]        = { 0.83f, 0.44f, 0.10f, 1.00f };
    colors[ImGuiCol_PlotHistogramHovered] = { 1.00f, 0.60f, 0.20f, 1.00f };
    colors[ImGuiCol_TextSelectedBg]       = { 0.83f, 0.44f, 0.10f, 0.35f };
    colors[ImGuiCol_DragDropTarget]       = { 1.00f, 0.58f, 0.15f, 0.90f };
    colors[ImGuiCol_NavHighlight]         = { 0.83f, 0.44f, 0.10f, 1.00f };
    colors[ImGuiCol_NavWindowingHighlight]= { 1.00f, 1.00f, 1.00f, 0.70f };
    colors[ImGuiCol_NavWindowingDimBg]    = { 0.80f, 0.80f, 0.80f, 0.20f };
    colors[ImGuiCol_ModalWindowDimBg]     = { 0.00f, 0.00f, 0.00f, 0.65f };
}
