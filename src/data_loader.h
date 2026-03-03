#pragma once

#include "flame_data.h"
#include <string>

// §3 — 从 JSON 文件加载 FlameNode 树
FlameNode loadFlameData(const std::string& filepath);
