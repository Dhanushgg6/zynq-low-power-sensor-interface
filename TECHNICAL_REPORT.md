# Technical Report: Zynq-7000 Low Power Sensor Interface

**Project Title:** Low Power Sensor Interface with Adaptive DVFS on Zynq-7000  
**Platform:** ZedBoard (xc7z020clg484-1)  
**Toolchain:** Xilinx Vivado / Vitis 2023.1, Baremetal C  

---

## 1. Introduction

### 1.1 Motivation

Embedded systems in real-world applications — industrial monitoring, IoT nodes, wearable devices — must balance computational performance with power consumption. Static clock configurations waste energy during idle periods, while aggressive power gating risks instability. This project demonstrates a practical, hardware-verified implementation of adaptive power management on the Xilinx Zynq-7000 SoC.

### 1.2 Objectives

1. Interface three I2C sensors on a shared bus over a single PMOD connector
2. Implement multi-level DVFS using direct SLCR register programming
3. Demonstrate measurable power reduction using hardware INA219 power monitoring
4. Display live sensor data on 720p60 HDMI output
5. Compare PM build against a fixed-clock baseline build

### 1.3 Project Scope

The project targets the PL (Programmable Logic) AXI fabric clock and PS7 ARM CPU clock as DVFS targets. The HDMI display pipeline runs on a dedicated clock domain (FCLK3) isolated from all DVFS operations, ensuring display stability throughout.

---

## 2. System Architecture

### 2.1 Hardware Platform

The ZedBoard features a Zynq-7000 SoC (xc7z020clg484-1) combining:
- Dual-core ARM Cortex-A9 PS7 (up to 667 MHz)
- Artix-7 equivalent PL fabric (85K logic cells)
- 512 MB DDR3 RAM
- ADV7511 HDMI transmitter
- Multiple PMOD connectors for sensor interfacing

### 2.2 Clock Domain Architecture

A critical design decision was separating the pixel clock from the DVFS-controlled fabric clock:

```
IO PLL (~1600 MHz)
├── FCLK0 → DVFS target (AXI fabric)
│     COOL: DIV1=2, DIV0=20 → 40 MHz
│     NORM: DIV1=2, DIV0=14 → 57 MHz  
│     HOT:  DIV1=2, DIV0=7  → 114 MHz
├── FCLK1 (50 MHz) → clk_wiz_0 MMCM → clk_fast_w (40 MHz AXI interconnect)
└── FCLK3 (74.25 MHz) → VTC + VDMA + Video Out [ISOLATED]

ARM PLL (1333 MHz)
└── ARM_CLK_CTRL → CPU
      COOL: DIVISOR=8 → 167 MHz
      NORM: DIVISOR=4 → 333 MHz
      HOT:  DIVISOR=2 → 666 MHz
```

This architecture allows unrestricted DVFS on FCLK0 and CPU without any risk to the HDMI synchronisation signals.

### 2.3 Sensor Architecture

All three sensors share a single I2C bus on PMOD JA (axi_iic_0 @ 0x41600000):

| Sensor | Address | Purpose |
|--------|---------|---------|
| INA219 | 0x40 | Board power measurement (12V adapter) |
| TMP102 | 0x48 | Ambient temperature (DVFS trigger) |
| MPU-6500 | 0x68 | 6-axis IMU (accelerometer + gyroscope) |

The I2C bus operates at 100 kHz (standard mode). All three devices were verified on both ESP32 (Arduino IDE) and ZedBoard before integration.

---

## 3. Power Management Implementation

### 3.1 Inverted DVFS Design

Conventional DVFS starts at maximum performance and throttles down under thermal stress. This project inverts that philosophy: **the system boots at minimum power and scales up only when thermal conditions require it**.

This is more appropriate for sensor interface applications where:
- Most time is spent in idle polling
- Occasional bursts of activity (temperature events) justify higher clock speeds
- Energy efficiency is the primary design objective

### 3.2 Thermal State Machine

```
         < 35°C          35-40°C          > 40°C
    ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
    │    COOL      │  │    NORM      │  │    HOT       │
    │  CPU=167MHz  │  │  CPU=333MHz  │  │  CPU=666MHz  │
    │ FCLK0=40MHz  │  │ FCLK0=57MHz  │  │ FCLK0=114MHz │
    │   Poll=4s    │  │   Poll=2s    │  │   Poll=1s    │
    └──────────────┘  └──────────────┘  └──────────────┘
         ↑ 33°C (hyst)      ↑ 38°C (hyst)
```

Hysteresis of 2°C prevents state chattering at boundary temperatures.

### 3.3 SLCR Register Programming

The Zynq-7000 System Level Control Register (SLCR) is used for runtime clock management:

