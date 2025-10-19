#!/bin/bash
#==============================================================================
# Minicom FAST Protocol - Build Script
# Supports Linux and macOS
#==============================================================================

set -e  # Exit on error

echo "=== Minicom with FAST Streaming Protocol - Build Script ==="
echo ""

# Detect OS
OS="$(uname -s)"
case "$OS" in
    Linux*)     PLATFORM="Linux";;
    Darwin*)    PLATFORM="macOS";;
    *)          PLATFORM="Unknown";;
esac

echo "Platform detected: $PLATFORM"
echo ""

# Get script directory (works on both Linux and macOS)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# Setup PATH for local autopoint
export PATH="$SCRIPT_DIR/.local-tools/usr/bin:$PATH"

# Check if we need to use local autopoint or system gettext
if command -v autopoint >/dev/null 2>&1; then
    AUTOPOINT_LOC=$(which autopoint)
    echo "Found autopoint at: $AUTOPOINT_LOC"
else
    echo "ERROR: autopoint not found!"
    echo ""
    if [ "$PLATFORM" = "macOS" ]; then
        echo "On macOS, please install gettext:"
        echo "  brew install gettext"
        echo "  export PATH=\"/opt/homebrew/opt/gettext/bin:\$PATH\""
    else
        echo "The local autopoint should work. Please check .local-tools/"
    fi
    exit 1
fi

# Clean previous build artifacts
echo ""
echo "Cleaning previous build artifacts..."
make clean 2>/dev/null || true
rm -rf build autom4te.cache config.status config.log
echo "Done."
echo ""

# Run autoreconf
echo "Running autoreconf to generate build system..."
autoreconf -fi
echo "Done."
echo ""

# Configure
echo "Configuring build (prefix=$SCRIPT_DIR/build)..."
./configure --prefix="$SCRIPT_DIR/build"
echo "Done."
echo ""

# Detect number of CPU cores
if [ "$PLATFORM" = "macOS" ]; then
    NCPU=$(sysctl -n hw.ncpu)
else
    NCPU=$(nproc)
fi

# Build
echo "Building minicom with FAST protocol ($NCPU parallel jobs)..."
make -j"$NCPU"
echo "Done."
echo ""

# Install to local build directory
echo "Installing to $SCRIPT_DIR/build/..."
make install
echo "Done."
echo ""

# Show success message
echo "=== Build Complete ==="
echo ""
echo "Minicom with FAST protocol is ready!"
echo ""
echo "Binary location:"
echo "  $SCRIPT_DIR/build/bin/minicom"
echo ""
echo "To run:"
echo "  $SCRIPT_DIR/build/bin/minicom -D /dev/ttyUSB0 -b 1000000"
echo ""
echo "To upload firmware:"
echo "  1. Press Ctrl-A then S"
echo "  2. Select 'Fast' protocol"
echo "  3. Choose your firmware file"
echo ""
echo "Enjoy fast streaming at 90-104 KB/sec!"
