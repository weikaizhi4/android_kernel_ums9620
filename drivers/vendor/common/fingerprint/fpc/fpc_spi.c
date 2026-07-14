/*
 * FPC Fingerprint sensor device driver
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
 * Copyright (c) 2018 Fingerprint Cards AB <tech@fingerprints.com>
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
#include <linux/clk.h>

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#endif

//#define FPC_DRIVER_FOR_ISEE

#define FPC_DEV_MAJOR 0

#ifdef FPC_DRIVER_FOR_ISEE
#include "teei_fp.h"
#include "tee_client_api.h"
#endif

#include "fpc_spi.h"

#ifdef FPC_DRIVER_FOR_ISEE
struct TEEC_UUID uuid_ta_fpc = { 0x7778c03f, 0xc30c, 0x4dd0,
	{0xa3, 0x19, 0xea, 0x29, 0x64, 0x3d, 0x4d, 0x4b}
};
#endif

#define FPC_RESET_LOW_US 5000
#define FPC_RESET_HIGH1_US 100
#define FPC_RESET_HIGH2_US 5000
#define FPC_TTW_HOLD_TIME 1000

static struct class *fpc_class;
static struct device *fpc_device;

#define N_SPI_MINORS		32	/* ... up to 256 */
static int major;

static struct fpc_data *g_fpc_data;

#define FPC_DRIVER_VERSION	"v2022-10-25"
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
struct zlog_mod_info fpc_zlog_fp_dev = {
	.module_no = ZLOG_MODULE_FP,
	.name = "fingerprint",
	.device_name = "KERR",
	.ic_name = "FPC",
	.module_name = "FP",
	.fops = NULL,
};
#endif

#ifdef USE_PIN_CTL
static DEFINE_MUTEX(spidev_set_gpio_mutex);

static int select_pin_ctl(struct fpc_data *fpc, const char *name)
{
	size_t i;
	int rc;
	struct device *dev = fpc->dev;
	for (i = 0; i < ARRAY_SIZE(fpc->pinctrl_state); i++) {
		const char *n = pctl_names[i];
		if (!strncmp(n, name, strlen(n))) {
			mutex_lock(&spidev_set_gpio_mutex);
			rc = pinctrl_select_state(fpc->pinctrl_fpc, fpc->pinctrl_state[i]);
			mutex_unlock(&spidev_set_gpio_mutex);
			if (rc)
				dev_err(dev, "cannot select '%s'\n", name);
			else
				dev_dbg(dev, "Selected '%s'\n", name);
			goto exit;
		}
	}
	rc = -EINVAL;
	dev_err(dev, "%s:'%s' not found\n", __func__, name);
exit:
	return rc;
}
#endif

static int fpc_gpio_hw_reset(struct fpc_data *fpc, unsigned int delay_ms)
{
	int ret = 0;

	fpc_debug(DEBUG_LOG, "%s enter!\n", __func__);

	if (fpc == NULL) {
		fpc_debug(ERR_LOG, "fpc_data is NULL\n");
		return -ENODEV;
	}
	if (gpio_is_valid(fpc->rst_gpio)) {
		/* gpio_direction_output(gf_dev->reset_gpio, 1); */
		gpio_set_value(fpc->rst_gpio, 0);
		msleep(20);
		gpio_set_value(fpc->rst_gpio, 1);
		msleep(delay_ms);
		fpc_debug(INFO_LOG, "----fpc hw reset ok----\n");
	}

	return ret;
}


static int set_clks(struct fpc_data *fpc, bool enable)
{
	int rc = 0;

	fpc_debug(DEBUG_LOG, "%s enter!\n", __func__);

	if (fpc == NULL) {
		fpc_debug(ERR_LOG, "fpc_data is NULL\n");
		return -EINVAL;
	}

	//if(fpc->clocks_enabled == enable)
	//	return rc;
#if defined(USE_SPI_BUS)
	if (enable) {
		mt_spi_enable_master_clk(fpc->spi);
		//fpc->clocks_enabled = true;
		//rc = 1;
		fpc_debug(DEBUG_LOG, "set_clks enable\n");
	} else {
		mt_spi_disable_master_clk(fpc->spi);
		//fpc->clocks_enabled = false;
		//rc = 0;
		fpc_debug(DEBUG_LOG, "set_clks disable\n");
	}
#elif defined(USE_PLATFORM_BUS)
	fpc_debug(DEBUG_LOG, "%s:not use spi!\n", __func__);
#endif
	return rc;
}

