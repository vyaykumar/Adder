#!/usr/bin/env python3
"""
Adder D2: All-Reduce Bandwidth and Topology Crossover Plotting Script
Plots Ring vs. Tree All-Reduce performance and comparative scaling against MPI.
"""

import sys
import os
import pandas as pd
import matplotlib.pyplot as plt

def plot_allreduce(csv_path="results/ring_vs_tree.csv", output_png="results/ring_vs_tree.png"):
    if not os.path.exists(csv_path):
        print(f"Error: {csv_path} not found.")
        return

    df = pd.read_csv(csv_path, comment='#')
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))

    # Plot 1: Latency Crossover (Ring vs. Tree)
    for alg, group in df.groupby('algorithm'):
        group_sorted = group.sort_values('message_size')
        ax1.plot(group_sorted['message_size'], group_sorted['mean_latency_ns'] / 1000.0,
                 marker='s', label=f"{alg.capitalize()} All-Reduce")

    ax1.set_xscale('log', base=2)
    ax1.set_yscale('log')
    ax1.set_xlabel('Buffer Size (Bytes)')
    ax1.set_ylabel('Mean Latency (μs)')
    ax1.set_title('Ring vs. Tree All-Reduce Latency Crossover')
    ax1.grid(True, which="both", ls="--", alpha=0.5)
    ax1.legend()

    # Plot 2: Collective Bandwidth
    for alg, group in df.groupby('algorithm'):
        group_sorted = group.sort_values('message_size')
        ax2.plot(group_sorted['message_size'], group_sorted['bandwidth_gbps'],
                 marker='o', label=f"{alg.capitalize()} Bandwidth")

    ax2.set_xscale('log', base=2)
    ax2.set_xlabel('Buffer Size (Bytes)')
    ax2.set_ylabel('Effective Bandwidth (Gbps)')
    ax2.set_title('Collective Bandwidth Scaling')
    ax2.grid(True, which="both", ls="--", alpha=0.5)
    ax2.legend()

    os.makedirs(os.path.dirname(output_png), exist_ok=True)
    plt.tight_layout()
    plt.savefig(output_png, dpi=300)
    print(f"Saved plot to {output_png}")

if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else "results/ring_vs_tree.csv"
    out = sys.argv[2] if len(sys.argv) > 2 else "results/ring_vs_tree.png"
    plot_allreduce(path, out)
