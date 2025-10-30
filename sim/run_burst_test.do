# Complete SPI Burst Mode Verification

# Create work library
vlib work

# Compile modules
vlog -sv ../hdl/spi_master.v
vlog -sv spi_burst_full_tb.v

# Run simulation
vsim -c work.spi_burst_full_tb
run -all
quit
