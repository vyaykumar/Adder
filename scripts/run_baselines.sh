#!/usr/bin/env bash
# Project Adder - Run and Record perftest Baselines (D1.1)
# Usage:
#   Local Loopback (Server + Client):
#     ./scripts/run_baselines.sh loopback
#   Cross-Host Server (run on Debian homelab):
#     ./scripts/run_baselines.sh server
#   Cross-Host Client (run on sender pointing to Debian homelab IP):
#     ./scripts/run_baselines.sh client <DEBIAN_IP>

set -e

MODE="${1:-}"
TARGET_IP="${2:-}"
RESULTS_DIR="results"
mkdir -p "$RESULTS_DIR"

case "$MODE" in
    loopback)
        echo "=== Running Local Soft-RoCE Loopback Baselines (Cores 4 & 5) ==="
        
        # 1. ib_send_lat sweep
        echo "[1/3] Measuring ib_send_lat (two-sided send)..."
        taskset -c 4 ib_send_lat -d rxe0 -i 1 -x 1 -F >/dev/null 2>&1 &
        SRV_PID=$!
        sleep 1
        taskset -c 5 ib_send_lat -d rxe0 -i 1 -x 1 -F -a 127.0.0.1 > "$RESULTS_DIR/baseline_loopback_send_lat.txt"
        wait $SRV_PID || true
        echo "      Saved to $RESULTS_DIR/baseline_loopback_send_lat.txt"

        # 2. ib_write_lat sweep
        echo "[2/3] Measuring ib_write_lat (one-sided write)..."
        taskset -c 4 ib_write_lat -d rxe0 -i 1 -x 1 -F >/dev/null 2>&1 &
        SRV_PID=$!
        sleep 1
        taskset -c 5 ib_write_lat -d rxe0 -i 1 -x 1 -F -a 127.0.0.1 > "$RESULTS_DIR/baseline_loopback_write_lat.txt"
        wait $SRV_PID || true
        echo "      Saved to $RESULTS_DIR/baseline_loopback_write_lat.txt"

        # 3. ib_send_bw sweep
        echo "[3/3] Measuring ib_send_bw (bandwidth)..."
        taskset -c 4 ib_send_bw -d rxe0 -i 1 -x 1 -F >/dev/null 2>&1 &
        SRV_PID=$!
        sleep 1
        taskset -c 5 ib_send_bw -d rxe0 -i 1 -x 1 -F -a 127.0.0.1 > "$RESULTS_DIR/baseline_loopback_send_bw.txt"
        wait $SRV_PID || true
        echo "      Saved to $RESULTS_DIR/baseline_loopback_send_bw.txt"

        echo "=== Local Loopback Baselines Complete ==="
        ;;

    server)
        echo "=== Starting perftest Server on Core 1 (Waiting for Client) ==="
        echo "Run './scripts/run_baselines.sh client <THIS_IP>' on the client machine."
        taskset -c 1 ib_send_lat -d rxe0 -i 1 -x 1 -F
        ;;

    client)
        if [ -z "$TARGET_IP" ]; then
            echo "Usage: ./scripts/run_baselines.sh client <SERVER_IP>"
            exit 1
        fi
        echo "=== Running Cross-Host Ethernet Baseline against $TARGET_IP ==="
        taskset -c 1 ib_send_lat -d rxe0 -i 1 -x 1 -F -a "$TARGET_IP" | tee "$RESULTS_DIR/baseline_ethernet_send_lat.txt"
        echo "      Saved to $RESULTS_DIR/baseline_ethernet_send_lat.txt"
        ;;

    *)
        echo "Usage: $0 {loopback | server | client <SERVER_IP>}"
        exit 1
        ;;
esac
