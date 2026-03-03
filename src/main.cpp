#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include <GLFW/glfw3.h>

#include "data_loader.h"
#include "flame_data.h"
#include "timeline_view.h"
#include "flame_view.h"

#include <cstdio>
#include <cstdlib>
#include <string>

static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

int main(int argc, char* argv[]) {
    // 数据文件路径，默认 data/sample.json
    std::string dataFile = "data/sample.json";
    if (argc > 1) {
        dataFile = argv[1];
    }

    // 加载数据
    FlameNode root;
    try {
        root = loadFlameData(dataFile);
        printf("Loaded flame data from: %s\n", dataFile.c_str());
    } catch (const std::exception& e) {
        fprintf(stderr, "Error loading data: %s\n", e.what());
        return 1;
    }

    // 初始化 GLFW
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    // OpenGL 3.3 Core
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1280, 800, "imflame - Flame Graph Viewer", nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // VSync

    // 初始化 Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // 初始化视图
    TimelineView timelineView;
    timelineView.init(root);

    FlameView flameView;

    // 主循环
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // 全屏窗口
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("##Main", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImVec2 contentPos = ImGui::GetCursorScreenPos();
        ImVec2 contentSize = ImGui::GetContentRegionAvail();

        // §4 — 上下区域高度比 3:7，上方最小 150px
        float totalHeight = contentSize.y;
        float timelineHeight = totalHeight * 0.3f;
        if (timelineHeight < 150.0f) timelineHeight = 150.0f;
        if (timelineHeight > totalHeight) timelineHeight = totalHeight;
        float flameHeight = totalHeight - timelineHeight;

        float canvasWidth = contentSize.x;

        // §5 — 上方：时间序列曲线
        timelineView.draw(canvasWidth, timelineHeight);

        // §4/6 — 下方：火焰图
        // 火焰图区域紧贴上方曲线图的下边缘
        ImVec2 flameCursorPos = ImGui::GetCursorScreenPos();
        double t = timelineView.getCursorTime();
        flameView.draw(root, t, flameCursorPos, canvasWidth);

        // 为火焰图预留空间（使滚动条正确工作）
        ImGui::Dummy(ImVec2(canvasWidth, flameHeight));

        ImGui::End();

        // 渲染
        ImGui::Render();
        int displayW, displayH;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // 清理
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
