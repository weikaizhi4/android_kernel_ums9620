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
 * the event syst
 em. A user space process will need to traverse all the event
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
#include "fpc1020.h"

static struct fpc_dev g_fpc_dev;
static irqreturn_t fpc_irq_handler(int irq, void *handle);

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

#ifdef USE_SPI_BUS
static int fpc_read_id(void)
{
	int status = 0;
	struct spi_message m;
	u8 tx[3] = {0xfc, 0, 0};
	struct spi_transfer id = {
		.speed_hz = 1000000,
		.tx_buf = tx,
		.rx_buf = tx,
		.len = sizeof(tx),
	};
	fpc_debug(DEBUG_LOG, "%s enter!\n", __func__);
	status = spi_setup(g_fpc_dev.spidev);
	fpc_debug(INFO_LOG, "%s:spi_setup, status=%d\n", __func__, status);
	spi_message_init(&m);
	spi_message_add_tail(&id, &m);
	status = spi_sync(g_fpc_dev.spidev, &m);
	fpc_debug(INFO_LOG, "%s:spi_sync=%d id=%2x %2x %2x\n", __func__, status, tx[0], tx[1], tx[2]);
	if (tx[1] < 0x10) {
		return GENERIC_ERR;
	}
	fpc_debug(DEBUG_LOG, "%s exit!\n", __func__);
	return GENERIC_OK;
}
#endif

#ifdef FPC_USE_PINCTRL
static void fpc_pinctrl(void)
{
	int i = 0;
	const int ms[4] = {2, 2, 2, 3};
	const char ptl_name[4][16] = {"fpc_vdd_on", "fpc_rst_hi", "fpc_rst_lo", "fpc_rst_hi"};
	struct pinctrl_state *ptl_state = NULL;
	fpc_debug(DEBUG_LOG, "%s enter!\n", __func__);
	for (i = 0; i < 4; i++) {
		ptl_state = pinctrl_lookup_state(g_fpc_dev.ptl, ptl_name[i]);
		if (!IS_ERR(ptl_state)) {
			fpc_debug(INFO_LOG, "%s:found:%s\n", __func__, ptl_name[i]);
			pinctrl_select_state(g_fpc_dev.ptl, ptl_state);
			msleep(ms[i]);
		} else {
			fpc_debug(ERR_LOG, "%s:no found:%s\n", __func__, ptl_name[i]);
		}
	}
	fpc_debug(DEBUG_LOG, "%s exit!\n", __func__);
}
#endif

