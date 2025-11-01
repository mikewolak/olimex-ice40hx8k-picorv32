# ModelSim compilation script for SD bootloader test

# Create work library
vlib work

# Compile HDL source files (same order as compile_full_system.do)
vlog -sv sram_model.v
vlog -sv ../hdl/circular_buffer.v
vlog -sv ../hdl/uart.v
vlog -sv ../hdl/uart_peripheral.v
vlog -sv ../hdl/timer_peripheral.v
vlog -sv ../hdl/spi_master.v
vlog -sv ../hdl/mmio_peripherals.v
vlog -sv ../hdl/bootloader_rom.v
vlog -sv ../hdl/picorv32.v
vlog -sv ../hdl/mem_controller.v
vlog -sv ../hdl/sram_controller_unified.v
vlog -sv ../hdl/sram_unified_adapter.v
vlog -sv +define+SIMULATION +define+BOOTLOADER_SIM ../hdl/ice40_picorv32_top.v

# Compile testbench
vlog -sv tb_sd_bootloader.v

echo "Compilation complete"
