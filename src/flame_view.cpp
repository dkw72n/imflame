#include "flame_view.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numeric>

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

    // 重置本帧双击检测和悬停检测
    doubleClickedNode_ = nullptr;
    hoveredNode_ = nullptr;

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

                    // 记录悬停节点
                    hoveredNode_ = ancestor;

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

        // 记录悬停节点
        hoveredNode_ = &node;

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

// === Diff 模式相关实现 ===

// Diff 颜色映射
// normalizedDelta 范围 [-1, 1]:
//   +1 → 纯红 (255, 50, 50)
//   -1 → 纯蓝 (50, 100, 255)
//    0 → 灰色 (128, 128, 128)
ImU32 FlameView::diffColor(double normalizedDelta) {
    // clamp to [-1, 1]
    float nd = (float)std::max(-1.0, std::min(1.0, normalizedDelta));

    float r, g, b;
    if (nd > 0.0f) {
        // 灰色 → 红色 (interpolate)
        float t = nd;
        r = 128.0f + t * (255.0f - 128.0f);
        g = 128.0f + t * (50.0f - 128.0f);
        b = 128.0f + t * (50.0f - 128.0f);
    } else if (nd < 0.0f) {
        // 灰色 → 蓝色 (interpolate)
        float t = -nd;
        r = 128.0f + t * (50.0f - 128.0f);
        g = 128.0f + t * (100.0f - 128.0f);
        b = 128.0f + t * (255.0f - 128.0f);
    } else {
        r = 128.0f; g = 128.0f; b = 128.0f;
    }

    return IM_COL32((int)r, (int)g, (int)b, 255);
}

// 递归查找整棵树中 |inclusive(t1) - inclusive(t0)| 的最大值
double FlameView::findMaxAbsDelta(const FlameNode& node, double t0, double t1) {
    double incl0 = inclusive(node, t0);
    double incl1 = inclusive(node, t1);
    double maxAbs = std::abs(incl1 - incl0);

    for (const auto& child : node.children) {
        double childMax = findMaxAbsDelta(child, t0, t1);
        if (childMax > maxAbs) maxAbs = childMax;
    }
    return maxAbs;
}

// Diff 模式入口
void FlameView::drawDiff(const FlameNode& root, double t0, double t1,
                          ImVec2 canvasPos, float canvasWidth) {
    // 节点尺寸按 t1 绘制
    double rootInclT1 = inclusive(root, t1);

    if (rootInclT1 <= 0.0) {
        return; // t1 时刻无数据
    }

    // 计算全树最大绝对差值，用于归一化颜色
    double maxAbsDelta = findMaxAbsDelta(root, t0, t1);
    if (maxAbsDelta < 1e-12) maxAbsDelta = 1.0; // 避免除零

    // 重置本帧双击检测和悬停检测
    doubleClickedNode_ = nullptr;
    hoveredNode_ = nullptr;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    if (zoomedNode_ != nullptr) {
        double zoomInclT1 = inclusive(*zoomedNode_, t1);
        if (zoomInclT1 <= 0.0) {
            resetZoom();
        } else {
            // 绘制祖先链面包屑（diff 颜色）
            for (size_t i = 0; i < zoomPath_.size() - 1; ++i) {
                const FlameNode* ancestor = zoomPath_[i];
                double ancestorIncl0 = inclusive(*ancestor, t0);
                double ancestorIncl1 = inclusive(*ancestor, t1);
                double delta = ancestorIncl1 - ancestorIncl0;
                double nd = delta / maxAbsDelta;

                float blockY = canvasPos.y + (int)i * BLOCK_HEIGHT;
                ImVec2 p0(canvasPos.x, blockY);
                ImVec2 p1(canvasPos.x + canvasWidth, blockY + BLOCK_HEIGHT);

                ImU32 color = diffColor(nd);
                // 半透明用于面包屑
                ImU32 dimColor = (color & 0x00FFFFFF) | 0xAA000000;
                drawList->AddRectFilled(p0, p1, dimColor);

                const char* label = ancestor->name.c_str();
                ImVec2 textSize = ImGui::CalcTextSize(label);
                if (textSize.x + 4.0f < canvasWidth) {
                    float textX = canvasPos.x + 2.0f;
                    float textY = blockY + (BLOCK_HEIGHT - textSize.y) * 0.5f;
                    drawList->AddText(ImVec2(textX, textY), IM_COL32(255, 255, 255, 255), label);
                }

                ImVec2 mousePos = ImGui::GetMousePos();
                if (mousePos.x >= p0.x && mousePos.x < p1.x &&
                    mousePos.y >= p0.y && mousePos.y < p1.y) {
                    drawList->AddRect(p0, p1, IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);

                    ImGui::BeginTooltip();
                    ImGui::Text("Name:      %s", ancestor->name.c_str());
                    ImGui::Text("Incl(t0):  %.2f", ancestorIncl0);
                    ImGui::Text("Incl(t1):  %.2f", ancestorIncl1);
                    ImGui::Text("Delta:     %+.2f", delta);
                    ImGui::Text("Delta %%:  %+.1f%%",
                                ancestorIncl0 > 0.0 ? (delta / ancestorIncl0) * 100.0 : 0.0);
                    ImGui::EndTooltip();

                    // 记录悬停节点
                    hoveredNode_ = ancestor;

                    if (ImGui::IsMouseDoubleClicked(0)) {
                        doubleClickedNode_ = ancestor;
                    }
                }
            }

            int depthOffset = (int)(zoomPath_.size() - 1);
            drawNodeDiff(drawList, *zoomedNode_, t0, t1, zoomInclT1, zoomInclT1, rootInclT1,
                         maxAbsDelta, canvasPos.x, canvasPos.y, canvasWidth, depthOffset);
        }
    }

    if (zoomedNode_ == nullptr) {
        drawNodeDiff(drawList, root, t0, t1, rootInclT1, rootInclT1, rootInclT1,
                     maxAbsDelta, canvasPos.x, canvasPos.y, canvasWidth, 0);
    }

    // 处理双击事件
    if (doubleClickedNode_ != nullptr) {
        if (doubleClickedNode_ == &root) {
            resetZoom();
        } else if (doubleClickedNode_ == zoomedNode_) {
            resetZoom();
        } else {
            zoomedNode_ = doubleClickedNode_;
            zoomPath_.clear();
            findNodePath(root, zoomedNode_, zoomPath_);
        }
        doubleClickedNode_ = nullptr;
    }
}

