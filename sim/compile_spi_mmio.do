# ModelSim compilation script for SPI MMIO integration test

# Create work library
vlib work

# Compile SPI Master module
vlog -sv ../hdl/spi_master.v

# Compile MMIO testbench
vlog -sv spi_mmio_tb.v

echo "Compilation complete"