static int hw_reset(struct	fpc_data *fpc)
{
	int ret = 0;

	fpc_debug(DEBUG_LOG, "%s enter!\n", __func__);

	if (fpc == NULL) {
		fpc_debug(ERR_LOG, "fpc_data is NULL\n");
		return -ENODEV;
	}

#ifdef USE_PIN_CTL
	int irq_gpio;
	select_pin_ctl(fpc, "fpsensor_fpc_rst_high");
	usleep_range(FPC_RESET_HIGH1_US, FPC_RESET_HIGH1_US + 100);

	select_pin_ctl(fpc, "fpsensor_fpc_rst_low");
	usleep_range(FPC_RESET_LOW_US, FPC_RESET_LOW_US + 100);

	select_pin_ctl(fpc, "fpsensor_fpc_rst_high");
	usleep_range(FPC_RESET_HIGH2_US, FPC_RESET_HIGH2_US + 100);

	irq_gpio = gpio_get_value(fpc->irq_gpio);
	fpc_debug(ERR_LOG, "IRQ after reset %d\n", irq_gpio);
	fpc_debug(ERR_LOG, "Using GPIO#%d as IRQ.\n", fpc->irq_gpio );
	fpc_debug(ERR_LOG, "Using GPIO#%d as RST.\n", fpc->rst_gpio );
#else
	ret = fpc_gpio_hw_reset(fpc, 20);
	if (ret) {
		fpc_debug(ERR_LOG, "fpc_gpio_hw_reset failed, ret=%d\n", ret);
		return ret;
	}
#endif
	return ret;
}

static ssize_t hw_reset_set(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int rc;
	struct	fpc_data *fpc = NULL;

	fpc_debug(DEBUG_LOG, "%s enter!\n", __func__);

	fpc = g_fpc_data;
	if (fpc == NULL) {
		fpc_debug(ERR_LOG, "fpc_data is NULL\n");
		return -EINVAL;
	}

	if (!strncmp(buf, "reset", strlen("reset"))) {
		rc = hw_reset(fpc);
		return rc ? rc : count;
	}
	else {
		return -EINVAL;
	}
}
static DEVICE_ATTR(hw_reset, S_IWUSR, NULL, hw_reset_set);

/**
 * sysfs node for controlling whether the driver is allowed
 * to wake up the platform on interrupt.
 */
static ssize_t wakeup_enable_set(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct	fpc_data *fpc = NULL;
	ssize_t ret = count;

	fpc_debug(DEBUG_LOG, "%s enter!\n", __func__);

	fpc = g_fpc_data;
	if (fpc == NULL) {
		fpc_debug(ERR_LOG, "fpc_data is NULL\n");
		return -EINVAL;
	}

	if (!strncmp(buf, "enable", strlen("enable"))) {
		fpc->wakeup_enabled = true;
		smp_wmb();
	} else if (!strncmp(buf, "disable", strlen("disable"))) {
		fpc->wakeup_enabled = false;
		smp_wmb();
	}
	else {
		ret = -EINVAL;
	}
	return ret;
}
static DEVICE_ATTR(wakeup_enable, S_IWUSR, NULL, wakeup_enable_set);

/**
 * sysf node to check the interrupt status of the sensor, the interrupt
 * handler should perform sysf_notify to allow userland to poll the node.
 */
static ssize_t irq_get(struct device *device,
			struct device_attribute *attribute,
			char* buffer)
{
	struct	fpc_data *fpc = NULL;
	int irq = 0;

	fpc_debug(DEBUG_LOG, "%s enter!\n", __func__);

	fpc = g_fpc_data;
	if (fpc == NULL) {
		fpc_debug(ERR_LOG, "fpc_data is NULL\n");
		return -EINVAL;
	}
	irq = gpio_get_value(fpc->irq_gpio);

	fpc_debug(INFO_LOG, "irq_get irq=%d", irq);

	return scnprintf(buffer, PAGE_SIZE, "%i\n", irq);
}

/**
 * writing to the irq node will just drop a printk message
 * and return success, used for latency measurement.
 */
static ssize_t irq_ack(struct device *device,
			struct device_attribute *attribute,
			const char *buffer, size_t count)
{
	struct	fpc_data *fpc = NULL;

	fpc_debug(DEBUG_LOG, "%s enter!\n", __func__);

	fpc = g_fpc_data;
	if (fpc == NULL) {
		fpc_debug(ERR_LOG, "fpc_data is NULL\n");
		return -EINVAL;
	}
	fpc_debug(INFO_LOG, "%s\n", __func__);

	return count;
}
static DEVICE_ATTR(irq, S_IRUSR | S_IWUSR, irq_get, irq_ack);

