//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// uart_peripheral.v - UART MMIO Peripheral
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//
// NOT FOR COMMERCIAL USE
// Educational and research purposes only
//==============================================================================

module uart_peripheral (
    input wire clk,
    input wire resetn,

    // MMIO Interface
    input wire        mmio_valid,
    input wire        mmio_write,
    input wire [31:0] mmio_addr,
    input wire [31:0] mmio_wdata,
    input wire [ 3:0] mmio_wstrb,
    output reg [31:0] mmio_rdata,
    output reg        mmio_ready,

    // UART TX Interface
    output reg [ 7:0] uart_tx_data,
    output reg        uart_tx_valid,
    input wire        uart_tx_busy,

    // UART RX Interface (circular buffer)
    input wire [ 7:0] uart_rx_data,
    output reg        uart_rx_rd_en,
    input wire        uart_rx_empty
);

    // Memory Map
    localparam ADDR_UART_TX_DATA   = 32'h80000000;
    localparam ADDR_UART_TX_STATUS = 32'h80000004;
    localparam ADDR_UART_RX_DATA   = 32'h80000008;
    localparam ADDR_UART_RX_STATUS = 32'h8000000C;

    always @(posedge clk) begin
        if (!resetn) begin
            mmio_rdata <= 32'h0;
            mmio_ready <= 1'b0;
            uart_tx_data <= 8'h0;
            uart_tx_valid <= 1'b0;
            uart_rx_rd_en <= 1'b0;
        end else begin
            // Default: clear control signals
            mmio_ready <= 1'b0;
            uart_tx_valid <= 1'b0;
            uart_rx_rd_en <= 1'b0;

            if (mmio_valid && !mmio_ready) begin
                if (mmio_write) begin
                    // ============ WRITE OPERATIONS ============
                    case (mmio_addr)
                        ADDR_UART_TX_DATA: begin
                            // Write to UART TX
                            if (!uart_tx_busy) begin
                                uart_tx_data <= mmio_wdata[7:0];
                                uart_tx_valid <= 1'b1;
                                mmio_ready <= 1'b1;

                                // synthesis translate_off
                                $display("[UART] TX: 0x%02x ('%c')",
                                         mmio_wdata[7:0],
                                         (mmio_wdata[7:0] >= 32 && mmio_wdata[7:0] < 127) ? mmio_wdata[7:0] : 8'h2E);
                                // synthesis translate_on
                            end
                            // If busy, don't ack - CPU must retry
                        end

                        default: begin
                            // Write to invalid register - ignore
                            mmio_ready <= 1'b1;
                        end
                    endcase

                end else begin
                    // ============ READ OPERATIONS ============
                    case (mmio_addr)
                        ADDR_UART_TX_STATUS: begin
                            // Read UART TX status
                            mmio_rdata <= {31'h0, uart_tx_busy};
                            mmio_ready <= 1'b1;
                        end

                        ADDR_UART_RX_DATA: begin
                            // Read from UART RX buffer
                            if (!uart_rx_empty) begin
                                mmio_rdata <= {24'h0, uart_rx_data};
                                uart_rx_rd_en <= 1'b1;  // Advance buffer pointer
                                mmio_ready <= 1'b1;

                                // synthesis translate_off
                                $display("[UART] RX: 0x%02x ('%c')",
                                         uart_rx_data,
                                         (uart_rx_data >= 32 && uart_rx_data < 127) ? uart_rx_data : 8'h2E);
                                // synthesis translate_on
                            end else begin
                                // Buffer empty - return 0
                                mmio_rdata <= 32'h0;
                                mmio_ready <= 1'b1;
                            end
                        end

                        ADDR_UART_RX_STATUS: begin
                            // Read UART RX status
                            mmio_rdata <= {31'h0, ~uart_rx_empty};
                            mmio_ready <= 1'b1;

                            // synthesis translate_off
                            $display("[UART] RX STATUS: empty=%b, returning %b", uart_rx_empty, ~uart_rx_empty);
                            // synthesis translate_on
                        end

                        default: begin
                            // Read from invalid register - return 0
                            mmio_rdata <= 32'h0;
                            mmio_ready <= 1'b1;
                        end
                    endcase
                end
            end
        end
    end

endmodule