// Diff 模式递归绘制
void FlameView::drawNodeDiff(ImDrawList* drawList, const FlameNode& node, double t0, double t1,
                              double zoomInclusive, double parentInclusive, double rootInclusive,
                              double maxAbsDelta,
                              float x, float y, float totalWidth, int depth) {
    // 尺寸按 t1
    double nodeIncl1 = inclusive(node, t1);
    if (nodeIncl1 <= 0.0) return;

    float blockWidth = (float)(nodeIncl1 / zoomInclusive) * totalWidth;
    if (blockWidth < 1.0f) return;

    float blockY = y + depth * BLOCK_HEIGHT;

    // 颜色按 diff
    double nodeIncl0 = inclusive(node, t0);
    double delta = nodeIncl1 - nodeIncl0;
    double nd = delta / maxAbsDelta;

    ImU32 color = diffColor(nd);
    ImVec2 p0(x, blockY);
    ImVec2 p1(x + blockWidth, blockY + BLOCK_HEIGHT);
    drawList->AddRectFilled(p0, p1, color);

    // 文字标签
    {
        const char* label = node.name.c_str();
        ImVec2 textSize = ImGui::CalcTextSize(label);
        if (textSize.x + 4.0f < blockWidth) {
            float textX = x + 2.0f;
            float textY = blockY + (BLOCK_HEIGHT - textSize.y) * 0.5f;
            drawList->AddText(ImVec2(textX, textY), IM_COL32(255, 255, 255, 255), label);
        }
    }

    // 悬停交互 — Diff Tooltip
    ImVec2 mousePos = ImGui::GetMousePos();
    if (mousePos.x >= p0.x && mousePos.x < p1.x &&
        mousePos.y >= p0.y && mousePos.y < p1.y) {
        drawList->AddRect(p0, p1, IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);

        double selfVal0 = query(node, t0);
        double selfVal1 = query(node, t1);

        ImGui::BeginTooltip();
        ImGui::Text("Name:       %s", node.name.c_str());
        ImGui::Separator();
        ImGui::Text("Self(t0):   %.2f  -> Self(t1):   %.2f  [%+.2f]", selfVal0, selfVal1, selfVal1 - selfVal0);
        ImGui::Text("Incl(t0):   %.2f  -> Incl(t1):   %.2f  [%+.2f]", nodeIncl0, nodeIncl1, delta);
        if (nodeIncl0 > 0.0) {
            ImGui::Text("Change:     %+.1f%%", (delta / nodeIncl0) * 100.0);
        } else if (delta > 0.0) {
            ImGui::Text("Change:     +inf%% (new)");
        }
        ImGui::Text("%% of root(t1): %.1f%%", (nodeIncl1 / rootInclusive) * 100.0);
        ImGui::EndTooltip();

        // 记录悬停节点
        hoveredNode_ = &node;

        if (ImGui::IsMouseDoubleClicked(0)) {
            doubleClickedNode_ = &node;
        }
    }

    // 子节点递归（尺寸按 t1）
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
        double childIncl1 = inclusive(*child, t1);
        if (childIncl1 <= 0.0) continue;

        float childWidth = (float)(childIncl1 / zoomInclusive) * totalWidth;
        if (childWidth < 1.0f) continue;

        drawNodeDiff(drawList, *child, t0, t1, zoomInclusive, nodeIncl1, rootInclusive,
                     maxAbsDelta, childX, y, totalWidth, depth + 1);
        childX += childWidth;
    }
}
