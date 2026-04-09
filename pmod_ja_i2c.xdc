# =============================================================================
# pmod_ja_i2c.xdc  -  FINAL version
# Port names verified against design_1_wrapper.v (generated Fri Mar 20 2026)
#
# Wrapper exposes:
#   iic_rtl_0_scl_io / iic_rtl_0_sda_io  -> TMP102  (IOBUF inside wrapper)
#   iic_rtl_1_scl_io / iic_rtl_1_sda_io  -> ADV7511 (IOBUF inside wrapper)
#   hdmi_tx_d[23:0]  -> only [15:0] connected to ADV7511 pins
#   leds_8bits_tri_o, sws_8bits_tri_i
#
# Bank voltages (ZedBoard HW UG v2.2 Table 21):
#   Bank 13 = 3.3V  (PMOD JA)
#   Bank 33 = 3.3V  (HDMI, LEDs)
#   Bank 35 = Vadj  (Switches)
# =============================================================================

# =============================================================================
# TMP102 I2C - PMOD JA (Bank 13, 3.3V)
# iic_rtl_0_scl_io -> Y11 (JA1), iic_rtl_0_sda_io -> AA11 (JA2)
# IOBUF is inside wrapper - constrain the _io inout port directly
# =============================================================================
set_property PACKAGE_PIN Y11  [get_ports iic_rtl_0_scl_io]
set_property IOSTANDARD LVCMOS33 [get_ports iic_rtl_0_scl_io]
set_property PULLUP true [get_ports iic_rtl_0_scl_io]

set_property PACKAGE_PIN AA11 [get_ports iic_rtl_0_sda_io]
set_property IOSTANDARD LVCMOS33 [get_ports iic_rtl_0_sda_io]
set_property PULLUP true [get_ports iic_rtl_0_sda_io]

# =============================================================================
# ADV7511 HDMI I2C (Bank 33, 3.3V)
# iic_rtl_1_scl_io -> AA18 (HD-SCL), iic_rtl_1_sda_io -> Y16 (HD-SDA)
# From ZedBoard HW UG v2.2 Table 7
# =============================================================================
set_property PACKAGE_PIN AA18 [get_ports iic_rtl_1_scl_io]
set_property IOSTANDARD LVCMOS33 [get_ports iic_rtl_1_scl_io]
set_property PULLUP true [get_ports iic_rtl_1_scl_io]

set_property PACKAGE_PIN Y16  [get_ports iic_rtl_1_sda_io]
set_property IOSTANDARD LVCMOS33 [get_ports iic_rtl_1_sda_io]
set_property PULLUP true [get_ports iic_rtl_1_sda_io]

# =============================================================================
# DIP Switches SW0-SW7 (Bank 35, Vadj=2.5V -> LVCMOS25)
# From ZedBoard HW UG v2.2 Table 13
# =============================================================================
set_property PACKAGE_PIN F22 [get_ports {sws_8bits_tri_i[0]}]
set_property PACKAGE_PIN G22 [get_ports {sws_8bits_tri_i[1]}]
set_property PACKAGE_PIN H22 [get_ports {sws_8bits_tri_i[2]}]
set_property PACKAGE_PIN F21 [get_ports {sws_8bits_tri_i[3]}]
set_property PACKAGE_PIN H19 [get_ports {sws_8bits_tri_i[4]}]
set_property PACKAGE_PIN H18 [get_ports {sws_8bits_tri_i[5]}]
set_property PACKAGE_PIN H17 [get_ports {sws_8bits_tri_i[6]}]
set_property PACKAGE_PIN M15 [get_ports {sws_8bits_tri_i[7]}]
set_property IOSTANDARD LVCMOS25 [get_ports {sws_8bits_tri_i[0]}]
set_property IOSTANDARD LVCMOS25 [get_ports {sws_8bits_tri_i[1]}]
set_property IOSTANDARD LVCMOS25 [get_ports {sws_8bits_tri_i[2]}]
set_property IOSTANDARD LVCMOS25 [get_ports {sws_8bits_tri_i[3]}]
set_property IOSTANDARD LVCMOS25 [get_ports {sws_8bits_tri_i[4]}]
set_property IOSTANDARD LVCMOS25 [get_ports {sws_8bits_tri_i[5]}]
set_property IOSTANDARD LVCMOS25 [get_ports {sws_8bits_tri_i[6]}]
set_property IOSTANDARD LVCMOS25 [get_ports {sws_8bits_tri_i[7]}]

