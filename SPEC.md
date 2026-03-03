

需求已完全明确，以下是可执行的 spec。

---

# spec.md — 火焰图展示工具

## 1. 概述

基于 Dear ImGui + ImPlot 的交互式火焰图工具。上方为时间序列曲线（根节点 inclusive cost 随时间变化），下方为选定时间点的自顶向下火焰图。两者通过可拖动游标联动。

## 2. 数据模型

### 2.1 结构定义

```cpp
struct Sample {
    double time;   // 时间戳（秒）
    double value;  // self cost
};

struct FlameNode {
    std::string name;
    std::vector<Sample> samples; // 按 time 升序排列
    std::vector<FlameNode> children;
};
```

### 2.2 语义

| 术语 | 定义 |
|------|------|
| **self cost** | 节点 `samples` 中存储的原始值，代表该节点自身开销 |
| **inclusive cost** | `self_cost(t) + Σ child.inclusive_cost(t)`，递归定义 |

- 每个节点（包括叶子和非叶子）都携带自己的 `samples`
- 不同节点的采样时间点**不要求对齐**

### 2.3 时间点查询：前值保持

```
query(node, t):
    在 node.samples 中二分查找最后一个 time ≤ t 的 Sample
    若找到 → 返回其 value
    若不存在 → 返回 0.0
```

### 2.4 inclusive cost 计算

```
inclusive(node, t):
    return query(node, t) + Σ inclusive(child, t) for child in node.children
```

## 3. 输入数据格式

使用 JSON 文件，程序启动时加载：

```json
{
  "name": "root",
  "samples": [[0.0, 10.0], [1.0, 12.0], [3.0, 8.0]],
  "children": [
    {
      "name": "child_a",
      "samples": [[0.0, 5.0], [2.0, 7.0]],
      "children": []
    },
    {
      "name": "child_b",
      "samples": [[0.5, 3.0], [1.5, 4.0]],
      "children": [
        {
          "name": "grandchild",
          "samples": [[0.0, 1.0], [1.0, 2.0]],
          "children": []
        }
      ]
    }
  ]
}
```

`samples` 中每个元素为 `[time, value]`，须按 `time` 升序排列。

## 4. 界面布局

```
┌──────────────────────────────────────────┐
│         ImPlot 时间序列曲线               │
│                                          │
│   Y: 根节点 inclusive cost               │
│   X: 时间轴                              │
│         │← 可拖动垂直游标                 │
├──────────────────────────────────────────┤
│  [===========root===========]  ← 深度 0  │
│  [===child_a===][==child_b==]  ← 深度 1  │
│                 [grandchild ]  ← 深度 2  │
│                                          │
│         火焰图（自顶向下）                │
└──────────────────────────────────────────┘
```

- 上下两个区域的高度比例约为 **3:7**，上方区域最小高度 150px
- 根节点色块**紧贴**上方曲线图的下边缘

## 5. 上方：时间序列曲线

### 5.1 数据

- 横轴：时间
- 纵轴：根节点在每个时间点的 **inclusive cost**
- 绘制用的时间点集合：整棵树所有节点的所有 `sample.time` 取**并集去重排序**
- 对每个时间点 `t`，计算 `inclusive(root, t)` 作为 Y 值
- 用 **ImPlot 阶梯线（`ImPlot::PlotStairs`）** 绘制，视觉上匹配前值保持语义

### 5.2 游标

- 一条垂直线，初始位置为数据中最小时间点
- 拖动方式：鼠标在曲线图区域内点击或拖拽，游标跟随鼠标 X 坐标
- 游标范围限制在 `[min_time, max_time]`
- 游标线上方显示当前时间值，格式 `t = {value:.3f}s`

## 6. 下方：火焰图

### 6.1 渲染规则

