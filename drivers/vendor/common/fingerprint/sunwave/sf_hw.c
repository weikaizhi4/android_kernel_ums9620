/**
 * The device control driver for Sunwave's fingerprint sensor.
 *
 * Copyright (C) 2016 Sunwave Corporation. <http://www.sunwavecorp.com>
 * Copyright (C) 2016 Langson L. <mailto: liangzh@sunwavecorp.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
**/

#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/spi/spi.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include <linux/gpio.h>
#include <linux/of_gpio.h>

#include "sf_ctl.h"

#ifdef CONFIG_MTK_CLKMGR
#include "mach/mt_clkmgr.h"
#endif

#define MODULE_NAME "fortsense-sf_hw"
//#define xprintk(level, fmt, args...) printk(level MODULE_NAME"-%d: "fmt, __LINE__, ##args)

#ifndef CONFIG_OF
#error "error: this driver 'MODULE_NAME' only support dts."
#endif

static struct sf_ctl_device *sf_ctl_dev = NULL;
#if (SF_MTK_CPU && REE_MTK_ANDROID_L)
typedef enum {
#if (SF_POWER_MODE_SEL == PWR_MODE_GPIO)
    SF_PIN_STATE_PWR_ON,
    SF_PIN_STATE_PWR_OFF,
#endif
    SF_PIN_STATE_RST_LOW,
    SF_PIN_STATE_RST_HIGH,
    SF_PIN_STATE_INT_SET,
#if SF_SPI_DTS_CS
    SF_PIN_STATE_CS_SET,
#endif
#if SF_SPI_DTS_Ck
    SF_PIN_STATE_CK_SET,
#endif
#if SF_SPI_DTS_MI
    SF_PIN_STATE_MI_SET,
#endif
#if SF_SPI_DTS_MO
    SF_PIN_STATE_MO_SET,
#endif
#if SF_SPI_DTS_MI_PU
    SF_PIN_STATE_MI_PU,
#elif SF_SPI_DTS_MI_PD
    SF_PIN_STATE_MI_PD,
#endif
#if SF_SPI_DTS_MO_PU
    SF_PIN_STATE_MO_PU,
#elif SF_SPI_DTS_MO_PD
    SF_PIN_STATE_MO_PD,
#endif
    /* Array size */
    SF_PIN_STATE_MAX
} sf_pin_state_t;


static struct pinctrl *sf_pinctrl = NULL;
static struct pinctrl_state *sf_pin_states[SF_PIN_STATE_MAX] = {NULL, };

static const char *sf_pinctrl_state_names[SF_PIN_STATE_MAX] = {
#if (SF_POWER_MODE_SEL == PWR_MODE_GPIO)
    FINGER_POWER_ON, FINGER_POWER_OFF,
#endif
    FINGER_RESET_LOW, FINGER_RESET_HIGH, FINGER_INT_SET,
#if SF_SPI_DTS_CS
    FINGER_CS_SET,
#endif
#if SF_SPI_DTS_Ck
    FINGER_CK_SET,
#endif
#if SF_SPI_DTS_MI
    FINGER_MI_SET,
#endif
#if SF_SPI_DTS_MO
    FINGER_MO_SET,
#endif
#if SF_SPI_DTS_MI_PU
    FINGER_MI_PU,
#elif SF_SPI_DTS_MI_PD
    FINGER_MI_PD,
#endif
#if SF_SPI_DTS_MO_PU
    FINGER_MO_PU,
#elif SF_SPI_DTS_MO_PD
    FINGER_MO_PD,
#endif
};
#endif

