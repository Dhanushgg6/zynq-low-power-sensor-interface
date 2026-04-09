/*
 * main_baseline.c — BASELINE: No Power Management
 * ZedBoard / Zynq-7000  (Baremetal)
 *
 * This is the REFERENCE build with NO power management.
 * Used to compare against the PM build (main.c).
 *
 * Differences from main.c (PM build):
 *   - CPU fixed at 666MHz  (no scaling)
 *   - FCLK0 fixed at ~114MHz (no DVFS)
 *   - Fixed 2s poll interval (no adaptation)
 *   - No WFI sleep — busy-wait between polls
 *   - HDMI banner: RED "BASELINE / NO PM"
 *
 * Same sensors: TMP102 + INA219 + MPU6500 on JA
 * Same HDMI display pipeline (VDMA + VTC + ADV7511)
 */

#include "xparameters.h"
#include "xil_printf.h"
#include "xstatus.h"
#include "xiic.h"
#include "xgpio.h"
#include "xaxivdma.h"
#include "xvtc.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "sleep.h"
#include <string.h>
#include <stdint.h>

/* ================================================================
 * Hardware
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

/* SLCR registers */
#define SLCR_BASE          0xF8000000U
#define SLCR_LOCK_REG      (SLCR_BASE + 0x004U)
#define SLCR_UNLOCK_REG    (SLCR_BASE + 0x008U)
#define SLCR_FCLK0_CTRL    (SLCR_BASE + 0x170U)
#define SLCR_ARM_CLK_CTRL  (SLCR_BASE + 0x120U)
#define SLCR_APER_CLK_CTRL (SLCR_BASE + 0x12CU)
#define SLCR_LOCK_KEY      0x767BU
#define SLCR_UNLOCK_KEY    0xDF0DU
#define SLCR_FCLK_DIV0_SHIFT  8U
#define SLCR_FCLK_DIV0_MASK   (0x3FU << 8U)
#define SLCR_FCLK_DIV1_SHIFT  20U
#define SLCR_FCLK_DIV1_MASK   (0x3FU << 20U)
#define ARM_CLK_DIV_MASK   0x00003F00U
#define ARM_CLK_DIV_FAST   0x00000200U  /* DIVISOR=2 -> 666MHz */
/* Enable ALL PS7 peripheral clocks for maximum power baseline */
#define APER_ENABLE_ALL    0x3303FFFFU

/* INA219 */
#define INA219_ADDR        0x40
#define INA219_REG_CONFIG  0x00
#define INA219_REG_BUS     0x02
#define INA219_REG_POWER   0x03
#define INA219_REG_CALIB   0x05
#define INA219_CONFIG_VAL  0x3FFF
#define INA219_CALIB_VAL   4481

/* TMP102 */
#define TMP102_ADDR_MIN    0x48
#define TMP102_ADDR_MAX    0x4B
#define TMP102_REG_TEMP    0x00

/* MPU-6500 */
#define MPU_ADDR           0x68
#define MPU_REG_WHOAMI     0x75
#define MPU_REG_PWR_MGMT1  0x6B
#define MPU_REG_ACCEL_CFG  0x1C
#define MPU_REG_GYRO_CFG   0x1B
#define MPU_REG_ACCEL_X_H  0x3B

/* Device IDs */
#define VDMA_DEVICE_ID     XPAR_AXI_VDMA_0_DEVICE_ID
#define IIC_DEVICE_ID      XPAR_AXI_IIC_0_DEVICE_ID
#define IIC1_DEVICE_ID     XPAR_AXI_IIC_1_DEVICE_ID
#define GPIO_LED_DEVICE_ID XPAR_AXI_GPIO_0_DEVICE_ID
#define VTC_DEVICE_ID      XPAR_V_TC_0_DEVICE_ID

/* Baseline fixed parameters */
#define BASELINE_POLL_S    2      /* fixed 2s poll, no adaptation */
#define XADC_NOT_READY     (0x7FFFFFFF)

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

