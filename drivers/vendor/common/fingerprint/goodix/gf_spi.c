/*
 * TEE driver for goodix fingerprint sensor
 * Copyright (C) 2016 Goodix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#define pr_fmt(fmt)		KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/ktime.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/timer.h>
#include <linux/notifier.h>
#include <linux/fb.h>
#include <linux/pm_qos.h>
#include <linux/cpufreq.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
#include <linux/pm_wakeup.h>
#else
#include <linux/wakelock.h>
#endif
#include <linux/kdev_t.h>
#include <linux/proc_fs.h>

#include "gf_spi.h"

#define VER_MAJOR   1
#define VER_MINOR   2
#define PATCH_LEVEL 11
#define GOODIXFP_DRIVER_VERSION		"v2022-10-20"

#define WAKELOCK_HOLD_TIME 500 /* in ms */

#define GF_SPIDEV_NAME     "goodix,fingerprint"
/*device name after register in charater*/
#define GF_DEV_NAME            "goodix_fp"
#define	GF_INPUT_NAME	    "qwerty"	/*"goodix_fp" */

#define	CHRD_DRIVER_NAME	"goodix_fp_spi"
#define	CLASS_NAME		    "goodix_fp"

#define N_SPI_MINORS		32	/* ... up to 256 */
static int SPIDEV_MAJOR;

static DECLARE_BITMAP(minors, N_SPI_MINORS);
static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
static struct wakeup_source *fp_wakelock;
#else
static struct wake_lock fp_wakelock;
#endif
static struct gf_dev gf;

static struct gf_key_map maps[] = {
	{ EV_KEY, GF_KEY_INPUT_HOME },
	{ EV_KEY, GF_KEY_INPUT_MENU },
	{ EV_KEY, GF_KEY_INPUT_BACK },
	{ EV_KEY, GF_KEY_INPUT_POWER },
#if defined(SUPPORT_NAV_EVENT)
	{ EV_KEY, GF_NAV_INPUT_UP },
	{ EV_KEY, GF_NAV_INPUT_DOWN },
	{ EV_KEY, GF_NAV_INPUT_RIGHT },
	{ EV_KEY, GF_NAV_INPUT_LEFT },
	{ EV_KEY, GF_KEY_INPUT_CAMERA },
	{ EV_KEY, GF_NAV_INPUT_CLICK },
	{ EV_KEY, GF_NAV_INPUT_DOUBLE_CLICK },
	{ EV_KEY, GF_NAV_INPUT_LONG_PRESS },
	{ EV_KEY, GF_NAV_INPUT_HEAVY },
#endif
};

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
struct zlog_mod_info goodix_zlog_fp_dev = {
	.module_no = ZLOG_MODULE_FP,
	.name = "fingerprint",
	.device_name = "ShenYue",
	.ic_name = "goodix",
	.module_name = "FP",
	.fops = NULL,
};
#endif

/*fp nav*/
static void gf_report_uevent(struct gf_dev *gf_dev, char *str)
{

	char *envp[2];

	gf_debug(DEBUG_LOG, "%s enter!\n", __func__);

	envp[0] = str;
	envp[1] = NULL;

	if (gf_dev->nav_dev) {
		gf_debug(DEBUG_LOG, "%s:gf_dev->nav_dev is not null!\n", __func__);
		kobject_uevent_env(&(gf_dev->nav_dev->dev.kobj), KOBJ_CHANGE, envp);
	} else {
		gf_debug(ERR_LOG, "%s:gf_dev->nav_dev is null!\n", __func__);
	}
}

static void gf_enable_irq(struct gf_dev *gf_dev)
{
	gf_debug(DEBUG_LOG, "%s enter!\n", __func__);

	if (gf_dev->irq_enabled) {
		gf_debug(WARN_LOG, "%s:IRQ has been enabled\n", __func__);
	} else {
		enable_irq(gf_dev->irq);
		gf_dev->irq_enabled = 1;
		gf_debug(DEBUG_LOG, "%s:enable_irq\n", __func__);
	}

}

static void gf_disable_irq(struct gf_dev *gf_dev)
{
	gf_debug(DEBUG_LOG, "%s enter!\n", __func__);

	if (gf_dev->irq_enabled) {
		disable_irq_nosync(gf_dev->irq);
		gf_dev->irq_enabled = 0;
		gf_debug(DEBUG_LOG, "%s:disable_irq\n", __func__);
	} else {
		gf_debug(WARN_LOG, "%s:IRQ has been disabled\n", __func__);
	}

}

#ifdef AP_CONTROL_CLK
static long spi_clk_max_rate(struct clk *clk, unsigned long rate)
{
	long lowest_available, nearest_low, step_size, cur;
	long step_direction = -1;
	long guess = rate;
	int max_steps = 10;

	cur = clk_round_rate(clk, rate);
	if (cur == rate)
		return rate;

	/* if we got here then: cur > rate */
	lowest_available = clk_round_rate(clk, 0);
	if (lowest_available > rate)
		return -EINVAL;

	step_size = (rate - lowest_available) >> 1;
	nearest_low = lowest_available;

	while (max_steps-- && step_size) {
		guess += step_size * step_direction;
		cur = clk_round_rate(clk, guess);

		if ((cur < rate) && (cur > nearest_low))
			nearest_low = cur;
		/*
		 * if we stepped too far, then start stepping in the other
		 * direction with half the step size
		 */
		if (((cur > rate) && (step_direction > 0))
				|| ((cur < rate) && (step_direction < 0))) {
			step_direction = -step_direction;
			step_size >>= 1;
		}
	}
	return nearest_low;
}

