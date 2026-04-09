/*
 * main.c  —  Zynq Low Power Sensor Interface + HDMI Display
 * ZedBoard / Zynq-7000  (Baremetal)
 *
 * Sensors on PMOD JA (axi_iic_0, shared I2C bus):
 *   INA219  @ 0x40  — power monitor (Vin+/Vin- on 12V adapter)
 *   TMP102  @ 0x48  — ambient temperature (primary DVFS trigger)
 *   MPU6500 @ 0x68  — 6-axis IMU (accel + gyro, AD0=GND)
 *
 * Clock architecture:
 *   FCLK0 = ~114MHz -> AXI fabric (DVFS target)
 *   FCLK1 = 50MHz   -> clk_wiz_0 MMCM (stable, isolated)
 *   FCLK3 = 74.25MHz-> pixel clock (VTC/VDMA, independent)
 *   clk_fast_w = 40MHz -> AXI interconnect
 *
 * Power management (inverted DVFS — low power by default):
 *   BOOT    : CPU=333MHz, FCLK0=~57MHz  — minimum power baseline
 *   T < 32C : COOL — 4s poll, CPU=333MHz, FCLK0=57MHz  (SLOW)
 *   T 32-35C: NORM — 2s poll, CPU=333MHz, FCLK0=57MHz  (SLOW)
 *   T > 35C : HOT  — 1s poll, CPU=666MHz, FCLK0=114MHz (FAST)
 *   WFI sleep between polls (CPU halted)
 *   Adaptive polling intervals
 *
 * Address map:
 *   axi_vdma_0   : 0x43000000
 *   axi_iic_0    : 0x41600000  (INA219 + TMP102 + MPU6500, JA)
 *   axi_iic_1    : 0x41610000  (ADV7511, HDMI)
 *   axi_gpio_0   : 0x41200000  (LEDs)
 *   xadc_wiz_0   : 0x43C00000
 *   v_tc_0       : 0x43C10000
 *   Frame buffer : 0x01000000
 */

#include "xparameters.h"
#include "xil_printf.h"
#include "xstatus.h"
#include "xiic.h"
#include "xgpio.h"
#include "xaxivdma.h"
#include "xvtc.h"
#include "xscutimer.h"
#include "xscugic.h"
#include "xil_exception.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "sleep.h"
#include <string.h>
#include <stdint.h>

/* ================================================================
 * Hardware addresses
 * ================================================================ */
#define FB_ADDR            0x01000000
#define DISP_W             1280
#define DISP_H              720
#define DISP_BPP           3
#define FB_SIZE            (DISP_W * DISP_H * DISP_BPP)
#define XADC_BASEADDR      0x43C00000
#define XADC_TEMP_REG      0x200
#define XADC_VCCINT_REG    0x204
#define ADV7511_ADDR       0x39

/* ================================================================
 * SLCR
 * ================================================================ */
#define SLCR_BASE          0xF8000000U
#define SLCR_LOCK_REG      (SLCR_BASE + 0x004U)
#define SLCR_UNLOCK_REG    (SLCR_BASE + 0x008U)
#define SLCR_FCLK0_CTRL    (SLCR_BASE + 0x170U)
#define SLCR_ARM_CLK_CTRL  (SLCR_BASE + 0x120U)
#define SLCR_LOCK_KEY      0x767BU
#define SLCR_UNLOCK_KEY    0xDF0DU
#define SLCR_FCLK_DIV0_SHIFT  8U
#define SLCR_FCLK_DIV0_MASK   (0x3FU << 8U)
#define SLCR_FCLK_DIV1_SHIFT  20U
#define SLCR_FCLK_DIV1_MASK   (0x3FU << 20U)
#define ARM_CLK_DIV_MASK   0x00003F00U
#define ARM_CLK_DIV_FAST   0x00000200U  /* DIVISOR=2 -> 666MHz */
#define ARM_CLK_DIV_MED    0x00000400U  /* DIVISOR=4 -> 333MHz */
#define ARM_CLK_DIV_ULTRA  0x00000800U  /* DIVISOR=8 -> 167MHz */
/* 3-level DVFS:
 * FAST : DIV1=2, DIV0=7  -> ~114MHz  (HOT  state)
 * MED  : DIV1=2, DIV0=14 -> ~57MHz   (NORM state)
 * ULTRA: DIV1=2, DIV0=20 -> ~40MHz   (COOL state) */
#define FCLK0_DIV1_FAST    2U
#define FCLK0_DIV0_FAST    7U
#define FCLK0_DIV1_MED     2U
#define FCLK0_DIV0_MED    14U
#define FCLK0_DIV1_ULTRA   2U
#define FCLK0_DIV0_ULTRA  20U

/* ================================================================
 * INA219 (0x40)
 * ================================================================ */
#define INA219_ADDR        0x40
#define INA219_REG_CONFIG  0x00
#define INA219_REG_BUS     0x02
#define INA219_REG_POWER   0x03
#define INA219_REG_CALIB   0x05
#define INA219_CONFIG_VAL  0x3FFF
#define INA219_CALIB_VAL   4481

/* ================================================================
 * TMP102 (0x48)
 * ================================================================ */
#define TMP102_ADDR_MIN    0x48
#define TMP102_ADDR_MAX    0x4B
#define TMP102_REG_TEMP    0x00

/* ================================================================
 * MPU-6500 (0x68, AD0=GND)
 * ================================================================ */
#define MPU_ADDR           0x68
#define MPU_REG_WHOAMI     0x75   /* should return 0x70 (MPU6500) or 0x71 */
#define MPU_REG_PWR_MGMT1  0x6B
#define MPU_REG_ACCEL_CFG  0x1C   /* accel full scale */
#define MPU_REG_GYRO_CFG   0x1B   /* gyro full scale  */
#define MPU_REG_ACCEL_X_H  0x3B   /* first of 6 accel bytes */
#define MPU_REG_GYRO_X_H   0x43   /* first of 6 gyro bytes  */
#define MPU_REG_TEMP_H     0x41   /* MPU internal temp       */
/* Accel scale: ±2g -> 16384 LSB/g */
#define MPU_ACCEL_SCALE    16384
/* Gyro  scale: ±250°/s -> 131 LSB/°/s */
#define MPU_GYRO_SCALE     131
/* Motion threshold: if |total_accel - 1g| > this (in 0.01g units) -> motion */
#define MPU_MOTION_THRESH  15  /* 0.15g deviation from gravity */

/* ================================================================
 * Device IDs
 * ================================================================ */
#define VDMA_DEVICE_ID       XPAR_AXI_VDMA_0_DEVICE_ID
#define IIC_DEVICE_ID        XPAR_AXI_IIC_0_DEVICE_ID
#define IIC1_DEVICE_ID       XPAR_AXI_IIC_1_DEVICE_ID
#define GPIO_LED_DEVICE_ID   XPAR_AXI_GPIO_0_DEVICE_ID
#define VTC_DEVICE_ID        XPAR_V_TC_0_DEVICE_ID
#define TIMER_DEVICE_ID      XPAR_XSCUTIMER_0_DEVICE_ID
#define GIC_DEVICE_ID        XPAR_SCUGIC_SINGLE_DEVICE_ID
#define TIMER_IRQ_ID         XPAR_SCUTIMER_INTR

/* ================================================================
 * Thermal / DVFS parameters
 * INVERTED DVFS: system boots SLOW, scales UP when HOT
 * ================================================================ */
#define WINDOW_SIZE            16
#define FIR_TAPS               11
#define ANOMALY_FACTOR_X100   200
/* COOL < 32C : 4s poll, SLOW clocks — minimum power
 * NORM 32-35C: 2s poll, SLOW clocks — moderate
 * HOT  > 35C : 1s poll, FAST clocks — full performance */