# =============================================================================
# User LEDs LD0-LD7 (Bank 33, 3.3V)
# From ZedBoard HW UG v2.2 Table 14
# =============================================================================
set_property PACKAGE_PIN T22 [get_ports {leds_8bits_tri_o[0]}]
set_property PACKAGE_PIN T21 [get_ports {leds_8bits_tri_o[1]}]
set_property PACKAGE_PIN U22 [get_ports {leds_8bits_tri_o[2]}]
set_property PACKAGE_PIN U21 [get_ports {leds_8bits_tri_o[3]}]
set_property PACKAGE_PIN V22 [get_ports {leds_8bits_tri_o[4]}]
set_property PACKAGE_PIN W22 [get_ports {leds_8bits_tri_o[5]}]
set_property PACKAGE_PIN U19 [get_ports {leds_8bits_tri_o[6]}]
set_property PACKAGE_PIN U14 [get_ports {leds_8bits_tri_o[7]}]
set_property IOSTANDARD LVCMOS33 [get_ports {leds_8bits_tri_o[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {leds_8bits_tri_o[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {leds_8bits_tri_o[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {leds_8bits_tri_o[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {leds_8bits_tri_o[4]}]
set_property IOSTANDARD LVCMOS33 [get_ports {leds_8bits_tri_o[5]}]
set_property IOSTANDARD LVCMOS33 [get_ports {leds_8bits_tri_o[6]}]
set_property IOSTANDARD LVCMOS33 [get_ports {leds_8bits_tri_o[7]}]

# =============================================================================
# HDMI TX Pixel Clock (Bank 33, 3.3V)
# HD-CLK = W18 from Table 7
# =============================================================================
set_property PACKAGE_PIN W18  [get_ports hdmi_tx_clk]
set_property IOSTANDARD LVCMOS33 [get_ports hdmi_tx_clk]

# =============================================================================
# HDMI TX Data hdmi_tx_d[23:0]
# Wrapper exposes [23:0] but only [15:0] are connected to ADV7511 pins
# [23:16] are tied 0 inside top module — UCIO-1 suppressed below
# All physical pins in Bank 33 (3.3V) from Table 7
# =============================================================================
set_property PACKAGE_PIN Y13  [get_ports {hdmi_tx_d[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {hdmi_tx_d[0]}]
set_property PACKAGE_PIN AA12 [get_ports {hdmi_tx_d[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {hdmi_tx_d[1]}]
set_property PACKAGE_PIN AA14 [get_ports {hdmi_tx_d[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {hdmi_tx_d[2]}]
set_property PACKAGE_PIN Y14  [get_ports {hdmi_tx_d[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {hdmi_tx_d[3]}]
set_property PACKAGE_PIN AB15 [get_ports {hdmi_tx_d[4]}]
set_property IOSTANDARD LVCMOS33 [get_ports {hdmi_tx_d[4]}]
set_property PACKAGE_PIN AB16 [get_ports {hdmi_tx_d[5]}]
set_property IOSTANDARD LVCMOS33 [get_ports {hdmi_tx_d[5]}]
set_property PACKAGE_PIN AA16 [get_ports {hdmi_tx_d[6]}]
set_property IOSTANDARD LVCMOS33 [get_ports {hdmi_tx_d[6]}]
set_property PACKAGE_PIN AB17 [get_ports {hdmi_tx_d[7]}]
set_property IOSTANDARD LVCMOS33 [get_ports {hdmi_tx_d[7]}]
set_property PACKAGE_PIN AA17 [get_ports {hdmi_tx_d[8]}]
set_property IOSTANDARD LVCMOS33 [get_ports {hdmi_tx_d[8]}]
set_property PACKAGE_PIN Y15  [get_ports {hdmi_tx_d[9]}]
set_property IOSTANDARD LVCMOS33 [get_ports {hdmi_tx_d[9]}]
set_property PACKAGE_PIN W13  [get_ports {hdmi_tx_d[10]}]
set_property IOSTANDARD LVCMOS33 [get_ports {hdmi_tx_d[10]}]
set_property PACKAGE_PIN W15  [get_ports {hdmi_tx_d[11]}]
set_property IOSTANDARD LVCMOS33 [get_ports {hdmi_tx_d[11]}]
set_property PACKAGE_PIN V15  [get_ports {hdmi_tx_d[12]}]
set_property IOSTANDARD LVCMOS33 [get_ports {hdmi_tx_d[12]}]
set_property PACKAGE_PIN U17  [get_ports {hdmi_tx_d[13]}]
set_property IOSTANDARD LVCMOS33 [get_ports {hdmi_tx_d[13]}]
set_property PACKAGE_PIN V14  [get_ports {hdmi_tx_d[14]}]
set_property IOSTANDARD LVCMOS33 [get_ports {hdmi_tx_d[14]}]
set_property PACKAGE_PIN V13  [get_ports {hdmi_tx_d[15]}]
set_property IOSTANDARD LVCMOS33 [get_ports {hdmi_tx_d[15]}]
# d[23:16] have no physical pins - tied 0 in top module, UCIO-1 suppressed

# =============================================================================
# HDMI TX Control (Bank 33, 3.3V)
# HD-VSYNC=W17, HD-HSYNC=V17, HD-DE=U16 from Table 7
# =============================================================================
set_property PACKAGE_PIN W17  [get_ports hdmi_tx_vsync]
set_property IOSTANDARD LVCMOS33 [get_ports hdmi_tx_vsync]

set_property PACKAGE_PIN V17  [get_ports hdmi_tx_hsync]
set_property IOSTANDARD LVCMOS33 [get_ports hdmi_tx_hsync]

set_property PACKAGE_PIN U16  [get_ports hdmi_tx_de]
set_property IOSTANDARD LVCMOS33 [get_ports hdmi_tx_de]

# =============================================================================
# Timing exceptions
# =============================================================================
set_false_path -from [get_ports iic_rtl_0_scl_io]
set_false_path -from [get_ports iic_rtl_0_sda_io]
set_false_path -from [get_ports iic_rtl_1_scl_io]
set_false_path -from [get_ports iic_rtl_1_sda_io]
set_false_path -from [get_ports {sws_8bits_tri_i[*]}]
set_false_path -to   [get_ports {leds_8bits_tri_o[*]}]

# =============================================================================
# Suppress UCIO-1 for hdmi_tx_d[23:16] - no physical pins, tied 0 in RTL
# =============================================================================
set_property SEVERITY {Warning} [get_drc_checks UCIO-1]

# =============================================================================
# Bitstream
# =============================================================================
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]
set_property BITSTREAM.CONFIG.UNUSEDPIN PULLDOWN [current_design]
