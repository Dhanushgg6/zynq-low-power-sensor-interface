# Hardware Setup Guide

## Required Components

| Component | Quantity | Notes |
|-----------|----------|-------|
| ZedBoard (xc7z020clg484-1) | 1 | With 12V power adapter |
| TMP102 temperature sensor module | 1 | I2C, 3.3V |
| INA219 power monitor module | 1 | 0.1Ω shunt on board |
| MPU-6500 IMU module | 1 | GY-6500 or similar, 3.3V |
| Breadboard (half-size or larger) | 1 | For shared I2C bus |
| Jumper wires (male-male) | ~15 | For sensor connections |
| HDMI cable | 1 | Type A, to monitor |
| HDMI monitor | 1 | Must support 1280×720 @ 60Hz |
| Micro USB cable | 1 | For UART (PuTTY) |
| Wire cutters / stripper | 1 | For INA219 shunt wiring |

---

## PMOD JA Sensor Wiring

### Breadboard Layout

Use the breadboard to create a shared I2C bus connecting all three sensors to PMOD JA:

```
ZedBoard PMOD JA pinout (top row, left to right):
┌────┬────┬────┬────┬─────┬─────┐
│ JA1│ JA2│ JA3│ JA4│ GND │ VCC │
│ SCL│ SDA│    │    │     │ 3.3V│
└────┴────┴────┴────┴─────┴─────┘
```

### Step-by-step wiring

**Step 1 — Power rails on breadboard:**
- JA6 (VCC, 3.3V) → breadboard + rail
- JA5 (GND) → breadboard − rail

**Step 2 — SCL bus row:**
- JA1 → breadboard row A
- TMP102 SCL → same row A
- INA219 SCL → same row A
- MPU6500 SCL → same row A

**Step 3 — SDA bus row:**
- JA2 → breadboard row B
- TMP102 SDA → same row B
- INA219 SDA → same row B
- MPU6500 SDA → same row B

**Step 4 — Power each sensor:**
- TMP102 VCC → + rail, GND → − rail, ADD0 → − rail
- INA219 VCC → + rail, GND → − rail
- MPU6500 VCC → + rail, GND → − rail, AD0 → − rail

**Step 5 — MPU6500 mode select:**
- MPU6500 CS pin → + rail (3.3V) — **CRITICAL: selects I2C mode**
- MPU6500 AD0 pin → − rail (GND) — sets address 0x68

### I2C Address Summary

| Sensor | Address | Pin config |
|--------|---------|-----------|
| INA219 | 0x40 | Default (no address pins on this module) |
| TMP102 | 0x48 | ADD0 = GND |
| MPU6500 | 0x68 | AD0 = GND, CS = VCC |

---

## INA219 Power Measurement Wiring

The INA219 measures current through a shunt resistor placed in series with the power supply line.

### Setup

1. Cut the **positive** wire of the 12V ZedBoard power adapter
2. Connect the cut ends to INA219:
   - Adapter side (from wall) → **Vin+**
   - Board side (to ZedBoard) → **Vin−**
3. The INA219 I2C pins (VCC, GND, SCL, SDA) connect to the breadboard as above

```
Wall Adapter (+) ──[cut]── Vin+ [INA219] Vin− ── ZedBoard (+)
Wall Adapter (−) ──────────────────────────────── ZedBoard (−)
```

> **Warning:** Only cut the positive wire. Ensure connections are secure before powering on.

---

## HDMI Connection

Connect a standard HDMI cable from the ZedBoard HDMI-OUT port to your monitor. The display outputs at **1280×720 @ 60Hz**. If your monitor doesn't auto-detect, manually set input to HDMI and resolution to 720p.

---

## UART (PuTTY) Setup

1. Connect Micro USB cable from ZedBoard UART port to PC
2. Open Device Manager → find COM port number
3. PuTTY settings:
   - Connection type: Serial
   - Speed: 115200 baud
   - Data bits: 8, Stop bits: 1, Parity: None, Flow control: None

---

## Boot Switches

Set ZedBoard boot mode switches for JTAG programming:
- SW10: all OFF (JTAG mode)

---

## Verification Checklist

Before running the application:

- [ ] All three sensors visible in JA bus scan (`JA 0x40`, `0x48`, `0x68`)
- [ ] HDMI monitor shows display (blue background = PM build, red = baseline)
- [ ] PuTTY shows `BOOT COMPLETE — system running at minimum power`
- [ ] `INA219 init OK` printed at boot
- [ ] `MPU6500 WHO_AM_I=0x70` printed at boot
- [ ] `TMP102 @ 0x48` found in scan
- [ ] Power readings showing ~3800-3850 mW at room temperature