static ssize_t clk_enable_set(struct device *device,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct	fpc_data *fpc = NULL;

	fpc_debug(DEBUG_LOG, "%s enter!\n", __func__);

	fpc = g_fpc_data;
	if (fpc == NULL) {
		fpc_debug(ERR_LOG, "fpc_data is NULL\n");
		return -EINVAL;
	}

	if (buf == NULL) {
		fpc_debug(ERR_LOG, "clk_enable_set buf is null\n");
		return -EINVAL;
	}

	fpc_debug(DEBUG_LOG,"clk_enable_set *buf = %s\n", buf);

	if (*buf == '1') {
		set_clks(fpc, true);
	}
	if (*buf == '0') {
		set_clks(fpc, false);
	}

	return count;
}
static DEVICE_ATTR(clk_enable, S_IWUSR, NULL, clk_enable_set);

static struct attribute *fpc_attributes[] = {
	&dev_attr_hw_reset.attr,
	&dev_attr_wakeup_enable.attr,
	&dev_attr_clk_enable.attr,
	&dev_attr_irq.attr,
	NULL
};

static const struct attribute_group fpc_attribute_group = {
	.attrs = fpc_attributes,
};

static irqreturn_t fpc_irq_handler(int irq, void *handle)
{
	struct fpc_data *fpc = handle;
	/* struct device *dev = fpc->dev; */
	static int current_level = 0; /* We assume low level from start */
	current_level = !current_level;

	if (current_level) {
		fpc_debug(DEBUG_LOG, "Reconfigure irq to trigger in low level\n");
		irq_set_irq_type(irq, IRQF_TRIGGER_LOW);
	} else {
		fpc_debug(DEBUG_LOG, "Reconfigure irq to trigger in high level\n");
		irq_set_irq_type(irq, IRQF_TRIGGER_HIGH);
	}

	/* Make sure 'wakeup_enabled' is updated before using it
	** since this is interrupt context (other thread...) */
	smp_rmb();
	if (fpc->wakeup_enabled) {
		__pm_wakeup_event(fpc->ttw_wl, FPC_TTW_HOLD_TIME);
	}

	sysfs_notify(&fpc->nav_dev->dev.kobj, NULL, dev_attr_irq.attr.name);

	return IRQ_HANDLED;
}

static int fpc_request_named_gpio(struct fpc_data *fpc, const char *label, int *gpio)
{
	struct device *dev = NULL;
	struct device_node *np =NULL;
	int ret = -1;
	fpc_debug(INFO_LOG, "%s enter!\n", __func__);
	if (fpc == NULL) {
		fpc_debug(ERR_LOG, "fpc is NULL\n");
		return -ENODEV;
	}

	dev = &fpc->spi->dev;

	np = dev->of_node;
	ret = of_get_named_gpio(np, label, 0);
	if (ret < 0) {
		fpc_debug(ERR_LOG, "failed to get %s\n", label);
		return ret;
	}
	*gpio = ret;
	if (gpio_is_valid(*gpio)) {
		ret = gpio_request(*gpio, label);
		if (ret) {
			fpc_debug(ERR_LOG, "failed to request gpio %d\n", *gpio);
			return ret;
		}
	}
	fpc_debug(INFO_LOG, "%s -> %d\n", label, *gpio);
	return ret;
}

static int fpc_power_on(struct fpc_data *fpc)
{
	int ret = 0;

	fpc_debug(INFO_LOG, "%s enter!\n", __func__);

	if (fpc == NULL) {
		fpc_debug(ERR_LOG, "fpc is NULL.\n");
		return -ENODEV;
	}

	if (fpc->power_type == 1) {
		/*  powered by pmic regulator */
		if (fpc->fp_reg != NULL){
			ret = regulator_enable(fpc->fp_reg);
			if (ret) {
				fpc_debug(ERR_LOG, "%s:regulator enable failed(%d)\n", __func__, ret);
				goto err;
			} else {
				fpc_debug(INFO_LOG, "----fpc regulator power on ok ----\n");
			}
		}
	} else {
		/* TODO: add your power control here */
		if (gpio_is_valid(fpc->power_gpio)) {
			gpio_set_value(fpc->power_gpio, 1);
			fpc_debug(INFO_LOG, "----fpc gpio power on ok ----\n");
		}
	}

	msleep(20);

err:
	return ret;
}