static void fpc_gpio_exit(struct device *dev)
{
#ifdef FPC_USE_PINCTRL
	int i = 0;
	const char ptl_name[2][16] = {"fpc_vdd_lo", "fpc_rst_lo"};
	struct pinctrl_state *ptl_state = NULL;
#endif
	fpc_debug(INFO_LOG, "%s enter!\n", __func__);

	/*--------------------fpc_pwr--------------------*/
	if (g_fpc_dev.power_type == 1) {
		if (!IS_ERR(g_fpc_dev.fp_reg)) {
			regulator_disable(g_fpc_dev.fp_reg);
			regulator_put(g_fpc_dev.fp_reg);
			g_fpc_dev.fp_reg = NULL;
			fpc_debug(INFO_LOG, "%s:regulator_disable and regulator_put\n", __func__);
		}
	} else {
		if (gpio_is_valid(g_fpc_dev.pwr_gpio)) {
			gpio_set_value(g_fpc_dev.pwr_gpio, 0);
			devm_gpio_free(dev, g_fpc_dev.pwr_gpio);
			g_fpc_dev.pwr_gpio = 0;
			fpc_debug(INFO_LOG, "%s:set power low and remove pwr_gpio\n", __func__);
		}
	}

	/*--------------------fpc_irq--------------------*/
	if (g_fpc_dev.irq_num != 0){
		disable_irq_nosync(g_fpc_dev.irq_num);
		devm_free_irq(dev, g_fpc_dev.irq_num, dev);
		g_fpc_dev.irq_num = 0;
		fpc_debug(INFO_LOG, "%s:disable_irq_nosync and devm_free_irq\n", __func__);
	}
	if (gpio_is_valid(g_fpc_dev.irq_gpio)) {
		devm_gpio_free(dev, g_fpc_dev.irq_gpio);
		g_fpc_dev.irq_gpio = 0;
		fpc_debug(INFO_LOG, "%s:set irq low and free irq_gpio\n", __func__);
	}

	/*--------------------fpc_rst--------------------*/
	if (gpio_is_valid(g_fpc_dev.rst_gpio)) {
		gpio_set_value(g_fpc_dev.rst_gpio, 0);
		devm_gpio_free(dev, g_fpc_dev.rst_gpio);
		g_fpc_dev.rst_gpio = 0;
		fpc_debug(INFO_LOG, "%s:set rst low and free rst_gpio\n", __func__);
	}

#ifdef FPC_USE_PINCTRL
	if(!IS_ERR(g_fpc_dev.ptl)){
		fpc_debug(INFO_LOG, "%s:free pinctrl\n", __func__);
		for (i = 0; i < 2; i++) {
			ptl_state = pinctrl_lookup_state(g_fpc_dev.ptl, ptl_name[i]);
			if (!IS_ERR(ptl_state)) {
				pinctrl_select_state(g_fpc_dev.ptl, ptl_state);
			}
		}
	}
#endif

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	if (g_fpc_dev.zlog_fp_client) {
		zlog_unregister_client(g_fpc_dev.zlog_fp_client);
		fpc_debug(INFO_LOG, "%s zlog_unregister_client fpc_zlog_fp_dev\n", __func__);
	}
#endif

	fpc_debug(INFO_LOG, "%s exit!\n", __func__);
}

static int fpc_wakeup_init(struct device *dev)
{
	fpc_debug(DEBUG_LOG, "%s enter!\n", __func__);

	#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 10, 0)
		g_fpc_dev.ttw_wl = wakeup_source_register(dev, "fpc_ttw_wl");
	#else
		wakeup_source_init(g_fpc_dev.ttw_wl, "fpc_ttw_wl");
	#endif
	fpc_debug(INFO_LOG, "%s:fpc_wakeup_init done\n", __func__);
	/*enable_irq(g_fpc_dev.irq_num); //Fix Unbalanced enable for IRQ
	fpc_debug(INFO_LOG, "%s:enable_irq\n", __func__);*/
	fpc_debug(DEBUG_LOG, "%s exit!\n", __func__);
	return GENERIC_OK;
}

static int fpc_irq_init(struct device *dev)
{
	int ret = 0;
	/* IRQF_NO_SUSPEND flag will cause can not unlock when suspend
	See Documentation/power/suspend-and-interrupts.txt
	int irqf = IRQF_TRIGGER_RISING | IRQF_ONESHOT | IRQF_NO_SUSPEND; */
	int irqf = IRQF_TRIGGER_RISING | IRQF_ONESHOT;
	fpc_debug(INFO_LOG, "%s enter!\n", __func__);
	fpc_debug(INFO_LOG, "%s:irq flags:0x%08x\n", __func__, irqf);
	g_fpc_dev.irq_num = gpio_to_irq(g_fpc_dev.irq_gpio);
	if (g_fpc_dev.irq_num == 0) {
		fpc_debug(ERR_LOG, "%s:gpio_to_irq failed\n", __func__);
		return GENERIC_ERR;
	} else {
		fpc_debug(INFO_LOG, "%s:gpio_to_irq success\n", __func__);
	}

	ret = devm_request_threaded_irq(dev, g_fpc_dev.irq_num,
			NULL, fpc_irq_handler, irqf, dev_name(dev), dev);
	if (ret != 0) {
		fpc_debug(ERR_LOG, "%s:request irq_num[%d] failed, ret=%d\n", __func__, g_fpc_dev.irq_num, ret);
		goto err;
	} else {
		fpc_debug(INFO_LOG, "%s:request irq_num[%d] success\n", __func__, g_fpc_dev.irq_num);
	}
	enable_irq_wake(g_fpc_dev.irq_num);

err:
	return ret;
}

