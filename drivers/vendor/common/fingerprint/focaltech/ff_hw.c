/**
 * ff_hw.c
 *
**/

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>

#include "ff_log.h"
#include "ff_ctl.h"

# undef LOG_TAG
#define LOG_TAG "SPRD"

/*
 * Driver configuration. See ff_ctl.c
 */
extern struct ff_driver_config_t *g_config;

int ff_ctl_init_pins(void)
{
    int err = 0;
    struct device_node *dev_node = NULL;
    enum of_gpio_flags flags;
    struct device *dev = NULL;
    FF_LOGV("%s enter", __func__);

    if (unlikely(!g_config)) {
        return (-ENOSYS);
    }

#if defined(USE_PLATFORM_BUS)
    dev = &g_config->platform_dev->dev;
#elif defined(USE_SPI_BUS)
    dev = &g_config->spi_dev->dev;
#endif
    dev_node = dev->of_node;

    /* Find device tree node. */
    /*dev_node = of_find_compatible_node(NULL, NULL, FF_COMPATIBLE_NODE);
    if (!dev_node) {
        FF_LOGE("of_find_compatible_node(.., '%s') failed", FF_COMPATIBLE_NODE);
        return (-ENODEV);
    }*/

    /*--------------------Initialize rst pin--------------------*/
    g_config->gpio_rst_pin = of_get_named_gpio_flags(dev_node, "ff_gpio_rst_pin", 0, &flags);
    FF_LOGI("rst_gpio=%d", g_config->gpio_rst_pin);
    if (gpio_is_valid(g_config->gpio_rst_pin)) {
        err = gpio_request(g_config->gpio_rst_pin, "ff_gpio_rst_pin");
        if (err) {
            FF_LOGE("gpio_request_rst(%d) failed, err=%d", g_config->gpio_rst_pin, err);
            goto rst1;
        } else {
            FF_LOGI("gpio_request_rst(%d) success", g_config->gpio_rst_pin);
        }
        err = gpio_direction_output(g_config->gpio_rst_pin, 0);
        if (err) {
            FF_LOGE("gpio_direction_output_rst(%d, 0) failed, err=%d", g_config->gpio_rst_pin, err);
            goto rst2;
        } else {
            FF_LOGI("gpio_direction_output_rst(%d, 0) success", g_config->gpio_rst_pin);
        }
    } else {
        FF_LOGE("g_config->gpio_rst_pin(%d) is invalid", g_config->gpio_rst_pin);
        err = -EINVAL;
        goto rst1;
    }

    /*--------------------Initialize int pin--------------------*/
    g_config->gpio_int_pin = of_get_named_gpio_flags(dev_node, "ff_gpio_int_pin", 0, &flags);
    FF_LOGI("irq_gpio=%d", g_config->gpio_int_pin);
    if (gpio_is_valid(g_config->gpio_int_pin)) {
        err = gpio_request(g_config->gpio_int_pin, "ff_gpio_int_pin");
        if (err) {
            FF_LOGE("gpio_request_int(%d) failed, err=%d", g_config->gpio_int_pin, err);
            goto int1;
        } else {
            FF_LOGI("gpio_request_int(%d) success", g_config->gpio_int_pin);
        }
        err = gpio_direction_input(g_config->gpio_int_pin);
        if (err) {
            FF_LOGE("gpio_direction_input_int(%d) failed, err=%d", g_config->gpio_int_pin, err);
            goto int2;
        } else {
            FF_LOGI("gpio_direction_input_int(%d) success", g_config->gpio_int_pin);
        }
    } else {
        FF_LOGE("g_config->gpio_int_pin(%d) is invalid", g_config->gpio_int_pin);
        err = -EINVAL;
        goto int1;
    }

    /* Retrieve the IRQ number. */
    g_config->irq_num = gpio_to_irq(g_config->gpio_int_pin);
    if (g_config->irq_num < 0) {
        FF_LOGE("gpio_to_irq failed");
        goto int3;
    } else {
        FF_LOGD("gpio_to_irq success, irq_num=%d", g_config->irq_num);
    }

    /*--------------------Initialize pwr pin--------------------*/
    err = of_property_read_u32(dev_node, "power-type", &g_config->power_type);
    if (err < 0) {
        FF_LOGE("%s:Power type get failed from dts, err=%d", __func__, err);
    }
    FF_LOGI("%s:Power type[%d]", __func__, g_config->power_type);

    if (1 == g_config->power_type) {
        /* powered by regulator */
        FF_LOGI("%s PWR_MODE_REGULATOR", __func__);
        err = of_property_read_u32(dev_node, "power-voltage", &g_config->power_voltage);
        if (err < 0) {
            FF_LOGE("%s:Power voltage get failed from dts, err=%d", __func__, err);
        }
        FF_LOGI("%s:Power voltage[%d]", __func__, g_config->power_voltage);

		FF_LOGI("regulator_vdd");
		g_config->fp_reg = regulator_get(dev, "vdd");
		if (IS_ERR(g_config->fp_reg)) {
			FF_LOGE("%s:regulator_get vdd failed from dts", __func__);
            err = IS_ERR(g_config->fp_reg);
            goto pwr1;
		} else {
			FF_LOGI("%s:regulator_get vdd success from dts", __func__);
		}

		err = regulator_set_voltage(g_config->fp_reg, g_config->power_voltage, g_config->power_voltage);
		if (err) {
			FF_LOGV("%s:regulator_set_voltage failed, err=%d", __func__, err);
			goto pwr2;
		} else {
			FF_LOGI("%s:regulator_set_voltage success", __func__);
		}
    } else {
        /* powered by gpio */
        FF_LOGI("%s PWR_MODE_GPIO", __func__);
        g_config->gpio_pwr_pin = of_get_named_gpio_flags(dev_node, "ff_gpio_pwr_pin", 0, &flags);
        FF_LOGI("pwr_gpio=%d", g_config->gpio_pwr_pin);
        if (gpio_is_valid(g_config->gpio_pwr_pin)) {
            err = gpio_request(g_config->gpio_pwr_pin, "ff_gpio_pwr_pin");
            if (err) {
                FF_LOGE("gpio_request_pwr(%d) failed, err=%d", g_config->gpio_pwr_pin, err);
                goto pwr1;
            } else {
                FF_LOGI("gpio_request_pwr(%d) success", g_config->gpio_pwr_pin);
            }
            err = gpio_direction_output(g_config->gpio_pwr_pin, 0);
            if (err) {
                FF_LOGE("gpio_direction_output_pwr(%d) failed, err=%d", g_config->gpio_pwr_pin, err);
                goto pwr2;
            } else {
                FF_LOGI("gpio_direction_output_pwr(%d) success", g_config->gpio_pwr_pin);
            }
        } else {
            FF_LOGE("g_config->gpio_pwr_pin(%d) is invalid", g_config->gpio_pwr_pin);
            err = -EINVAL;
            goto pwr1;
        }
    }

    FF_LOGI("%s leave", __func__);
    return err;

pwr2:
    if (1 == g_config->power_type) {
        regulator_put(g_config->fp_reg);
    } else {
        gpio_free(g_config->gpio_pwr_pin);
    }
pwr1:
    if (1 == g_config->power_type) {
        g_config->fp_reg = NULL;
    } else {
        g_config->gpio_pwr_pin = 0;
    }

int3:
    g_config->irq_num = 0;
int2:
    gpio_free(g_config->gpio_int_pin);
int1:
    g_config->gpio_int_pin = 0;

rst2:
    gpio_free(g_config->gpio_rst_pin);
rst1:
    g_config->gpio_rst_pin = 0;
    return err;
}