static int fpc_power_off(struct fpc_data *fpc)
{
	int ret = 0;

	fpc_debug(INFO_LOG, "%s enter!\n", __func__);

	if (fpc == NULL) {
		fpc_debug(ERR_LOG, "fpc is NULL.\n");
		return -ENODEV;
	}

	if (fpc->power_type == 1) {
		/*  powered by pmic regulator */
		if (fpc->fp_reg != NULL){
			ret = regulator_disable(fpc->fp_reg);
			if (ret) {
				fpc_debug(ERR_LOG, "%s:regulator disable failed(%d)\n", __func__, ret);
				goto err;
			} else {
				fpc_debug(INFO_LOG, "----fpc regulator power off ok----\n");
			}
		}
	} else {
		/* TODO: add your power control here */
		if (gpio_is_valid(fpc->power_gpio)) {
			gpio_set_value(fpc->power_gpio, 0);
			fpc_debug(INFO_LOG, "----fpc gpio power off ok----\n");
		}
	}
err:
	return ret;
}


static int fpc_get_power_type(struct fpc_data *fpc)
{
	struct device *dev = NULL;
	struct device_node *np =NULL;
	int ret = 0;
	fpc_debug(INFO_LOG, "%s enter!\n", __func__);
	if (fpc == NULL) {
		fpc_debug(ERR_LOG, "fpc is NULL\n");
		return -ENODEV;
	}

	dev = &fpc->spi->dev;

	np = dev->of_node;
	ret = of_property_read_u32(np, "power-type", &fpc->power_type);
	if (ret < 0) {
		fpc_debug(ERR_LOG, "failed to get power type from dts, use default gpio power\n");
	}
	fpc_debug(INFO_LOG, "%s:Power type[%d]\n", __func__, fpc->power_type);

	/* get power voltage from dts config */
	ret = of_property_read_u32(np, "power-voltage", &fpc->power_voltage);
	if (ret < 0) {
		fpc_debug(ERR_LOG, "Power voltage get failed from dts, ret=%d\n", ret);
	}
	fpc_debug(INFO_LOG, "%s:Power voltage[%d]\n", __func__, fpc->power_voltage);

	fpc_debug(INFO_LOG, "%s exit!\n", __func__);
	return 0;
}

static int fpc_fingerprint_power_init(struct fpc_data *fpc)
{
	int ret = 0;
	struct device *dev = NULL;

	fpc_debug(INFO_LOG, "%s enter!\n", __func__);

	if (fpc == NULL) {
		fpc_debug(ERR_LOG, "fpsensor_dev is NULL\n");
		return -ENODEV;
	}

	dev = fpc->dev;

	fpc_debug(INFO_LOG, "regulator_vdd");
	fpc->fp_reg = regulator_get(dev, "vdd");

	if (IS_ERR(fpc->fp_reg)) {
		fpc_debug(ERR_LOG, "%s:get regulator failed\n", __func__);
		return IS_ERR(fpc->fp_reg);
	}

	ret = regulator_set_voltage(fpc->fp_reg, fpc->power_voltage, fpc->power_voltage);

	if (ret) {
		fpc_debug(ERR_LOG, "%s:regulator_set_voltage(%d)\n", __func__, ret);
		goto err_power_init;
	}
	/*ret = regulator_enable(fpsensor_dev->fp_reg);
	if (ret) {
		fp_debug(ERR_LOG, "%s regulator enable failed(%d)\n",
			__func__, ret);
		goto err_power_init;
	}*/
	return ret;

err_power_init:
	regulator_put(fpc->fp_reg);
	return ret;
}

