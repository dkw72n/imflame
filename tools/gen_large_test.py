#!/usr/bin/env python3
"""生成大规模测试数据用于性能测试"""

import json
import random

def generate_node(name, depth, max_depth, branch_factor, samples_per_node):
    """递归生成火焰图节点"""
    node = {
        "name": name,
        "samples": [[t, random.uniform(1.0, 100.0)] for t in range(samples_per_node)],
        "children": []
    }
    
    if depth < max_depth:
        for i in range(branch_factor):
            child_name = f"{name}_child{i}"
            child = generate_node(child_name, depth + 1, max_depth, branch_factor, samples_per_node)
            node["children"].append(child)
    
    return node

def main():
    # 生成 1000 节点 × 1000 样本的测试数据
    # 深度 4, 分支因子 5: 1 + 5 + 25 + 125 + 625 = 781 节点
    print("Generating large test data (781 nodes × 1000 samples)...")
    root = generate_node("root", 0, 4, 5, 1000)
    
    output_file = "data/large_test.json"
    with open(output_file, "w") as f:
        json.dump(root, f)
    
    print(f"Generated {output_file}")
    
    # 计算文件大小
    import os
    size = os.path.getsize(output_file)
    print(f"File size: {size / 1024 / 1024:.2f} MB")

if __name__ == "__main__":
    main()
