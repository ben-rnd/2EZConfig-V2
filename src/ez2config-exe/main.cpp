#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
#include "injector.h"
#include "settings.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnarrowing"
#include "strings.h"
#pragma GCC diagnostic pop
#include "../libs/input/input_manager.h"
#include "bindings.h"
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

static InputManager*              g_input    = nullptr;
static BindingStore               g_bindings;
static constexpr int IO_COUNT       = (int)(sizeof(ioButtons)          / sizeof(ioButtons[0]));
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

    // Start input subsystem
    g_input = new InputManager();

    // Load bindings from settings (passes array pointers — no strings.h dep in binding layer)
    g_bindings.load(g_settings, *g_input, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K);

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

    delete g_input;
    g_input = nullptr;

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
            enum class BindState { Normal, Listening };
            static BindState s_state      = BindState::Normal;
            static BindState s_prevState  = BindState::Normal;
            static int       s_listenIdx  = -1;
            static float     s_listenTimer = 0.0f;
            static bool      s_prevKeys[256] = {};

            if (s_state != s_prevState) {
                if (s_state == BindState::Listening) g_input->startCapture();
                else                                 g_input->stopCapture();
                s_prevState = s_state;
            }

            const char** actionList  = g_isDancer ? ez2DancerIOButtons : ioButtons;
            const int    actionCount = g_isDancer ? DANCER_COUNT_K : IO_COUNT;

            ImGui::BeginChild("##buttonsScroll", ImVec2(0, 0), false);
            for (int i = 0; i < actionCount; i++) {
                ImGui::PushID(i);
                ButtonBinding& bnd = g_isDancer ? g_bindings.dancerButtons[i] : g_bindings.buttons[i];

                if (s_state == BindState::Listening && s_listenIdx == i) {
                    // Listening row
                    ImGui::TextColored(ImVec4(1,1,0,1), "Press a button...");
                    ImGui::SameLine();
                    char timerStr[16]; snprintf(timerStr, sizeof(timerStr), "(%.1fs)", s_listenTimer);
                    ImGui::TextUnformatted(timerStr);
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel")) {
                        g_input->stopCapture();
                        s_state     = BindState::Normal;
                        s_listenIdx = -1;
                    }

                    // Poll HID capture
                    auto hit = g_input->pollCapture();
                    if (hit) {
                        bnd = ButtonBinding::fromCapture(*hit);
                        g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K);
                        s_state     = BindState::Normal;
                        s_listenIdx = -1;
                    }

                    // Poll keyboard
                    if (s_state == BindState::Listening) {
                        for (int vk = 0x01; vk < 0xFF; vk++) {
                            bool pr = (GetAsyncKeyState(vk) & 0x8000) != 0;
                            if (pr && !s_prevKeys[vk] &&
                                vk != VK_LBUTTON && vk != VK_RBUTTON && vk != VK_MBUTTON) {
                                ButtonBinding kb;
                                kb.vk_code = vk;
                                bnd = kb;
                                g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K);
                                g_input->stopCapture();
                                s_state     = BindState::Normal;
                                s_listenIdx = -1;
                                s_prevKeys[vk] = true;
                                break;
                            }
                            s_prevKeys[vk] = pr;
                        }
                    }

                    // Timeout
                    s_listenTimer -= ImGui::GetIO().DeltaTime;
                    if (s_listenTimer <= 0.0f) {
                        g_input->stopCapture();
                        s_state = BindState::Normal;
                    }
                } else {
                    // Normal row
                    std::string label = bnd.getDisplayString(*g_input);
                    bool active = bnd.isSet() && (bnd.isKeyboard()
                        ? (GetAsyncKeyState(bnd.vk_code) & 0x8000) != 0
                        : g_input->getButtonState(bnd.device_path, bnd.button_idx));

                    ImGui::Text("%s:", actionList[i]);
                    ImGui::SameLine(160.0f);
                    if (active)
                        ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "%s", label.c_str());
                    else
                        ImGui::TextUnformatted(label.c_str());

                    ImGui::SameLine(310.0f);
                    if (ImGui::Button("Bind")) {
                        s_state       = BindState::Listening;
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
                            g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K);
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
                // Edit popup state
                static int   s_editPort = -1;
                static bool  s_openPopup = false;
                static int   s_devIdx[2]  = {0, 0};
                static int   s_axisIdx[2] = {0, 0};
                static bool  s_initialized[2] = {false, false};
                static bool  s_capturingVtt[2][2]  = {};
                static bool  s_vttPrevKeys[256]    = {};

                // Build device list for combo (only devices with axes)
                std::vector<Device> devs = g_input->getDevices();
                std::vector<Device> axisDevs;
                for (auto& d : devs)
                    if (!d.value_caps_names.empty()) axisDevs.push_back(d);

                // 3-column table: [name | binding | Edit button]
                if (ImGui::BeginTable("##analogTable", 3, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Turntable", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                    ImGui::TableSetupColumn("Binding",   ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("##editbtn", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableHeadersRow();

                    for (int p = 0; p < BindingStore::ANALOG_COUNT; p++) {
                        ImGui::PushID(p);
                        ImGui::TableNextRow();

                        // Col 0: name + live preview bar
                        ImGui::TableSetColumnIndex(0);
                        AnalogBinding& ab = g_bindings.analogs[p];
                        if (ab.isSet()) {
                            float rawAxis = g_input->getAxisValue(ab.device_path, ab.axis_idx);
                            if (ab.reverse) rawAxis = 1.0f - rawAxis;
                            if (ab.sensitivity != 1.0f) {
                                float dev = (rawAxis - 0.5f) * ab.sensitivity;
                                rawAxis = std::clamp(0.5f + dev, 0.0f, 1.0f);
                            }
                            if (ab.dead_zone > 0.0f) {
                                float dist = std::abs(rawAxis - 0.5f);
                                if (dist < ab.dead_zone) rawAxis = 0.5f;
                            }
                            uint8_t vttPos = g_input->getVttPosition(p);
                            float combined = std::clamp(rawAxis + (float)(vttPos - 128) / 255.0f, 0.0f, 1.0f);
                            char overlay[32]; snprintf(overlay, sizeof(overlay), "%.0f/255", combined * 255.0f);
                            ImGui::ProgressBar(combined, ImVec2(100.0f, 0), overlay);
                            ImGui::SameLine();
                        }
                        ImGui::TextUnformatted(analogs[p]);

                        // Col 1: binding label
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(ab.getDisplayString(*g_input).c_str());

                        // Col 2: [Edit] [Clear] buttons
                        ImGui::TableSetColumnIndex(2);
                        if (ImGui::Button("Edit")) {
                            s_editPort = p;
                            s_openPopup = true;
                            s_initialized[p] = false;
                        }
                        if (ab.isSet()) {
                            ImGui::SameLine();
                            if (ImGui::Button("Clear")) {
                                ab.clear();
                                g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K);
                                g_input->setVttKeys(p, 0, 0, 3);
                            }
                        }

                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }

                // Open popup outside PushID scope
                if (s_openPopup) { ImGui::OpenPopup("EditAnalog"); s_openPopup = false; }

                // Edit popup modal
                if (ImGui::BeginPopupModal("EditAnalog", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    int p = s_editPort;
                    if (p >= 0 && p < BindingStore::ANALOG_COUNT) {
                        ImGui::PushID(p);
                        ImGui::Text("Editing: %s", analogs[p]);
                        ImGui::Separator();
                        AnalogBinding& ab = g_bindings.analogs[p];

                        // Initialize combo selection once per popup open
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

                        // Device combo (axes-only devices + "(none)")
                        std::vector<const char*> devLabels;
                        devLabels.push_back("(none)");
                        for (auto& d : axisDevs) devLabels.push_back(d.name.c_str());
                        if (ImGui::Combo("Device", &s_devIdx[p], devLabels.data(), (int)devLabels.size())) {
                            s_axisIdx[p] = 0;
                            if (s_devIdx[p] > 0) {
                                const Device& d = axisDevs[(size_t)(s_devIdx[p] - 1)];
                                ab.device_path = d.path; ab.device_name = d.name; ab.axis_idx = 0;
                            } else { ab.clearAxis(); }
                            g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K);
                        }

                        // Axis combo (value_caps_names[] of selected device)
                        if (s_devIdx[p] > 0) {
                            const Device& d = axisDevs[(size_t)(s_devIdx[p] - 1)];
                            int axCount = (int)d.value_caps_names.size();
                            std::vector<const char*> axLabels;
                            for (auto& l : d.value_caps_names) axLabels.push_back(l.c_str());
                            if (axCount > 0 && ImGui::Combo("Axis", &s_axisIdx[p], axLabels.data(), axCount)) {
                                ab.axis_idx = s_axisIdx[p];
                                g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K);
                            }
                        } else {
                            ImGui::TextDisabled("Axis: (select device first)");
                        }

                        // Reverse / Sensitivity / Dead Zone
                        if (ImGui::Checkbox("Reverse", &ab.reverse))
                            g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K);
                        if (ImGui::SliderFloat("Sensitivity", &ab.sensitivity, 0.1f, 5.0f))
                            g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K);
                        if (ImGui::SliderFloat("Dead Zone", &ab.dead_zone, 0.0f, 0.5f))
                            g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K);

                        // VTT Step
                        if (ImGui::SliderInt("VTT Step", &ab.vtt_step, 1, 10)) {
                            g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K);
                            g_input->setVttKeys(p, ab.vtt_plus_vk, ab.vtt_minus_vk, ab.vtt_step);
                        }

                        // VTT+ key capture
                        {
                            char vttPlusLabel[64] = "(unbound)";
                            if (ab.vtt_plus_vk != 0) {
                                UINT sc = MapVirtualKeyA((UINT)ab.vtt_plus_vk, MAPVK_VK_TO_VSC);
                                GetKeyNameTextA((LONG)(sc << 16), vttPlusLabel, sizeof(vttPlusLabel));
                            }
                            ImGui::Text("VTT+: %s", vttPlusLabel); ImGui::SameLine();
                            if (s_capturingVtt[p][0]) {
                                ImGui::TextColored({1,1,0,1}, "[Press key...]");
                                for (int vk = 0x01; vk < 0xFF; vk++) {
                                    bool pr = (GetAsyncKeyState(vk) & 0x8000) != 0;
                                    if (pr && !s_vttPrevKeys[vk] && vk != VK_LBUTTON && vk != VK_RBUTTON && vk != VK_MBUTTON) {
                                        ab.vtt_plus_vk = vk;
                                        g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K);
                                        g_input->setVttKeys(p, ab.vtt_plus_vk, ab.vtt_minus_vk, ab.vtt_step);
                                        s_capturingVtt[p][0] = false; s_vttPrevKeys[vk] = true; break;
                                    }
                                    s_vttPrevKeys[vk] = pr;
                                }
                            } else {
                                if (ImGui::Button("Bind##vttp")) {
                                    s_capturingVtt[p][0] = true;
                                    for (int vk = 0; vk < 256; vk++) s_vttPrevKeys[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
                                }
                            }
                        }

                        // VTT- key capture
                        {
                            char vttMinusLabel[64] = "(unbound)";
                            if (ab.vtt_minus_vk != 0) {
                                UINT sc = MapVirtualKeyA((UINT)ab.vtt_minus_vk, MAPVK_VK_TO_VSC);
                                GetKeyNameTextA((LONG)(sc << 16), vttMinusLabel, sizeof(vttMinusLabel));
                            }
                            ImGui::Text("VTT-: %s", vttMinusLabel); ImGui::SameLine();
                            if (s_capturingVtt[p][1]) {
                                ImGui::TextColored({1,1,0,1}, "[Press key...]");
                                for (int vk = 0x01; vk < 0xFF; vk++) {
                                    bool pr = (GetAsyncKeyState(vk) & 0x8000) != 0;
                                    if (pr && !s_vttPrevKeys[vk] && vk != VK_LBUTTON && vk != VK_RBUTTON && vk != VK_MBUTTON) {
                                        ab.vtt_minus_vk = vk;
                                        g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K);
                                        g_input->setVttKeys(p, ab.vtt_plus_vk, ab.vtt_minus_vk, ab.vtt_step);
                                        s_capturingVtt[p][1] = false; s_vttPrevKeys[vk] = true; break;
                                    }
                                    s_vttPrevKeys[vk] = pr;
                                }
                            } else {
                                if (ImGui::Button("Bind##vttm")) {
                                    s_capturingVtt[p][1] = true;
                                    for (int vk = 0; vk < 256; vk++) s_vttPrevKeys[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
                                }
                            }
                        }

                        // Live preview
                        if (ab.isSet()) {
                            float rawAxis = g_input->getAxisValue(ab.device_path, ab.axis_idx);
                            if (ab.reverse) rawAxis = 1.0f - rawAxis;
                            if (ab.sensitivity != 1.0f) {
                                float dev = (rawAxis - 0.5f) * ab.sensitivity;
                                rawAxis = std::clamp(0.5f + dev, 0.0f, 1.0f);
                            }
                            if (ab.dead_zone > 0.0f) {
                                float dist = std::abs(rawAxis - 0.5f);
                                if (dist < ab.dead_zone) rawAxis = 0.5f;
                            }
                            uint8_t vttPos = g_input->getVttPosition(p);
                            float combined = std::clamp(rawAxis + (float)(vttPos - 128) / 255.0f, 0.0f, 1.0f);
                            char overlay[32]; snprintf(overlay, sizeof(overlay), "%.0f/255", combined * 255.0f);
                            ImGui::ProgressBar(combined, ImVec2(-1, 0), overlay);
                        } else {
                            ImGui::ProgressBar(0.5f, ImVec2(-1, 0), "(unbound)");
                        }

                        ImGui::PopID();
                    }
                    ImGui::Separator();
                    if (ImGui::Button("Close", ImVec2(120, 0))) {
                        ImGui::CloseCurrentPopup();
                        s_editPort = -1;
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