static int fpc_get_gpio_dts_info(struct fpc_data *fpc)
{
	int ret = 0;

	fpc_debug(INFO_LOG, "%s enter!\n", __func__);
	if (fpc == NULL) {
		fpc_debug(ERR_LOG, "fpc is NULL\n");
		return -ENODEV;
	}

	/* get interrupt gpio resource */
	ret = fpc_request_named_gpio(fpc, "fpint-gpios", &fpc->irq_gpio);
	if (ret) {
		fpc_debug(ERR_LOG, "Failed to request irq GPIO, err=%d\n", ret);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		if (fpc->zlog_fp_client) {
			zlog_client_record(fpc->zlog_fp_client, "Failed to request fpc irq gpio\n");
			zlog_client_notify(fpc->zlog_fp_client,  ZLOG_FP_REQUEST_INT_GPIO_ERROR_NO);
		}
#endif
		return -1;
	} else {
		fpc_debug(INFO_LOG, "%s:Success to request irq GPIO\n", __func__);
	}
	gpio_direction_input(fpc->irq_gpio);

	/* get reest gpio resourece */
	ret = fpc_request_named_gpio(fpc, "fpreset-gpios", &fpc->rst_gpio);
	if (ret) {
		fpc_debug(ERR_LOG, "Failed to request reset GPIO, err=%d\n", ret);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		if (fpc->zlog_fp_client) {
			zlog_client_record(fpc->zlog_fp_client, "Failed to request fpc rst gpio\n");
			zlog_client_notify(fpc->zlog_fp_client,  ZLOG_FP_REQUEST_RST_GPIO_ERROR_NO);
		}
#endif
		return -1;
	} else {
		fpc_debug(INFO_LOG, "%s:Success to request reset GPIO\n", __func__);
	}
	/* set reset direction output */
	gpio_direction_output(fpc->rst_gpio, 0);
	/*fpc_hw_reset(1250); */

	/* get power gpio resourece */
	ret = fpc_get_power_type(fpc);
	if (ret) {
		fpc_debug(ERR_LOG, "Failed to get power type, err=%d\n", ret);
		return -1;
	}

	if (1 == fpc->power_type) {
		fpc_debug(INFO_LOG, "%s PWR_MODE_REGULATOR!\n", __func__);
		/* powered by pmic regulator */
		ret = fpc_fingerprint_power_init(fpc);
		if (ret) {
			fpc_debug(ERR_LOG, "%s:fingerprint_power_init failed\n", __func__);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
			if (fpc->zlog_fp_client) {
				zlog_client_record(fpc->zlog_fp_client, "Failed to regulator get and set fpc vcc\n");
				zlog_client_notify(fpc->zlog_fp_client,  ZLOG_FP_REGULATOR_GET_SET_ERROR_NO);
			}
#endif
			return -1;
		}
	} else {
		fpc_debug(INFO_LOG, "%s PWR_MODE_GPIO!\n", __func__);
		ret = fpc_request_named_gpio(fpc, "fppwr-gpios", &fpc->power_gpio);
		if (ret) {
			fpc_debug(ERR_LOG, "Failed to request power GPIO, err=%d\n", ret);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
			if (fpc->zlog_fp_client) {
				zlog_client_record(fpc->zlog_fp_client, "Failed to request fpc pwr gpio\n");
				zlog_client_notify(fpc->zlog_fp_client,  ZLOG_FP_REQUEST_PWR_GPIO_ERROR_NO);
			}
#endif
			return -1;
		} else {
			fpc_debug(INFO_LOG, "%s:Success to request power GPIO\n", __func__);
		}
		/* set power direction output */
		gpio_direction_output(fpc->power_gpio, 0);
	}
	fpc_debug(INFO_LOG, "%s exit, ret=%d\n", __func__, ret);

	return ret;
}

static int fpc_gpio_free(struct fpc_data *fpc)
{
    int ret = 0;
    struct device *dev;

    fpc_debug(INFO_LOG, "%s enter!\n", __func__);

    if (fpc == NULL) {
        fpc_debug(ERR_LOG, "fpsensor is NULL\n");
        return -ENODEV;
    }

    dev = fpc->dev;

    if (gpio_is_valid(fpc->irq_gpio)) {
        gpio_free(fpc->irq_gpio);
        fpc->irq_gpio = 0;
        fpc_debug(INFO_LOG, "%s:remove irq_gpio success\n", __func__);
    }
    if (gpio_is_valid(fpc->rst_gpio)) {
        gpio_set_value(fpc->rst_gpio, 0);
        gpio_free(fpc->rst_gpio);
        fpc->rst_gpio = 0;
        fpc_debug(INFO_LOG, "%s:set reset low and remove reset_gpio success\n", __func__);
    }
    if (1 == fpc->power_type) {
        /* powered by pmic regulator */
        if (fpc->fp_reg != NULL) {
            /* regulator_disable has implement in fpc_power_off
            Fix unbalanced disables for VFP */
            /* ret = regulator_disable(fpc->fp_reg); */
            regulator_put(fpc->fp_reg);
            fpc->fp_reg = NULL;
            fpc_debug(INFO_LOG, "%s:regulator_disable and regulator_put success\n", __func__);
        }
    } else {
        if (fpc->power_gpio != 0) {
            gpio_set_value(fpc->power_gpio, 0);
            gpio_free(fpc->power_gpio);
            fpc->power_gpio = 0;
            fpc_debug(INFO_LOG, "%s:set power low and remove pwr_gpio success\n", __func__);
        }
    }
    fpc_debug(INFO_LOG, "%s exit, ret=%d\n", __func__, ret);
    return ret;
}

