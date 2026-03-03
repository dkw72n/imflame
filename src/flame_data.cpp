#include "flame_data.h"
#include <algorithm>
#include <set>

// §2.3 — 前值保持查询
// 在 node.samples 中二分查找最后一个 time ≤ t 的 Sample
// 若找到 → 返回其 value；若不存在 → 返回 0.0
double query(const FlameNode& node, double t) {
    const auto& s = node.samples;
    if (s.empty()) return 0.0;

    // 找第一个 time > t 的位置
    auto it = std::upper_bound(s.begin(), s.end(), t,
        [](double val, const Sample& sample) {
            return val < sample.time;
        });

    if (it == s.begin()) {
        // 所有采样点的时间都大于 t
        return 0.0;
    }
    --it;
    return it->value;
}

// §2.4 — inclusive cost 递归计算
double inclusive(const FlameNode& node, double t) {
    double result = query(node, t);
    for (const auto& child : node.children) {
        result += inclusive(child, t);
    }
    return result;
}

// §5.1 — 收集全局时间点集合
static void collectTimesRecursive(const FlameNode& node, std::set<double>& times) {
    for (const auto& s : node.samples) {
        times.insert(s.time);
    }
    for (const auto& child : node.children) {
        collectTimesRecursive(child, times);
    }
}

std::vector<double> collectAllTimes(const FlameNode& root) {
    std::set<double> timeSet;
    collectTimesRecursive(root, timeSet);
    return std::vector<double>(timeSet.begin(), timeSet.end());
}
