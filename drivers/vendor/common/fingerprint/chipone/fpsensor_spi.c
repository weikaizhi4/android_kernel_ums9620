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

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <net/sock.h>
#include <linux/compat.h>
#include <linux/notifier.h>
#include "fpsensor_spi.h"
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#endif
/*#include <linux/cdc_com.h>*/


#define FPSENSOR_DRIVER_VERSION           "v2022-10-20"
#define FP_NOTIFY                         1
#define FPSENSOR_INPUT                    0
#define REMOVE_FPSENSOR_DEV               1
#define SLEEP_WAKEUP_HIGH_LEV             0   //sc9832 must open this
#define FP_NOTIFY_ADF                     0

#if FP_NOTIFY
#if FP_NOTIFY_ADF
#include <video/adf_notifier.h>
#define FP_NOTIFY_ON    DRM_MODE_DPMS_ON
#define FP_NOTIFY_OFF   DRM_MODE_DPMS_OFF
#else
#define FP_NOTIFY_ON    FB_BLANK_UNBLANK
#define FP_NOTIFY_OFF   FB_BLANK_POWERDOWN
#endif
#endif

/***********************input *************************/
#ifndef FPSENSOR_INPUT_HOME_KEY
#define FPSENSOR_INPUT_HOME_KEY     KEY_HOMEPAGE /* KEY_HOME */
#define FPSENSOR_INPUT_MENU_KEY     KEY_MENU
#define FPSENSOR_INPUT_BACK_KEY     KEY_BACK
#define FPSENSOR_INPUT_FF_KEY       KEY_POWER
#define FPSENSOR_INPUT_CAMERA_KEY   KEY_CAMERA
#define FPSENSOR_INPUT_OTHER_KEY    KEY_VOLUMEDOWN  /* temporary key value for capture use */
#endif

#define FPSENSOR_NAV_UP_KEY     19  /*KEY_UP*/
#define FPSENSOR_NAV_DOWN_KEY   20  /*KEY_DOWN*/
#define FPSENSOR_NAV_LEFT_KEY   21  /*KEY_LEFT*/
#define FPSENSOR_NAV_RIGHT_KEY  22  /*KEY_RIGHT*/
#define FPSENSOR_NAV_TAP_KEY    23


/*************************************************************/
/* global variables                         */
static fpsensor_data_t *g_fpsensor = NULL;
uint32_t g_cmd_sn = 0;
static chipone_chip_info fpsensor_chip_info = {0,0,"unknow"};

int32_t  CHIPONE_FP_IC=0;

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
struct zlog_mod_info chipone_zlog_fp_dev = {
	.module_no = ZLOG_MODULE_FP,
	.name = "fingerprint",
	.device_name = "Sunwin",
	.ic_name = "chipone",
	.module_name = "FP",
	.fops = NULL,
};
#endif

/* -------------------------------------------------------------------- */
/* fingerprint chip hardware configuration                                  */
/* -------------------------------------------------------------------- */
/*fp nav*/
static void chipone_report_uevent(fpsensor_data_t *fpsensor, char *str)
{

    char *envp[2];

    fp_debug(DEBUG_LOG, "%s enter!\n", __func__);

    envp[0] = str;
    envp[1] = NULL;

    if (fpsensor->nav_dev) {
        fp_debug(DEBUG_LOG, "%s:fpsensor->nav_dev is not null!\n", __func__);
        kobject_uevent_env(&(fpsensor->nav_dev->dev.kobj), KOBJ_CHANGE, envp);
    } else {
        fp_debug(ERR_LOG, "%s:fpsensor->nav_dev is null!\n", __func__);
    }
}

static int fpsensor_gpio_free(fpsensor_data_t *fpsensor)
{
    int ret = 0;
    struct device *dev;

    fp_debug(INFO_LOG, "%s enter!\n", __func__);

    if (fpsensor == NULL) {
        fp_debug(ERR_LOG, "fpsensor is NULL.\n");
        return -ENODEV;
    }
#if defined(USE_PLATFORM_BUS)
    dev = &fpsensor->platform_device->dev;
#elif defined(USE_SPI_BUS)
    dev = &fpsensor->spi_device->dev;
#endif
    if (fpsensor->irq_gpio != 0) {
        devm_gpio_free(dev, fpsensor->irq_gpio);
        fpsensor->irq_gpio = 0;
        fp_debug(INFO_LOG, "%s:remove irq_gpio success\n", __func__);
    }
    if (fpsensor->reset_gpio != 0) {
        gpio_set_value(fpsensor->reset_gpio, 0);
        devm_gpio_free(dev, fpsensor->reset_gpio);
        fpsensor->reset_gpio = 0;
        fp_debug(INFO_LOG, "%s:set reset low and remove reset_gpio success\n", __func__);
    }
    if (1 == fpsensor->power_type) {
        /* powered by pmic regulator */
        if (fpsensor->fp_reg != NULL) {
            ret = regulator_disable(fpsensor->fp_reg);
            regulator_put(fpsensor->fp_reg);
            fpsensor->fp_reg = NULL;
            fp_debug(INFO_LOG, "%s:regulator_disable and regulator_put success\n", __func__);
        }
    } else {
        if (fpsensor->power_gpio != 0) {
            gpio_set_value(fpsensor->power_gpio, 0);
            devm_gpio_free(dev, fpsensor->power_gpio);
            fpsensor->power_gpio = 0;
            fp_debug(INFO_LOG, "%s:set power low and remove pwr_gpio success\n", __func__);
        }
    }
    return ret;
}

static void fpsensor_irq_free(fpsensor_data_t *fpsensor_dev)
{
    fp_debug(INFO_LOG, "%s enter!\n", __func__);

    if (fpsensor_dev->irq) {
        free_irq(fpsensor_dev->irq, fpsensor_dev);
        fp_debug(INFO_LOG, "%s:free_irq!\n", __func__);
        fpsensor_dev->irq = 0;
        fpsensor_dev->irq_enabled = 0;
    }
}

static int fpsensor_irq_gpio_cfg(fpsensor_data_t *fpsensor)
{
    int error = 0;

    fp_debug(INFO_LOG, "%s enter!\n", __func__);

    if (fpsensor == NULL) {
        fp_debug(ERR_LOG, "fpsensor is NULL\n");
        return -ENODEV;
    }

    fpsensor->irq = gpio_to_irq(fpsensor->irq_gpio);
    fp_debug(INFO_LOG, "fpsensor irq number:[%d]\n", fpsensor->irq);
    if (fpsensor->irq <= 0) {
        fp_debug(ERR_LOG, "fpsensor gpio_to_irq failed!\n");
        return -EINVAL;
    }

    return error;
}

/* delay us after reset */
static void fpsensor_hw_reset(int delay)
{
    fp_debug(DEBUG_LOG, "%s enter!\n", __func__);

    if (gpio_is_valid(g_fpsensor->reset_gpio)) {
        /*gpio_set_value(g_fpsensor->reset_gpio, 1);
        usleep_range(delay, delay + 100);*/
        gpio_set_value(g_fpsensor->reset_gpio, 0);
        usleep_range(delay, delay + 100);
        gpio_set_value(g_fpsensor->reset_gpio, 1);
        fp_debug(INFO_LOG, "----fpsensor reset ok----\n");
        usleep_range(delay, delay + 100);
    }
    fp_debug(DEBUG_LOG, "%s exit!\n", __func__);
}

