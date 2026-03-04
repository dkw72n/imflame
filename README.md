# imflame — 火焰图展示工具

基于 Dear ImGui + ImPlot 的交互式火焰图查看器。上方为时间序列曲线（根节点 inclusive cost 随时间变化），下方为选定时间点的自顶向下火焰图，两者通过可拖动游标联动。

## 功能特性

- **时间序列曲线**：使用阶梯线绘制根节点 inclusive cost 的时间变化
- **可拖动游标**：点击或拖拽游标选择时间点
- **火焰图**：自顶向下展示各节点的 inclusive cost 占比
- **Self Cost 可视化**：在色块右侧以高亮色显示节点自身开销
- **悬停 Tooltip**：显示节点名称、self cost、inclusive cost、占比信息
- **前值保持**：采用二分查找实现不对齐采样点的准确查询

## 技术栈

| 组件 | 选型 |
|------|------|
| 语言 | C++17 |
| GUI 框架 | Dear ImGui (docking 分支) |
| 图表 | ImPlot |
| 图形后端 | GLFW + OpenGL3 |
| JSON 解析 | nlohmann/json |
| 构建系统 | CMake 3.20+ |

## 构建方法

### 前置依赖

- CMake 3.20 或更高版本
- C++17 兼容编译器（MSVC 2019+, GCC 8+, Clang 7+）
- Git（用于 FetchContent 下载依赖）
- OpenGL 开发库

### 编译步骤

```bash
# 创建构建目录
mkdir build
cd build

# 配置（首次运行会自动下载依赖，需联网）
cmake ..

# 编译
cmake --build . --config Release
```

### Windows (Visual Studio) - 使用 CMakePresets (推荐)

```bash
# 使用预设配置（推荐）
cmake --preset vs2022-x64
cmake --build --preset vs2022-x64-release
```

### Windows (Visual Studio) - 传统方式

```bash
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

## 运行方法

```bash
# 使用默认数据文件 (data/sample.json)
./imflame

# 指定数据文件
./imflame path/to/your/data.json
```

## 数据格式

输入为 JSON 文件，格式如下：

```json
{
  "name": "root",
  "samples": [[0.0, 10.0], [1.0, 12.0]],
  "children": [
    {
      "name": "child_a",
      "samples": [[0.0, 5.0]],
      "children": []
    }
  ]
}
```

- `name`：节点名称
- `samples`：`[time, value]` 对数组，按 time 升序排列，value 为 self cost
- `children`：子节点数组

## 操作说明

- **拖动游标**：在上方曲线图中点击或拖拽黄色垂直线，选择时间点
- **查看详情**：将鼠标悬停在火焰图色块上，查看 Tooltip 信息
- **时间联动**：游标移动时，下方火焰图自动更新

## 项目结构

```
imflame/
├── CMakeLists.txt          # 构建配置
├── README.md               # 本文件
├── data/
│   └── sample.json         # 示例数据
├── src/
│   ├── main.cpp            # 程序入口
│   ├── flame_data.h/.cpp   # 数据模型与查询算法
│   ├── data_loader.h/.cpp  # JSON 数据加载
│   ├── timeline_view.h/.cpp # 时间序列曲线视图
│   └── flame_view.h/.cpp   # 火焰图视图
└── SPEC.md / PLAN.md / PROGRESS.md  # 项目文档
```
