// 8bitparallel.v - Optimeret til Block RAM uden special-attributter
module parallel_interface (
    input  wire       masterclk,    // C6
    input  wire       wr_rd,        // D6
    inout  wire [7:0] databus,      // C7-H11

    output wire       full_signal,  // C11
    output wire       empty_signal, // D9

    input  wire       fpgaclk,
    input  wire       rd_en,        
    output reg  [7:0] dout          
);

    // Hukommelse - 512 bytes
    reg [7:0] mem [0:511]; 
    reg [8:0] w_ptr = 0;
    reg [8:0] r_ptr = 0;
    
    // Synkront register til Master (Pico) læsning
    reg [7:0] ram_data_out;

    // --- TRI-STATE LOGIK ---
    // Advarslen om tri-state i loggen er normal for iCE40
    assign databus = (wr_rd) ? ram_data_out : 8'bz;

    // --- PORT A: MASTER INTERFACE ---
    // Alt der rører 'mem' på masterclk samles her
    always @(posedge masterclk) begin
        if (!wr_rd) begin
            mem[w_ptr] <= databus;
            w_ptr <= w_ptr + 1;
        end
        // Denne linje er nøglen til BRAM-inference
        ram_data_out <= mem[r_ptr];
    end

    // Pointer-skift på negedge (som ønsket)
    always @(negedge masterclk) begin
        if (wr_rd && !empty_signal) begin
            r_ptr <= r_ptr + 1;
        end
    end

    // --- PORT B: INTERN MONITOR (LEDs) ---
    // Vi bruger Port B til at føde dout, så vi ikke overskrider 2 porte
    always @(posedge fpgaclk) begin
        if (rd_en) begin
            dout <= ram_data_out;
        end
    end
    

    // --- STATUS ---
    assign empty_signal = (w_ptr == r_ptr);
    assign full_signal  = ((w_ptr + 9'd32) == r_ptr);

endmodule
