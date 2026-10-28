#!/usr/bin/env python3
"""
Adder D3: Straggler Mitigation Tail Latency Plotting Script
Visualizes P50, P99, and P99.9 tail latency with and without adaptive rescheduling.
"""

import sys
import os
import pandas as pd
import matplotlib.pyplot as plt

def plot_straggler(csv_path="results/straggler_tail_latency.csv", output_png="results/straggler_tail_latency.png"):
    if not os.path.exists(csv_path):
        print(f"Error: {csv_path} not found.")
        return

    df = pd.read_csv(csv_path, comment='#')
    fig, ax = plt.subplots(figsize=(10, 6))

    mitigated = df[df['mitigation_enabled'] == 1].sort_values('straggler_delay_us')
    unmitigated = df[df['mitigation_enabled'] == 0].sort_values('straggler_delay_us')

    if not unmitigated.empty:
        ax.plot(unmitigated['straggler_delay_us'], unmitigated['p99_ns'] / 1000.0,
                'r--o', label="Without Mitigation (P99)")
        ax.plot(unmitigated['straggler_delay_us'], unmitigated['p999_ns'] / 1000.0,
                'r:x', label="Without Mitigation (P99.9)")

    if not mitigated.empty:
        ax.plot(mitigated['straggler_delay_us'], mitigated['p99_ns'] / 1000.0,
                'g-s', label="With Adaptive Mitigation (P99)")
        ax.plot(mitigated['straggler_delay_us'], mitigated['p999_ns'] / 1000.0,
                'g:^', label="With Adaptive Mitigation (P99.9)")

    ax.set_xlabel('Injected Straggler Delay (μs)')
    ax.set_ylabel('Tail Latency (μs)')
    ax.set_title('Adder: Tail Latency Mitigation under Injected Straggler Delay')
    ax.grid(True, which="both", ls="--", alpha=0.5)
    ax.legend()

    os.makedirs(os.path.dirname(output_png), exist_ok=True)
    plt.tight_layout()
    plt.savefig(output_png, dpi=300)
    print(f"Saved plot to {output_png}")

if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else "results/straggler_tail_latency.csv"
    out = sys.argv[2] if len(sys.argv) > 2 else "results/straggler_tail_latency.png"
    plot_straggler(path, out)