| 属性 | 规则 |
|------|------|
| 色块宽度 | `节点 inclusive_cost(t) / root inclusive_cost(t) × 画布总宽度` |
| 色块高度 | 固定 **24px** |
| 垂直位置 | `depth × 24px`（根为 depth 0，紧贴上方图表） |
| 水平位置 | 子节点从父节点左边界开始，按**名称字母序**依次向右排列 |
| 颜色 | 对 `name` 做哈希，映射到 HSV 色环（H = hash % 360, S = 0.6, V = 0.9） |
| 最小宽度 | 色块宽度 < 1px 时不绘制 |

### 6.2 self cost 的可视化

当一个节点的 `self_cost(t) > 0` 时，在该节点色块的**右侧**保留一段宽度表示 self 部分：

```
[  child_a  |  child_b  | self ]   ← parent 节点
                                    self 段宽度 = self_cost / inclusive_cost × 色块宽度
```

self 段与该节点色块颜色相同但**亮度提高 20%**，以示区分。子节点从左侧开始排列，self 部分占据剩余空间在右侧。

### 6.3 交互

**悬停 Tooltip**：鼠标停留在任一色块上时显示：

```
Name:       child_b
Self cost:  4.00
Inclusive:   6.00
% of root:  35.3%
% of parent: 50.0%
```

**高亮**：悬停时该色块边框变为白色 2px。

**双击放大**：双击任一色块，该节点水平放大充满整个 X 轴：

- 被放大节点的 inclusive cost 作为宽度基准，其子节点按 `child.inclusive / zoomed.inclusive × 画布宽度` 排列
- 被放大节点上方显示**祖先面包屑**：从根节点到被放大节点的路径上的每个祖先各占一行，占满画布宽度，使用半透明颜色以示上下文
- 双击已放大的节点或双击根节点 → 回到全局视图
- 双击祖先面包屑中的某个祖先 → 切换缩放到该祖先层级
- Tooltip 中 `% of root` 始终基于真实根节点的 inclusive cost，不受缩放影响
- 缩放模式下，若被放大节点在当前时间点的 inclusive cost 为 0，自动回退到全局视图

## 7. 渲染使用的绘图 API

| 区域 | 方法 |
|------|------|
| 时间序列曲线 | ImPlot (`BeginPlot` / `PlotStairs` / `DragLineX`) |
| 火焰图 | ImGui `ImDrawList`（`AddRectFilled` / `AddRect` / `AddText`） |
| Tooltip | `ImGui::BeginTooltip` / `ImGui::Text` |

## 8. 性能约束

- 目标支持 **1000 个节点、每节点 10000 个采样点** 不卡顿
- inclusive cost 在游标移动时重新计算（无需预计算全部时间点，只算当前 `t`）
- 火焰图仅绘制可见区域的色块（宽度 ≥ 1px 的节点）

## 9. 验收用例

### 用例 1：基础渲染
加载示例 JSON → 上方出现阶梯曲线 → 下方出现火焰图 → 色块宽度之和等于画布宽度

### 用例 2：时间联动
拖动游标从 `t=0` 到 `t=3` → 火焰图各色块宽度实时变化 → 曲线上高亮点跟随

### 用例 3：前值保持正确性
节点 `child_a` 的 samples 为 `[[0, 5], [2, 7]]`：
- 游标在 `t=1.5` → child_a self cost = 5（取 t=0 的值）
- 游标在 `t=2.0` → child_a self cost = 7
- 游标在 `t=-1` → child_a self cost = 0

### 用例 4：inclusive = self + children
在任意时间点 `t`，任一节点色块宽度 = 其所有子节点色块宽度之和 + self 段宽度

### 用例 5：Tooltip 内容
悬停某色块 → 弹出 Tooltip → 数值与手动计算一致

### 用例 6：零值处理
若 `inclusive(root, t) == 0`，火焰图区域显示空白，不崩溃

### 用例 7：双击放大
双击 `child_b` 色块 → `child_b` 水平放大充满整个 X 轴 → 其子节点 `grandchild` 按比例放大 → 上方出现 `root` 祖先面包屑 → 再次双击 `child_b` 或双击 `root` 面包屑 → 回到全局视图
