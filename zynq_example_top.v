`timescale 1ns / 1ps
module zynq_example_top(
    inout  [14:0] DDR_addr,
    inout   [2:0] DDR_ba,
    inout         DDR_cas_n,
    inout         DDR_ck_n,
    inout         DDR_ck_p,
    inout         DDR_cke,
    inout         DDR_cs_n,
    inout   [3:0] DDR_dm,
    inout  [31:0] DDR_dq,
    inout   [3:0] DDR_dqs_n,
    inout   [3:0] DDR_dqs_p,
    inout         DDR_odt,
    inout         DDR_ras_n,
    inout         DDR_reset_n,
    inout         DDR_we_n,
    inout         FIXED_IO_ddr_vrn,
    inout         FIXED_IO_ddr_vrp,
    inout  [53:0] FIXED_IO_mio,
    inout         FIXED_IO_ps_clk,
    inout         FIXED_IO_ps_porb,
    inout         FIXED_IO_ps_srstb,
    output  [7:0] leds_8bits_tri_o,
    input   [7:0] sws_8bits_tri_i,
    inout         iic_rtl_0_scl_io,
    inout         iic_rtl_0_sda_io,
    inout         iic_rtl_1_scl_io,
    inout         iic_rtl_1_sda_io,
    inout         iic_jb_scl_io,
    inout         iic_jb_sda_io,
    output        hdmi_tx_clk,
    output [15:0] hdmi_tx_d,
    output        hdmi_tx_de,
    output        hdmi_tx_hsync,
    output        hdmi_tx_vsync
);
    wire        clk_ps;
    wire [0:0]  rstn_vec;
    wire        rst      = ~rstn_vec[0];
    wire        clk_fast_w;
    wire        clk_slow_w;
    wire [0:0]  pl_enable_vec;
    wire        pl_enable = pl_enable_vec[0];
    wire clk_sel = sws_8bits_tri_i[1];
    wire clk_en  = pl_enable | rst;
    wire clk_pl;
    BUFGCTRL u_bufgctrl_clkpl(
        .I0(clk_fast_w),.I1(clk_slow_w),
        .S0(~clk_sel),.S1(clk_sel),
        .CE0(clk_en),.CE1(clk_en),
        .IGNORE0(1'b0),.IGNORE1(1'b0),
        .O(clk_pl)
    );
    wire [31:0] BRAM_addr;
    wire        BRAM_clk;
    wire [31:0] BRAM_din;
    wire [31:0] BRAM_dout;
    wire        BRAM_en;
    wire        BRAM_rst;
    wire  [3:0] BRAM_we;
    fibonacci_bram #(.N_FIB(16),.BRAM_AW(32),.BRAM_DW(32)) u_fib_bram(
        .clk(clk_pl),.rst(rst),
        .xadc_temp_raw(16'b0),.xadc_eoc(1'b0),
        .BRAM_addr(BRAM_addr),.BRAM_clk(BRAM_clk),
        .BRAM_din(BRAM_din),.BRAM_dout(BRAM_dout),
        .BRAM_en(BRAM_en),.BRAM_rst(BRAM_rst),.BRAM_we(BRAM_we)
    );
    design_1_wrapper u_bd(
        .BRAM_PORTB_0_addr(BRAM_addr),.BRAM_PORTB_0_clk(BRAM_clk),
        .BRAM_PORTB_0_din(BRAM_din),.BRAM_PORTB_0_dout(BRAM_dout),
        .BRAM_PORTB_0_en(BRAM_en),.BRAM_PORTB_0_rst(BRAM_rst),.BRAM_PORTB_0_we(BRAM_we),
        .DDR_addr(DDR_addr),.DDR_ba(DDR_ba),.DDR_cas_n(DDR_cas_n),
        .DDR_ck_n(DDR_ck_n),.DDR_ck_p(DDR_ck_p),.DDR_cke(DDR_cke),
        .DDR_cs_n(DDR_cs_n),.DDR_dm(DDR_dm),.DDR_dq(DDR_dq),
        .DDR_dqs_n(DDR_dqs_n),.DDR_dqs_p(DDR_dqs_p),.DDR_odt(DDR_odt),
        .DDR_ras_n(DDR_ras_n),.DDR_reset_n(DDR_reset_n),.DDR_we_n(DDR_we_n),
        .FIXED_IO_ddr_vrn(FIXED_IO_ddr_vrn),.FIXED_IO_ddr_vrp(FIXED_IO_ddr_vrp),
        .FIXED_IO_mio(FIXED_IO_mio),.FIXED_IO_ps_clk(FIXED_IO_ps_clk),
        .FIXED_IO_ps_porb(FIXED_IO_ps_porb),.FIXED_IO_ps_srstb(FIXED_IO_ps_srstb),
        .FCLK_CLK0(clk_ps),.clk_fast(clk_fast_w),.clk_slow(clk_slow_w),
        .peripheral_aresetn(rstn_vec),
        .leds_8bits_tri_o(leds_8bits_tri_o),.sws_8bits_tri_i(sws_8bits_tri_i),
        .iic_rtl_0_scl_io(iic_rtl_0_scl_io),.iic_rtl_0_sda_io(iic_rtl_0_sda_io),
        .iic_rtl_1_scl_io(iic_rtl_1_scl_io),.iic_rtl_1_sda_io(iic_rtl_1_sda_io),
        .iic_jb_scl_io(iic_jb_scl_io),
        .iic_jb_sda_io(iic_jb_sda_io),
        .pl_enable(pl_enable_vec),
        .hdmi_tx_clk(hdmi_tx_clk),.hdmi_tx_d({8'b0, hdmi_tx_d[15:0]}),
        .hdmi_tx_de(hdmi_tx_de),.hdmi_tx_hsync(hdmi_tx_hsync),.hdmi_tx_vsync(hdmi_tx_vsync)
    );
endmodule
