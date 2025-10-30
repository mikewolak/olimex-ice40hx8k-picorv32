# ModelSim compilation script for SPI Master testbench

# Create work library
vlib work

# Compile SPI Master module
vlog -sv ../hdl/spi_master.v

# Compile testbench
vlog -sv spi_master_tb.v

echo "Compilation complete"