static int fpc_gpio_init(struct device *dev)
{
	int ret = 0;
	fpc_debug(INFO_LOG, "%s enter!\n", __func__);
#ifdef FPC_USE_PINCTRL
	if (!IS_ERR(g_fpc_dev.ptl)) {
		fpc_pinctrl();
	}
#endif
	/*--------------------fpc_pwr--------------------*/
	if (g_fpc_dev.power_type == 1) {
		if (!IS_ERR(g_fpc_dev.fp_reg)) {
			fpc_debug(INFO_LOG, "%s:regulator power\n", __func__);
			ret = regulator_enable(g_fpc_dev.fp_reg);
			if (ret != 0) {
				fpc_debug(ERR_LOG, "%s:regulator_enable failed, ret=%d\n", __func__, ret);
				goto err;
			} else {
				fpc_debug(INFO_LOG, "%s:regulator_enable success\n", __func__);
			}
		} else {
			fpc_debug(ERR_LOG, "%s:g_fpc_dev.fp_reg is err\n", __func__);
		}
	} else {
		if (gpio_is_valid(g_fpc_dev.pwr_gpio)) {
			fpc_debug(INFO_LOG, "%s:gpio power\n", __func__);
			gpio_direction_output(g_fpc_dev.pwr_gpio, 1);
			fpc_debug(INFO_LOG, "%s:=== power on ===\n", __func__);
		}
	}

	/*--------------------fpc_rst--------------------*/
	if (gpio_is_valid(g_fpc_dev.rst_gpio)) {
		fpc_debug(INFO_LOG, "%s:gpio reset\n", __func__);
		msleep(2);
		gpio_direction_output(g_fpc_dev.rst_gpio, 1);
		msleep(2);
		gpio_set_value(g_fpc_dev.rst_gpio, 0);
		msleep(2);
		gpio_set_value(g_fpc_dev.rst_gpio, 1);
		msleep(2);
		fpc_debug(INFO_LOG, "%s:=== reset ok ===\n", __func__);
	}

	/*--------------------fpc_irq--------------------*/
	if (gpio_is_valid(g_fpc_dev.irq_gpio)) {
		fpc_debug(INFO_LOG, "%s:gpio irq\n", __func__);
		gpio_direction_input(g_fpc_dev.irq_gpio);
	}

	fpc_debug(INFO_LOG, "%s:after reset, irq=%d\n", __func__, gpio_get_value(g_fpc_dev.irq_gpio));
#ifdef USE_SPI_BUS
	if (g_fpc_dev.spidev != NULL && fpc_read_id() != 0) {
		fpc_gpio_exit(dev);
		return GENERIC_ERR;
	}
#endif

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
    g_fpc_dev.zlog_fp_client = zlog_register_client(&fpc_zlog_fp_dev);
    if (g_fpc_dev.zlog_fp_client) {
        fpc_debug(INFO_LOG, "%s zlog_register_fpc_client success\n", __func__);
    } else {
        fpc_debug(ERR_LOG, "%s zlog_register_fpc_client fail\n", __func__);
    }
#endif

	fpc_debug(INFO_LOG, "%s exit, ret=%d\n", __func__, ret);

err:
	return ret;
}