int ff_ctl_free_pins(void)
{
    int err = 0;
    FF_LOGV("%s enter", __func__);

    /* Release GPIO resources. */
    if (g_config && g_config->gpio_rst_pin) {
        gpio_set_value(g_config->gpio_rst_pin, 0);
        gpio_free(g_config->gpio_rst_pin);
        FF_LOGI("%s:set rst low and free rst_gpio success", __func__);
        g_config->gpio_rst_pin = 0;
    }

    if (g_config && g_config->gpio_int_pin) {
        gpio_free(g_config->gpio_int_pin);
        FF_LOGI("%s:free irq_gpio success", __func__);
        g_config->gpio_int_pin = 0;
    }

    if (g_config && (1 == g_config->power_type)) {
        if (g_config->fp_reg != NULL) {
            err = regulator_disable(g_config->fp_reg);
            regulator_put(g_config->fp_reg);
            FF_LOGI("%s:regulator_disable and regulator_put success", __func__);
            g_config->fp_reg = NULL;
        }
    } else {
        if (g_config && g_config->gpio_pwr_pin) {
            gpio_set_value(g_config->gpio_pwr_pin, 0);
            gpio_free(g_config->gpio_pwr_pin);
            FF_LOGI("%s:set pwr low and free pwr_gpio success", __func__);
            g_config->gpio_pwr_pin = 0;
        }
    }

    FF_LOGV("%s leave", __func__);
    return err;
}

int ff_ctl_enable_spiclk(bool on)
{
#if defined(USE_PLATFORM_BUS)
	return 0;
#elif defined(USE_SPI_BUS)
    if (on) {
        mt_spi_enable_master_clk(g_config->spi_dev);
    } else {
        mt_spi_disable_master_clk(g_config->spi_dev);
    }
#endif
}

int ff_ctl_enable_power(bool on)
{
    int err = 0;
    FF_LOGD("power: %s", on ? "on" : "off");

    if (unlikely(!g_config)) {
        return (-ENOSYS);
    }

    if (1 == g_config->power_type) {
        /*  powered by regulator */
        if (g_config->fp_reg != NULL) {
            if (on) {
                err = regulator_enable(g_config->fp_reg);
                if (err) {
                    FF_LOGE("%s:regulator_enable failed, err=%d", __func__, err);
                    return err;
                } else {
                    FF_LOGI("%s:regulator_enable success", __func__);
                }
            } else {
                err = regulator_disable(g_config->fp_reg);
                if (err) {
                    FF_LOGE("%s:regulator_disable failed, err=%d", __func__, err);
                    return err;
                } else {
                    FF_LOGI("%s:regulator_disable success", __func__);
                }
            }
        }
    } else {
        if (on) {
            if (gpio_is_valid(g_config->gpio_pwr_pin)) {
                gpio_set_value(g_config->gpio_pwr_pin, 1);
                FF_LOGI("---- ff power on ok ----");
            }
        } else {
            if (gpio_is_valid(g_config->gpio_pwr_pin)) {
                gpio_set_value(g_config->gpio_pwr_pin, 0);
                FF_LOGI("---- ff power off ok ----");
            }
        }
    }

    FF_LOGV("%s leave", __func__);
    return err;
}

int ff_ctl_reset_device(void)
{
    int err = 0;
    FF_LOGV("%s enter", __func__);

    if (unlikely(!g_config)) {
        return (-ENOSYS);
    }

    /* 3-1: Pull down RST pin. */
    gpio_set_value(g_config->gpio_rst_pin, 0);

    /* 3-2: Delay for 10ms. */
    mdelay(10);

    /* Pull up RST pin. */
    gpio_set_value(g_config->gpio_rst_pin, 1);

    FF_LOGV("%s leave", __func__);
    return err;
}

const char *ff_ctl_arch_str(void)
{
    return "sprd";
}