/* ================================================================
 * Globals
 * ================================================================ */
static XIic      Iic, Iic1;
static XGpio     GpioLed;
static XAxiVdma  Vdma;
static XVtc      Vtc;
static uint8_t  *fb = (uint8_t *)FB_ADDR;
static u32       g_elapsed_s = 0;
static int32_t   g_power_mw  = -1;
static int32_t   g_vccint_mv = -1;
static int16_t   g_ax=0, g_ay=0, g_az=0;
static int16_t   g_gx=0, g_gy=0, g_gz=0;
static int       g_mpu_ok = 0;

/* ================================================================
 * SLCR helpers
 * ================================================================ */
static inline void slcr_unlock(void){Xil_Out32(SLCR_UNLOCK_REG,SLCR_UNLOCK_KEY);}
static inline void slcr_lock(void)  {Xil_Out32(SLCR_LOCK_REG,  SLCR_LOCK_KEY);  }

static void baseline_clocks_init(void)
{
    slcr_unlock();
    /* CPU -> 666MHz (DIVISOR=2) */
    u32 arm = (Xil_In32(SLCR_ARM_CLK_CTRL) & ~ARM_CLK_DIV_MASK) | ARM_CLK_DIV_FAST;
    Xil_Out32(SLCR_ARM_CLK_CTRL, arm);
    /* FCLK0 -> ~114MHz (DIV1=2, DIV0=7) */
    u32 fclk = Xil_In32(SLCR_FCLK0_CTRL);
    fclk &= ~(SLCR_FCLK_DIV1_MASK | SLCR_FCLK_DIV0_MASK);
    fclk |= ((2U & 0x3FU) << SLCR_FCLK_DIV1_SHIFT)
          | ((7U & 0x3FU) << SLCR_FCLK_DIV0_SHIFT);
    Xil_Out32(SLCR_FCLK0_CTRL, fclk);
    /* Enable ALL peripheral clocks */
    u32 aper = Xil_In32(SLCR_APER_CLK_CTRL);
    xil_printf("APER_CLK_CTRL before: 0x%08lX\r\n",(unsigned long)aper);
    Xil_Out32(SLCR_APER_CLK_CTRL, APER_ENABLE_ALL);
    slcr_lock();
    xil_printf("APER_CLK_CTRL after:  0x%08lX\r\n",
               (unsigned long)Xil_In32(SLCR_APER_CLK_CTRL));
    xil_printf("FCLK0_CTRL:   0x%08lX\r\n",
               (unsigned long)Xil_In32(SLCR_FCLK0_CTRL));
    xil_printf("ARM_CLK_CTRL: 0x%08lX\r\n",
               (unsigned long)Xil_In32(SLCR_ARM_CLK_CTRL));
    xil_printf("All peripheral clocks enabled\r\n");
}

/* ================================================================
 * Utilities
 * ================================================================ */
static int abs32(int32_t x) { return x<0?(int)-x:(int)x; }

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
    u32 h=secs/3600,m=(secs%3600)/60,s=secs%60;
    buf[0]='0'+(h/10);buf[1]='0'+(h%10);buf[2]=':';
    buf[3]='0'+(m/10);buf[4]='0'+(m%10);buf[5]=':';
    buf[6]='0'+(s/10);buf[7]='0'+(s%10);buf[8]='\0';
}

static int32_t r12_to_cx100(int16_t r)
{ return ((int32_t)r*625)/100; }

static int32_t read_xadc_c_x100(void)
{
    volatile uint32_t *x=(volatile uint32_t *)XADC_BASEADDR;
    uint16_t r=(uint16_t)(x[XADC_TEMP_REG/4]>>4)&0x0FFF;
    if(!r) return XADC_NOT_READY;
    return ((int32_t)r*50398)/4096-27315;
}