#define DVFS_THRESHOLD_C_X100  3500   /* 35C -> enter NORM    */
#define DVFS_HOT_C_X100        4000   /* 40C -> FAST clocks   */
#define DVFS_HYST_C_X100        200   /* 2C  hysteresis       */
#define XADC_HOT_C_X100        6500
#define XADC_NOT_READY         (0x7FFFFFFF)
#define POLL_COOL_S            4
#define POLL_NORM_S            2
#define POLL_HOT_S             1
#define HDMI_REFRESH_POLLS     8

/* LED bits */
#define LED_DVFS_FAST_BIT  0x01   /* lit when running FAST */
#define LED_MOTION_BIT     0x02   /* lit on motion detect  */
#define LED_EVENT_BIT      0x04
#define LED_XADC_HOT_BIT   0x08

/* ================================================================
 * Thermal state
 * ================================================================ */
typedef enum { THERMAL_COOL=0, THERMAL_NORM=1, THERMAL_HOT=2 } ThermalState;

/* ================================================================
 * Colours RGB24
 * ================================================================ */
#define COL_BG_IDLE    0x00001A33
#define COL_BG_EVENT   0x00331A00
#define COL_BG_MOTION  0x00002200
#define COL_BG_HOT     0x00330000
#define COL_PANEL      0x00002244
#define COL_BORDER     0x000055AA
#define COL_WHITE      0x00FFFFFF
#define COL_YELLOW     0x00FFDD00
#define COL_GREEN      0x0000FF88
#define COL_RED        0x00FF4444
#define COL_CYAN       0x0000FFFF
#define COL_ORANGE     0x00FF8800
#define COL_PURPLE     0x00AA44FF

/* ================================================================
 * Font 5x7
 * ================================================================ */
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},
    {0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},
    {0x08,0x2A,0x1C,0x2A,0x08},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},
    {0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
    {0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x00,0x08,0x14,0x22,0x41},{0x14,0x14,0x14,0x14,0x14},
    {0x41,0x22,0x14,0x08,0x00},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},
    {0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},
    {0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x41,0x49,0x3A},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x04,0x02,0x7F},
    {0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},
    {0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},{0x03,0x04,0x78,0x04,0x03},
    {0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},
    {0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
    {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},
    {0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
    {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},
    {0x08,0x7E,0x09,0x01,0x02},{0x08,0x14,0x54,0x54,0x3C},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},
    {0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
    {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},
    {0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x40,0x3C},
    {0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},
    {0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},
    {0x08,0x08,0x2A,0x1C,0x08},
};

static const int32_t fir_coeffs[FIR_TAPS] = {
    9,35,84,159,223,250,223,159,84,35,9
};

/* ================================================================
 * Globals
 * ================================================================ */
static XIic      Iic, Iic1;
static XGpio     GpioLed;
static XScuTimer Timer;
static XScuGic   Gic;
static XAxiVdma  Vdma;
static XVtc      Vtc;

static volatile int timer_fired = 0;
static uint8_t  *fb = (uint8_t *)FB_ADDR;

/* Temperature window */
static int32_t   sample_window[WINDOW_SIZE];
static int       sample_count    = 0;
static int       window_head     = 0;
static u32       g_elapsed_s     = 0;

/* State */
static ThermalState g_thermal_state    = THERMAL_COOL;
static int          g_dvfs_fast        = 0;   /* 0=not FAST */
static int          g_dvfs_level       = 0;   /* 0=ULTRA, 1=MED, 2=FAST */
static int          g_cpu_fast         = 0;   /* 0=333MHz, 1=666MHz */
static ThermalState g_last_drawn_state = THERMAL_COOL;
static int          g_display_counter  = 0;
static int16_t      g_last_drawn_raw   = 0x7FFF;
static int32_t      g_power_mw         = -1;
static int32_t      g_vccint_mv        = -1;
static int16_t      g_last_uart_raw    = 0x7FFF;
static ThermalState g_last_uart_state  = THERMAL_COOL;

/* MPU-6500 data */
static int16_t  g_ax=0, g_ay=0, g_az=0;   /* raw accel */
static int16_t  g_gx=0, g_gy=0, g_gz=0;   /* raw gyro  */
static int      g_motion = 0;              /* 1=motion detected */
static int      g_mpu_ok = 0;             /* 1=MPU init succeeded */

/* ================================================================
 * Timer / WFI
 * ================================================================ */
static void timer_isr(void *unused)
{
    (void)unused;
    XScuTimer_ClearInterruptStatus(&Timer);
    timer_fired = 1;
}

static void wfi_sleep(u32 seconds)
{
    u32 freq = XPAR_CPU_CORTEXA9_0_CPU_CLK_FREQ_HZ / 2;
    u32 load = freq * seconds - 1;
    timer_fired = 0;
    XScuTimer_Stop(&Timer);
    XScuTimer_DisableAutoReload(&Timer);
    XScuTimer_LoadTimer(&Timer, load);
    XScuTimer_EnableInterrupt(&Timer);
    XScuTimer_Start(&Timer);
    while (!timer_fired) __asm__ volatile ("wfi");
    XScuTimer_Stop(&Timer);
    XScuTimer_DisableInterrupt(&Timer);
}

static int gic_timer_init(void)
{
    XScuGic_Config *gc = XScuGic_LookupConfig(GIC_DEVICE_ID);
    if (!gc) return XST_FAILURE;
    int s = XScuGic_CfgInitialize(&Gic, gc, gc->CpuBaseAddress);
    if (s != XST_SUCCESS) return s;
    s = XScuGic_Connect(&Gic, TIMER_IRQ_ID,
                        (Xil_ExceptionHandler)timer_isr, NULL);
    if (s != XST_SUCCESS) return s;
    XScuGic_Enable(&Gic, TIMER_IRQ_ID);
    XScuTimer_Config *tc = XScuTimer_LookupConfig(TIMER_DEVICE_ID);
    if (!tc) return XST_FAILURE;
    s = XScuTimer_CfgInitialize(&Timer, tc, tc->BaseAddr);
    if (s != XST_SUCCESS) return s;
    XScuTimer_DisableAutoReload(&Timer);
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_IRQ_INT,
                     (Xil_ExceptionHandler)XScuGic_InterruptHandler, &Gic);
    Xil_ExceptionEnable();
    return XST_SUCCESS;
}

/* ================================================================
 * SLCR helpers
 * ================================================================ */
static inline void slcr_unlock(void) { Xil_Out32(SLCR_UNLOCK_REG, SLCR_UNLOCK_KEY); }
static inline void slcr_lock(void)   { Xil_Out32(SLCR_LOCK_REG,   SLCR_LOCK_KEY);   }

static void fclk0_set_freq(u32 div1, u32 div0)
{
    slcr_unlock();
    u32 reg = Xil_In32(SLCR_FCLK0_CTRL);
    reg &= ~(SLCR_FCLK_DIV1_MASK | SLCR_FCLK_DIV0_MASK);
    reg |= ((div1 & 0x3FU) << SLCR_FCLK_DIV1_SHIFT)
         | ((div0 & 0x3FU) << SLCR_FCLK_DIV0_SHIFT);
    Xil_Out32(SLCR_FCLK0_CTRL, reg);
    slcr_lock();
    usleep(10);
}

static void cpu_set_level(int level, u32 div, const char *label)
{
    slcr_unlock();
    u32 reg = (Xil_In32(SLCR_ARM_CLK_CTRL) & ~ARM_CLK_DIV_MASK) | div;
    Xil_Out32(SLCR_ARM_CLK_CTRL, reg);
    slcr_lock();
    g_cpu_fast = (level == 2);
    xil_printf("[CPU] -> %s\r\n", label);
}

/* 3-level inverted DVFS:
 * COOL (<35C): CPU=167MHz, FCLK0=~40MHz  — ULTRA low power
 * NORM (35-40C):CPU=333MHz, FCLK0=~57MHz — medium
 * HOT  (>40C): CPU=666MHz, FCLK0=~114MHz — full performance */
