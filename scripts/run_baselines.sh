#!/usr/bin/env bash
# Project Adder - Run and Record perftest Baselines (D1.1)
# Usage:
#   Local Loopback (Server + Client on single host):
#     ./scripts/run_baselines.sh loopback
#   Distributed Cloud Server (run on remote Azure instance):
#     ./scripts/run_baselines.sh server
#   Distributed Cloud Client (run on sender pointing to Azure instance IP):
#     ./scripts/run_baselines.sh client <TARGET_IP>
#
# Note: Physical cross-host Ethernet cable links have been forgone in favor
# of dedicated Azure Cloud instances. Debian 12 is retained for standalone
# bare-metal loopback reference execution.

set -e

MODE="${1:-}"
TARGET_IP="${2:-}"
RESULTS_DIR="results"
mkdir -p "$RESULTS_DIR"

# Dynamically select valid cores based on available CPU count
NUM_CPUS=$(nproc)
if [ "$NUM_CPUS" -ge 6 ]; then
    # On 6+ core machines (e.g. 12th Gen Intel laptop), use E-cores 4 & 5
    CORE_SRV=4
    CORE_CLI=5
elif [ "$NUM_CPUS" -ge 4 ]; then
    CORE_SRV=2
    CORE_CLI=3
elif [ "$NUM_CPUS" -ge 2 ]; then
    CORE_SRV=0
    CORE_CLI=1
else
    CORE_SRV=0
    CORE_CLI=0
fi

echo "[Topology] Detected $NUM_CPUS CPUs. Selected Core $CORE_SRV (Server) and Core $CORE_CLI (Client)."

case "$MODE" in
    loopback)
        # Find the actual IP bound to the interface hosting rxe0
        LOCAL_IP=$(ip -o -4 route show to default 2>/dev/null | awk '{print $5}' | xargs -I{} ip -o -4 addr show {} 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -n1)
        if [ -z "$LOCAL_IP" ]; then
            LOCAL_IP=$(hostname -I | awk '{print $1}')
        fi
        [ -z "$LOCAL_IP" ] && LOCAL_IP="127.0.0.1"

        echo "=== Running Local Soft-RoCE Loopback Baselines (Cores $CORE_SRV & $CORE_CLI on $LOCAL_IP) ==="
        
        # Kill any lingering perftest processes
        killall ib_send_lat ib_write_lat ib_send_bw 2>/dev/null || true
        sleep 1

        # 1. ib_send_lat sweep
        echo "[1/3] Measuring ib_send_lat (two-sided send)..."
        taskset -c "$CORE_SRV" ib_send_lat -d rxe0 -i 1 -x 1 -F -a >/dev/null 2>&1 &
        SRV_PID=$!
        sleep 1
        taskset -c "$CORE_CLI" ib_send_lat -d rxe0 -i 1 -x 1 -F -a "$LOCAL_IP" > "$RESULTS_DIR/baseline_loopback_send_lat.txt"
        wait $SRV_PID || true
        echo "      Saved to $RESULTS_DIR/baseline_loopback_send_lat.txt"

        # 2. ib_write_lat sweep
        echo "[2/3] Measuring ib_write_lat (one-sided write)..."
        taskset -c "$CORE_SRV" ib_write_lat -d rxe0 -i 1 -x 1 -F -a >/dev/null 2>&1 &
        SRV_PID=$!
        sleep 1
        taskset -c "$CORE_CLI" ib_write_lat -d rxe0 -i 1 -x 1 -F -a "$LOCAL_IP" > "$RESULTS_DIR/baseline_loopback_write_lat.txt"
        wait $SRV_PID || true
        echo "      Saved to $RESULTS_DIR/baseline_loopback_write_lat.txt"

        # 3. ib_send_bw sweep
        echo "[3/3] Measuring ib_send_bw (bandwidth)..."
        taskset -c "$CORE_SRV" ib_send_bw -d rxe0 -i 1 -x 1 -F -a >/dev/null 2>&1 &
        SRV_PID=$!
        sleep 1
        taskset -c "$CORE_CLI" ib_send_bw -d rxe0 -i 1 -x 1 -F -a "$LOCAL_IP" > "$RESULTS_DIR/baseline_loopback_send_bw.txt"
        wait $SRV_PID || true
        echo "      Saved to $RESULTS_DIR/baseline_loopback_send_bw.txt"

        echo "=== Local Loopback Baselines Complete ==="
        ;;

    server)
        SINGLE_CORE=$((NUM_CPUS > 1 ? 1 : 0))
        echo "=== Starting perftest Server on Core $SINGLE_CORE (Waiting for Client) ==="
        echo "Run './scripts/run_baselines.sh client <THIS_IP>' on the client machine."
        taskset -c "$SINGLE_CORE" ib_send_lat -d rxe0 -i 1 -x 1 -F -a
        ;;

    client)
        if [ -z "$TARGET_IP" ]; then
            echo "Usage: ./scripts/run_baselines.sh client <SERVER_IP>"
            exit 1
        fi
        SINGLE_CORE=$((NUM_CPUS > 1 ? 1 : 0))
        echo "=== Running Distributed Cloud Baseline against $TARGET_IP on Core $SINGLE_CORE ==="
        taskset -c "$SINGLE_CORE" ib_send_lat -d rxe0 -i 1 -x 1 -F -a "$TARGET_IP" | tee "$RESULTS_DIR/baseline_cloud_send_lat.txt"
        echo "      Saved to $RESULTS_DIR/baseline_cloud_send_lat.txt"
        ;;

    *)
        echo "Usage: $0 {loopback | server | client <SERVER_IP>}"
        exit 1
        ;;
esac
