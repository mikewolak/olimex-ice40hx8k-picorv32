#!/bin/bash
#==============================================================================
# Build FPGA bitstream with optimized unified SRAM controller
#==============================================================================

set -e

cd hdl

echo "=========================================="
echo "Synthesizing with Yosys..."
echo "=========================================="

yosys -p "
    read_verilog -sv ice40_picorv32_top.v;
    read_verilog -sv sram_controller_unified.v;
    read_verilog -sv sram_unified_adapter.v;
    read_verilog -sv mem_controller.v;
    read_verilog -sv ../picorv32.v;
    read_verilog -sv uart_simple.v;
    read_verilog -sv timer_simple.v;
    read_verilog -sv boot_rom.v;
    synth_ice40 -top ice40_picorv32_top -json ice40_picorv32.json
" 2>&1 | tee synth.log

echo ""
echo "=========================================="
echo "Place and Route with nextpnr..."
echo "=========================================="

nextpnr-ice40 \
    --hx8k \
    --package ct256 \
    --json ice40_picorv32.json \
    --pcf ice40_picorv32.pcf \
    --asc ice40_picorv32.asc \
    --freq 50 \
    2>&1 | tee pnr.log

echo ""
echo "=========================================="
echo "Generating bitstream with icepack..."
echo "=========================================="

icepack ice40_picorv32.asc ice40_picorv32.bin

echo ""
echo "=========================================="
echo "Build Complete!"
echo "=========================================="
echo "Bitstream: hdl/ice40_picorv32.bin"
ls -lh ice40_picorv32.bin
echo ""
echo "To program FPGA:"
echo "  cd tools/openocd"
echo "  sudo openocd -f olimex-arm-usb-tiny-h.cfg -f ice40-hx8k.cfg -c \"init; svf ../../hdl/ice40_picorv32.svf; exit\""