static void spi_clock_set(struct gf_dev *gf_dev, int speed)
{
	long rate;
	int rc;

	rate = spi_clk_max_rate(gf_dev->core_clk, speed);
	if (rate < 0) {
		gf_debug(ERR_LOG, "%s:no match found for requested clock frequency:%d",
				__func__, speed);
		return;
	}

	rc = clk_set_rate(gf_dev->core_clk, rate);
}

static int gfspi_ioctl_clk_init(struct gf_dev *data)
{

	data->clk_enabled = 0;
	data->core_clk = clk_get(&data->spi->dev, "core_clk");
	if (IS_ERR_OR_NULL(data->core_clk)) {
		gf_debug(ERR_LOG, "%s:fail to get core_clk\n", __func__);
		return -EPERM;
	}
	data->iface_clk = clk_get(&data->spi->dev, "iface_clk");
	if (IS_ERR_OR_NULL(data->iface_clk)) {
		gf_debug(ERR_LOG, "%s:fail to get iface_clk\n", __func__);
		clk_put(data->core_clk);
		data->core_clk = NULL;
		return -ENOENT;
	}
	return 0;
}

static int gfspi_ioctl_clk_enable(struct gf_dev *data)
{
	int err;

	if (data->clk_enabled)
		return 0;

	err = clk_prepare_enable(data->core_clk);
	if (err) {
		gf_debug(ERR_LOG, "%s:fail to enable core_clk\n", __func__);
		return -EPERM;
	}

	err = clk_prepare_enable(data->iface_clk);
	if (err) {
		gf_debug(ERR_LOG, "%s:fail to enable iface_clk\n", __func__);
		clk_disable_unprepare(data->core_clk);
		return -ENOENT;
	}

	data->clk_enabled = 1;

	return 0;
}

static int gfspi_ioctl_clk_disable(struct gf_dev *data)
{

	if (!data->clk_enabled)
		return 0;

	clk_disable_unprepare(data->core_clk);
	clk_disable_unprepare(data->iface_clk);
	data->clk_enabled = 0;

	return 0;
}

static int gfspi_ioctl_clk_uninit(struct gf_dev *data)
{

	if (data->clk_enabled)
		gfspi_ioctl_clk_disable(data);

	if (!IS_ERR_OR_NULL(data->core_clk)) {
		clk_put(data->core_clk);
		data->core_clk = NULL;
	}

	if (!IS_ERR_OR_NULL(data->iface_clk)) {
		clk_put(data->iface_clk);
		data->iface_clk = NULL;
	}

	return 0;
}
#endif

static void nav_event_input(struct gf_dev *gf_dev, uint16_t nav_event)
{
	uint32_t nav_input = 0;

	gf_debug(DEBUG_LOG, "%s enter!nav_event = %d\n", __func__, nav_event);
	switch (nav_event) {
	case GF_NAV_FINGER_DOWN:
		gf_debug(DEBUG_LOG, "%s:nav finger down\n", __func__);
		break;

	case GF_NAV_FINGER_UP:
		gf_debug(DEBUG_LOG, "%s:nav finger up\n", __func__);
		break;

	case GF_NAV_DOWN:
		nav_input = GF_NAV_DOWN;
		gf_report_uevent(gf_dev, FP_NAV_DOWN);
		gf_debug(INFO_LOG, "%s:nav down\n", __func__);
		break;

	case GF_NAV_UP:
		nav_input = GF_NAV_UP;
		gf_report_uevent(gf_dev, FP_NAV_UP);
		gf_debug(INFO_LOG, "%s:nav up\n", __func__);
		break;

	case GF_NAV_LEFT:
		nav_input = GF_NAV_LEFT;
		gf_report_uevent(gf_dev, FP_NAV_LEFT);
		gf_debug(INFO_LOG, "%s:nav left\n", __func__);
		break;

	case GF_NAV_RIGHT:
		nav_input = GF_NAV_RIGHT;
		gf_report_uevent(gf_dev, FP_NAV_RIGHT);
		gf_debug(INFO_LOG, "%s:nav right\n", __func__);
		break;

	case GF_NAV_CLICK:
		nav_input = GF_NAV_INPUT_CLICK;
		gf_debug(DEBUG_LOG, "%s:nav click\n", __func__);
		break;

	case GF_NAV_HEAVY:
		nav_input = GF_NAV_INPUT_HEAVY;
		gf_debug(DEBUG_LOG, "%s:nav heavy\n", __func__);
		break;

	case GF_NAV_LONG_PRESS:
		nav_input = GF_NAV_INPUT_LONG_PRESS;
		gf_debug(DEBUG_LOG, "%s:nav long press\n", __func__);
		break;

	case GF_NAV_DOUBLE_CLICK:
		nav_input = GF_NAV_INPUT_DOUBLE_CLICK;
		gf_debug(DEBUG_LOG, "%s:nav double click\n", __func__);
		break;

	default:
		gf_debug(ERR_LOG, "%s:unknown nav event: %d\n", __func__, nav_event);
		break;
	}

	/*if ((nav_event != GF_NAV_FINGER_DOWN) &&
			(nav_event != GF_NAV_FINGER_UP)) {
		input_report_key(gf_dev->input, nav_input, 1);
		input_sync(gf_dev->input);
		input_report_key(gf_dev->input, nav_input, 0);
		input_sync(gf_dev->input);
	}*/
}

static irqreturn_t gf_irq(int irq, void *handle)
{
#if defined(GF_NETLINK_ENABLE)
	char msg = GF_NET_EVENT_IRQ;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
	/* __pm_wakeup_event(fp_wakelock, msecs_to_jiffies(WAKELOCK_HOLD_TIME)); */
	__pm_wakeup_event(fp_wakelock, 1000);
#else
	wake_lock_timeout(&fp_wakelock, msecs_to_jiffies(WAKELOCK_HOLD_TIME));
#endif
	sendnlmsg(&msg);
#elif defined(GF_FASYNC)
	struct gf_dev *gf_dev = &gf;

	if (gf_dev->async)
		kill_fasync(&gf_dev->async, SIGIO, POLL_IN);
#endif

	return IRQ_HANDLED;
}

