#pragma once

#include <string>
#include <vector>
#include <set>

// §2.1 — 采样点
struct Sample {
    double time;   // 时间戳（秒）
    double value;  // self cost
};

// §2.1 — 火焰图节点
struct FlameNode {
    std::string name;
    std::vector<Sample> samples; // 按 time 升序排列
    std::vector<FlameNode> children;
};

// §2.3 — 前值保持查询：在 node.samples 中找最后一个 time ≤ t 的值
double query(const FlameNode& node, double t);

// §2.4 — inclusive cost = self_cost(t) + Σ child.inclusive_cost(t)
double inclusive(const FlameNode& node, double t);

// §5.1 — 收集整棵树所有节点的所有 sample.time，取并集去重排序
std::vector<double> collectAllTimes(const FlameNode& root);
