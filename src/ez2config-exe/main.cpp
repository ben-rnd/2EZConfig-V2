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
static constexpr int LIGHT_COUNT_M  = (int)(sizeof(lights)             / sizeof(lights[0]));

// VTT position state (center=128). Polled from render loop each frame.
static uint8_t g_vtt_pos[2] = {128, 128};

// Returns true if the given VttKey is currently held (keyboard or HID button).
static bool checkVttKey(const VttKey& k) {
    if (k.vk != 0)
        return (GetAsyncKeyState(k.vk) & 0x8000) != 0;
    if (!k.device_path.empty() && k.button_idx >= 0)
        return g_input->getButtonState(k.device_path, k.button_idx);
    return false;
}

int main() {
    glfwSetErrorCallback([](int e, const char* d) { fprintf(stderr, "GLFW error %d: %s\n", e, d); });
    if (!glfwInit()) return 1;

    GLFWwindow* window = glfwCreateWindow(640, 480, "2EZConfig", nullptr, nullptr);
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
    g_bindings.load(g_settings, *g_input, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K, lights, LIGHT_COUNT_M);

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
    // Poll VTT each frame — supports both keyboard VK and HID controller button.
    for (int port = 0; port < 2; port++) {
        const AnalogBinding& ab = g_bindings.analogs[port];
        if (ab.vtt_plus.isSet() && checkVttKey(ab.vtt_plus))
            g_vtt_pos[port] = (uint8_t)(((int)g_vtt_pos[port] + ab.vtt_step) & 0xFF);
        if (ab.vtt_minus.isSet() && checkVttKey(ab.vtt_minus))
            g_vtt_pos[port] = (uint8_t)(((int)g_vtt_pos[port] - ab.vtt_step + 256) & 0xFF);
    }

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
            
            ImGui::BeginChild("##buttonsScroll", ImVec2(0, ImGui::GetWindowHeight() - 85), false);
            // 3-column table: [name | binding | Edit button]
            if (ImGui::BeginTable("##buttonable", 3, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Binding",   ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableHeadersRow();

                for (int i = 0; i < actionCount; i++) {
                    ImGui::PushID(i);
                    ImGui::TableNextRow();
                    ButtonBinding& bnd = g_isDancer ? g_bindings.dancerButtons[i] : g_bindings.buttons[i];

                    if (s_state == BindState::Listening && s_listenIdx == i) {
                        ImGui::TableSetColumnIndex(0);
                        // Listening row
                        ImGui::TextColored(ImVec4(1,1,0,1), "Press a button...");
                        ImGui::TableSetColumnIndex(1);
                        char timerStr[16]; snprintf(timerStr, sizeof(timerStr), "(%.1fs)", s_listenTimer);
                        ImGui::TextUnformatted(timerStr);
                        ImGui::TableSetColumnIndex(2);
                        if (ImGui::Button("Cancel")) {
                            g_input->stopCapture();
                            s_state     = BindState::Normal;
                            s_listenIdx = -1;
                        }

                        // Poll HID capture
                        auto hit = g_input->pollCapture();
                        if (hit) {
                            bnd = ButtonBinding::fromCapture(*hit);
                            g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K, lights, LIGHT_COUNT_M);
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
                                    g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K, lights, LIGHT_COUNT_M);
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
                        ImGui::TableSetColumnIndex(0);

                        // Active highlight
                        bool active = bnd.isSet() && (bnd.isKeyboard()
                            ? (GetAsyncKeyState(bnd.vk_code) & 0x8000) != 0
                            : g_input->getButtonState(bnd.device_path, bnd.button_idx));

                        if (active)
                            ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "%s:", actionList[i]);
                        else
                            ImGui::TextUnformatted(actionList[i]);

                        ImGui::TableSetColumnIndex(1);

                        if (active)
                            ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "%s", bnd.getDisplayString(*g_input).c_str());
                        else
                            ImGui::TextUnformatted(bnd.getDisplayString(*g_input).c_str());

                        ImGui::TableSetColumnIndex(2);

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
                                g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K, lights, LIGHT_COUNT_M);
                            }
                        }
                    }

                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        // ---- Analogs Tab ----
        if (!g_isDancer) {
            if (ImGui::BeginTabItem("Analogs")) {
                    // Edit popup state
                    static int   s_editPort = -1;
                    static bool  s_openPopup = false;
                    static int   s_devIdx[2]  = {0, 0};
                    static int   s_axisIdx[2] = {0, 0};
                    static bool  s_initialized[2] = {false, false};
                    // VTT capture: [port][0=plus, 1=minus]
                    static bool  s_capturingVtt[2][2]  = {};
                    // Keyboard prev-keys for VTT capture (shared across both directions)
                    static bool  s_vttPrevKeys[256]    = {};

                    // Build device list for combo: only Generic Desktop (page 0x01)
                    // devices with axes. Excludes consumer control (0x0C), system
                    // control, and other non-joystick HID collections.
                    std::vector<Device> devs = g_input->getDevices();
                    std::vector<Device> axisDevs;
                    for (auto& d : devs)
                        if (!d.value_caps_names.empty() && d.hid && d.hid->caps.UsagePage == 0x01)
                            axisDevs.push_back(d);

                    // 3-column table: [name | binding | Edit button]
                    if (ImGui::BeginTable("##analogTable", 3, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg)) {
                        ImGui::TableSetupColumn("Turntable", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                        ImGui::TableSetupColumn("Binding",   ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                        ImGui::TableHeadersRow();

                        for (int p = 0; p < BindingStore::ANALOG_COUNT; p++) {
                            ImGui::PushID(p);
                            ImGui::TableNextRow();

                            // Col 0: name + live preview bar
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("P%d", p+1);
                            AnalogBinding& ab = g_bindings.analogs[p];
                            if(ab.isSet() || ab.hasVtt()){
                                ImGui::SameLine();
                                float display = ab.getPosition(*g_input, g_vtt_pos[p]) / 255.0f;
                                ImGui::ProgressBar(display, ImVec2(40.0f, 0));
                            }

                            // Col 1: binding label
                            ImGui::TableSetColumnIndex(1);
                            
                            if(!ab.isSet() && ab.hasVtt()){
                                ImGui::TextUnformatted("Virtual TT keys Assigned");
                            }else{
                                ImGui::TextUnformatted(ab.getDisplayString(*g_input).c_str());
                            }

                            // Col 2: [Edit] [Clear] buttons
                            ImGui::TableSetColumnIndex(2);
                            if (ImGui::Button("Edit")) {
                                s_editPort = p;
                                s_openPopup = true;
                                s_initialized[p] = false;
                            }
                            if (ab.isSet() || ab.hasVtt()) {
                                ImGui::SameLine();
                                if (ImGui::Button("Clear")) {
                                    ab.clear();
                                    g_vtt_pos[p] = 128;
                                    g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K, lights, LIGHT_COUNT_M);
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
                                g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K, lights, LIGHT_COUNT_M);
                            }

                            // Axis combo (value_caps_names[] of selected device)
                            if (s_devIdx[p] > 0) {
                                const Device& d = axisDevs[(size_t)(s_devIdx[p] - 1)];
                                int axCount = (int)d.value_caps_names.size();
                                std::vector<const char*> axLabels;
                                for (auto& l : d.value_caps_names) axLabels.push_back(l.c_str());
                                if (axCount > 0 && ImGui::Combo("Axis", &s_axisIdx[p], axLabels.data(), axCount)) {
                                    ab.axis_idx = s_axisIdx[p];
                                    g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K, lights, LIGHT_COUNT_M);
                                }
                            } else {
                                ImGui::TextDisabled("Axis: (select device first)");
                            }

                            // Reverse checkbox
                            if (ImGui::Checkbox("Reverse", &ab.reverse))
                                g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K, lights, LIGHT_COUNT_M);

                            ImGui::Separator();

                            // VTT- capture — accepts HID button OR keyboard key
                            {
                                ImGui::Text("Virtual TT-: %s", ab.vtt_minus.getLabel().c_str());
                                ImGui::SameLine();
                                if (s_capturingVtt[p][1]) {
                                    ImGui::TextColored(ImVec4(1,1,0,1), "[Press button or key...]");

                                    // Poll HID capture
                                    auto hit = g_input->pollCapture();
                                    if (hit) {
                                        ab.vtt_minus.vk          = 0;
                                        ab.vtt_minus.device_path = hit->path;
                                        ab.vtt_minus.device_name = hit->device_name;
                                        ab.vtt_minus.button_idx  = hit->button_idx;
                                        g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K, lights, LIGHT_COUNT_M);
                                        g_input->stopCapture();
                                        s_capturingVtt[p][1] = false;
                                    } else {
                                        // Poll keyboard
                                        for (int vk = 0x01; vk < 0xFF; vk++) {
                                            bool pr = (GetAsyncKeyState(vk) & 0x8000) != 0;
                                            if (pr && !s_vttPrevKeys[vk] &&
                                                vk != VK_LBUTTON && vk != VK_RBUTTON && vk != VK_MBUTTON) {
                                                ab.vtt_minus.clear();
                                                ab.vtt_minus.vk = vk;
                                                g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K, lights, LIGHT_COUNT_M);
                                                g_input->stopCapture();
                                                s_capturingVtt[p][1] = false;
                                                s_vttPrevKeys[vk] = true;
                                                break;
                                            }
                                            s_vttPrevKeys[vk] = pr;
                                        }
                                    }
                                } else {
                                    if (ImGui::Button("Bind##vttm")) {
                                        // Stop any other VTT capture in progress
                                        s_capturingVtt[p][0] = false;
                                        s_capturingVtt[p][1] = true;
                                        g_input->startCapture();
                                        for (int vk = 0; vk < 256; vk++)
                                            s_vttPrevKeys[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
                                    }
                                    if (ab.vtt_minus.isSet()) {
                                        ImGui::SameLine();
                                        if (ImGui::Button("Clear##vttm")) {
                                            ab.vtt_minus.clear();
                                            g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K, lights, LIGHT_COUNT_M);
                                        }
                                    }
                                }
                            }
                            
                            
                            // VTT+ capture — accepts HID button OR keyboard key
                            {
                                ImGui::Text("Virtual TT+: %s", ab.vtt_plus.getLabel().c_str());
                                ImGui::SameLine();
                                if (s_capturingVtt[p][0]) {
                                    ImGui::TextColored(ImVec4(1,1,0,1), "[Press button or key...]");

                                    // Poll HID capture
                                    auto hit = g_input->pollCapture();
                                    if (hit) {
                                        ab.vtt_plus.vk          = 0;
                                        ab.vtt_plus.device_path = hit->path;
                                        ab.vtt_plus.device_name = hit->device_name;
                                        ab.vtt_plus.button_idx  = hit->button_idx;
                                        g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K, lights, LIGHT_COUNT_M);
                                        g_input->stopCapture();
                                        s_capturingVtt[p][0] = false;
                                    } else {
                                        // Poll keyboard
                                        for (int vk = 0x01; vk < 0xFF; vk++) {
                                            bool pr = (GetAsyncKeyState(vk) & 0x8000) != 0;
                                            if (pr && !s_vttPrevKeys[vk] &&
                                                vk != VK_LBUTTON && vk != VK_RBUTTON && vk != VK_MBUTTON) {
                                                ab.vtt_plus.clear();
                                                ab.vtt_plus.vk = vk;
                                                g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K, lights, LIGHT_COUNT_M);
                                                g_input->stopCapture();
                                                s_capturingVtt[p][0] = false;
                                                s_vttPrevKeys[vk] = true;
                                                break;
                                            }
                                            s_vttPrevKeys[vk] = pr;
                                        }
                                    }
                                } else {
                                    if (ImGui::Button("Bind##vttp")) {
                                        // Stop any other VTT capture in progress
                                        s_capturingVtt[p][1] = false;
                                        s_capturingVtt[p][0] = true;
                                        g_input->startCapture();
                                        for (int vk = 0; vk < 256; vk++)
                                            s_vttPrevKeys[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
                                    }
                                    if (ab.vtt_plus.isSet()) {
                                        ImGui::SameLine();
                                        if (ImGui::Button("Clear##vttp")) {
                                            ab.vtt_plus.clear();
                                            g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K, lights, LIGHT_COUNT_M);
                                        }
                                    }
                                }
                            }


                            // VTT Step
                            if (ImGui::SliderInt("Virtual TT Step Amount", &ab.vtt_step, 1, 10))
                            g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K, lights, LIGHT_COUNT_M);

                            ImGui::Separator();

                            // Live preview
                            {
                                float display = 0.5;
                                char overlay[32];
                                if (ab.isSet() || ab.hasVtt()){
                                    display = ab.getPosition(*g_input, g_vtt_pos[p]) / 255.0f;
                                    snprintf(overlay, sizeof(overlay), "%.0f", display * 255.0f);
                                }
                                else
                                    snprintf(overlay, sizeof(overlay), "(unbound)");

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
                                        g_input->stopCapture();
                                        s_capturingVtt[pp][dir] = false;
                                    }
                                }
                            }
                            ImGui::CloseCurrentPopup();
                            s_editPort = -1;
                        }
                        ImGui::EndPopup();
                    }
            ImGui::EndTabItem();
            }
        }
    

        // ---- Lights Tab ----
        if (!g_isDancer && ImGui::BeginTabItem("Lights")) {
            static int   s_bindLightIdx   = -1;       // which lights[] row is being bound
            static bool  s_openLightPopup = false;
            static int   s_lightDevIdx    = 0;        // combo index (0 = "(none)")
            static int   s_lightOutIdx    = -1;       // flat output index
            static float s_testTimer      = 0.0f;
            static std::string s_testPath;
            static int   s_testOutIdx     = -1;

            // Advance test timer — auto-turn-off after 1 second
            if (s_testTimer > 0.0f) {
                s_testTimer -= ImGui::GetIO().DeltaTime;
                if (s_testTimer <= 0.0f && !s_testPath.empty()) {
                    g_input->setLight(s_testPath, s_testOutIdx, 0.0f);
                }
            }

            // Get output-capable devices for bind popup.
            // Show all devices with at least one output cap (matches spice2x filter).
            std::vector<Device> allDevs = g_input->getDevices();
            std::vector<Device> outputDevs;
            for (auto& d : allDevs) {
                if (!d.button_output_caps_names.empty() || !d.value_output_caps_names.empty())
                    outputDevs.push_back(d);
            }

            // Table: 23 light channels
            ImGui::BeginChild("##lightsScroll", ImVec2(0, ImGui::GetWindowHeight() - 85), false);
            if (ImGui::BeginTable("##lighttable", 3,
                    ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Light",   ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Actions",    ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableHeadersRow();

                for (int i = 0; i < LIGHT_COUNT_M; i++) {
                    ImGui::PushID(i);
                    ImGui::TableNextRow();

                    LightBinding& lb = g_bindings.lights[i];

                    std::string lightLabel = lb.getDisplayString(*g_input);
                    bool isTestActive = (s_testTimer > 0.0f && lb.isSet()
                                        && s_testPath == lb.device_path
                                        && s_testOutIdx == lb.output_idx);

                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(lights[i]);

                    ImGui::TableSetColumnIndex(1);
                    if (isTestActive)
                        ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "%s", lightLabel.c_str());
                    else
                        ImGui::TextUnformatted(lightLabel.c_str());

                    ImGui::TableSetColumnIndex(2);
                    if (ImGui::Button("Bind")) {
                        s_bindLightIdx   = i;
                        s_lightDevIdx    = 0;
                        s_lightOutIdx    = -1;
                        s_openLightPopup = true;
                    }

                    if(lb.isSet()){
                        ImGui::SameLine();
                        if (ImGui::Button("Clear")) {
                            lb.clear();
                            g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K,
                                            lights, LIGHT_COUNT_M);
                        }
                    }

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::EndChild();

            // Bind popup
            if (s_openLightPopup) { ImGui::OpenPopup("BindLight"); s_openLightPopup = false; }
            if (ImGui::BeginPopupModal("BindLight", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Bind Light: %s", (s_bindLightIdx >= 0 ? lights[s_bindLightIdx] : "?"));
                ImGui::Separator();

                // Device combo — output-capable devices only
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

                // Instant-save on any combo change (matching Buttons tab pattern)
                if ((prevDevIdx != s_lightDevIdx || prevOutIdx != s_lightOutIdx)
                    && s_lightDevIdx > 0 && s_lightOutIdx >= 0 && s_bindLightIdx >= 0) {
                    LightBinding& lb = g_bindings.lights[s_bindLightIdx];
                    lb.device_path = outputDevs[s_lightDevIdx - 1].path;
                    lb.device_name = outputDevs[s_lightDevIdx - 1].name;
                    lb.output_idx  = s_lightOutIdx;
                    g_bindings.save(g_settings, ioButtons, IO_COUNT, ez2DancerIOButtons, DANCER_COUNT_K,
                                    lights, LIGHT_COUNT_M);
                }

                ImGui::Separator();

                // Test button — pulses light for 1 second then auto-off
                bool canTest = s_lightDevIdx > 0 && s_lightOutIdx >= 0;
                if (!canTest) ImGui::BeginDisabled();
                if (ImGui::Button("Test")) {
                    s_testPath   = outputDevs[s_lightDevIdx - 1].path;
                    s_testOutIdx = s_lightOutIdx;
                    g_input->setLight(s_testPath, s_testOutIdx, 1.0f);
                    s_testTimer  = 1.0f;
                }
                if (!canTest) ImGui::EndDisabled();

                ImGui::SameLine();
                if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();

                ImGui::EndPopup();
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::SetCursorPos({ ImGui::GetWindowWidth() - 250, ImGui::GetWindowHeight() - 20 });
    ImGui::TextDisabled("Made by kasaski (kissass) - 2026");

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
