#include "flame_data.h"
#include <algorithm>
#include <set>

// §2.3 — 前值保持查询
// 在 node.samples 中二分查找最后一个 time ≤ t 的 Sample
// 若找到 → 返回其 value；若不存在 → 返回 0.0
double query(const FlameNode& node, double t) {
    if (node.sample_count == 0) return 0.0;

    auto it = std::upper_bound(node.samples.get(), node.samples.get() + node.sample_count, t,
        [](double t_val, const Sample& s) {
            return t_val < s.time;
        });

    if (it == node.samples.get()) {
        // 所有采样点的时间都大于 t
        return 0.0;
    }
    --it;
    return it->value;
}

// §2.4 — inclusive cost 递归计算
double inclusive(const FlameNode& node, double t) {
    double cost = query(node, t);
    for (uint32_t i = 0; i < node.child_count; ++i) {
        cost += inclusive(node.children[i], t);
    }
    return cost;
}

// §5.1 — 收集全局时间点集合
static void collectTimesRecursive(const FlameNode& node, std::set<double>& times) {
    for (uint32_t i = 0; i < node.sample_count; ++i) {
        times.insert(node.samples[i].time);
    }
    for (uint32_t i = 0; i < node.child_count; ++i) {
        collectTimesRecursive(node.children[i], times);
    }
}

std::vector<double> collectAllTimes(const FlameNode& root) {
    std::set<double> timeSet;
    collectTimesRecursive(root, timeSet);
    return std::vector<double>(timeSet.begin(), timeSet.end());
}