#!/bin/bash
set -e

COMMAND_NAME="simtemp-cli"
TARGET_BIN="/usr/local/bin/${COMMAND_NAME}"

# Get the absolute path to the directory containing this script
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
# Get the absolute path to main.py
SOURCE_FILE="${SCRIPT_DIR}/../user/cli/main.py"

# --- Check for sudo ---
if [ "$(id -u)" -ne 0 ]; then
  echo "This script must be run with sudo." >&2
  echo "Usage: sudo ./scripts/install.sh"
  exit 1
fi

echo "Installing '${COMMAND_NAME}' to ${TARGET_BIN}..."

# Make the Python script executable
echo "Adding execute permissions to main.py..."
chmod +x "${SOURCE_FILE}"

# Create the symbolic link
#    -f, --force: remove existing destination files
#    -s, --symbolic: make symbolic links instead of hard links
echo "Creating symbolic link..."
ln -sf "${SOURCE_FILE}" "${TARGET_BIN}"

echo ""
echo "Install complete!"
echo "You can now run '${COMMAND_NAME}' from anywhere in your terminal."