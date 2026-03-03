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
void TimelineView::draw(float availableWidth, float availableHeight) {
    if (times_.empty()) return;

    // Escape 或右键 取消选区
    if (rangeSelected_ || dragging_) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            rangeSelected_ = false;
            dragging_ = false;
        }
    }

    ImPlot::SetNextAxesToFit();
    if (ImPlot::BeginPlot("##Timeline", ImVec2(availableWidth, availableHeight),
                          ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect)) {

        ImPlot::SetupAxes("Time (s)", "Inclusive Cost", ImPlotAxisFlags_None, ImPlotAxisFlags_AutoFit);

        // §5.1 — 阶梯线绘制
        ImPlot::PlotStairs("root inclusive", times_.data(), values_.data(), (int)times_.size());

        // === 拖拽选区交互 ===
        if (ImPlot::IsPlotHovered()) {
            ImPlotPoint mp = ImPlot::GetPlotMousePos();

            // 按住 Shift + 左键拖拽 开始选区
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::GetIO().KeyShift) {
                dragging_ = true;
                dragStartTime_ = mp.x;
                dragEndTime_ = mp.x;
                rangeSelected_ = false;
            }

            if (dragging_) {
                dragEndTime_ = mp.x;

                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                    dragging_ = false;
                    // 确保 t0 < t1，且有足够宽度
                    double t0 = std::min(dragStartTime_, dragEndTime_);
                    double t1 = std::max(dragStartTime_, dragEndTime_);
                    t0 = std::max(t0, minTime_);
                    t1 = std::min(t1, maxTime_);
                    if (std::abs(t1 - t0) > 1e-9) {
                        rangeSelected_ = true;
                        rangeT0_ = t0;
                        rangeT1_ = t1;
                    }
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

        // 非选区模式下：游标和点击
        if (!rangeSelected_ && !dragging_) {
            // §5.2 — 可拖动游标
            if (ImPlot::DragLineX(0, &cursorTime_, ImVec4(1, 1, 0, 1), 1.0f,
                                  ImPlotDragToolFlags_None)) {
                // 游标被拖动
            }

            // §5.2 — 限制游标范围
            if (cursorTime_ < minTime_) cursorTime_ = minTime_;
            if (cursorTime_ > maxTime_) cursorTime_ = maxTime_;

            // §5.2 — 点击定位游标（非 Shift 时）
            if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(0) && !ImGui::GetIO().KeyShift) {
                ImPlotPoint mp = ImPlot::GetPlotMousePos();
                cursorTime_ = mp.x;
                if (cursorTime_ < minTime_) cursorTime_ = minTime_;
                if (cursorTime_ > maxTime_) cursorTime_ = maxTime_;
            }

            // §5.2 — 游标标签
            {
                ImPlotPoint annotationPos;
                annotationPos.x = cursorTime_;
                ImPlotRect plotLimits = ImPlot::GetPlotLimits();
                annotationPos.y = plotLimits.Y.Max;

                char label[64];
                snprintf(label, sizeof(label), "t = %.3fs", cursorTime_);
                ImPlot::Annotation(annotationPos.x, annotationPos.y, ImVec4(1, 1, 0, 1),
                                   ImVec2(10, 10), true, "%s", label);
            }
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
            "Hold Shift + Drag to select time range for Diff mode");
    }
}