static int sf_spi_clock_enable(bool on)
{
    int err = 0;
    sf_debug(DEBUG_LOG, "%s enter!\n", __func__);
#if (SF_REE_PLATFORM) && (SF_MTK_CPU)
#ifdef CONFIG_MTK_CLKMGR

    if (on) {
#ifdef CONFIG_ARCH_MT6580
        /* Switch to 104MHz before transfer */
        clkmux_sel(MT_CLKMUX_SPI_GFMUX_SEL, MT_CG_UPLL_D12, "spi");
#endif
        enable_clock(MT_CG_PERI_SPI0, "spi");
    }
    else {
        disable_clock(MT_CG_PERI_SPI0, "spi");
#ifdef CONFIG_ARCH_MT6580
        /* Switch back to 26MHz after transfer */
        clkmux_sel(MT_CLKMUX_SPI_GFMUX_SEL, MT_CG_SYS_26M, "spi");
#endif
    }

#elif defined(MT6797)
    /* changed after MT6797 platform */
    struct mt_spi_t *ms = NULL;
    static int count;
    ms = spi_master_get_devdata(sf_ctl_dev->pdev->master);

    if (on && (count == 0)) {
        mt_spi_enable_clk(ms);
        count = 1;
    }
    else if ((count > 0) && (on == 0)) {
        mt_spi_disable_clk(ms);
        count = 0;
    }
#endif
#else
#endif

#if defined(USE_SPI_BUS)
    //static int count;
    if (on)
    {
        mt_spi_enable_master_clk(sf_ctl_dev->pdev);
        sf_debug(DEBUG_LOG, "mt_spi_enable_master_clk\n");
        //count = 1;
    }
    else
    {
        mt_spi_disable_master_clk(sf_ctl_dev->pdev);
        sf_debug(DEBUG_LOG, "mt_spi_disable_master_clk\n");
        // count = 0;
    }

    sf_debug(DEBUG_LOG, "%s exit!\n", __func__);
#endif
    return err;
}

static int sf_ctl_device_reset(void)
{
    int err = 0;
    sf_debug(DEBUG_LOG, "%s enter!\n", __func__);
#if SF_MTK_CPU
#if REE_MTK_ANDROID_L
    sf_debug(DEBUG_LOG, "SF_MTK_CPU\n");

    mt_set_gpio_out(GPIO_SW_RST_PIN, 1);
    msleep(1);
    mt_set_gpio_out(GPIO_SW_RST_PIN, 0);
    msleep(10);
    mt_set_gpio_out(GPIO_SW_RST_PIN, 1);
#else
    if (sf_pinctrl == NULL) {
        sf_debug(ERR_LOG, "sf_pinctrl is NULL\n");
        return -1;
    }
    sf_debug(DEBUG_LOG, "pinctrl method\n");

    err = pinctrl_select_state(sf_pinctrl, sf_pin_states[SF_PIN_STATE_RST_HIGH]);
    msleep(1);
    err = pinctrl_select_state(sf_pinctrl, sf_pin_states[SF_PIN_STATE_RST_LOW]);
    msleep(10);
    err = pinctrl_select_state(sf_pinctrl, sf_pin_states[SF_PIN_STATE_RST_HIGH]);
#endif
#else
    if (sf_ctl_dev->rst_num == 0) {
        sf_debug(ERR_LOG, "sf_ctl_dev->rst_num is not get\n");
        return -1;
    }
    sf_debug(DEBUG_LOG, "gpio method\n");

    if (gpio_is_valid(sf_ctl_dev->rst_num)) {
        /*gpio_set_value(sf_ctl_dev->rst_num, 1);
        usleep_range(1000, 1100);*/
        gpio_set_value(sf_ctl_dev->rst_num, 0);
        usleep_range(2000, 2100);
        gpio_set_value(sf_ctl_dev->rst_num, 1);
        sf_debug(INFO_LOG, "---- sunwave hw reset ok ----\n");
        usleep_range(2000, 2100);
    }
#endif
    sf_debug(DEBUG_LOG, "%s exit!\n", __func__);
    return err;
}

#if (SF_POWER_MODE_SEL == PWR_MODE_GPIO)
static int sf_ctl_device_power_by_gpio(bool on)
{
    int err = 0;
    sf_debug(DEBUG_LOG, "%s enter!\n", __func__);
#if SF_MTK_CPU
    sf_debug(DEBUG_LOG, "SF_MTK_CPU\n", __func__);
    if (on) {
        err = pinctrl_select_state(sf_pinctrl, sf_pin_states[SF_PIN_STATE_PWR_ON]);
        msleep(10);
    }
    else {
        err = pinctrl_select_state(sf_pinctrl, sf_pin_states[SF_PIN_STATE_PWR_OFF]);
    }
#else
    if (on) {
        err = gpio_direction_output(sf_ctl_dev->pwr_num, 1);
        sf_debug(INFO_LOG, "----- sunwave power on ok -----\n");
        usleep_range(7000, 7100);
    } else {
        err = gpio_direction_output(sf_ctl_dev->pwr_num, 0);
        sf_debug(INFO_LOG, "----- sunwave power off ok -----\n");
    }
#endif
    sf_debug(DEBUG_LOG, "%s exit!\n", __func__);
    return err;
}
#endif

