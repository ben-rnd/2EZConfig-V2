#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
#include "injector.h"
#include "settings.h"
#include "input.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnarrowing"
#include "strings.h"
#pragma GCC diagnostic pop
extern "C" {
#include "usb-hid-usage.h"
}
#include <GLFW/glfw3.h>
#include <cstdio>
#include <string>
#include <vector>

static void renderUI();
static void setTheme();

// File-scope settings and game selector state — accessible to all tab functions
static SettingsManager g_settings;
static int  g_gameIdx  = 0;     // index into flat combo list (DJ games first, then Dancer)
static bool g_isDancer = false; // derived from g_gameIdx

// ---------------------------------------------------------------------------
// Buttons tab: binding label helper
// ---------------------------------------------------------------------------
static std::string getButtonBindingLabel(const char* action) {
    auto& gs = g_settings.globalSettings();
    if (!gs.contains("button_bindings")) return "(unbound)";
    auto& bb = gs["button_bindings"];
    if (!bb.contains(action)) return "(unbound)";
    auto& b = bb[action];
    std::string type = b.value("type", "");
    if (type == "HidButton") {
        uint16_t page = (uint16_t)b.value("usage_page", 0);
        uint16_t id   = (uint16_t)b.value("usage_id",   0);
        char* name = getHidUsageText((uint32_t)page, (uint32_t)id);
        std::string label = name ? std::string(name) : ("Button 0x" + std::to_string(id));
        if (name) free(name);
        return label;
    } else if (type == "Keyboard") {
        uint32_t vk = b.value("vk_code", (uint32_t)0);
        char buf[64] = {};
        UINT sc = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
        GetKeyNameTextA((LONG)(sc << 16), buf, sizeof(buf));
        return std::string("Key: ") + (buf[0] ? buf : "?");
    }
    return "(unbound)";
}

// ---------------------------------------------------------------------------
// Analogs tab: write a single field to analog_bindings[name]["axis"] and save
// ---------------------------------------------------------------------------
template<typename T>
static void writeAnalogAxisField(int port, const char* field, T value) {
    g_settings.globalSettings()["analog_bindings"][analogs[port]]["axis"][field] = value;
    g_settings.save();
}

template<typename T>
static void writeAnalogVttField(int port, const char* field, T value) {
    g_settings.globalSettings()["analog_bindings"][analogs[port]]["vtt"][field] = value;
    g_settings.save();
}

// ---------------------------------------------------------------------------
// Analogs tab: build axis list by opening device and querying HID value caps
// ---------------------------------------------------------------------------
static std::vector<std::pair<uint16_t,uint16_t>> s_axisUsages[2];
static std::vector<std::string>                  s_axisLabels[2];