static void dvfs_update(int32_t t_cx100)
{
    ThermalState new_state;
    if      (t_cx100 >= DVFS_HOT_C_X100)       new_state = THERMAL_HOT;
    else if (t_cx100 >= DVFS_THRESHOLD_C_X100)  new_state = THERMAL_NORM;
    else                                         new_state = THERMAL_COOL;

    /* Hysteresis on downward transitions */
    if (new_state < g_thermal_state) {
        int32_t hyst = (g_thermal_state == THERMAL_HOT)
                     ? DVFS_HOT_C_X100       - DVFS_HYST_C_X100
                     : DVFS_THRESHOLD_C_X100 - DVFS_HYST_C_X100;
        if (t_cx100 > hyst) new_state = g_thermal_state;
    }
    if (new_state == g_thermal_state) return;
    g_thermal_state = new_state;

    if (new_state == THERMAL_HOT) {
        /* Scale UP — maximum performance */
        cpu_set_level(2, ARM_CLK_DIV_FAST, "666MHz");
        fclk0_set_freq(FCLK0_DIV1_FAST, FCLK0_DIV0_FAST);
        g_dvfs_fast = 1; g_dvfs_level = 2;
        xil_printf("[DVFS] L2 FAST:  FCLK0=~114MHz CPU=666MHz  (HOT >40C)\r\n");
    } else if (new_state == THERMAL_NORM) {
        /* Medium — balanced */
        cpu_set_level(1, ARM_CLK_DIV_MED, "333MHz");
        fclk0_set_freq(FCLK0_DIV1_MED, FCLK0_DIV0_MED);
        g_dvfs_fast = 0; g_dvfs_level = 1;
        xil_printf("[DVFS] L1 MED:   FCLK0=~57MHz  CPU=333MHz  (NORM 35-40C)\r\n");
    } else {
        /* COOL — ultra low power */
        fclk0_set_freq(FCLK0_DIV1_ULTRA, FCLK0_DIV0_ULTRA);
        cpu_set_level(0, ARM_CLK_DIV_ULTRA, "167MHz");
        g_dvfs_fast = 0; g_dvfs_level = 0;
        xil_printf("[DVFS] L0 ULTRA: FCLK0=~40MHz  CPU=167MHz  (COOL <35C)\r\n");
    }

    u32 leds = XGpio_DiscreteRead(&GpioLed, 1);
    XGpio_DiscreteWrite(&GpioLed, 1,
        g_dvfs_fast ? (leds|LED_DVFS_FAST_BIT) : (leds&~LED_DVFS_FAST_BIT));
}

static u32 poll_interval_s(void)
{
    switch (g_thermal_state) {
        case THERMAL_COOL: return POLL_COOL_S;
        case THERMAL_HOT:  return POLL_HOT_S;
        default:           return POLL_NORM_S;
    }
}

/* ================================================================
 * XADC
 * ================================================================ */
static int32_t read_xadc_c_x100(void)
{
    volatile uint32_t *x = (volatile uint32_t *)XADC_BASEADDR;
    uint16_t r = (uint16_t)(x[XADC_TEMP_REG/4] >> 4) & 0x0FFF;
    if (!r) return XADC_NOT_READY;
    return ((int32_t)r * 50398) / 4096 - 27315;
}

static int32_t read_xadc_vccint_mv(void)
{
    volatile uint32_t *x = (volatile uint32_t *)XADC_BASEADDR;
    uint16_t r = (uint16_t)(x[XADC_VCCINT_REG/4] >> 4) & 0x0FFF;
    if (!r) return -1;
    return ((int32_t)r * 3000) / 4096;
}

/* ================================================================
 * Utilities
 * ================================================================ */
static int     abs32(int32_t x) { return x<0?(int)-x:(int)x; }
static int32_t r12_to_cx100(int16_t r) { return ((int32_t)r*625)/100; }
static int32_t isqrt(int32_t n)
{
    if(n<=0) return 0;
    int32_t x=n,y=1;
    while(x>y){x=(x+y)/2;y=n/x;}
    return x;
}
static void i32_to_str(char *b, int32_t v, int dp)
{
    int i=0;
    if(v<0){b[i++]='-';v=-v;}
    if(dp==2){
        int w=v/100,f=v%100;
        if(!w) b[i++]='0';
        else{char t[8];int ti=0,q=w;
             while(q>0){t[ti++]='0'+(q%10);q/=10;}
             for(int j=ti-1;j>=0;j--) b[i++]=t[j];}
        b[i++]='.';b[i++]='0'+(f/10);b[i++]='0'+(f%10);
    } else {
        if(!v) b[i++]='0';
        else{char t[12];int ti=0,q=v;
             while(q>0){t[ti++]='0'+(q%10);q/=10;}
             for(int j=ti-1;j>=0;j--) b[i++]=t[j];}
    }
    b[i]='\0';
}

static void fmt_time(char *buf, u32 secs)
{
    u32 h=secs/3600, m=(secs%3600)/60, s=secs%60;
    buf[0]='0'+(h/10);buf[1]='0'+(h%10);buf[2]=':';
    buf[3]='0'+(m/10);buf[4]='0'+(m%10);buf[5]=':';
    buf[6]='0'+(s/10);buf[7]='0'+(s%10);buf[8]='\0';
}

/* ================================================================
 * IIC0 init (shared bus: TMP102 + INA219 + MPU6500)
 * ================================================================ */
static int iic0_init(void)
{
    XIic_Config *c = XIic_LookupConfig(IIC_DEVICE_ID);
    if(!c) return XST_FAILURE;
    int s = XIic_CfgInitialize(&Iic, c, c->BaseAddress);
    if(s!=XST_SUCCESS) return s;
    XIic_Reset(&Iic);
    return XIic_Start(&Iic);
}

/* Generic I2C write register */
static int i2c_write_reg(uint8_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return (XIic_Send(Iic.BaseAddress, dev, buf, 2, XIIC_STOP)==2)
           ? XST_SUCCESS : XST_FAILURE;
}

/* Generic I2C read N bytes from register */
static int i2c_read_regs(uint8_t dev, uint8_t reg, uint8_t *out, int len)
{
    if(XIic_Send(Iic.BaseAddress, dev, &reg, 1, XIIC_STOP)!=1)
        return XST_FAILURE;
    usleep(1000);
    if(XIic_Recv(Iic.BaseAddress, dev, out, len, XIIC_STOP)!=len)
        return XST_FAILURE;
    return XST_SUCCESS;
}

/* ================================================================
 * INA219
 * ================================================================ */
static int ina219_write_reg(uint8_t reg, uint16_t val)
{
    uint8_t buf[3]={reg,(uint8_t)(val>>8),(uint8_t)(val&0xFF)};
    int s=XIic_Send(Iic.BaseAddress,INA219_ADDR,buf,3,XIIC_STOP);
    usleep(2000);
    return (s==3)?XST_SUCCESS:XST_FAILURE;
}

static int ina219_read_reg(uint8_t reg, int16_t *out)
{
    uint8_t rx[2];
    if(XIic_Send(Iic.BaseAddress,INA219_ADDR,&reg,1,XIIC_STOP)!=1)
        return XST_FAILURE;
    usleep(2000);
    if(XIic_Recv(Iic.BaseAddress,INA219_ADDR,rx,2,XIIC_STOP)!=2)
        return XST_FAILURE;
    *out=(int16_t)((rx[0]<<8)|rx[1]);
    return XST_SUCCESS;
}

static int ina219_init(void)
{
    if(ina219_write_reg(INA219_REG_CALIB,  INA219_CALIB_VAL) !=XST_SUCCESS) return XST_FAILURE;
    if(ina219_write_reg(INA219_REG_CONFIG, INA219_CONFIG_VAL)!=XST_SUCCESS) return XST_FAILURE;
    usleep(5000);
    xil_printf("INA219 init OK\r\n");
    return XST_SUCCESS;
}

