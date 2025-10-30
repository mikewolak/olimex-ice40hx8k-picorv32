# ModelSim simulation script for SPI IRQ testbench
# Comprehensive interrupt verification

# Create work library
vlib work

# Compile source files
vlog -sv spi_master.v
vlog -sv spi_irq_test_tb.v

# Simulate
vsim -voptargs=+acc work.spi_irq_test_tb

# Add waves
add wave -divider "Clock and Reset"
add wave sim:/spi_irq_test_tb/clk
add wave sim:/spi_irq_test_tb/resetn

add wave -divider "SPI IRQ Signal"
add wave sim:/spi_irq_test_tb/spi_irq
add wave sim:/spi_irq_test_tb/spi_irq_prev
add wave -decimal sim:/spi_irq_test_tb/irq_count
add wave -decimal sim:/spi_irq_test_tb/irq_width_cycles
add wave sim:/spi_irq_test_tb/irq_active

add wave -divider "DUT Internal - IRQ Generation"
add wave sim:/spi_irq_test_tb/dut/irq_pulse
add wave sim:/spi_irq_test_tb/dut/state
add wave sim:/spi_irq_test_tb/dut/dma_state
add wave sim:/spi_irq_test_tb/dut/dma_active
add wave sim:/spi_irq_test_tb/dut/dma_irq_en
add wave sim:/spi_irq_test_tb/dut/burst_mode
add wave -decimal sim:/spi_irq_test_tb/dut/burst_count

add wave -divider "MMIO Interface"
add wave sim:/spi_irq_test_tb/mmio_valid
add wave sim:/spi_irq_test_tb/mmio_write
add wave -hex sim:/spi_irq_test_tb/mmio_addr
add wave -hex sim:/spi_irq_test_tb/mmio_wdata
add wave -hex sim:/spi_irq_test_tb/mmio_rdata
add wave sim:/spi_irq_test_tb/mmio_ready

add wave -divider "Test Status"
add wave -decimal sim:/spi_irq_test_tb/test_num
add wave -decimal sim:/spi_irq_test_tb/tests_passed
add wave -decimal sim:/spi_irq_test_tb/tests_failed

# Run simulation
run -all

# Save waveform (optional)
# write format wave -window .main_pane.wave.interior.cs.body.pw.wf spi_irq_test.do

quit