static int fpsensor_fingerprint_power_init(fpsensor_data_t *fpsensor_dev)
{
    int ret = 0;
    struct device *dev = NULL;

    fp_debug(INFO_LOG, "%s enter!\n", __func__);

    if (fpsensor_dev == NULL) {
        fp_debug(ERR_LOG, "fpsensor_dev is NULL\n");
        return -ENODEV;
    }

#if defined(USE_PLATFORM_BUS)
    dev = &fpsensor_dev->platform_device->dev;
#elif defined(USE_SPI_BUS)
    dev = &fpsensor_dev->spi_device->dev;
#endif

    fp_debug(INFO_LOG, "regulator_vdd");
    fpsensor_dev->fp_reg = regulator_get(dev, "vdd");

    if (IS_ERR(fpsensor_dev->fp_reg)) {
        fp_debug(ERR_LOG, "%s:regulator_get vdd failed from dts\n", __func__);
        return IS_ERR(fpsensor_dev->fp_reg);
    } else {
        fp_debug(INFO_LOG, "%s:regulator_get vdd success from dts\n", __func__);
    }

    ret = regulator_set_voltage(fpsensor_dev->fp_reg, fpsensor_dev->power_voltage, fpsensor_dev->power_voltage);

    if (ret) {
        fp_debug(ERR_LOG, "%s:regulator_set_voltage failed, ret=%d\n", __func__, ret);
        goto err_power_init;
    } else {
        fp_debug(INFO_LOG, "%s:regulator_set_voltage success\n", __func__);
    }
    /*ret = regulator_enable(fpsensor_dev->fp_reg);
    if (ret) {
        fp_debug(ERR_LOG, "%s regulator enable failed(%d)\n",
            __func__, ret);
        goto err_power_init;
    }*/
    fp_debug(INFO_LOG, "%s exit, ret=%d\n", __func__, ret);
    return ret;

err_power_init:
    regulator_put(fpsensor_dev->fp_reg);
    return ret;
}

static int fpsensor_get_gpio_dts_info(fpsensor_data_t *fpsensor_dev)
{
    struct device *dev = NULL;
    struct device_node *np =NULL;
    int ret = 0;
    fp_debug(INFO_LOG, "%s enter!\n", __func__);

    if (fpsensor_dev == NULL) {
        fp_debug(ERR_LOG, "fpsensor_dev is NULL\n");
        return -ENODEV;
    }
#if defined(USE_PLATFORM_BUS)
    dev = &fpsensor_dev->platform_device->dev;
#elif defined(USE_SPI_BUS)
    dev = &fpsensor_dev->spi_device->dev;
#endif
    np = dev->of_node;

    /*--------------------chipone_irq--------------------*/
    fpsensor_dev->irq_gpio = of_get_named_gpio(np, "fpint-gpios", 0);
    fp_debug(INFO_LOG, "%s:fpsensor_dev->irq_gpio:%d\n", __func__, fpsensor_dev->irq_gpio);
    if (gpio_is_valid(fpsensor_dev->irq_gpio)) {
        ret = devm_gpio_request(dev, fpsensor_dev->irq_gpio, "fpint-gpios");
        if (ret) {
            fp_debug(ERR_LOG, "failed to request irq gpio, ret=%d\n", ret);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
            if (g_fpsensor->zlog_fp_client) {
                zlog_client_record(g_fpsensor->zlog_fp_client, "Failed to request chiponefp irq gpio\n");
                zlog_client_notify(g_fpsensor->zlog_fp_client,  ZLOG_FP_REQUEST_INT_GPIO_ERROR_NO);
            }
#endif
            goto err_irq;
        } else {
            fp_debug(INFO_LOG, "%s:Success to request irq gpio\n", __func__);
            gpio_direction_input(fpsensor_dev->irq_gpio);
        }
    }

    /*--------------------chipone_rst--------------------*/
    fpsensor_dev->reset_gpio = of_get_named_gpio(np, "fpreset-gpios", 0);
    fp_debug(INFO_LOG, "%s:fpsensor_dev->reset_gpio:%d\n", __func__, fpsensor_dev->reset_gpio);
    if (gpio_is_valid(fpsensor_dev->reset_gpio)) {
        ret = devm_gpio_request(dev, fpsensor_dev->reset_gpio, "fpreset-gpios");
        if (ret) {
            fp_debug(ERR_LOG, "failed to request rst gpio, ret=%d\n", ret);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
            if (g_fpsensor->zlog_fp_client) {
                zlog_client_record(g_fpsensor->zlog_fp_client, "Failed to request chiponefp rst gpio\n");
                zlog_client_notify(g_fpsensor->zlog_fp_client,  ZLOG_FP_REQUEST_RST_GPIO_ERROR_NO);
            }
#endif
            goto err_rst;
        } else {
            fp_debug(INFO_LOG, "%s:Success to request reset gpio\n", __func__);
            gpio_direction_output(fpsensor_dev->reset_gpio, 0);
        }
    }

    /*--------------------chipone_pwr--------------------*/
    ret = of_property_read_u32(np, "power-type", &fpsensor_dev->power_type);
    if (ret < 0) {
        fp_debug(ERR_LOG, "%s:Power type get failed from dts, ret=%d\n", __func__, ret);
    }
    fp_debug(INFO_LOG, "%s:Power type[%d]\n", __func__, fpsensor_dev->power_type);

    if (1 == fpsensor_dev->power_type) {
        /* powered by pmic regulator */
        fp_debug(INFO_LOG, "%s PWR_MODE_REGULATOR\n", __func__);
        ret = of_property_read_u32(np, "power-voltage", &fpsensor_dev->power_voltage);
        if (ret < 0) {
            fp_debug(ERR_LOG, "%s:Power voltage get failed from dts, ret=%d\n", __func__, ret);
        }
        fp_debug(INFO_LOG, "%s:Power voltage[%d]\n", __func__, fpsensor_dev->power_voltage);

        ret = fpsensor_fingerprint_power_init(fpsensor_dev);
        if (ret) {
            fp_debug(ERR_LOG, "%s:fingerprint_power_init failed, ret=%d\n", __func__, ret);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
            if (g_fpsensor->zlog_fp_client) {
                zlog_client_record(g_fpsensor->zlog_fp_client, "Failed to regulator get and set chiponefp vcc\n");
                zlog_client_notify(g_fpsensor->zlog_fp_client,  ZLOG_FP_REGULATOR_GET_SET_ERROR_NO);
            }
#endif
            goto err_pwr;
        } else {
            fp_debug(INFO_LOG, "%s:fingerprint_power_init success\n", __func__);
        }
    } else {
        fp_debug(INFO_LOG, "%s PWR_MODE_GPIO\n", __func__);
        fpsensor_dev->power_gpio = of_get_named_gpio(np, "fppwr-gpios", 0);
        fp_debug(INFO_LOG, "%s:fpsensor_dev->power_gpio:%d\n", __func__, fpsensor_dev->power_gpio);
        if (gpio_is_valid(fpsensor_dev->power_gpio)) {
            ret = devm_gpio_request(dev, fpsensor_dev->power_gpio, "fppwr-gpios");
            if (ret) {
                fp_debug(ERR_LOG, "failed to request pwr gpio, ret=%d\n", ret);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
                if (g_fpsensor->zlog_fp_client) {
                    zlog_client_record(g_fpsensor->zlog_fp_client, "Failed to request chiponefp pwr gpio\n");
                    zlog_client_notify(g_fpsensor->zlog_fp_client,  ZLOG_FP_REQUEST_PWR_GPIO_ERROR_NO);
                }
#endif
                goto err_pwr;
            } else {
                fp_debug(INFO_LOG, "%s:Success to request power gpio\n", __func__);
                gpio_direction_output(fpsensor_dev->power_gpio, 0);
            }
        }
    }
    fp_debug(INFO_LOG, "%s exit, ret=%d\n", __func__, ret);
    return ret;

err_pwr:
    if (1 == fpsensor_dev->power_type) {
        fpsensor_dev->fp_reg = NULL;
    } else {
        fpsensor_dev->power_gpio = 0;
    }
    devm_gpio_free(dev, fpsensor_dev->reset_gpio);
err_rst:
    fpsensor_dev->reset_gpio = 0;
    devm_gpio_free(dev, fpsensor_dev->irq_gpio);
err_irq:
    fpsensor_dev->irq_gpio = 0;
    return ret;
}