static int32_t read_xadc_vccint_mv(void)
{
    volatile uint32_t *x=(volatile uint32_t *)XADC_BASEADDR;
    uint16_t r=(uint16_t)(x[XADC_VCCINT_REG/4]>>4)&0x0FFF;
    if(!r) return -1;
    return ((int32_t)r*3000)/4096;
}

/* ================================================================
 * IIC0
 * ================================================================ */
static int iic0_init(void)
{
    XIic_Config *c=XIic_LookupConfig(IIC_DEVICE_ID);
    if(!c) return XST_FAILURE;
    int s=XIic_CfgInitialize(&Iic,c,c->BaseAddress);
    if(s!=XST_SUCCESS) return s;
    XIic_Reset(&Iic);
    return XIic_Start(&Iic);
}

static int i2c_write_reg(uint8_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2]={reg,val};
    return (XIic_Send(Iic.BaseAddress,dev,buf,2,XIIC_STOP)==2)
           ? XST_SUCCESS:XST_FAILURE;
}

static int i2c_read_regs(uint8_t dev, uint8_t reg, uint8_t *out, int len)
{
    if(XIic_Send(Iic.BaseAddress,dev,&reg,1,XIIC_STOP)!=1) return XST_FAILURE;
    usleep(1000);
    if(XIic_Recv(Iic.BaseAddress,dev,out,len,XIIC_STOP)!=len) return XST_FAILURE;
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
    if(XIic_Send(Iic.BaseAddress,INA219_ADDR,&reg,1,XIIC_STOP)!=1) return XST_FAILURE;
    usleep(2000);
    if(XIic_Recv(Iic.BaseAddress,INA219_ADDR,rx,2,XIIC_STOP)!=2) return XST_FAILURE;
    *out=(int16_t)((rx[0]<<8)|rx[1]);
    return XST_SUCCESS;
}

static int ina219_init(void)
{
    if(ina219_write_reg(INA219_REG_CALIB, INA219_CALIB_VAL) !=XST_SUCCESS) return XST_FAILURE;
    if(ina219_write_reg(INA219_REG_CONFIG,INA219_CONFIG_VAL)!=XST_SUCCESS) return XST_FAILURE;
    usleep(5000);
    return XST_SUCCESS;
}

static int32_t ina219_read_power_mw(void)
{
    int16_t raw,vraw;
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
    uint8_t reg=TMP102_REG_TEMP,rx[2];
    if(XIic_Send(Iic.BaseAddress,addr,&reg,1,XIIC_STOP)!=1) return XST_FAILURE;
    if(XIic_Recv(Iic.BaseAddress,addr,rx,2,XIIC_STOP)!=2)   return XST_FAILURE;
    *out=(int16_t)(((int16_t)((rx[0]<<8)|rx[1]))>>4);
    return XST_SUCCESS;
}

static int tmp102_probe(uint8_t a)
{
    uint8_t r=TMP102_REG_TEMP;
    return XIic_Send(Iic.BaseAddress,a,&r,1,XIIC_STOP)==1
           ? XST_SUCCESS:XST_FAILURE;
}

/* ================================================================
 * MPU-6500
 * ================================================================ */
static int mpu6500_init(void)
{
    uint8_t whoami=0;
    if(i2c_read_regs(MPU_ADDR,MPU_REG_WHOAMI,&whoami,1)!=XST_SUCCESS)
        return XST_FAILURE;
    xil_printf("MPU6500 WHO_AM_I=0x%02X\r\n",(unsigned)whoami);
    if(i2c_write_reg(MPU_ADDR,MPU_REG_PWR_MGMT1,0x00)!=XST_SUCCESS)
        return XST_FAILURE;
    usleep(100000);
    i2c_write_reg(MPU_ADDR,MPU_REG_ACCEL_CFG,0x00);
    i2c_write_reg(MPU_ADDR,MPU_REG_GYRO_CFG, 0x00);
    return XST_SUCCESS;
}