static int fpc_disable_irq(struct fpc_data *fpc)
{
    int ret = 0;
    fpc_debug(DEBUG_LOG, "%s enter!\n", __func__);

    if (fpc == NULL) {
        fpc_debug(ERR_LOG, "fpc is NULL.\n");
        return -ENODEV;
    }
    if (fpc->irq_num) {
		disable_irq_nosync(fpc->irq_num);
		//fpc->irq_enabled = 0;
		fpc_debug(INFO_LOG, "%s:disable_irq_nosync!\n", __func__);
	}
    //setRcvIRQ(0);
    fpc_debug(DEBUG_LOG, "%s exit\n", __func__);
    return ret;
}

static void fpc_irq_free(struct fpc_data *fpc)
{
    fpc_debug(INFO_LOG, "%s enter!\n", __func__);

	if (fpc == NULL) {
		fpc_debug(ERR_LOG, "[%s] the paramters is NULL\n", __func__);
		return;
	}

    if (fpc->irq_num) {
        free_irq(fpc->irq_num, fpc);
		fpc->irq_num = 0;
        fpc_debug(INFO_LOG, "%s:free_irq!\n", __func__);
    }
}

static int fpc_irq_setup(struct fpc_data *fpc)
{
	int ret = 0;
	int irqf = 0;

	if (fpc == NULL) {
		fpc_debug(ERR_LOG, "[%s] the paramters is NULL\n", __func__);
		return -1;
	}
	fpc->irq_num = gpio_to_irq(fpc->irq_gpio);
	fpc_debug(INFO_LOG, "fpc->irq_num= %d\n", fpc->irq_num);

	/* IRQF_NO_SUSPEND flag will cause can not unlock when suspend
	See Documentation/power/suspend-and-interrupts.txt
	irqf = IRQF_TRIGGER_HIGH | IRQF_ONESHOT | IRQF_NO_SUSPEND; */
	irqf = IRQF_TRIGGER_HIGH | IRQF_ONESHOT;
	fpc_debug(INFO_LOG, "irq flag:0x%08x\n", irqf);
	ret = request_threaded_irq(fpc->irq_num, NULL, fpc_irq_handler, irqf, dev_name(fpc->dev), fpc);
	if (ret) {
		fpc_debug(ERR_LOG, "[%s]could not request irq %d\n", __func__, fpc->irq_num);
		goto err1;
	}
	fpc_debug(INFO_LOG, "[%s] requested irq %d\n", __func__, fpc->irq_num);

	/* Request that the interrupt should be wakeable */
	enable_irq_wake(fpc->irq_num);

err1:
	return ret;

}
static int fpc_open(struct inode *inode, struct file *filp)
{
	int ret = 0;
	struct fpc_data *fpc = NULL;

	fpc_debug(INFO_LOG, "%s enter", __func__);

	fpc = g_fpc_data;

	if (fpc == NULL) {
		fpc_debug(ERR_LOG, "%s patamtes is bad", __func__);
		return -1;
	}

	filp->private_data = fpc;
	fpc->users++;
	ret = fpc_get_gpio_dts_info(fpc);
	if (ret) {
		fpc_debug(ERR_LOG, "[%s] fpc_get_gpio_dts_info falied, ret=%d", __func__, ret);
		return ret;
	}

	ret = fpc_irq_setup(fpc);
	if (ret) {
		fpc_debug(ERR_LOG, "[%s] fpc_irq_setup falied, ret=%d", __func__, ret);
		goto free_gpio;
	}

	ret = fpc_power_on(fpc);
	if (ret) {
		fpc_debug(ERR_LOG, "[%s] fpc_power_on falied, ret=%d", __func__, ret);
		goto free_gpio;
	}
	msleep(20);

	hw_reset(fpc);

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	fpc->zlog_fp_client = zlog_register_client(&fpc_zlog_fp_dev);
	if (fpc->zlog_fp_client) {
		fpc_debug(INFO_LOG, "%s zlog_register_fpc_client success\n", __func__);
	} else {
		fpc_debug(ERR_LOG, "%s zlog_register_fpc_client fail\n", __func__);
	}
#endif

	fpc_debug(INFO_LOG, "%s exit, ret=%d", __func__, ret);
	return ret;

free_gpio:
	fpc_irq_free(fpc);
	fpc_gpio_free(fpc);
	return ret;
}