static int irq_setup(struct gf_dev *gf_dev)
{
	int status;

	gf_debug(DEBUG_LOG, "%s enter!\n", __func__);

	gf_dev->irq = gf_irq_num(gf_dev);
	gf_debug(INFO_LOG, "gf_dev->irq:[%d]\n", gf_dev->irq);

	/*status = request_threaded_irq(gf_dev->irq, NULL, gf_irq,
			IRQF_TRIGGER_RISING | IRQF_ONESHOT | IRQF_NO_SUSPEND,
			"gf", gf_dev);*/
	/* IRQF_NO_SUSPEND flag will cause can not unlock when suspend in MTK kernel4.19
	See Documentation/power/suspend-and-interrupts.txt */
	status = request_threaded_irq(gf_dev->irq, NULL, gf_irq,
			IRQF_TRIGGER_RISING | IRQF_ONESHOT,
			"gf", gf_dev);

	if (status) {
		gf_debug(ERR_LOG, "%s:failed to request_threaded_irq, err is %d\n", __func__, status);
		return status;
	} else {
		gf_debug(INFO_LOG, "%s:success to request_threaded_irq\n", __func__);
	}
	enable_irq_wake(gf_dev->irq);
	gf_dev->irq_enabled = 1;

	return status;
}

/*static void irq_cleanup(struct gf_dev *gf_dev)
{
	gf_debug(DEBUG_LOG, "%s enter!\n", __func__);

	gf_dev->irq_enabled = 0;
	disable_irq(gf_dev->irq);
	disable_irq_wake(gf_dev->irq);
	gf_debug(DEBUG_LOG, "%s : free_irq\n", __func__);
	free_irq(gf_dev->irq, gf_dev);
}*/

static void gf_kernel_key_input(struct gf_dev *gf_dev, struct gf_key *gf_key)
{
	uint32_t key_input = 0;

	if (gf_key->key == GF_KEY_HOME) {
		key_input = GF_KEY_INPUT_HOME;
	} else if (gf_key->key == GF_KEY_POWER) {
		key_input = GF_KEY_INPUT_POWER;
	} else if (gf_key->key == GF_KEY_CAMERA) {
		key_input = GF_KEY_INPUT_CAMERA;
	} else {
		/* add special key define */
		key_input = gf_key->key;
	}
	gf_debug(DEBUG_LOG, "%s:received key event[%d], key=%d, value=%d\n",
			__func__, key_input, gf_key->key, gf_key->value);

#if defined(SUPPORT_KEY_EVENT)
	if ((GF_KEY_POWER == gf_key->key || GF_KEY_CAMERA == gf_key->key)
			&& (gf_key->value == 1)) {
		input_report_key(gf_dev->input, key_input, 1);
		input_sync(gf_dev->input);
		input_report_key(gf_dev->input, key_input, 0);
		input_sync(gf_dev->input);
	}

	if (gf_key->key == GF_KEY_HOME) {
		input_report_key(gf_dev->input, key_input, gf_key->value);
		input_sync(gf_dev->input);
	}
#endif
}

static long gf_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct gf_dev *gf_dev = &gf;
	struct gf_key gf_key;
#if defined(SUPPORT_NAV_EVENT)
	uint16_t nav_event = GF_NAV_NONE;
#endif
	int retval = 0;
	u8 netlink_route = NETLINK_TEST;
	struct gf_ioc_chip_info info;

	gf_debug(DEBUG_LOG, "%s enter!cmd = 0x%x\n", __func__, cmd);

	if (_IOC_TYPE(cmd) != GF_IOC_MAGIC)
		return -ENODEV;

	if (_IOC_DIR(cmd) & _IOC_READ)
#if (KERNEL_VERSION(5, 4, 0) > LINUX_VERSION_CODE)
		retval = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
#else
		retval = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
#endif
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
#if (KERNEL_VERSION(5, 4, 0) > LINUX_VERSION_CODE)
		retval = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
#else
		retval = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
