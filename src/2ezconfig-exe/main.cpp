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
    char buf[MAX_PATH] = {};
    if (GetEnvironmentVariableA("APPDATA", buf, MAX_PATH))
        return std::string(buf) + "\\2ezconfig";
    return ".";
}

static void renderUI();
static void setTheme();
static void renderSettingsTab();
static void renderButtonsTab();
static void renderAnalogsTab();
static void renderAnalogEditPopup(const std::vector<Device>& axisDevs);
static void renderLightsTab();
static void renderLightBindPopup(const std::vector<Device>& outputDevs);
static void renderPatchesTab();
static void renderPatchRow(Patch& patch);
static int  pollKeyboardPress(bool* prevKeys);
static void renderVttKeyBind(const char* label, const char* bindId, const char* clearId,
                             ButtonBinding& key, bool& capturing, bool& otherCapturing, bool* prevKeys);
static void globalCheckbox(const char* label, const char* key, bool defaultVal);

// All app-wide state grouped in one place
struct AppState {
    SettingsManager settings;
    InputManager*   input    = nullptr;
    BindingStore    bindings;
    int             gameIdx  = 0;
    bool            isDancer = false;
    uint8_t         vtt_pos[2] = {128, 128};
};
static AppState    g_app;
static GLFWwindow* g_window = nullptr;

// Analog-edit popup state (persists across frames; opened by renderAnalogsTab)
static int  s_editPort         = -1;
static bool s_openPopup        = false;
static int  s_devIdx[2]        = {0, 0};
static int  s_axisIdx[2]       = {0, 0};
static bool s_initialized[2]   = {false, false};
static bool s_capturingVtt[2][2] = {};
static bool s_vttPrevKeys[256]   = {};

// Light-bind popup state (persists across frames; opened by renderLightsTab)
static int         s_bindLightIdx   = -1;
static bool        s_openLightPopup = false;
static int         s_lightDevIdx    = 0;
static int         s_lightOutIdx    = -1;
static float       s_testTimer      = 0.0f;
static std::string s_testPath;
static int         s_testOutIdx     = -1;


