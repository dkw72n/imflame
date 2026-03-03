#pragma once

#include "flame_data.h"
#include "imgui.h"
#include <vector>

// §6 — 火焰图视图
class FlameView {
public:
    // 绘制火焰图（普通模式）
    // root: 火焰图根节点
    // t: 当前游标时间
    // canvasPos: 火焰图区域左上角屏幕坐标
    // canvasWidth: 画布宽度
    void draw(const FlameNode& root, double t, ImVec2 canvasPos, float canvasWidth);

    // 绘制火焰图（Diff 模式）
    // 节点尺寸按 t1 绘制，颜色反映 node(t1) - node(t0) 的差值
    void drawDiff(const FlameNode& root, double t0, double t1, ImVec2 canvasPos, float canvasWidth);

    // 重置缩放（回到全局视图）
    void resetZoom() { zoomedNode_ = nullptr; zoomPath_.clear(); }

private:
    static constexpr float BLOCK_HEIGHT = 24.0f; // §6.1 — 色块高度固定 24px

    // §6.1 — 名称哈希 → 颜色映射
    static ImU32 nameToColor(const std::string& name, float brightnessBoost = 0.0f);

    // Diff 颜色映射: delta > 0 红色, delta < 0 蓝色, delta == 0 灰色
    // normalizedDelta: delta / maxAbsDelta，范围 [-1, 1]
    static ImU32 diffColor(double normalizedDelta);

    // 递归绘制节点（普通模式）
    void drawNode(ImDrawList* drawList, const FlameNode& node, double t,
                  double zoomInclusive, double parentInclusive, double rootInclusive,
                  float x, float y, float totalWidth, int depth);

    // 递归绘制节点（Diff 模式）
    void drawNodeDiff(ImDrawList* drawList, const FlameNode& node, double t0, double t1,
                      double zoomInclusive, double parentInclusive, double rootInclusive,
                      double maxAbsDelta,
                      float x, float y, float totalWidth, int depth);

    // 在树中查找目标节点，并记录从根到目标的路径
    bool findNodePath(const FlameNode& node, const FlameNode* target,
                      std::vector<const FlameNode*>& path);

    // 递归查找整棵树中 |inclusive(t1) - inclusive(t0)| 的最大值，用于归一化颜色
    double findMaxAbsDelta(const FlameNode& node, double t0, double t1);

    // 当前聚焦（放大）的节点指针，nullptr 表示无缩放
    const FlameNode* zoomedNode_ = nullptr;

    // 从 root 到 zoomedNode_ 的路径（含两端）
    std::vector<const FlameNode*> zoomPath_;

    // 记录本帧被双击的节点（用于在递归结束后统一处理）
    const FlameNode* doubleClickedNode_ = nullptr;

    // 当前帧鼠标悬停的火焰图节点（用于时间轴叠加曲线）
    const FlameNode* hoveredNode_ = nullptr;

public:
    // 获取当前悬停的节点（可能为 nullptr）
    const FlameNode* getHoveredNode() const { return hoveredNode_; }

    // 获取当前缩放聚焦的节点（可能为 nullptr，表示无缩放）
    const FlameNode* getZoomedNode() const { return zoomedNode_; }
};