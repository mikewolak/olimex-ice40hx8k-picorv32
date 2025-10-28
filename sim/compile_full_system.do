# ModelSim compilation script for full system simulation

# Create work library
vlib work

# Compile SRAM model
vlog -sv sram_model.v

# Compile all HDL source files
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
vlog -sv +define+SIMULATION ../hdl/ice40_picorv32_top.v

# Compile testbench
vlog -sv tb_full_system.v

echo "Compilation complete"