static void setRcvIRQ(int val)
{
    fpsensor_data_t *fpsensor_dev = g_fpsensor;
    fp_debug(DEBUG_LOG, "%s enter! val is %d\n", __func__, val);
    fpsensor_dev->RcvIRQ = val;
}

static int fpsensor_hw_power_enable(fpsensor_data_t *fpsensor_dev)
{
    int ret = 0;

    fp_debug(INFO_LOG, "%s enter!\n", __func__);

    if (fpsensor_dev == NULL) {
        fp_debug(ERR_LOG, "fpsensor_dev is NULL\n");
        return -ENODEV;
    }

    /* TODO: add your power control here */
    if (1 == fpsensor_dev->power_type) {
        /*  powered by pmic regulator */
        if (fpsensor_dev->fp_reg != NULL) {
            ret = regulator_enable(fpsensor_dev->fp_reg);
            if (ret) {
                fp_debug(ERR_LOG, "%s:regulator_enable failed, ret=%d\n", __func__, ret);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
                if (g_fpsensor->zlog_fp_client) {
                    zlog_client_record(g_fpsensor->zlog_fp_client, "Failed to regulator enable chiponefp vcc\n");
                    zlog_client_notify(g_fpsensor->zlog_fp_client,  ZLOG_FP_REGULATOR_ENABLE_ERROR_NO);
                }
#endif
                return ret;
            } else {
                fp_debug(INFO_LOG, "%s:regulator_enable success\n", __func__);
            }
        }
    } else {
        if (gpio_is_valid(fpsensor_dev->power_gpio)) {
            gpio_set_value(fpsensor_dev->power_gpio, 1);
            usleep_range(1000, 1001);
            fp_debug(INFO_LOG, "----fpsensor power on ok ----\n");
        }
    }
    return ret;
}

static int fpsensor_hw_power_disable(fpsensor_data_t *fpsensor_dev)
{
    int ret = 0;

    fp_debug(INFO_LOG, "%s enter!\n", __func__);

    if (fpsensor_dev == NULL) {
        fp_debug(ERR_LOG, "fpsensor_dev is NULL.\n");
        return -ENODEV;
    }

    /* TODO: add your power control here */
    if (1 == fpsensor_dev->power_type) {
        /*  powered by pmic regulator */
        if (fpsensor_dev->fp_reg != NULL) {
            ret = regulator_disable(fpsensor_dev->fp_reg);
            if (ret) {
                fp_debug(ERR_LOG, "%s:regulator_disable failed, ret=%d\n", __func__, ret);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
                if (g_fpsensor->zlog_fp_client) {
                    zlog_client_record(g_fpsensor->zlog_fp_client, "Failed to regulator disable chiponefp vcc\n");
                    zlog_client_notify(g_fpsensor->zlog_fp_client,  ZLOG_FP_REGULATOR_DISABLE_ERROR_NO);
                }
#endif
                return ret;
            } else {
                fp_debug(INFO_LOG, "%s:regulator_disable success\n", __func__);
            }
        }
    } else {
        if (gpio_is_valid(fpsensor_dev->power_gpio)) {
            gpio_set_value(fpsensor_dev->power_gpio, 0);
            usleep_range(1000, 1001);
            fp_debug(INFO_LOG, "----fpsensor power off ok ----\n");
        }
    }
    return ret;
}

static int fpsensor_enable_irq(fpsensor_data_t *fpsensor_dev)
{
    int ret = 0;
    fp_debug(DEBUG_LOG, "%s enter!\n", __func__);

    if (fpsensor_dev == NULL) {
        fp_debug(ERR_LOG, "fpsensor_dev is NULL.\n");
        return -ENODEV;
    }
    setRcvIRQ(0);
    /* Request that the interrupt should be wakeable */
    if (fpsensor_dev->irq_enabled == 0) {
        enable_irq(fpsensor_dev->irq);
        fpsensor_dev->irq_enabled = 1;
        fp_debug(INFO_LOG, "fpsensor_enable_irq\n");
    }
    fp_debug(DEBUG_LOG, "%s exit\n", __func__);
    return ret;
}

static int fpsensor_disable_irq(fpsensor_data_t *fpsensor_dev)
{
    int ret = 0;
    fp_debug(DEBUG_LOG, "%s enter!\n", __func__);

    if (fpsensor_dev == NULL) {
        fp_debug(ERR_LOG, "fpsensor_dev is NULL.\n");
        return -ENODEV;
    }


    if (0 == fpsensor_dev->device_available) {
        fp_debug(ERR_LOG, "devices not available\n");
    } else {
        if (0 == fpsensor_dev->irq_enabled) {
            fp_debug(ERR_LOG, "irq already disabled\n");
        } else if (fpsensor_dev->irq) {
            disable_irq_nosync(fpsensor_dev->irq);
            fpsensor_dev->irq_enabled = 0;
            fp_debug(INFO_LOG, "%s:disable_irq_nosync!\n", __func__);
        }
    }
    setRcvIRQ(0);
    fp_debug(DEBUG_LOG, "%s exit\n", __func__);
    return ret;
}

static irqreturn_t fpsensor_irq(int irq, void *handle)
{

    fpsensor_data_t *fpsensor_dev = (fpsensor_data_t *)handle;
    fp_debug(DEBUG_LOG, "%s enter!\n", __func__);

#if SLEEP_WAKEUP_HIGH_LEV
    int irqf = 0;
    irqf = IRQF_TRIGGER_RISING | IRQF_ONESHOT | IRQF_NO_SUSPEND;
    if (fpsensor_dev->suspend_flag == 1) {
        irq_set_irq_type(fpsensor_dev->irq, irqf);
        fpsensor_dev->suspend_flag = 0;
    }
#endif

    /* Make sure 'wakeup_enabled' is updated before using it
    ** since this is interrupt context (other thread...) */
    smp_rmb();
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
    /* __pm_wakeup_event(fpsensor_dev->ttw_wl, msecs_to_jiffies(1000));*/
    __pm_wakeup_event(fpsensor_dev->ttw_wl, 1000);
#else
    wake_lock_timeout(&fpsensor_dev->ttw_wl, msecs_to_jiffies(1000));
#endif

    setRcvIRQ(1);
    wake_up_interruptible(&fpsensor_dev->wq_irq_return);

    return IRQ_HANDLED;
}

// release and cleanup fpsensor char device
/*static int  fpsensor_dev_cleanup(fpsensor_data_t *fpsensor)
{
    int ret = 0;
    fp_debug(INFO_LOG, "%s enter!\n", __func__);

    if (fpsensor == NULL) {
        fp_debug(ERR_LOG, "fpsensor is NULL.\n");
        return -ENODEV;
    }

#if FPSENSOR_INPUT
    if (fpsensor->input != NULL) {
        input_unregister_device(fpsensor->input);
        fpsensor->input = NULL;
    }
#endif
    cdev_del(&fpsensor->cdev);
    unregister_chrdev_region(fpsensor->devno, FPSENSOR_NR_DEVS);
    device_destroy(fpsensor->class, fpsensor->devno);
    class_destroy(fpsensor->class);
    fp_debug(DEBUG_LOG, "%s exit!\n", __func__);
    return ret;
}*/

