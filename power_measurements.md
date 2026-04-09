# INA219 Power Measurement Log

## Measurement Conditions
- **Instrument:** INA219 (I2C, 12-bit, 0.1Ω shunt)
- **Supply:** 12V ZedBoard adapter
- **Ambient:** ~24°C room temperature
- **HDMI:** Connected, 720p60 active
- **Sensors:** TMP102 + INA219 + MPU6500 all active

---

## PM Build — COOL State
**Config:** CPU=167MHz, FCLK0=~40MHz, Poll=4s, WFI sleep

```
[00:00:04] Pwr=3814mW  State=COOL  FCLK0=40MHz  CPU=167MHz
[00:00:08] Pwr=3810mW  State=COOL  FCLK0=40MHz  CPU=167MHz
[00:00:12] Pwr=3814mW  State=COOL  FCLK0=40MHz  CPU=167MHz
[00:00:16] Pwr=3810mW  State=COOL  FCLK0=40MHz  CPU=167MHz
[00:00:20] Pwr=3814mW  State=COOL  FCLK0=40MHz  CPU=167MHz
```
**Average: ~3812 mW**

---

## PM Build — NORM State
**Config:** CPU=333MHz, FCLK0=~57MHz, Poll=2s, WFI sleep

```
[00:00:12] Pwr=3814mW  State=NORM  FCLK0=57MHz  CPU=333MHz
[00:00:14] Pwr=3844mW  State=NORM  FCLK0=57MHz  CPU=333MHz
[00:00:20] Pwr=3844mW  State=NORM  FCLK0=57MHz  CPU=333MHz
[00:00:22] Pwr=3848mW  State=NORM  FCLK0=57MHz  CPU=333MHz
[00:00:24] Pwr=3848mW  State=NORM  FCLK0=57MHz  CPU=333MHz
```
**Average: ~3840 mW**

---

## PM Build — HOT State
**Config:** CPU=666MHz, FCLK0=~114MHz, Poll=1s, WFI sleep

```
[00:00:35] Pwr=3932mW  State=HOT  FCLK0=114MHz  CPU=666MHz
[00:00:36] Pwr=3932mW  State=HOT  FCLK0=114MHz  CPU=666MHz
[00:00:37] Pwr=3920mW  State=HOT  FCLK0=114MHz  CPU=666MHz
[00:00:38] Pwr=3920mW  State=HOT  FCLK0=114MHz  CPU=666MHz
[00:00:39] Pwr=3932mW  State=HOT  FCLK0=114MHz  CPU=666MHz
```
**Average: ~3927 mW**

---

## Baseline Build
**Config:** CPU=666MHz (fixed), FCLK0=~114MHz (fixed), Poll=2s, usleep (no WFI)
**APER_CLK_CTRL:** 0x3303FFFF (all peripherals enabled)

```
[00:00:02] Pwr=3922mW  CPU=666MHz  FCLK0=114MHz  BASELINE
[00:00:04] Pwr=3920mW  CPU=666MHz  FCLK0=114MHz  BASELINE
[00:00:06] Pwr=3922mW  CPU=666MHz  FCLK0=114MHz  BASELINE
[00:00:08] Pwr=3920mW  CPU=666MHz  FCLK0=114MHz  BASELINE
[00:00:10] Pwr=3922mW  CPU=666MHz  FCLK0=114MHz  BASELINE
```
**Average: ~3921 mW**

---

## Summary Table

| Build | State | CPU | FCLK0 | Avg Power | Delta vs Baseline |
|-------|-------|-----|--------|-----------|------------------|
| Baseline | Fixed | 666 MHz | 114 MHz | 3921 mW | — |
| PM Build | COOL | 167 MHz | ~40 MHz | 3812 mW | **−109 mW** |
| PM Build | NORM | 333 MHz | ~57 MHz | 3840 mW | **−81 mW** |
| PM Build | HOT | 666 MHz | ~114 MHz | 3927 mW | ≈ 0 mW |

---

## DVFS Transition Log

```
Boot:      FCLK0_CTRL=0x00201400  (DIV1=2, DIV0=20, ~40MHz)  ← ULTRA
At 35°C:   [DVFS] L1 MED:  FCLK0=~57MHz  CPU=333MHz  (NORM 35-40C)
At 40°C:   [DVFS] L2 FAST: FCLK0=~114MHz CPU=666MHz  (HOT >40C)
Cool down: [DVFS] L1 MED:  FCLK0=~57MHz  CPU=333MHz  (NORM 35-40C)
           [DVFS] L0 ULTRA:FCLK0=~40MHz  CPU=167MHz  (COOL <35C)
```

All transitions verified by SLCR register readback.
