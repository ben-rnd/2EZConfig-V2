#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
#include "injector.h"
#include "settings.h"
#include "../libs/input/input.h"
#include "bindings.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnarrowing"
#include "strings.h"
#pragma GCC diagnostic pop
#include <GLFW/glfw3.h>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

static void renderUI();
static void setTheme();

// File-scope settings and game selector state — accessible to all tab functions
static SettingsManager g_settings;
static int  g_gameIdx  = 0;     // index into flat combo list (DJ games first, then Dancer)
static bool g_isDancer = false; // derived from g_gameIdx

// File-scope binding store and device list
static BindingStore              g_bindings;
static std::vector<Input::DeviceDesc> g_devices;

// Array size helpers — computed once (strings.h arrays are file-scope statics)
static constexpr int IO_COUNT     = (int)(sizeof(ioButtons)          / sizeof(ioButtons[0]));
static constexpr int DANCER_COUNT_K = (int)(sizeof(ez2DancerIOButtons) / sizeof(ez2DancerIOButtons[0]));

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

    // Start input subsystem (no settings parameter — clean API)
    Input::init();

    // Cache device list once at startup
    g_devices = Input::getDevices();

    // Load bindings from settings (passes array pointers — no strings.h dep in binding layer)
    g_bindings.load(g_settings, ioButtons, ez2DancerIOButtons, IO_COUNT, DANCER_COUNT_K);

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
            static BindState s_bindState     = BindState::Normal;
            static BindState s_prevBindState = BindState::Normal;
            static int       s_listenIdx     = -1;
            static float     s_listenTimer   = 0.0f;

            // Propagate capture mode to input subsystem
            if (s_bindState != s_prevBindState) {
                if (s_bindState == BindState::Listening)
                    Input::startCapture();
                else
                    Input::stopCapture();
                s_prevBindState = s_bindState;
            }

            // Select action list and binding array based on game type
            const char** actionList  = g_isDancer ? ez2DancerIOButtons : ioButtons;
            const int    actionCount = g_isDancer ? DANCER_COUNT_K : IO_COUNT;

            // Scrollable region to contain all action rows
            ImGui::BeginChild("##button_rows", ImVec2(0, 0), false);
            for (int i = 0; i < actionCount; i++) {
                ImGui::PushID(i);

                // Get the correct binding array entry
                ButtonBinding& bnd = g_isDancer ? g_bindings.dancerButtons[i] : g_bindings.buttons[i];

                if (s_bindState == BindState::Listening && s_listenIdx == i) {
                    // LISTENING state for this row
                    ImGui::TextColored({1.0f, 1.0f, 0.0f, 1.0f}, "Press a button...");
                    ImGui::SameLine();
                    char timerBuf[16];
                    snprintf(timerBuf, sizeof(timerBuf), "(%.0fs)", s_listenTimer);
                    ImGui::TextDisabled(timerBuf);
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel")) {
                        Input::stopCapture();
                        s_bindState = BindState::Normal;
                        s_listenIdx = -1;
                    }

                    // Poll for HID button press via new capture API
                    auto hit = Input::pollCapture();
                    if (hit.has_value()) {
                        bnd = ButtonBinding::fromCapture(*hit);
                        g_bindings.save(g_settings, ioButtons, ez2DancerIOButtons, IO_COUNT, DANCER_COUNT_K);
                        s_bindState = BindState::Normal;
                        s_listenIdx = -1;
                        // No Input::shutdown/init — manager always running
                    }

                    // Poll for keyboard key press (scan VK 0x01..0xFE)
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
                                    ButtonBinding kb;
                                    kb.vk_code = vk;
                                    bnd = kb;
                                    g_bindings.save(g_settings, ioButtons, ez2DancerIOButtons, IO_COUNT, DANCER_COUNT_K);
                                    s_bindState = BindState::Normal;
                                    s_listenIdx = -1;
                                    prevKeys[vk] = true;
                                    // No Input::shutdown/init — manager always running
                                    break;
                                }
                            }
                            prevKeys[vk] = pressed;
                        }
                    }

                    // Timeout
                    s_listenTimer -= ImGui::GetIO().DeltaTime;
                    if (s_listenTimer <= 0.0f) {
                        Input::stopCapture();
                        s_bindState = BindState::Normal;
                        s_listenIdx = -1;
                    }
                } else {
                    // NORMAL state for this row
                    std::string bindLabel = bnd.getDisplayString(g_devices);

                    // Active indicator: use getButtonState for HID, GetAsyncKeyState for keyboard
                    bool isPressed = false;
                    if (bnd.isSet()) {
                        if (bnd.isKeyboard()) {
                            isPressed = (GetAsyncKeyState(bnd.vk_code) & 0x8000) != 0;
                        } else {
                            isPressed = Input::getButtonState(bnd.device_id, bnd.button_idx);
                        }
                    }

                    ImGui::Text("%s:", actionList[i]);
                    ImGui::SameLine(160.0f);
                    if (isPressed) {
                        ImGui::TextColored({1.0f, 1.0f, 0.0f, 1.0f}, "%s", bindLabel.c_str());
                    } else {
                        ImGui::TextUnformatted(bindLabel.c_str());
                    }
                    ImGui::SameLine(310.0f);
                    if (ImGui::Button("Bind")) {
                        s_bindState   = BindState::Listening;
                        s_listenIdx   = i;
                        s_listenTimer = 5.0f;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Clear")) {
                        bnd.clear();
                        g_bindings.save(g_settings, ioButtons, ez2DancerIOButtons, IO_COUNT, DANCER_COUNT_K);
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
                static constexpr int ANALOG_COUNT = BindingStore::ANALOG_COUNT;  // 2

                // VTT capture state: [port][0=plus, 1=minus]
                static bool  s_capturingVTT[2][2]   = {{false,false},{false,false}};
                static bool  s_vttPrevKeys[256]     = {};

                // Edit popup per-port state (combo indices, loaded once per popup open)
                static int   s_editPopupDevIdx[2]   = {0, 0};  // combo index (0 = none)
                static int   s_editPopupAxisIdx[2]  = {0, 0};
                static bool  s_editPopupInitialized[2] = {false, false};

                // Table: one row per turntable
                static int  s_editPopupPort = -1;
                static bool s_openEditPopup = false;

                if (ImGui::BeginTable("##analogTable", 3, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Turntable",   ImGuiTableColumnFlags_WidthFixed, 110.0f);
                    ImGui::TableSetupColumn("Binding",     ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("##edit_btn",  ImGuiTableColumnFlags_WidthFixed, 50.0f);
                    ImGui::TableHeadersRow();

                    for (int p = 0; p < ANALOG_COUNT; p++) {
                        ImGui::PushID(p);
                        ImGui::TableNextRow();

                        // Column 0: name (display label from strings.h)
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(analogs[p]);

                        // Column 1: binding summary — delegate to AnalogBinding::getDisplayString
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(g_bindings.analogs[p].getDisplayString(g_devices).c_str());

                        // Column 2: Edit button
                        ImGui::TableSetColumnIndex(2);
                        if (ImGui::Button("Edit")) {
                            s_editPopupPort = p;
                            s_openEditPopup = true;
                            s_editPopupInitialized[p] = false;  // re-initialize combo indices on open
                        }

                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }

                // Open the popup here — outside every PushID scope
                if (s_openEditPopup) {
                    ImGui::OpenPopup("EditAnalog");
                    s_openEditPopup = false;
                }

                // Modal popup — rendered once, outside the table loop
                if (ImGui::BeginPopupModal("EditAnalog", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    int p = s_editPopupPort;
                    if (p >= 0 && p < ANALOG_COUNT) {
                        ImGui::PushID(p);
                        ImGui::Text("Editing: %s", analogs[p]);
                        ImGui::Separator();

                        AnalogBinding& ab = g_bindings.analogs[p];

                        // Initialize combo indices from current binding (once per popup open)
                        if (!s_editPopupInitialized[p]) {
                            s_editPopupInitialized[p] = true;
                            s_editPopupDevIdx[p]  = 0;
                            s_editPopupAxisIdx[p] = 0;
                            if (ab.isSet()) {
                                for (int di = 0; di < (int)g_devices.size(); di++) {
                                    if (g_devices[(size_t)di].id == ab.device_id) {
                                        s_editPopupDevIdx[p] = di + 1;  // +1 for "(none)"
                                        s_editPopupAxisIdx[p] = (ab.axis_idx >= 0) ? ab.axis_idx : 0;
                                        break;
                                    }
                                }
                            }
                        }

                        // Build device labels: index 0 = "(none)", 1..N = device names
                        int deviceCount = (int)g_devices.size() + 1;
                        std::vector<const char*> deviceLabels;
                        deviceLabels.reserve((size_t)deviceCount);
                        deviceLabels.push_back("(none)");
                        for (auto& d : g_devices)
                            deviceLabels.push_back(d.name.c_str());

                        // Device combo
                        if (ImGui::Combo("Device", &s_editPopupDevIdx[p], deviceLabels.data(), deviceCount)) {
                            s_editPopupAxisIdx[p] = 0;
                            if (s_editPopupDevIdx[p] > 0) {
                                const auto& desc = g_devices[(size_t)(s_editPopupDevIdx[p] - 1)];
                                ab.device_id   = desc.id;
                                ab.device_name = desc.name;
                                ab.axis_idx    = 0;
                            } else {
                                ab.clearAxis();
                            }
                            g_bindings.save(g_settings, ioButtons, ez2DancerIOButtons, IO_COUNT, DANCER_COUNT_K);
                            // No Input::shutdown/init — manager always running
                        }

                        // Axis combo — from DeviceDesc::axis_labels
                        int axisCount = 0;
                        std::vector<const char*> axisLabelPtrs;
                        if (s_editPopupDevIdx[p] > 0) {
                            const auto& desc = g_devices[(size_t)(s_editPopupDevIdx[p] - 1)];
                            axisCount = (int)desc.axis_labels.size();
                            for (auto& lbl : desc.axis_labels)
                                axisLabelPtrs.push_back(lbl.c_str());
                        }
                        if (axisCount > 0) {
                            if (ImGui::Combo("Axis", &s_editPopupAxisIdx[p], axisLabelPtrs.data(), axisCount)) {
                                ab.axis_idx = s_editPopupAxisIdx[p];
                                g_bindings.save(g_settings, ioButtons, ez2DancerIOButtons, IO_COUNT, DANCER_COUNT_K);
                                // No Input::shutdown/init — manager always running
                            }
                        } else {
                            ImGui::TextDisabled("Axis: (select device first)");
                        }

                        // Reverse / sensitivity / dead_zone
                        if (ImGui::Checkbox("Reverse", &ab.reverse)) {
                            g_bindings.save(g_settings, ioButtons, ez2DancerIOButtons, IO_COUNT, DANCER_COUNT_K);
                        }
                        if (ImGui::SliderFloat("Sensitivity", &ab.sensitivity, 0.1f, 5.0f)) {
                            g_bindings.save(g_settings, ioButtons, ez2DancerIOButtons, IO_COUNT, DANCER_COUNT_K);
                        }
                        if (ImGui::SliderFloat("Dead Zone", &ab.dead_zone, 0.0f, 0.5f)) {
                            g_bindings.save(g_settings, ioButtons, ez2DancerIOButtons, IO_COUNT, DANCER_COUNT_K);
                        }

                        // VTT step
                        if (ImGui::SliderInt("VTT Step", &ab.vtt_step, 1, 10)) {
                            g_bindings.save(g_settings, ioButtons, ez2DancerIOButtons, IO_COUNT, DANCER_COUNT_K);
                            Input::setVttKeys(p, ab.vtt_plus_vk, ab.vtt_minus_vk, ab.vtt_step);
                        }

                        // VTT+ capture
                        {
                            char vttPlusLabel[64] = "(unbound)";
                            if (ab.vtt_plus_vk != 0) {
                                UINT sc = MapVirtualKeyA((UINT)ab.vtt_plus_vk, MAPVK_VK_TO_VSC);
                                GetKeyNameTextA((LONG)(sc << 16), vttPlusLabel, sizeof(vttPlusLabel));
                            }
                            ImGui::Text("VTT+: %s", vttPlusLabel);
                            ImGui::SameLine();
                            if (s_capturingVTT[p][0]) {
                                ImGui::TextColored({1.0f, 1.0f, 0.0f, 1.0f}, "[Press key...]");
                                for (int vk = 0x01; vk < 0xFF; vk++) {
                                    bool pressed = (GetAsyncKeyState(vk) & 0x8000) != 0;
                                    if (pressed && !s_vttPrevKeys[vk]) {
                                        if (vk != VK_LBUTTON && vk != VK_RBUTTON && vk != VK_MBUTTON) {
                                            ab.vtt_plus_vk = vk;
                                            g_bindings.save(g_settings, ioButtons, ez2DancerIOButtons, IO_COUNT, DANCER_COUNT_K);
                                            Input::setVttKeys(p, ab.vtt_plus_vk, ab.vtt_minus_vk, ab.vtt_step);
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
                                    for (int vk = 0; vk < 256; vk++)
                                        s_vttPrevKeys[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
                                }
                            }
                        }

                        // VTT- capture
                        {
                            char vttMinusLabel[64] = "(unbound)";
                            if (ab.vtt_minus_vk != 0) {
                                UINT sc = MapVirtualKeyA((UINT)ab.vtt_minus_vk, MAPVK_VK_TO_VSC);
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
                                            ab.vtt_minus_vk = vk;
                                            g_bindings.save(g_settings, ioButtons, ez2DancerIOButtons, IO_COUNT, DANCER_COUNT_K);
                                            Input::setVttKeys(p, ab.vtt_plus_vk, ab.vtt_minus_vk, ab.vtt_step);
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
                                    for (int vk = 0; vk < 256; vk++)
                                        s_vttPrevKeys[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
                                }
                            }
                        }

                        // Live preview — always-running InputManager, no shutdown/init cycle
                        if (ab.isSet()) {
                            float rawAxis = Input::getAxisValue(ab.device_id, ab.axis_idx);
                            // Apply reverse
                            if (ab.reverse) rawAxis = 1.0f - rawAxis;
                            // Apply sensitivity (scale deviation from center)
                            if (ab.sensitivity != 1.0f) {
                                float dev = (rawAxis - 0.5f) * ab.sensitivity;
                                rawAxis = std::clamp(0.5f + dev, 0.0f, 1.0f);
                            }
                            // Apply dead_zone: snap to center if within dead_zone of 0.5
                            if (ab.dead_zone > 0.0f) {
                                float dist = rawAxis - 0.5f;
                                if (dist < 0.0f) dist = -dist;
                                if (dist < ab.dead_zone) rawAxis = 0.5f;
                            }
                            // Add VTT contribution from always-running VTT thread
                            uint8_t vttPos = Input::getVttPosition(p);
                            float combined = std::clamp(rawAxis + (float)(vttPos - 128) / 255.0f, 0.0f, 1.0f);
                            char overlay[32];
                            snprintf(overlay, sizeof(overlay), "%.0f/255", combined * 255.0f);
                            ImGui::ProgressBar(combined, ImVec2(-1, 0), overlay);
                        } else {
                            ImGui::ProgressBar(0.5f, ImVec2(-1, 0), "(unbound)");
                        }

                        ImGui::PopID();
                    }

                    ImGui::Separator();
                    if (ImGui::Button("Close", ImVec2(120, 0))) {
                        ImGui::CloseCurrentPopup();
                        s_editPopupPort = -1;
                    }
                    ImGui::EndPopup();
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
