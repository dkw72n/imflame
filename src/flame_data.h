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
    
    // §8.4 — inclusive 计算缓存（每帧复用）
    mutable double cachedInclusive = -1.0;
    mutable double cachedTime = -1.0;
};

// §2.3 — 前值保持查询：在 node.samples 中找最后一个 time ≤ t 的值
double query(const FlameNode& node, double t);

// §2.4 — inclusive cost 递归计算
double inclusive(const FlameNode& node, double t);

// §8.4 — 清除 inclusive 缓存（每帧开始前调用）
void clearInclusiveCache(FlameNode& node);

// §5.1 — 收集整棵树所有节点的所有 sample.time，取并集去重排序
std::vector<double> collectAllTimes(const FlameNode& root);