static int fpc_dts_init(struct device *dev)
{
	int ret = 0;
	const char *node_name = "fpc,fpc1020";
	fpc_debug(INFO_LOG, "%s enter!\n", __func__);
#ifdef FPC_USE_PINCTRL
	g_fpc_dev.ptl = devm_pinctrl_get(dev);
#endif
	g_fpc_dev.node = of_find_compatible_node(NULL, NULL, node_name);
	if (g_fpc_dev.node == NULL) {
		fpc_debug(ERR_LOG, "%s:find node %s failed\n", __func__, node_name);
		return GENERIC_ERR;
	} else {
		fpc_debug(INFO_LOG, "%s:find node %s success\n", __func__, node_name);
	}

	/*--------------------fpc_irq--------------------*/
	g_fpc_dev.irq_gpio = of_get_named_gpio(g_fpc_dev.node, "fpc_irq", 0);
	if (!gpio_is_valid(g_fpc_dev.irq_gpio)) {
		fpc_debug(ERR_LOG, "%s:get name fpc_irq failed\n", __func__);
		goto err_irq;
	} else {
		fpc_debug(INFO_LOG, "%s:get name fpc_irq success\n", __func__);
	}
	fpc_debug(INFO_LOG, "%s:irq_gpio[%d]\n", __func__, g_fpc_dev.irq_gpio);
	ret = devm_gpio_request(dev, g_fpc_dev.irq_gpio, "fpc_irq");
	if (ret) {
		fpc_debug(ERR_LOG, "%s:request fpc_irq failed, ret=%d\n", __func__, ret);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		if (g_fpc_dev.zlog_fp_client) {
			zlog_client_record(g_fpc_dev.zlog_fp_client, "Failed to request fpc irq gpio\n");
			zlog_client_notify(g_fpc_dev.zlog_fp_client,  ZLOG_FP_REQUEST_INT_GPIO_ERROR_NO);
		}
#endif
		goto err_irq;
	} else {
		fpc_debug(INFO_LOG, "%s:request fpc_irq success\n", __func__);
	}

	/*--------------------fpc_rst--------------------*/
	g_fpc_dev.rst_gpio = of_get_named_gpio(g_fpc_dev.node, "fpc_rst", 0);
	if (!gpio_is_valid(g_fpc_dev.rst_gpio)) {
		fpc_debug(ERR_LOG, "%s:get name fpc_rst failed\n", __func__);
		goto err_rst;
	} else {
		fpc_debug(INFO_LOG, "%s:get name fpc_rst success\n", __func__);
	}
	fpc_debug(INFO_LOG, "%s:rst_gpio[%d]\n", __func__, g_fpc_dev.rst_gpio);
	ret = devm_gpio_request(dev, g_fpc_dev.rst_gpio, "fpc_rst");
	if (ret) {
		fpc_debug(ERR_LOG, "%s:request fpc_rst failed, ret=%d\n", __func__, ret);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		if (g_fpc_dev.zlog_fp_client) {
			zlog_client_record(g_fpc_dev.zlog_fp_client, "Failed to request fpc rst gpio\n");
			zlog_client_notify(g_fpc_dev.zlog_fp_client,  ZLOG_FP_REQUEST_RST_GPIO_ERROR_NO);
		}
#endif
		goto err_rst;
	} else {
		fpc_debug(INFO_LOG, "%s:request fpc_rst success\n", __func__);
	}

	/*--------------------fpc_pwr--------------------*/
	ret = of_property_read_u32(g_fpc_dev.node, "power-type", &g_fpc_dev.power_type);
	if (ret < 0) {
		fpc_debug(ERR_LOG, "%s:Power type get failed from dts, ret=%d\n", __func__, ret);
	}
	fpc_debug(INFO_LOG, "%s:power type[%d]\n", __func__, g_fpc_dev.power_type);

	if (g_fpc_dev.power_type == 1) {
		/* get power voltage from dts config */
		ret = of_property_read_u32(g_fpc_dev.node, "power-voltage", &g_fpc_dev.power_voltage);
		if (ret < 0) {
			fpc_debug(ERR_LOG, "Power voltage get failed from dts, ret=%d\n", ret);
		}
		fpc_debug(INFO_LOG, "%s:Power voltage[%d]\n", __func__, g_fpc_dev.power_voltage);

		g_fpc_dev.fp_reg = devm_regulator_get(dev, "vdd");
		if (IS_ERR(g_fpc_dev.fp_reg)) {
			fpc_debug(ERR_LOG, "%s:get regulator failed\n", __func__);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
			if (g_fpc_dev.zlog_fp_client) {
				zlog_client_record(g_fpc_dev.zlog_fp_client, "Failed to regulator get and set fpc vcc\n");
				zlog_client_notify(g_fpc_dev.zlog_fp_client,  ZLOG_FP_REGULATOR_GET_SET_ERROR_NO);
			}
#endif
			goto err_pwr1;
		} else {
			fpc_debug(INFO_LOG, "%s:get regulator success\n", __func__);
		}

		ret = regulator_set_voltage(g_fpc_dev.fp_reg, g_fpc_dev.power_voltage, g_fpc_dev.power_voltage);
		if (ret) {
			fpc_debug(ERR_LOG, "%s:regulator_set_voltage failed, ret=%d\n", __func__, ret);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
			if (g_fpc_dev.zlog_fp_client) {
				zlog_client_record(g_fpc_dev.zlog_fp_client, "Failed to regulator get and set fpc vcc\n");
				zlog_client_notify(g_fpc_dev.zlog_fp_client,  ZLOG_FP_REGULATOR_GET_SET_ERROR_NO);
			}
#endif
			goto err_pwr2;
		} else {
			fpc_debug(INFO_LOG, "%s:regulator_set_voltage success\n", __func__);
		}
	} else {
		g_fpc_dev.pwr_gpio = of_get_named_gpio(g_fpc_dev.node, "fpc_vdd", 0);
		if (!gpio_is_valid(g_fpc_dev.pwr_gpio)) {
			fpc_debug(ERR_LOG, "%s:get name fpc_vdd failed\n", __func__);
			goto err_pwr1;
		} else {
			fpc_debug(INFO_LOG, "%s:get name fpc_vdd success\n", __func__);
		}
		fpc_debug(INFO_LOG, "%s:pwr_gpio[%d]\n", __func__, g_fpc_dev.pwr_gpio);
		ret = devm_gpio_request(dev, g_fpc_dev.pwr_gpio, "fpc_vdd");
		if (ret) {
			fpc_debug(ERR_LOG, "%s:request fpc_vdd failed, ret=%d\n", __func__, ret);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
			if (g_fpc_dev.zlog_fp_client) {
				zlog_client_record(g_fpc_dev.zlog_fp_client, "Failed to request fpc pwr gpio\n");
				zlog_client_notify(g_fpc_dev.zlog_fp_client,  ZLOG_FP_REQUEST_PWR_GPIO_ERROR_NO);
			}
#endif
			goto err_pwr2;
		} else {
			fpc_debug(INFO_LOG, "%s:request fpc_vdd success\n", __func__);
		}
	}

	fpc_debug(INFO_LOG, "%s exit, ret=%d\n", __func__, ret);
	return ret;

err_pwr2:
	if (g_fpc_dev.power_type == 1) {
		devm_regulator_put(g_fpc_dev.fp_reg);
	}

err_pwr1:
	if (g_fpc_dev.power_type == 1) {
		g_fpc_dev.fp_reg = NULL;
	} else {
		g_fpc_dev.pwr_gpio = 0;
	}
	devm_gpio_free(dev, g_fpc_dev.rst_gpio);

err_rst:
	g_fpc_dev.rst_gpio = 0;
	devm_gpio_free(dev, g_fpc_dev.irq_gpio);

err_irq:
	g_fpc_dev.irq_gpio = 0;
	return GENERIC_ERR;
}