```c
// Unlock SLCR
Xil_Out32(0xF8000008, 0xDF0D);

// Set FCLK0 to ~40MHz (DIV1=2, DIV0=20)
Xil_Out32(0xF8000170, 0x00201400);

// Set CPU to 167MHz (DIVISOR=8)
u32 reg = Xil_In32(0xF8000120);
reg = (reg & ~0x00003F00) | 0x00000800;
Xil_Out32(0xF8000120, reg);

// Lock SLCR
Xil_Out32(0xF8000004, 0x767B);
```

Register writes are verified by readback. FCLK0_CTRL value `0x00201400` confirms DIV1=2, DIV0=20 → ~40 MHz operation.

### 3.4 WFI Sleep

Between sensor polls, the CPU executes the ARM `WFI` (Wait For Interrupt) instruction:

```c
while (!timer_fired) __asm__ volatile ("wfi");
```

This halts the instruction pipeline, cache, and most processor subsystems until the SCU timer interrupt fires. In COOL state with 4s poll interval, the CPU is active for less than 2% of the time.

### 3.5 Adaptive Polling

Poll interval adapts to thermal state:
- COOL (< 35°C): 4s — minimal sensor activity, maximum idle time
- NORM (35-40°C): 2s — moderate responsiveness
- HOT (> 40°C): 1s — rapid temperature tracking

---

## 4. Sensor Integration

### 4.1 TMP102 Temperature Sensor

- **Interface:** I2C, address 0x48 (ADD0=GND)
- **Resolution:** 12-bit, 0.0625°C/LSB
- **Range:** -40°C to +125°C
- **Reading:** Register 0x00, 2 bytes, MSB first, right-shift 4
- **DVFS trigger:** Temperature change ≥ 0.5°C (8 LSBs) triggers UART output

```c
// Read temperature
uint8_t reg = 0x00;
XIic_Send(base, 0x48, &reg, 1, XIIC_STOP);
XIic_Recv(base, 0x48, rx, 2, XIIC_STOP);
int16_t raw = ((rx[0]<<8)|rx[1]) >> 4;
float celsius = raw * 0.0625f;
```

### 4.2 INA219 Power Monitor

- **Interface:** I2C, address 0x40
- **Shunt:** 0.1Ω, configured for 32V / 3A range
- **Calibration:** 4481 (CurrentLSB = 91.6 µA)
- **Power LSB:** ~2 mW per register bit
- **Placement:** Vin+ / Vin- on 12V adapter cable

The INA219 provides real-time board-level power measurement, allowing direct comparison between PM and baseline builds.

### 4.3 MPU-6500 IMU

- **Interface:** I2C, address 0x68 (AD0=GND), CS=VCC for I2C mode
- **WHO_AM_I:** 0x70 (MPU-6500 confirmed)
- **Accel range:** ±2g (16384 LSB/g)
- **Gyro range:** ±250°/s (131 LSB/°/s)
- **Reading:** 14-byte burst read from register 0x3B (Accel X/Y/Z + Temp + Gyro X/Y/Z)

Motion detection uses deviation from 1g gravity vector:
```c
int32_t mag = isqrt(ax² + ay² + az²);
int32_t deviation = abs(mag - 16384) * 100 / 16384;  // in 0.01g
bool motion = (deviation > 15);  // threshold: 0.15g
```

### 4.4 XADC Die Temperature

The Zynq-7000 internal XADC monitors die temperature and VCCINT:
- **Die temp formula:** T(°C) = (ADC × 503.975 / 4096) - 273.15
- **VCCINT formula:** V(mV) = ADC × 3000 / 4096
- Used as secondary temperature indicator alongside TMP102

---

## 5. HDMI Display

### 5.1 Display Pipeline

```
DDR3 Framebuffer (0x01000000)
    └─ AXI VDMA (DMA read)
        └─ AXI4-Stream to Video Out
            └─ Video Timing Controller (VTC, 74.25 MHz)
                └─ ADV7511 HDMI Transmitter
                    └─ HDMI Monitor (1280×720 @ 60Hz)
```

### 5.2 Dashboard Layout

The 1280×720 HDMI dashboard is divided into three columns:

**Left Column — Temperature**
- TMP102 ambient reading with bar graph
- XADC die temperature
- FIR-filtered temperature
- Statistical analysis (mean, min/max, trend)
- Thermal state indicator

**Centre Column — MPU-6500**
- Accelerometer X/Y/Z with bar graphs (±2g range)
- Gyroscope X/Y/Z readings
- Motion detection indicator

**Right Column — Power Management**
- Current DVFS mode (ULTRA/MED/FAST)
- FCLK0 frequency
- CPU frequency
- Sleep mode status
- Live INA219 power reading
- Power bar graph (3500–4500 mW range)
- Sensor status indicators

### 5.3 Flicker Prevention

