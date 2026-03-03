#include "timeline_view.h"
#include "imgui.h"
#include "implot.h"
#include <cstdio>

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

// §5.1/5.2 — 绘制时间序列曲线 + 游标
void TimelineView::draw(float availableWidth, float availableHeight) {
    if (times_.empty()) return;

    ImPlot::SetNextAxesToFit();
    if (ImPlot::BeginPlot("##Timeline", ImVec2(availableWidth, availableHeight),
                          ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect)) {

        ImPlot::SetupAxes("Time (s)", "Inclusive Cost", ImPlotAxisFlags_None, ImPlotAxisFlags_AutoFit);

        // §5.1 — 阶梯线绘制
        ImPlot::PlotStairs("root inclusive", times_.data(), values_.data(), (int)times_.size());

        // §5.2 — 可拖动游标
        if (ImPlot::DragLineX(0, &cursorTime_, ImVec4(1, 1, 0, 1), 1.0f,
                              ImPlotDragToolFlags_None)) {
            // 游标被拖动
        }

        // §5.2 — 限制游标范围
        if (cursorTime_ < minTime_) cursorTime_ = minTime_;
        if (cursorTime_ > maxTime_) cursorTime_ = maxTime_;

        // §5.2 — 点击定位游标
        if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(0)) {
            ImPlotPoint mp = ImPlot::GetPlotMousePos();
            cursorTime_ = mp.x;
            if (cursorTime_ < minTime_) cursorTime_ = minTime_;
            if (cursorTime_ > maxTime_) cursorTime_ = maxTime_;
        }

        // §5.2 — 游标标签：在游标线上方显示时间值
        {
            ImPlotPoint annotationPos;
            annotationPos.x = cursorTime_;
            // 在 Y 轴顶部附近标注
            ImPlotRect plotLimits = ImPlot::GetPlotLimits();
            annotationPos.y = plotLimits.Y.Max;

            char label[64];
            snprintf(label, sizeof(label), "t = %.3fs", cursorTime_);
            ImPlot::Annotation(annotationPos.x, annotationPos.y, ImVec4(1, 1, 0, 1),
                               ImVec2(10, 10), true, "%s", label);
        }

        ImPlot::EndPlot();
    }
}