static int32_t ina219_read_power_mw(void)
{
    int16_t raw, vraw;
    if(ina219_read_reg(INA219_REG_POWER,&raw) !=XST_SUCCESS) return -1;
    if(ina219_read_reg(INA219_REG_BUS,  &vraw)!=XST_SUCCESS) return -1;
    int32_t vbus_mv=((vraw>>3)*4);
    if(vbus_mv<1000||vbus_mv>20000) return -1;
    return (int32_t)raw*2;
}

/* ================================================================
 * TMP102
 * ================================================================ */
static int tmp102_read(uint8_t addr, int16_t *out)
{
    uint8_t reg=TMP102_REG_TEMP, rx[2];
    if(XIic_Send(Iic.BaseAddress,addr,&reg,1,XIIC_STOP)!=1) return XST_FAILURE;
    if(XIic_Recv(Iic.BaseAddress,addr,rx,2,XIIC_STOP)!=2)   return XST_FAILURE;
    *out=(int16_t)(((int16_t)((rx[0]<<8)|rx[1]))>>4);
    return XST_SUCCESS;
}

static int tmp102_probe(uint8_t a)
{
    uint8_t r=TMP102_REG_TEMP;
    return XIic_Send(Iic.BaseAddress,a,&r,1,XIIC_STOP)==1
           ? XST_SUCCESS : XST_FAILURE;
}

/* ================================================================
 * MPU-6500
 * ================================================================ */
static int mpu6500_init(void)
{
    /* Check WHO_AM_I */
    uint8_t whoami = 0;
    if(i2c_read_regs(MPU_ADDR, MPU_REG_WHOAMI, &whoami, 1)!=XST_SUCCESS){
        xil_printf("MPU6500: no response on I2C\r\n");
        return XST_FAILURE;
    }
    xil_printf("MPU6500: WHO_AM_I=0x%02X ", (unsigned)whoami);
    if(whoami==0x70)      xil_printf("(MPU-6500 OK)\r\n");
    else if(whoami==0x71) xil_printf("(MPU-9250 OK)\r\n");
    else if(whoami==0x68) xil_printf("(MPU-6050 OK)\r\n");
    else                  xil_printf("(unknown — proceeding)\r\n");

    /* Wake up — clear sleep bit */
    if(i2c_write_reg(MPU_ADDR, MPU_REG_PWR_MGMT1, 0x00)!=XST_SUCCESS){
        xil_printf("MPU6500: wake failed\r\n");
        return XST_FAILURE;
    }
    usleep(100000);  /* 100ms startup */

    /* Accel: ±2g (default, highest sensitivity) */
    i2c_write_reg(MPU_ADDR, MPU_REG_ACCEL_CFG, 0x00);
    /* Gyro:  ±250°/s (default) */
    i2c_write_reg(MPU_ADDR, MPU_REG_GYRO_CFG,  0x00);
    usleep(10000);

    xil_printf("MPU6500 init OK\r\n");
    return XST_SUCCESS;
}

static int mpu6500_read(void)
{
    uint8_t buf[14];
    /* Read 14 bytes: accel(6) + temp(2) + gyro(6) starting at ACCEL_X_H */
    if(i2c_read_regs(MPU_ADDR, MPU_REG_ACCEL_X_H, buf, 14)!=XST_SUCCESS)
        return XST_FAILURE;

    g_ax = (int16_t)((buf[0]<<8)|buf[1]);
    g_ay = (int16_t)((buf[2]<<8)|buf[3]);
    g_az = (int16_t)((buf[4]<<8)|buf[5]);
    /* buf[6..7] = internal temp, skip */
    g_gx = (int16_t)((buf[8] <<8)|buf[9]);
    g_gy = (int16_t)((buf[10]<<8)|buf[11]);
    g_gz = (int16_t)((buf[12]<<8)|buf[13]);

    /* Motion detection:
     * At rest: total accel magnitude = 1g = 16384 LSB
     * Motion: deviation > threshold */
    int32_t ax2 = (int32_t)g_ax*(int32_t)g_ax;
    int32_t ay2 = (int32_t)g_ay*(int32_t)g_ay;
    int32_t az2 = (int32_t)g_az*(int32_t)g_az;
    /* isqrt in LSB units, compare to 1g = 16384 */
    int32_t mag = isqrt(ax2/256 + ay2/256 + az2/256) * 16;
    int32_t dev_g100 = abs32(mag - 16384) * 100 / 16384; /* deviation in 0.01g */
    g_motion = (dev_g100 > MPU_MOTION_THRESH);
    return XST_SUCCESS;
}

/* ================================================================
 * IIC1 — ADV7511
 * ================================================================ */
static int iic1_init(void)
{
    XIic_Config *c = XIic_LookupConfig(IIC1_DEVICE_ID);
    if(!c){xil_printf("WARN: axi_iic_1 not found\r\n");return XST_FAILURE;}
    int s = XIic_CfgInitialize(&Iic1,c,c->BaseAddress);
    if(s!=XST_SUCCESS) return s;
    XIic_Reset(&Iic1);
    return XIic_Start(&Iic1);
}

static int adv7511_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2]={reg,val};
    int n=XIic_Send(Iic1.BaseAddress,ADV7511_ADDR,buf,2,XIIC_STOP);
    usleep(2000);
    return (n==2)?XST_SUCCESS:XST_FAILURE;
}

static uint8_t adv7511_read(uint8_t reg)
{
    uint8_t val=0xFF;
    if(XIic_Send(Iic1.BaseAddress,ADV7511_ADDR,&reg,1,XIIC_REPEATED_START)!=1)
        return 0xFF;
    usleep(1000);
    XIic_Recv(Iic1.BaseAddress,ADV7511_ADDR,&val,1,XIIC_STOP);
    usleep(1000);
    return val;
}

static void adv7511_init(void)
{
    xil_printf("ADV7511 init...\r\n");
    uint8_t rev=adv7511_read(0x00);
    xil_printf("  rev=0x%02X\r\n",rev);
    if(rev==0xFF){xil_printf("  not responding\r\n");return;}
    adv7511_write(0x41,0x10); usleep(200000);
    adv7511_write(0xD6,0x48); usleep(50000);
    adv7511_write(0x98,0x03); adv7511_write(0x9A,0xE0);
    adv7511_write(0x9C,0x30); adv7511_write(0x9D,0x61);
    adv7511_write(0xA2,0xA4); adv7511_write(0xA3,0xA4);
    adv7511_write(0xE0,0xD0); adv7511_write(0xF9,0x00);
    adv7511_write(0x15,0x01); adv7511_write(0x16,0x38);
    adv7511_write(0x17,0x02); adv7511_write(0x18,0x46);
    adv7511_write(0x48,0x08); adv7511_write(0x55,0x00);
    adv7511_write(0x56,0x28); adv7511_write(0xAF,0x16);
    adv7511_write(0x3C,0x04); adv7511_write(0x3B,0x07);
    adv7511_write(0x40,0x80); adv7511_write(0x4B,0x80);
    adv7511_write(0x4C,0x04); adv7511_write(0x0A,0x00);
    adv7511_write(0x0B,0x00); adv7511_write(0x0C,0x00);
    adv7511_write(0x41,0x10); usleep(200000);
    xil_printf("  0xAF=0x%02X (want 0x16)\r\n",adv7511_read(0xAF));
    uint8_t st=adv7511_read(0x42);
    xil_printf("  HPD: %s\r\n",(st&0x40)?"YES":"NO (override active)");
    xil_printf("ADV7511 done\r\n");
}

/* ================================================================
 * VDMA
 * ================================================================ */
