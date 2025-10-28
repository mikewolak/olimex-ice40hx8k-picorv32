# ModelSim simulation script for LED blink firmware test

# Load the design with firmware loading enabled
vsim -t 1ns +FIRMWARE work.tb_full_system

# Add signals to waveform
add wave -divider "Top Level"
add wave /tb_full_system/EXTCLK
add wave /tb_full_system/uut/clk
add wave /tb_full_system/uut/cpu_resetn
add wave /tb_full_system/LED1
add wave /tb_full_system/LED2
add wave /tb_full_system/UART_TX

add wave -divider "CPU State"
add wave -hex /tb_full_system/uut/cpu/pcpi_valid
add wave -hex /tb_full_system/uut/cpu/pcpi_insn
add wave -hex /tb_full_system/uut/cpu/pcpi_rs1
add wave -hex /tb_full_system/uut/cpu/pcpi_rs2
add wave -hex /tb_full_system/uut/cpu/mem_valid
add wave -hex /tb_full_system/uut/cpu/mem_addr
add wave -hex /tb_full_system/uut/cpu/mem_wdata
add wave -hex /tb_full_system/uut/cpu/mem_rdata

add wave -divider "CPU Memory Interface"
add wave /tb_full_system/uut/cpu_mem_valid
add wave /tb_full_system/uut/cpu_mem_ready
add wave -hex /tb_full_system/uut/cpu_mem_addr
add wave -hex /tb_full_system/uut/cpu_mem_wdata
add wave -hex /tb_full_system/uut/cpu_mem_rdata
add wave -hex /tb_full_system/uut/cpu_mem_wstrb

add wave -divider "MMIO LED Control"
add wave /tb_full_system/uut/mmio_valid
add wave /tb_full_system/uut/mmio_ready
add wave -hex /tb_full_system/uut/mmio_addr
add wave -hex /tb_full_system/uut/mmio_wdata
add wave /tb_full_system/uut/mmio_write

add wave -divider "SRAM Unified Adapter"
add wave /tb_full_system/uut/sram_unified/start
add wave /tb_full_system/uut/sram_unified/busy
add wave /tb_full_system/uut/sram_unified/done

add wave -divider "SRAM Controller (Unified)"
add wave /tb_full_system/uut/sram_unified/unified_ctrl/valid
add wave /tb_full_system/uut/sram_unified/unified_ctrl/ready
add wave -hex /tb_full_system/uut/sram_unified/unified_ctrl/addr
add wave -hex /tb_full_system/uut/sram_unified/unified_ctrl/wdata
add wave -hex /tb_full_system/uut/sram_unified/unified_ctrl/rdata
add wave -hex /tb_full_system/uut/sram_unified/unified_ctrl/wstrb
add wave -hex /tb_full_system/uut/sram_unified/unified_ctrl/state

add wave -divider "SRAM Physical"
add wave -hex /tb_full_system/SA
add wave -hex /tb_full_system/SD
add wave /tb_full_system/SRAM_CS_N
add wave /tb_full_system/SRAM_OE_N
add wave /tb_full_system/SRAM_WE_N

# Run simulation
run -all
