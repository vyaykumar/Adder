#!/usr/bin/env bash
# Project Adder - Setup Soft-RoCE Environment & Verify Port State
# Usage: ./scripts/setup_env.sh [netdev_interface]

set -e

IFACE="${1:-}"

# 1. Elevate memory locking limits
ulimit -l unlimited
echo "[1/4] Unlocked memory registration limits (ulimit -l unlimited)"

# 2. Set CPU performance governor if available
if [ -d "/sys/devices/system/cpu/cpu0/cpufreq" ]; then
    for gov in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
        [ -f "$gov" ] && echo "performance" | sudo tee "$gov" > /dev/null
    done
    echo "[2/4] CPU scaling governor set to 'performance'"
else
    echo "[2/4] CPU governor directory not found (skipping or running under VM)"
fi

# Disable Intel Turbo Boost to minimize clock jitter
if [ -f "/sys/devices/system/cpu/intel_pstate/no_turbo" ]; then
    echo "1" | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo > /dev/null
    echo "      Intel Turbo Boost disabled for latency consistency"
fi

# 3. Load kernel modules
sudo modprobe rdma_rxe
sudo modprobe ib_uverbs
echo "[3/4] Loaded rdma_rxe and ib_uverbs kernel modules"

# Detect interface if not provided
if [ -z "$IFACE" ]; then
    IFACE=$(ip -o -4 route show to default 2>/dev/null | awk '{print $5}' | head -n1)
    if [ -z "$IFACE" ]; then
        IFACE=$(ip -o link show up | awk -F': ' '{print $2}' | grep -v 'lo' | head -n1)
    fi
fi

if [ -z "$IFACE" ]; then
    echo "[Error] No active network interface found. Specify one: ./setup_env.sh <interface>"
    exit 1
fi

echo "      Binding Soft-RoCE to netdev: $IFACE"

# Remove existing rxe0 if present
if rdma link show rxe0 >/dev/null 2>&1; then
    echo "      Removing existing rxe0 link..."
    sudo rdma link delete rxe0 || true
fi

# Add fresh rxe link
sudo rdma link add rxe0 type rxe netdev "$IFACE"
echo "[4/4] Created Soft-RoCE link 'rxe0' on $IFACE"

echo ""
echo "=== RDMA Device Status ==="
ibv_devinfo -d rxe0
echo ""
echo "=== RoCEv2 GID Table (Index 1) ==="
ibv_devinfo -d rxe0 -v | grep -A 2 "GID\[  1\]" || true
echo ""
echo "Setup complete. Ready to run perftest baselines!"
