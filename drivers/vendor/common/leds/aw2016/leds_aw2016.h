/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __LINUX_AW2016_LED_H__
#define __LINUX_AW2016_LED_H__

#define AW2016_DRIVER_VERSION "V1.0.3"

#define AW2016_NAME "aw2016_led"

//#define DEBUG

#define AW_DEBUG
#ifdef AW_DEBUG
#define AW_LOG(fmt, args...)    pr_info("[%s] %s %d: " fmt, AW2016_NAME, \
        __func__, __LINE__, ##args)
#else
#define AW_LOG(fmt, args...)
#endif
#define AW_ERR(fmt, args...)    pr_err("[%s] %s %d: " fmt, AW2016_NAME, \
        __func__, __LINE__, ##args)

#define COLOR_PINK              0xaf3020
#define COLOR_GREEN             0x20af00
#define COLOR_BLUE              0x0050af
#define COLOR_CYAN              0x20af30
#define COLOR_ORANGE            0xaf5000
#define COLOR_YELLOW            0x6f8000
#define COLOR_PURPLE            0x701080

#define CHECK_PATTERN_STATUS_DELAY      100
#define CHECK_PATTERN_STATUS_MORE_TIMES 10
#define DANCE_FLASH_TIMES       3
#define DANCE_SLEEP_MS          1000

#define LED_CURRENT_MAX         15
#define LED_COLOR_MAX           255
#define PATTERN_REPEAT_TIMES_MAX        15

/* register address */
#define AW2016_REG_RESET                0x00
#define AW2016_REG_GCR1                 0x01
#define AW2016_REG_STATUS               0x02
#define AW2016_REG_PATST                0x03
#define AW2016_REG_GCR2                 0x04
#define AW2016_REG_LEDEN                0x30
#define AW2016_REG_LCFG1                0x31
#define AW2016_REG_LCFG2                0x32
#define AW2016_REG_LCFG3                0x33
#define AW2016_REG_PWM1                 0x34
#define AW2016_REG_PWM2                 0x35
#define AW2016_REG_PWM3                 0x36
#define AW2016_REG_LED1T0               0x37
#define AW2016_REG_LED1T1               0x38
#define AW2016_REG_LED1T2               0x39
#define AW2016_REG_LED2T0               0x3A
#define AW2016_REG_LED2T1               0x3B
#define AW2016_REG_LED2T2               0x3C
#define AW2016_REG_LED3T0               0x3D
#define AW2016_REG_LED3T1               0x3E
#define AW2016_REG_LED3T2               0x3F

/* register bits */
#define AW2016_CHIPID                   0x09
#define AW2016_CHIP_RESET_MASK          0x55
#define AW2016_CHIP_DISABLE_MASK        0x00
#define AW2016_CHIP_ENABLE_MASK         0x01
#define AW2016_CHARGE_DISABLE_MASK      0x02
#define AW2016_LED_ALL_ENABLE_MASK      0x07
#define AW2016_LED_SYNC_MODE_MASK       0x80
#define AW2016_LED_PATTERN_STATUS_MASK  0x01
#define AW2016_LED_BREATH_MODE_MASK     0x10
#define AW2016_LED_MANUAL_MODE_MASK     0x00
#define AW2016_LED_BREATHE_PWM_MASK     0xFF
#define AW2016_LED_MANUAL_PWM_MASK      0xFF
#define AW2016_LED_FADEIN_MODE_MASK     0x20
#define AW2016_LED_FADEOUT_MODE_MASK    0x40
#define AW2016_CHIP_STANDBY             0x02

/* aw2016 register read/write access*/
#define REG_NONE_ACCESS                 0
#define REG_RD_ACCESS                   1 << 0
#define REG_WR_ACCESS                   1 << 1
#define AW2016_REG_MAX                  0x7F