static int fpc_driver_init(struct device *dev)
{
	int i = 0;
	int (*fpc_p[4])(struct device *dev) = {fpc_dts_init, fpc_gpio_init, fpc_irq_init, fpc_wakeup_init};
	fpc_debug(INFO_LOG, "%s enter!\n", __func__);
	for (i = 0; i < 4; i++) {
		if(fpc_p[i](dev) != GENERIC_OK) return GENERIC_ERR;
	}
	fpc_debug(INFO_LOG, "%s exit!\n", __func__);
	return GENERIC_OK;
}

static ssize_t clk_enable_set(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	(void)dev;(void)attr;(void)buf;
	fpc_debug(DEBUG_LOG, "%s enter!\n", __func__);
#ifdef USE_SPI_BUS
	if (*buf == '1') {
		mt_spi_enable_master_clk(g_fpc_dev.spidev);
	} else {
		mt_spi_disable_master_clk(g_fpc_dev.spidev);
	}
	fpc_debug(INFO_LOG, "%s, buf=%s\n", __func__, buf);
#endif
	fpc_debug(DEBUG_LOG, "%s exit!\n", __func__);
	return count;
}
static DEVICE_ATTR(clk_enable, S_IWUSR, NULL, clk_enable_set);

static ssize_t wakeup_enable_set(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	fpc_debug(DEBUG_LOG, "%s enter!\n", __func__);
	__pm_wakeup_event(g_fpc_dev.ttw_wl, 2000);
	(void)dev;(void)attr;(void)buf;
	fpc_debug(DEBUG_LOG, "%s exit!\n", __func__);
	return count;
}
static DEVICE_ATTR(wakeup_enable, S_IWUSR, NULL, wakeup_enable_set);