#endif
	if (retval)
		return -EFAULT;

	switch (cmd) {
	case GF_IOC_INIT:
		gf_debug(DEBUG_LOG, "%s:GF_IOC_INIT\n", __func__);
		if (copy_to_user((void __user *)arg, (void *)&netlink_route, sizeof(u8))) {
			gf_debug(ERR_LOG, "GF_IOC_INIT failed\n");
			retval = -EFAULT;
			break;
		}
		break;

	case GF_IOC_EXIT:
		gf_debug(DEBUG_LOG, "%s:GF_IOC_EXIT\n", __func__);
		break;

	case GF_IOC_DISABLE_IRQ:
		gf_debug(DEBUG_LOG, "%s:GF_IOC_DISABEL_IRQ\n", __func__);
		gf_disable_irq(gf_dev);
		break;

	case GF_IOC_ENABLE_IRQ:
		gf_debug(INFO_LOG, "%s:GF_IOC_ENABLE_IRQ\n", __func__);
		gf_enable_irq(gf_dev);
		break;

	case GF_IOC_RESET:
		gf_debug(INFO_LOG, "%s:GF_IOC_RESET\n", __func__);
		gf_hw_reset(gf_dev, 3);
		break;

	case GF_IOC_INPUT_KEY_EVENT:
		gf_debug(DEBUG_LOG, "%s:GF_IOC_INPUT_KEY_EVENT\n", __func__);
		if (copy_from_user(&gf_key, (void __user *)arg, sizeof(struct gf_key))) {
			gf_debug(ERR_LOG, "failed to copy input key event from user to kernel\n");
			retval = -EFAULT;
			break;
		}

		gf_kernel_key_input(gf_dev, &gf_key);
		break;

#if defined(SUPPORT_NAV_EVENT)
	case GF_IOC_NAV_EVENT:
		gf_debug(DEBUG_LOG, "%s:GF_IOC_NAV_EVENT\n", __func__);
		gf_debug(DEBUG_LOG, "nav_event = %d\n", nav_event);
		if (copy_from_user(&nav_event, (uint16_t *)arg, sizeof(uint16_t))) {
			gf_debug(ERR_LOG, "failed to copy nav event from user to kernel\n");
			retval = -EFAULT;
			break;
		}
		gf_debug(DEBUG_LOG, "nav_event = %d\n", nav_event);
		nav_event_input(gf_dev, nav_event);
		break;
#endif

	case GF_IOC_ENABLE_SPI_CLK:
		gf_debug(DEBUG_LOG, "%s:GF_IOC_ENABLE_SPI_CLK\n", __func__);
#if defined(USE_SPI_BUS)
		mt_spi_enable_master_clk(gf_dev->spi);
#endif

#ifdef AP_CONTROL_CLK
		gfspi_ioctl_clk_enable(gf_dev);
#else
		gf_debug(DEBUG_LOG, "doesn't support control clock!\n");
#endif
		break;

	case GF_IOC_DISABLE_SPI_CLK:
		gf_debug(DEBUG_LOG, "%s:GF_IOC_DISABLE_SPI_CLK\n", __func__);
#if defined(USE_SPI_BUS)
		mt_spi_disable_master_clk(gf_dev->spi);
#endif

#ifdef AP_CONTROL_CLK
		gfspi_ioctl_clk_disable(gf_dev);
#else
		gf_debug(DEBUG_LOG, "doesn't support control clock!\n");
#endif
		break;

	case GF_IOC_ENABLE_POWER:
		gf_debug(INFO_LOG, "%s:GF_IOC_ENABLE_POWER\n", __func__);
		retval = gf_power_on(gf_dev);
		break;

	case GF_IOC_DISABLE_POWER:
		gf_debug(INFO_LOG, "%s:GF_IOC_DISABLE_POWER\n", __func__);
		if (gpio_is_valid(gf_dev->reset_gpio)) {
			gpio_set_value(gf_dev->reset_gpio, 0);
			gf_debug(DEBUG_LOG, "before GF_IOC_DISABLE_POWER reset gpio set low success!\n");
		}
		msleep(2);
		retval = gf_power_off(gf_dev);
		break;

	case GF_IOC_ENTER_SLEEP_MODE:
		gf_debug(DEBUG_LOG, "%s:GF_IOC_ENTER_SLEEP_MODE\n", __func__);
		break;

	case GF_IOC_GET_FW_INFO:
		gf_debug(DEBUG_LOG, "%s:GF_IOC_GET_FW_INFO\n", __func__);
		break;

	case GF_IOC_REMOVE:
		gf_debug(INFO_LOG, "%s:GF_IOC_REMOVE\n", __func__);
		/*irq_cleanup(gf_dev);*/
		/*gf_cleanup(gf_dev);*/
		break;

	case GF_IOC_CHIP_INFO:
		gf_debug(DEBUG_LOG, "%s:GF_IOC_CHIP_INFO\n", __func__);
		if (copy_from_user(&info, (void __user *)arg, sizeof(struct gf_ioc_chip_info))) {
			retval = -EFAULT;
			break;
		}
		gf_debug(DEBUG_LOG, "vendor_id : 0x%x\n", info.vendor_id);
		gf_debug(DEBUG_LOG, "mode : 0x%x\n", info.mode);
		gf_debug(DEBUG_LOG, "operation: 0x%x\n", info.operation);
		break;

	default:
		gf_debug(ERR_LOG, "unsupport cmd:0x%x\n", cmd);
		break;
	}

	return retval;
}

#ifdef CONFIG_COMPAT
static long gf_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	return gf_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#endif /*CONFIG_COMPAT*/

#ifdef SUPPORT_REE_SPI
#define ERR_NO_SENSOR    111

struct gf_tx_buf_t {
	uint8_t cmd;
	uint8_t addr_h;
	uint8_t addr_l;
	uint8_t len_h;
	uint8_t len_l;
	uint8_t buf[512];
};

struct gf_rx_buf_t {
	uint8_t cmd;
	uint8_t buf[512];
};

static u32 gf_spi_speed = 1*1000000;

static u8 g_vendor_id = 0;

static int gf_check_gf3658_chip_id(struct gf_dev *gf_dev);
int gf_spi_read_bytes_ree_new(struct gf_dev *gf_dev, u16 addr, u32 data_len, u8 *buf);


static ssize_t gf_debug_show(struct device *dev,
			     struct device_attribute *attr, char *buf);

static ssize_t gf_debug_store(struct device *dev,
			      struct device_attribute *attr, const char *buf, size_t count);

static DEVICE_ATTR(debug, S_IRUGO | S_IWUSR, gf_debug_show, gf_debug_store);

static struct attribute *gf_debug_attrs[] = {
	&dev_attr_debug.attr,
	NULL
};

static const struct attribute_group gf_debug_attr_group = {
	.attrs = gf_debug_attrs,
	.name = "debug"
};

/* -------------------------------------------------------------------- */
/* devfs                                                              */
/* -------------------------------------------------------------------- */
static ssize_t gf_debug_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	gf_debug(DEBUG_LOG, "%s enter!\n", __func__);
	return sprintf(buf, "vendor id 0x%x\n", g_vendor_id);
}

