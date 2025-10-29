#===============================================================================
# ModelSim Simulation Makefile for RV32IMC PicoRV32 System
#
# Copyright (c) October 2025 Michael Wolak
# Email: mikewolak@gmail.com, mike@epromfoundry.com
#===============================================================================

.PHONY: all compile sim clean help led_blink

# Default target
all: help

# Compile the design
compile:
	@echo "========================================="
	@echo "Compiling HDL for ModelSim (RV32IMC)"
	@echo "========================================="
	vsim -c -do compile_full_system.do -do quit

# Run LED blink simulation
led_blink: compile build-led-blink
	@echo ""
	@echo "========================================="
	@echo "Running LED Blink Simulation (RV32IMC)"
	@echo "========================================="
	@echo "Firmware: ../firmware/led_blink.hex"
	@echo "Architecture: RV32IMC (compressed ISA)"
	@echo ""
	vsim -c -do run_led_blink.do

# Build led_blink firmware with RV32IMC
build-led-blink:
	@echo "Building led_blink firmware with RV32IMC..."
	@cd ../firmware && $(MAKE) TARGET=led_blink USE_NEWLIB=0 single-target
	@echo "Generating hex file for simulation..."
	@riscv64-unknown-elf-objcopy -O verilog ../firmware/led_blink.elf ../firmware/led_blink.hex
	@echo "✓ Firmware ready: ../firmware/led_blink.hex"
	@ls -lh ../firmware/led_blink.{bin,hex}

# Run baseline test (minimal)
baseline: compile
	@echo "Running baseline test..."
	vsim -c -do run_baseline.do

# Run full system test
full: compile
	@echo "Running full system test..."
	vsim -c -do run_full_system.do

# Interactive simulation (GUI)
gui: compile build-led-blink
	@echo "Starting ModelSim GUI..."
	vsim -do run_led_blink.do

# Clean simulation files
clean:
	@echo "Cleaning simulation files..."
	@rm -rf work
	@rm -f transcript
	@rm -f *.wlf
	@rm -f *.vcd
	@rm -f vsim.dbg
	@echo "✓ Clean complete"

# Help
help:
	@echo "========================================="
	@echo "ModelSim Simulation - RV32IMC System"
	@echo "========================================="
	@echo ""
	@echo "Targets:"
	@echo "  make compile       - Compile HDL design"
	@echo "  make led_blink     - Build firmware and run LED blink test"
	@echo "  make gui           - Run simulation in ModelSim GUI"
	@echo "  make baseline      - Run minimal baseline test"
	@echo "  make full          - Run full system test"
	@echo "  make clean         - Remove simulation artifacts"
	@echo "  make help          - Show this help"
	@echo ""
	@echo "Current Configuration:"
	@echo "  Architecture: RV32IMC (compressed instructions)"
	@echo "  Clock: 50 MHz (divided from 100 MHz)"
	@echo "  Firmware: led_blink (bare metal)"
	@echo ""
	@echo "Requirements:"
	@echo "  - ModelSim/QuestaSim"
	@echo "  - RISC-V toolchain (riscv64-unknown-elf-gcc)"
	@echo ""
	@echo "Quick start:"
	@echo "  make led_blink     # Complete simulation"
	@echo ""
