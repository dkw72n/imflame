#pragma once

#include <string>
#include <vector>
#include <set>
#include <memory>

// §2.1 — 采样点
struct Sample {
    double time;   // 时间戳（秒）
    double value;  // self cost
};

// §2.1 — 火焰图节点 (优化内存布局)
struct FlameNode {
    const std::string* name = nullptr; // 使用字符串池，避免重复分配
    uint32_t sample_count = 0;
    uint32_t child_count = 0;
    std::unique_ptr<Sample[]> samples;
    std::unique_ptr<FlameNode[]> children;
};

// §2.3 — 前值保持查询：在 node.samples 中找最后一个 time ≤ t 的值
double query(const FlameNode& node, double t);

// §2.4 — inclusive cost = self_cost(t) + Σ child.inclusive_cost(t)
double inclusive(const FlameNode& node, double t);

// §5.1 — 收集整棵树所有节点的所有 sample.time，取并集去重排序
std::vector<double> collectAllTimes(const FlameNode& root);