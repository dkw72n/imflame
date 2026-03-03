#!/usr/bin/env python3
"""
生成压力测试数据：1000 个节点 × 10000 个采样点
用法: python tools/gen_test_data.py > data/stress_test.json
"""

import json
import random
import sys

NUM_NODES = 1000
SAMPLES_PER_NODE = 10000
MAX_DEPTH = 10
TIME_RANGE = (0.0, 100.0)
VALUE_RANGE = (0.1, 10.0)

node_count = 0

def gen_samples():
    times = sorted(random.uniform(*TIME_RANGE) for _ in range(SAMPLES_PER_NODE))
    return [[round(t, 6), round(random.uniform(*VALUE_RANGE), 2)] for t in times]

def gen_tree(remaining, depth=0):
    global node_count
    if remaining <= 0:
        return None

    node_count += 1
    name = f"node_{node_count:04d}"
    node = {
        "name": name,
        "samples": gen_samples(),
        "children": []
    }
    remaining -= 1

    if depth < MAX_DEPTH and remaining > 0:
        num_children = random.randint(1, min(5, remaining))
        per_child = remaining // num_children
        for i in range(num_children):
            alloc = per_child if i < num_children - 1 else remaining
            child = gen_tree(alloc, depth + 1)
            if child:
                node["children"].append(child)
                remaining -= count_nodes(child)
                if remaining <= 0:
                    break

    return node

def count_nodes(node):
    count = 1
    for c in node["children"]:
        count += count_nodes(c)
    return count

if __name__ == "__main__":
    random.seed(42)
    tree = gen_tree(NUM_NODES)
    actual = count_nodes(tree)
    print(json.dumps(tree), file=sys.stdout if len(sys.argv) < 2 else open(sys.argv[1], 'w'))
    print(f"Generated {actual} nodes with {SAMPLES_PER_NODE} samples each.", file=sys.stderr)
