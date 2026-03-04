#include "timeline_view.h"
#include "imgui.h"
#include "implot.h"
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <queue>

TimelineView::CurveData TimelineView::buildInclusiveCurveAsync(const FlameNode& node, std::atomic<bool>& cancelFlag) {
    std::vector<const FlameNode*> nodes;
    auto collect = [&](auto& self, const FlameNode& n) -> void {
        if (cancelFlag.load(std::memory_order_relaxed)) return;
        nodes.push_back(&n);
        for (uint32_t i = 0; i < n.child_count; ++i) {
            self(self, n.children[i]);
        }
    };
    collect(collect, node);
    if (cancelFlag.load(std::memory_order_relaxed)) return {};

    struct IteratorState {
        const Sample* current;
        const Sample* end;
        int node_idx;

        bool operator>(const IteratorState& other) const {
            return current->time > other.current->time;
        }
    };

    std::priority_queue<IteratorState, std::vector<IteratorState>, std::greater<IteratorState>> pq;

    size_t total_samples = 0;
    for (int i = 0; i < nodes.size(); ++i) {
        total_samples += nodes[i]->sample_count;
        if (nodes[i]->sample_count > 0) {
            pq.push({nodes[i]->samples.get(), nodes[i]->samples.get() + nodes[i]->sample_count, i});
        }
    }

    CurveData curve;
    curve.times.reserve(total_samples);
    curve.values.reserve(total_samples);

    std::vector<double> current_vals(nodes.size(), 0.0);
    double current_sum = 0.0;

    int loop_counter = 0;
    while (!pq.empty()) {
        // 每处理 1000 个事件检查一次中断标志
        if (++loop_counter > 1000) {
            if (cancelFlag.load(std::memory_order_relaxed)) return {};
            loop_counter = 0;
        }

        double t = pq.top().current->time;
        
        // Process all events at time t
        while (!pq.empty() && pq.top().current->time == t) {
            auto state = pq.top();
            pq.pop();

            int idx = state.node_idx;
            double val = state.current->value;
            current_sum += (val - current_vals[idx]);
            current_vals[idx] = val;

            state.current++;
            if (state.current != state.end) {
                pq.push(state);
            }
        }

        curve.times.push_back(t);
        curve.values.push_back(current_sum);
    }

    if (cancelFlag.load(std::memory_order_relaxed)) return {};

    // 如果节点曲线提前结束，追加一个点到时间轴最后，保持前值，使阶梯线延伸到最后
    if (maxTime_ > minTime_ && !curve.times.empty() && curve.times.back() < maxTime_) {
        curve.times.push_back(maxTime_);
        curve.values.push_back(curve.values.back());
    }

    // §7.3 — 曲线数据降采样 (LOD)
    constexpr size_t MAX_POINTS = 4000;
    if (curve.times.size() > MAX_POINTS) {
        CurveData downsampled;
        downsampled.times.reserve(MAX_POINTS);
        downsampled.values.reserve(MAX_POINTS);

        size_t n = curve.times.size();
        double min_t = curve.times.front();
        double max_t = curve.times.back();
        
        size_t num_buckets = MAX_POINTS / 2;
        double bucket_duration = (max_t - min_t) / num_buckets;
        if (bucket_duration <= 0) bucket_duration = 1.0;

        double current_val = 0.0;
        size_t idx = 0;
        
        for (size_t b = 0; b < num_buckets; ++b) {
            double b_start = min_t + b * bucket_duration;
            double b_end = b_start + bucket_duration;

            double b_min = current_val;
            double b_max = current_val;

            while (idx < n && curve.times[idx] < b_end) {
                current_val = curve.values[idx];
                b_min = std::min(b_min, current_val);
                b_max = std::max(b_max, current_val);
                idx++;
            }

            downsampled.times.push_back(b_start);
            downsampled.values.push_back(b_min);
            if (b_max > b_min) {
                downsampled.times.push_back(b_start + bucket_duration * 0.5);
                downsampled.values.push_back(b_max);
            }
        }
        
        // 确保最后一个点被添加
        if (idx < n) {
            downsampled.times.push_back(curve.times.back());
            downsampled.values.push_back(curve.values.back());
        } else if (!downsampled.times.empty() && downsampled.times.back() < max_t) {
            downsampled.times.push_back(max_t);
            downsampled.values.push_back(current_val);
        }

        curve = std::move(downsampled);
    }

    curve.times.shrink_to_fit();
    curve.values.shrink_to_fit();

    return curve;
}

TimelineView::CurveData TimelineView::buildInclusiveCurve(const FlameNode& node) {
    std::atomic<bool> dummyFlag{false};
    return buildInclusiveCurveAsync(node, dummyFlag);
}

TimelineView::CurveData TimelineView::buildLowResCurve(const FlameNode& node) const {
    CurveData curve;
    constexpr int LOW_RES_POINTS = 100;
    curve.times.reserve(LOW_RES_POINTS);
    curve.values.reserve(LOW_RES_POINTS);

    if (maxTime_ > minTime_) {
        double step = (maxTime_ - minTime_) / (LOW_RES_POINTS - 1);
        for (int i = 0; i < LOW_RES_POINTS; ++i) {
            double t = minTime_ + i * step;
            curve.times.push_back(t);
            curve.values.push_back(inclusive(node, t));
        }
    }
    return curve;
}