static int mpu6500_read(void)
{
    uint8_t buf[14];
    if(i2c_read_regs(MPU_ADDR,MPU_REG_ACCEL_X_H,buf,14)!=XST_SUCCESS)
        return XST_FAILURE;
    g_ax=(int16_t)((buf[0]<<8)|buf[1]);
    g_ay=(int16_t)((buf[2]<<8)|buf[3]);
    g_az=(int16_t)((buf[4]<<8)|buf[5]);
    g_gx=(int16_t)((buf[8] <<8)|buf[9]);
    g_gy=(int16_t)((buf[10]<<8)|buf[11]);
    g_gz=(int16_t)((buf[12]<<8)|buf[13]);
    return XST_SUCCESS;
}

/* ================================================================
 * IIC1 — ADV7511
 * ================================================================ */
static int iic1_init(void)
{
    XIic_Config *c=XIic_LookupConfig(IIC1_DEVICE_ID);
    if(!c) return XST_FAILURE;
    int s=XIic_CfgInitialize(&Iic1,c,c->BaseAddress);
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
    return val;
}

static void adv7511_init(void)
{
    xil_printf("ADV7511 init...\r\n");
    uint8_t rev=adv7511_read(0x00);
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
    xil_printf("  rev=0x%02X  0xAF=0x%02X  HPD:%s\r\n",
               rev, adv7511_read(0xAF),
               (adv7511_read(0x42)&0x40)?"YES":"NO");
    xil_printf("ADV7511 done\r\n");
}

/* ================================================================
 * VDMA
 * ================================================================ */
static int vdma_init(void)
{
    XAxiVdma_Config *cfg=XAxiVdma_LookupConfig(VDMA_DEVICE_ID);
    if(!cfg) return XST_FAILURE;
    int s=XAxiVdma_CfgInitialize(&Vdma,cfg,cfg->BaseAddress);
    if(s!=XST_SUCCESS) return s;
    XAxiVdma_DmaSetup rd;
    memset(&rd,0,sizeof(rd));
    rd.VertSizeInput=DISP_H; rd.HoriSizeInput=DISP_W*DISP_BPP;
    rd.Stride=DISP_W*DISP_BPP; rd.EnableCircularBuf=1;
    s=XAxiVdma_DmaConfig(&Vdma,XAXIVDMA_READ,&rd);
    if(s!=XST_SUCCESS) return s;
    UINTPTR addrs[3]={(UINTPTR)FB_ADDR,(UINTPTR)FB_ADDR,(UINTPTR)FB_ADDR};
    s=XAxiVdma_DmaSetBufferAddr(&Vdma,XAXIVDMA_READ,addrs);
    if(s!=XST_SUCCESS) return s;
    s=XAxiVdma_DmaStart(&Vdma,XAXIVDMA_READ);
    if(s!=XST_SUCCESS) return s;
    xil_printf("VDMA: OK\r\n");
    return XST_SUCCESS;
}

/* ================================================================
 * Drawing
 * ================================================================ */
#define COL_BG      0x001A0000   /* dark red background — baseline */
#define COL_PANEL   0x003A0000
#define COL_BORDER  0x00AA0000
#define COL_WHITE   0x00FFFFFF
#define COL_YELLOW  0x00FFDD00
#define COL_RED     0x00FF4444
#define COL_ORANGE  0x00FF8800
#define COL_CYAN    0x0000FFFF
#define COL_GREEN   0x0000FF88

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
static void fb_flush(void)
{ Xil_DCacheFlushRange((UINTPTR)fb,FB_SIZE); }

/* ================================================================
 * HDMI draw — baseline dashboard (red theme, NO PM banner)
 * ================================================================ */