static int fpc_release(struct inode *inode, struct file *filp)
{
	int ret = 0;
	struct fpc_data *fpc;

	fpc_debug(INFO_LOG, "%s enter!\n", __func__);

	fpc = filp->private_data;

	if (fpc == NULL) {
		fpc_debug(ERR_LOG, "%s patamtes is bad", __func__);
		return -1;
	}

	filp->private_data = NULL;

    /*last close??*/
    fpc->users--;
    if (fpc->users <= 0) {
		ret = fpc_power_off(fpc);
		if (ret) {
			fpc_debug(ERR_LOG, "[%s] fpc_power_off falied, ret=%d", __func__, ret);
		}
        fpc_debug(INFO_LOG, "%s:irq=%d\n", __func__, fpc->irq_num);
		fpc_disable_irq(fpc);
		fpc_debug(INFO_LOG, "%s:fpc_disable_irq\n", __func__);
        fpc_irq_free(fpc);
        fpc_debug(INFO_LOG, "%s:fpc_irq_free\n", __func__);
        //fpsensor_dev->device_available = 0;
        fpc_gpio_free(fpc);
    }

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
    if (fpc->zlog_fp_client) {
        zlog_unregister_client(fpc->zlog_fp_client);
        fpc_debug(INFO_LOG, "%s zlog_unregister_client fpc_zlog_fp_dev\n", __func__);
    }
#endif

    fpc_debug(INFO_LOG, "%s exit, ret=%d\n", __func__, ret);

    return ret;
}

static const struct file_operations fpc_fops = {
	.owner = THIS_MODULE,
	.open			= fpc_open,
	.release		= fpc_release,
};

#if defined(USE_PLATFORM_BUS)
static int fpc_probe(struct platform_device *spi)
#elif defined(USE_SPI_BUS)
static int fpc_probe(struct spi_device *spi)
#endif
{
	int ret = 0;
	struct device *dev;
	struct fpc_data *fpc;
	struct device_node *dnode = NULL;

	fpc_debug(INFO_LOG, "%s\n", __func__);

	dev = &spi->dev;

	fpc = kzalloc(sizeof(*fpc), GFP_KERNEL);
	if (!fpc) {
		fpc_debug(ERR_LOG, "failed to allocate memory for struct fpc_data\n");
		ret = -ENOMEM;
		goto exit;
	}

	g_fpc_data = fpc;

	fpc->spi = spi;
	fpc->dev = dev;

	//dev_set_drvdata(dev, fpc);

	fpc->users = 0;
	fpc->irq_gpio = 0;
	fpc->rst_gpio = 0;
	fpc->power_gpio = 0;
	fpc->power_type = 0;
	fpc->power_voltage = 2800000;
	fpc->fp_reg = NULL;
	fpc->nav_dev = NULL;
	fpc->clocks_enabled = false;
	fpc->wakeup_enabled = false;
	fpc->sys_created = 0;

	fpc->ttw_wl = wakeup_source_register(dev, "fpc_ttw_wl");

	/*fp_nav*/
	dnode = of_find_compatible_node(NULL, NULL, "zte_fp_nav");
	if (dnode) {
		fpc_debug(DEBUG_LOG, "fp-nav device node found!\n");
		fpc->nav_dev = of_find_device_by_node(dnode);
		if (fpc->nav_dev)
		{
			fpc_debug(INFO_LOG, "%s:fp-nav device uevent found!\n", __func__);
		} else {
			fpc_debug(ERR_LOG, "fp-nav device uevent not found!\n");
			goto exit;
		}
	} else {
		fpc_debug(ERR_LOG, "fp-nav device node not found!\n");
		goto exit;
	}

	if (!fpc->sys_created) {
		if (fpc->nav_dev == NULL) {
			fpc_debug(ERR_LOG, "fp-nav is NULL\n");
			goto exit;
		}
		ret = sysfs_create_group(&fpc->nav_dev->dev.kobj, &fpc_attribute_group);
		if (ret) {
			fpc_debug(ERR_LOG, "could not create sysfs\n");
			goto exit;
		}
		fpc->sys_created = 1;
	} else {
		fpc_debug(DEBUG_LOG, "sysfs has created, sys_created=%d\n", fpc->sys_created);
	}

	//(void)hw_reset(fpc);
	fpc_debug(DEBUG_LOG, "%s: ok\n", __func__);
	return ret;
exit:
	return ret;
}

