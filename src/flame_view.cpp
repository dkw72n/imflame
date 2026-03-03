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

// 在树中查找目标节点，构建路径
bool FlameView::findNodePath(const FlameNode& node, const FlameNode* target,
                              std::vector<const FlameNode*>& path) {
    path.push_back(&node);
    if (&node == target) return true;
    for (const auto& child : node.children) {
        if (findNodePath(child, target, path)) return true;
    }
    path.pop_back();
    return false;
}

// §6 — 绘制火焰图入口
void FlameView::draw(const FlameNode& root, double t, ImVec2 canvasPos, float canvasWidth) {
    double rootIncl = inclusive(root, t);

    // §9 用例 6 — 零值处理
    if (rootIncl <= 0.0) {
        return; // 显示空白，不崩溃
    }

    // 重置本帧双击检测
    doubleClickedNode_ = nullptr;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    if (zoomedNode_ != nullptr) {
        // === 缩放模式 ===
        double zoomIncl = inclusive(*zoomedNode_, t);
        if (zoomIncl <= 0.0) {
            // 聚焦节点在当前时间无数据，回退到全局视图
            resetZoom();
        } else {
            // 先绘制祖先链（每个祖先占满整个宽度，作为上下文面包屑）
            for (size_t i = 0; i < zoomPath_.size() - 1; ++i) {
                const FlameNode* ancestor = zoomPath_[i];
                double ancestorIncl = inclusive(*ancestor, t);
                double ancestorSelf = query(*ancestor, t);
                float blockY = canvasPos.y + (int)i * BLOCK_HEIGHT;
                ImVec2 p0(canvasPos.x, blockY);
                ImVec2 p1(canvasPos.x + canvasWidth, blockY + BLOCK_HEIGHT);

                // 用较暗的颜色绘制祖先
                ImU32 color = nameToColor(ancestor->name);
                // 半透明处理表示这是上下文
                ImU32 dimColor = (color & 0x00FFFFFF) | 0xAA000000;
                drawList->AddRectFilled(p0, p1, dimColor);

                // 文字标签
                const char* label = ancestor->name.c_str();
                ImVec2 textSize = ImGui::CalcTextSize(label);
                if (textSize.x + 4.0f < canvasWidth) {
                    float textX = canvasPos.x + 2.0f;
                    float textY = blockY + (BLOCK_HEIGHT - textSize.y) * 0.5f;
                    drawList->AddText(ImVec2(textX, textY), IM_COL32(200, 200, 200, 255), label);
                }

                // 悬停祖先时显示 tooltip + 高亮
                ImVec2 mousePos = ImGui::GetMousePos();
                if (mousePos.x >= p0.x && mousePos.x < p1.x &&
                    mousePos.y >= p0.y && mousePos.y < p1.y) {
                    drawList->AddRect(p0, p1, IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);

                    ImGui::BeginTooltip();
                    ImGui::Text("Name:       %s", ancestor->name.c_str());
                    ImGui::Text("Self cost:  %.2f", ancestorSelf);
                    ImGui::Text("Inclusive:   %.2f", ancestorIncl);
                    ImGui::Text("%% of root:  %.1f%%", (ancestorIncl / rootIncl) * 100.0);
                    ImGui::EndTooltip();

                    // 双击祖先 → 缩放到该祖先
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        doubleClickedNode_ = ancestor;
                    }
                }
            }

            // 从聚焦节点开始绘制，depth 偏移 = 祖先数量
            int depthOffset = (int)(zoomPath_.size() - 1);
            drawNode(drawList, *zoomedNode_, t, zoomIncl, zoomIncl, rootIncl,
                     canvasPos.x, canvasPos.y, canvasWidth, depthOffset);
        }
    }

    if (zoomedNode_ == nullptr) {
        // === 全局视图 ===
        drawNode(drawList, root, t, rootIncl, rootIncl, rootIncl,
                 canvasPos.x, canvasPos.y, canvasWidth, 0);
    }

    // 处理双击事件（在递归完成后统一处理，避免递归中修改状态）
    if (doubleClickedNode_ != nullptr) {
        if (doubleClickedNode_ == &root) {
            // 双击根节点 → 回到全局视图
            resetZoom();
        } else if (doubleClickedNode_ == zoomedNode_) {
            // 双击已聚焦的节点 → 回到全局视图
            resetZoom();
        } else {
            // 双击其他节点 → 缩放到该节点
            zoomedNode_ = doubleClickedNode_;
            zoomPath_.clear();
            findNodePath(root, zoomedNode_, zoomPath_);
        }
        doubleClickedNode_ = nullptr;
    }
}

// 递归绘制节点
void FlameView::drawNode(ImDrawList* drawList, const FlameNode& node, double t,
                          double zoomInclusive, double parentInclusive, double rootInclusive,
                          float x, float y, float totalWidth, int depth) {
    double nodeInclusive = inclusive(node, t);
    if (nodeInclusive <= 0.0) return;

    // 色块宽度 = 节点 inclusive / 聚焦节点 inclusive × 画布总宽度
    float blockWidth = (float)(nodeInclusive / zoomInclusive) * totalWidth;

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

        // 双击 → 记录被双击的节点
        if (ImGui::IsMouseDoubleClicked(0)) {
            doubleClickedNode_ = &node;
        }
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

        float childWidth = (float)(childIncl / zoomInclusive) * totalWidth;
        if (childWidth < 1.0f) continue;

        drawNode(drawList, *child, t, zoomInclusive, nodeInclusive, rootInclusive,
                 childX, y, totalWidth, depth + 1);
        childX += childWidth;
    }
}