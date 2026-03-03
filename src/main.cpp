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

    // 将 ImPlot 默认的左键平移改为中键平移，避免与选区拖拽和游标点击冲突
    ImPlotInputMap& inputMap = ImPlot::GetInputMap();
    inputMap.Pan = ImGuiMouseButton_Middle;

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // 初始化视图
    TimelineView timelineView;
    timelineView.init(root);

    FlameView flameView;

    // 上一帧火焰图中悬停的节点（用于在时间轴叠加曲线，延迟一帧视觉上无感知）
    const FlameNode* prevHoveredNode = nullptr;

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
        // 传入上一帧悬停的火焰图节点叠加显示其曲线，以及当前缩放聚焦节点切换主曲线
        const FlameNode* focusNode = flameView.getZoomedNode();
        timelineView.draw(canvasWidth, timelineHeight, prevHoveredNode, focusNode);

        // §4/6 — 下方：火焰图
        // 火焰图区域紧贴上方曲线图的下边缘
        ImVec2 flameCursorPos = ImGui::GetCursorScreenPos();

        if (timelineView.isRangeSelected()) {
            // Diff 模式：按选区 (t0, t1) 绘制差异火焰图
            double t0 = timelineView.getRangeT0();
            double t1 = timelineView.getRangeT1();
            flameView.drawDiff(root, t0, t1, flameCursorPos, canvasWidth);
        } else {
            // 普通模式：按游标时间绘制
            double t = timelineView.getCursorTime();
            flameView.draw(root, t, flameCursorPos, canvasWidth);
        }

        // 记录本帧悬停节点，供下一帧时间轴使用
        prevHoveredNode = flameView.getHoveredNode();

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
