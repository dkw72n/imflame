#include "flame_view.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

// §6.1 — 名称哈希 → HSV 颜色映射
// H = hash(name) % 360, S = 0.6, V = 0.9
// brightnessBoost: §6.2 self 段亮度 +20% (传入 0.2)
ImU32 FlameView::nameToColor(const std::string& name, float brightnessBoost) {
    // 简单哈希
    std::hash<std::string> hasher;
    size_t h = hasher(name);

    float hue = (float)(h % 360) / 360.0f;
    float sat = 0.6f;
    float val = std::min(0.9f + brightnessBoost, 1.0f);

    ImVec4 color;
    ImGui::ColorConvertHSVtoRGB(hue, sat, val, color.x, color.y, color.z);
    color.w = 1.0f;
    return ImGui::ColorConvertFloat4ToU32(color);
}

// §6 — 绘制火焰图入口
void FlameView::draw(const FlameNode& root, double t, ImVec2 canvasPos, float canvasWidth) {
    double rootIncl = inclusive(root, t);

    // §9 用例 6 — 零值处理
    if (rootIncl <= 0.0) {
        return; // 显示空白，不崩溃
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawNode(drawList, root, t, rootIncl, rootIncl, canvasPos.x, canvasPos.y, canvasWidth, 0);
}

// 递归绘制节点
void FlameView::drawNode(ImDrawList* drawList, const FlameNode& node, double t,
                          double rootInclusive, double parentInclusive,
                          float x, float y, float totalWidth, int depth) {
    double nodeInclusive = inclusive(node, t);
    if (nodeInclusive <= 0.0) return;

    // §6.1 — 色块宽度 = 节点 inclusive / root inclusive × 画布总宽度
    float blockWidth = (float)(nodeInclusive / rootInclusive) * totalWidth;

    // §6.1 — 最小宽度裁剪
    if (blockWidth < 1.0f) return;

    float blockY = y + depth * BLOCK_HEIGHT;

    // §6.1 — 绘制色块
    ImU32 color = nameToColor(node.name);
    ImVec2 p0(x, blockY);
    ImVec2 p1(x + blockWidth, blockY + BLOCK_HEIGHT);
    drawList->AddRectFilled(p0, p1, color);

    // §6.2 — self cost 可视化
    double selfCost = query(node, t);
    if (selfCost > 0.0 && nodeInclusive > 0.0) {
        float selfWidth = (float)(selfCost / nodeInclusive) * blockWidth;
        if (selfWidth >= 1.0f) {
            ImU32 selfColor = nameToColor(node.name, 0.2f);
            ImVec2 selfP0(x + blockWidth - selfWidth, blockY);
            ImVec2 selfP1(x + blockWidth, blockY + BLOCK_HEIGHT);
            drawList->AddRectFilled(selfP0, selfP1, selfColor);
        }
    }

    // 绘制文字标签（如果色块足够宽）
    {
        const char* label = node.name.c_str();
        ImVec2 textSize = ImGui::CalcTextSize(label);
        if (textSize.x + 4.0f < blockWidth) {
            float textX = x + 2.0f;
            float textY = blockY + (BLOCK_HEIGHT - textSize.y) * 0.5f;
            drawList->AddText(ImVec2(textX, textY), IM_COL32(255, 255, 255, 255), label);
        }
    }

    // §6.3 — 悬停交互
    ImVec2 mousePos = ImGui::GetMousePos();
    if (mousePos.x >= p0.x && mousePos.x < p1.x &&
        mousePos.y >= p0.y && mousePos.y < p1.y) {
        // 高亮：白色 2px 边框
        drawList->AddRect(p0, p1, IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);

        // Tooltip
        ImGui::BeginTooltip();
        ImGui::Text("Name:       %s", node.name.c_str());
        ImGui::Text("Self cost:  %.2f", selfCost);
        ImGui::Text("Inclusive:   %.2f", nodeInclusive);
        ImGui::Text("%% of root:  %.1f%%", (nodeInclusive / rootInclusive) * 100.0);
        if (parentInclusive > 0.0) {
            ImGui::Text("%% of parent: %.1f%%", (nodeInclusive / parentInclusive) * 100.0);
        }
        ImGui::EndTooltip();
    }

    // §6.1 — 子节点按名称字母序排列，从父节点左边界开始向右排列
    std::vector<const FlameNode*> sortedChildren;
    for (const auto& child : node.children) {
        sortedChildren.push_back(&child);
    }
    std::sort(sortedChildren.begin(), sortedChildren.end(),
        [](const FlameNode* a, const FlameNode* b) {
            return a->name < b->name;
        });

    float childX = x;
    for (const FlameNode* child : sortedChildren) {
        double childIncl = inclusive(*child, t);
        if (childIncl <= 0.0) continue;

        float childWidth = (float)(childIncl / rootInclusive) * totalWidth;
        if (childWidth < 1.0f) continue;

        drawNode(drawList, *child, t, rootInclusive, nodeInclusive,
                 childX, y, totalWidth, depth + 1);
        childX += childWidth;
    }
}
