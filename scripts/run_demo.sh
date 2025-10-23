#!/bin/bash

# run_demo.sh - Loads the module, runs a test, and unloads the module.
# Must be run with sudo.

set -e

# --- Configuration ---
MODULE_NAME="nxp_simtemp"
MODULE_PATH="../kernel/${MODULE_NAME}.ko"
CLI_APP="../user/cli/main.py"
SYSFS_PATH="/sys/class/misc/simtemp"

# --- Helper Functions ---
cleanup() {
    echo
    echo "--- Cleaning up ---"
    if lsmod | grep -q "$MODULE_NAME"; then
        echo "Unloading module '$MODULE_NAME'..."
        rmmod "$MODULE_NAME" || echo "Failed to unload module. It might already be unloaded."
        dmesg | tail -n 5
    else
        echo "Module was not loaded."
    fi
}

# --- Main Logic ---

# Check for root privileges
if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: This script must be run as root (use sudo)."
    exit 1
fi

# Check if module file exists
if [ ! -f "$MODULE_PATH" ]; then
    echo "ERROR: Kernel module '$MODULE_PATH' not found."
    echo "Please run ./scripts/build.sh first."
    exit 1
fi

# Register cleanup function to run on exit (even on error)
trap cleanup EXIT

# Load module
echo "--- Starting Demo ---"
echo "Loading module: $MODULE_NAME"
insmod "$MODULE_PATH"
dmesg | tail -n 5

# Verify device creation
if [ ! -c "/dev/simtemp" ] || [ ! -d "$SYSFS_PATH" ]; then
    echo "FAIL: Device node or sysfs directory not created."
    exit 1
fi
echo "Device node /dev/simtemp and sysfs path found."

# Run automated test
echo
echo "Running automated CLI test..."
# Set threshold to 22C and mode to ramp to guarantee an alert
TEST_CMD="python3 ${CLI_APP} --test --set-threshold 22000 --set-mode ramp"
echo "Executing: $TEST_CMD"

if $TEST_CMD; then
    exit 0
else
    exit 1
fi