static ssize_t compatible_all_set(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	fpc_debug(INFO_LOG, "%s enter!\n", __func__);
	if (0 == strncmp(buf, "enable", strlen("enable")) && g_fpc_dev.irq_num == 0) {
		fpc_debug(INFO_LOG, "%s:fpc_driver_init\n", __func__);
		if (fpc_driver_init(dev) != GENERIC_OK) {
			return GENERIC_ERR;
		}
	} else if (0 == strncmp(buf, "disable", strlen("disable")) && g_fpc_dev.irq_num != 0) {
		fpc_debug(INFO_LOG, "%s:fpc_gpio_exit\n", __func__);
		fpc_gpio_exit(dev);
	}
	(void)attr;
	fpc_debug(INFO_LOG, "%s exit!\n", __func__);
	return count;
}

static DEVICE_ATTR(compatible_all, S_IWUSR, NULL, compatible_all_set);

static ssize_t irq_get(struct device *dev, struct device_attribute *attr, char *buf)
{
	fpc_debug(DEBUG_LOG, "%s enter!\n", __func__);
	(void)dev;(void)attr;
	fpc_debug(INFO_LOG, "%s:gpio_get_value(g_fpc_dev.irq_gpio)=%d\n", __func__, gpio_get_value(g_fpc_dev.irq_gpio));
	return scnprintf(buf, PAGE_SIZE, "%i\n", gpio_get_value(g_fpc_dev.irq_gpio));
}

static ssize_t irq_ack(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	fpc_debug(DEBUG_LOG, "%s enter!\n", __func__);
	(void)dev;
	(void)attr;
	(void)buf;
	return count;
}
static DEVICE_ATTR(irq, S_IRUSR | S_IWUSR, irq_get, irq_ack);

static struct attribute *fpc_attributes[] = {
	&dev_attr_wakeup_enable.attr,
	&dev_attr_irq.attr,
	&dev_attr_clk_enable.attr,
	&dev_attr_compatible_all.attr,
	NULL
};

static struct attribute_group fpc_attribute_group = {
	.attrs = fpc_attributes,
};

static irqreturn_t fpc_irq_handler(int irq, void *handle)
{
	struct device *dev = (struct device *)handle;
	fpc_debug(DEBUG_LOG, "%s enter!\n", __func__);
	sysfs_notify(&dev->kobj, NULL, dev_attr_irq.attr.name);
	fpc_debug(DEBUG_LOG, "%s exit!\n", __func__);
	return IRQ_HANDLED;
}

static struct of_device_id fpc_of_match[] = {
	{ .compatible = "fpc,fpc1020", },
	{}
};

MODULE_DEVICE_TABLE(of, fpc_of_match);

#if defined(USE_PLATFORM_BUS)
int fpc_plat_probe(struct platform_device *dev)
{
	int ret = sysfs_create_group(&dev->dev.kobj, &fpc_attribute_group);
	fpc_debug(INFO_LOG, "%s enter!\n", __func__);
	if (ret != 0) {
		fpc_debug(ERR_LOG, "%s:sysfs_create_group failed, ret=%d\n", __func__, ret);
		return GENERIC_ERR;
	} else {
		fpc_debug(INFO_LOG, "%s:sysfs_create_group success\n", __func__);
	}

	g_fpc_dev.irq_num = 0;
	g_fpc_dev.irq_gpio = 0;
	g_fpc_dev.rst_gpio = 0;
	g_fpc_dev.pwr_gpio = 0;
	g_fpc_dev.power_type = 0;
	g_fpc_dev.power_voltage = 2800000;
	g_fpc_dev.fp_reg = NULL;

	fpc_debug(INFO_LOG, "%s exit!\n", __func__);
	return GENERIC_OK;
}

