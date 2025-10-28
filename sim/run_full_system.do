# ModelSim simulation script for full system

# Load the design
vsim -t 1ns work.tb_full_system

# Add signals to waveform
add wave -divider "Top Level"
add wave /tb_full_system/EXTCLK
add wave /tb_full_system/uut/clk
add wave /tb_full_system/uut/cpu_resetn
add wave /tb_full_system/LED1
add wave /tb_full_system/LED2
add wave /tb_full_system/UART_TX

add wave -divider "CPU Memory Interface"
add wave /tb_full_system/uut/cpu_mem_valid
add wave /tb_full_system/uut/cpu_mem_ready
add wave -hex /tb_full_system/uut/cpu_mem_addr
add wave -hex /tb_full_system/uut/cpu_mem_wdata
add wave -hex /tb_full_system/uut/cpu_mem_rdata
add wave -hex /tb_full_system/uut/cpu_mem_wstrb

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
