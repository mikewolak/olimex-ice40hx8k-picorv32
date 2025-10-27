#!/bin/bash
# Build additional libraries with -fPIC for overlay use
# Builds: incurses, microrl

set -e

if [ ! -f .config ]; then
    echo "ERROR: .config not found"
    exit 1
fi

source .config

# Derive arch/abi from config
ARCH="rv32i"
if [ "${CONFIG_ENABLE_MUL}" = "y" ] && [ "${CONFIG_ENABLE_DIV}" = "y" ]; then
    ARCH="${ARCH}m"
fi
if [ "${CONFIG_COMPRESSED_ISA}" = "y" ]; then
    ARCH="${ARCH}c"
fi
ABI="ilp32"

SYSROOT_PIC="firmware/overlay_sdk/sysroot_pic"
LIB_DIR="lib"
BUILD_DIR="firmware/overlay_sdk/build_pic"

# Compiler flags for PIC
CFLAGS="-march=$ARCH -mabi=$ABI -O2 -g -fPIC -fno-plt -fno-common -ffreestanding -fno-builtin"
CFLAGS="$CFLAGS -I$SYSROOT_PIC/riscv64-unknown-elf/include"

echo "========================================="
echo "Building Overlay Libraries with -fPIC"
echo "========================================="
echo "Architecture: $ARCH"
echo "ABI:          $ABI"
echo "Install to:   $SYSROOT_PIC"
echo ""

mkdir -p $BUILD_DIR

#==============================================================================
# Build incurses library
#==============================================================================

echo ""
echo "Building incurses..."
echo "--------------------"

riscv64-unknown-elf-gcc $CFLAGS \
    -I$LIB_DIR/incurses \
    -c $LIB_DIR/incurses/incurses.c \
    -o $BUILD_DIR/incurses.o

riscv64-unknown-elf-ar rcs $SYSROOT_PIC/riscv64-unknown-elf/lib/libincurses.a $BUILD_DIR/incurses.o

echo "✓ libincurses.a created"
ls -lh $SYSROOT_PIC/riscv64-unknown-elf/lib/libincurses.a

# Copy header
cp $LIB_DIR/incurses/curses.h $SYSROOT_PIC/riscv64-unknown-elf/include/
echo "✓ curses.h installed"

#==============================================================================
# Build microrl library
#==============================================================================

echo ""
echo "Building microrl..."
echo "-------------------"

riscv64-unknown-elf-gcc $CFLAGS \
    -I$LIB_DIR/microrl \
    -c $LIB_DIR/microrl/microrl.c \
    -o $BUILD_DIR/microrl.o

riscv64-unknown-elf-ar rcs $SYSROOT_PIC/riscv64-unknown-elf/lib/libmicrorl.a $BUILD_DIR/microrl.o

echo "✓ libmicrorl.a created"
ls -lh $SYSROOT_PIC/riscv64-unknown-elf/lib/libmicrorl.a

# Copy header
cp $LIB_DIR/microrl/microrl.h $SYSROOT_PIC/riscv64-unknown-elf/include/
echo "✓ microrl.h installed"

#==============================================================================
# Summary
#==============================================================================

echo ""
echo "========================================="
echo "✓ Overlay libraries built successfully"
echo "========================================="
echo ""
echo "Installed libraries:"
ls -lh $SYSROOT_PIC/riscv64-unknown-elf/lib/lib{incurses,microrl}.a
echo ""
echo "Installed headers:"
ls -lh $SYSROOT_PIC/riscv64-unknown-elf/include/{curses,microrl}.h
echo ""
echo "You can now build overlays with incurses/microrl support!"
echo "Link with: -lincurses -lmicrorl"
echo ""