int fpc_plat_remove(struct platform_device *dev)
{
	fpc_debug(INFO_LOG, "%s enter!\n", __func__);
	fpc_gpio_exit(&dev->dev);
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 10, 0)
	wakeup_source_unregister(g_fpc_dev.ttw_wl);
#else
	wakeup_source_trash(g_fpc_dev.ttw_wl);
#endif
	sysfs_remove_group(&dev->dev.kobj, &fpc_attribute_group);
	fpc_debug(INFO_LOG, "%s exit!\n", __func__);
	return GENERIC_OK;
}

static struct platform_driver fpc_plat_driver = {
	.driver = {
		.name = FPC_MODULE_NAME,
		.owner = THIS_MODULE,
		.of_match_table = fpc_of_match,
	},
	.probe = fpc_plat_probe,
	.remove = fpc_plat_remove,
};
#elif defined(USE_SPI_BUS)
int fpc_spi_probe(struct spi_device *dev)
{
	int ret = sysfs_create_group(&dev->dev.kobj, &fpc_attribute_group);
	fpc_debug(INFO_LOG, "%s enter!\n", __func__);
	if (ret != 0) {
		fpc_debug(ERR_LOG, "%s:sysfs_create_group failed, ret=%d\n", __func__, ret);
		return GENERIC_ERR;
	} else {
		fpc_debug(INFO_LOG, "%s:sysfs_create_group success\n", __func__);
	}
	g_fpc_dev.spidev = dev;
	fpc_debug(INFO_LOG, "%s exit!\n", __func__);
	return fpc_driver_init(&dev->dev);
}

int fpc_spi_remove(struct spi_device *dev)
{
	fpc_debug(INFO_LOG, "%s enter!\n", __func__);
	fpc_gpio_exit(&dev->dev);
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 10, 0)
	wakeup_source_unregister(g_fpc_dev.ttw_wl);
#else
	wakeup_source_trash(g_fpc_dev.ttw_wl);
#endif
	sysfs_remove_group(&dev->dev.kobj, &fpc_attribute_group);
	return GENERIC_OK;
}

static struct spi_driver fpc_spi_driver = {
	.driver = {
		.name = FPC_MODULE_NAME,
		.owner = THIS_MODULE,
		.bus = &spi_bus_type,
		.of_match_table = fpc_of_match,
	},
	.probe = fpc_spi_probe,
	.remove = fpc_spi_remove,
};
#endif

int fpc_init(void)
{
	fpc_debug(INFO_LOG, "%s enter! driver version:%s\n", __func__, FPC_DRIVER_VERSION);
#if defined(USE_PLATFORM_BUS)
	fpc_debug(INFO_LOG, "%s:platform_driver_register", __func__);
	return platform_driver_register(&fpc_plat_driver);
#elif defined(USE_SPI_BUS)
	fpc_debug(INFO_LOG, "%s:spi_register_driver", __func__);
	return spi_register_driver(&fpc_spi_driver);
#endif
}

void fpc_exit(void)
{
	fpc_debug(INFO_LOG, "%s enter!\n", __func__);
#if defined(USE_PLATFORM_BUS)
	fpc_debug(INFO_LOG, "%s:platform_driver_unregister", __func__);
	platform_driver_unregister(&fpc_plat_driver);
#elif defined(USE_SPI_BUS)
	fpc_debug(INFO_LOG, "%s:spi_unregister_driver", __func__);
	spi_unregister_driver(&fpc_spi_driver);
#endif
}

/*module_init(fpc_init);
module_exit(fpc_exit);*/

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("sheldon <sheldon.xie@fingerprints.com>");
MODULE_DESCRIPTION("fpc fingerprint sensor device driver");