/*
 * FPC Capacitive Fingerprint sensor device driver
 *
 * This driver will control the platform resources that the FPC fingerprint
 * sensor needs to operate. The major things are probing the sensor to check
 * that it is actually connected and let the Kernel know this and with that also
 * enabling and disabling of regulators, enabling and disabling of platform
 * clocks.
 * *
 * The driver will expose most of its available functionality in sysfs which
 * enables dynamic control of these features from eg. a user space process.
 *
 * The sensor's IRQ events will be pushed to Kernel's event handling system and
 * are exposed in the drivers event node. This makes it possible for a user
 * space process to poll the input node and receive IRQ events easily. Usually
 * this node is available under /dev/input/eventX where 'X' is a number given by
 * the event system. A user space process will need to traverse all the event
 * nodes and ask for its parent's name (through EVIOCGNAME) which should match
 * the value in device tree named input-device-name.
 *
 *
 * Copyright (c) 2020-2021 Fingerprint Cards AB <tech@fingerprints.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License Version 2
 * as published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/pm_wakeup.h>
#include <linux/regulator/consumer.h>
#include <linux/version.h>
#include <linux/uaccess.h>
#include <linux/clk.h>

#define FPC_MODULE_NAME "fpc1020"
#define GENERIC_OK 0
#define GENERIC_ERR -1
#define FPC_GPIO_NUM 3

/*#define FPC_REE 0*/

#ifdef CONFIG_VENDOR_SOC_MTK_COMPILE
#define  USE_SPI_BUS
#include <linux/platform_data/spi-mt65xx.h>
extern void mt_spi_enable_master_clk(struct spi_device *spidev);
extern void mt_spi_disable_master_clk(struct spi_device *spidev);
#endif

#ifdef CONFIG_VENDOR_SOC_SPRD_COMPILE
#define  USE_PLATFORM_BUS
#endif

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
#include "zlog_common_base.h"
#endif

typedef enum {
	ERR_LOG = 0,
	WARN_LOG,
	INFO_LOG,
	DEBUG_LOG,
	ALL_LOG,
} fpc1020_debug_level_t;

static fpc1020_debug_level_t fpc1020_debug_level = INFO_LOG;

#define fpc_debug(level, fmt, args...) do { \
			if (fpc1020_debug_level >= level) {\
				pr_warn("[fpc_info] " fmt, ##args); \
			} \
		} while (0)

struct fpc_dev {
	int irq_num;
	int irq_gpio;
	int rst_gpio;
	int pwr_gpio;
    /* 0 fingerprint use system gpio control  power, 1 pmic power */
	int power_type;
	struct regulator *fp_reg;
	int power_voltage;
#ifdef USE_SPI_BUS
	struct spi_device *spidev;
#endif
	struct wakeup_source *ttw_wl;
	struct device_node *node;
#ifdef FPC_USE_PINCTRL
	struct pinctrl *ptl;
#endif

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	struct zlog_client *zlog_fp_client;
#endif

};

#if defined(USE_PLATFORM_BUS)
int fpc_plat_probe(struct platform_device *dev);
int fpc_plat_remove(struct platform_device *dev);
#elif defined(USE_SPI_BUS)
int fpc_spi_probe(struct spi_device *dev);
int fpc_spi_remove(struct spi_device *dev);
#endif
