# 开发计划 — 火焰图展示工具 (imflame)

## 技术栈

| 组件 | 选型 |
|------|------|
| 语言 | C++17 |
| GUI 框架 | Dear ImGui (docking 分支) |
| 图表 | ImPlot |
| 图形后端 | GLFW + OpenGL3 |
| JSON 解析 | nlohmann/json |
| 构建系统 | CMake |

## 依赖关系

```
阶段0 ──→ 阶段1 ──→ 阶段2 ──┐
                              ├──→ 阶段4 ──→ 阶段5 ──→ 阶段6
                   阶段3 ──┘
```

- 阶段 2 和 3 可并行开发（两者都依赖阶段 1 的数据模型）
- 阶段 4 需要 2 和 3 都完成（交互需要曲线+火焰图都就绪）

---

## 阶段 0：项目搭建

| # | 任务 | 说明 |
|---|------|------|
| 0.1 | 搭建 CMake 工程结构 | 创建 `CMakeLists.txt`，配置 C++17 标准 |
| 0.2 | 集成第三方依赖 | Dear ImGui + GLFW/OpenGL3 后端、ImPlot、nlohmann/json |
| 0.3 | 创建入口 `main.cpp` | 初始化窗口、ImGui 上下文、ImPlot 上下文，跑通空白窗口 |
| 0.4 | 编写 `README.md` | 构建说明、运行方法 |

### 产出文件
- `CMakeLists.txt`
- `src/main.cpp`
- `README.md`
- `third_party/` (依赖目录)

---

## 阶段 1：数据模型与加载

| # | 任务 | 说明 | 对应 SPEC |
|---|------|------|-----------|
| 1.1 | 定义 `Sample` / `FlameNode` 结构体 | 按 SPEC 结构定义 | §2.1 |
| 1.2 | 实现 JSON 解析器 | 从文件加载为 `FlameNode` 树 | §3 |
| 1.3 | 实现 `query(node, t)` | 二分查找前值保持 | §2.3 |
| 1.4 | 实现 `inclusive(node, t)` | 递归计算 inclusive cost | §2.4 |
| 1.5 | 收集全局时间点集合 | 遍历整棵树取所有 `sample.time` 的并集去重排序 | §5.1 |
| 1.6 | 单元测试 | 验证前值保持、inclusive 计算、零值处理 | §9 用例 3/4/6 |

### 产出文件
- `src/flame_data.h` — 结构体定义
- `src/flame_data.cpp` — 查询与计算逻辑
- `src/data_loader.h / .cpp` — JSON 加载
- `data/sample.json` — 示例数据文件

---

## 阶段 2：上方时间序列曲线

| # | 任务 | 说明 | 对应 SPEC |
|---|------|------|-----------|
| 2.1 | 预计算曲线数据 | 对全局时间点集合计算 `inclusive(root, t)` 得到 `(time[], value[])` | §5.1 |
| 2.2 | 用 ImPlot 绘制阶梯线 | `ImPlot::BeginPlot` + `ImPlot::PlotStairs` | §5.1 |
| 2.3 | 实现可拖动游标 | `ImPlot::DragLineX`，初始位置 = 最小时间，限制范围 `[min_time, max_time]` | §5.2 |
| 2.4 | 游标标签显示 | 在游标线上方显示 `t = {value:.3f}s` | §5.2 |

### 产出文件
- `src/timeline_view.h / .cpp` — 时间序列曲线视图

---

## 阶段 3：下方火焰图渲染

| # | 任务 | 说明 | 对应 SPEC |
|---|------|------|-----------|
| 3.1 | 布局计算 | 上下区域 3:7 比例分配，上方最小 150px | §4 |
| 3.2 | 名称哈希 → 颜色映射 | `H = hash(name) % 360, S=0.6, V=0.9` | §6.1 |
| 3.3 | 火焰图递归渲染 | 使用 `ImDrawList` 的 `AddRectFilled` / `AddText`，按深度、名称字母序排列子节点 | §6.1 |
| 3.4 | self cost 可视化 | 在色块右侧预留 self 段，亮度 +20% | §6.2 |
| 3.5 | 最小宽度裁剪 | 宽度 < 1px 的色块不绘制 | §6.1, §8 |

### 产出文件
- `src/flame_view.h / .cpp` — 火焰图视图

---

## 阶段 4：交互功能

