/*
 * Copyright (C) 2012-2022 fpsensor Technology (Beijing) Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __FPSENSOR_SPI_H
#define __FPSENSOR_SPI_H

#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/wait.h>
#include <linux/fb.h>
#include <linux/notifier.h>
#include <linux/spi/spidev.h>
#include <linux/spi/spi.h>
#include <linux/version.h>

/*********************** wakelock setting **********************/
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
#include <linux/pm_wakeup.h>
#else
#include <linux/wakelock.h>
#endif

#ifdef CONFIG_VENDOR_SOC_SPRD_COMPILE
#define  USE_PLATFORM_BUS	1
#endif

#ifdef CONFIG_VENDOR_SOC_MTK_COMPILE
#define  USE_SPI_BUS   1
extern void mt_spi_enable_master_clk(struct spi_device *spidev);
extern void mt_spi_disable_master_clk(struct spi_device *spidev);
#endif

/*#define FPSENSOR_GET_CHIPID        0   // 0:off    1: on*/

#if defined(FPSENSOR_GET_CHIPID)
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,0,0)
#include "mt_spi.h"
#include "mt_spi_hal.h"
#include "mt_gpio.h"
#include "mach/gpio_const.h"
#else
#include "fpsensor_spi_def.h"
#endif
#include <tee_fp.h>
#endif

/**********************IO Magic**********************/
#define CONFIG_OF               1

/*platform device name*/
#define FPSENSOR_DEV_NAME       "fpsensor"
/*device name after register in charater*/
#define FPSENSOR_CLASS_NAME     "fpsensor"
#define FPSENSOR_DEV_MAJOR      0 // 255
#define N_SPI_MINORS            32    /* ... up to 256 */
#define FPSENSOR_NR_DEVS        1
#define FPSENSOR_INPUT_NAME     "fpsensor_keys"
/*fp_nav*/
#define FP_NAV_UP          "fp_nav_event_up=true"
#define FP_NAV_DOWN        "fp_nav_event_down=true"
#define FP_NAV_LEFT        "fp_nav_event_left=true"
#define FP_NAV_RIGHT	    "fp_nav_event_right=true"

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
#include "zlog_common_base.h"
#endif

typedef enum fpsensor_key_event {
    FPSENSOR_KEY_NONE = 0,
    FPSENSOR_KEY_HOME,
    FPSENSOR_KEY_MENU,
    FPSENSOR_KEY_BACK,
#ifdef CONFIG_CHIPONE_NAV_NEW
    FPSENSOR_KEY_RIGHT = 106,
    FPSENSOR_KEY_CAPTURE,
    FPSENSOR_KEY_UP = 103,
    FPSENSOR_KEY_DOWN = 108,
    FPSENSOR_KEY_POWER,
    FPSENSOR_KEY_LEFT = 105,
#else
    FPSENSOR_KEY_LEFT = 103,
    FPSENSOR_KEY_CAPTURE,
    FPSENSOR_KEY_DOWN,   // 105
    FPSENSOR_KEY_UP,     // 106
    FPSENSOR_KEY_POWER,
    FPSENSOR_KEY_RIGHT,  // 108
#endif
    FPSENSOR_KEY_TAP,
    FPSENSOR_KEY_HEAVY,
    FPSENSOR_KEY_MAX = 1000,
} fpsensor_key_event_t;

struct fpsensor_key {
    int32_t key;
    uint32_t value;
};