static int vdma_init(void)
{
    XAxiVdma_Config *cfg=XAxiVdma_LookupConfig(VDMA_DEVICE_ID);
    if(!cfg){xil_printf("VDMA: not found\r\n");return XST_FAILURE;}
    int s=XAxiVdma_CfgInitialize(&Vdma,cfg,cfg->BaseAddress);
    if(s!=XST_SUCCESS){xil_printf("VDMA: init %d\r\n",s);return s;}
    XAxiVdma_DmaSetup rd;
    memset(&rd,0,sizeof(rd));
    rd.VertSizeInput=DISP_H; rd.HoriSizeInput=DISP_W*DISP_BPP;
    rd.Stride=DISP_W*DISP_BPP; rd.FrameDelay=0; rd.EnableCircularBuf=1;
    s=XAxiVdma_DmaConfig(&Vdma,XAXIVDMA_READ,&rd);
    if(s!=XST_SUCCESS){xil_printf("VDMA: DmaConfig %d\r\n",s);return s;}
    UINTPTR addrs[3]={(UINTPTR)FB_ADDR,(UINTPTR)FB_ADDR,(UINTPTR)FB_ADDR};
    s=XAxiVdma_DmaSetBufferAddr(&Vdma,XAXIVDMA_READ,addrs);
    if(s!=XST_SUCCESS){xil_printf("VDMA: SetBuf %d\r\n",s);return s;}
    s=XAxiVdma_DmaStart(&Vdma,XAXIVDMA_READ);
    if(s!=XST_SUCCESS){xil_printf("VDMA: Start %d\r\n",s);return s;}
    xil_printf("VDMA: OK\r\n");
    return XST_SUCCESS;
}

/* ================================================================
 * Drawing
 * ================================================================ */
static void put_pixel(int x,int y,uint32_t rgb)
{
    if(x<0||x>=DISP_W||y<0||y>=DISP_H) return;
    int o=(y*DISP_W+x)*DISP_BPP;
    fb[o]=(rgb>>16)&0xFF;fb[o+1]=(rgb>>8)&0xFF;fb[o+2]=rgb&0xFF;
}
static void fill_rect(int x,int y,int w,int h,uint32_t c)
{ for(int r=y;r<y+h;r++) for(int j=x;j<x+w;j++) put_pixel(j,r,c); }
static void draw_char(int px,int py,char c,uint32_t fg,uint32_t bg,int sc)
{
    if(c<32||c>126) c='?';
    const uint8_t *g=font5x7[c-32];
    for(int col=0;col<5;col++)
        for(int row=0;row<7;row++)
            fill_rect(px+col*sc,py+row*sc,sc,sc,(g[col]&(1<<row))?fg:bg);
}
static void draw_str(int px,int py,const char *s,uint32_t fg,uint32_t bg,int sc)
{ int x=px; while(*s){draw_char(x,py,*s,fg,bg,sc);x+=(5+1)*sc;s++;} }
static void draw_bar(int x,int y,int w,int h,
                     int32_t v,int32_t mn,int32_t mx,uint32_t fc,uint32_t bc)
{
    fill_rect(x,y,w,h,bc);
    int rng=mx-mn; if(rng<=0) rng=1;
    int fw=(int)((int64_t)(v-mn)*w/rng);
    if(fw<0) fw=0; if(fw>w) fw=w;
    fill_rect(x,y,fw,h,fc);
}
static void fb_flush(void)
{ Xil_DCacheFlushRange((UINTPTR)fb,FB_SIZE); }

/* ================================================================
 * HDMI Dashboard — 3-column layout:
 *   Left:   Temperature (TMP102 + XADC)
 *   Centre: MPU-6500 accelerometer + gyroscope
 *   Right:  Power management status
 * ================================================================ */
