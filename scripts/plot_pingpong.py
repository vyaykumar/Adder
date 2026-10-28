#!/usr/bin/env python3
"""
Adder D1: RDMA Ping-Pong Latency Plotting Script
Generates latency comparison curves across message sizes, transports (RC vs UD),
and modes (Send/Recv vs RDMA Write, Inline vs Registered).
"""

import sys
import os
import pandas as pd
import matplotlib.pyplot as plt

def plot_pingpong(csv_path="results/pingpong_latency.csv", output_png="results/pingpong_latency.png"):
    if not os.path.exists(csv_path):
        print(f"Error: {csv_path} not found.")
        return

    # Skip comment lines beginning with #
    df = pd.read_csv(csv_path, comment='#')
    print(f"Loaded {len(df)} records from {csv_path}")

    fig, ax = plt.subplots(figsize=(10, 6))

    for (transport, percentile), group in df.groupby(['transport', 'percentile']):
        if percentile in [50.0, 99.0]:
            group_sorted = group.sort_values('message_size')
            label = f"{transport.upper()} P{int(percentile)}"
            ax.plot(group_sorted['message_size'], group_sorted['latency_ns'] / 1000.0,
                    marker='o', label=label)

    ax.set_xscale('log', base=2)
    ax.set_yscale('log')
    ax.set_xlabel('Message Size (Bytes)')
    ax.set_ylabel('One-Way Latency (μs)')
    ax.set_title('Adder: RDMA Ping-Pong Latency vs. Message Size')
    ax.grid(True, which="both", ls="--", alpha=0.5)
    ax.legend()

    os.makedirs(os.path.dirname(output_png), exist_ok=True)
    plt.tight_layout()
    plt.savefig(output_png, dpi=300)
    print(f"Saved plot to {output_png}")

if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else "results/pingpong_latency.csv"
    out = sys.argv[2] if len(sys.argv) > 2 else "results/pingpong_latency.png"
    plot_pingpong(path, out)