#if (SF_POWER_MODE_SEL == PWR_MODE_REGULATOR)
static int sf_ctl_device_power_by_regulator(bool on)
{
    static bool isPowerOn = false;
    int err = 0;
    sf_debug(DEBUG_LOG, "%s enter!\n", __func__);

    if (sf_ctl_dev->vdd_reg == NULL) {
        sf_debug(ERR_LOG, "ctl_dev->vdd_reg is NULL\n");
        return (-ENODEV);
    }

    if (on && !isPowerOn) {
        err = regulator_enable(sf_ctl_dev->vdd_reg);
        if (err) {
            sf_debug(ERR_LOG, "%s:regulator_enable failed, err=%d\n", __func__, err);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
            if (sf_ctl_dev->zlog_fp_client) {
                zlog_client_record(sf_ctl_dev->zlog_fp_client, "Failed to regulator enable sunwavefp vcc\n");
                zlog_client_notify(sf_ctl_dev->zlog_fp_client,  ZLOG_FP_REGULATOR_ENABLE_ERROR_NO);
            }
#endif
            return err;
        } else {
            sf_debug(INFO_LOG, "%s:regulator_enable success\n", __func__);
        }

        msleep(10);
    }
    else if (!on && isPowerOn) {
        err = regulator_disable(sf_ctl_dev->vdd_reg);
        if (err) {
            sf_debug(ERR_LOG, "Regulator vdd disable failed, err=%d\n", err);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
            if (sf_ctl_dev->zlog_fp_client) {
                zlog_client_record(sf_ctl_dev->zlog_fp_client, "Failed to regulator disable sunwavefp vcc\n");
                zlog_client_notify(sf_ctl_dev->zlog_fp_client,  ZLOG_FP_REGULATOR_DISABLE_ERROR_NO);
            }
#endif
            return err;
        } else {
            sf_debug(INFO_LOG, "%s:regulator_disable success\n", __func__);
        }
    }
    sf_debug(DEBUG_LOG, "%s exit!\n", __func__);
    return err;
}
#endif

static int sf_ctl_device_power(bool on)
{
    int err = 0;
    sf_debug(DEBUG_LOG, "%s enter!\n", __func__);
#if (SF_POWER_MODE_SEL == PWR_MODE_GPIO)
    sf_debug(INFO_LOG, "%s:PWR_MODE_GPIO\n", __func__);
    err = sf_ctl_device_power_by_gpio(on);
#elif (SF_POWER_MODE_SEL == PWR_MODE_REGULATOR)
    sf_debug(INFO_LOG, "%s:PWR_MODE_REGULATOR\n", __func__);
    err = sf_ctl_device_power_by_regulator(on);
#endif
    sf_debug(DEBUG_LOG, "%s exit!\n", __func__);
    return err;
}

#if REE_MTK_ANDROID_L
static int sf_ctl_device_free_gpio(struct sf_ctl_device *ctl_dev)
{
    int err = 0;
    sf_debug(DEBUG_LOG, "%s enter!\n", __func__);
    return err;
}

