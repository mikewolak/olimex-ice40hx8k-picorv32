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

# ============================================================================
# RISC-V Toolchain
# ============================================================================

echo "Downloading RISC-V toolchain..."

case "$OS" in
    Linux)
        if [ "$ARCH" = "x86_64" ]; then
            RISCV_URL="https://github.com/stnolting/riscv-gcc-prebuilt/releases/download/rv32i-4.0.0/riscv32-unknown-elf.gcc-12.1.0.tar.gz"
        else
            echo "ERROR: No pre-built RISC-V toolchain for $OS $ARCH"
            echo "Please use: make CONFIG_TOOLCHAIN_BUILD=y"
            exit 1
        fi
        ;;
    Darwin)
        echo "ERROR: macOS users should use Homebrew:"
        echo "  brew install riscv-gnu-toolchain"
        exit 1
        ;;
    *)
        echo "ERROR: Unsupported OS: $OS"
        exit 1
        ;;
esac

if [ ! -f downloads/riscv-toolchain.tar.gz ]; then
    wget -O downloads/riscv-toolchain.tar.gz "$RISCV_URL"
fi

echo "Extracting RISC-V toolchain..."
tar -xzf downloads/riscv-toolchain.tar.gz -C "$INSTALL_DIR" --strip-components=1

# ============================================================================
# OSS CAD Suite (Yosys, NextPNR, IceStorm)
# ============================================================================

echo ""
echo "Downloading OSS CAD Suite..."

case "$OS" in
    Linux)
        if [ "$ARCH" = "x86_64" ]; then
            FPGA_URL="https://github.com/YosysHQ/oss-cad-suite-build/releases/download/2024-10-09/oss-cad-suite-linux-x64-20241009.tgz"
        else
            echo "ERROR: No pre-built OSS CAD Suite for $OS $ARCH"
            exit 1
        fi
        ;;
    Darwin)
        if [ "$ARCH" = "arm64" ]; then
            FPGA_URL="https://github.com/YosysHQ/oss-cad-suite-build/releases/download/2024-10-09/oss-cad-suite-darwin-arm64-20241009.tgz"
        else
            FPGA_URL="https://github.com/YosysHQ/oss-cad-suite-build/releases/download/2024-10-09/oss-cad-suite-darwin-x64-20241009.tgz"
        fi
        ;;
    *)
        echo "ERROR: Unsupported OS: $OS"
        exit 1
        ;;
esac

if [ ! -f downloads/oss-cad-suite.tgz ]; then
    wget -O downloads/oss-cad-suite.tgz "$FPGA_URL"
fi

echo "Extracting OSS CAD Suite..."
mkdir -p downloads/oss-cad-suite
tar -xzf downloads/oss-cad-suite.tgz -C downloads/oss-cad-suite --strip-components=1

# Link binaries to our toolchain dir
ln -sf $(pwd)/downloads/oss-cad-suite/bin/* "$INSTALL_DIR/bin/" 2>/dev/null || true

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
