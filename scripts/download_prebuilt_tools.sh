#!/bin/bash
# Download pre-built toolchains (much faster than building)

set -e

INSTALL_DIR="build/toolchain"
OS=$(uname -s)
ARCH=$(uname -m)

echo "========================================="
echo "Downloading Pre-built Toolchains"
echo "========================================="
echo "OS:   $OS"
echo "Arch: $ARCH"
echo ""

mkdir -p "$INSTALL_DIR"
mkdir -p downloads

# Detect download tool (curl preferred for macOS, wget for Linux)
if command -v curl >/dev/null 2>&1; then
    DOWNLOAD_CMD="curl -L -o"
elif command -v wget >/dev/null 2>&1; then
    DOWNLOAD_CMD="wget -O"
else
    echo "ERROR: Neither curl nor wget found. Please install one of them."
    exit 1
fi

# ============================================================================
# RISC-V Toolchain
# ============================================================================

echo "Downloading RISC-V toolchain..."

case "$OS" in
    Linux)
        if [ "$ARCH" = "x86_64" ]; then
            # Use rv32i release (GCC 13.2.0 with rv32im support)
            # Note: rv32e releases don't support rv32im which we need
            RISCV_URL="https://github.com/stnolting/riscv-gcc-prebuilt/releases/download/rv32i-131023/riscv32-unknown-elf.gcc-13.2.0.tar.gz"
            echo "Downloading rv32i RISC-V toolchain (GCC 13.2.0)"
        else
            echo "ERROR: No pre-built RISC-V toolchain for $OS $ARCH"
            echo "Please use: make CONFIG_TOOLCHAIN_BUILD=y"
            exit 1
        fi
        ;;
    Darwin)
        echo "macOS detected - Installing RISC-V toolchain via Homebrew..."
        if ! command -v brew >/dev/null 2>&1; then
            echo ""
            echo "ERROR: Homebrew not found. Please install Homebrew first:"
            echo "  /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
            echo ""
            echo "Then run: brew install riscv-gnu-toolchain"
            exit 1
        fi

        echo "Installing riscv-gnu-toolchain (this may take a while)..."
        if ! brew list riscv-gnu-toolchain >/dev/null 2>&1; then
            brew install riscv-gnu-toolchain
        else
            echo "✓ riscv-gnu-toolchain already installed"
        fi

        # Create symlinks in our toolchain directory
        mkdir -p "$INSTALL_DIR/bin"
        BREW_PREFIX=$(brew --prefix)
        for tool in "$BREW_PREFIX"/bin/riscv*-elf-*; do
            if [ -f "$tool" ]; then
                ln -sf "$tool" "$INSTALL_DIR/bin/$(basename $tool)"
            fi
        done
        echo "✓ RISC-V toolchain symlinked from Homebrew"
        SKIP_RISCV_DOWNLOAD=1
        ;;
    *)
        echo "ERROR: Unsupported OS: $OS"
        exit 1
        ;;
esac

if [ "$SKIP_RISCV_DOWNLOAD" != "1" ]; then
    # Check if toolchain is already installed
    if [ -f "$INSTALL_DIR/bin/riscv32-unknown-elf-gcc" ] || [ -f "$INSTALL_DIR/bin/riscv64-unknown-elf-gcc" ]; then
        echo "✓ RISC-V toolchain already installed, skipping"
    else
        if [ ! -f downloads/riscv-toolchain.tar.gz ]; then
            $DOWNLOAD_CMD downloads/riscv-toolchain.tar.gz "$RISCV_URL"
        fi

        echo "Extracting RISC-V toolchain..."
        tar -xzf downloads/riscv-toolchain.tar.gz -C "$INSTALL_DIR"
        echo "✓ RISC-V toolchain extracted"
    fi
fi

# ============================================================================
# OSS CAD Suite (Yosys, NextPNR, IceStorm)
# ============================================================================

echo ""
echo "Downloading OSS CAD Suite..."

# Get latest release from GitHub API
LATEST_RELEASE=$(curl -s https://api.github.com/repos/YosysHQ/oss-cad-suite-build/releases/latest | grep '"tag_name":' | sed -E 's/.*"([^"]+)".*/\1/')

if [ -z "$LATEST_RELEASE" ]; then
    echo "ERROR: Could not fetch latest oss-cad-suite release"
    exit 1
fi

echo "Latest OSS CAD Suite release: $LATEST_RELEASE"

case "$OS" in
    Linux)
        if [ "$ARCH" = "x86_64" ]; then
            FPGA_URL="https://github.com/YosysHQ/oss-cad-suite-build/releases/download/${LATEST_RELEASE}/oss-cad-suite-linux-x64-${LATEST_RELEASE//-/}.tgz"
        else
            echo "ERROR: No pre-built OSS CAD Suite for $OS $ARCH"
            exit 1
        fi
        ;;
    Darwin)
        if [ "$ARCH" = "arm64" ]; then
            FPGA_URL="https://github.com/YosysHQ/oss-cad-suite-build/releases/download/${LATEST_RELEASE}/oss-cad-suite-darwin-arm64-${LATEST_RELEASE//-/}.tgz"
        else
            FPGA_URL="https://github.com/YosysHQ/oss-cad-suite-build/releases/download/${LATEST_RELEASE}/oss-cad-suite-darwin-x64-${LATEST_RELEASE//-/}.tgz"
        fi
        ;;
    *)
        echo "ERROR: Unsupported OS: $OS"
        exit 1
        ;;
esac

# Check if OSS CAD Suite is already installed
if [ -f downloads/oss-cad-suite/bin/yosys ] && \
   [ -f downloads/oss-cad-suite/bin/nextpnr-ice40 ] && \
   [ -f downloads/oss-cad-suite/bin/icepack ]; then
    echo "✓ OSS CAD Suite already installed, skipping"
else
    if [ ! -f downloads/oss-cad-suite.tgz ]; then
        echo "Downloading: $FPGA_URL"
        $DOWNLOAD_CMD downloads/oss-cad-suite.tgz "$FPGA_URL"
    fi

    echo "Extracting OSS CAD Suite..."
    mkdir -p downloads/oss-cad-suite
    tar -xzf downloads/oss-cad-suite.tgz -C downloads/oss-cad-suite --strip-components=1
    echo "✓ OSS CAD Suite extracted"

    # Link binaries to our toolchain dir
    ln -sf $(pwd)/downloads/oss-cad-suite/bin/* "$INSTALL_DIR/bin/" 2>/dev/null || true
fi

echo ""
echo "========================================="
echo "✓ Pre-built toolchains downloaded"
echo "========================================="
echo ""
echo "RISC-V GCC:"
"$INSTALL_DIR/bin/riscv32-unknown-elf-gcc" --version 2>/dev/null | head -1 || \
"$INSTALL_DIR/bin/riscv64-unknown-elf-gcc" --version 2>/dev/null | head -1

echo ""
echo "FPGA Tools:"
downloads/oss-cad-suite/bin/yosys -V | head -1
downloads/oss-cad-suite/bin/nextpnr-ice40 --version 2>&1 | head -1