#if FPSENSOR_INPUT
static int fpsensor_input_init(fpsensor_data_t *fpsensor_dev)
{
    int status = 0;

    fp_debug(DEBUG_LOG, "%s enter!\n", __func__);

    if (fpsensor_dev == NULL) {
        fp_debug(ERR_LOG, "fpsensor_dev is NULL\n");
        return -ENODEV;
    }

    fpsensor_dev->input = input_allocate_device();
    if (fpsensor_dev->input == NULL) {
        fp_debug(ERR_LOG, "Failed to allocate input device\n");
        status = -ENOMEM;
        goto out;
    }
    __set_bit(EV_KEY, fpsensor_dev->input->evbit);
    __set_bit(FPSENSOR_INPUT_HOME_KEY, fpsensor_dev->input->keybit);

    __set_bit(FPSENSOR_INPUT_MENU_KEY, fpsensor_dev->input->keybit);
    __set_bit(FPSENSOR_INPUT_BACK_KEY, fpsensor_dev->input->keybit);
    __set_bit(FPSENSOR_INPUT_FF_KEY, fpsensor_dev->input->keybit);

    __set_bit(FPSENSOR_NAV_TAP_KEY, fpsensor_dev->input->keybit);
    __set_bit(FPSENSOR_NAV_UP_KEY, fpsensor_dev->input->keybit);
    __set_bit(FPSENSOR_NAV_DOWN_KEY, fpsensor_dev->input->keybit);
    __set_bit(FPSENSOR_NAV_RIGHT_KEY, fpsensor_dev->input->keybit);
    __set_bit(FPSENSOR_NAV_LEFT_KEY, fpsensor_dev->input->keybit);
    __set_bit(FPSENSOR_INPUT_CAMERA_KEY, fpsensor_dev->input->keybit);
    fpsensor_dev->input->name = FPSENSOR_INPUT_NAME;
    if (input_register_device(fpsensor_dev->input)) {
        fp_debug(ERR_LOG, "Failed to register input device\n");
        status = -ENODEV;
        goto err1;
    }
    goto out;

err1:
    input_free_device(fpsensor_dev->input);
    fpsensor_dev->input = NULL;

out:
    fp_debug(DEBUG_LOG, "%s exit!\n", __func__);
    return status;
}
#endif

static long fpsensor_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    fpsensor_data_t *fpsensor_dev = NULL;
    int retval = 0;
    unsigned int val = 0;
    int irqf;
    struct fpsensor_key fpsensor_key;
#if FPSENSOR_INPUT
    uint32_t key_event;