static void hdmi_draw(int32_t ambient, int32_t die,
                      int32_t mean, int32_t stddev,
                      int32_t t_min, int32_t t_max,
                      int32_t slope, int32_t fir_out,
                      int anomaly, int is_event, u32 poll_s)
{
    char b[64], b2[32];
    uint32_t bg = g_motion ? COL_BG_MOTION :
                  (g_thermal_state==THERMAL_HOT) ? COL_BG_HOT :
                  is_event ? COL_BG_EVENT : COL_BG_IDLE;

    fill_rect(0,0,DISP_W,DISP_H,bg);

    /* Title bar */
    fill_rect(0,0,DISP_W,56,COL_PANEL);
    fill_rect(0,54,DISP_W,2,COL_BORDER);
    draw_str(20,14,"ZYNQ-7000  LOW POWER SENSOR INTERFACE  ZedBoard",
             COL_YELLOW,COL_PANEL,2);
    const char *state_str = g_motion ? "MOTION" :
                            anomaly  ? "ANOMALY" :
                            is_event ? "EVENT" : "IDLE";
    uint32_t sc2 = g_motion  ? COL_GREEN :
                   anomaly   ? COL_RED :
                   is_event  ? COL_ORANGE : COL_WHITE;
    draw_str(DISP_W-200,16,state_str,sc2,COL_PANEL,2);

    /* ── LEFT COLUMN: Temperature ── */
    int lx=14, ly=68;
    draw_str(lx,ly,"TEMPERATURE",COL_CYAN,bg,2); ly+=26;

    draw_str(lx,ly,"Ambient:",COL_WHITE,bg,2);
    i32_to_str(b,ambient,2); strcat(b," C");
    draw_str(lx+190,ly,b,COL_YELLOW,bg,2); ly+=20;
    draw_bar(lx,ly,380,10,ambient,2000,5000,COL_ORANGE,COL_PANEL); ly+=18;

    draw_str(lx,ly,"Die Tmp:",COL_WHITE,bg,2);
    if(die!=XADC_NOT_READY){i32_to_str(b,die,2);strcat(b," C");}
    else strcpy(b,"N/A");
    draw_str(lx+190,ly,b,COL_CYAN,bg,2); ly+=20;
    if(die!=XADC_NOT_READY)
        draw_bar(lx,ly,380,10,die,3000,8500,COL_RED,COL_PANEL);
    ly+=18;

    draw_str(lx,ly,"FIR Out:",COL_WHITE,bg,2);
    i32_to_str(b,fir_out,2); strcat(b," C");
    draw_str(lx+190,ly,b,COL_WHITE,bg,2); ly+=20;

    draw_str(lx,ly,"Mean:   ",COL_WHITE,bg,2);
    i32_to_str(b,mean,2);
    draw_str(lx+190,ly,b,COL_WHITE,bg,2); ly+=20;

    draw_str(lx,ly,"Min/Max:",COL_WHITE,bg,2);
    i32_to_str(b,t_min,2); i32_to_str(b2,t_max,2);
    strcat(b,"/"); strcat(b,b2);
    draw_str(lx+190,ly,b,COL_WHITE,bg,2); ly+=20;

    /* Trend */
    const char *tr=(slope>10)?"WARMING":(slope<-10)?"COOLING":"STABLE";
    uint32_t tc=(slope>10)?COL_RED:(slope<-10)?COL_GREEN:COL_WHITE;
    draw_str(lx,ly,"Trend:  ",COL_WHITE,bg,2);
    b[0]=(slope>=0)?'+':'-'; b[1]='\0';
    i32_to_str(b+1,abs32(slope),2); strcat(b,"C/m");
    draw_str(lx+190,ly,b,tc,bg,2); ly+=20;
    draw_str(lx+190,ly,tr,tc,bg,2); ly+=26;

    /* Thermal state */
    draw_str(lx,ly,"State:  ",COL_WHITE,bg,2);
    const char *sname=(g_thermal_state==THERMAL_COOL)?"COOL":
                      (g_thermal_state==THERMAL_HOT) ?"HOT ":"NORM";
    uint32_t scol=(g_thermal_state==THERMAL_COOL)?COL_GREEN:
                  (g_thermal_state==THERMAL_HOT) ?COL_RED:COL_ORANGE;
    draw_str(lx+190,ly,sname,scol,bg,2); ly+=20;

    draw_str(lx,ly,"Poll:   ",COL_WHITE,bg,2);
    b[0]='0'+(char)poll_s; b[1]='s'; b[2]='\0';
    draw_str(lx+190,ly,b,COL_WHITE,bg,2);

    /* ── CENTRE COLUMN: MPU-6500 ── */
    int mx2=430, my=68;
    draw_str(mx2,my,"MPU-6500  IMU",COL_PURPLE,bg,2); my+=26;

    if(g_mpu_ok){
        /* Accel values in 0.01g units */
        int32_t ax_g = (int32_t)g_ax*100/MPU_ACCEL_SCALE;
        int32_t ay_g = (int32_t)g_ay*100/MPU_ACCEL_SCALE;
        int32_t az_g = (int32_t)g_az*100/MPU_ACCEL_SCALE;

        draw_str(mx2,my,"ACCELEROMETER (g)",COL_CYAN,bg,2); my+=22;
        draw_str(mx2,my,"Ax:",COL_WHITE,bg,2);
        b[0]=ax_g>=0?'+':'-'; i32_to_str(b+1,abs32(ax_g),2);
        draw_str(mx2+60,my,b,COL_YELLOW,bg,2); my+=18;
        draw_bar(mx2,my,360,10,ax_g+200,-200,200,COL_YELLOW,COL_PANEL); my+=16;

        draw_str(mx2,my,"Ay:",COL_WHITE,bg,2);
        b[0]=ay_g>=0?'+':'-'; i32_to_str(b+1,abs32(ay_g),2);
        draw_str(mx2+60,my,b,COL_ORANGE,bg,2); my+=18;
        draw_bar(mx2,my,360,10,ay_g+200,-200,200,COL_ORANGE,COL_PANEL); my+=16;

        draw_str(mx2,my,"Az:",COL_WHITE,bg,2);
        b[0]=az_g>=0?'+':'-'; i32_to_str(b+1,abs32(az_g),2);
        draw_str(mx2+60,my,b,COL_CYAN,bg,2); my+=18;
        draw_bar(mx2,my,360,10,az_g+200,-200,200,COL_CYAN,COL_PANEL); my+=22;

        draw_str(mx2,my,"GYROSCOPE (dps)",COL_CYAN,bg,2); my+=22;
        int32_t gx_d=(int32_t)g_gx/MPU_GYRO_SCALE;
        int32_t gy_d=(int32_t)g_gy/MPU_GYRO_SCALE;
        int32_t gz_d=(int32_t)g_gz/MPU_GYRO_SCALE;

        draw_str(mx2,my,"Gx:",COL_WHITE,bg,2);
        b[0]=gx_d>=0?'+':'-'; i32_to_str(b+1,abs32(gx_d),0);
        draw_str(mx2+60,my,b,COL_YELLOW,bg,2); my+=18;

        draw_str(mx2,my,"Gy:",COL_WHITE,bg,2);
        b[0]=gy_d>=0?'+':'-'; i32_to_str(b+1,abs32(gy_d),0);
        draw_str(mx2+60,my,b,COL_ORANGE,bg,2); my+=18;

        draw_str(mx2,my,"Gz:",COL_WHITE,bg,2);
        b[0]=gz_d>=0?'+':'-'; i32_to_str(b+1,abs32(gz_d),0);
        draw_str(mx2+60,my,b,COL_CYAN,bg,2); my+=26;

        /* Motion indicator */
        draw_str(mx2,my,"Motion: ",COL_WHITE,bg,2);
        draw_str(mx2+190,my,g_motion?"DETECTED":"None",
                 g_motion?COL_GREEN:COL_WHITE,bg,2);
    } else {
        draw_str(mx2,my+20,"MPU-6500 not found",COL_RED,bg,2);
        draw_str(mx2,my+44,"Check AD0=GND",COL_ORANGE,bg,2);
        draw_str(mx2,my+68,"CS=VCC (if module)",COL_ORANGE,bg,2);
    }

    /* ── RIGHT COLUMN: Power Management ── */
    int rx=860, ry=68;
    draw_str(rx,ry,"POWER MANAGEMENT",COL_CYAN,bg,2); ry+=26;

    draw_str(rx,ry,"Mode:  ",COL_WHITE,bg,2);
    draw_str(rx+190,ry,g_dvfs_level==2?"FAST (HOT)":g_dvfs_level==1?"MED (NORM)":"ULTRA (COOL)",
             g_dvfs_level==2?COL_RED:g_dvfs_level==1?COL_ORANGE:COL_GREEN,bg,2); ry+=22;

    draw_str(rx,ry,"FCLK0: ",COL_WHITE,bg,2);
    draw_str(rx+190,ry,g_dvfs_level==2?"~114MHz":g_dvfs_level==1?"~57MHz":"~40MHz",
             g_dvfs_level==2?COL_RED:g_dvfs_level==1?COL_ORANGE:COL_GREEN,bg,2); ry+=22;

    draw_str(rx,ry,"CPU:   ",COL_WHITE,bg,2);
    draw_str(rx+190,ry,g_dvfs_level==2?"666MHz":g_dvfs_level==1?"333MHz":"167MHz",
             g_cpu_fast?COL_RED:COL_GREEN,bg,2); ry+=22;

    draw_str(rx,ry,"Sleep: ",COL_WHITE,bg,2);
    draw_str(rx+190,ry,"WFI",COL_GREEN,bg,2); ry+=22;

    draw_str(rx,ry,"TotPwr:",COL_WHITE,bg,2);
    if(g_power_mw>0){
        char pw[24]; i32_to_str(pw,g_power_mw,0); strcat(pw," mW");
        draw_str(rx+190,ry,pw,COL_YELLOW,bg,2);
    } else {
        draw_str(rx+190,ry,"N/A",COL_WHITE,bg,2);
    }
    ry+=22;

    draw_str(rx,ry,"VCCINT:",COL_WHITE,bg,2);
    if(g_vccint_mv>0){
        char vc[24]; i32_to_str(vc,g_vccint_mv,0); strcat(vc," mV");
        draw_str(rx+190,ry,vc,COL_CYAN,bg,2);
    } else {
        draw_str(rx+190,ry,"N/A",COL_WHITE,bg,2);
    }
    ry+=28;

    draw_str(rx,ry,"SENSORS",COL_CYAN,bg,2); ry+=22;
    draw_str(rx,ry,"TMP102:",COL_WHITE,bg,2);
    draw_str(rx+190,ry,"0x48 OK",COL_GREEN,bg,2); ry+=20;
    draw_str(rx,ry,"INA219:",COL_WHITE,bg,2);
    draw_str(rx+190,ry,"0x40 OK",COL_GREEN,bg,2); ry+=20;
    draw_str(rx,ry,"MPU6500:",COL_WHITE,bg,2);
    draw_str(rx+190,ry,g_mpu_ok?"0x68 OK":"MISSING",
             g_mpu_ok?COL_GREEN:COL_RED,bg,2); ry+=28;

    /* Power bars */
    if(g_power_mw>0){
        draw_str(rx,ry,"Pwr bar:",COL_WHITE,bg,2); ry+=18;
        draw_bar(rx,ry,360,14,g_power_mw,3500,4500,
                 g_dvfs_level==2?COL_RED:g_dvfs_level==1?COL_ORANGE:COL_GREEN,COL_PANEL);
        ry+=18;
        draw_str(rx,ry,"3500mW",COL_WHITE,bg,1);
        draw_str(rx+300,ry,"4500mW",COL_WHITE,bg,1);
    }

    /* History bars (bottom) */
    int bx=14, by=DISP_H-90;
    draw_str(bx,by-16,"TEMP HISTORY (16 samples)",COL_CYAN,bg,1);
    int n=(sample_count<WINDOW_SIZE)?sample_count:WINDOW_SIZE;
    int32_t hmn=10000,hmx=-10000;
    for(int i=0;i<n;i++){
        if(sample_window[i]<hmn) hmn=sample_window[i];
        if(sample_window[i]>hmx) hmx=sample_window[i];
    }
    if(hmx==hmn) hmx=hmn+100;
    for(int i=0;i<n;i++){
        int idx=(window_head-n+i+WINDOW_SIZE)%WINDOW_SIZE;
        int32_t v=sample_window[idx];
        int bh=(int)((int64_t)(v-hmn)*60/(hmx-hmn));
        if(bh<2) bh=2;
        fill_rect(bx+i*25,by,20,65,COL_PANEL);
        fill_rect(bx+i*25,by+65-bh,20,bh,
                  g_dvfs_level==2?COL_RED:g_dvfs_level==1?COL_ORANGE:COL_GREEN);
    }

    /* Footer */
    fill_rect(0,DISP_H-22,DISP_W,22,COL_PANEL);
    fill_rect(0,DISP_H-22,DISP_W,1,COL_BORDER);
    char ts_h[12]; fmt_time(ts_h,g_elapsed_s);
    char foot[96];
    strcpy(foot,"ZedBoard Zynq-7000 | Low Power Sensor Interface | ");
    strcat(foot,"TMP102+INA219+MPU6500 | DVFS+WFI | T+");
    strcat(foot,ts_h);
    draw_str(10,DISP_H-16,foot,COL_WHITE,COL_PANEL,1);
}

