# Presentation Guide

## Suggested Slide Structure (12–15 slides)

---

### Slide 1 — Title
**"Low Power Sensor Interface on Zynq-7000 SoC"**
- Your name, department, year
- ZedBoard photo + sensor setup photo

---

### Slide 2 — Problem Statement
**"Why Power Management Matters"**
- IoT devices run on battery — every mW counts
- Embedded systems waste energy running at max speed when idle
- This project: demonstrate adaptive power scaling on real hardware
- Key question: *Can we measure power savings on a ZedBoard?*

---

### Slide 3 — System Overview
**Block diagram showing:**
- ZedBoard (Zynq-7000)
- Three sensors on shared I2C bus (JA)
- HDMI output
- INA219 measuring 12V adapter power

*"Three sensors, one I2C bus, one PMOD connector"*

---

### Slide 4 — Hardware Architecture
**Clock domain diagram:**
```
IO PLL → FCLK0 (DVFS target) → AXI fabric
       → FCLK1 → clk_wiz → AXI interconnect
       → FCLK3 → Pixel clock (ISOLATED)
ARM PLL → CPU clock (DVFS target)
```
**Key point:** Pixel clock completely isolated — HDMI never drops during DVFS

---

### Slide 5 — Inverted DVFS Philosophy
**"Boot slow, scale up when needed"**

| State | Condition | CPU | FCLK0 | Power |
|-------|-----------|-----|--------|-------|
| COOL | < 35°C | 167 MHz | 40 MHz | ~3812 mW |
| NORM | 35–40°C | 333 MHz | 57 MHz | ~3849 mW |
| HOT | > 40°C | 666 MHz | 114 MHz | ~3927 mW |

*Show state machine diagram with hysteresis*

---

### Slide 6 — Power Management Techniques
**Four techniques implemented:**
1. **DVFS** — SLCR register programming (CPU + FCLK0)
2. **WFI Sleep** — ARM instruction, CPU halted between polls
3. **Adaptive Polling** — 4s/2s/1s based on thermal state
4. **3-Level Clock Scaling** — Ultra/Med/Fast

*Show SLCR register code snippet*

---

### Slide 7 — Sensor Integration
**Three sensors on one I2C bus:**
- TMP102 (0x48) — temperature, drives DVFS
- INA219 (0x40) — measures board power
- MPU-6500 (0x68) — 6-axis IMU

*Show breadboard photo with JA connections*
*Show I2C bus scan output from PuTTY*

---

### Slide 8 — HDMI Dashboard
**Photo/screenshot of HDMI display showing:**
- Left: Temperature readings
- Centre: MPU-6500 accel/gyro data
- Right: Power management state, live power reading

*"All data visible in real-time on 720p60 HDMI"*

---

### Slide 9 — Implementation: SLCR Programming
**Code snippet showing DVFS:**
```c
// COOL → ~40MHz
slcr_unlock();
Xil_Out32(SLCR_FCLK0_CTRL, 0x00201400);  // DIV1=2, DIV0=20
u32 arm = (Xil_In32(SLCR_ARM_CLK_CTRL)
           & ~0x00003F00) | 0x00000800;   // DIVISOR=8 → 167MHz
Xil_Out32(SLCR_ARM_CLK_CTRL, arm);
slcr_lock();
```
**Verified by register readback:**
`FCLK0_CTRL=0x00201400` → confirms ~40 MHz

---

### Slide 10 — Results: Power Measurements
**Graph showing power vs state:**

```
4000 mW ─────────────────── Baseline (3921 mW)
         ─────────────────── HOT state (3927 mW)
3900 mW
         ─────────────────── NORM state (3849 mW)
3800 mW  ─────────────────── COOL state (3812 mW)
```

**Delta: ~109 mW saved in idle (COOL) state**

---

### Slide 11 — Baseline vs PM Build Comparison

| Feature | PM Build | Baseline |
|---------|----------|---------|
| CPU (idle) | 167 MHz | 666 MHz |
| FCLK0 (idle) | ~40 MHz | ~114 MHz |
| Peripherals | HW default | All enabled |
| Sleep | WFI | busy-wait |
| Poll | 4s adaptive | 2s fixed |
| HDMI theme | Blue | **Red** |
| Power (idle) | **~3812 mW** | ~3921 mW |

*Show side-by-side HDMI photos*

---

### Slide 12 — Key Findings

1. DVFS confirmed working via SLCR register readback
2. ~109 mW saving (2.8%) in COOL state vs baseline
3. 3-level clock scaling (167/333/666 MHz + 40/57/114 MHz) verified
4. All three sensors operational on shared I2C bus
5. HDMI stable at all DVFS levels (isolated pixel clock)

**Limitation acknowledged:** PS7 dominates total power (~1.5W fixed). Controllable PL+CPU fraction is ~15% of board total.

---

### Slide 13 — Live Demo
**Show on hardware:**
1. PM build booting → `CPU=167MHz FCLK0=40MHz ~3812mW`
2. Hold TMP102 → DVFS transitions in real-time on PuTTY
3. HDMI showing live sensor data
4. Switch to baseline build → constant ~3922mW, red display

---

### Slide 14 — Conclusions

- Successfully implemented inverted DVFS on Zynq-7000 baremetal
- Three sensors integrated on single I2C bus — no hardware conflicts
- Measurable, repeatable power delta demonstrated on hardware
- Techniques applicable to battery-powered IoT and industrial monitoring
- Foundation for future work: DDR frequency scaling, PS7 power domains

---

### Slide 15 — Future Work

- Integrate TMP117 (higher precision) when available
- DDR frequency scaling for additional savings
- Battery life estimation with actual battery
- Power domain isolation using Zynq PS7 power management APIs
- Extend to multi-node wireless sensor network

---

## Demo Tips

**Before demo:**
1. Pre-flash PM build, verify all sensors showing
2. Have PuTTY open at 115200 baud
3. HDMI connected and showing blue dashboard
4. Have baseline .elf ready to flash quickly

**During demo:**
1. Point out COOL state power (~3812 mW) in PuTTY
2. Hold TMP102 — show DVFS transition live
3. Point out HDMI updating in real-time
4. Flash baseline — show red display, higher power

**Talking point for questions:**
- "The 2.8% saving is modest because PS7 is dominant — but the techniques are proven and scale to dedicated embedded designs"
- "In a custom ASIC or low-power MCU, the same DVFS technique gives 10-30% savings"
