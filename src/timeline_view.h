#pragma once

#include "flame_data.h"
#include <vector>
#include <unordered_map>
#include <atomic>
#include <memory>
#include <thread>
#include <mutex>

// §5 — 时间序列曲线视图
class TimelineView {
public:
    // 初始化：预计算曲线数据
    void init(const FlameNode& root);

    // 绘制时间序列曲线 + 游标，返回当前游标时间
    // availableHeight: 分配给此视图的高度
    // hoveredNode: 火焰图中当前悬停的节点（可为 nullptr），用于叠加显示其曲线
    // focusNode: 火焰图中当前缩放聚焦的节点（可为 nullptr），用于切换主曲线
    void draw(float availableWidth, float availableHeight,
              const FlameNode* hoveredNode = nullptr,
              const FlameNode* focusNode = nullptr);

    // 获取当前游标时间
    double getCursorTime() const { return cursorTime_; }

    // === Diff 模式：时间范围选区 ===
    // 是否存在有效的时间范围选区
    bool isRangeSelected() const { return rangeSelected_; }
    // 选区起始时间（较小值）
    double getRangeT0() const { return rangeT0_; }
    // 选区结束时间（较大值）
    double getRangeT1() const { return rangeT1_; }

private:
    struct CurveData {
        std::vector<double> times;
        std::vector<double> values;
    };

    CurveData buildInclusiveCurve(const FlameNode& node);

    CurveData rootCurve_;
    double cursorTime_ = 0.0;
    double minTime_ = 0.0;
    double maxTime_ = 0.0;
    double maxValue_ = 0.0;   // Y 轴固定上界（root inclusive 最大值）

    // 时间范围选区状态
    bool dragging_ = false;       // 是否正在拖拽选区
    double dragStartTime_ = 0.0;  // 拖拽起始时间（在 plot 坐标系中）
    double dragEndTime_ = 0.0;    // 拖拽当前/结束时间
    bool rangeSelected_ = false;  // 是否有有效选区
    double rangeT0_ = 0.0;       // 选区起始（较小值）
    double rangeT1_ = 0.0;       // 选区结束（较大值）
    bool firstFrame_ = true;     // 首帧标记，用于仅首次自适应轴范围

    // §8.1 — 高精度曲线永久缓存
    std::unordered_map<const FlameNode*, CurveData> curveCache_;
    
    // §8.2 — 渐进式渲染 (LOD)
    // 快速生成低精度曲线（100个点），用于首次 Hover 时的瞬间反馈
    CurveData buildLowResCurve(const FlameNode& node) const;
    
    // 获取曲线：如果缓存有高精度则返回高精度，否则返回实时计算的低精度曲线，并触发异步计算
    CurveData getCurveProgressive(const FlameNode* node);

    // §8.3 — 异步高精度计算状态
    struct AsyncState {
        std::atomic<bool> cancelFlag{false};
        std::atomic<bool> isReady{false};
        const FlameNode* targetNode = nullptr;
        CurveData result;
    };
    std::shared_ptr<AsyncState> currentAsyncState_;
    
    // 检查异步任务是否完成，若完成则存入缓存
    void checkAsyncResult();
    
    // 启动异步计算任务
    void startAsyncBuild(const FlameNode* node);
    
    // 带有中断检查的高精度计算
    CurveData buildInclusiveCurveAsync(const FlameNode& node, std::atomic<bool>& cancelFlag);
};
