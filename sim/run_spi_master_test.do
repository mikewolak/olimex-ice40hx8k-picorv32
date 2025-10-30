# ModelSim simulation script for SPI Master test

# Load the testbench
vsim -t 1ns work.spi_master_tb

# Add signals to waveform
add wave -divider "Test Control"
add wave -decimal /spi_master_tb/test_count
add wave -decimal /spi_master_tb/test_passed
add wave -decimal /spi_master_tb/test_failed

add wave -divider "Clock and Reset"
add wave /spi_master_tb/clk
add wave /spi_master_tb/resetn

add wave -divider "MMIO Interface"
add wave /spi_master_tb/mmio_valid
add wave /spi_master_tb/mmio_ready
add wave /spi_master_tb/mmio_write
add wave -hex /spi_master_tb/mmio_addr
add wave -hex /spi_master_tb/mmio_wdata
add wave -hex /spi_master_tb/mmio_rdata
add wave -hex /spi_master_tb/mmio_wstrb

add wave -divider "SPI Physical Interface"
add wave /spi_master_tb/spi_cs
add wave /spi_master_tb/spi_sck
add wave /spi_master_tb/spi_mosi
add wave /spi_master_tb/spi_miso
add wave /spi_master_tb/spi_irq

add wave -divider "SPI Master Internal State"
add wave -hex /spi_master_tb/dut/state
add wave /spi_master_tb/dut/busy
add wave /spi_master_tb/dut/tx_valid
add wave -hex /spi_master_tb/dut/tx_data
add wave -hex /spi_master_tb/dut/rx_data
add wave -decimal /spi_master_tb/dut/bit_count

add wave -divider "Burst Mode Registers"
add wave /spi_master_tb/dut/burst_mode
add wave -decimal /spi_master_tb/dut/burst_count
add wave /spi_master_tb/dut/irq_pulse

add wave -divider "Clock Control"
add wave /spi_master_tb/dut/cpol
add wave /spi_master_tb/dut/cpha
add wave -decimal /spi_master_tb/dut/clk_div
add wave /spi_master_tb/dut/spi_clk_en
add wave -decimal /spi_master_tb/dut/clk_counter

# Run simulation
run -all
