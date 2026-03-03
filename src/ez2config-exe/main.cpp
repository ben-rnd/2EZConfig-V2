#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
#include "injector.h"
#include <GLFW/glfw3.h>
#include <cstdio>

static void renderUI();
static void setTheme();

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

    ImGui::Text("R U READY 2 GO INSIDA DJ BOX?");
    ImGui::SameLine(ImGui::GetWindowWidth() - 80);
    if (ImGui::Button("Play EZ2!")) { Injector::LaunchAndInject("ez2dj.exe"); }

    ImGui::Separator();

    if (ImGui::BeginTabBar("##tabs")) {
        if (ImGui::BeginTabItem("Settings")) { ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Buttons"))  { ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Analogs"))  { ImGui::EndTabItem(); }
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