#endif
    fp_debug(DEBUG_LOG, "%s:fpsensor ioctl cmd:0x%x\n", __func__, cmd);
    fpsensor_dev = (fpsensor_data_t *)filp->private_data;
    fpsensor_dev->cancel = 0 ;
    switch (cmd) {
    case FPSENSOR_IOC_INIT:
        fp_debug(INFO_LOG, "%s:FPSENSOR_IOC_INIT\n", __func__);
        retval = fpsensor_get_gpio_dts_info(fpsensor_dev);
        if (retval) {
            break;
        }
        fpsensor_irq_gpio_cfg(fpsensor_dev);
        //regist irq
        /* irqf = IRQF_TRIGGER_RISING | IRQF_ONESHOT | IRQF_NO_SUSPEND; */
        /* IRQF_NO_SUSPEND flag will cause can not unlock when suspend in MTK kernel4.19
        See Documentation/power/suspend-and-interrupts.txt */
        irqf = IRQF_TRIGGER_RISING | IRQF_ONESHOT;
        fp_debug(INFO_LOG, "irq flag:0x%08x\n", irqf);
#if defined(USE_PLATFORM_BUS)
        retval = devm_request_threaded_irq(&g_fpsensor->platform_device->dev, g_fpsensor->irq, fpsensor_irq,
                                           NULL, irqf, dev_name(&g_fpsensor->platform_device->dev), g_fpsensor);
#elif defined(USE_SPI_BUS)
        retval = devm_request_threaded_irq(&g_fpsensor->spi_device->dev, g_fpsensor->irq, fpsensor_irq,
                                           NULL, irqf, dev_name(&g_fpsensor->spi_device->dev), g_fpsensor);
#endif

        //retval = request_irq(g_fpsensor->irq, fpsensor_irq,irqf, "fpsnesor_int",NULL);
        if (retval == 0) {
            fp_debug(INFO_LOG, "devm_request_threaded_irq success!\n");
        } else {
            fp_debug(ERR_LOG, "devm_request_threaded_irq failed, retval=%d\n", retval);
            break;
        }
        enable_irq_wake(g_fpsensor->irq);
        fpsensor_dev->device_available = 1;
        // fix Unbalanced enable for IRQ, disable irq at first
        fpsensor_dev->irq_enabled = 1;
        fpsensor_disable_irq(fpsensor_dev);
        fpsensor_hw_power_enable(fpsensor_dev);
        fp_debug(DEBUG_LOG, "fpsensor init finished======\n");
        break;

    case FPSENSOR_IOC_EXIT:
        fp_debug(INFO_LOG, "%s:FPSENSOR_IOC_EXIT\n", __func__);
        //fpsensor_disable_irq(fpsensor_dev);
        //fpsensor_dev->device_available = 0;
        //fpsensor_irq_free(fpsensor_dev);
        //fpsensor_gpio_free(fpsensor_dev);
        //fp_debug(DEBUG_LOG, "fpsensor exit finish======\n");
        break;

    case FPSENSOR_IOC_RESET:
        fp_debug(DEBUG_LOG, "%s:FPSENSOR_IOC_RESET\n", __func__);
        fpsensor_hw_reset(2000);
        break;

    case FPSENSOR_IOC_ENABLE_IRQ:
        fp_debug(DEBUG_LOG, "%s:FPSENSOR_IOC_ENABLE_IRQ\n", __func__);
        fpsensor_enable_irq(fpsensor_dev);
        break;

    case FPSENSOR_IOC_DISABLE_IRQ:
        fp_debug(DEBUG_LOG, "%s:FPSENSOR_IOC_DISABLE_IRQ\n", __func__);
        fpsensor_disable_irq(fpsensor_dev);
        break;
    case FPSENSOR_IOC_GET_INT_VAL:
        fp_debug(DEBUG_LOG, "%s:FPSENSOR_IOC_GET_INT_VAL\n", __func__);
        val = gpio_get_value(fpsensor_dev->irq_gpio);
        if (copy_to_user((void __user *)arg, (void *)&val, sizeof(uint32_t))) {
            fp_debug(ERR_LOG, "Failed to copy data to user\n");
            retval = -EFAULT;
            break;
        }
        retval = 0;
        break;
    case FPSENSOR_IOC_ENABLE_SPI_CLK:
        fp_debug(DEBUG_LOG, "%s:FPSENSOR_IOC_ENABLE_SPI_CLK\n", __func__);
#if defined(USE_SPI_BUS)
        mt_spi_enable_master_clk(fpsensor_dev->spi_device);
        fp_debug(DEBUG_LOG, "mt_spi_enable_master_clk\n");
#endif
        break;
    case FPSENSOR_IOC_DISABLE_SPI_CLK:
        fp_debug(DEBUG_LOG, "%s:FPSENSOR_IOC_DISABLE_SPI_CLK\n", __func__);
#if defined(USE_SPI_BUS)
        mt_spi_disable_master_clk(fpsensor_dev->spi_device);
        fp_debug(DEBUG_LOG, "mt_spi_disable_master_clk\n");
#endif
        break;
    case FPSENSOR_IOC_ENABLE_POWER:
        fp_debug(INFO_LOG, "%s:FPSENSOR_IOC_ENABLE_POWER\n", __func__);
        fpsensor_hw_power_enable(fpsensor_dev);
        break;
    case FPSENSOR_IOC_DISABLE_POWER:
        fp_debug(INFO_LOG, "%s:FPSENSOR_IOC_DISABLE_POWER\n", __func__);
        fpsensor_hw_power_disable(fpsensor_dev);
        break;
    case FPSENSOR_IOC_INIT_INPUT_DEV:
        fp_debug(DEBUG_LOG, "%s:FPSENSOR_IOC_INIT_INPUT_DEV\n", __func__);
#if FPSENSOR_INPUT
        retval = fpsensor_input_init(fpsensor_dev);
        if (retval) {
            fp_debug(ERR_LOG, "fpsensor init fail\n");
        }
#endif
        break;
    case FPSENSOR_IOC_INPUT_KEY_EVENT:
        fp_debug(DEBUG_LOG, "%s:FPSENSOR_IOC_INPUT_KEY_EVENT\n", __func__);
        if (copy_from_user(&fpsensor_key, (struct fpsensor_key *)arg, sizeof(struct fpsensor_key))) {
            fp_debug(ERR_LOG, "Failed to copy input key event from user to kernel\n");
            retval = -EFAULT;
            break;
        }
#if FPSENSOR_INPUT
        if (FPSENSOR_KEY_HOME == fpsensor_key.key) {
            key_event = FPSENSOR_INPUT_HOME_KEY;
        } else if (FPSENSOR_KEY_POWER == fpsensor_key.key) {
            key_event = FPSENSOR_INPUT_FF_KEY;
        } else if (FPSENSOR_KEY_CAPTURE == fpsensor_key.key) {
            key_event = FPSENSOR_INPUT_CAMERA_KEY;
        } else {
            /* add special key define */
            key_event = FPSENSOR_INPUT_OTHER_KEY;
        }
        fp_debug(DEBUG_LOG, "received key event[%d], key=%d, value=%d\n",
                       key_event, fpsensor_key.key, fpsensor_key.value);
        if ((FPSENSOR_KEY_POWER == fpsensor_key.key || FPSENSOR_KEY_CAPTURE == fpsensor_key.key)
            && (fpsensor_key.value == 1)) {
            input_report_key(fpsensor_dev->input, key_event, 1);
            input_sync(fpsensor_dev->input);
            input_report_key(fpsensor_dev->input, key_event, 0);
            input_sync(fpsensor_dev->input);
        } else if (FPSENSOR_KEY_UP == fpsensor_key.key) {
            input_report_key(fpsensor_dev->input, FPSENSOR_NAV_UP_KEY, 1);
            input_sync(fpsensor_dev->input);
            input_report_key(fpsensor_dev->input, FPSENSOR_NAV_UP_KEY, 0);
            input_sync(fpsensor_dev->input);
        } else if (FPSENSOR_KEY_DOWN == fpsensor_key.key) {
            input_report_key(fpsensor_dev->input, FPSENSOR_NAV_DOWN_KEY, 1);
            input_sync(fpsensor_dev->input);
            input_report_key(fpsensor_dev->input, FPSENSOR_NAV_DOWN_KEY, 0);
            input_sync(fpsensor_dev->input);
        } else if (FPSENSOR_KEY_RIGHT == fpsensor_key.key) {
            input_report_key(fpsensor_dev->input, FPSENSOR_NAV_RIGHT_KEY, 1);
            input_sync(fpsensor_dev->input);
            input_report_key(fpsensor_dev->input, FPSENSOR_NAV_RIGHT_KEY, 0);
            input_sync(fpsensor_dev->input);
        } else if (FPSENSOR_KEY_LEFT == fpsensor_key.key) {
            input_report_key(fpsensor_dev->input, FPSENSOR_NAV_LEFT_KEY, 1);
            input_sync(fpsensor_dev->input);
            input_report_key(fpsensor_dev->input, FPSENSOR_NAV_LEFT_KEY, 0);
            input_sync(fpsensor_dev->input);
        } else  if (FPSENSOR_KEY_TAP == fpsensor_key.key) {
            input_report_key(fpsensor_dev->input, FPSENSOR_NAV_TAP_KEY, 1);
            input_sync(fpsensor_dev->input);
            input_report_key(fpsensor_dev->input, FPSENSOR_NAV_TAP_KEY, 0);
            input_sync(fpsensor_dev->input);
        } else if ((FPSENSOR_KEY_POWER != fpsensor_key.key) && (FPSENSOR_KEY_CAPTURE != fpsensor_key.key)) {
            input_report_key(fpsensor_dev->input, key_event, fpsensor_key.value);
            input_sync(fpsensor_dev->input);
        }
#endif
        fp_debug(INFO_LOG, "received key:key=%d, value=%d\n", fpsensor_key.key, fpsensor_key.value);
        if (fpsensor_key.value)//fpsensor_key.value=1 report
        {
            switch (fpsensor_key.key)
            {
                case FPSENSOR_KEY_DOWN:
                    chipone_report_uevent(fpsensor_dev, FP_NAV_DOWN);
                    fp_debug(INFO_LOG, "%s:nav down\n", __func__);
                break;

                case FPSENSOR_KEY_UP:
                    chipone_report_uevent(fpsensor_dev, FP_NAV_UP);
                    fp_debug(INFO_LOG, "%s:nav up\n", __func__);
                break;

                case FPSENSOR_KEY_LEFT:
                    chipone_report_uevent(fpsensor_dev, FP_NAV_LEFT);
                    fp_debug(INFO_LOG, "%s:nav left\n", __func__);
                break;

                case FPSENSOR_KEY_RIGHT:
                    chipone_report_uevent(fpsensor_dev, FP_NAV_RIGHT);
                    fp_debug(INFO_LOG, "%s:nav right\n", __func__);
                break;

                default:
                    fp_debug(ERR_LOG, "%s:unknown nav key: %d\n", __func__, fpsensor_key.key);
                break;
            }
        }
        else//fpsensor_key.value=0 report
        {
            fp_debug(DEBUG_LOG, "not report uevent\n");
        }
        break;
    case FPSENSOR_IOC_REMOVE:
        fp_debug(INFO_LOG, "%s:FPSENSOR_IOC_REMOVE\n", __func__);
        //Only run release
        //fpsensor_disable_irq(fpsensor_dev);
        //fpsensor_irq_free(fpsensor_dev);
        //fpsensor_dev->device_available = 0;
        //fpsensor_hw_power_disable(fpsensor_dev);
        //fpsensor_gpio_free(fpsensor_dev);
#if REMOVE_FPSENSOR_DEV
        //fpsensor_dev_cleanup(fpsensor_dev);
#endif
        fp_debug(DEBUG_LOG, "remove finished\n");
        break;
    case FPSENSOR_IOC_CANCEL_WAIT:
        fp_debug(DEBUG_LOG, "%s:FPSENSOR_IOC_CANCEL_WAIT\n", __func__);
        wake_up_interruptible(&fpsensor_dev->wq_irq_return);
        fpsensor_dev->cancel = 1;
        break;
#if FP_NOTIFY
    case FPSENSOR_IOC_GET_FP_STATUS :
        val = fpsensor_dev->fb_status;
        fp_debug(DEBUG_LOG, "%s:FPSENSOR_IOC_GET_FP_STATUS, fb_status:%d\n", __func__, fpsensor_dev->fb_status);
        if (copy_to_user((void __user *)arg, (void *)&val, sizeof(uint32_t))) {
            fp_debug(ERR_LOG, "Failed to copy data to user\n");
            retval = -EFAULT;
            break;
        }
        retval = 0;
        break;
#endif
    case FPSENSOR_IOC_ENABLE_REPORT_BLANKON:
        fp_debug(DEBUG_LOG, "%s:FPSENSOR_IOC_ENABLE_REPORT_BLANKON\n", __func__);
        if (copy_from_user(&val, (void __user *)arg, sizeof(uint32_t))) {
            retval = -EFAULT;
            break;
        }
        fpsensor_dev->enable_report_blankon = val;
        fp_debug(DEBUG_LOG, "%s:fpsensor_dev->enable_report_blankon:%d\n", __func__, val);
        break;
    case FPSENSOR_IOC_UPDATE_DRIVER_SN:
        fp_debug(DEBUG_LOG, "%s:FPSENSOR_IOC_UPDATE_DRIVER_SN\n", __func__);
        if (copy_from_user(&g_cmd_sn, (void __user *)arg, sizeof(uint32_t))) {
            fp_debug(ERR_LOG, "Failed to copy g_cmd_sn from user to kernel\n");
            retval = -EFAULT;
            break;
        }
        break;
    case FPSENSOR_IOC_GET_CHIP_INFO:
        fp_debug(DEBUG_LOG, "%s:FPSENSOR_IOC_GET_CHIP_INFO\n", __func__);
        if (copy_from_user(&fpsensor_chip_info,
            (chipone_chip_info *)arg, sizeof(fpsensor_chip_info))) {
            fp_debug(ERR_LOG, "fpsensor Failed to get fpsensor_chip_info from user\n");
            retval = -EFAULT;
            break;
        }
        fp_debug(ERR_LOG, "fpsensor get chip info:hardware_id==%x, sensor_id==%d, version info==%s\n",
                        fpsensor_chip_info.hardware_id,fpsensor_chip_info.sensor_id,fpsensor_chip_info.ver_name);
        CHIPONE_FP_IC=fpsensor_chip_info.hardware_id;
#ifdef __WW6_CDC_COM__
        check_fpsensor_chip();
#endif
        break;
    default:
        fp_debug(ERR_LOG, "fpsensor doesn't support this command(0x%x)\n", cmd);
        break;
    }

    return retval;
}