static void hdmi_draw_baseline(int32_t ambient, int32_t die,
                               int32_t power_mw, u32 elapsed_s)
{
    char b[64];
    fill_rect(0,0,DISP_W,DISP_H,COL_BG);

    /* Title — RED to clearly indicate baseline */
    fill_rect(0,0,DISP_W,56,COL_PANEL);
    fill_rect(0,54,DISP_W,2,COL_BORDER);
    draw_str(20,8, "ZYNQ-7000  LOW POWER SENSOR INTERFACE",
             COL_YELLOW,COL_PANEL,2);
    draw_str(20,30,"BASELINE BUILD — NO POWER MANAGEMENT",
             COL_RED,COL_PANEL,2);
    draw_str(DISP_W-200,8,"NO PM",COL_RED,COL_PANEL,3);

    /* Left — sensor readings */
    int lx=20, ly=72;
    draw_str(lx,ly,"SENSOR READINGS",COL_CYAN,COL_BG,2); ly+=28;

    draw_str(lx,ly,"Ambient :",COL_WHITE,COL_BG,2);
    i32_to_str(b,ambient,2); strcat(b," C");
    draw_str(lx+220,ly,b,COL_YELLOW,COL_BG,2); ly+=22;

    draw_str(lx,ly,"Die Temp:",COL_WHITE,COL_BG,2);
    if(die!=XADC_NOT_READY){i32_to_str(b,die,2);strcat(b," C");}
    else strcpy(b,"N/A");
    draw_str(lx+220,ly,b,COL_ORANGE,COL_BG,2); ly+=22;

    draw_str(lx,ly,"Power   :",COL_WHITE,COL_BG,2);
    if(power_mw>0){i32_to_str(b,power_mw,0);strcat(b," mW");}
    else strcpy(b,"N/A");
    draw_str(lx+220,ly,b,COL_RED,COL_BG,3); ly+=36;

    /* MPU readings */
    draw_str(lx,ly,"MPU-6500",COL_CYAN,COL_BG,2); ly+=24;
    i32_to_str(b,g_ax,0);
    draw_str(lx,ly,"Ax:",COL_WHITE,COL_BG,2);
    draw_str(lx+80,ly,b,COL_YELLOW,COL_BG,2); ly+=18;
    i32_to_str(b,g_ay,0);
    draw_str(lx,ly,"Ay:",COL_WHITE,COL_BG,2);
    draw_str(lx+80,ly,b,COL_YELLOW,COL_BG,2); ly+=18;
    i32_to_str(b,g_az,0);
    draw_str(lx,ly,"Az:",COL_WHITE,COL_BG,2);
    draw_str(lx+80,ly,b,COL_YELLOW,COL_BG,2); ly+=18;
    i32_to_str(b,g_gx,0);
    draw_str(lx,ly,"Gx:",COL_WHITE,COL_BG,2);
    draw_str(lx+80,ly,b,COL_CYAN,COL_BG,2); ly+=18;
    i32_to_str(b,g_gy,0);
    draw_str(lx,ly,"Gy:",COL_WHITE,COL_BG,2);
    draw_str(lx+80,ly,b,COL_CYAN,COL_BG,2); ly+=18;
    i32_to_str(b,g_gz,0);
    draw_str(lx,ly,"Gz:",COL_WHITE,COL_BG,2);
    draw_str(lx+80,ly,b,COL_CYAN,COL_BG,2);

    /* Right — baseline config box */
    int rx=600, ry=72;
    draw_str(rx,ry,"FIXED CONFIGURATION",COL_RED,COL_BG,2); ry+=28;

    draw_str(rx,ry,"CPU   :",COL_WHITE,COL_BG,2);
    draw_str(rx+180,ry,"666 MHz  (fixed)",COL_RED,COL_BG,2); ry+=22;
    draw_str(rx,ry,"FCLK0 :",COL_WHITE,COL_BG,2);
    draw_str(rx+180,ry,"~114 MHz (fixed)",COL_RED,COL_BG,2); ry+=22;
    draw_str(rx,ry,"Poll  :",COL_WHITE,COL_BG,2);
    draw_str(rx+180,ry,"2s (fixed)",COL_RED,COL_BG,2); ry+=22;
    draw_str(rx,ry,"Sleep :",COL_WHITE,COL_BG,2);
    draw_str(rx+180,ry,"NONE (busy-wait)",COL_RED,COL_BG,2); ry+=22;
    draw_str(rx,ry,"DVFS  :",COL_WHITE,COL_BG,2);
    draw_str(rx+180,ry,"DISABLED",COL_RED,COL_BG,2); ry+=22;
    draw_str(rx,ry,"APER  :",COL_WHITE,COL_BG,2);
    draw_str(rx+180,ry,"ALL ON (max pwr)",COL_RED,COL_BG,2); ry+=36;

    draw_str(rx,ry,"SENSORS",COL_CYAN,COL_BG,2); ry+=22;
    draw_str(rx,ry,"TMP102  : 0x48",COL_GREEN,COL_BG,2); ry+=20;
    draw_str(rx,ry,"INA219  : 0x40",COL_GREEN,COL_BG,2); ry+=20;
    draw_str(rx,ry,g_mpu_ok?"MPU6500 : 0x68":"MPU6500 : MISSING",
             g_mpu_ok?COL_GREEN:COL_RED,COL_BG,2); ry+=36;

    /* Big power number in centre for easy comparison */
    draw_str(rx,ry,"TOTAL POWER",COL_WHITE,COL_BG,2); ry+=28;
    if(power_mw>0){
        i32_to_str(b,power_mw,0); strcat(b," mW");
        draw_str(rx,ry,b,COL_RED,COL_BG,4);
    } else {
        draw_str(rx,ry,"--- mW",COL_WHITE,COL_BG,4);
    }

    /* Footer */
    fill_rect(0,DISP_H-22,DISP_W,22,COL_PANEL);
    fill_rect(0,DISP_H-22,DISP_W,1,COL_BORDER);
    char ts[12]; fmt_time(ts,elapsed_s);
    char foot[96];
    strcpy(foot,"BASELINE: CPU=666MHz FCLK0=114MHz Poll=2s No-WFI No-DVFS | T+");
    strcat(foot,ts);
    draw_str(10,DISP_H-16,foot,COL_WHITE,COL_PANEL,1);
}