| # | 任务 | 说明 | 对应 SPEC |
|---|------|------|-----------|
| 4.1 | 时间联动 | 游标变动 → 重新计算当前 `t` 的 inclusive cost → 火焰图实时更新 | §5.2, §9 用例 2 |
| 4.2 | 悬停高亮 | 鼠标在色块上 → 白色 2px 边框 | §6.3 |
| 4.3 | Tooltip | 显示 Name / Self cost / Inclusive / % of root / % of parent | §6.3, §9 用例 5 |
| 4.4 | 双击放大 | 双击色块 → 该节点水平放大充满 X 轴，显示祖先面包屑，双击已放大节点/根节点回退 | §6.3, §9 用例 7 |
| 4.5 | 时间范围选区 | 时间轴 Shift + 拖拽选择时间范围 `[t0, t1]`，绘制选区矩形，Esc/右键取消 | §5.3 |
| 4.6 | Diff 模式火焰图 | 选区激活时切换为 diff 渲染，尺寸按 t1，颜色按 delta 映射红/蓝/灰 | §6.4, §9 用例 8 |

### 产出文件
- 修改 `src/timeline_view.h / .cpp` — 添加拖拽选区状态和交互
- 修改 `src/flame_view.h` — 添加缩放状态（zoomedNode_、zoomPath_）、findNodePath()、diffColor()、drawDiff()、drawNodeDiff()、findMaxAbsDelta()
- 修改 `src/flame_view.cpp` — 添加交互逻辑、缩放渲染、祖先面包屑、Diff 模式渲染
- 修改 `src/main.cpp` — 联动状态管理、普通/Diff 模式切换

---

## 阶段 5：性能优化

| # | 任务 | 说明 | 对应 SPEC |
|---|------|------|-----------|
| 5.1 | 仅按需计算 | 游标移动时只计算当前 `t` 的 inclusive cost，不预算所有时间点 | §8 |
| 5.2 | 可见性裁剪 | 仅绘制宽度 ≥ 1px 的节点，跳过不可见子树 | §8 |
| 5.3 | 压力测试 | 生成 1000 节点 × 10000 采样点的测试数据，验证不卡顿 | §8 |

### 产出文件
- `tools/gen_test_data.py` — 压力测试数据生成脚本

---

## 阶段 6：验收与收尾

| # | 任务 | 说明 | 对应 SPEC |
|---|------|------|-----------|
| 6.1 | 逐条过验收用例 | 用例 1–8 | §9 |
| 6.2 | 边界情况处理 | 空 JSON / 单节点 / root inclusive = 0 不崩溃 | §9 用例 6 |
| 6.3 | 代码清理与文档 | 注释、README 更新 | — |

### 验收用例清单

| 用例 | 描述 | 通过标准 |
|------|------|----------|
| 1 | 基础渲染 | 加载 JSON → 阶梯曲线 + 火焰图正常显示，色块宽度之和 = 画布宽度 |
| 2 | 时间联动 | 拖动游标 → 火焰图实时变化，曲线高亮点跟随 |
| 3 | 前值保持正确性 | `child_a` 在 t=1.5 → self=5, t=2.0 → self=7, t=-1 → self=0 |
| 4 | inclusive = self + children | 任意节点色块宽度 = 子节点色块宽度之和 + self 段宽度 |
| 5 | Tooltip 内容 | 悬停色块 → 数值与手动计算一致 |
| 6 | 零值处理 | root inclusive = 0 时显示空白，不崩溃 |
| 7 | 双击放大 | 双击 `child_b` → 放大充满 X 轴 → 显示祖先面包屑 → 双击回退到全局视图 |
| 8 | Diff 模式 | Shift+拖拽选区 → diff 火焰图显示红/蓝/灰色块 → Tooltip 显示 t0/t1 对比 → Esc 取消 |

---

## 阶段 7：内存深度优化（专项）

| # | 任务 | 说明 | 预期收益 |
|---|------|------|----------|
| 7.1 | 消除 `std::vector` 冗余容量 | 在 `data_loader.cpp` 中解析 `samples` 和 `children` 时，提前获取数组大小并 `reserve`，或在加载完成后调用 `shrink_to_fit()`。 | 减少 20%~50% 的常驻内存浪费 |
| 7.2 | 引入 SAX/流式 JSON 解析 | 替换 `nlohmann::json::parse` (DOM解析)，改用 `nlohmann::json::sax_parse` 在读取词法单元时直接构建 `FlameNode` 树。 | 消除解析期 5~10 倍的峰值内存开销 |
| 7.3 | 曲线数据降采样 (LOD) | 在 `TimelineView::buildInclusiveCurve` 中，根据屏幕像素宽度对密集时间点进行降采样（如保留区间最大/最小值），限制 `CurveData` 的最大点数。 | 显著降低 `TimelineView` 多份缓存的常驻内存，提升 ImPlot 渲染性能 |
| 7.4 | 优化 `FlameNode` 结构内存 | 评估 `FlameNode` 的内存占用，在海量节点且样本极少的极端场景下，考虑更紧凑的数据结构或内存池分配。 | 降低海量节点场景下的基础常驻内存 |