DDR contention between VDMA scanout and CPU framebuffer writes causes visible flicker. Resolved by:
1. Waiting 20ms (> one 720p60 frame) before `Xil_DCacheFlushRange()`
2. Throttling HDMI redraws to every 8 idle polls
3. Only redrawing when sensor values change

---

## 6. Baseline Comparison

### 6.1 Baseline Configuration

The baseline build (`main_baseline.c`) represents the worst-case power scenario:

| Parameter | PM Build (COOL) | Baseline |
|-----------|----------------|---------|
| CPU clock | 167 MHz | 666 MHz |
| FCLK0 | ~40 MHz | ~114 MHz |
| Peripheral clocks | HW default | All enabled (0x3303FFFF) |
| Poll interval | 4s adaptive | 2s fixed |
| CPU sleep | WFI | `usleep()` busy-wait |
| LED state | State indicators | All 8 LEDs ON |
| HDMI theme | Blue | Red |

### 6.2 Measured Power Comparison

| State | Build | Power (INA219) |
|-------|-------|----------------|
| Idle (room temp) | PM Build COOL | ~3810 mW |
| Idle (room temp) | Baseline | ~3922 mW |
| **Delta** | | **~112 mW** |
| Under thermal load | PM Build HOT | ~3922 mW |
| Under thermal load | Baseline | ~3922 mW |

### 6.3 Energy Savings Estimate

In a typical deployment scenario (room temperature, 23°C ambient):
- PM Build: ~3810 mW × continuous = 3.81 W
- Baseline: ~3922 mW × continuous = 3.92 W
- Saving: 112 mW = **2.86% reduction**
- Over 24 hours: 112 mW × 24h = **2.69 Wh saved per day**
- Over 1 year: ~981 Wh ≈ **~0.98 kWh per year**

---

## 7. Signal Processing

### 7.1 FIR Filter

An 11-tap Hamming-windowed FIR low-pass filter smooths TMP102 readings:

```
Coefficients: [9, 35, 84, 159, 223, 250, 223, 159, 84, 35, 9]
Sum: 1224 (normalisation divisor)
```

The filtered output reduces noise in the temperature readings displayed on HDMI.

### 7.2 Statistical Analysis

A 16-sample sliding window maintains:
- **Mean:** Running average of last 16 temperature samples
- **Standard deviation:** For anomaly detection
- **Linear regression slope:** Temperature trend in °C/min
- **Anomaly detection:** Sample deviates > 2σ from mean

### 7.3 Thermal Event Detection

A rapid temperature change ≥ 0.5°C between consecutive polls is flagged as a thermal event, triggering immediate HDMI redraw and LED indicator.

---

## 8. Results and Discussion

### 8.1 DVFS Verification

SLCR register readback confirms frequency changes:

```
Boot:   FCLK0_CTRL=0x00201400 → DIV1=2, DIV0=20 → ~40 MHz ✓
NORM:   FCLK0_CTRL=0x00200E00 → DIV1=2, DIV0=14 → ~57 MHz ✓
HOT:    FCLK0_CTRL=0x00200700 → DIV1=2, DIV0=7  → ~114 MHz ✓
```

### 8.2 Power Delta Analysis

The measured ~112 mW delta between COOL and HOT states is primarily from:

| Component | Contribution |
|-----------|-------------|
| CPU 167→666 MHz scaling | ~60-80 mW |
| FCLK0 40→114 MHz scaling | ~20-40 mW |
| Increased poll activity | ~5-10 mW |
| **Total** | **~85-130 mW** |

The PS7 (ARM cores, DDR, USB PHY, Ethernet PHY) contributes ~1.5W fixed baseline that is not controllable at this level. The controllable portion represents approximately 3-4% of total board power — a realistic and honest result for this platform.

### 8.3 Limitations

1. **PS7 dominates total power** (~1.5W) — limits achievable percentage reduction
2. **INA219 resolution** (~4 mW LSB at 12V) limits precision of small deltas
3. **Thermal coupling** — heating TMP102 by hand also slightly warms the board, causing a small confounding effect on power readings
4. **Single-board measurement** — no isolation between PS7 and PL power rails on ZedBoard

---

## 9. Conclusions

This project successfully demonstrates:

1. **Inverted DVFS** on Zynq-7000 with three distinct power states, triggered by real sensor data
2. **Direct SLCR register programming** for CPU and FCLK0 frequency scaling without OS support
3. **Three-sensor I2C bus sharing** on a single PMOD connector with no address conflicts
4. **Measurable power delta** of ~112 mW (2.86%) between minimum and maximum power states
5. **Stable HDMI output** at all DVFS levels through clock domain isolation

The project demonstrates all key embedded power management techniques — DVFS, WFI sleep, and adaptive polling — on real hardware with measured verification.

---

## 10. References

See `docs/REFERENCES.md` for full reference list.
