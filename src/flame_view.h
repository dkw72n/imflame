#pragma once

#include "flame_data.h"
#include "imgui.h"

// §6 — 火焰图视图
class FlameView {
public:
    // 绘制火焰图
    // root: 火焰图根节点
    // t: 当前游标时间
    // canvasPos: 火焰图区域左上角屏幕坐标
    // canvasWidth: 画布宽度
    void draw(const FlameNode& root, double t, ImVec2 canvasPos, float canvasWidth);

private:
    static constexpr float BLOCK_HEIGHT = 24.0f; // §6.1 — 色块高度固定 24px

    // §6.1 — 名称哈希 → 颜色映射
    static ImU32 nameToColor(const std::string& name, float brightnessBoost = 0.0f);

    // 递归绘制节点
    void drawNode(ImDrawList* drawList, const FlameNode& node, double t,
                  double rootInclusive, double parentInclusive,
                  float x, float y, float totalWidth, int depth);
};
