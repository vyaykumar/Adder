#!/usr/bin/env bash
# Project Adder - Homelab Branch-Aware Auto-Sync Daemon
# Polls GitHub for changes and automatically checks out and updates
# whatever branch was most recently updated on remote.

INTERVAL_SECS=10

echo "=== Starting Adder Homelab Auto-Sync Daemon ==="
echo "Polling every ${INTERVAL_SECS}s for remote updates across all branches..."

while true; do
    # Fetch all branches from origin silently
    git fetch origin --quiet 2>/dev/null || true

    # Find the branch on origin that has the most recent commit
    LATEST_REMOTE_BRANCH=$(git for-each-ref --sort=-committerdate --format='%(refname:short)' refs/remotes/origin/ | grep -v 'origin/HEAD' | head -n1 | sed 's|origin/||')
    
    if [ -n "$LATEST_REMOTE_BRANCH" ]; then
        CURRENT_LOCAL_BRANCH=$(git rev-parse --abbrev-ref HEAD)

        # If Debian is on a different branch than the latest pushed feature branch, switch to it!
        if [ "$CURRENT_LOCAL_BRANCH" != "$LATEST_REMOTE_BRANCH" ]; then
            echo "[Auto-Sync] Detected active feature branch '$LATEST_REMOTE_BRANCH'. Switching from '$CURRENT_LOCAL_BRANCH'..."
            git checkout "$LATEST_REMOTE_BRANCH" --quiet 2>/dev/null || git checkout -b "$LATEST_REMOTE_BRANCH" "origin/$LATEST_REMOTE_BRANCH" --quiet
        fi

        # Check if local is behind origin
        LOCAL_HASH=$(git rev-parse HEAD)
        REMOTE_HASH=$(git rev-parse "origin/$LATEST_REMOTE_BRANCH")

        if [ "$LOCAL_HASH" != "$REMOTE_HASH" ]; then
            echo "[Auto-Sync] New commits found on '$LATEST_REMOTE_BRANCH'! Fast-forwarding..."
            git pull --ff-only origin "$LATEST_REMOTE_BRANCH"
            echo "[Auto-Sync] Updated to $(git rev-parse --short HEAD) successfully."
        fi
    fi

    sleep "$INTERVAL_SECS"
done
