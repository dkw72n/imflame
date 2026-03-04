

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
├══════════════════════════════════════════┤  ← 可拖动分隔条
│  [===========root===========]  ← 深度 0  │
│  [===child_a===][==child_b==]  ← 深度 1  │
│                 [grandchild ]  ← 深度 2  │
│                                          │
│         火焰图（自顶向下）                │
└──────────────────────────────────────────┘
```

- 上下两个区域的高度比例可拖拽调整，**默认 3:7**
- 上方区域高度限制：**最小 150px，最大 350px**
- 分隔条高度 6px，浅灰色（RGB: 80,80,80），悬停时变亮
- 根节点色块**紧贴**分隔条下边缘

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

### 5.3 时间范围选区（Diff 模式触发）

- 在时间序列曲线区域中，按住 **Shift + 左键拖拽** 可选择一段时间范围 `[t0, t1]`
- 拖拽过程中实时显示半透明蓝色选区矩形，选区两端标注时间值
- 释放鼠标后选区固定，火焰图切换为 **Diff 模式**
- 选区激活时，普通模式的游标和点击交互被禁用
- 按 **Escape** 或 **右键** 取消选区，回到普通模式
- 状态栏显示当前模式提示信息

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

### 6.4 Diff 模式

当时间轴存在选区 `[t0, t1]` 时，火焰图切换为 Diff 模式：

**尺寸**：每个节点的色块宽度按 `t1` 时刻的 `inclusive(node, t1)` 计算（与普通模式规则一致，只是固定使用 `t1`）。

**颜色**：由 `delta = inclusive(node, t1) - inclusive(node, t0)` 决定：

| delta 值 | 颜色 |
|-----------|------|
| `delta > 0` | 红色系，越大越红（从灰色渐变到纯红 `rgb(255, 50, 50)`） |
| `delta < 0` | 蓝色系，绝对值越大越蓝（从灰色渐变到纯蓝 `rgb(50, 100, 255)`） |
| `delta == 0` | 灰色 `rgb(128, 128, 128)` |

- 颜色归一化：使用整棵树中 `max(|delta|)` 作为归一化基准，确保颜色映射充分利用色域
- self cost 段不单独高亮（diff 模式下无 self 段可视化）

**Tooltip**（Diff 模式特有）：

```
Name:       child_b
─────────────────────
Self(t0):   3.00  -> Self(t1):   4.00  [+1.00]
Incl(t0):   4.00  -> Incl(t1):   6.00  [+2.00]
Change:     +50.0%
% of root(t1): 35.3%
```

**双击放大**：Diff 模式下双击放大功能保持一致，面包屑颜色同样使用 diff 颜色映射。

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

### 用例 8：Diff 模式
在时间序列曲线区域按住 Shift + 左键拖拽选取 `t0=0.5, t1=2.0` → 火焰图切换为 Diff 模式 → 色块尺寸按 `t1=2.0` 绘制 → 节点颜色反映 `inclusive(t1) - inclusive(t0)` 差值：增长的节点显示红色，减少的显示蓝色，无变化的显示灰色 → 悬停 Tooltip 显示 `t0`/`t1` 的 Self/Inclusive 对比及 Delta 信息 → 按 Escape 取消选区回到普通模式

## 10. 开屏界面与加载进度

### 10.1 设计目标

程序启动时立即显示窗口和加载界面，避免大文件加载时长时间黑屏等待。

### 10.2 加载流程

1. 初始化 GLFW 和 ImGui 上下文
2. 创建窗口并进入**开屏循环**
3. 在开屏循环中启动**后台线程**异步加载数据
4. 后台线程使用 `std::async` + `std::future` 执行 `loadFlameData()`
5. 主线程渲染加载界面并轮询 `std::future` 状态
6. 加载完成后进入**主循环**

### 10.3 进度条实现

- **SAX 流式解析**：`nlohmann::json::sax_parse` 支持流式解析，无需一次性加载整个 JSON 到内存
- **字节位置轮询**：启动独立的 `ProgressPoller` 线程，每 50ms 检查 `std::ifstream::tellg()` 获取当前读取位置
- **进度计算**：`progress = current_position / total_file_size`，最大显示 95%（剩余 5% 用于解析完成后的数据处理）
- **回调机制**：`ProgressCallback` 类型为 `std::function<void(double)>`，进度更新时直接调用回调函数

### 10.4 UI 展示

- 全屏深色背景 (RGB: 24, 24, 24)
- 居中加载内容窗口，包含：
  - "Loading data..." 文字
  - 进度条（蓝色，ImGui ProgressBar）
  - 百分比显示
  - 当前加载的文件路径
- 加载完成后自动切换到主界面
- 加载失败时打印错误信息到控制台并退出
