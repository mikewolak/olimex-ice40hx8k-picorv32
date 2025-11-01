# ModelSim run script for SD bootloader test

# Load the design
vsim -t 1ns work.tb_sd_bootloader

# Add signals to waveform
add wave -divider "Top Level"
add wave /tb_sd_bootloader/EXTCLK
add wave /tb_sd_bootloader/uut/clk
add wave /tb_sd_bootloader/uut/cpu_resetn
add wave /tb_sd_bootloader/LED1
add wave /tb_sd_bootloader/LED2

add wave -divider "UART"
add wave /tb_sd_bootloader/UART_TX
add wave /tb_sd_bootloader/uart_byte_count

add wave -divider "SPI / SD Card"
add wave /tb_sd_bootloader/SPI_CS
add wave /tb_sd_bootloader/SPI_SCK
add wave /tb_sd_bootloader/SPI_MOSI
add wave /tb_sd_bootloader/SPI_MISO
add wave -hex /tb_sd_bootloader/sd_state
add wave -hex /tb_sd_bootloader/spi_cmd_buffer

add wave -divider "CPU Instruction Fetch"
add wave /tb_sd_bootloader/uut/cpu_mem_valid
add wave /tb_sd_bootloader/uut/cpu_mem_ready
add wave -hex /tb_sd_bootloader/uut/cpu_mem_addr
add wave -hex /tb_sd_bootloader/uut/cpu_mem_rdata
add wave -hex /tb_sd_bootloader/uut/cpu_mem_wdata
add wave -hex /tb_sd_bootloader/uut/cpu_mem_wstrb

add wave -divider "Bootloader ROM"
add wave /tb_sd_bootloader/uut/bootrom_enable
add wave -hex /tb_sd_bootloader/uut/bootrom_addr
add wave -hex /tb_sd_bootloader/uut/bootrom_rdata

add wave -divider "SRAM Controller"
add wave /tb_sd_bootloader/uut/sram_unified/start
add wave /tb_sd_bootloader/uut/sram_unified/busy
add wave /tb_sd_bootloader/uut/sram_unified/done
add wave -hex /tb_sd_bootloader/uut/sram_unified/unified_ctrl/state

add wave -divider "SRAM Physical"
add wave -hex /tb_sd_bootloader/SA
add wave -hex /tb_sd_bootloader/SD
add wave /tb_sd_bootloader/SRAM_CS_N
add wave /tb_sd_bootloader/SRAM_OE_N
add wave /tb_sd_bootloader/SRAM_WE_N

# Run simulation
run -all