void TimelineView::checkAsyncResult() {
    if (currentAsyncState_ && currentAsyncState_->isReady.load(std::memory_order_acquire)) {
        // 异步任务完成，且未被取消
        if (!currentAsyncState_->cancelFlag.load(std::memory_order_relaxed)) {
            curveCache_[currentAsyncState_->targetNode] = std::move(currentAsyncState_->result);
        }
        currentAsyncState_.reset();
    }
}

void TimelineView::startAsyncBuild(const FlameNode* node) {
    // 如果当前正在计算同一个节点，无需重复启动
    if (currentAsyncState_ && currentAsyncState_->targetNode == node) {
        return;
    }

    // 如果有正在计算的其他节点，取消它
    if (currentAsyncState_) {
        currentAsyncState_->cancelFlag.store(true, std::memory_order_relaxed);
    }

    // 创建新的异步状态
    currentAsyncState_ = std::make_shared<AsyncState>();
    currentAsyncState_->targetNode = node;

    // 启动后台线程
    std::shared_ptr<AsyncState> state = currentAsyncState_;
    std::thread([state, node, this]() {
        CurveData result = this->buildInclusiveCurveAsync(*node, state->cancelFlag);
        if (!state->cancelFlag.load(std::memory_order_relaxed)) {
            state->result = std::move(result);
            state->isReady.store(true, std::memory_order_release);
        }
    }).detach();
}

TimelineView::CurveData TimelineView::getCurveProgressive(const FlameNode* node) {
    auto it = curveCache_.find(node);
    if (it != curveCache_.end()) {
        // 命中高精度缓存，直接返回
        return it->second;
    }
    
    // 未命中缓存，触发异步高精度计算
    startAsyncBuild(node);
    
    // 实时计算低精度曲线（耗时 < 1ms）作为临时反馈
    return buildLowResCurve(*node);
}

// §5.1 — 预计算曲线数据
void TimelineView::init(const FlameNode& root) {
    rootCurve_ = buildInclusiveCurve(root);

    if (!rootCurve_.times.empty()) {
        minTime_ = rootCurve_.times.front();
        maxTime_ = rootCurve_.times.back();
        cursorTime_ = minTime_;  // §5.2 初始位置 = 最小时间

        // 预计算 Y 轴最大值，用于固定 Y 轴范围，避免曲线切换时轴范围跳变
        maxValue_ = 0.0;
        for (size_t i = 0; i < rootCurve_.values.size(); ++i) {
            if (rootCurve_.values[i] > maxValue_) maxValue_ = rootCurve_.values[i];
        }
    }
}

// §5.1/5.2 — 绘制时间序列曲线 + 游标 + 拖拽选区
void TimelineView::draw(float availableWidth, float availableHeight, const FlameNode* hoveredNode, const FlameNode* focusNode) {
    if (rootCurve_.times.empty()) return;

    // 检查是否有异步计算完成的高精度曲线
    checkAsyncResult();

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

        ImPlot::SetupAxes("Time (s)", "Inclusive Cost", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
        // Y 轴固定范围 [0, maxValue_]，避免曲线切换时轴范围跳变晃眼
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, maxValue_ * 1.05, ImPlotCond_Always);
        // 限制 X 轴：不允许平移超出数据边界，不允许缩小到超出数据全范围
        ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, minTime_, maxTime_);
        ImPlot::SetupAxisZoomConstraints(ImAxis_X1, 0, maxTime_ - minTime_);

        // §5.1 — 阶梯线绘制
        // 如果有焦点节点（火焰图缩放状态），主曲线切换为焦点节点的 inclusive 曲线，替换掉 root
        if (focusNode != nullptr) {
            CurveData focusCurve = getCurveProgressive(focusNode);
            // 焦点节点曲线替换 root 作为主曲线，显式指定蓝色与 hover 橙色区分
            ImPlotSpec focusSpec;
            focusSpec.LineColor = ImVec4(0.4f, 0.7f, 1.0f, 1.0f);
            focusSpec.LineWeight = 1.5f;
            ImPlot::PlotStairs(focusNode->name->c_str(), focusCurve.times.data(), focusCurve.values.data(), (int)focusCurve.times.size(), focusSpec);
        } else {
            ImPlot::PlotStairs("root inclusive", rootCurve_.times.data(), rootCurve_.values.data(), (int)rootCurve_.times.size());
        }

        // 悬停节点的叠加曲线（颜色较淡，半透明，以示区分）
        if (hoveredNode != nullptr && hoveredNode != focusNode) {
            CurveData hoverCurve = getCurveProgressive(hoveredNode);

            // 用较淡的橙色绘制悬停节点曲线，与主曲线区分
            ImPlotSpec hoverSpec;
            hoverSpec.LineColor = ImVec4(1.0f, 0.6f, 0.2f, 0.7f);
            hoverSpec.LineWeight = 1.5f;
            ImPlot::PlotStairs(hoveredNode->name->c_str(), hoverCurve.times.data(), hoverCurve.values.data(), (int)hoverCurve.times.size(), hoverSpec);
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