#!/bin/bash
#==============================================================================
# Long-running SD Bootloader Simulation with Status Updates
# Runs for up to 2 hours with status every 1 minute
#==============================================================================

LOG_FILE="/tmp/sd_bootloader_long_sim.log"
STATUS_FILE="/tmp/sd_sim_status.txt"

echo "Starting long SD bootloader simulation..."
echo "Log file: $LOG_FILE"
echo "Status updates every 60 seconds"
echo ""

# Start simulation in background
cd /home/mwolak/olimex-ice40hx8k-picorv32/sim

# Run simulation with 2 hour timeout (7200000 ms)
(
    /home/mwolak/intelFPGA_lite/20.1/modelsim_ase/bin/vsim -c -do "
        do compile_sd_bootloader.do;
        do run_sd_bootloader.do;

        # Run for 2 hours of simulation time (2 hours = 7,200,000,000,000 ns)
        # But use smaller chunks so we can monitor progress

        set sim_time 0
        set increment 60000000000
        set max_time 7200000000000

        while {\$sim_time < \$max_time} {
            run 60s
            set sim_time [expr \$sim_time + \$increment]
            echo \"=== STATUS: Simulated [expr \$sim_time / 1000000000] seconds ===\"
        }

        quit
    " 2>&1 | tee "$LOG_FILE"
) &

SIM_PID=$!

echo "Simulation started with PID: $SIM_PID"
echo ""

# Monitor progress every 60 seconds
START_TIME=$(date +%s)
while kill -0 $SIM_PID 2>/dev/null; do
    sleep 60

    ELAPSED=$(($(date +%s) - START_TIME))
    ELAPSED_MIN=$((ELAPSED / 60))

    # Extract latest status from log
    UART_COUNT=$(grep -c "UART_WRITE" "$LOG_FILE" 2>/dev/null || echo "0")
    SPI_COUNT=$(grep -c "SPI_WRITE" "$LOG_FILE" 2>/dev/null || echo "0")
    SD_CMDS=$(grep -c "SD_CARD.*CMD" "$LOG_FILE" 2>/dev/null || echo "0")
    SRAM_WRITES=$(grep -c "SRAM_MODEL.*WRITE.*addr=0x00" "$LOG_FILE" 2>/dev/null || echo "0")

    echo "[$ELAPSED_MIN min] UART: $UART_COUNT | SPI: $SPI_COUNT | SD CMDs: $SD_CMDS | SRAM writes to 0x00xxx: $SRAM_WRITES"

    # Check if stuck
    LAST_LOG=$(tail -5 "$LOG_FILE" 2>/dev/null)
    if echo "$LAST_LOG" | grep -q "ERROR\|TIMEOUT\|STUCK"; then
        echo "!!! PROBLEM DETECTED - Check log file"
    fi
done

wait $SIM_PID
EXIT_CODE=$?

TOTAL_TIME=$(($(date +%s) - START_TIME))
TOTAL_MIN=$((TOTAL_TIME / 60))

echo ""
echo "Simulation completed in $TOTAL_MIN minutes"
echo "Exit code: $EXIT_CODE"
echo ""
echo "Final statistics:"
grep "Test Complete\|UART bytes\|SD.*sectors" "$LOG_FILE" | tail -10

exit $EXIT_CODE
