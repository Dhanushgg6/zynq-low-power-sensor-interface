# Zynq-7000 Low Power Sensor Interface

**Final Year Engineering Project**  
**Platform:** ZedBoard (Xilinx Zynq-7000, xc7z020clg484-1)  
**Toolchain:** Vivado / Vitis 2023.1 | Baremetal C  

---

## Project Overview

This project implements a **Low Power Sensor Interface** on the ZedBoard Zynq-7000 SoC, demonstrating practical power management techniques on embedded FPGA hardware. The system reads data from three sensors over a shared I2C bus, displays live data on a 1280×720 HDMI monitor, and dynamically scales clock frequencies and CPU speed based on ambient temperature.

### Key Features

- **3-sensor I2C interface** on a single shared bus (PMOD JA):
  - TMP102 — ambient temperature sensor (primary control input)
  - INA219 — power monitor (measures real-time board power consumption)
  - MPU-6500 — 6-axis IMU (accelerometer + gyroscope)
- **Inverted DVFS** — system boots at minimum power, scales up only when needed
- **3-level adaptive power management** driven by temperature
- **720p60 HDMI dashboard** showing live sensor data and power state
- **Baseline comparison build** (no PM) for quantitative power delta measurement

---

## Power Management Architecture

### Inverted DVFS Philosophy

Unlike conventional DVFS (which starts at maximum speed and throttles down), this system **boots at minimum power** and scales up only when thermal conditions demand it.

| State | Condition | CPU | FCLK0 | Poll | Power (typical) |
|-------|-----------|-----|--------|------|----------------|
| COOL  | < 35°C    | 167 MHz | ~40 MHz | 4s | ~3810 mW |
| NORM  | 35–40°C   | 333 MHz | ~57 MHz | 2s | ~3848 mW |
| HOT   | > 40°C    | 666 MHz | ~114 MHz | 1s | ~3922 mW |

**Hysteresis:** 2°C on downward transitions (NORM→COOL at 33°C, HOT→NORM at 38°C)

### Techniques Implemented

1. **DVFS (Dynamic Voltage Frequency Scaling)**  
   FCLK0 AXI fabric clock scaled via SLCR_FCLK0_CTRL register  
   3 levels: ~40MHz / ~57MHz / ~114MHz

2. **CPU Frequency Scaling**  
   ARM Cortex-A9 scaled via SLCR_ARM_CLK_CTRL register  
   3 levels: 167MHz / 333MHz / 666MHz

3. **WFI Sleep**  
   CPU halted between sensor polls using ARM `WFI` instruction  
   ~98% idle time in COOL state (4s poll period)

4. **Adaptive Polling**  
   Poll interval adapts: 4s (COOL) → 2s (NORM) → 1s (HOT)  
   Reduces unnecessary I2C transactions by 75% in idle conditions

### Measured Results

| Build | CPU | FCLK0 | Measured Power |
|-------|-----|--------|----------------|
| Baseline (No PM) | 666 MHz fixed | ~114 MHz fixed | ~3922 mW |
| PM Build (COOL) | 167 MHz | ~40 MHz | ~3810 mW |
| PM Build (HOT) | 666 MHz | ~114 MHz | ~3922 mW |
| **Delta (idle)** | | | **~112 mW saved** |

---

## Hardware Architecture

### Clock Domain Architecture

```
PS7 ARM PLL (1333 MHz)
├── ARM_CLK_CTRL → CPU clock (167/333/666 MHz) [DVFS target]
└── IO PLL (1600 MHz)
    ├── FCLK0 (DIV1=2, DIV0=7/14/20) → AXI fabric [DVFS target]
    ├── FCLK1 (50 MHz) → clk_wiz_0 MMCM input [stable]
    └── FCLK3 (74.25 MHz) → Pixel clock [HDMI, isolated from DVFS]
```

**Key design decision:** Pixel clock on dedicated FCLK3 — completely isolated from DVFS. HDMI display remains stable at all DVFS levels.

### Address Map

| Peripheral | Base Address | Description |
|-----------|-------------|-------------|
| axi_vdma_0 | 0x43000000 | Video DMA (framebuffer → HDMI) |
| axi_iic_0 | 0x41600000 | I2C bus (TMP102 + INA219 + MPU6500) |
| axi_iic_1 | 0x41610000 | ADV7511 HDMI transmitter I2C |
| axi_iic_2 | 0x41620000 | PMOD JB (reserved) |
| axi_gpio_0 | 0x41200000 | LEDs |
| xadc_wiz_0 | 0x43C00000 | XADC (die temperature + VCCINT) |
| v_tc_0 | 0x43C10000 | Video Timing Controller |
| Frame buffer | 0x01000000 | 1280×720×3 = 2.76 MB in DDR |

### PMOD JA Sensor Wiring