static ssize_t gf_debug_store(struct device *dev,
			      struct device_attribute *attr, const char *buf, size_t count)
{
	struct gf_dev *gf_dev = &gf;
	int retval = 0;
	int flag=0;
	int reg = 0;
	unsigned char rbuf[4] = {0};

	if (sscanf(buf, "%x, %x", &flag, &reg) != 2 ){
		gf_debug(ERR_LOG, "%s: the number of data are wrong!\n", __func__);
		goto exit;
	}

	gf_debug(INFO_LOG, "%s: flag:0x%x, reg:0x%x\n", __func__, flag, reg);

	if (gf_dev == NULL ){
		gf_debug(ERR_LOG, "%s: gf_dev is NULL \n", __func__);
		goto exit;
	}

	if (flag == 1) {
		gf_debug(INFO_LOG, "%s: parameter is -1, power on test===============\n", __func__);
		retval = gf_power_on(gf_dev);
	} else if (flag == 2) {
		gf_debug(INFO_LOG, "%s: parameter is -2, reset test===============\n", __func__);
		gf_hw_reset(gf_dev, 5);
	} else if (flag == 3) {
		gf_debug(INFO_LOG, "%s: parameter is -3, power off test===============\n", __func__);
		retval = gf_power_off(gf_dev);
	} else if (flag == 3) {
		gf_debug(INFO_LOG, "%s: parameter is -8, enable spi clock test===============\n", __func__);
		mt_spi_enable_master_clk(gf_dev->spi);
	} else if (flag == 4) {
		gf_debug(INFO_LOG, "%s: parameter is -9, disable spi clock test===============\n", __func__);
		mt_spi_disable_master_clk(gf_dev->spi);
	} else if (flag == 5) {
		gf_debug(INFO_LOG, "%s: parameter is -id, spi read gf3658 ID test===============\n", __func__);
		gf_check_gf3658_chip_id(gf_dev);
	} else if (flag == 6) {
		gf_debug(INFO_LOG, "%s: reg is [0x%x] ===============\n", __func__, reg);
		/* read data start from offset 4 */
		gf_spi_read_bytes_ree_new(gf_dev, (u16)reg, 4, rbuf);
		gf_debug(INFO_LOG, "%s:reg[0x%x] is 0x%02x 0x%02x 0x%02x 0x%02x\n", __func__,
			 reg, rbuf[3], rbuf[2], rbuf[1], rbuf[0]);
	} else {
		gf_debug(ERR_LOG, "%s: wrong parameter!===============\n", __func__);
	}

exit:
	return count;
}

void endian_exchange(u8 *buf, u32 len)
{
	u32 i;
	u8 buf_tmp;
	u32 size = len / 2;

	gf_debug(DEBUG_LOG, "%s enter!\n", __func__);

	for (i = 0; i < size; i++) {
		buf_tmp = buf[2 * i + 1];
		buf[2 * i + 1] = buf[2 * i];
		buf[2 * i] = buf_tmp;
	}
}

int gf_spi_read_bytes_ree_new(struct gf_dev *gf_dev, u16 addr, u32 data_len, u8 *buf)
{
	struct spi_message msg;
	struct spi_transfer xfer;

	struct gf_tx_buf_t *g_tx_buf;
	struct gf_rx_buf_t *g_rx_buf;

	gf_debug(DEBUG_LOG, "%s enter!\n", __func__);

	g_tx_buf = kzalloc(512 + 5, GFP_KERNEL);
	g_rx_buf = kzalloc(512 + 5, GFP_KERNEL);

	 gf_debug(INFO_LOG,"%s %d g_tx_buf:%p g_rx_buf:%p\n", __func__, __LINE__, g_tx_buf, g_rx_buf); 

	g_tx_buf->cmd = 0xF0;
	g_tx_buf->addr_h = (uint8_t) ((addr >> 8) & 0xFF);
	g_tx_buf->addr_l = (uint8_t) (addr & 0xFF);

	spi_message_init(&msg);
	memset(&xfer, 0, sizeof(struct spi_transfer));
	xfer.tx_buf = g_tx_buf;
	xfer.rx_buf = g_rx_buf;
	xfer.len = 3;

	xfer.speed_hz = gf_spi_speed;

	spi_message_add_tail(&xfer, &msg);
	spi_sync(gf_dev->spi, &msg);

	spi_message_init(&msg);
	memset(&xfer, 0, sizeof(struct spi_transfer));
	g_tx_buf->cmd = 0xF1;

	xfer.tx_buf = g_tx_buf;
	xfer.rx_buf = g_rx_buf;

	xfer.len = data_len + 1;

	xfer.speed_hz = gf_spi_speed;

	spi_message_add_tail(&xfer, &msg);
	spi_sync(gf_dev->spi, &msg);

	memcpy(buf, g_rx_buf->buf, data_len);

	/*change the read data to little endian. */
	endian_exchange(buf, data_len);

	kfree(g_tx_buf);
	kfree(g_rx_buf);

	return 0;
}

static int gf_check_gf3658_chip_id(struct gf_dev *gf_dev)
{
	u32 time_out = 0;
	u8 tmp_buf[8] = {0};

	do {
		/* read data start from offset 4 */
		gf_spi_read_bytes_ree_new(gf_dev, 0x0000, 4, tmp_buf);
		gf_debug(INFO_LOG, "%s, chip id0 is 0x%02x 0x%02x 0x%02x 0x%02x\n", __func__,
			 tmp_buf[3], tmp_buf[2], tmp_buf[1], tmp_buf[0]);
		gf_debug(INFO_LOG, "%s, chip id1 is 0x%02x 0x%02x 0x%02x 0x%02x\n", __func__,
			 tmp_buf[4], tmp_buf[5], tmp_buf[6], tmp_buf[7]);

		time_out++;
		/* 9P MP chip version is 0x00900802*/
		if ((tmp_buf[2] == 0x25) && ((tmp_buf[1] == 0x03) ||(tmp_buf[1] == 0x04)) && ((tmp_buf[0] == 0xa8) ||(tmp_buf[0] == 0xa2))) {
			gf_debug(INFO_LOG, "%s, GF3658 chip id check pass, time_out=%d\n", __func__, time_out);
			return 0;
		}
	} while (time_out < 3);

	gf_debug(ERR_LOG, "%s, chip id read failed, time_out=%d\n", __func__, time_out);
	return -ERR_NO_SENSOR;
}
#endif

