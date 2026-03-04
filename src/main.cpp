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

    // 设置 ImGui 窗口背景色为 (24, 24, 24)，影响 ImPlot 背景
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg] = ImVec4(24.0f / 255.0f, 24.0f / 255.0f, 24.0f / 255.0f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(24.0f / 255.0f, 24.0f / 255.0f, 24.0f / 255.0f, 1.0f);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // 初始化视图
    TimelineView timelineView;
    timelineView.init(root);

    FlameView flameView;

    // 分隔条位置（时序图占用的比例，0.0 ~ 1.0）
    float timelineRatio_ = 0.3f;
    // 分隔条拖拽状态
    bool draggingDivider_ = false;
    // 分隔条高度
    constexpr float DIVIDER_HEIGHT = 6.0f;
    // 分隔条悬停检测区域扩展（上下各扩展一点方便选中）
    constexpr float DIVIDER_HIT_EXTEND = 4.0f;
    // 时序图最大高度
    constexpr float TIMELINE_MAX_HEIGHT = 350.0f;

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

        // §4 — 根据可调整的比例计算高度，上方最小 150px，最大 350px
        float totalHeight = contentSize.y;
        float timelineHeight = totalHeight * timelineRatio_;
        if (timelineHeight < 150.0f) timelineHeight = 150.0f;
        if (timelineHeight > TIMELINE_MAX_HEIGHT) timelineHeight = TIMELINE_MAX_HEIGHT;
        if (timelineHeight > totalHeight - 100.0f) timelineHeight = totalHeight - 100.0f;
        float flameHeight = totalHeight - timelineHeight - DIVIDER_HEIGHT;

        float canvasWidth = contentSize.x;

        // 计算分隔条区域（位于时序图和火焰图之间）
        ImVec2 timelineCursorPos = ImGui::GetCursorScreenPos();
        float timelineBottom = timelineCursorPos.y + timelineHeight;
        ImVec2 dividerMin(timelineCursorPos.x, timelineBottom);
        ImVec2 dividerMax(timelineCursorPos.x + canvasWidth, timelineBottom + DIVIDER_HEIGHT);

        // 处理分隔条拖拽
        ImVec2 mousePos = ImGui::GetMousePos();

        // 检测鼠标是否在分隔条区域内
        bool mouseOverDivider = mousePos.x >= dividerMin.x && mousePos.x < dividerMax.x &&
                                mousePos.y >= dividerMin.y - DIVIDER_HIT_EXTEND && 
                                mousePos.y < dividerMax.y + DIVIDER_HIT_EXTEND;

        // 更新光标样式
        if (mouseOverDivider || draggingDivider_) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        }

        if (draggingDivider_) {
            // 拖拽中：根据鼠标位置更新比例（限制最大高度 350px）
            float newTimelineHeight = mousePos.y - contentPos.y;
            newTimelineHeight = std::max(150.0f, std::min({newTimelineHeight, TIMELINE_MAX_HEIGHT, totalHeight - 100.0f}));
            timelineRatio_ = newTimelineHeight / totalHeight;
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouseOverDivider) {
            // 左键点击分隔条：开始拖拽
            draggingDivider_ = true;
        }

        // 检测鼠标释放：结束拖拽
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            draggingDivider_ = false;
        }

        // 绘制分隔条（浅灰色）
        ImDrawList* dividerDrawList = ImGui::GetWindowDrawList();
        // 悬停或拖拽时稍微变亮
        ImU32 dividerColor = draggingDivider_ ? IM_COL32(120, 120, 120, 255) : 
                             (mouseOverDivider ? IM_COL32(100, 100, 100, 255) : 
                             IM_COL32(80, 80, 80, 255));
        dividerDrawList->AddRectFilled(dividerMin, dividerMax, dividerColor);

        // §5 — 上方：时间序列曲线
        // 传入上一帧悬停的火焰图节点叠加显示其曲线，以及当前缩放聚焦节点切换主曲线
        const FlameNode* focusNode = flameView.getZoomedNode();
        timelineView.draw(canvasWidth, timelineHeight, prevHoveredNode, focusNode);

        // §4/6 — 下方：火焰图
        // 火焰图区域紧贴分隔条的下边缘
        ImVec2 flameCursorPos = ImGui::GetCursorScreenPos();

        // 显式填充火焰图背景色 (24, 24, 24)
        ImDrawList* flameBgDrawList = ImGui::GetWindowDrawList();
        flameBgDrawList->AddRectFilled(flameCursorPos, ImVec2(flameCursorPos.x + canvasWidth, flameCursorPos.y + flameHeight), IM_COL32(24, 24, 24, 255));

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
        glClearColor(24.0f / 255.0f, 24.0f / 255.0f, 24.0f / 255.0f, 1.0f);
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