```
ZedBoard PMOD JA (top row):
  JA1 (SCL) ──┬── TMP102 SCL
              ├── INA219 SCL
              └── MPU6500 SCL

  JA2 (SDA) ──┬── TMP102 SDA
              ├── INA219 SDA
              └── MPU6500 SDA

  JA6 (3.3V) ─┬── TMP102 VCC
              ├── INA219 VCC
              └── MPU6500 VCC

  JA5 (GND) ──┬── TMP102 GND (ADD0→GND → addr 0x48)
              ├── INA219 GND
              └── MPU6500 GND (AD0→GND → addr 0x68)

I2C Addresses: INA219=0x40, TMP102=0x48, MPU6500=0x68
```

### INA219 Power Measurement

The INA219 measures current through a 0.1Ω shunt resistor on the 12V adapter input line. Configured for 32V range, 12-bit resolution, continuous mode.

- **Vin+** connected to adapter positive (after fuse)
- **Vin−** connected to board input
- Calibration: 4481 (for 0.1Ω shunt, 3A max, 91.6µA/LSB)

---

## SLCR Register Reference

Key registers used for runtime power management:

```c
// Base
#define SLCR_BASE         0xF8000000

// Clock control
#define SLCR_FCLK0_CTRL   0xF8000170  // AXI fabric clock
#define SLCR_ARM_CLK_CTRL 0xF8000120  // CPU clock

// FCLK0: DIV1[25:20], DIV0[13:8]
// COOL: DIV1=2, DIV0=20 → 1600/(2×20) = 40 MHz
// NORM: DIV1=2, DIV0=14 → 1600/(2×14) = 57 MHz
// HOT:  DIV1=2, DIV0=7  → 1600/(2×7)  = 114 MHz

// ARM_CLK: DIVISOR[13:8]
// COOL: DIVISOR=8 → 1333/8 = 167 MHz
// NORM: DIVISOR=4 → 1333/4 = 333 MHz
// HOT:  DIVISOR=2 → 1333/2 = 666 MHz

// SLCR access: unlock with 0xDF0D, lock with 0x767B
```

---

## Repository Structure

```
zynq-low-power-sensor-interface/
├── README.md                    # This file
├── src/
│   ├── main.c                   # PM build (main application)
│   ├── main_baseline.c          # Baseline build (no PM, for comparison)
│   ├── zynq_example_top.v       # Top-level Verilog wrapper
│   ├── tmp102_esp32_test.ino    # ESP32 TMP102 verification sketch
│   └── ina219_esp32_test.ino   # ESP32 INA219 verification sketch
├── constraints/
│   └── pmod_ja_i2c.xdc          # XDC pin constraints
├── docs/
│   ├── TECHNICAL_REPORT.md      # Full technical report
│   ├── HARDWARE_SETUP.md        # Wiring and hardware guide
│   ├── RESULTS.md               # Measured results and analysis
│   └── REFERENCES.md            # References and datasheets
├── scripts/
│   ├── vivado_build.tcl         # Vivado build automation
│   └── power_analysis.tcl       # Power report script
├── hardware/
│   └── block_design_notes.md    # Block design documentation
└── results/
    └── power_measurements.md    # INA219 power measurement log
```

---

## Build Instructions

### Prerequisites
- Vivado / Vitis 2023.1
- ZedBoard with xc7z020clg484-1
- HDMI monitor (1280×720 capable)
- Sensors: TMP102, INA219, MPU-6500 on PMOD JA breadboard

### Vivado (Hardware)
1. Open Vivado 2023.1
2. Open project: `project_2.xpr`
3. Block design already configured — generate bitstream:
   ```tcl
   launch_runs impl_1 -to_step write_bitstream -jobs 4
   wait_on_run impl_1
   ```
4. Export hardware: File → Export → Export Hardware (include bitstream)

### Vitis (Software — PM Build)
1. Create platform from exported XSA
2. Create application project: `finalyear_appl`
3. Replace `helloworld.c` with `src/main.c`
4. Build and run on hardware

### Vitis (Software — Baseline Build)
1. Using same platform
2. Create application project: `baseline_noPM`
3. Replace `helloworld.c` with `src/main_baseline.c`
4. Build and run on hardware

---

## Demonstration Procedure

1. Flash **PM build** → observe UART: `CPU=167MHz FCLK0=40MHz` at room temp (~3810 mW)
2. Hold TMP102 sensor between fingers → temperature rises
3. At 35°C: `[DVFS] L1 MED: FCLK0=~57MHz CPU=333MHz` → power rises to ~3848 mW
4. At 40°C: `[DVFS] L2 FAST: FCLK0=~114MHz CPU=666MHz` → power rises to ~3922 mW
5. Release sensor → temperature falls → DVFS scales back down
6. Flash **Baseline build** → observe constant ~3922 mW regardless of temperature
7. Delta at idle: **~112 mW** (PM build saves power in all non-HOT states)

---

## Authors

**Dhanush D Kamath** — Final Year Engineering Student 
**Amogh Kamath Kondadi** — Final Year Engineering Student  
**I Prateek Pai** — Final Year Engineering Student  
Department of Electronics & Communication Engineering Engineering
**NMAM Institute of Technology Nitte**

---

## License

This project is submitted as a final year academic project.