static int gf_open(struct inode *inode, struct file *filp)
{
	struct gf_dev *gf_dev = &gf;
	int status = -ENXIO;
	struct device_node *dnode = NULL;

	gf_debug(DEBUG_LOG, "%s enter!\n", __func__);

	mutex_lock(&device_list_lock);

	list_for_each_entry(gf_dev, &device_list, device_entry) {
		if (gf_dev->devt == inode->i_rdev) {
			gf_debug(INFO_LOG, "%s:goodixfp_dev Found!\n", __func__);
			status = 0;
			break;
		}
	}
       /*fp_nav*/
	dnode = of_find_compatible_node(NULL, NULL, "zte_fp_nav");
	if (dnode) {
		gf_debug(DEBUG_LOG, "fp-nav device node found!\n");
		gf_dev->nav_dev = of_find_device_by_node(dnode);
		if (gf_dev->nav_dev)
		{
			gf_debug(INFO_LOG, "%s:fp-nav device uevent found!\n", __func__);
		} else {
			gf_debug(ERR_LOG, "fp-nav device uevent not found!\n");
		}
	} else {
		gf_debug(ERR_LOG, "fp-nav device node not found!\n");
	}

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	gf_dev->zlog_fp_client = zlog_register_client(&goodix_zlog_fp_dev);
	if (gf_dev->zlog_fp_client) {
		gf_debug(INFO_LOG, "%s zlog_register_goodixfp_client success\n", __func__);
	} else {
		gf_debug(ERR_LOG, "%s zlog_register_goodixfp_client fail\n", __func__);
	}
#endif

	if (status == 0) {
		gf_dev->users++;
		filp->private_data = gf_dev;
		nonseekable_open(inode, filp);
		/*if (gf_dev->irq > 0) {
			gf_debug(INFO_LOG, "the irq(%d) has been registered, so free it\n",  gf_dev->irq);
			free_irq(gf_dev->irq,  gf_dev);
			gf_dev->irq = 0;
		}*/
		gf_debug(INFO_LOG, "%s:Succeed to open goodixfp_dev!irq = %d\n",
				__func__, gf_dev->irq);
		if (gf_dev->users == 1) {
			status = gf_parse_dts(gf_dev);
			if (status)
				goto err_parse_dt;

			status = irq_setup(gf_dev);
			if (status)
				goto err_irq;
		}
		gf_dev->device_available = 1;
	} else {
		gf_debug(ERR_LOG, "No device for minor %d\n", iminor(inode));
	}
#ifdef SUPPORT_REE_SPI
	/* Ree Read Chip Id */
	status = gf_power_on(gf_dev);
	gf_hw_reset(gf_dev, 20);
	mt_spi_enable_master_clk(gf_dev->spi);
	gf_check_gf3658_chip_id(gf_dev);
	mt_spi_disable_master_clk(gf_dev->spi);
	status = gf_power_off(gf_dev);
#endif
	mutex_unlock(&device_list_lock);

	return status;
err_irq:
	gf_cleanup(gf_dev);
err_parse_dt:
	return status;
}

#ifdef GF_FASYNC
static int gf_fasync(int fd, struct file *filp, int mode)
{
	struct gf_dev *gf_dev = filp->private_data;
	int ret;

	ret = fasync_helper(fd, filp, mode, &gf_dev->async);
	gf_debug(INFO_LOG, "ret = %d\n", ret);
	return ret;
}
#endif

static int gf_release(struct inode *inode, struct file *filp)
{
	struct gf_dev *gf_dev = &gf;
	int status = 0;

	gf_debug(DEBUG_LOG, "%s enter!\n", __func__);

	mutex_lock(&device_list_lock);
	gf_dev = filp->private_data;
	filp->private_data = NULL;

	/*last close?? */
	gf_dev->users--;
	if (!gf_dev->users) {
		gf_debug(INFO_LOG, "%s:disble_irq. irq = %d\n", __func__, gf_dev->irq);
		gf_disable_irq(gf_dev);
		gf_debug(INFO_LOG, "%s:free_irq\n", __func__);
		disable_irq_wake(gf_dev->irq);
		free_irq(gf_dev->irq,  gf_dev);
		gf_cleanup(gf_dev);
		gf_dev->irq = 0;
		gf_dev->device_available = 0;
	}

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	if (gf_dev->zlog_fp_client) {
		zlog_unregister_client(gf_dev->zlog_fp_client);
		gf_debug(INFO_LOG, "%s zlog_unregister_client goodix_zlog_fp_dev\n", __func__);
	}
#endif

	mutex_unlock(&device_list_lock);
	return status;
}

static const struct file_operations gf_fops = {
	.owner = THIS_MODULE,
	/* REVISIT switch to aio primitives, so that userspace
	 * gets more complete API coverage.  It'll simplify things
	 * too, except for the locking.
	 */
	.unlocked_ioctl = gf_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = gf_compat_ioctl,
#endif /*CONFIG_COMPAT*/
	.open = gf_open,
	.release = gf_release,
#ifdef GF_FASYNC
	.fasync = gf_fasync,
#endif
};

