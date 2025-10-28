# ModelSim simulation script for baseline (sram_proc_new)

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

add wave -divider "SRAM Processor (Baseline)"
add wave /tb_full_system/uut/sram_ctrl/start
add wave /tb_full_system/uut/sram_ctrl/busy
add wave /tb_full_system/uut/sram_ctrl/done
add wave -hex /tb_full_system/uut/sram_ctrl/state

add wave -divider "SRAM Physical"
add wave -hex /tb_full_system/SA
add wave -hex /tb_full_system/SD
add wave /tb_full_system/SRAM_CS_N
add wave /tb_full_system/SRAM_OE_N
add wave /tb_full_system/SRAM_WE_N

# Run simulation
run -all