/* ================================================================
 * Main
 * ================================================================ */
int main(void)
{
    xil_printf("\r\n=== BASELINE: No Power Management ===\r\n");
    xil_printf("CPU=666MHz FCLK0=114MHz Poll=2s No-WFI No-DVFS\r\n");
    xil_printf("Sensors: TMP102 + INA219 + MPU6500\r\n\r\n");

    /* Set max clocks + enable all peripheral clocks */
    baseline_clocks_init();

    /* IIC0 */
    if(iic0_init()!=XST_SUCCESS){
        xil_printf("ERROR: IIC0 init failed\r\n"); return XST_FAILURE;}
    xil_printf("IIC0 OK\r\n");

    /* IIC1 */
    iic1_init();

    /* Scan JA bus */
    xil_printf("Scanning JA...\r\n");
    for(uint8_t sa=0x08;sa<=0x77;sa++){
        uint8_t reg=0x00;
        if(XIic_Send(Iic.BaseAddress,sa,&reg,1,XIIC_STOP)==1){
            xil_printf("  0x%02X: ACK",(unsigned)sa);
            if(sa==0x40) xil_printf(" INA219");
            if(sa==0x48) xil_printf(" TMP102");
            if(sa==0x68) xil_printf(" MPU6500");
            xil_printf("\r\n");
        }
        usleep(3000);
    }

    XIic_Reset(&Iic); usleep(10000); XIic_Start(&Iic); usleep(5000);

    /* INA219 */
    if(ina219_init()!=XST_SUCCESS)
        xil_printf("WARN: INA219 init failed\r\n");
    else
        xil_printf("INA219 OK\r\n");

    /* MPU-6500 */
    if(mpu6500_init()==XST_SUCCESS){
        g_mpu_ok=1;
        xil_printf("MPU6500 OK\r\n");
    } else {
        xil_printf("WARN: MPU6500 not found\r\n");
    }

    /* GPIO */
    XGpio_Initialize(&GpioLed,GPIO_LED_DEVICE_ID);
    XGpio_SetDataDirection(&GpioLed,1,0);
    XGpio_DiscreteWrite(&GpioLed,1,0xFF); /* all LEDs on — baseline indicator */

    /* Framebuffer */
    for(int i=0;i<FB_SIZE;i+=3){fb[i]=0x1A;fb[i+1]=0x00;fb[i+2]=0x00;}
    fb_flush();

    /* VDMA + VTC */
    vdma_init();
    XVtc_Config *vtc_cfg=XVtc_LookupConfig(VTC_DEVICE_ID);
    if(vtc_cfg&&XVtc_CfgInitialize(&Vtc,vtc_cfg,vtc_cfg->BaseAddress)==XST_SUCCESS)
        XVtc_EnableGenerator(&Vtc);
    usleep(500000);
    adv7511_init();

    /* TMP102 scan */
    xil_printf("Scanning TMP102...\r\n");
    uint8_t good_addr=0; int found=0;
    for(uint8_t a=TMP102_ADDR_MIN;a<=TMP102_ADDR_MAX;a++){
        uint8_t r=TMP102_REG_TEMP;
        int ok=(XIic_Send(Iic.BaseAddress,a,&r,1,XIIC_STOP)==1);
        xil_printf("  0x%02X: %s\r\n",a,ok?"ACK":"no");
        if(ok&&!found){found=1;good_addr=a;}
    }
    if(!found){xil_printf("ERROR: No TMP102\r\n");return XST_FAILURE;}
    xil_printf("TMP102 @ 0x%02X\r\n",good_addr);

    xil_printf("\r\n>>> BASELINE RUNNING — NO POWER MANAGEMENT <<<\r\n");
    xil_printf("CPU=666MHz  FCLK0=~114MHz  Poll=%ds  Sleep=NONE\r\n\r\n",
               BASELINE_POLL_S);

    /* ============================================================
     * Main loop — NO WFI, fixed poll, fixed clocks
     * ============================================================ */
    while(1)
    {
        /* Busy-wait — no WFI, no sleep */
        usleep(BASELINE_POLL_S * 1000000U);
        g_elapsed_s += BASELINE_POLL_S;

        /* TMP102 */
        int16_t raw=0;
        if(tmp102_read(good_addr,&raw)!=XST_SUCCESS){
            xil_printf("TMP102 err\r\n"); continue;}
        int32_t t_now=r12_to_cx100(raw);

        /* XADC */
        int32_t die=read_xadc_c_x100();
        g_vccint_mv=read_xadc_vccint_mv();

        /* INA219 */
        g_power_mw=ina219_read_power_mw();

        /* MPU-6500 */
        if(g_mpu_ok) mpu6500_read();

        /* UART */
        char ts[12],b[32],pw[16],vc[16];
        fmt_time(ts,g_elapsed_s);
        if(g_power_mw>0) i32_to_str(pw,g_power_mw,0);
        else{pw[0]='N';pw[1]='/';pw[2]='A';pw[3]='\0';}
        if(g_vccint_mv>0) i32_to_str(vc,g_vccint_mv,0);
        else{vc[0]='N';vc[1]='/';vc[2]='A';vc[3]='\0';}
        i32_to_str(b,t_now,2);
        xil_printf("[%s] Pwr=%smW  Amb=%sC  CPU=666MHz  FCLK0=114MHz  BASELINE\r\n",
                   ts,pw,b);
        if(g_mpu_ok){
            xil_printf("  MPU: Ax=%d Ay=%d Az=%d  Gx=%d Gy=%d Gz=%d\r\n",
                       (int)g_ax,(int)g_ay,(int)g_az,
                       (int)g_gx,(int)g_gy,(int)g_gz);
        }

        /* HDMI */
        hdmi_draw_baseline(t_now,die,g_power_mw,g_elapsed_s);
        usleep(20000);
        fb_flush();
    }
    return 0;
}