static int sf_ctl_device_init_gpio_pins(struct sf_ctl_device *ctl_dev)
{
    int err = 0;
    sf_ctl_dev = ctl_dev;
    sf_debug(DEBUG_LOG, "%s enter!\n", __func__);
#if MTK_L5_X_POWER_ON
    //power on
    mt_set_gpio_mode(GPIO_SW_PWR_PIN, GPIO_SW_PWR_M_GPIO);
    mt_set_gpio_dir(GPIO_SW_PWR_PIN, GPIO_DIR_OUT);
    mt_set_gpio_out(GPIO_SW_PWR_PIN, 1);
    msleep(1);
#endif
    /*set reset pin to high*/
    mt_set_gpio_mode(GPIO_SW_RST_PIN, GPIO_MODE_00);
    mt_set_gpio_dir(GPIO_SW_RST_PIN, GPIO_DIR_OUT);
    mt_set_gpio_out(GPIO_SW_RST_PIN, 1);
#if MTK_L5_X_IRQ_SET
    //irq setting
    mt_set_gpio_mode(GPIO_SW_INT_PIN, GPIO_FORTSENSE_IRQ_M_EINT);
    mt_set_gpio_dir(GPIO_SW_INT_PIN, GPIO_DIR_IN);
    mt_set_gpio_pull_enable(GPIO_SW_INT_PIN, GPIO_PULL_DISABLE);
    mt_set_gpio_pull_enable(GPIO_SW_INT_PIN, GPIO_PULL_ENABLE);
    mt_set_gpio_pull_select(GPIO_SW_INT_PIN, GPIO_PULL_UP);
#endif
    ctl_dev->irq_num = mt_gpio_to_irq(GPIO_SW_IRQ_NUM);
    sf_debug(DEBUG_LOG, "irq number is %d\n", ctl_dev->irq_num);
    sf_debug(DEBUG_LOG, "%s exit!\n", __func__);
    return err;
}
#else
static int sf_ctl_device_free_gpio(struct sf_ctl_device *ctl_dev)
{
    int err = 0;
    sf_debug(DEBUG_LOG, "%s enter!\n", __func__);

    if (ctl_dev->pdev->dev.of_node) {
        ctl_dev->pdev->dev.of_node = NULL;
    }

#if SF_MTK_CPU
    if (sf_pinctrl) {
        pinctrl_put(sf_pinctrl);
        sf_pinctrl = NULL;
    }
#else
    if (gpio_is_valid(ctl_dev->rst_num)) {
        gpio_set_value(ctl_dev->rst_num, 0);
        gpio_free(ctl_dev->rst_num);
        ctl_dev->rst_num = 0;
        sf_debug(INFO_LOG, "%s:set reset low and remove reset_gpio success\n", __func__);
    }

    if (gpio_is_valid(ctl_dev->irq_pin)) {
        gpio_free(ctl_dev->irq_pin);
        ctl_dev->irq_pin = 0;
        sf_debug(INFO_LOG, "%s:remove irq_gpio success\n", __func__);
    }
#endif
#if (SF_POWER_MODE_SEL == PWR_MODE_GPIO)
    if (gpio_is_valid(ctl_dev->pwr_num)) {
        gpio_set_value(ctl_dev->pwr_num, 0);
        gpio_free(ctl_dev->pwr_num);
        ctl_dev->pwr_num = 0;
        sf_debug(INFO_LOG, "%s:set power low and remove pwr_gpio success\n", __func__);
    }
#elif (SF_POWER_MODE_SEL == PWR_MODE_REGULATOR)
    if (ctl_dev->vdd_reg) {
        err = regulator_disable(ctl_dev->vdd_reg);
        regulator_put(ctl_dev->vdd_reg);
        sf_debug(INFO_LOG, "%s:regulator_disable and regulator_put success\n", __func__);
        ctl_dev->vdd_reg = NULL;
    }
#endif
    sf_debug(DEBUG_LOG, "%s exit!\n", __func__);
    return err;
}

