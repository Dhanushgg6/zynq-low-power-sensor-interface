# Block Design Notes

## design_1.bd — IP Core Summary

| IP Core | Instance Name | Address | Configuration |
|---------|--------------|---------|---------------|
| ZYNQ7 Processing System | processing_system7_0 | — | FCLK0/1/3 enabled |
| AXI Interconnect | axi_interconnect_0 | — | 9 master ports |
| AXI IIC | axi_iic_0 | 0x41600000 | PMOD JA (TMP102+INA219+MPU6500) |
| AXI IIC | axi_iic_1 | 0x41610000 | ADV7511 HDMI I2C |
| AXI IIC | axi_iic_2 | 0x41620000 | PMOD JB (reserved) |
| AXI GPIO | axi_gpio_0 | 0x41200000 | LEDs (8-bit output) |
| AXI VDMA | axi_vdma_0 | 0x43000000 | MM2S read channel only |
| Video Timing Controller | v_tc_0 | 0x43C10000 | 1280x720@60Hz |
| AXI4S to Video Out | v_axi4s_vid_out_0 | — | 24-bit RGB |
| XADC Wizard | xadc_wiz_0 | 0x43C00000 | Temperature + VCCINT |
| Clocking Wizard | clk_wiz_0 | — | 40MHz from FCLK1 |
| Block Memory Generator | blk_mem_gen_0 | 0x40000000 | 64KB dual-port BRAM |
| Proc System Reset (x4) | proc_sys_reset_* | — | Multiple reset domains |

## Clock Connections

```
FCLK0 (DVFS target, ~40/57/114MHz)
  └── AXI interconnect master clock
  └── All AXI slave clocks (IIC, GPIO, VDMA S_AXI_LITE, XADC, VTC)

FCLK1 (50MHz, stable)
  └── clk_wiz_0 input
      └── clk_out1 (40MHz) = clk_fast_w → AXI interconnect

FCLK3 (74.25MHz, isolated)
  └── v_tc_0 clock
  └── v_axi4s_vid_out_0 clock
  └── axi_vdma_0 M_AXIS_MM2S clock
```

## Key Design Decisions

### Why FCLK3 for pixel clock?
Any clock used as a pixel clock must be extremely stable — even a single clock glitch causes HDMI sync loss. By using FCLK3 (a dedicated PS7 output) instead of a clock derived from FCLK0, we ensure the pixel clock is completely unaffected by DVFS operations on FCLK0.

### Why not gate FCLK0?
The SLCR_FPGA_RST_CTRL register was investigated for FCLK0 gating. However, setting the FPGA0_OUT_RST bit resets the **entire PL fabric**, including VDMA and VTC, which causes immediate HDMI loss. Clock gating via FCLK0 alone is not possible through standard Zynq registers — the FCLK enable bit (bit 0 of FCLK0_CTRL) is reserved and has no effect.

### AXI interconnect width
The AXI interconnect is configured with 9 master ports to accommodate all peripherals. clk_fast_w (40MHz) feeds the interconnect for stable AXI transactions even when FCLK0 is at its lowest (40MHz) setting.

## Timing Results (Post-Implementation)

- **WNS (Worst Negative Slack):** +2.836 ns ✓
- **TNS:** 0.000 ns ✓
- **Hold violation:** −0.303 ns on internal Xilinx BRAM reset path (benign, Xilinx IP internal)
- **All user paths:** Timing met

## Wrapper Port List (design_1_wrapper.v)

Key I/O ports:
```verilog
inout  iic_jb_scl_io;    // PMOD JB SCL (axi_iic_2)
inout  iic_jb_sda_io;    // PMOD JB SDA (axi_iic_2)
inout  iic_rtl_0_scl_io; // PMOD JA SCL (axi_iic_0)
inout  iic_rtl_0_sda_io; // PMOD JA SDA (axi_iic_0)
inout  iic_rtl_1_scl_io; // ADV7511 SCL (axi_iic_1)
inout  iic_rtl_1_sda_io; // ADV7511 SDA (axi_iic_1)
output hdmi_tx_clk;       // 74.25MHz pixel clock
output [15:0] hdmi_tx_d; // 16-bit RGB data (upper 8 tied 0)
output hdmi_tx_de;        // Data enable
output hdmi_tx_hsync;     // Horizontal sync
output hdmi_tx_vsync;     // Vertical sync
output FCLK_CLK0;         // FCLK0 exposed to top module
output clk_fast;          // 40MHz AXI clock
output clk_slow;          // (unused in final design)
```