static void rebuildAxisList(int port, const std::string& devicePath) {
    s_axisUsages[port].clear();
    s_axisLabels[port].clear();
    if (devicePath.empty()) return;
    HANDLE h = CreateFileA(devicePath.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    PHIDP_PREPARSED_DATA ppd = nullptr;
    HidD_GetPreparsedData(h, &ppd);
    if (ppd) {
        HIDP_CAPS caps = {};
        HidP_GetCaps(ppd, &caps);
        USHORT numVal = caps.NumberInputValueCaps;
        std::vector<HIDP_VALUE_CAPS> vc((size_t)numVal);
        HidP_GetValueCaps(HidP_Input, vc.data(), &numVal, ppd);
        for (auto& v : vc) {
            uint16_t page = v.UsagePage;
            uint16_t id   = v.IsRange ? v.Range.UsageMin : v.NotRange.Usage;
            char* name = getHidUsageText((uint32_t)page, (uint32_t)id);
            std::string label = name ? std::string(name) : ("0x" + std::to_string(id));
            if (name) free(name);
            s_axisUsages[port].push_back({page, id});
            s_axisLabels[port].push_back(label);
        }
        HidD_FreePreparsedData(ppd);
    }
    CloseHandle(h);
}

int main() {
    glfwSetErrorCallback([](int e, const char* d) { fprintf(stderr, "GLFW error %d: %s\n", e, d); });
    if (!glfwInit()) return 1;

    GLFWwindow* window = glfwCreateWindow(420, 450, "2EZConfig", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();

    setTheme();

    // Load settings from exe directory
    g_settings.load(".", ".");

    // Initialize game selector state from persisted game_id
    {
        std::string gid = g_settings.gameSettings().value("game_id", "ez2dj_1st");
        static const int DJ_COUNT_M     = (int)(sizeof(djGames)     / sizeof(djGames[0]));
        static const int DANCER_COUNT_M = (int)(sizeof(dancerGames) / sizeof(dancerGames[0]));
        g_gameIdx = 0;
        g_isDancer = false;
        bool found = false;
        for (int i = 0; !found && i < DJ_COUNT_M; i++) {
            if (gid == djGames[i].id) { g_gameIdx = i; found = true; }
        }
        for (int i = 0; !found && i < DANCER_COUNT_M; i++) {
            if (gid == dancerGames[i].id) { g_gameIdx = DJ_COUNT_M + i; g_isDancer = true; found = true; }
        }
        // Migration: old-style "ez2dancer" (no version) — default to first Dancer entry
        if (!found && gid == "ez2dancer") { g_gameIdx = DJ_COUNT_M; g_isDancer = true; }
    }

    // Start input subsystem
    Input::init(g_settings);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        renderUI();

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.09f, 0.09f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    Input::shutdown();
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

    ImGui::TextUnformatted("2EZConfig");
    ImGui::SameLine();
    // Right-align Play EZ2 button: compute position so the button sits at window right edge
    {
        float btnWidth = 90.0f;
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - btnWidth);
        // Get current exeName at frame time (not from Settings tab scope)
        static const int DJ_COUNT_TB = (int)(sizeof(djGames) / sizeof(djGames[0]));
        const char* tbExeName = (g_gameIdx >= DJ_COUNT_TB) ? "EZ2Dancer.exe" : djGames[g_gameIdx].defaultExeName;
        if (ImGui::Button("Play EZ2", ImVec2(btnWidth, 0))) {
            Injector::LaunchAndInject(tbExeName);
        }
    }
    ImGui::Separator();

    if (ImGui::BeginTabBar("##tabs")) {

        // ---- Settings Tab ----
        if (ImGui::BeginTabItem("Settings")) {
            static const int DJ_COUNT     = (int)(sizeof(djGames)     / sizeof(djGames[0]));
            static const int DANCER_COUNT = (int)(sizeof(dancerGames) / sizeof(dancerGames[0]));
            static const int TOTAL_COUNT  = DJ_COUNT + DANCER_COUNT;

            // Build flat label array once
            static const char* gameComboItems[DJ_COUNT + DANCER_COUNT];
            static bool        gameComboBuilt = false;
            if (!gameComboBuilt) {
                for (int i = 0; i < DJ_COUNT;     i++) gameComboItems[i]           = djGames[i].name;
                for (int i = 0; i < DANCER_COUNT; i++) gameComboItems[DJ_COUNT + i] = dancerGames[i].name;
                gameComboBuilt = true;
            }

            if (ImGui::Combo("Game", &g_gameIdx, gameComboItems, TOTAL_COUNT)) {
                g_isDancer = (g_gameIdx >= DJ_COUNT);
                std::string gameId;
                if (g_isDancer) {
                    int dancerIdx = g_gameIdx - DJ_COUNT;
                    gameId = dancerGames[dancerIdx].id;
                } else {
                    gameId = djGames[g_gameIdx].id;
                }
                g_settings.gameSettings()["game_id"] = gameId;
                g_settings.save();
            }

            ImGui::EndTabItem();
        }

        // ---- Buttons Tab ----
        if (ImGui::BeginTabItem("Buttons")) {
            // Bind state machine statics
            enum class BindState { Normal, Listening };
            static BindState s_bindState   = BindState::Normal;
            static int       s_listenIdx   = -1;
            static float     s_listenTimer = 0.0f;

            // Select action list based on game type
            const char** actionList  = g_isDancer ? ez2DancerIOButtons : ioButtons;
            const int    actionCount = g_isDancer
                ? (int)(sizeof(ez2DancerIOButtons) / sizeof(ez2DancerIOButtons[0]))
                : (int)(sizeof(ioButtons)          / sizeof(ioButtons[0]));

            // Scrollable region to contain all action rows
            ImGui::BeginChild("##button_rows", ImVec2(0, 0), false);
            for (int i = 0; i < actionCount; i++) {
                const char* action = actionList[i];
                ImGui::PushID(i);

                if (s_bindState == BindState::Listening && s_listenIdx == i) {
                    // LISTENING state for this row
                    ImGui::TextColored({1.0f, 1.0f, 0.0f, 1.0f}, "Press a button...");
                    ImGui::SameLine();
                    char timerBuf[16];
                    snprintf(timerBuf, sizeof(timerBuf), "(%.0fs)", s_listenTimer);
                    ImGui::TextDisabled(timerBuf);
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel")) {
                        s_bindState = BindState::Normal;
                        s_listenIdx = -1;
                    }

                    // Poll for HID button press
                    auto hit = Input::pollNextButtonPress();
                    if (hit.has_value()) {
                        auto& bb = g_settings.globalSettings()["button_bindings"][action];
                        bb["type"]        = "HidButton";
                        bb["device_path"] = hit->device_path;
                        bb["usage_page"]  = (int)hit->usage_page;
                        bb["usage_id"]    = (int)hit->usage_id;
                        g_settings.save();
                        s_bindState = BindState::Normal;
                        s_listenIdx = -1;
                    }

                    // Poll for keyboard key press (scan VK 0x01..0xFE)
                    // Use static prevKeys array to detect newly-pressed key
                    static bool prevKeys[256] = {};
                    if (!hit.has_value()) {
                        for (int vk = 0x01; vk < 0xFF; vk++) {
                            bool pressed = (GetAsyncKeyState(vk) & 0x8000) != 0;
                            if (pressed && !prevKeys[vk]) {
                                // Ignore mouse buttons to avoid the click that started
                                // listen mode from immediately binding
                                if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) {
                                    // skip
                                } else {
                                    auto& bb = g_settings.globalSettings()["button_bindings"][action];
                                    bb["type"]    = "Keyboard";
                                    bb["vk_code"] = vk;
                                    g_settings.save();
                                    s_bindState = BindState::Normal;
                                    s_listenIdx = -1;
                                    // Update prevKeys for this vk to avoid re-triggering
                                    prevKeys[vk] = true;
                                    break;
                                }
                            }
                            prevKeys[vk] = pressed;
                        }
                    }

                    // Timeout
                    s_listenTimer -= ImGui::GetIO().DeltaTime;
                    if (s_listenTimer <= 0.0f) {
                        s_bindState = BindState::Normal;
                        s_listenIdx = -1;
                    }
                } else {
                    // NORMAL state for this row
                    std::string bindLabel = getButtonBindingLabel(action);
                    ImGui::Text("%s:", action);
                    ImGui::SameLine(160.0f);
                    ImGui::TextUnformatted(bindLabel.c_str());
                    ImGui::SameLine(310.0f);
                    if (ImGui::Button("Bind")) {
                        s_bindState   = BindState::Listening;
                        s_listenIdx   = i;
                        s_listenTimer = 5.0f;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Clear")) {
                        auto& gs = g_settings.globalSettings();
                        if (gs.contains("button_bindings") && gs["button_bindings"].contains(action)) {
                            gs["button_bindings"].erase(action);
                            g_settings.save();
                        }
                    }
                }

                ImGui::PopID();
            }
            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        // ---- Analogs Tab ----
        if (ImGui::BeginTabItem("Analogs")) {
            if (g_isDancer) {
                ImGui::TextDisabled("No analog inputs for EZ2Dancer.");
            } else {
                // Per-turntable state statics
                static const int ANALOG_COUNT = (int)(sizeof(analogs) / sizeof(analogs[0])); // 2
                static int   s_analogDeviceIdx[2]   = {0, 0};
                static int   s_analogAxisIdx[2]     = {0, 0};
                static bool  s_analogReverse[2]     = {false, false};
                static float s_analogSensitivity[2] = {1.0f, 1.0f};
                static int   s_analogDeadZone[2]    = {10, 10};
                static int   s_vttPlusVK[2]         = {0, 0};
                static int   s_vttMinusVK[2]        = {0, 0};
                static int   s_vttStep[2]           = {3, 3};
                static bool  s_analogStateLoaded    = false;

                // VTT capture state: [port][0=plus, 1=minus]
                static bool  s_capturingVTT[2][2]   = {{false,false},{false,false}};
                static bool  s_vttPrevKeys[256]     = {};

                // Device list
                static std::vector<Input::DeviceDesc> s_analogDevices;
                static bool s_analogDevicesLoaded = false;

                // Load persisted analog settings on first render
                if (!s_analogStateLoaded) {
                    s_analogStateLoaded = true;
                    auto& gs = g_settings.globalSettings();
                    for (int p = 0; p < ANALOG_COUNT; p++) {
                        const char* name = analogs[p];
                        if (!gs.contains("analog_bindings") || !gs["analog_bindings"].contains(name)) continue;
                        auto& ab = gs["analog_bindings"][name];
                        if (ab.contains("axis")) {
                            auto& ax = ab["axis"];
                            s_analogReverse[p]     = ax.value("reverse",     false);
                            s_analogSensitivity[p] = ax.value("sensitivity", 1.0f);
                            s_analogDeadZone[p]    = ax.value("dead_zone",   10);
                        }
                        if (ab.contains("vtt")) {
                            auto& vt = ab["vtt"];
                            s_vttPlusVK[p]  = vt.value("plus_vk",  0);
                            s_vttMinusVK[p] = vt.value("minus_vk", 0);
                            s_vttStep[p]    = vt.value("step",      3);
                        }
                    }
                }

                // Enumerate devices once and cache
                if (!s_analogDevicesLoaded) {
                    s_analogDevicesLoaded = true;
                    s_analogDevices = Input::enumerateDevices();
                }

                // Build device label list:
                // combo index 0 = "(none)", 1..N = HID devices
                // (Mouse Wheel option is omitted here for simplicity — axis or VTT only)
                int deviceCount = (int)s_analogDevices.size() + 1;
                std::vector<const char*> deviceLabels;
                deviceLabels.reserve((size_t)deviceCount);
                deviceLabels.push_back("(none)");
                for (auto& d : s_analogDevices) {
                    // Use product string if available, else device path
                    deviceLabels.push_back(d.product.empty() ? d.path.c_str() : d.product.c_str());
                }

                // Per-turntable rendering
                for (int p = 0; p < ANALOG_COUNT; p++) {
                    ImGui::PushID(p);
                    ImGui::Text("%s", analogs[p]);
                    ImGui::Separator();

                    // Device combo
                    int prevDevIdx = s_analogDeviceIdx[p];
                    if (ImGui::Combo("Device", &s_analogDeviceIdx[p], deviceLabels.data(), deviceCount)) {
                        if (s_analogDeviceIdx[p] != prevDevIdx) {
                            // Device changed — rebuild axis list
                            std::string path;
                            if (s_analogDeviceIdx[p] > 0) {
                                path = s_analogDevices[(size_t)(s_analogDeviceIdx[p] - 1)].path;
                            }
                            rebuildAxisList(p, path);
                            s_analogAxisIdx[p] = 0;
                            // Persist device_path
                            writeAnalogAxisField(p, "device_path", path);
                        }
                    }

                    // Axis combo
                    int axisCount = (int)s_axisLabels[p].size();
                    if (axisCount > 0) {
                        std::vector<const char*> axisLabelPtrs;
                        axisLabelPtrs.reserve((size_t)axisCount);
                        for (auto& lbl : s_axisLabels[p]) axisLabelPtrs.push_back(lbl.c_str());

                        if (ImGui::Combo("Axis", &s_analogAxisIdx[p], axisLabelPtrs.data(), axisCount)) {
                            auto& usage = s_axisUsages[p][(size_t)s_analogAxisIdx[p]];
                            writeAnalogAxisField(p, "usage_page", (int)usage.first);
                            writeAnalogAxisField(p, "usage_id",   (int)usage.second);
                        }
                    } else {
                        ImGui::TextDisabled("Axis: (select device first)");
                    }

                    // Reverse checkbox
                    if (ImGui::Checkbox("Reverse", &s_analogReverse[p])) {
                        writeAnalogAxisField(p, "reverse", s_analogReverse[p]);
                    }

                    // Sensitivity slider (0.1 to 5.0)
                    if (ImGui::SliderFloat("Sensitivity", &s_analogSensitivity[p], 0.1f, 5.0f)) {
                        writeAnalogAxisField(p, "sensitivity", s_analogSensitivity[p]);
                    }

                    // Dead zone slider (0 to 127)
                    if (ImGui::SliderInt("Dead Zone", &s_analogDeadZone[p], 0, 127)) {
                        writeAnalogAxisField(p, "dead_zone", s_analogDeadZone[p]);
                    }

                    // VTT step slider (1 to 10)
                    if (ImGui::SliderInt("VTT Step", &s_vttStep[p], 1, 10)) {
                        writeAnalogVttField(p, "step", s_vttStep[p]);
                    }

                    // VTT+ key capture
                    {
                        char vttPlusLabel[64] = "(unbound)";
                        if (s_vttPlusVK[p] != 0) {
                            UINT sc = MapVirtualKeyA((UINT)s_vttPlusVK[p], MAPVK_VK_TO_VSC);
                            GetKeyNameTextA((LONG)(sc << 16), vttPlusLabel, sizeof(vttPlusLabel));
                        }
                        ImGui::Text("VTT+: %s", vttPlusLabel);
                        ImGui::SameLine();
                        if (s_capturingVTT[p][0]) {
                            ImGui::TextColored({1.0f, 1.0f, 0.0f, 1.0f}, "[Press key...]");
                            // Scan for newly-pressed key (keyboard only)
                            for (int vk = 0x01; vk < 0xFF; vk++) {
                                bool pressed = (GetAsyncKeyState(vk) & 0x8000) != 0;
                                if (pressed && !s_vttPrevKeys[vk]) {
                                    if (vk != VK_LBUTTON && vk != VK_RBUTTON && vk != VK_MBUTTON) {
                                        s_vttPlusVK[p] = vk;
                                        writeAnalogVttField(p, "plus_vk", vk);
                                        s_capturingVTT[p][0] = false;
                                        s_vttPrevKeys[vk] = true;
                                        break;
                                    }
                                }
                                s_vttPrevKeys[vk] = pressed;
                            }
                        } else {
                            if (ImGui::Button("Bind##vttp")) {
                                s_capturingVTT[p][0] = true;
                                // Reset prevKeys to avoid immediate capture
                                for (int vk = 0; vk < 256; vk++)
                                    s_vttPrevKeys[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
                            }
                        }
                    }

                    // VTT- key capture
                    {
                        char vttMinusLabel[64] = "(unbound)";
                        if (s_vttMinusVK[p] != 0) {
                            UINT sc = MapVirtualKeyA((UINT)s_vttMinusVK[p], MAPVK_VK_TO_VSC);
                            GetKeyNameTextA((LONG)(sc << 16), vttMinusLabel, sizeof(vttMinusLabel));
                        }
                        ImGui::Text("VTT-: %s", vttMinusLabel);
                        ImGui::SameLine();
                        if (s_capturingVTT[p][1]) {
                            ImGui::TextColored({1.0f, 1.0f, 0.0f, 1.0f}, "[Press key...]");
                            for (int vk = 0x01; vk < 0xFF; vk++) {
                                bool pressed = (GetAsyncKeyState(vk) & 0x8000) != 0;
                                if (pressed && !s_vttPrevKeys[vk]) {
                                    if (vk != VK_LBUTTON && vk != VK_RBUTTON && vk != VK_MBUTTON) {
                                        s_vttMinusVK[p] = vk;
                                        writeAnalogVttField(p, "minus_vk", vk);
                                        s_capturingVTT[p][1] = false;
                                        s_vttPrevKeys[vk] = true;
                                        break;
                                    }
                                }
                                s_vttPrevKeys[vk] = pressed;
                            }
                        } else {
                            if (ImGui::Button("Bind##vttm")) {
                                s_capturingVTT[p][1] = true;
                                // Reset prevKeys to avoid immediate capture
                                for (int vk = 0; vk < 256; vk++)
                                    s_vttPrevKeys[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
                            }
                        }
                    }

                    // Live preview bar
                    uint8_t val  = Input::getAnalogValue(analogs[p]);
                    float   frac = (float)val / 255.0f;
                    char    overlay[32];
                    snprintf(overlay, sizeof(overlay), "%d/255", (int)val);
                    ImGui::ProgressBar(frac, ImVec2(-1, 0), overlay);

                    ImGui::Separator();
                    ImGui::PopID();
                }
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Lights"))   { ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    ImGui::SetCursorPos({ ImGui::GetWindowWidth() - 175, ImGui::GetWindowHeight() - 20 });
    ImGui::TextDisabled("Made by kasaski - 2022");

    ImGui::End();
}

static void setTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding    = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.PopupBorderSize  = 0.0f;
    style.GrabRounding     = 4.0f;

    ImVec4* c = ImGui::GetStyle().Colors;
    c[ImGuiCol_Text]                 = { 1.00f, 1.00f, 1.00f, 1.00f };
    c[ImGuiCol_TextDisabled]         = { 0.73f, 0.75f, 0.74f, 1.00f };
    c[ImGuiCol_WindowBg]             = { 0.09f, 0.09f, 0.09f, 0.94f };
    c[ImGuiCol_ChildBg]              = { 0.00f, 0.00f, 0.00f, 0.00f };
    c[ImGuiCol_PopupBg]              = { 0.08f, 0.08f, 0.08f, 0.94f };
    c[ImGuiCol_Border]               = { 0.20f, 0.20f, 0.20f, 0.50f };
    c[ImGuiCol_BorderShadow]         = { 0.00f, 0.00f, 0.00f, 0.00f };
    c[ImGuiCol_FrameBg]              = { 0.71f, 0.39f, 0.39f, 0.54f };
    c[ImGuiCol_FrameBgHovered]       = { 0.84f, 0.66f, 0.66f, 0.40f };
    c[ImGuiCol_FrameBgActive]        = { 0.84f, 0.66f, 0.66f, 0.67f };
    c[ImGuiCol_TitleBg]              = { 0.47f, 0.22f, 0.22f, 0.67f };
    c[ImGuiCol_TitleBgActive]        = { 0.47f, 0.22f, 0.22f, 1.00f };
    c[ImGuiCol_TitleBgCollapsed]     = { 0.47f, 0.22f, 0.22f, 0.67f };
    c[ImGuiCol_MenuBarBg]            = { 0.34f, 0.16f, 0.16f, 1.00f };
    c[ImGuiCol_ScrollbarBg]          = { 0.02f, 0.02f, 0.02f, 0.53f };
    c[ImGuiCol_ScrollbarGrab]        = { 0.31f, 0.31f, 0.31f, 1.00f };
    c[ImGuiCol_ScrollbarGrabHovered] = { 0.41f, 0.41f, 0.41f, 1.00f };
    c[ImGuiCol_ScrollbarGrabActive]  = { 0.51f, 0.51f, 0.51f, 1.00f };
    c[ImGuiCol_CheckMark]            = { 1.00f, 1.00f, 1.00f, 1.00f };
    c[ImGuiCol_SliderGrab]           = { 0.71f, 0.39f, 0.39f, 1.00f };
    c[ImGuiCol_SliderGrabActive]     = { 0.84f, 0.66f, 0.66f, 1.00f };
    c[ImGuiCol_Button]               = { 1.00f, 0.19f, 0.19f, 0.40f };
    c[ImGuiCol_ButtonHovered]        = { 0.71f, 0.39f, 0.39f, 0.65f };
    c[ImGuiCol_ButtonActive]         = { 0.20f, 0.20f, 0.20f, 0.50f };
    c[ImGuiCol_Header]               = { 0.56f, 0.10f, 0.10f, 1.00f };
    c[ImGuiCol_HeaderHovered]        = { 0.84f, 0.66f, 0.66f, 0.65f };
    c[ImGuiCol_HeaderActive]         = { 0.84f, 0.66f, 0.66f, 0.00f };
    c[ImGuiCol_Separator]            = { 0.43f, 0.43f, 0.50f, 0.50f };
    c[ImGuiCol_SeparatorHovered]     = { 0.56f, 0.10f, 0.10f, 1.00f };
    c[ImGuiCol_SeparatorActive]      = { 0.56f, 0.10f, 0.10f, 1.00f };
    c[ImGuiCol_ResizeGrip]           = { 0.56f, 0.10f, 0.10f, 1.00f };
    c[ImGuiCol_ResizeGripHovered]    = { 0.84f, 0.66f, 0.66f, 0.66f };
    c[ImGuiCol_ResizeGripActive]     = { 0.84f, 0.66f, 0.66f, 0.66f };
    c[ImGuiCol_Tab]                  = { 0.56f, 0.10f, 0.10f, 1.00f };
    c[ImGuiCol_TabHovered]           = { 0.84f, 0.66f, 0.66f, 0.66f };
    c[ImGuiCol_TabActive]            = { 0.84f, 0.66f, 0.66f, 0.66f };
    c[ImGuiCol_TabUnfocused]         = { 0.07f, 0.10f, 0.15f, 0.97f };
    c[ImGuiCol_TabUnfocusedActive]   = { 0.14f, 0.26f, 0.42f, 1.00f };
    c[ImGuiCol_PlotLines]            = { 0.61f, 0.61f, 0.61f, 1.00f };
    c[ImGuiCol_PlotLinesHovered]     = { 1.00f, 0.43f, 0.35f, 1.00f };
    c[ImGuiCol_PlotHistogram]        = { 0.90f, 0.70f, 0.00f, 1.00f };
    c[ImGuiCol_PlotHistogramHovered] = { 1.00f, 0.60f, 0.00f, 1.00f };
    c[ImGuiCol_TextSelectedBg]       = { 0.26f, 0.59f, 0.98f, 0.35f };
    c[ImGuiCol_DragDropTarget]       = { 1.00f, 1.00f, 0.00f, 0.90f };
    c[ImGuiCol_NavHighlight]         = { 0.41f, 0.41f, 0.41f, 1.00f };
    c[ImGuiCol_NavWindowingHighlight]= { 1.00f, 1.00f, 1.00f, 0.70f };
    c[ImGuiCol_NavWindowingDimBg]    = { 0.80f, 0.80f, 0.80f, 0.20f };
    c[ImGuiCol_ModalWindowDimBg]     = { 0.80f, 0.80f, 0.80f, 0.35f };
}