static int sf_ctl_device_init_gpio_pins(struct sf_ctl_device *ctl_dev)
{
    int err = 0;
    sf_ctl_dev = ctl_dev;
    sf_debug(DEBUG_LOG, "%s enter!\n", __func__);
    ctl_dev->pdev->dev.of_node = of_find_compatible_node(NULL, NULL, COMPATIBLE_SW_FP);

    if (!ctl_dev->pdev->dev.of_node) {
        sf_debug(ERR_LOG, "of_find_compatible_node failed\n");
        return (-ENODEV);
    }

#if SF_MTK_CPU
    sf_pinctrl = pinctrl_get(&ctl_dev->pdev->dev);

    if (!sf_pinctrl) {
        sf_debug(ERR_LOG, "pinctrl_get failed\n");
        return (-ENODEV);
    }

    ctl_dev->irq_num = irq_of_parse_and_map(ctl_dev->pdev->dev.of_node, 0);
    sf_debug(DEBUG_LOG, "irq number is %d\n", ctl_dev->irq_num);
    {
        int i = 0;

        for (i = 0; i < SF_PIN_STATE_MAX; ++i) {
            sf_pin_states[i] = pinctrl_lookup_state(sf_pinctrl,
                                                    sf_pinctrl_state_names[i]);

            if (!sf_pin_states[i]) {
                sf_debug(ERR_LOG, "can't find %s pinctrl_state\n",
                        sf_pinctrl_state_names[i]);
                err = (-ENODEV);
                break;
            }
        }

        if (i < SF_PIN_STATE_MAX) {
            sf_debug(ERR_LOG, "%s failed\n", __func__);
        }

        /* init spi,sunch as cs clck miso mosi mode, gpio pullup pulldown */
        for (i = SF_PIN_STATE_INT_SET + 1; i < SF_PIN_STATE_MAX; ++i) {
            err = pinctrl_select_state(sf_pinctrl, sf_pin_states[i]);

            if (err) {
                sf_debug(ERR_LOG, "%s:pinctrl_select_state(%s) failed\n", __func__, sf_pinctrl_state_names[i]);
                break;
            }

            sf_debug(DEBUG_LOG, "%s:pinctrl_select_state(%s) ok\n", __func__, sf_pinctrl_state_names[i]);
        }
    }
#else
    /*reset-gpio*/
    ctl_dev->rst_num = of_get_named_gpio(ctl_dev->pdev->dev.of_node, COMPATIBLE_RESET_GPIO, 0);
    sf_debug(INFO_LOG, "%s:reset gpio number=%d\n", __func__, ctl_dev->rst_num);
    if (gpio_is_valid(ctl_dev->rst_num))
    {
        err = gpio_request(ctl_dev->rst_num, "sf-reset");
        if (err)
        {
            sf_debug(ERR_LOG, "Failed to request reset gpio\n");
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
            if (ctl_dev->zlog_fp_client) {
                zlog_client_record(ctl_dev->zlog_fp_client, "Failed to request sunwavefp rst gpio\n");
                zlog_client_notify(ctl_dev->zlog_fp_client,  ZLOG_FP_REQUEST_RST_GPIO_ERROR_NO);
            }
#endif
            return err;
        } else {
            sf_debug(INFO_LOG, "Success to request reset gpio\n");
        }
        gpio_direction_output(ctl_dev->rst_num, 0);
    }
    else
    {
        sf_debug(ERR_LOG, "not valid reset gpio\n");
        return -EIO;
    }
    /*irq-gpio*/
    ctl_dev->irq_pin = of_get_named_gpio(ctl_dev->pdev->dev.of_node, COMPATIBLE_IRQ_GPIO, 0);
    sf_debug(INFO_LOG, "%s:irq gpio number=%d\n", __func__, ctl_dev->irq_pin);
    if (gpio_is_valid(ctl_dev->irq_pin))
    {
        //ctl_dev->irq_num = gpio_to_irq(ctl_dev->irq_pin);
        //sf_debug(INFO_LOG, "irq_num = %d\n", ctl_dev->irq_num);
        err = gpio_request(ctl_dev->irq_pin, "sf-irq");
        if (err)
        {
            sf_debug(ERR_LOG, "Failed to request irq gpio\n");
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
            if (ctl_dev->zlog_fp_client) {
                zlog_client_record(ctl_dev->zlog_fp_client, "Failed to request sunwavefp irq gpio\n");
                zlog_client_notify(ctl_dev->zlog_fp_client,  ZLOG_FP_REQUEST_INT_GPIO_ERROR_NO);
            }
#endif
            return err;
        } else {
            sf_debug(INFO_LOG, "Success to request irq gpio\n");
        }
        gpio_direction_input(ctl_dev->irq_pin);
    }
    else
    {
        sf_debug(ERR_LOG, "not valid irq gpio\n");
        return -EIO;
    }

#if (SF_POWER_MODE_SEL == PWR_MODE_GPIO)
    /*power-gpio*/
    ctl_dev->pwr_num = of_get_named_gpio(ctl_dev->pdev->dev.of_node, COMPATIBLE_PWR_GPIO, 0);
    sf_debug(INFO_LOG, "%s:pwr gpio number=%d\n", __func__, ctl_dev->pwr_num);
    if (gpio_is_valid(ctl_dev->pwr_num))
    {
        err = gpio_request(ctl_dev->pwr_num, "sf-pwr");

        if (err) {
            sf_debug(ERR_LOG, "Failed to request pwr gpio\n");
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
            if (ctl_dev->zlog_fp_client) {
                zlog_client_record(ctl_dev->zlog_fp_client, "Failed to request sunwavefp pwr gpio\n");
                zlog_client_notify(ctl_dev->zlog_fp_client,  ZLOG_FP_REQUEST_PWR_GPIO_ERROR_NO);
            }
#endif
            return err;
        } else {
            sf_debug(INFO_LOG, "Success to request power gpio\n");
        }
        gpio_direction_output(ctl_dev->pwr_num, 0);
    }
    else
    {
        sf_debug(ERR_LOG, "not valid pwr gpio\n");
        return -EIO;
    }
#endif
#endif
#if (SF_POWER_MODE_SEL == PWR_MODE_REGULATOR)
    ctl_dev->vdd_reg = regulator_get(&ctl_dev->pdev->dev, SF_VDD_NAME);
    if (IS_ERR(ctl_dev->vdd_reg)) {
        err = PTR_ERR(ctl_dev->vdd_reg);
        sf_debug(ERR_LOG, "%s:regulator_get %s failed from dts, err=%d\n", __func__, SF_VDD_NAME, err);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
        if (ctl_dev->zlog_fp_client) {
            zlog_client_record(ctl_dev->zlog_fp_client, "Failed to regulator get and set sunwavefp vcc\n");
            zlog_client_notify(ctl_dev->zlog_fp_client,  ZLOG_FP_REGULATOR_GET_SET_ERROR_NO);
        }
#endif
        return err;
    } else {
        sf_debug(INFO_LOG, "%s:regulator_get %s success from dts\n", __func__, SF_VDD_NAME);
    }
    /* get power voltage from dts config */
    err = of_property_read_u32(ctl_dev->pdev->dev.of_node, "power-voltage", &ctl_dev->power_voltage);
    if (err < 0) {
        sf_debug(ERR_LOG, "%s:Power voltage get failed from dts, err=%d\n", __func__, err);
    }
    sf_debug(INFO_LOG, "%s:Power voltage[%d]\n",__func__, ctl_dev->power_voltage);

    if (regulator_count_voltages(ctl_dev->vdd_reg) > 0) {
        err = regulator_set_voltage(ctl_dev->vdd_reg, ctl_dev->power_voltage,
                                    ctl_dev->power_voltage);
        if (err) {
            sf_debug(ERR_LOG, "%s:regulator_set_voltage failed, err=%d\n", __func__, err);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
            if (ctl_dev->zlog_fp_client) {
                zlog_client_record(ctl_dev->zlog_fp_client, "Failed to regulator get and set sunwavefp vcc\n");
                zlog_client_notify(ctl_dev->zlog_fp_client,  ZLOG_FP_REGULATOR_GET_SET_ERROR_NO);
            }
#endif
            return err;
        } else {
            sf_debug(INFO_LOG, "%s:regulator_set_voltage success\n", __func__);
        }
    }
#endif
    sf_ctl_device_power(true);
    sf_debug(DEBUG_LOG, "%s exit!\n", __func__);
    return err;
}
#endif

