/*
 * platform indepent driver interface
 *
 * Coypritht (c) 2017 Goodix
 */
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/timer.h>
#include <linux/err.h>

#include "gf_spi.h"


static int gf_fingerprint_power_init(struct gf_dev *gf_dev)
{
	int ret = 0;
	struct device *dev = NULL;

	gf_debug(DEBUG_LOG, "%s:get regulator from dts\n", __func__);

	if ((gf_dev == NULL) || (gf_dev->spi == NULL)) {
		gf_debug(ERR_LOG, "%s, gf_dev or spi is NULL\n", __func__);
		return -ENODEV;
	}

	dev = &gf_dev->spi->dev;

	gf_debug(INFO_LOG, "regulator_vdd");
	gf_dev->fp_reg = regulator_get(dev, "vdd");

	if (IS_ERR(gf_dev->fp_reg)) {
		gf_debug(ERR_LOG, "%s:regulator_get vdd failed from dts\n", __func__);
		return IS_ERR(gf_dev->fp_reg);
	} else {
		gf_debug(INFO_LOG, "%s:regulator_get vdd success from dts\n", __func__);
	}

	ret = regulator_set_voltage(gf_dev->fp_reg, gf_dev->power_voltage, gf_dev->power_voltage);

	if (ret) {
		gf_debug(ERR_LOG, "%s:regulator_set_voltage failed, ret=%d\n", __func__, ret);
		goto err;
	} else {
		gf_debug(INFO_LOG, "%s:regulator_set_voltage success\n", __func__);
	}
	/*ret = regulator_enable(gf_dev->fp_reg);
	if (ret) {
		gf_debug(ERR_LOG, "%s:regulator enable failed(%d)\n",
			__func__, ret);
		goto err;
	}*/

	return 0;

err:
	regulator_put(gf_dev->fp_reg);
	gf_dev->fp_reg = NULL;
	return ret;
}


