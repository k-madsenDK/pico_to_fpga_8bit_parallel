module main (
    input  wire       clk_in,       // 100MHz oscillator
    input  wire       masterclk,    // C6
    input  wire       wr_rd,        // D6 (Mangler i din nuværende main)
    inout  wire [7:0] databus,      // C7-H11 (Skal være inout for bi-direktionel)
    output wire [7:0] led,          // De 8 onboard LEDs
    output wire       full,         // C11
    output wire       empty         // D9 (Mangler i din nuværende main)
);

    wire buffer_empty;

    // Instantiér interfacet med de præcise navne fra 8bitparalle.v
    parallel_interface bus_receiver (
        .masterclk(masterclk),      // [cite: 167]
        .wr_rd(wr_rd),              // [cite: 167]
        .databus(databus),          // Rettet fra .din [cite: 167, 180]
        .full_signal(full),         // [cite: 167]
        .empty_signal(buffer_empty), // Rettet fra .empty 
        .fpgaclk(clk_in),           // 
        .rd_en(!buffer_empty),      // 
        .dout(led)                  // 
    );

    // Forbind det interne empty-signal til den ydre pin til Picoen
    assign empty = buffer_empty;

endmodule