static long fpsensor_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    return fpsensor_ioctl(filp, cmd, (unsigned long)(arg));
}

static unsigned int fpsensor_poll(struct file *filp, struct poll_table_struct *wait)
{
    unsigned int ret = 0;
    fp_debug(DEBUG_LOG, "%s enter!Support poll opertion in version\n", __func__);
    ret |= POLLIN;
    poll_wait(filp, &g_fpsensor->wq_irq_return, wait);
    if (g_fpsensor->cancel == 1) {
        fp_debug(ERR_LOG, "cancle\n");
        ret =  POLLERR;
        g_fpsensor->cancel = 0;
        return ret;
    }
    if (g_fpsensor->RcvIRQ) {
        if (g_fpsensor->RcvIRQ == 2) {
            fp_debug(DEBUG_LOG, "get fp on notify\n");
            ret |= POLLHUP;
        } else {
            fp_debug(ERR_LOG, "******get irq\n");
            ret |= POLLRDNORM;
        }
    } else {
        ret = 0;
        fp_debug(DEBUG_LOG, "fpsensor_poll return 0\n");
    }
    return ret;
}

static int fpsensor_open(struct inode *inode, struct file *filp)
{
    fpsensor_data_t *fpsensor_dev;
    struct device_node *dnode = NULL;/*fp_nav node*/

    fp_debug(INFO_LOG, "%s enter!\n", __func__);
    fpsensor_dev = container_of(inode->i_cdev, fpsensor_data_t, cdev);
    fpsensor_dev->users++;
    fpsensor_dev->device_available = 1;
    filp->private_data = fpsensor_dev;

    /*fp_nav*/
    dnode = of_find_compatible_node(NULL, NULL, "zte_fp_nav");
    if (dnode) {
        fp_debug(DEBUG_LOG, "fp-nav device node found!\n");
        fpsensor_dev->nav_dev = of_find_device_by_node(dnode);
        if (fpsensor_dev->nav_dev)
        {
            fp_debug(INFO_LOG, "%s:fp-nav device uevent found!\n", __func__);
        } else {
            fp_debug(ERR_LOG, "%s:fp-nav device uevent not found!\n", __func__);
        }
    } else {
        fp_debug(ERR_LOG, "%s:fp-nav device node not found!\n", __func__);
    }

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
    g_fpsensor->zlog_fp_client = zlog_register_client(&chipone_zlog_fp_dev);
    if (g_fpsensor->zlog_fp_client) {
        fp_debug(INFO_LOG, "%s zlog_register_chiponefp_client success\n", __func__);
    } else {
        fp_debug(ERR_LOG, "%s zlog_register_chiponefp_client fail\n", __func__);
    }
#endif

    fp_debug(INFO_LOG, "%s exit!\n", __func__);
    return 0;
}

static int fpsensor_release(struct inode *inode, struct file *filp)
{
    fpsensor_data_t *fpsensor_dev;
    int status = 0;

    fp_debug(INFO_LOG, "%s enter!\n", __func__);
    fpsensor_dev = filp->private_data;
    filp->private_data = NULL;

    /*last close??*/
    fpsensor_dev->users--;
    if (fpsensor_dev->users <= 0) {
        fp_debug(INFO_LOG, "%s:irq=%d\n", __func__, fpsensor_dev->irq);
        fpsensor_disable_irq(fpsensor_dev);
        fp_debug(INFO_LOG, "%s:fpsensor_disable_irq\n", __func__);
        fpsensor_irq_free(fpsensor_dev);
        fp_debug(INFO_LOG, "%s:fpsensor_irq_free\n", __func__);
        fpsensor_dev->device_available = 0;
        fpsensor_gpio_free(fpsensor_dev);
    }

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
    if (g_fpsensor->zlog_fp_client) {
        zlog_unregister_client(g_fpsensor->zlog_fp_client);
        fp_debug(INFO_LOG, "%s zlog_unregister_client chipone_zlog_fp_dev\n", __func__);
    }
#endif

    fp_debug(INFO_LOG, "%s exit!\n", __func__);
    return status;
}

static ssize_t fpsensor_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    fp_debug(ERR_LOG, "Not support read opertion in TEE version\n");
    return -EFAULT;
}

static ssize_t fpsensor_write(struct file *filp, const char __user *buf, size_t count,
                              loff_t *f_pos)
{
    fp_debug(ERR_LOG, "Not support write opertion in TEE version\n");
    return -EFAULT;
}

static const struct file_operations fpsensor_fops = {
    .owner          = THIS_MODULE,
    .write          = fpsensor_write,
    .read           = fpsensor_read,
    .unlocked_ioctl = fpsensor_ioctl,
    .compat_ioctl   = fpsensor_compat_ioctl,
    .open           = fpsensor_open,
    .release        = fpsensor_release,
    .poll           = fpsensor_poll,

};

// create and register a char device for fpsensor
static int fpsensor_dev_setup(fpsensor_data_t *fpsensor)
{
    int ret = 0;
    dev_t dev_no = 0;
    struct device *dev = NULL;
    int fpsensor_dev_major = FPSENSOR_DEV_MAJOR;
    int fpsensor_dev_minor = 0;

    fp_debug(DEBUG_LOG, "%s enter!\n", __func__);

    if (fpsensor == NULL) {
        fp_debug(ERR_LOG, "fpsensor is NULL\n");
        return -ENODEV;
    }

    if (fpsensor_dev_major > 0) {
        dev_no = MKDEV(fpsensor_dev_major, fpsensor_dev_minor);
        ret = register_chrdev_region(dev_no, FPSENSOR_NR_DEVS, FPSENSOR_DEV_NAME);
    } else {
        ret = alloc_chrdev_region(&dev_no, fpsensor_dev_minor, FPSENSOR_NR_DEVS, FPSENSOR_DEV_NAME);
        fpsensor_dev_major = MAJOR(dev_no);
        fpsensor_dev_minor = MINOR(dev_no);
        fp_debug(INFO_LOG, "%s:fpsensor device major is %d, minor is %d\n", __func__, fpsensor_dev_major, fpsensor_dev_minor);
    }

    if (ret < 0) {
        fp_debug(ERR_LOG, "can not get device major number %d\n", fpsensor_dev_major);
        goto out;
    }

    cdev_init(&fpsensor->cdev, &fpsensor_fops);
    fpsensor->cdev.owner = THIS_MODULE;
    fpsensor->cdev.ops   = &fpsensor_fops;
    fpsensor->devno      = dev_no;
    ret = cdev_add(&fpsensor->cdev, dev_no, FPSENSOR_NR_DEVS);
    if (ret) {
        fp_debug(ERR_LOG, "add char dev for fpsensor failed\n");
        goto release_region;
    }

    fpsensor->class = class_create(THIS_MODULE, FPSENSOR_CLASS_NAME);
    if (IS_ERR(fpsensor->class)) {
        fp_debug(ERR_LOG, "create fpsensor class failed\n");
        ret = PTR_ERR(fpsensor->class);
        goto release_cdev;
    }
#if defined(USE_PLATFORM_BUS)
    dev = device_create(fpsensor->class, &fpsensor->platform_device->dev, dev_no, fpsensor, FPSENSOR_DEV_NAME);
#elif defined(USE_SPI_BUS)
    dev = device_create(fpsensor->class, &fpsensor->spi_device->dev, dev_no, fpsensor, FPSENSOR_DEV_NAME);
#endif

    if (IS_ERR(dev)) {
        fp_debug(ERR_LOG, "create device for fpsensor failed\n");
        ret = PTR_ERR(dev);
        goto release_class;
    }
    fp_debug(DEBUG_LOG, "%s exit!\n", __func__);
    return ret;

release_class:
    class_destroy(fpsensor->class);
    fpsensor->class = NULL;
release_cdev:
    cdev_del(&fpsensor->cdev);
release_region:
    unregister_chrdev_region(dev_no, FPSENSOR_NR_DEVS);
out:
    fp_debug(DEBUG_LOG, "%s exit!\n", __func__);
    return ret;
}