int sf_platform_init(struct sf_ctl_device *ctl_dev)
{
    int err = 0;
    sf_debug(DEBUG_LOG, "%s enter!\n", __func__);

    if (ctl_dev) {
        ctl_dev->gpio_init  = sf_ctl_device_init_gpio_pins;
        ctl_dev->power_on   = sf_ctl_device_power;
        ctl_dev->spi_clk_on = sf_spi_clock_enable;
        ctl_dev->reset      = sf_ctl_device_reset;
        ctl_dev->free_gpio  = sf_ctl_device_free_gpio;
    } else {
        sf_debug(ERR_LOG, "ctl_dev is NULL!\n");
        err = -1;
    }

    sf_debug(DEBUG_LOG, "%s exit!\n", __func__);
    return err;
}

void sf_platform_exit(struct sf_ctl_device *ctl_dev)
{
    sf_debug(DEBUG_LOG, "%s enter!\n", __func__);

    if (ctl_dev) {
        ctl_dev->gpio_init  = NULL;
        ctl_dev->power_on   = NULL;
        ctl_dev->spi_clk_on = NULL;
        ctl_dev->reset      = NULL;
        ctl_dev->free_gpio  = NULL;
    }
    else {
        sf_debug(ERR_LOG, "ctl_dev is NULL!\n");
    }

    sf_debug(DEBUG_LOG, "%s exit!\n", __func__);
}