#if defined(USE_SPI_BUS)
static int fpc_remove(struct spi_device *spi)
#elif defined(USE_PLATFORM_BUS)
static int fpc_remove(struct platform_device *spi)
#endif
{
	struct	fpc_data *fpc = g_fpc_data;
	if (fpc == NULL) {
		fpc_debug(ERR_LOG,"%s bad paramters", __func__);
		return -ENODEV;
	}

	if (fpc->nav_dev && fpc->sys_created) {
		sysfs_remove_group(&fpc->nav_dev->dev.kobj, &fpc_attribute_group);
	}
	wakeup_source_unregister(fpc->ttw_wl);
	kfree(fpc);
	fpc = NULL;
	fpc_debug(INFO_LOG, "%s\n", __func__);

	return 0;
}

#if defined(USE_SPI_BUS)
static struct of_device_id mt6797_of_match[] = {
	{ .compatible = "fpc,fpc_spi", },
	{}
};
MODULE_DEVICE_TABLE(of, mt6797_of_match);

static struct spi_driver fpc_driver = {
	.driver = {
		.name	= "fpc_spi",
		.owner	= THIS_MODULE,
		.of_match_table = mt6797_of_match,
	},
#elif defined(USE_PLATFORM_BUS)
static struct of_device_id sprd_of_match[] = {
	{ .compatible = "fpc,fpc_platform", },
	{}
};
MODULE_DEVICE_TABLE(of, sprd_of_match);

static struct platform_driver fpc_driver = {
	.driver = {
		.name	= "fpc_platform",
		.owner	= THIS_MODULE,
		.of_match_table = sprd_of_match,
	},
#endif
	.probe	= fpc_probe,
	.remove	= fpc_remove
};

int fpc_sensor_init(void)
{
	int status = 0;

	fpc_debug(INFO_LOG, "[%s][%d] enter, driver version:%s\n", __func__, __LINE__, FPC_DRIVER_VERSION);

	BUILD_BUG_ON(N_SPI_MINORS > 256);
	major = register_chrdev(FPC_DEV_MAJOR, "fpc_dev", &fpc_fops);

	if (major < 0) {
		fpc_debug(ERR_LOG, "[%s] register_chrdev  failed! status=%d\n", __func__, major);
		return major;
	}

	fpc_class = class_create(THIS_MODULE, "fpc_fp");

	if (IS_ERR(fpc_class)) {
		fpc_debug(ERR_LOG, "[%s] class_create silfp_class failed!\n", __func__);
		unregister_chrdev(major, fpc_driver.driver.name);
		return PTR_ERR(fpc_class);
	}

	fpc_device = device_create(fpc_class, NULL, MKDEV(major, 0), NULL, "fpc_fp");
	if(IS_ERR(fpc_device)) {
		class_destroy(fpc_class);
		unregister_chrdev(major,"hello");
		return -EBUSY;
	}

#if defined(USE_SPI_BUS)
	status = spi_register_driver(&fpc_driver);
	fpc_debug(INFO_LOG, "----fpc spi_register_driver ok----status : %d\n", status);
#elif defined(USE_PLATFORM_BUS)
	status = platform_driver_register(&fpc_driver);
	fpc_debug(INFO_LOG, "----fpc platform_driver_register ok----status : %d\n", status);
#endif
	if (status < 0) {
		device_destroy(fpc_class, MKDEV(major, 0));
		class_destroy(fpc_class);
		unregister_chrdev(major, fpc_driver.driver.name);
		fpc_debug(ERR_LOG, "[%s] register_driver fail! ret=%d\n", __func__, status);
		return status;
	}
	fpc_debug(INFO_LOG, "[%s] module_init success!\n", __func__);
	return status;
}

void fpc_sensor_exit(void)
{
	fpc_debug(INFO_LOG, "[%s] enter!\n", __func__);
#if defined(USE_SPI_BUS)
	spi_unregister_driver(&fpc_driver);
#elif defined(USE_PLATFORM_BUS)
	platform_driver_unregister(&fpc_driver);
#endif
	device_destroy(fpc_class, MKDEV(major, 0));
	class_destroy(fpc_class);
	unregister_chrdev(major, fpc_driver.driver.name);
}

/*module_init(fpc_sensor_init);
module_exit(fpc_sensor_exit);*/

MODULE_AUTHOR("Jack. Wang, <jack.wang@fingerprints.com>");
MODULE_DESCRIPTION("fpc fingerprint sensor device driver");
MODULE_LICENSE("GPL");

