#include "timeline_view.h"
#include "imgui.h"
#include "implot.h"
#include <cstdio>
#include <algorithm>
#include <cmath>

// §5.1 — 预计算曲线数据
void TimelineView::init(const FlameNode& root) {
    times_ = collectAllTimes(root);
    values_.resize(times_.size());
    for (size_t i = 0; i < times_.size(); ++i) {
        values_[i] = inclusive(root, times_[i]);
    }

    if (!times_.empty()) {
        minTime_ = times_.front();
        maxTime_ = times_.back();
        cursorTime_ = minTime_;  // §5.2 初始位置 = 最小时间
    }
}

// §5.1/5.2 — 绘制时间序列曲线 + 游标 + 拖拽选区
void TimelineView::draw(float availableWidth, float availableHeight, const FlameNode* hoveredNode) {
    if (times_.empty()) return;

    // Escape 或右键 取消选区
    if (rangeSelected_ || dragging_) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            rangeSelected_ = false;
            dragging_ = false;
        }
    }

    // 仅首帧自适应轴范围，之后允许用户自由缩放/平移
    if (firstFrame_) {
        ImPlot::SetNextAxesToFit();
    }
    if (ImPlot::BeginPlot("##Timeline", ImVec2(availableWidth, availableHeight),
                          ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect)) {
        firstFrame_ = false;

        ImPlot::SetupAxes("Time (s)", "Inclusive Cost", ImPlotAxisFlags_None, ImPlotAxisFlags_AutoFit);
        // 限制 X 轴：不允许平移超出数据边界，不允许缩小到超出数据全范围
        ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, minTime_, maxTime_);
        ImPlot::SetupAxisZoomConstraints(ImAxis_X1, 0, maxTime_ - minTime_);

        // §5.1 — 阶梯线绘制
        ImPlot::PlotStairs("root inclusive", times_.data(), values_.data(), (int)times_.size());

        // 悬停节点的叠加曲线（颜色较淡，半透明，以示区分）
        if (hoveredNode != nullptr) {
            hoverValues_.resize(times_.size());
            for (size_t i = 0; i < times_.size(); ++i) {
                hoverValues_[i] = inclusive(*hoveredNode, times_[i]);
            }

            // 用较淡的橙色绘制悬停节点曲线，与主曲线区分
            ImPlotSpec hoverSpec;
            hoverSpec.LineColor = ImVec4(1.0f, 0.6f, 0.2f, 0.7f);
            hoverSpec.LineWeight = 1.5f;
            ImPlot::PlotStairs(hoveredNode->name.c_str(), times_.data(), hoverValues_.data(), (int)times_.size(), hoverSpec);
        }

        // === 拖拽选区交互（左键拖拽）===
        if (ImPlot::IsPlotHovered()) {
            ImPlotPoint mp = ImPlot::GetPlotMousePos();

            // 左键按下开始拖拽
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                dragging_ = true;
                dragStartTime_ = mp.x;
                dragEndTime_ = mp.x;
                rangeSelected_ = false;
            }
        }

        if (dragging_) {
            ImPlotPoint mp = ImPlot::GetPlotMousePos();
            dragEndTime_ = mp.x;

            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                dragging_ = false;
                double t0 = std::min(dragStartTime_, dragEndTime_);
                double t1 = std::max(dragStartTime_, dragEndTime_);
                t0 = std::max(t0, minTime_);
                t1 = std::min(t1, maxTime_);
                if (std::abs(t1 - t0) > 1e-9) {
                    // 拖拽距离足够大，视为选区
                    rangeSelected_ = true;
                    rangeT0_ = t0;
                    rangeT1_ = t1;
                } else {
                    // 拖拽距离很小，视为点击定位游标
                    cursorTime_ = mp.x;
                    if (cursorTime_ < minTime_) cursorTime_ = minTime_;
                    if (cursorTime_ > maxTime_) cursorTime_ = maxTime_;
                }
            }
        }

        // 绘制选区矩形（拖拽中 或 已选定时）
        if (dragging_ || rangeSelected_) {
            double st0 = dragging_ ? std::min(dragStartTime_, dragEndTime_) : rangeT0_;
            double st1 = dragging_ ? std::max(dragStartTime_, dragEndTime_) : rangeT1_;
            ImPlotRect plotLimits = ImPlot::GetPlotLimits();

            // 使用 ImPlot 绘制半透明矩形
            ImVec2 pMin = ImPlot::PlotToPixels(st0, plotLimits.Y.Max);
            ImVec2 pMax = ImPlot::PlotToPixels(st1, plotLimits.Y.Min);
            ImDrawList* drawList = ImPlot::GetPlotDrawList();
            drawList->AddRectFilled(pMin, pMax, IM_COL32(100, 180, 255, 60));
            drawList->AddRect(pMin, pMax, IM_COL32(100, 180, 255, 180), 0.0f, 0, 1.5f);

            // 选区两端标注时间
            char labelT0[64], labelT1[64];
            snprintf(labelT0, sizeof(labelT0), "t0=%.3fs", st0);
            snprintf(labelT1, sizeof(labelT1), "t1=%.3fs", st1);
            ImPlot::Annotation(st0, plotLimits.Y.Max, ImVec4(0.4f, 0.7f, 1.0f, 1.0f),
                               ImVec2(-5, -15), true, "%s", labelT0);
            ImPlot::Annotation(st1, plotLimits.Y.Max, ImVec4(0.4f, 0.7f, 1.0f, 1.0f),
                               ImVec2(5, -15), true, "%s", labelT1);
        }

        // 非选区模式下：显示游标线和标签
        if (!rangeSelected_ && !dragging_) {
            // §5.2 — 游标竖线（只读显示，不再使用 DragLineX 以免与拖拽选区冲突）
            ImPlotRect plotLimits = ImPlot::GetPlotLimits();
            ImVec2 lineTop = ImPlot::PlotToPixels(cursorTime_, plotLimits.Y.Max);
            ImVec2 lineBot = ImPlot::PlotToPixels(cursorTime_, plotLimits.Y.Min);
            ImDrawList* drawList = ImPlot::GetPlotDrawList();
            drawList->AddLine(lineTop, lineBot, IM_COL32(255, 255, 0, 255), 1.0f);

            // §5.2 — 游标标签
            char label[64];
            snprintf(label, sizeof(label), "t = %.3fs", cursorTime_);
            ImPlot::Annotation(cursorTime_, plotLimits.Y.Max, ImVec4(1, 1, 0, 1),
                               ImVec2(10, 10), true, "%s", label);
        }

        ImPlot::EndPlot();
    }

    // 选区模式提示
    if (rangeSelected_) {
        ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f),
            "Diff Mode: t0=%.3fs, t1=%.3fs (Press Esc or Right-click to cancel)",
            rangeT0_, rangeT1_);
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
            "Click to set cursor, Drag to select time range for Diff mode");
    }
}