static int goodix_fb_state_chg_callback(struct notifier_block *nb,
		unsigned long val, void *data)
{
	struct gf_dev *gf_dev;
	struct fb_event *evdata = data;
	unsigned int blank;
	char msg = 0;

	if (val != FB_EVENT_BLANK) /* FB_EARLY_EVENT_BLANK */
		return 0;
	gf_debug(INFO_LOG, "%s:go to the goodix_fb_state_chg_callback value=%d\n",
			__func__, (int)val);
	gf_dev = container_of(nb, struct gf_dev, notifier);
	if (evdata && evdata->data && val == FB_EVENT_BLANK && gf_dev) {
		blank = *(int *)(evdata->data);
		switch (blank) {
		case FB_BLANK_POWERDOWN:
			if (gf_dev->device_available == 1) {
				gf_dev->fb_black = 1;
#if defined(GF_NETLINK_ENABLE)
				msg = GF_NET_EVENT_FB_BLACK;
				sendnlmsg(&msg);
#elif defined(GF_FASYNC)
				if (gf_dev->async)
					kill_fasync(&gf_dev->async, SIGIO, POLL_IN);
#endif
			}
			break;
		case FB_BLANK_UNBLANK:
			if (gf_dev->device_available == 1) {
				gf_dev->fb_black = 0;
#if defined(GF_NETLINK_ENABLE)
				msg = GF_NET_EVENT_FB_UNBLACK;
				sendnlmsg(&msg);
#elif defined(GF_FASYNC)
				if (gf_dev->async)
					kill_fasync(&gf_dev->async, SIGIO, POLL_IN);
#endif
			}
			break;
		default:
			gf_debug(DEBUG_LOG, "%s defalut\n", __func__);
			break;
		}
	}
	return NOTIFY_OK;
}

static struct notifier_block goodix_noti_block = {
	.notifier_call = goodix_fb_state_chg_callback,
};

static struct class *gf_class;
#if defined(USE_SPI_BUS)
static int gf_probe(struct spi_device *spi)
#elif defined(USE_PLATFORM_BUS)
static int gf_probe(struct platform_device *spi)
#endif
{
	struct gf_dev *gf_dev = &gf;
	int status = -EINVAL;
	unsigned long minor;
	int i;

	gf_debug(INFO_LOG, "%s enter!\n", __func__);

	/* Initialize the driver data */
	INIT_LIST_HEAD(&gf_dev->device_entry);

	gf_dev->spi = spi;

	gf_dev->nav_dev = NULL;/*fp_nav*/
	gf_dev->irq_gpio = -EINVAL;
	gf_dev->reset_gpio = -EINVAL;
	gf_dev->pwr_gpio = -EINVAL;
	/* set defualt fingerprint powered by gpio */
	gf_dev->power_type = 0;
	gf_dev->power_voltage = 2800000;/*default fp voltage*/
	gf_dev->fp_reg = NULL;
	gf_dev->device_available = 0;
	gf_dev->fb_black = 0;
	gf_dev->irq = 0;

	/* If we can allocate a minor number, hook up this device.
	 * Reusing minors is fine so long as udev or mdev is working.
	 */
	mutex_lock(&device_list_lock);
	minor = find_first_zero_bit(minors, N_SPI_MINORS);
	if (minor < N_SPI_MINORS) {
		struct device *dev;

		gf_dev->devt = MKDEV(SPIDEV_MAJOR, minor);
		dev = device_create(gf_class, &gf_dev->spi->dev, gf_dev->devt,
				gf_dev, GF_DEV_NAME);
		status = IS_ERR(dev) ? PTR_ERR(dev) : 0;
	} else {
		gf_debug(ERR_LOG, "no minor number available\n");
		status = -ENODEV;
		mutex_unlock(&device_list_lock);
		goto error_hw;
	}

	if (status == 0) {
		set_bit(minor, minors);
		list_add(&gf_dev->device_entry, &device_list);
	} else {
		gf_dev->devt = 0;
		goto error_hw;
	}
	mutex_unlock(&device_list_lock);

	#ifdef SUPPORT_REE_SPI
	/* create sysfs */
	status = sysfs_create_group(&gf_dev->spi->dev.kobj, &gf_debug_attr_group);
	if (status) {
		gf_debug(ERR_LOG, "%s:Failed to create sysfs file\n", __func__);
		status = -ENODEV;
		goto error_debug;
	} else {
		gf_debug(DEBUG_LOG, "%s:Success create sysfs file\n", __func__);
	}
	#endif

	gf_dev->input = input_allocate_device();
	if (gf_dev->input == NULL) {
		gf_debug(ERR_LOG, "%s:Failed to allocate input device\n", __func__);
		status = -ENOMEM;
		goto error_dev;
	}
	for (i = 0; i < ARRAY_SIZE(maps); i++)
		input_set_capability(gf_dev->input, maps[i].type, maps[i].code);

	gf_dev->input->name = GF_INPUT_NAME;
	status = input_register_device(gf_dev->input);
	if (status) {
		gf_debug(ERR_LOG, "Failed to register input device\n");
		goto error_input;
	}

#ifdef AP_CONTROL_CLK
	/* Enable spi clock */
	if (gfspi_ioctl_clk_init(gf_dev))
		goto gfspi_probe_clk_init_failed;

	if (gfspi_ioctl_clk_enable(gf_dev))
		goto gfspi_probe_clk_enable_failed;

	spi_clock_set(gf_dev, 1000000);
	gf_debug(DEBUG_LOG, "Get the clk resource finish\n");
#endif

	gf_dev->notifier = goodix_noti_block;
	fb_register_client(&gf_dev->notifier);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
	fp_wakelock = wakeup_source_register(NULL, "fp_wakelock");
#else
	wake_lock_init(&fp_wakelock, WAKE_LOCK_SUSPEND, "fp_wakelock");
#endif

	gf_debug(INFO_LOG, "%s:version : %s\n", __func__, GOODIXFP_DRIVER_VERSION);

	return status;

#ifdef AP_CONTROL_CLK
gfspi_probe_clk_enable_failed:
	gfspi_ioctl_clk_uninit(gf_dev);
gfspi_probe_clk_init_failed:
#endif

error_input:
	if (gf_dev->input != NULL)
		input_free_device(gf_dev->input);

error_dev:
	#ifdef SUPPORT_REE_SPI
	sysfs_remove_group(&gf_dev->spi->dev.kobj, &gf_debug_attr_group);
error_debug:
	#endif
	if (gf_dev->devt != 0) {
		gf_debug(ERR_LOG, "Err: status = %d\n", status);
		mutex_lock(&device_list_lock);
		list_del(&gf_dev->device_entry);
		device_destroy(gf_class, gf_dev->devt);
		clear_bit(MINOR(gf_dev->devt), minors);
		mutex_unlock(&device_list_lock);
	}
error_hw:
	gf_dev->device_available = 0;

	return status;
}