#if FP_NOTIFY
static int fpsensor_fb_notifier_callback(struct notifier_block* self, unsigned long event, void* data)
{
    int retval = 0;
    static char screen_status[64] = { '\0' };
    struct fb_event* evdata = data;
    unsigned int blank;
    fpsensor_data_t *fpsensor_dev = g_fpsensor;

    fp_debug(DEBUG_LOG, "%s enter. event:0x%lx\n",  __func__, event);
#if FP_NOTIFY_ADF
    if (event != ADF_EVENT_BLANK) {
        return NOTIFY_DONE;
    }
#else
    if (event != FB_EVENT_BLANK /* FB_EARLY_EVENT_BLANK */) {
        return 0;
    }
#endif

    blank = *(int*)evdata->data;
    fp_debug(DEBUG_LOG, "blank=0x%x\n", blank);

    switch (blank) {
    case FP_NOTIFY_ON:
        fp_debug(DEBUG_LOG, "lcd on notify\n");
        sprintf(screen_status, "SCREEN_STATUS=%s", "ON");
        fpsensor_dev->fb_status = 1;
        if( fpsensor_dev->enable_report_blankon) {
            fpsensor_dev->RcvIRQ = 2;
            wake_up_interruptible(&fpsensor_dev->wq_irq_return);
        }
        break;
    case FP_NOTIFY_OFF:
        fp_debug(DEBUG_LOG, "lcd off notify\n");
        sprintf(screen_status, "SCREEN_STATUS=%s", "OFF");
        fpsensor_dev->fb_status = 0;
        break;

    default:
        fp_debug(ERR_LOG, "other notifier, ignore\n");
        break;
    }

    fp_debug(DEBUG_LOG, "%s leave\n", screen_status);
    return retval;
}
#endif

#if defined(FPSENSOR_GET_CHIPID)
static struct mt_chip_conf fpsensor_spi_conf_mt65xx = {
    .setuptime = 15,
    .holdtime = 15,
    .high_time = 21,
    .low_time = 21,
    .cs_idletime = 20,
    .ulthgh_thrsh = 0,

    .cpol = 0,
    .cpha = 0,

    .rx_mlsb = 1,
    .tx_mlsb = 1,

    .tx_endian = 0,
    .rx_endian = 0,

    .com_mod = FIFO_TRANSFER,
    .pause = 1,
    .finish_intr = 1,
    .deassert = 0,
    .ulthigh = 0,
    .tckdly = 0,
};

typedef enum {
    SPEED_500KHZ = 500,
    SPEED_1MHZ = 1000,
    SPEED_2MHZ = 2000,
    SPEED_3MHZ = 3000,
    SPEED_4MHZ = 4000,
    SPEED_6MHZ = 6000,
    SPEED_8MHZ = 8000,
    SPEED_KEEP,
    SPEED_UNSUPPORTED
} SPI_SPEED;

void fpsensor_spi_set_mode(struct spi_device *spi, SPI_SPEED speed, int flag)
{
    struct mt_chip_conf *mcc = &fpsensor_spi_conf_mt65xx;

    if (flag == 0) {
        mcc->com_mod = FIFO_TRANSFER;
    } else {
        mcc->com_mod = DMA_TRANSFER;
    }

    switch (speed) {
    case SPEED_500KHZ:
        mcc->high_time = 120;
        mcc->low_time = 120;
        break;
    case SPEED_1MHZ:
        mcc->high_time = 60;
        mcc->low_time = 60;
        break;
    case SPEED_2MHZ:
        mcc->high_time = 30;
        mcc->low_time = 30;
        break;
    case SPEED_3MHZ:
        mcc->high_time = 20;
        mcc->low_time = 20;
        break;
    case SPEED_4MHZ:
        mcc->high_time = 15;
        mcc->low_time = 15;
        break;
    case SPEED_6MHZ:
        mcc->high_time = 10;
        mcc->low_time = 10;
        break;
    case SPEED_8MHZ:
        mcc->high_time = 8;
        mcc->low_time = 8;
        break;
    case SPEED_KEEP:
    case SPEED_UNSUPPORTED:
        break;
    }

    if (spi_setup(spi) < 0) {
        fp_debug(ERR_LOG, "fpsensor:Failed to set spi\n");
    }
}

/* -------------------------------------------------------------------- */
int fpsensor_spi_setup(fpsensor_data_t *fpsensor)
{
    int ret = 0;

    FUNC_ENTRY();
    fpsensor->spi->mode = SPI_MODE_0;
    fpsensor->spi->bits_per_word = 8;
//  fpsensor->spi->chip_select = 0;
    fpsensor->spi->controller_data = (void *)&fpsensor_spi_conf_mt65xx;
    ret = spi_setup(fpsensor->spi);
    if (ret < 0) {
        fp_debug(ERR_LOG, "spi_setup failed\n");
        return ret;
    }
    fpsensor_spi_set_mode(fpsensor->spi, fpsensor->spi_freq_khz, 0);

    return ret;
}

int fpsensor_detect_hwid(fpsensor_data_t *fpsensor)
{
    unsigned int hwid = 0, status = 0, match= 0, retry = 2;
    unsigned char chipid_tx[4] = {0};
    unsigned char chipid_rx[4] = {0};

    fpsensor_hw_reset(1250);
    fpsensor_dev->spi_freq_khz = 6000u;
    fpsensor_spi_setup(fpsensor_dev);

    do {
            chipid_tx[0] = 0x08;
            chipid_tx[1] = 0x55;
            status = tee_spi_transfer((void*)&fpsensor_spi_conf_mt65xx, sizeof(fpsensor_spi_conf_mt65xx), chipid_tx, chipid_rx, 2);
            if (status != 0) {
                fp_debug(ERR_LOG, "%s, tee spi transfer failed, status=0x%x\n", __func__,status);
                return status;
            }

            chipid_tx[0] = 0x00;
            chipid_rx[1] = 0x00;
            chipid_rx[2] = 0x00;
            status = tee_spi_transfer((void*)&fpsensor_spi_conf_mt65xx, sizeof(fpsensor_spi_conf_mt65xx), chipid_tx, chipid_rx, 3);
            if (status == 0) {
                fp_debug(DEBUG_LOG, "chipid_rx : %x  %x  %x  %x \n",chipid_rx[0],chipid_rx[1],chipid_rx[2],chipid_rx[3]);
                hwid = (chipid_rx[1] << 8) | (chipid_rx[2]);
                if ((hwid == 0x7332) || (hwid == 0x7153) || (hwid == 0x7230) ||(hwid == 0x7222)||(hwid == 0x7312)) {
                    fp_debug(DEBUG_LOG,"get HWID == 0x%x, is fpsensor\n",hwid);
                    match = 0;
                } else {
                    match = -1;
                }
                return match;
            } else {
                return status;
            }

    }
    while (retry--);
}
#endif