int gf_parse_dts(struct gf_dev *gf_dev)
{
	int rc = 0;
	struct device *dev = NULL;
	struct device_node *np = NULL;

	gf_debug(DEBUG_LOG, "%s enter!\n", __func__);

	if ((gf_dev == NULL) || (gf_dev->spi == NULL)) {
		gf_debug(ERR_LOG, "%s:gf_dev or spi is NULL\n", __func__);
		return -ENODEV;
	}

	dev = &gf_dev->spi->dev;
	np = dev->of_node;

       /*---reset gpio---*/
	gf_dev->reset_gpio = of_get_named_gpio(np, "fp-gpio-reset", 0);
	gf_debug(INFO_LOG, "%s:gf_dev->reset_gpio:%d\n", __func__, gf_dev->reset_gpio);

	if (gpio_is_valid(gf_dev->reset_gpio)) {
		/*rc = devm_gpio_request(dev, gf_dev->reset_gpio, "goodix_reset");*/
		rc = gpio_request(gf_dev->reset_gpio, "goodix_reset");
		if (rc) {
			gf_debug(ERR_LOG, "failed to request reset gpio!rc=%d\n", rc);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
			if (gf_dev->zlog_fp_client) {
				zlog_client_record(gf_dev->zlog_fp_client, "Failed to request goodixfp rst gpio\n");
				zlog_client_notify(gf_dev->zlog_fp_client,  ZLOG_FP_REQUEST_RST_GPIO_ERROR_NO);
			}
#endif
			goto err_reset;
		} else {
			gf_debug(INFO_LOG, "success to request reset gpio!\n");
			gpio_direction_output(gf_dev->reset_gpio, 0);
		}
	}

	/*---irq gpio---*/
	gf_dev->irq_gpio = of_get_named_gpio(np, "fp-gpio-irq", 0);
	gf_debug(INFO_LOG, "%s:gf_dev->irq_gpio:%d\n", __func__, gf_dev->irq_gpio);

	if (gpio_is_valid(gf_dev->irq_gpio)) {
		/*rc = devm_gpio_request(dev, gf_dev->irq_gpio, "goodix_irq");*/
		rc = gpio_request(gf_dev->irq_gpio, "goodix_irq");
		if (rc) {
			gf_debug(ERR_LOG, "failed to request irq gpio!rc=%d\n", rc);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
			if (gf_dev->zlog_fp_client) {
				zlog_client_record(gf_dev->zlog_fp_client, "Failed to request goodixfp irq gpio\n");
				zlog_client_notify(gf_dev->zlog_fp_client,  ZLOG_FP_REQUEST_INT_GPIO_ERROR_NO);
			}
#endif
			goto err_irq;
		} else {
			gf_debug(INFO_LOG, "success to request irq gpio!\n");
			gpio_direction_input(gf_dev->irq_gpio);
		}
	}

	/* get power type from dts config */
	rc = of_property_read_u32(np, "power-type", &gf_dev->power_type);
	if (rc < 0) {
		gf_debug(ERR_LOG, "%s:Power type get failed from dts, rc=%d\n", __func__, rc);
	}
	gf_debug(INFO_LOG, "%s:Power type[%d]\n", __func__, gf_dev->power_type);

	if (gf_dev->power_type == 1) {
		/* get power voltage from dts config */
		gf_debug(INFO_LOG, "%s PWR_MODE_REGULATOR!\n", __func__);
		rc = of_property_read_u32(np, "power-voltage", &gf_dev->power_voltage);
		if (rc < 0) {
			gf_debug(ERR_LOG, "Power voltage get failed from dts, rc=%d\n", rc);
		}
		gf_debug(INFO_LOG, "%s:Power voltage[%d]\n", __func__, gf_dev->power_voltage);

		/*  powered by pmic regulator */
		rc = gf_fingerprint_power_init(gf_dev);
		if (rc) {
			gf_debug(ERR_LOG, "%s, fingerprint_power_init failed\n", __func__);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
			if (gf_dev->zlog_fp_client) {
				zlog_client_record(gf_dev->zlog_fp_client, "Failed to regulator get and set goodixfp vcc\n");
				zlog_client_notify(gf_dev->zlog_fp_client,  ZLOG_FP_REGULATOR_GET_SET_ERROR_NO);
			}
#endif
			goto err_pwr;
		}
	} else {
		/*---powered by gpio control---*/
		gf_dev->pwr_gpio = of_get_named_gpio(np, "fp-gpio-pwr", 0);
		gf_debug(INFO_LOG, "%s:gf_dev->pwr_gpio:%d\n", __func__, gf_dev->pwr_gpio);

		if (gpio_is_valid(gf_dev->pwr_gpio)) {
			/*rc = devm_gpio_request(dev, gf_dev->irq_gpio, "goodix_pwr");*/
			rc = gpio_request(gf_dev->pwr_gpio, "goodix_pwr");
			if (rc) {
				gf_debug(ERR_LOG, "failed to request pwr gpio!rc=%d\n", rc);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
				if (gf_dev->zlog_fp_client) {
					zlog_client_record(gf_dev->zlog_fp_client, "Failed to request goodixfp pwr gpio\n");
					zlog_client_notify(gf_dev->zlog_fp_client,  ZLOG_FP_REQUEST_PWR_GPIO_ERROR_NO);
				}
#endif
				goto err_pwr;
			} else {
				gf_debug(INFO_LOG, "success to request pwr gpio!\n");
				gpio_direction_output(gf_dev->pwr_gpio, 0);
			}
		}
	}
	gf_debug(DEBUG_LOG, "%s done!\n", __func__);
	return rc;

err_pwr:
	/* get power for pmic regulator */
	if (gf_dev->power_type == 1) {
		gf_debug(ERR_LOG, "%s:gf_fingerprint_power_init failed\n", __func__);
	} else {
		/*devm_gpio_free(dev, gf_dev->irq_gpio);*/
		gf_dev->pwr_gpio = 0;
	}
	gpio_free(gf_dev->irq_gpio);
err_irq:
	/*devm_gpio_free(dev, gf_dev->reset_gpio);*/
	gpio_free(gf_dev->reset_gpio);
	gf_dev->irq_gpio = 0;
err_reset:
	gf_dev->reset_gpio = 0;
	return rc;
}

int gf_cleanup(struct gf_dev *gf_dev)
{
	int ret = 0;

	gf_debug(DEBUG_LOG, "%s enter!\n", __func__);

	if (gf_dev == NULL) {
		gf_debug(ERR_LOG, "gf_dev is NULL.\n");
		return -ENODEV;
	}

	if (gpio_is_valid(gf_dev->irq_gpio)) {
		gpio_free(gf_dev->irq_gpio);
		gf_debug(INFO_LOG, "%s:gf remove irq_gpio success\n", __func__);
		gf_dev->irq_gpio = 0;
	}
	if (gpio_is_valid(gf_dev->reset_gpio)) {
		gpio_set_value(gf_dev->reset_gpio, 0);
		gpio_free(gf_dev->reset_gpio);
		gf_debug(INFO_LOG, "%s:gf set reset low and remove reset_gpio success\n", __func__);
		gf_dev->reset_gpio = 0;
	}

	if (gf_dev->power_type == 1) {
		/*  powered by pmic regulator */
		if (gf_dev->fp_reg != NULL){
			/* regulator_disable has implement in gf_power_off
			Fix unbalanced disables for VFP */
			/* ret = regulator_disable(gf_dev->fp_reg); */
			regulator_put(gf_dev->fp_reg);
			gf_debug(INFO_LOG, "%s:regulator_disable and regulator_put success\n", __func__);
			gf_dev->fp_reg = NULL;
		}
	} else {
		if (gpio_is_valid(gf_dev->pwr_gpio)) {
			gpio_set_value(gf_dev->pwr_gpio, 0);
			gpio_free(gf_dev->pwr_gpio);
			gf_debug(INFO_LOG, "%s:gf set power low and remove pwr_gpio success\n", __func__);
			gf_dev->pwr_gpio = 0;
		}
	}

	return ret;
}

