#!/bin/bash
set -e

COMMAND_NAME="simtemp-cli"
TARGET_BIN="/usr/local/bin/${COMMAND_NAME}"

# --- Check for sudo ---
if [ "$(id -u)" -ne 0 ]; then
  echo "This script must be run with sudo." >&2
  echo "Usage: sudo ./scripts/uninstall.sh"
  exit 1
fi

echo "Uninstalling '${COMMAND_NAME}' from ${TARGET_BIN}..."

# Check if the file exists and is a link
if [ -L "${TARGET_BIN}" ]; then
    # Remove the symbolic link
    #    -f, --force: ignore nonexistent files, never prompt
    rm -f "${TARGET_BIN}"
    echo "Symbolic link removed."
else
    echo "Warning: '${TARGET_BIN}' does not exist or is not a symbolic link. Nothing to do."
fi

echo "Uninstall complete."