/* define commands */
#define FPSENSOR_IOC_MAGIC      0xf0
#define FPSENSOR_IOC_INIT                       _IOWR(FPSENSOR_IOC_MAGIC,0,uint32_t)
#define FPSENSOR_IOC_EXIT                       _IOWR(FPSENSOR_IOC_MAGIC,1,uint32_t)
#define FPSENSOR_IOC_RESET                      _IOWR(FPSENSOR_IOC_MAGIC,2,uint32_t)
#define FPSENSOR_IOC_ENABLE_IRQ                 _IOWR(FPSENSOR_IOC_MAGIC,3,uint32_t)
#define FPSENSOR_IOC_DISABLE_IRQ                _IOWR(FPSENSOR_IOC_MAGIC,4,uint32_t)
#define FPSENSOR_IOC_GET_INT_VAL                _IOWR(FPSENSOR_IOC_MAGIC,5,uint32_t)
#define FPSENSOR_IOC_DISABLE_SPI_CLK            _IOWR(FPSENSOR_IOC_MAGIC,6,uint32_t)
#define FPSENSOR_IOC_ENABLE_SPI_CLK             _IOWR(FPSENSOR_IOC_MAGIC,7,uint32_t)
#define FPSENSOR_IOC_ENABLE_POWER               _IOWR(FPSENSOR_IOC_MAGIC,8,uint32_t)
#define FPSENSOR_IOC_DISABLE_POWER              _IOWR(FPSENSOR_IOC_MAGIC,9,uint32_t)
#define FPSENSOR_IOC_INPUT_KEY_EVENT            _IOWR(FPSENSOR_IOC_MAGIC,10,struct fpsensor_key)
/* fp sensor has change to sleep mode while screen off */
#define FPSENSOR_IOC_ENTER_SLEEP_MODE           _IOWR(FPSENSOR_IOC_MAGIC,11,uint32_t)
#define FPSENSOR_IOC_REMOVE                     _IOWR(FPSENSOR_IOC_MAGIC,12,uint32_t)
#define FPSENSOR_IOC_CANCEL_WAIT                _IOWR(FPSENSOR_IOC_MAGIC,13,uint32_t)
#define FPSENSOR_IOC_GET_FP_STATUS              _IOWR(FPSENSOR_IOC_MAGIC,19,uint32_t)
#define FPSENSOR_IOC_INIT_INPUT_DEV             _IOWR(FPSENSOR_IOC_MAGIC,20,uint32_t)
#define FPSENSOR_IOC_ENABLE_REPORT_BLANKON      _IOWR(FPSENSOR_IOC_MAGIC,21,uint32_t)
#define FPSENSOR_IOC_UPDATE_DRIVER_SN           _IOWR(FPSENSOR_IOC_MAGIC,22,uint32_t)
#define FPSENSOR_IOC_GET_CHIP_INFO              _IOWR(FPSENSOR_IOC_MAGIC,25,unsigned int*)

typedef struct {
    dev_t devno;
    struct class *class;
    struct device *device;
    struct cdev cdev;
#if defined(USE_PLATFORM_BUS)
    struct platform_device *platform_device;
#elif defined(USE_SPI_BUS)
    struct spi_device *spi_device;
#endif
    /*zte_fp_nav device*/
    struct platform_device *nav_dev;
    struct input_dev *input;
    unsigned int users;
    u8 device_available;    /* changed during fingerprint chip sleep and wakeup phase */
    u8 irq_enabled;
    volatile unsigned int RcvIRQ;
    int irq;
    int irq_gpio;
    int reset_gpio;
    int power_gpio;

    /* 0 fingerprint use system gpio control  power, 1 pmic/vddcammot power */
    int power_type;
    struct regulator *fp_reg;
    /* get the voltage from dts file */
    int power_voltage;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
    struct wakeup_source *ttw_wl;
#else
    struct wake_lock ttw_wl;
#endif

    wait_queue_head_t wq_irq_return;
    int cancel;
    u8 suspend_flag;
    struct notifier_block notifier;
    u8 fb_status;
    int enable_report_blankon;
#if defined(FPSENSOR_GET_CHIPID)
    u16 spi_freq_khz;
#endif

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
    struct zlog_client *zlog_fp_client;
#endif

} fpsensor_data_t;

typedef struct
{
    int32_t hardware_id;
    int32_t sensor_id;
    char ver_name[64];
}chipone_chip_info;

typedef enum {
	ERR_LOG = 0,
	WARN_LOG,
	INFO_LOG,
	DEBUG_LOG,
	ALL_LOG,
} chiponefp_debug_level_t;

static chiponefp_debug_level_t chipone_debug_level = INFO_LOG;

#define fp_debug(level, fmt, args...) do { \
			if (chipone_debug_level >= level) {\
				pr_warn("[chiponefp_info] " fmt, ##args); \
			} \
		} while (0)

#endif    /* __FPSENSOR_SPI_TEE_H */
