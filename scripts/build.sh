#!/bin/bash

# build.sh - Builds the kernel module and user-space application.
# Accepts arguments like: clean, debug, build32

set -e

# --- Configuration ---
KERNEL_DIR_DEFAULT="/lib/modules/$(uname -r)/build"
KERNEL_DIR="${KDIR:-$KERNEL_DIR_DEFAULT}"
MODULE_SUBDIR="../kernel"
CLI_SUBDIR="../user/cli"

# --- Argument Parsing ---
MAKE_ARGS=""
TARGET_INFO="native 64-bit"

# Handle 'clean' as a special case that exits immediately.
# This allows 'clean' to be used with other arguments without side effects.
if [[ " $@ " =~ " clean " ]]; then
    echo "--- Cleaning kernel module artifacts in '$MODULE_SUBDIR/'... ---"
    make -C "$MODULE_SUBDIR" KDIR="$KERNEL_DIR" clean
    echo "--- Clean finished ---"
    exit 0
fi

# Parse arguments to build the final make command.
for arg in "$@"; do
  case "$arg" in
    build32)
      MAKE_ARGS+=" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-"
      TARGET_INFO="32-bit ARM"
      ;;
    debug)
      # This flag will be appended to the make command.
      # The kernel's Makefile uses it to enable debug symbols.
      MAKE_ARGS+=" EXTRA_CFLAGS=-DDEBUG"
      ;;
    *)
      echo "Error: Invalid argument '$arg'." >&2
      echo "Usage: $0 [build32] [debug] [clean]" >&2
      exit 1
      ;;
  esac
done

# --- Main Logic ---
echo "--- Starting build script for nxp_simtemp ---"

# Check for kernel headers
if [ ! -d "$KERNEL_DIR" ]; then
    echo "ERROR: Kernel headers not found."
    echo "Please specify KDIR or install headers for kernel $(uname -r)."
    echo "On Debian/Ubuntu: sudo apt install linux-headers-$(uname -r)"
    exit 1
fi
echo "Using kernel headers from: $KERNEL_DIR"
echo

# Build the kernel module
BUILD_TYPE="for $TARGET_INFO"
if [[ "$MAKE_ARGS" == *"DDEBUG"* ]]; then
    BUILD_TYPE+=" (with debug flags)"
fi
echo "Building kernel module in '$MODULE_SUBDIR/' $BUILD_TYPE..."

# Add notes for cross-compilation
if [[ "$TARGET_INFO" == "32-bit ARM" ]]; then
    echo "NOTE: This requires a 32-bit ARM cross-compiler (e.g., arm-linux-gnueabihf-)"
    echo "      and you must provide the path to the 32-bit ARM kernel headers via KDIR."
    if [ "$KERNEL_DIR" == "$KERNEL_DIR_DEFAULT" ]; then
        echo "Warning: KDIR is not set. The default 64-bit x86 headers will fail."
    fi
fi

# The make command is executed with all collected arguments.
make -C "$MODULE_SUBDIR" KDIR="$KERNEL_DIR" $MAKE_ARGS

echo "Kernel module built successfully: $MODULE_SUBDIR/nxp_simtemp.ko"

# Check user-space app (Python doesn't need building)
echo
echo "Checking user-space application in '$CLI_SUBDIR/'..."
if [ ! -f "$CLI_SUBDIR/main.py" ]; then
    echo "ERROR: CLI application '$CLI_SUBDIR/main.py' not found."
    exit 1
fi
echo "User-space application found."

# Final success message
echo
echo "--- Build finished successfully ---"