#if defined(USE_PLATFORM_BUS)
static int fpsensor_probe(struct platform_device *pdev)
#elif defined(USE_SPI_BUS)
static int fpsensor_probe(struct spi_device *spi)
#endif
{
    int status = 0;
    fpsensor_data_t *fpsensor_dev = NULL;

    fp_debug(INFO_LOG, "%s enter!\n", __func__);
    /* Allocate driver data */
    fpsensor_dev = kzalloc(sizeof(*fpsensor_dev), GFP_KERNEL);
    if (!fpsensor_dev) {
        status = -ENOMEM;
        fp_debug(ERR_LOG, "Failed to alloc memory for fpsensor device\n");
        goto out;
    }

    /* Initialize the driver data */
    g_fpsensor = fpsensor_dev;
#if defined(USE_PLATFORM_BUS)
    fpsensor_dev->platform_device = pdev ;
#elif defined(USE_SPI_BUS)
    fpsensor_dev->spi_device = spi ;
#endif
    fpsensor_dev->nav_dev = NULL;/*fp_nav*/
    fpsensor_dev->device_available  = 0;
    fpsensor_dev->users             = 0;
    fpsensor_dev->irq               = 0;
    fpsensor_dev->power_gpio        = 0;
    fpsensor_dev->reset_gpio        = 0;
    fpsensor_dev->irq_gpio          = 0;
    fpsensor_dev->irq_enabled       = 0;
    fpsensor_dev->suspend_flag      = 0;
    fpsensor_dev->input             = NULL;
    fpsensor_dev->fb_status = 1;
    /* set defualt fingerprint powered by gpio */
    fpsensor_dev->power_type        = 0;
    fpsensor_dev->fp_reg            = NULL;
    fpsensor_dev->power_voltage     = 2800000;/*default fp voltage*/
    /* setup a char device for fpsensor */

#if defined(FPSENSOR_GET_CHIPID)
    mt_spi_enable_master_clk(fpsensor_dev->spi_device);
    status = fpsensor_detect_hwid(fpsensor_dev);
    mt_spi_disable_master_clk(fpsensor_dev->spi_device);
    if (0 != status)
    {
        fp_debug(ERR_LOG, "get chip id error\n");
        goto release_drv_data;
    }
#endif

    status = fpsensor_dev_setup(fpsensor_dev);
    if (status) {
        fp_debug(ERR_LOG, "fpsensor setup char device failed, err is %d", status);
        goto release_drv_data;
    }

    init_waitqueue_head(&fpsensor_dev->wq_irq_return);

#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
    g_fpsensor->ttw_wl = wakeup_source_register(NULL, "fpsensor_ttw_wl");
#else
    wake_lock_init(&g_fpsensor->ttw_wl, WAKE_LOCK_SUSPEND, "fpsensor_ttw_wl");
#endif

    fpsensor_dev->device_available = 1;
   /*power on*/
    //fpsensor_hw_power_enable(fpsensor_dev);
    //udelay(1000);
#if FP_NOTIFY
    fpsensor_dev->notifier.notifier_call = fpsensor_fb_notifier_callback;
#if FP_NOTIFY_ADF
    status = adf_register_client(&fpsensor_dev->notifier);
    if (status < 0) {
        fp_debug(ERR_LOG, "failed to register adf notifier");
    } else {
        fp_debug(DEBUG_LOG, "succeed to register adf notifier");
    }
#else
    fb_register_client(&fpsensor_dev->notifier);
#endif
#endif

    fp_debug(INFO_LOG, "%s:finished, driver version: %s\n", __func__, FPSENSOR_DRIVER_VERSION);
    goto out;

release_drv_data:
    if (fpsensor_dev != NULL) {
        kfree(fpsensor_dev);
        fpsensor_dev = NULL;
        g_fpsensor = NULL;
    }
out:
    fp_debug(INFO_LOG, "%s exit, status=%d\n", __func__, status);
    return status;
}

#if defined(USE_PLATFORM_BUS)
static int fpsensor_remove(struct platform_device *pdev)
#elif defined(USE_SPI_BUS)
static int fpsensor_remove(struct spi_device *spi)
#endif
{
    fpsensor_data_t *fpsensor_dev = g_fpsensor;
    fp_debug(INFO_LOG, "%s enter\n", __func__);
    if (fpsensor_dev != NULL) {
#if FP_NOTIFY
#if FP_NOTIFY_ADF
        adf_unregister_client(&fpsensor_dev->notifier);
#else
        fb_unregister_client(&fpsensor_dev->notifier);
#endif
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
        wakeup_source_unregister(fpsensor_dev->ttw_wl);
#else
        wake_lock_destroy(&fpsensor_dev->ttw_wl);
#endif

        kfree(fpsensor_dev);
        fpsensor_dev = NULL;
    }
    g_fpsensor = NULL;
    fp_debug(INFO_LOG, "%s exit\n", __func__);
    return 0;
}

#ifdef CONFIG_PM
#if defined(USE_PLATFORM_BUS)
static int fpsensor_suspend(struct platform_device *pdev, pm_message_t state)
{
#if SLEEP_WAKEUP_HIGH_LEV
    int irqf = 0;
    irqf = IRQF_TRIGGER_HIGH | IRQF_ONESHOT | IRQF_NO_SUSPEND;
    irq_set_irq_type(g_fpsensor->irq, irqf);
    g_fpsensor->suspend_flag = 1;
#endif
    return 0;
}

static int fpsensor_resume(struct platform_device *pdev)
{
    return 0;
}
#elif defined(USE_SPI_BUS)
#endif
#endif

/*-------------------------------------------------------------------------*/
static struct of_device_id fpsensor_of_match[] = {
    { .compatible = "chipone,fingerprint", },
    {}
};
MODULE_DEVICE_TABLE(of, fpsensor_of_match);

#if defined(USE_PLATFORM_BUS)
static struct platform_driver fpsensor_driver = {
#elif defined(USE_SPI_BUS)
static struct spi_driver fpsensor_driver = {
#endif
    .driver = {
        .name = FPSENSOR_DEV_NAME,
        .owner = THIS_MODULE,
#if defined(USE_PLATFORM_BUS)
        .bus = &platform_bus_type,
#elif defined(USE_SPI_BUS)
        .bus = &spi_bus_type,
#endif
        .of_match_table = fpsensor_of_match,
    },
    .probe = fpsensor_probe,
    .remove = fpsensor_remove,
#ifdef CONFIG_PM
#if defined(USE_PLATFORM_BUS)
    .suspend = fpsensor_suspend,
    .resume = fpsensor_resume,
#elif defined(USE_SPI_BUS)
#endif
#endif
};

int fpsensor_init(void)
{
    int status;
#if defined(USE_PLATFORM_BUS)
    status = platform_driver_register(&fpsensor_driver);
    fp_debug(INFO_LOG, "%s:platform_driver_register", __func__);
#elif defined(USE_SPI_BUS)
    status = spi_register_driver(&fpsensor_driver);
    fp_debug(INFO_LOG, "%s:spi_register_driver", __func__);
#endif
    if (status < 0) {
        fp_debug(ERR_LOG, "%s:Failed to register driver\n", __func__);
    } else {
        fp_debug(INFO_LOG, "%s:Success to register driver\n", __func__);
    }

    return status;
}

void fpsensor_exit(void)
{
#if defined(USE_PLATFORM_BUS)
    platform_driver_unregister(&fpsensor_driver);
    fp_debug(INFO_LOG, "%s:platform_driver_unregister", __func__);
#elif defined(USE_SPI_BUS)
    spi_unregister_driver(&fpsensor_driver);
    fp_debug(INFO_LOG, "%s:spi_unregister_driver", __func__);
#endif
}

/*module_init(fpsensor_init);
module_exit(fpsensor_exit);*/

MODULE_AUTHOR("xhli");
MODULE_DESCRIPTION(" Fingerprint chip TEE driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:fpsensor-drivers");
