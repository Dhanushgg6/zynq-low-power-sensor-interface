# Measured Results and Analysis

## Power Measurement Setup

- **Instrument:** INA219 power monitor (I2C, 12-bit)
- **Shunt:** 0.1Ω on 12V adapter positive rail
- **Resolution:** ~4 mW per LSB at 12V
- **Calibration register:** 4481 (91.6 µA/LSB current, ~2 mW/LSB power)
- **Measurement:** Continuous, sampled every poll cycle

---

## PM Build Power Measurements

### COOL State (< 35°C ambient)
- CPU: 167 MHz | FCLK0: ~40 MHz | Poll: 4s | Sleep: WFI

| Sample | Power (mW) |
|--------|-----------|
| 1 | 3814 |
| 2 | 3810 |
| 3 | 3814 |
| 4 | 3810 |
| 5 | 3814 |
| **Average** | **~3812 mW** |

### NORM State (35–40°C ambient)
- CPU: 333 MHz | FCLK0: ~57 MHz | Poll: 2s | Sleep: WFI

| Sample | Power (mW) |
|--------|-----------|
| 1 | 3844 |
| 2 | 3848 |
| 3 | 3844 |
| 4 | 3860 |
| 5 | 3848 |
| **Average** | **~3849 mW** |

### HOT State (> 40°C ambient)
- CPU: 666 MHz | FCLK0: ~114 MHz | Poll: 1s | Sleep: WFI

| Sample | Power (mW) |
|--------|-----------|
| 1 | 3932 |
| 2 | 3920 |
| 3 | 3922 |
| 4 | 3920 |
| 5 | 3922 |
| **Average** | **~3923 mW** |

---

## Baseline Build Power Measurements

- CPU: 666 MHz (fixed) | FCLK0: ~114 MHz (fixed)
- Poll: 2s (fixed) | Sleep: usleep() busy-wait
- All peripheral clocks enabled (APER_CLK_CTRL = 0x3303FFFF)

| Sample | Power (mW) |
|--------|-----------|
| 1 | 3922 |
| 2 | 3920 |
| 3 | 3922 |
| 4 | 3920 |
| 5 | 3922 |
| **Average** | **~3921 mW** |

---

## Comparison Summary

| Build | State | CPU | FCLK0 | Power | vs Baseline |
|-------|-------|-----|--------|-------|-------------|
| Baseline | Fixed | 666 MHz | 114 MHz | ~3921 mW | — |
| PM Build | COOL | 167 MHz | ~40 MHz | ~3812 mW | **−109 mW** |
| PM Build | NORM | 333 MHz | ~57 MHz | ~3849 mW | **−72 mW** |
| PM Build | HOT | 666 MHz | ~114 MHz | ~3923 mW | ≈ 0 mW |

**Maximum power saving (COOL state): ~109 mW (2.78%)**

---

## DVFS Transition Verification

SLCR register readback confirms correct frequency programming:

| State | FCLK0_CTRL value | DIV1 | DIV0 | Frequency |
|-------|-----------------|------|------|-----------|
| COOL  | 0x00201400 | 2 | 20 | ~40 MHz ✓ |
| NORM  | 0x00200E00 | 2 | 14 | ~57 MHz ✓ |
| HOT   | 0x00200700 | 2 | 7  | ~114 MHz ✓ |

IO PLL assumed: ~1600 MHz. Frequency = 1600 / (DIV1 × DIV0).

---

## Sensor Readings

### TMP102 (ambient temperature)
- Room temperature reading: ~30–32°C
- Heating by hand reaches ~35–42°C for DVFS demonstration
- Resolution: 0.0625°C/LSB (12-bit mode)

### XADC (die temperature)
- Idle die temperature: ~44–48°C
- Rises slightly during HOT state due to increased CPU activity
- Does not reach warning threshold (65°C) during testing

### MPU-6500 (IMU)
At rest on flat surface:
- Ax: ~6800 LSB (~0.41g X-axis)
- Ay: ~3400 LSB (~0.21g Y-axis)
- Az: ~16700 LSB (~1.02g Z-axis — gravity)
- Gx/Gy/Gz: ~100–400 LSB (noise floor, no rotation)

---

## Vivado Power Estimation

From Vivado `report_power` (post-implementation):

| Component | Estimated Power |
|-----------|----------------|
| Clocking | ~15 mW |
| Logic | ~5 mW |
| BRAM | ~2 mW |
| DSP | ~0 mW |
| I/O | ~3 mW |
| **Total PL Dynamic** | **~25 mW** |
| PS7 (estimated) | ~1500 mW |

Note: Vivado estimates PL fabric power only. Actual measured board power (~3800–3922 mW) includes DDR, USB PHY, Ethernet PHY, HDMI transmitter, and all board-level components not modelled in Vivado.

---

## Energy Savings Projection

Assuming continuous operation at room temperature (COOL state):

```
Power saving per hour = 109 mW × 1h = 0.109 Wh
Power saving per day  = 109 mW × 24h = 2.616 Wh
Power saving per year = 109 mW × 8760h = 955 Wh ≈ 0.955 kWh
CO₂ equivalent (India grid, 0.82 kg/kWh) ≈ 0.78 kg CO₂/year saved
```

While small in absolute terms, the techniques demonstrated are directly applicable to battery-powered and large-scale embedded deployments where power savings compound significantly.