/* ================================================================
 * Window push
 * ================================================================ */
static void window_push(int32_t v)
{
    sample_window[window_head]=v;
    window_head=(window_head+1)%WINDOW_SIZE;
    sample_count++;
}

/* ================================================================
 * Analysis + display update
 * ================================================================ */
static void run_analysis(int32_t t_now, int16_t raw_now,
                         int32_t xadc_t, int is_event,
                         u32 poll_s, int force_hdmi, u32 elapsed_s)
{
    int n=(sample_count<WINDOW_SIZE)?sample_count:WINDOW_SIZE;
    if(n<2){xil_printf("[Collecting %d/%d]\r\n",n,WINDOW_SIZE);return;}

    int64_t sum=0;
    for(int i=0;i<n;i++) sum+=sample_window[i];
    int32_t mean=(int32_t)(sum/n);

    int64_t ssq=0;
    for(int i=0;i<n;i++){int64_t d=sample_window[i]-mean;ssq+=d*d;}
    int32_t stddev=isqrt((int32_t)(ssq/n));

    int64_t Sxy=0,Sx=(int64_t)n*(n-1)/2,Sy=0;
    int64_t Sx2=(int64_t)n*(n-1)*(2*n-1)/6;
    for(int i=0;i<n;i++){
        Sy+=sample_window[(window_head-n+i+WINDOW_SIZE)%WINDOW_SIZE];
        Sxy+=(int64_t)i*sample_window[(window_head-n+i+WINDOW_SIZE)%WINDOW_SIZE];
    }
    int64_t den=(int64_t)n*Sx2-Sx*Sx;
    int32_t slope=0;
    if(den!=0) slope=(int32_t)(((int64_t)n*Sxy-Sx*Sy)/den);
    int32_t spm=(slope*60)/(int32_t)poll_s;

    int fn=(n<FIR_TAPS)?n:FIR_TAPS;
    int64_t fa=0; int32_t fcs=0;
    for(int k=0;k<fn;k++){
        int idx=(window_head-1-k+WINDOW_SIZE)%WINDOW_SIZE;
        fa+=(int64_t)fir_coeffs[k]*sample_window[idx];
        fcs+=fir_coeffs[k];
    }
    int32_t fir_out=fcs>0?(int32_t)(fa/fcs):mean;

    int32_t dev=t_now-mean; if(dev<0) dev=-dev;
    int anomaly=(dev*100>ANOMALY_FACTOR_X100*stddev&&stddev>0);

    int32_t t_min=sample_window[0],t_max=sample_window[0];
    for(int i=1;i<n;i++){
        if(sample_window[i]<t_min) t_min=sample_window[i];
        if(sample_window[i]>t_max) t_max=sample_window[i];
    }

    /* UART — power every poll, temperature on >=0.5C change */
    char b[32], ts[12];
    fmt_time(ts,elapsed_s);

    char pw_buf[16], vc_buf[16];
    if(g_power_mw>0) i32_to_str(pw_buf,g_power_mw,0);
    else { pw_buf[0]='N';pw_buf[1]='/';pw_buf[2]='A';pw_buf[3]='\0'; }
    if(g_vccint_mv>0) i32_to_str(vc_buf,g_vccint_mv,0);
    else { vc_buf[0]='N';vc_buf[1]='/';vc_buf[2]='A';vc_buf[3]='\0'; }

    xil_printf("[%s] Pwr=%smW  State=%s  FCLK0=%s  CPU=%s  %s\r\n",
               ts, pw_buf,
               g_thermal_state==THERMAL_COOL?"COOL":
               g_thermal_state==THERMAL_HOT ?"HOT":"NORM",
               g_dvfs_level==2?"114MHz":g_dvfs_level==1?"57MHz":"40MHz",
               g_dvfs_level==2?"666MHz":g_dvfs_level==1?"333MHz":"167MHz",
               g_motion?"MOTION":"");

    int16_t raw_delta=(int16_t)(raw_now-g_last_uart_raw);
    if(raw_delta<0) raw_delta=-raw_delta;
    int uart_needed = is_event
                   || (g_thermal_state!=g_last_uart_state)
                   || (raw_delta>=8);
    if(uart_needed){
        xil_printf("[%s N=%d %s] ",ts,n,is_event?"EVENT":"IDLE");
        i32_to_str(b,t_now,2); xil_printf("Amb=%s C  ",b);
        if(xadc_t!=XADC_NOT_READY){i32_to_str(b,xadc_t,2);xil_printf("Die=%s C  ",b);}
        else xil_printf("Die=N/A  ");
        i32_to_str(b,mean,2); xil_printf("Mean=%s  ",b);
        xil_printf("Trend=%s%ld.%02ld C/min\r\n",
                   spm>=0?"+":"-",(long)abs32(spm)/100,(long)abs32(spm)%100);
        if(g_mpu_ok){
            xil_printf("  MPU: Ax=%d Ay=%d Az=%d  Gx=%d Gy=%d Gz=%d  %s\r\n",
                       (int)g_ax,(int)g_ay,(int)g_az,
                       (int)g_gx,(int)g_gy,(int)g_gz,
                       g_motion?"[MOTION]":"");
        }
        xil_printf("  VCCINT=%smV  Poll=%lus\r\n",
                   vc_buf,(unsigned long)poll_s);
        g_last_uart_raw  =raw_now;
        g_last_uart_state=g_thermal_state;
    }

    /* HDMI redraw */
    g_display_counter++;
    int do_redraw=force_hdmi||is_event||anomaly||g_motion
                 ||(g_display_counter>=HDMI_REFRESH_POLLS);
    int val_changed=(raw_now!=g_last_drawn_raw);
    if(do_redraw&&(force_hdmi||is_event||anomaly||val_changed||g_motion)){
        hdmi_draw(t_now,xadc_t,mean,stddev,t_min,t_max,
                  spm,fir_out,anomaly,is_event,poll_s);
        usleep(20000);
        fb_flush();
        g_display_counter=0;
        g_last_drawn_state=g_thermal_state;
        g_last_drawn_raw  =raw_now;
    }
}

/* ================================================================
 * Main
 * ================================================================ */
