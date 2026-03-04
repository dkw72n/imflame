#pragma once

#include "flame_data.h"
#include <string>
#include <functional>

// 进度回调类型：参数为 0.0~1.0 的进度值
using ProgressCallback = std::function<void(double)>;

// §3 — 从 JSON 文件加载 FlameNode 树
// progressCallback 可选，用于在解析过程中报告进度
FlameNode loadFlameData(const std::string& filepath, ProgressCallback progressCallback = nullptr);