int gf_power_on(struct gf_dev *gf_dev)
{
	int ret = 0;

	gf_debug(DEBUG_LOG, "%s enter!\n", __func__);

	if (gf_dev == NULL) {
		gf_debug(ERR_LOG, "gf_dev is NULL.\n");
		return -ENODEV;
	}

	if (gf_dev->power_type == 1) {
		/*  powered by pmic regulator */
		if (gf_dev->fp_reg != NULL) {
			ret = regulator_enable(gf_dev->fp_reg);
			if (ret) {
				gf_debug(ERR_LOG, "%s:regulator_enable failed, ret=%d\n", __func__, ret);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
				if (gf_dev->zlog_fp_client) {
					zlog_client_record(gf_dev->zlog_fp_client, "Failed to regulator enable goodixfp vcc\n");
					zlog_client_notify(gf_dev->zlog_fp_client,  ZLOG_FP_REGULATOR_ENABLE_ERROR_NO);
				}
#endif
				goto err;
			} else {
				gf_debug(INFO_LOG, "%s:regulator_enable success\n", __func__);
			}
		}
	} else {
		/* TODO: add your power control here */
		if (gpio_is_valid(gf_dev->pwr_gpio)) {
			gpio_set_value(gf_dev->pwr_gpio, 1);
			gf_debug(INFO_LOG, "----gf power on ok ----\n");
		}
	}

	msleep(20);

err:
	return ret;
}

int gf_power_off(struct gf_dev *gf_dev)
{
	int ret = 0;

	gf_debug(DEBUG_LOG, "%s enter!\n", __func__);

	if (gf_dev == NULL) {
		gf_debug(ERR_LOG, "gf_dev is NULL.\n");
		return -ENODEV;
	}

	if (gf_dev->power_type == 1) {
		/*  powered by pmic regulator */
		if (gf_dev->fp_reg != NULL) {
			ret = regulator_disable(gf_dev->fp_reg);
			if (ret) {
				gf_debug(ERR_LOG, "%s:regulator disable failed(%d)\n", __func__, ret);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
				if (gf_dev->zlog_fp_client) {
					zlog_client_record(gf_dev->zlog_fp_client, "Failed to regulator disable goodixfp vcc\n");
					zlog_client_notify(gf_dev->zlog_fp_client,  ZLOG_FP_REGULATOR_DISABLE_ERROR_NO);
				}
#endif
				goto err;
			} else {
				gf_debug(INFO_LOG, "%s:regulator_disable success\n", __func__);
			}
		}
	} else {
		/* TODO: add your power control here */
		if (gpio_is_valid(gf_dev->pwr_gpio)) {
			gpio_set_value(gf_dev->pwr_gpio, 0);
			gf_debug(INFO_LOG, "----gf power off ok----\n");
		}
	}
err:
	return ret;
}

int gf_hw_reset(struct gf_dev *gf_dev, unsigned int delay_ms)
{
	int ret = 0;

	gf_debug(DEBUG_LOG, "%s enter!\n", __func__);

	if (gf_dev == NULL) {
		gf_debug(ERR_LOG, "gf_dev is NULL.\n");
		return -ENODEV;
	}
	if (gpio_is_valid(gf_dev->reset_gpio)) {
		/* gpio_direction_output(gf_dev->reset_gpio, 1); */
		gpio_set_value(gf_dev->reset_gpio, 0);
		msleep(20);
		gpio_set_value(gf_dev->reset_gpio, 1);
		msleep(delay_ms);
		gf_debug(INFO_LOG, "----gf hw reset ok----\n");
	}

	return ret;
}

int gf_irq_num(struct gf_dev *gf_dev)
{
	gf_debug(DEBUG_LOG, "%s enter!\n", __func__);

	if (gf_dev == NULL) {
		gf_debug(ERR_LOG, "gf_dev is NULL.\n");
		return -ENODEV;
	} else {
		return gpio_to_irq(gf_dev->irq_gpio);
	}
}