int main(void)
{
    xil_printf("\r\n=== Zynq Low Power Sensor Interface ===\r\n");
    xil_printf("TMP102 + INA219 + MPU6500 | DVFS+WFI | 720p60 HDMI\r\n");
    xil_printf("Inverted DVFS: BOOT=SLOW, HOT=>FAST\r\n\r\n");

    if(gic_timer_init()!=XST_SUCCESS)
        xil_printf("WARN: GIC/Timer init failed\r\n");

    /* Boot at ULTRA low power clocks */
    xil_printf("Setting boot clocks: CPU=167MHz, FCLK0=~40MHz\r\n");
    slcr_unlock();
    u32 arm_reg=(Xil_In32(SLCR_ARM_CLK_CTRL)&~ARM_CLK_DIV_MASK)|ARM_CLK_DIV_ULTRA;
    Xil_Out32(SLCR_ARM_CLK_CTRL,arm_reg);
    slcr_lock();
    g_cpu_fast=0; g_dvfs_level=0;
    fclk0_set_freq(FCLK0_DIV1_ULTRA,FCLK0_DIV0_ULTRA);
    g_dvfs_fast=0;
    xil_printf("Boot clocks set. FCLK0=0x%08lX\r\n",
               (unsigned long)Xil_In32(SLCR_FCLK0_CTRL));

    /* IIC0 */
    xil_printf("Init: IIC0...\r\n");
    if(iic0_init()!=XST_SUCCESS){
        xil_printf("ERROR: axi_iic_0\r\n"); return XST_FAILURE;}
    xil_printf("Init: IIC0 OK\r\n");

    /* IIC1 */
    if(iic1_init()!=XST_SUCCESS)
        xil_printf("WARN: axi_iic_1 failed\r\n");

    /* Scan JA bus */
    xil_printf("Scanning JA bus...\r\n");
    for(uint8_t sa=0x08;sa<=0x77;sa++){
        uint8_t reg=0x00;
        if(XIic_Send(Iic.BaseAddress,sa,&reg,1,XIIC_STOP)==1){
            xil_printf("  JA 0x%02X: ACK",(unsigned)sa);
            if(sa==0x40) xil_printf(" <- INA219");
            if(sa==0x48||sa==0x49||sa==0x4A||sa==0x4B) xil_printf(" <- TMP102");
            if(sa==0x68) xil_printf(" <- MPU6500/9250 (AD0=GND)");
            if(sa==0x69) xil_printf(" <- MPU6500/9250 (AD0=VCC)");
            xil_printf("\r\n");
        }
        usleep(3000);
    }
    xil_printf("JA scan done\r\n\r\n");

    /* Reset IIC after scan */
    XIic_Reset(&Iic); usleep(10000); XIic_Start(&Iic); usleep(5000);

    /* INA219 */
    xil_printf("Init: INA219...\r\n");
    if(ina219_init()!=XST_SUCCESS)
        xil_printf("WARN: INA219 not responding\r\n");

    /* MPU-6500 */
    xil_printf("Init: MPU6500...\r\n");
    if(mpu6500_init()==XST_SUCCESS)
        g_mpu_ok=1;
    else
        xil_printf("WARN: MPU6500 not found — check AD0=GND, VCC=3.3V\r\n");

    /* GPIO */
    XGpio_Initialize(&GpioLed,GPIO_LED_DEVICE_ID);
    XGpio_SetDataDirection(&GpioLed,1,0);
    XGpio_DiscreteWrite(&GpioLed,1,0);

    /* Framebuffer */
    xil_printf("Clearing framebuffer...\r\n");
    for(int i=0;i<FB_SIZE;i+=3){fb[i]=0x00;fb[i+1]=0x1A;fb[i+2]=0x33;}
    fb_flush();

    /* VDMA + VTC */
    vdma_init();
    XVtc_Config *vtc_cfg=XVtc_LookupConfig(VTC_DEVICE_ID);
    if(vtc_cfg&&XVtc_CfgInitialize(&Vtc,vtc_cfg,vtc_cfg->BaseAddress)==XST_SUCCESS){
        XVtc_EnableGenerator(&Vtc);
        xil_printf("VTC: OK\r\n");
    }
    usleep(500000);
    adv7511_init();

    /* XADC */
    int32_t xd=read_xadc_c_x100();
    char buf[32]; i32_to_str(buf,xd,2);
    xil_printf("XADC die: %s C\r\n", xd==XADC_NOT_READY?"N/A":buf);

    /* TMP102 scan */
    xil_printf("Scanning TMP102...\r\n");
    uint8_t good_addr=0; int found_tmp=0;
    for(uint8_t a=TMP102_ADDR_MIN;a<=TMP102_ADDR_MAX;a++){
        int ok=(tmp102_probe(a)==XST_SUCCESS);
        xil_printf("  0x%02X: %s\r\n",a,ok?"ACK":"no");
        if(ok&&!found_tmp){found_tmp=1;good_addr=a;}
    }
    if(!found_tmp){xil_printf("ERROR: No TMP102\r\n");return XST_FAILURE;}
    xil_printf("TMP102 @ 0x%02X\r\n\r\n",good_addr);

    int16_t raw_prev=0;
    if(tmp102_read(good_addr,&raw_prev)!=XST_SUCCESS){
        xil_printf("ERROR: TMP102 first read\r\n");return XST_FAILURE;}
    int32_t t0=r12_to_cx100(raw_prev);
    window_push(t0);
    dvfs_update(t0);

    i32_to_str(buf,t0,2);
    xil_printf("Baseline: %s C  State=%d  Poll=%lus\r\n",
               buf,(int)g_thermal_state,(unsigned long)poll_interval_s());
    xil_printf("CPU=%s  FCLK0=%s\r\n",
               g_dvfs_level==2?"666MHz":g_dvfs_level==1?"333MHz":"167MHz",
               g_dvfs_level==2?"~114MHz":g_dvfs_level==1?"~57MHz":"~40MHz");
    xil_printf(">>> BOOT COMPLETE — system running at minimum power <<<\r\n\r\n");

    /* Splash screen */
    fill_rect(0,0,DISP_W,DISP_H,COL_BG_IDLE);
    fill_rect(0,0,DISP_W,56,COL_PANEL);
    fill_rect(0,54,DISP_W,2,COL_BORDER);
    draw_str(20,14,"ZYNQ-7000  LOW POWER SENSOR INTERFACE  ZedBoard",
             COL_YELLOW,COL_PANEL,2);
    draw_str(340,260,"System Ready",COL_GREEN,COL_BG_IDLE,3);
    draw_str(280,310,"TMP102 + INA219 + MPU6500",COL_CYAN,COL_BG_IDLE,2);
    draw_str(320,350,"Collecting samples...",COL_WHITE,COL_BG_IDLE,2);
    draw_str(200,400,"Boot mode: CPU=167MHz FCLK0=40MHz (ULTRA)",COL_ORANGE,COL_BG_IDLE,2);
    fb_flush();

    /* ============================================================
     * Main loop
     * ============================================================ */
    while(1)
    {
        u32 p=poll_interval_s();
        wfi_sleep(p);

        /* TMP102 */
        int16_t raw_now;
        if(tmp102_read(good_addr,&raw_now)!=XST_SUCCESS){
            xil_printf("TMP102 err\r\n"); continue;}
        int32_t t_now=r12_to_cx100(raw_now);
        window_push(t_now);
        g_elapsed_s+=p;

        /* XADC */
        int32_t xadc_t=read_xadc_c_x100();

        /* INA219 */
        g_power_mw =ina219_read_power_mw();
        g_vccint_mv=read_xadc_vccint_mv();

        /* MPU-6500 */
        if(g_mpu_ok){
            if(mpu6500_read()!=XST_SUCCESS){
                xil_printf("MPU6500 read err\r\n");
                g_motion=0;
            }
        }

        /* DVFS — temperature driven, inverted */
        dvfs_update(t_now);

        /* Thermal event */
        int16_t delta=(int16_t)(raw_now-raw_prev);
        if(delta<0) delta=-delta;
        int is_event=(delta>=8);

        if(is_event){
            xil_printf("[EVENT] Thermal spike delta=%d\r\n",(int)delta);
        }

        /* LEDs */
        u32 leds=XGpio_DiscreteRead(&GpioLed,1);
        if(g_motion) leds|=LED_MOTION_BIT; else leds&=~LED_MOTION_BIT;
        if(is_event) leds|=LED_EVENT_BIT;  else leds&=~LED_EVENT_BIT;
        if(xadc_t!=XADC_NOT_READY&&xadc_t>=XADC_HOT_C_X100)
            leds|=LED_XADC_HOT_BIT; else leds&=~LED_XADC_HOT_BIT;
        XGpio_DiscreteWrite(&GpioLed,1,leds);

        int state_changed=(g_thermal_state!=g_last_drawn_state);
        run_analysis(t_now,raw_now,xadc_t,is_event,p,state_changed,g_elapsed_s);

        raw_prev=raw_now;
    }
    return 0;
}
