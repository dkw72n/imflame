#pragma once

#include "flame_data.h"
#include <vector>

// §5 — 时间序列曲线视图
class TimelineView {
public:
    // 初始化：预计算曲线数据
    void init(const FlameNode& root);

    // 绘制时间序列曲线 + 游标，返回当前游标时间
    // availableHeight: 分配给此视图的高度
    void draw(float availableWidth, float availableHeight);

    // 获取当前游标时间
    double getCursorTime() const { return cursorTime_; }

private:
    std::vector<double> times_;   // 全局时间点
    std::vector<double> values_;  // 对应 inclusive(root, t)
    double cursorTime_ = 0.0;
    double minTime_ = 0.0;
    double maxTime_ = 0.0;
};
