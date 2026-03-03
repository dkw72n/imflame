#pragma once

#include "flame_data.h"
#include "imgui.h"
#include <vector>

// §6 — 火焰图视图
class FlameView {
public:
    // 绘制火焰图
    // root: 火焰图根节点
    // t: 当前游标时间
    // canvasPos: 火焰图区域左上角屏幕坐标
    // canvasWidth: 画布宽度
    void draw(const FlameNode& root, double t, ImVec2 canvasPos, float canvasWidth);

    // 重置缩放（回到全局视图）
    void resetZoom() { zoomedNode_ = nullptr; zoomPath_.clear(); }

private:
    static constexpr float BLOCK_HEIGHT = 24.0f; // §6.1 — 色块高度固定 24px

    // §6.1 — 名称哈希 → 颜色映射
    static ImU32 nameToColor(const std::string& name, float brightnessBoost = 0.0f);

    // 递归绘制节点
    void drawNode(ImDrawList* drawList, const FlameNode& node, double t,
                  double zoomInclusive, double parentInclusive, double rootInclusive,
                  float x, float y, float totalWidth, int depth);

    // 在树中查找目标节点，并记录从根到目标的路径
    bool findNodePath(const FlameNode& node, const FlameNode* target,
                      std::vector<const FlameNode*>& path);

    // 当前聚焦（放大）的节点指针，nullptr 表示无缩放
    const FlameNode* zoomedNode_ = nullptr;

    // 从 root 到 zoomedNode_ 的路径（含两端）
    std::vector<const FlameNode*> zoomPath_;

    // 记录本帧被双击的节点（用于在递归结束后统一处理）
    const FlameNode* doubleClickedNode_ = nullptr;
};