int main() {
    glfwSetErrorCallback([](int e, const char* d) { fprintf(stderr, "GLFW error %d: %s\n", e, d); });
    if (!glfwInit()) return 1;

    g_window = glfwCreateWindow(640, 480, "2EZConfig V2.0", nullptr, nullptr);
    if (!g_window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1);

    HWND hwnd = glfwGetWin32Window(g_window);
    HICON icon = LoadIconA(GetModuleHandleA(nullptr), "IDI_ICON1");
    SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;

    ImGui_ImplGlfw_InitForOpenGL(g_window, true);
    ImGui_ImplOpenGL2_Init();

    setTheme();

    std::string appDataDir = getAppDataDir();
    std::filesystem::create_directories(appDataDir);

    // Extract patches.json from embedded resource to appdata dir if missing or outdated
    {
        std::string patchesPath = appDataDir + "\\patches.json";
        HRSRC hRes = FindResourceA(nullptr, "PATCHES_JSON", MAKEINTRESOURCEA(10));
        if (hRes) {
            HGLOBAL hData = LoadResource(nullptr, hRes);
            if (hData) {
                DWORD sz = SizeofResource(nullptr, hRes);
                const char* ptr = static_cast<const char*>(LockResource(hData));
                if (ptr && sz) {
                    bool shouldWrite = (GetFileAttributesA(patchesPath.c_str()) == INVALID_FILE_ATTRIBUTES);
                    if (!shouldWrite) {
                        try {
                            auto embedded = nlohmann::json::parse(ptr, ptr + sz);
                            int embeddedVer = embedded.value("ver", 0);
                            std::ifstream diskFile(patchesPath);
                            if (diskFile.is_open()) {
                                auto diskJson = nlohmann::json::parse(diskFile);
                                if (!diskJson.contains("ver") || embeddedVer > diskJson.value("ver", 0))
                                    shouldWrite = true;
                            }
                        } catch (...) {}
                    }
                    if (shouldWrite) {
                        HANDLE hFile = CreateFileA(patchesPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                        if (hFile != INVALID_HANDLE_VALUE) {
                            DWORD written = 0;
                            WriteFile(hFile, ptr, sz, &written, nullptr);
                            CloseHandle(hFile);
                        }
                    }
                }
            }
        }
    }

    // Load settings: game-settings.json stays local, global-settings + patches in appdata
    g_app.settings.load(".", appDataDir);

    g_app.input = new InputManager();
    g_app.bindings.load(g_app.settings, *g_app.input);

    // Initialize game selector state from persisted game_id
    {
        std::string gid = g_app.settings.gameSettings().value("game_id", "ez2dj_1st");
        static const int DJ_COUNT_M     = (int)(sizeof(djGames)     / sizeof(djGames[0]));
        static const int DANCER_COUNT_M = (int)(sizeof(dancerGames) / sizeof(dancerGames[0]));
        g_app.gameIdx  = 0;
        g_app.isDancer = false;
        bool found = false;
        for (int i = 0; !found && i < DJ_COUNT_M; i++) {
            if (gid == djGames[i].id) { g_app.gameIdx = i; found = true; }
        }
        for (int i = 0; !found && i < DANCER_COUNT_M; i++) {
            if (gid == dancerGames[i].id) { g_app.gameIdx = DJ_COUNT_M + i; g_app.isDancer = true; found = true; }
        }
    }

    while (!glfwWindowShouldClose(g_window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        renderUI();

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(g_window, &w, &h);
        glViewport(0, 0, w, h);
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
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration   |
        ImGuiWindowFlags_NoMove         |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("##main", nullptr, flags);

    ImGui::TextUnformatted("2EZConfig ~ It rules once again ~ ");
    ImGui::SameLine();
    {
        float btnWidth = 90.0f;
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - btnWidth);
        // Get current exeName at frame time — use exe_name override if set in game-settings
        static const int DJ_COUNT_TB = (int)(sizeof(djGames) / sizeof(djGames[0]));
        const char* defaultExe = (g_app.gameIdx >= DJ_COUNT_TB) ? "EZ2Dancer.exe" : djGames[g_app.gameIdx].defaultExeName;
        std::string exeOverride = g_app.settings.gameSettings().value("exe_name", "");
        const char* tbExeName   = exeOverride.empty() ? defaultExe : exeOverride.c_str();
        if (ImGui::Button("Play EZ2", ImVec2(btnWidth, 0))) {
            std::vector<std::string> extraDlls;
            std::string extraDllsStr = g_app.settings.gameSettings().value("extra_dlls", "");
            std::istringstream iss(extraDllsStr);
            std::string token;
            while (iss >> token)
                extraDlls.push_back(token);
            Injector::LaunchAndInject(tbExeName, extraDlls);
            glfwSetWindowShouldClose(g_window, GLFW_TRUE);
        }
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

    ImGui::SetCursorPos({ ImGui::GetWindowWidth() - 250, ImGui::GetWindowHeight() - 20 });
    ImGui::TextDisabled("Made by kasaski (kissass) - 2026");

    ImGui::End();
}

static void renderPatchesTab() {
    std::string gameId = g_app.settings.gameSettings().value("game_id", "");
    auto& patches = g_app.settings.patchStore().patchesForGame(gameId);

    if (patches.empty()) {
        const char* msg = "No patches available for this game.";
        float textW = ImGui::CalcTextSize(msg).x;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - textW) * 0.5f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 40.0f);
        ImGui::TextDisabled("%s", msg);
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
        std::vector<const char*> opts;
        for (const auto& o : patch.options) opts.push_back(o.c_str());
        if (ImGui::Combo("##val", &patch.value, opts.data(), (int)opts.size()))
            changed = true;
    }

    if (!patch.description.empty() && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", patch.description.c_str());

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
    static const int DJ_COUNT     = (int)(sizeof(djGames)     / sizeof(djGames[0]));
    static const int DANCER_COUNT = (int)(sizeof(dancerGames) / sizeof(dancerGames[0]));
    static const int TOTAL_COUNT  = DJ_COUNT + DANCER_COUNT;

    static const char* gameComboItems[DJ_COUNT + DANCER_COUNT];
    static bool        gameComboBuilt = false;
    if (!gameComboBuilt) {
        for (int i = 0; i < DJ_COUNT;     i++) gameComboItems[i]           = djGames[i].name;
        for (int i = 0; i < DANCER_COUNT; i++) gameComboItems[DJ_COUNT + i] = dancerGames[i].name;
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
        static char exeNameBuf[MAX_PATH] = {};
        static int  lastGameIdx = -1;
        if (lastGameIdx != g_app.gameIdx) {
            lastGameIdx = g_app.gameIdx;
            std::string stored = g_app.settings.gameSettings().value("exe_name", "");
            strncpy(exeNameBuf, stored.c_str(), MAX_PATH - 1);
            exeNameBuf[MAX_PATH - 1] = '\0';
        }
        if (ImGui::InputText("Exe Name", exeNameBuf, MAX_PATH)) {
            if (exeNameBuf[0])
                g_app.settings.gameSettings()["exe_name"] = std::string(exeNameBuf);
            else
                g_app.settings.gameSettings().erase("exe_name");
            g_app.settings.save();
        }
    }

    {
        static char extraDllsBuf[2048] = {};
        static int  lastGameIdxDlls = -1;
        if (lastGameIdxDlls != g_app.gameIdx) {
            lastGameIdxDlls = g_app.gameIdx;
            std::string stored = g_app.settings.gameSettings().value("extra_dlls", "");
            strncpy(extraDllsBuf, stored.c_str(), sizeof(extraDllsBuf) - 1);
            extraDllsBuf[sizeof(extraDllsBuf) - 1] = '\0';
        }
        if (ImGui::InputText("Extra DLLs", extraDllsBuf, sizeof(extraDllsBuf))) {
            if (extraDllsBuf[0])
                g_app.settings.gameSettings()["extra_dlls"] = std::string(extraDllsBuf);
            else
                g_app.settings.gameSettings().erase("extra_dlls");
            g_app.settings.save();
        }
        ImGui::TextDisabled("Space-separated DLL paths injected after 2EZ.dll");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Global Settings");
    ImGui::Separator();
    globalCheckbox("Enable IO Emulation",                "io_emu",       true);
    globalCheckbox("Force 60Hz (experimental)",          "force_60hz",   false);
    globalCheckbox("Force High Priority (experimental)", "high_priority", false);

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
        if (s_state == BindState_Listening) g_app.input->startCapture();
        else                                 g_app.input->stopCapture();
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
                char timerStr[16]; snprintf(timerStr, sizeof(timerStr), "(%.1fs)", s_listenTimer);
                ImGui::TextUnformatted(timerStr);
                ImGui::TableSetColumnIndex(2);
                if (ImGui::Button("Cancel")) {
                    g_app.input->stopCapture();
                    s_state     = BindState_Normal;
                    s_listenIdx = -1;
                }

                CaptureResult hit;
                if (g_app.input->pollCapture(hit)) {
                    bnd = ButtonBinding::fromCapture(hit);
                    g_app.bindings.save(g_app.settings);
                    s_state     = BindState_Normal;
                    s_listenIdx = -1;
                }

                if (s_state == BindState_Listening) {
                    int vk = pollKeyboardPress(s_prevKeys);
                    if (vk >= 0) {
                        ButtonBinding kb;
                        kb.vk_code = vk;
                        bnd = kb;
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

                bool active = g_app.bindings.isHeld(bnd);

                if (active)
                    ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "%s:", actionList[i]);
                else
                    ImGui::TextUnformatted(actionList[i]);

                ImGui::TableSetColumnIndex(1);

                if (active)
                    ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "%s", g_app.bindings.getDisplayString(bnd).c_str());
                else
                    ImGui::TextUnformatted(g_app.bindings.getDisplayString(bnd).c_str());

                ImGui::TableSetColumnIndex(2);

                if (ImGui::Button("Bind")) {
                    s_state       = BindState_Listening;
                    s_listenIdx   = i;
                    s_listenTimer = 5.0f;
                    // Prime prev keys to avoid spurious immediate trigger
                    for (int vk = 0; vk < 256; vk++)
                        s_prevKeys[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
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
    for (int port = 0; port < 2; port++) {
        const AnalogBinding& ab = g_app.bindings.analogs[port];
        if (ab.vtt_plus.isSet()  && g_app.bindings.isHeld(ab.vtt_plus))
            g_app.vtt_pos[port] = (uint8_t)(((int)g_app.vtt_pos[port] + ab.vtt_step) & 0xFF);
        if (ab.vtt_minus.isSet() && g_app.bindings.isHeld(ab.vtt_minus))
            g_app.vtt_pos[port] = (uint8_t)(((int)g_app.vtt_pos[port] - ab.vtt_step + 256) & 0xFF);
    }

    // Build device list for combo: only Generic Desktop (page 0x01)
    // devices with axes. Excludes consumer control (0x0C), system
    // control, and other non-joystick HID collections.
    std::vector<Device> devs = g_app.input->getDevices();
    std::vector<Device> axisDevs;
    for (auto& d : devs)
        if (!d.value_caps_names.empty() && d.hid && d.hid->caps.UsagePage == 0x01)
            axisDevs.push_back(d);

    if (ImGui::BeginTable("##analogTable", 3, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Turntable", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Binding",   ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Actions",   ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableHeadersRow();

        for (int p = 0; p < BindingStore::ANALOG_COUNT; p++) {
            ImGui::PushID(p);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("P%d", p+1);
            AnalogBinding& ab = g_app.bindings.analogs[p];
            if (ab.isSet() || ab.hasVtt()) {
                ImGui::SameLine();
                float display = g_app.bindings.getPosition(ab, g_app.vtt_pos[p]) / 255.0f;
                ImGui::ProgressBar(display, ImVec2(40.0f, 0));
            }

            ImGui::TableSetColumnIndex(1);
            if (!ab.isSet() && ab.hasVtt())
                ImGui::TextUnformatted("Virtual TT keys Assigned");
            else
                ImGui::TextUnformatted(g_app.bindings.getDisplayString(ab).c_str());

            ImGui::TableSetColumnIndex(2);
            if (ImGui::Button("Edit")) {
                s_editPort       = p;
                s_openPopup      = true;
                s_initialized[p] = false;
            }
            if (ab.isSet() || ab.hasVtt()) {
                ImGui::SameLine();
                if (ImGui::Button("Clear")) {
                    ab.clear();
                    g_app.vtt_pos[p] = 128;
                    g_app.bindings.save(g_app.settings);
                }
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Open popup outside PushID scope
    if (s_openPopup) { ImGui::OpenPopup("EditAnalog"); s_openPopup = false; }
    renderAnalogEditPopup(axisDevs);
}

static void renderAnalogEditPopup(const std::vector<Device>& axisDevs) {
    if (!ImGui::BeginPopupModal("EditAnalog", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    int p = s_editPort;
    if (p >= 0 && p < BindingStore::ANALOG_COUNT) {
        ImGui::PushID(p);
        ImGui::Text("Editing: %s", analogNames[p]);
        ImGui::Separator();
        AnalogBinding& ab = g_app.bindings.analogs[p];

        if (!s_initialized[p]) {
            s_initialized[p] = true;
            s_devIdx[p] = 0; s_axisIdx[p] = 0;
            if (ab.isSet()) {
                for (int di = 0; di < (int)axisDevs.size(); di++) {
                    if (axisDevs[di].path == ab.device_path) {
                        s_devIdx[p]  = di + 1;  // +1 for "(none)" at index 0
                        s_axisIdx[p] = (ab.axis_idx >= 0) ? ab.axis_idx : 0;
                        break;
                    }
                }
            }
        }

        std::vector<const char*> devLabels;
        devLabels.push_back("(none)");
        for (auto& d : axisDevs) devLabels.push_back(d.name.c_str());
        if (ImGui::Combo("Device", &s_devIdx[p], devLabels.data(), (int)devLabels.size())) {
            s_axisIdx[p] = 0;
            if (s_devIdx[p] > 0) {
                const Device& d = axisDevs[(size_t)(s_devIdx[p] - 1)];
                ab.device_path = d.path; ab.device_name = d.name; ab.axis_idx = 0;
            } else { ab.device_path.clear(); ab.device_name.clear(); ab.axis_idx = -1; }
            g_app.bindings.save(g_app.settings);
        }

        if (s_devIdx[p] > 0) {
            const Device& d = axisDevs[(size_t)(s_devIdx[p] - 1)];
            int axCount = (int)d.value_caps_names.size();
            std::vector<const char*> axLabels;
            for (auto& l : d.value_caps_names) axLabels.push_back(l.c_str());
            if (axCount > 0 && ImGui::Combo("Axis", &s_axisIdx[p], axLabels.data(), axCount)) {
                ab.axis_idx = s_axisIdx[p];
                g_app.bindings.save(g_app.settings);
            }
        } else {
            ImGui::TextDisabled("Axis: (select device first)");
        }

        if (ImGui::Checkbox("Reverse", &ab.reverse))
            g_app.bindings.save(g_app.settings);

        ImGui::Separator();

        renderVttKeyBind("Virtual TT-:", "Bind##vttm", "Clear##vttm",
                         ab.vtt_minus, s_capturingVtt[p][1], s_capturingVtt[p][0], s_vttPrevKeys);
        renderVttKeyBind("Virtual TT+:", "Bind##vttp", "Clear##vttp",
                         ab.vtt_plus,  s_capturingVtt[p][0], s_capturingVtt[p][1], s_vttPrevKeys);

        if (ImGui::SliderInt("Virtual TT Step Amount", &ab.vtt_step, 1, 10))
            g_app.bindings.save(g_app.settings);

        ImGui::Separator();

        {
            float display = 0.5f;
            char overlay[32];
            if (ab.isSet() || ab.hasVtt()) {
                display = g_app.bindings.getPosition(ab, g_app.vtt_pos[p]) / 255.0f;
                snprintf(overlay, sizeof(overlay), "%.0f", display * 255.0f);
            } else {
                snprintf(overlay, sizeof(overlay), "(unbound)");
            }
            ImGui::ProgressBar(display, ImVec2(-0.5f, 0), overlay);
        }

        ImGui::PopID();
    }
    ImGui::Separator();
    if (ImGui::Button("Close", ImVec2(120, 0))) {
        // Stop any VTT capture in progress
        for (int pp = 0; pp < 2; pp++) {
            for (int dir = 0; dir < 2; dir++) {
                if (s_capturingVtt[pp][dir]) {
                    g_app.input->stopCapture();
                    s_capturingVtt[pp][dir] = false;
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
    std::vector<Device> allDevs = g_app.input->getDevices();
    std::vector<Device> outputDevs;
    for (auto& d : allDevs) {
        if (!d.button_output_caps_names.empty() || !d.value_output_caps_names.empty())
            outputDevs.push_back(d);
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

            LightBinding& lb = g_app.bindings.lights[i];

            std::string lightLabel = g_app.bindings.getDisplayString(lb);
            bool isTestActive = (s_testTimer > 0.0f && lb.isSet()
                                && s_testPath == lb.device_path
                                && s_testOutIdx == lb.output_idx);

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(lightNames[i]);

            ImGui::TableSetColumnIndex(1);
            if (isTestActive)
                ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "%s", lightLabel.c_str());
            else
                ImGui::TextUnformatted(lightLabel.c_str());

            ImGui::TableSetColumnIndex(2);
            if (ImGui::Button("Bind")) {
                s_bindLightIdx = i;
                s_lightDevIdx  = 0;
                s_lightOutIdx  = -1;
                // Populate from current binding if set
                if (lb.isSet()) {
                    for (int d = 0; d < (int)outputDevs.size(); d++) {
                        if (outputDevs[d].path == lb.device_path) {
                            s_lightDevIdx = d + 1;  // +1 for "(none)" entry
                            s_lightOutIdx = lb.output_idx;
                            break;
                        }
                    }
                }
                s_openLightPopup = true;
            }

            if (lb.isSet()) {
                ImGui::SameLine();
                if (ImGui::Button("Clear")) {
                    lb.clear();
                    g_app.bindings.save(g_app.settings);
                }
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    if (s_openLightPopup) { ImGui::OpenPopup("BindLight"); s_openLightPopup = false; }
    renderLightBindPopup(outputDevs);
}

static void renderLightBindPopup(const std::vector<Device>& outputDevs) {
    if (!ImGui::BeginPopupModal("BindLight", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::Text("Bind Light: %s", (s_bindLightIdx >= 0 ? lightNames[s_bindLightIdx] : "?"));
    ImGui::Separator();

    std::vector<const char*> devLabels;
    devLabels.push_back("(none)");
    for (auto& d : outputDevs) devLabels.push_back(d.name.c_str());
    int prevDevIdx = s_lightDevIdx;
    ImGui::Combo("Device", &s_lightDevIdx, devLabels.data(), (int)devLabels.size());
    if (prevDevIdx != s_lightDevIdx) s_lightOutIdx = -1;  // reset output on device change

    // Output combo — flat: button_output_caps_names then value_output_caps_names
    int prevOutIdx = s_lightOutIdx;
    if (s_lightDevIdx > 0) {
        const Device& selDev = outputDevs[s_lightDevIdx - 1];
        std::vector<const char*> outLabels;
        for (auto& n : selDev.button_output_caps_names) outLabels.push_back(n.c_str());
        for (auto& n : selDev.value_output_caps_names)  outLabels.push_back(n.c_str());
        if (!outLabels.empty()) {
            if (s_lightOutIdx < 0) s_lightOutIdx = 0;  // auto-select first
            ImGui::Combo("Output", &s_lightOutIdx, outLabels.data(), (int)outLabels.size());
        }
    } else {
        s_lightOutIdx = -1;
    }

    if ((prevDevIdx != s_lightDevIdx || prevOutIdx != s_lightOutIdx)
        && s_lightDevIdx > 0 && s_lightOutIdx >= 0 && s_bindLightIdx >= 0) {
        LightBinding& lb = g_app.bindings.lights[s_bindLightIdx];
        lb.device_path = outputDevs[s_lightDevIdx - 1].path;
        lb.device_name = outputDevs[s_lightDevIdx - 1].name;
        lb.output_idx  = s_lightOutIdx;
        g_app.bindings.save(g_app.settings);
    }

    ImGui::Separator();

    // Test button — pulses light for 0.5 seconds then auto-off
    bool canTest = s_lightDevIdx > 0 && s_lightOutIdx >= 0;
    if (!canTest) ImGui::BeginDisabled();
    if (ImGui::Button("Test")) {
        s_testPath   = outputDevs[s_lightDevIdx - 1].path;
        s_testOutIdx = s_lightOutIdx;
        g_app.input->setLight(s_testPath, s_testOutIdx, 1.0f);
        s_testTimer  = 0.5f;
    }
    if (!canTest) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

// Returns newly-pressed VK (ignoring mouse buttons), or -1 if none. Updates prevKeys[] in-place.
static int pollKeyboardPress(bool* prevKeys) {
    for (int vk = 0x01; vk < 0xFF; vk++) {
        bool pr = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (pr && !prevKeys[vk] && vk != VK_LBUTTON && vk != VK_RBUTTON && vk != VK_MBUTTON) {
            prevKeys[vk] = true;
            return vk;
        }
        prevKeys[vk] = pr;
    }
    return -1;
}

static void renderVttKeyBind(const char* label, const char* bindId, const char* clearId,
                             ButtonBinding& key, bool& capturing, bool& otherCapturing, bool* prevKeys) {
    ImGui::Text("%s %s", label, g_app.bindings.getDisplayString(key).c_str());
    ImGui::SameLine();
    if (capturing) {
        ImGui::TextColored(ImVec4(1,1,0,1), "[Press button or key...]");
        CaptureResult hit;
        if (g_app.input->pollCapture(hit)) {
            key = ButtonBinding::fromCapture(hit);
            g_app.bindings.save(g_app.settings);
            g_app.input->stopCapture();
            capturing = false;
        } else {
            int vk = pollKeyboardPress(prevKeys);
            if (vk >= 0) {
                key.clear();
                key.vk_code = vk;
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
            for (int vk = 0; vk < 256; vk++)
                prevKeys[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
        }
        if (key.isSet()) {
            ImGui::SameLine();
            if (ImGui::Button(clearId)) {
                key.clear();
                g_app.bindings.save(g_app.settings);
            }
        }
    }
}

static void globalCheckbox(const char* label, const char* key, bool defaultVal) {
    bool val = g_app.settings.globalSettings().value(key, defaultVal);
    if (ImGui::Checkbox(label, &val)) {
        g_app.settings.globalSettings()[key] = val;
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

    ImVec4* c = ImGui::GetStyle().Colors;
    c[ImGuiCol_Text]                 = { 0.92f, 0.91f, 0.88f, 1.00f };  // warm off-white
    c[ImGuiCol_TextDisabled]         = { 0.42f, 0.41f, 0.38f, 1.00f };  // mid grey
    c[ImGuiCol_WindowBg]             = { 0.07f, 0.07f, 0.07f, 0.97f };  // near black
    c[ImGuiCol_ChildBg]              = { 0.00f, 0.00f, 0.00f, 0.00f };
    c[ImGuiCol_PopupBg]              = { 0.09f, 0.09f, 0.09f, 0.97f };
    c[ImGuiCol_Border]               = { 0.25f, 0.23f, 0.20f, 0.60f };  // warm dark grey
    c[ImGuiCol_BorderShadow]         = { 0.00f, 0.00f, 0.00f, 0.00f };
    c[ImGuiCol_FrameBg]              = { 0.16f, 0.15f, 0.13f, 1.00f };  // dark charcoal
    c[ImGuiCol_FrameBgHovered]       = { 0.24f, 0.22f, 0.18f, 1.00f };
    c[ImGuiCol_FrameBgActive]        = { 0.30f, 0.27f, 0.22f, 1.00f };
    c[ImGuiCol_TitleBg]              = { 0.10f, 0.07f, 0.02f, 1.00f };  // very dark orange
    c[ImGuiCol_TitleBgActive]        = { 0.22f, 0.13f, 0.02f, 1.00f };
    c[ImGuiCol_TitleBgCollapsed]     = { 0.10f, 0.07f, 0.02f, 0.80f };
    c[ImGuiCol_MenuBarBg]            = { 0.10f, 0.07f, 0.02f, 1.00f };
    c[ImGuiCol_ScrollbarBg]          = { 0.02f, 0.02f, 0.02f, 0.53f };
    c[ImGuiCol_ScrollbarGrab]        = { 0.32f, 0.32f, 0.30f, 1.00f };
    c[ImGuiCol_ScrollbarGrabHovered] = { 0.44f, 0.42f, 0.38f, 1.00f };
    c[ImGuiCol_ScrollbarGrabActive]  = { 0.83f, 0.44f, 0.10f, 1.00f };  // orange on active
    c[ImGuiCol_CheckMark]            = { 0.88f, 0.50f, 0.10f, 1.00f };  // orange
    c[ImGuiCol_SliderGrab]           = { 0.83f, 0.44f, 0.10f, 1.00f };  // orange
    c[ImGuiCol_SliderGrabActive]     = { 1.00f, 0.60f, 0.20f, 1.00f };
    c[ImGuiCol_Button]               = { 0.83f, 0.44f, 0.10f, 0.30f };
    c[ImGuiCol_ButtonHovered]        = { 0.88f, 0.50f, 0.12f, 0.75f };
    c[ImGuiCol_ButtonActive]         = { 1.00f, 0.58f, 0.15f, 1.00f };
    c[ImGuiCol_Header]               = { 0.55f, 0.28f, 0.04f, 0.75f };  // dark orange
    c[ImGuiCol_HeaderHovered]        = { 0.83f, 0.44f, 0.10f, 0.75f };
    c[ImGuiCol_HeaderActive]         = { 0.90f, 0.52f, 0.12f, 1.00f };
    c[ImGuiCol_Separator]            = { 0.28f, 0.26f, 0.22f, 0.70f };
    c[ImGuiCol_SeparatorHovered]     = { 0.83f, 0.44f, 0.10f, 0.80f };
    c[ImGuiCol_SeparatorActive]      = { 0.90f, 0.52f, 0.12f, 1.00f };
    c[ImGuiCol_ResizeGrip]           = { 0.83f, 0.44f, 0.10f, 0.20f };
    c[ImGuiCol_ResizeGripHovered]    = { 0.83f, 0.44f, 0.10f, 0.65f };
    c[ImGuiCol_ResizeGripActive]     = { 1.00f, 0.58f, 0.15f, 0.95f };
    c[ImGuiCol_Tab]                  = { 0.40f, 0.22f, 0.05f, 1.00f };  // visible dark orange
    c[ImGuiCol_TabHovered]           = { 0.90f, 0.50f, 0.12f, 1.00f };
    c[ImGuiCol_TabActive]            = { 0.78f, 0.42f, 0.09f, 1.00f };  // bright active orange
    c[ImGuiCol_TabUnfocused]         = { 0.25f, 0.14f, 0.03f, 1.00f };
    c[ImGuiCol_TabUnfocusedActive]   = { 0.48f, 0.26f, 0.06f, 1.00f };
    c[ImGuiCol_PlotLines]            = { 0.55f, 0.55f, 0.52f, 1.00f };
    c[ImGuiCol_PlotLinesHovered]     = { 1.00f, 0.58f, 0.15f, 1.00f };
    c[ImGuiCol_PlotHistogram]        = { 0.83f, 0.44f, 0.10f, 1.00f };
    c[ImGuiCol_PlotHistogramHovered] = { 1.00f, 0.60f, 0.20f, 1.00f };
    c[ImGuiCol_TextSelectedBg]       = { 0.83f, 0.44f, 0.10f, 0.35f };
    c[ImGuiCol_DragDropTarget]       = { 1.00f, 0.58f, 0.15f, 0.90f };
    c[ImGuiCol_NavHighlight]         = { 0.83f, 0.44f, 0.10f, 1.00f };
    c[ImGuiCol_NavWindowingHighlight]= { 1.00f, 1.00f, 1.00f, 0.70f };
    c[ImGuiCol_NavWindowingDimBg]    = { 0.80f, 0.80f, 0.80f, 0.20f };
    c[ImGuiCol_ModalWindowDimBg]     = { 0.00f, 0.00f, 0.00f, 0.65f };
}