const unsigned char aw2016_reg_access[AW2016_REG_MAX] = {
    [AW2016_REG_RESET]  = REG_RD_ACCESS|REG_WR_ACCESS,
    [AW2016_REG_GCR1]   = REG_RD_ACCESS|REG_WR_ACCESS,
    [AW2016_REG_STATUS] = REG_RD_ACCESS,
    [AW2016_REG_PATST]  = REG_RD_ACCESS,
    [AW2016_REG_GCR2]   = REG_RD_ACCESS|REG_WR_ACCESS,
    [AW2016_REG_LEDEN]  = REG_RD_ACCESS|REG_WR_ACCESS,
    [AW2016_REG_LCFG1]  = REG_RD_ACCESS|REG_WR_ACCESS,
    [AW2016_REG_LCFG2]  = REG_RD_ACCESS|REG_WR_ACCESS,
    [AW2016_REG_LCFG3]  = REG_RD_ACCESS|REG_WR_ACCESS,
    [AW2016_REG_PWM1]   = REG_RD_ACCESS|REG_WR_ACCESS,
    [AW2016_REG_PWM2]   = REG_RD_ACCESS|REG_WR_ACCESS,
    [AW2016_REG_PWM3]   = REG_RD_ACCESS|REG_WR_ACCESS,
    [AW2016_REG_LED1T0] = REG_RD_ACCESS|REG_WR_ACCESS,
    [AW2016_REG_LED1T1] = REG_RD_ACCESS|REG_WR_ACCESS,
    [AW2016_REG_LED1T2] = REG_RD_ACCESS|REG_WR_ACCESS,
    [AW2016_REG_LED2T0] = REG_RD_ACCESS|REG_WR_ACCESS,
    [AW2016_REG_LED2T1] = REG_RD_ACCESS|REG_WR_ACCESS,
    [AW2016_REG_LED2T2] = REG_RD_ACCESS|REG_WR_ACCESS,
    [AW2016_REG_LED3T0] = REG_RD_ACCESS|REG_WR_ACCESS,
    [AW2016_REG_LED3T1] = REG_RD_ACCESS|REG_WR_ACCESS,
    [AW2016_REG_LED3T2] = REG_RD_ACCESS|REG_WR_ACCESS,
};

enum work_state {
    WORK_STOP,
    WORK_RUNNING,
};

struct aw2016_led {
    struct i2c_client *client;
    struct led_classdev cdev;
    struct aw2016_platform_data *pdata;
    struct work_struct brightness_work;
    struct work_struct blink_work;
    struct work_struct dance_work;
    struct mutex lock;
    enum work_state dance_work_state;
    int num_leds;
    int id;
    int blinking;
};

/* The definition of each time described as shown in figure.
 *        /-----------\
 *       /      |      \
 *      /|      |      |\
 *     / |      |      | \-----------
 *       |hold_time_ms |      |
 *       |             |      |
 * rise_time_ms  fall_time_ms |
 *                       off_time_ms
 */
struct aw2016_platform_data {
    int imax;
    int red_current;
    int green_current;
    int blue_current;
    int brightness;
    int led_current;
    int rise_time_ms;  // T1
    int hold_time_ms;  // T2
    int fall_time_ms;  // T3
    int off_time_ms;  // T4
    int delay_time_ms;  // T0
    int repeat_times;
    struct aw2016_led *led;
};

struct timings_cfg {
    uint16_t ms;
    uint8_t  reg_val;
};

struct timings_cfg timings_cfg_group[17] = {
    {0, 0x0},
    {40, 0x0},
    {130, 0x1},
    {260, 0x2},
    {380, 0x3},
    {510, 0x4},
    {770, 0x5},
    {1040, 0x6},
    {1600, 0x7},
    {2100, 0x8},
    {2600, 0x9},
    {3100, 0xA},
    {4200, 0xB},
    {5200, 0xC},
    {6200, 0xD},
    {7300, 0xE},
    {8300, 0xF},
};

#endif