#if defined(USE_SPI_BUS)
static int gf_remove(struct spi_device *spi)
#elif defined(USE_PLATFORM_BUS)
static int gf_remove(struct platform_device *spi)
#endif
{
	struct gf_dev *gf_dev = &gf;

	gf_debug(INFO_LOG, "%s enter!\n", __func__);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
	wakeup_source_unregister(fp_wakelock);
#else
	wake_lock_destroy(&fp_wakelock);
#endif
	fb_unregister_client(&gf_dev->notifier);
	if (gf_dev->input)
		input_unregister_device(gf_dev->input);
	input_free_device(gf_dev->input);

	/* prevent new opens */
	mutex_lock(&device_list_lock);
#ifdef SUPPORT_REE_SPI
	sysfs_remove_group(&gf_dev->spi->dev.kobj, &gf_debug_attr_group);
#endif
	list_del(&gf_dev->device_entry);
	device_destroy(gf_class, gf_dev->devt);
	clear_bit(MINOR(gf_dev->devt), minors);
	mutex_unlock(&device_list_lock);

	gf_debug(INFO_LOG, "%s exit!\n", __func__);
	return 0;
}

static const struct of_device_id gx_match_table[] = {
	{ .compatible = GF_SPIDEV_NAME },
	{},
};

#if defined(USE_SPI_BUS)
static struct spi_driver gf_driver = {
#elif defined(USE_PLATFORM_BUS)
static struct platform_driver gf_driver = {
#endif
	.driver = {
		.name = GF_DEV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = gx_match_table,
	},
	.probe = gf_probe,
	.remove = gf_remove,
};

int gf_init(void)
{
	int status;

	/* Claim our 256 reserved device numbers.  Then register a class
	 * that will key udev/mdev to add/remove /dev nodes.  Last, register
	 * the driver which manages those device numbers.
	 */
	gf_debug(INFO_LOG, "%s enter!\n", __func__);

	BUILD_BUG_ON(N_SPI_MINORS > 256);
	status = register_chrdev(SPIDEV_MAJOR, CHRD_DRIVER_NAME, &gf_fops);
	if (status < 0) {
		gf_debug(ERR_LOG, "Failed to register char device!\n");
		return status;
	}
	SPIDEV_MAJOR = status;
	gf_class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(gf_class)) {
		unregister_chrdev(SPIDEV_MAJOR, gf_driver.driver.name);
		gf_debug(ERR_LOG, "Failed to create class.\n");
		return PTR_ERR(gf_class);
	}
#if defined(USE_PLATFORM_BUS)
	status = platform_driver_register(&gf_driver);
	gf_debug(INFO_LOG, "%s:gf platform_driver_register ok ---status : %d\n", __func__, status);
#elif defined(USE_SPI_BUS)
	status = spi_register_driver(&gf_driver);
	gf_debug(INFO_LOG, "%s:gf spi_register_driver ok ---status : %d\n", __func__, status);
#endif
	if (status < 0) {
		class_destroy(gf_class);
		unregister_chrdev(SPIDEV_MAJOR, gf_driver.driver.name);
		gf_debug(ERR_LOG, "Failed to register driver.\n");
		return status;
	}

#ifdef GF_NETLINK_ENABLE
	netlink_init();
#endif
	gf_debug(INFO_LOG, "%s done! status : %d\n", __func__, status);
	return 0;
}
/* module_init(gf_init); */

void gf_exit(void)
{
	gf_debug(INFO_LOG, "%s enter!\n", __func__);

#ifdef GF_NETLINK_ENABLE
	netlink_exit();
#endif
#if defined(USE_PLATFORM_BUS)
	platform_driver_unregister(&gf_driver);
	gf_debug(INFO_LOG, "gf platform_driver_unregister ok\n");
#elif defined(USE_SPI_BUS)
	spi_unregister_driver(&gf_driver);
	gf_debug(INFO_LOG, "gf spi_unregister_driver ok\n");
#endif
	class_destroy(gf_class);
	unregister_chrdev(SPIDEV_MAJOR, gf_driver.driver.name);

	gf_debug(INFO_LOG, "%s exit!\n", __func__);
}
/* module_exit(gf_exit); */

MODULE_AUTHOR("Jiangtao Yi, <yijiangtao@goodix.com>");
MODULE_AUTHOR("Jandy Gou, <gouqingsong@goodix.com>");
MODULE_DESCRIPTION("goodix fingerprint sensor device driver");
MODULE_LICENSE("GPL");
