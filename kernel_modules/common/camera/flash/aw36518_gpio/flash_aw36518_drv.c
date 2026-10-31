// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022 Awinic Communications Inc.
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
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/mutex.h>
#include <linux/delay.h>

#include "sprd_img.h"
#include "flash_drv.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "AW36518: %d %d %s : " fmt, current->pid, __LINE__, __func__

#define FLASH_IC_DRIVER_NAME			"flash-aw36518"
#define AW_I2C_RETRIES					(5)
#define AW_I2C_RETRY_DELAY				(3)
#define AW36518_CHIP_NUM				(36518)

/* define registers */
#define AW36518_REG_CHIPID				(0x00)
#define AW36518_CHIPID					(0x30)
/*REG:0X01*/
#define AW36518_REG_ENABLE				(0x01)
#define AW36518_TX_ENABLE				(1 << 7)
#define AW36518_TX_DISABLE				(0)
#define AW36518_STROBE_EDGE				(1 << 6)
#define AW36518_STROBE_LEVEL			(0)
#define AW36518_STROBE_ENABLE			(1 << 5)
#define AW36518_STROBE_DISABLE			(0)
#define AW36518_TORCH_ENABLE			(1 << 4)
#define AW36518_TORCH_DISBLE			(0x00)

#define AW36518_LED_MASK				(0xF0)
#define AW36518_LED_SWITCH_MASK			(0xFC)

#define AW36518_DISABLE					(0x00)
#define AW36518_ENABLE_LED				(3 << 0)
#define AW36518_ENABLE_TORCH			(2 << 2)
#define AW36518_ENABLE_FLASH			(3 << 2)
#define AW36518_CHIP_STANDBY			(0x00)
/*REG:0X02*/
#define AW36518_REG_IVFM				(0x02)
#define AW36518_IVFM_ENABLE				(~(1 << 0))
#define AW36518_IVFM_DISABLE			(1 << 0)
/*REG:0X03*/
#define AW36518_REG_FLASH_LEVEL_LED1	(0x03)
/*REG:0X05*/
#define AW36518_REG_TORCH_LEVEL_LED1	(0x05)
/*REG:0X07*/
#define AW36518_REG_BOOST_CFG			(0x07)
#define AW36518_BOOST_CFG_SET			(0x09)
#define AW36518_BIT_SOFT_RST_MASK		(~(1<<7))
#define AW36518_BIT_SOFT_RST_ENABLE		(1<<7)
/*REG:0X08*/
#define AW36518_REG_TIMING_CONF			(0x08)
#define AW36518_TORCH_RAMP_TIME			(0x10)
#define AW36518_FLASH_TIMEOUT			(0x0E)
/*REG:0X09*/
#define AW36518_REG_DUMMY				(0x09)
#define AW36518_REG_DUMMY_FOR_A			(0x01)
/*REG:0X0A*/
#define AW36518_REG_FLAG1				(0X0A)
/*REG:0X0B*/
#define AW36518_REG_FLAG2				(0X0B)
/*REG:0X0C*/
#define AW36518_REG_DEVICE_ID			(0X0C)
#define AW36518_DEVICE_ID				(0x0A)
/*REG:0X0D*/
#define AW36518_REG_LAST_FLASH			(0X0D)
/*REG:0X31*/
#define AW36518_REG_CTRL1				(0x31)
#define AW36518_REG_CTRL1_SET_VALUE		(0x0C)

/*REG:0X69*/
#define AW36518_REG_CTRL2				(0x69)
#define AW36518_REG_CTRL2_SET_VALUE		(0x02)
/*REG:0X25*/
#define AW36518_REG_CHIP_VENDOR_ID		(0x25)
/*REG:0X04*/
#define AW36518_CHIP_VENDOR_ID			(0x04)
#define AW36518_CHIP_VENDOR_ID_MASK		(0xFB)

#define AW36518_REG_MAX					(0x70)
#define REG_NONE_ACCESS					(0)
#define REG_RD_ACCESS					(1 << 0)
#define REG_WR_ACCESS					(1 << 1)

static const unsigned char aw36518_reg_access[AW36518_REG_MAX] = {
	[AW36518_REG_CHIPID]			= (REG_RD_ACCESS),
	[AW36518_REG_ENABLE]			= (REG_RD_ACCESS | REG_WR_ACCESS),
	[AW36518_REG_IVFM]				= (REG_RD_ACCESS | REG_WR_ACCESS),
	[AW36518_REG_FLASH_LEVEL_LED1]	= (REG_RD_ACCESS | REG_WR_ACCESS),
	[AW36518_REG_TORCH_LEVEL_LED1]	= (REG_RD_ACCESS | REG_WR_ACCESS),
	[AW36518_REG_BOOST_CFG]			= (REG_RD_ACCESS | REG_WR_ACCESS),
	[AW36518_REG_TIMING_CONF]		= (REG_RD_ACCESS | REG_WR_ACCESS),
	[AW36518_REG_FLAG1]				= (REG_RD_ACCESS),
	[AW36518_REG_FLAG2]				= (REG_RD_ACCESS),
	[AW36518_REG_DEVICE_ID]			= (REG_RD_ACCESS),
	[AW36518_REG_LAST_FLASH]		= (REG_RD_ACCESS),
	[AW36518_REG_CHIP_VENDOR_ID]	= (REG_RD_ACCESS),
	[AW36518_REG_CTRL1]				= (REG_RD_ACCESS | REG_WR_ACCESS),
	[AW36518_REG_CTRL2]				= (REG_RD_ACCESS | REG_WR_ACCESS),
};

#define AW36518_DRIVER_VERSION	"V1.0.0"

struct flash_ic_cfg {
	unsigned int lvfm_enable;
	unsigned int torch_level;
	unsigned int preflash_level;
	unsigned int highlight_level;
	unsigned int torch_max_level;
	unsigned int flash_max_level;
};

struct flash_driver_data {
	struct i2c_client *i2c_info;
	struct mutex i2c_lock;
	struct device *dev;
	void *priv;
	struct flash_ic_cfg flash_cfg;
	u32 torch_led_index;
	bool mode_switch_init;
};

static int aw36518_i2c_write(struct i2c_client *client, unsigned char reg,
								   unsigned char val)
{
	int ret;
	unsigned char cnt = 0;

	while (cnt < AW_I2C_RETRIES) {
		ret = i2c_smbus_write_byte_data(client, reg, val);
		if (ret < 0) {
			pr_info("%s: i2c_write addr=0x%02X, data=0x%02X, cnt=%d, error=%d\n",
				   __func__, reg, val, cnt, ret);
		} else {
			break;
		}
		cnt++;
		msleep(AW_I2C_RETRY_DELAY);
	}

	return ret;
}

static int aw36518_i2c_read(struct i2c_client *client, unsigned char reg,
								 unsigned char *val)
{
	int ret;
	unsigned char cnt = 0;

	while (cnt < AW_I2C_RETRIES) {
		ret = i2c_smbus_read_byte_data(client, reg);
		if (ret < 0) {
			pr_info("%s: i2c_read addr=0x%02X, cnt=%d, error=%d\n",
				   __func__, reg, cnt, ret);
		} else {
			*val = ret;
			break;
		}
		cnt++;
		msleep(AW_I2C_RETRY_DELAY);
	}

	return ret;
}
static void aw36518_update_bits(struct i2c_client *client,
							unsigned char reg,
							unsigned int mask,
							unsigned char reg_data)
{
	unsigned char reg_val;

	aw36518_i2c_read(client, reg, &reg_val);
	reg_val &= mask;
	reg_val |= (reg_data & (~mask));
	aw36518_i2c_write(client, reg, reg_val);
}

static void aw36518_mode_cfg_init(struct flash_driver_data *drv_data)
{
	struct flash_driver_data *pdata = drv_data;

	if (pdata->mode_switch_init) {
		aw36518_i2c_write(pdata->i2c_info, AW36518_REG_CTRL2,
							AW36518_REG_CTRL2_SET_VALUE);
		aw36518_i2c_write(pdata->i2c_info, AW36518_REG_CTRL1,
							AW36518_REG_CTRL1_SET_VALUE);
	}
}

static int aw36518_flash_ic_open_torch(void *drvd, uint8_t idx)
{
	struct flash_driver_data *drv_data = (struct flash_driver_data *)drvd;
	unsigned char val = 0;

	if (!drv_data)
		return -EFAULT;
	pr_info("%s, torch_led_index:%d, idx:%d\n", __func__,
												drv_data->torch_led_index, idx);
	idx = drv_data->torch_led_index;

	if (drv_data->i2c_info == NULL) {
		pr_err("%s-i2c_info is NULL...\n");
		return -EINVAL;
	}

	val = AW36518_ENABLE_TORCH | (idx & AW36518_ENABLE_LED);
	aw36518_update_bits(drv_data->i2c_info,
						AW36518_REG_ENABLE,
						AW36518_LED_MASK, val);

	return 0;
}

static int aw36518_flash_ic_close_torch(void *drvd, uint8_t idx)
{
	struct flash_driver_data *drv_data = (struct flash_driver_data *)drvd;
	unsigned char val = 0;

	if (!drv_data)
		return -EFAULT;
	pr_info("%s, torch_led_index:%d, idx:%d\n", __func__,
												drv_data->torch_led_index, idx);
	idx = drv_data->torch_led_index;
	if (drv_data->i2c_info == NULL) {
		pr_err("%s-i2c_info is NULL...\n");
		return -EINVAL;
	}

	val = AW36518_DISABLE;
	aw36518_update_bits(drv_data->i2c_info,
						AW36518_REG_ENABLE,
						AW36518_LED_SWITCH_MASK, val);

	return 0;
}

static int aw36518_flash_ic_open_preflash(void *drvd, uint8_t idx)
{
	struct flash_driver_data *drv_data = (struct flash_driver_data *)drvd;
	unsigned char val = 0;

	if (!drv_data)
		return -EFAULT;

	pr_info("%s, torch_led_index:%d, idx:%d\n", __func__,
												drv_data->torch_led_index, idx);

	if (drv_data->i2c_info == NULL) {
		pr_err("%s-i2c_info is NULL...\n");
		return -EINVAL;
	}

	val = AW36518_ENABLE_FLASH | (idx & AW36518_ENABLE_LED);
	aw36518_update_bits(drv_data->i2c_info,
						AW36518_REG_ENABLE,
						AW36518_LED_MASK, val);

	return 0;
}

static int aw36518_flash_ic_close_preflash(void *drvd, uint8_t idx)
{
	struct flash_driver_data *drv_data = (struct flash_driver_data *)drvd;
	int ret = 0;
	unsigned char val = 0;

	if (!drv_data)
		return -EFAULT;

	pr_info("%s, torch_led_index:%d, idx:%d\n", __func__,
				drv_data->torch_led_index, idx);
	if (drv_data->i2c_info == NULL) {
		pr_err("%s-i2c_info is NULL...\n");
		return -EINVAL;
	}

	val = AW36518_DISABLE;
	aw36518_update_bits(drv_data->i2c_info,
						AW36518_REG_ENABLE,
						AW36518_LED_SWITCH_MASK, val);
	return ret;
}

static int aw36518_flash_ic_open_highlight(void *drvd, uint8_t idx)
{
	int ret = 0;
	struct flash_driver_data *drv_data = (struct flash_driver_data *)drvd;
	unsigned char val = 0;

	if (!drv_data)
		return -EFAULT;

	pr_info("%s, torch_led_index:%d, idx:%d\n", __func__,
												drv_data->torch_led_index, idx);

	if (drv_data->i2c_info == NULL) {
		pr_err("%s-i2c_info is NULL...\n");
		return -EINVAL;
	}

	val = AW36518_ENABLE_FLASH | (idx & AW36518_ENABLE_LED);
	aw36518_update_bits(drv_data->i2c_info,
						AW36518_REG_ENABLE,
						AW36518_LED_MASK, val);

	return ret;
}

static int aw36518_flash_ic_close_highlight(void *drvd, uint8_t idx)
{
	int ret = 0;
	struct flash_driver_data *drv_data = (struct flash_driver_data *)drvd;
	unsigned char val = 0;

	if (!drv_data)
		return -EFAULT;

	pr_info("%s, torch_led_index:%d, idx:%d\n", __func__,
												drv_data->torch_led_index, idx);

	if (drv_data->i2c_info == NULL) {
		pr_err("%s-i2c_info is NULL...\n");
		return -EINVAL;
	}

	val = AW36518_DISABLE;
	aw36518_update_bits(drv_data->i2c_info,
						AW36518_REG_ENABLE,
						AW36518_LED_SWITCH_MASK, val);
	return ret;
}

static int aw36518_flash_ic_cfg_value_torch(void *drvd, uint8_t idx,
					  struct sprd_flash_element *element)
{
	struct flash_driver_data *drv_data = (struct flash_driver_data *)drvd;
	int val = 0;

	if (!drv_data)
		return -EFAULT;

	pr_info("%s, torch_led_index:%d, element->index %d, idx:%d\n",
			__func__, drv_data->torch_led_index, element->index, idx);

	aw36518_mode_cfg_init(drv_data);

	val = element->index;
	if (drv_data->i2c_info) {
		aw36518_i2c_write(drv_data->i2c_info,
					AW36518_REG_TORCH_LEVEL_LED1,
					(u8)val);
	}

	return 0;
}

static int aw36518_flash_ic_cfg_value_preflash(void *drvd, uint8_t idx,
					  struct sprd_flash_element *element)
{
	struct flash_driver_data *drv_data = (struct flash_driver_data *)drvd;
	int val = 0;

	if (!drv_data)
		return -EFAULT;

	pr_info("%s, element->index %d, idx:%d\n", __func__, element->index, idx);

	aw36518_mode_cfg_init(drv_data);

	val = element->index;
	if (drv_data->i2c_info) {
		aw36518_i2c_write(drv_data->i2c_info,
					AW36518_REG_FLASH_LEVEL_LED1,
					(u8)val);
	}

	return 0;
}

static int aw36518_flash_ic_cfg_value_highlight(void *drvd, uint8_t idx,
					   struct sprd_flash_element *element)
{
	struct flash_driver_data *drv_data = (struct flash_driver_data *)drvd;
	int val = 0;

	if (!drv_data)
		return -EFAULT;

	pr_info("%s, element->index %d, idx:%d\n", __func__, element->index, idx);

	aw36518_mode_cfg_init(drv_data);

	val = element->index;
	if (drv_data->i2c_info) {
		aw36518_i2c_write(drv_data->i2c_info,
					AW36518_REG_FLASH_LEVEL_LED1,
					(u8)val);
	}

	return 0;
}

static const struct of_device_id aw36518_flash_ic_of_match_table[] = {
	{.compatible = "sprd,flash-aw36518_gpio"},
};

static const struct i2c_device_id sprd_flash_ic_ids[] = {
	{FLASH_IC_DRIVER_NAME, 0},
	{}
};
static const struct sprd_flash_driver_ops aw36518_flash_ic_ops = {
	.open_torch = aw36518_flash_ic_open_torch,
	.close_torch = aw36518_flash_ic_close_torch,
	.open_preflash = aw36518_flash_ic_open_preflash,
	.close_preflash = aw36518_flash_ic_close_preflash,
	.open_highlight = aw36518_flash_ic_open_highlight,
	.close_highlight = aw36518_flash_ic_close_highlight,
	.cfg_value_torch = aw36518_flash_ic_cfg_value_torch,
	.cfg_value_preflash = aw36518_flash_ic_cfg_value_preflash,
	.cfg_value_highlight = aw36518_flash_ic_cfg_value_highlight,

};
static int aw36518_read_chip_id(struct flash_driver_data *drv_data)
{
	int ret = 0;
	unsigned char val = 0;

	pr_info("enter\n");

	if (drv_data == NULL)
		return -EFAULT;

	ret = aw36518_i2c_read(drv_data->i2c_info, AW36518_REG_CHIPID, &val);
	if (ret < 0) {
		pr_err("%s error, ret = %d\n", ret);
		return ret;
	}
	pr_info("%s, chipID = %d\n", __func__, val);
	return 0;
}

static int aw36518_read_vendor_id(struct flash_driver_data *drv_data)
{
	unsigned char val, reg_data;

	if (drv_data == NULL)
		return -EFAULT;
	/* read chip vendor information */
	aw36518_i2c_read(drv_data->i2c_info, AW36518_REG_CHIP_VENDOR_ID, &val);
	pr_info("read %#x register vendorID = %#x\n", AW36518_REG_CHIP_VENDOR_ID, val);

	aw36518_i2c_read(drv_data->i2c_info, AW36518_REG_DUMMY, &reg_data);
	pr_info("read %#x register ver = %#x\n", reg_data);

	if (((val & ~AW36518_CHIP_VENDOR_ID_MASK) == AW36518_CHIP_VENDOR_ID) &&
		!(reg_data & AW36518_REG_DUMMY_FOR_A)) {
		pr_info("aw36518a mode_switch_init is true\n");
		drv_data->mode_switch_init = true;
	} else {
		drv_data->mode_switch_init = false;
		pr_info("aw36518 mode_switch_init is false\n");
	}

	return 0;
}
static int aw36518_flash_ic_init(void *drvd)
{
	int ret = 0;
	unsigned char val = 0;
	struct flash_driver_data *drv_data = (struct flash_driver_data *)drvd;

	if (!drv_data)
		return -EFAULT;

	if (drv_data->i2c_info) {
		aw36518_mode_cfg_init(drv_data);
		val = AW36518_TX_ENABLE | AW36518_STROBE_LEVEL |
				AW36518_STROBE_ENABLE | AW36518_TORCH_ENABLE;
		ret = aw36518_i2c_write(drv_data->i2c_info, AW36518_REG_ENABLE, val);

		val = AW36518_TORCH_RAMP_TIME | AW36518_FLASH_TIMEOUT;
		ret = aw36518_i2c_write(drv_data->i2c_info, AW36518_REG_TIMING_CONF, val);

		val = AW36518_BOOST_CFG_SET;
		ret = aw36518_i2c_write(drv_data->i2c_info, AW36518_REG_BOOST_CFG, val);
		pr_info("flash ic init ret = %d\n", ret);
	}

	return ret;
}

static void aw36518_flash_ic_reset(struct flash_driver_data *drvd)
{
	u8 val = 0;
	struct flash_driver_data *drv_data = drvd;

	if (drv_data->i2c_info) {
		val = AW36518_BIT_SOFT_RST_ENABLE;
		aw36518_update_bits(drv_data->i2c_info,
							AW36518_REG_BOOST_CFG,
							AW36518_BIT_SOFT_RST_MASK, val);
	}
	usleep_range(3000, 4000);
}

/**********************	attributes	****************************/
static ssize_t reg_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct flash_driver_data *drv_data = dev_get_drvdata(dev);
	unsigned char i = 0, reg_val = 0;
	ssize_t len = 0;
	int ret = 0;

	if (drv_data == NULL) {
		pr_info("drv_data is null...\n");
		return len;
	}
	for (i = 0; i < AW36518_REG_MAX; i++) {
		if (!(aw36518_reg_access[i] & REG_RD_ACCESS))
			continue;
		ret = aw36518_i2c_read(drv_data->i2c_info, i, &reg_val);
		if (ret < 0) {
			pr_info("i2c read failed,ret = %d\n", ret);
			break;
		}
		len += snprintf(buf+len, PAGE_SIZE-len, "reg:0x%02x=0x%02x\n",
						i, reg_val);
	}

	return len;
}

static ssize_t reg_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct flash_driver_data *drv_data = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	if (drv_data == NULL) {
		pr_info("drv_data is null...\n");
		return count;
	}
	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= AW36518_REG_MAX) {
		pr_info("reg = %#x.val = %#x\n", reg, val);
		aw36518_i2c_write(drv_data->i2c_info, (unsigned char)reg, (unsigned char)val);
	}

	return count;
}
static ssize_t reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct flash_driver_data *drv_data = dev_get_drvdata(dev);
	unsigned int val;

	if (drv_data == NULL) {
		pr_info("drv_data is null...\n");
		return count;
	}

	if (kstrtouint(buf, 16, &val) == 1) {
		pr_info("val = %#x.\n", val);
		if (val == 1) {
			aw36518_flash_ic_reset(drv_data);
			pr_info("reset OK\n");
		} else {
			pr_info("store parameter error.\n");
		}
	}

	return count;
}

static DEVICE_ATTR_RW(reg);
static DEVICE_ATTR_WO(reset);

static struct attribute *aw36518_attributes[] = {
	&dev_attr_reg.attr,
	&dev_attr_reset.attr,
	NULL,
};

static const struct attribute_group aw36518_attr_group = {
	.attrs = aw36518_attributes,
};

static int aw36518_flash_driver_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	int ret = 0;
	struct device *dev = &client->dev;
	struct flash_driver_data *pdata = NULL;
	unsigned int chip_num;

	pr_info("enter\n");

	if (!dev->of_node) {
		pr_err("%s, no device node\n", __func__);
		return -ENODEV;
	}

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	pdata->dev = &client->dev;
	client->dev.platform_data = (void *)pdata;
	pdata->i2c_info = client;
	mutex_init(&pdata->i2c_lock);

	ret = aw36518_read_chip_id(pdata);
	if (ret < 0)
		goto exit;
	aw36518_read_vendor_id(pdata);
	aw36518_flash_ic_reset(pdata);

	ret = of_property_read_u32(dev->of_node, "sprd,flash-ic", &chip_num);
	pr_info("sprd,flash-ic = %d\n", chip_num);
	if (ret || chip_num != AW36518_CHIP_NUM) {
		pr_err("flash-ic not compatiable ic = %d\n", chip_num);
		return -ENODEV;
	}

	ret = of_property_read_u32(dev->of_node,
			"sprd,touch-max-level", &pdata->flash_cfg.torch_max_level);
	if (ret)
		pr_info("torch-max-level no cfg\n");

	ret = of_property_read_u32(dev->of_node,
			"sprd,flash-max-level", &pdata->flash_cfg.flash_max_level);
	if (ret)
		pr_info("flash-max-level no cfg\n");

	ret = of_property_read_u32(dev->of_node,
			"sprd,torch-level", &pdata->flash_cfg.torch_level);
	if (ret)
		pr_info("torch-level no cfg\n");

	ret = of_property_read_u32(dev->of_node,
		"sprd,preflash-level", &pdata->flash_cfg.preflash_level);
	if (ret)
		pr_info("preflash-level no cfg\n");

	ret = of_property_read_u32(dev->of_node,
		"sprd,highlight-level", &pdata->flash_cfg.highlight_level);
	if (ret)
		pr_info("highlight-level no cfg\n");
	ret = of_property_read_u32(dev->of_node,
			 "sprd,lvfm-enable", &pdata->flash_cfg.lvfm_enable);
	if (ret)
		pr_info("lvfm-enable no cfg\n");
	ret = of_property_read_u32(dev->of_node,
				"torch-led-idx", &pdata->torch_led_index);
	if (ret)
		pdata->torch_led_index = SPRD_FLASH_LED0;

	ret = sysfs_create_group(&dev->kobj, &aw36518_attr_group);
	if (ret) {
		pr_err("failed to register sysfs. err: %d\n", ret);
		goto exit;
	}

	i2c_set_clientdata(client, pdata);

	ret = aw36518_flash_ic_init(pdata);

	ret = sprd_flash_register(&aw36518_flash_ic_ops, pdata, SPRD_FLASH_REAR);

exit:
	pr_info("exit\n");
	return ret;

}
static int aw36518_flash_driver_remove(struct i2c_client *client)
{
	struct flash_driver_data *pdata = NULL;

	pdata = (struct flash_driver_data *)client->dev.platform_data;

	if (pdata == NULL)
		return -1;

	sysfs_remove_group(&pdata->i2c_info->dev.kobj, &aw36518_attr_group);

	if (pdata)
		devm_kfree(&client->dev, pdata);

	pdata = NULL;
	client->dev.platform_data = NULL;

	return 0;
}

static struct i2c_driver aw36518_flash_ic_driver = {
	.driver = {
		.of_match_table = of_match_ptr(aw36518_flash_ic_of_match_table),
		.name = FLASH_IC_DRIVER_NAME,
		},
	.probe = aw36518_flash_driver_probe,
	.remove = aw36518_flash_driver_remove,
	.id_table = sprd_flash_ic_ids,
};

static int aw36518_flash_register_driver(void)
{
	int ret = 0;

	pr_info("driver version: %s\n", AW36518_DRIVER_VERSION);
	ret = i2c_add_driver(&aw36518_flash_ic_driver);

	return ret;
}

static void aw36518_flash_unregister_driver(void)
{
	i2c_del_driver(&aw36518_flash_ic_driver);
}

static int flashlight_aw36518_init(void)
{
	int ret = 0;

	ret = aw36518_flash_register_driver();
	return ret;
}

static void flashlight_aw36518_deinit(void)
{
	aw36518_flash_unregister_driver();
}

module_init(flashlight_aw36518_init);
module_exit(flashlight_aw36518_deinit);
MODULE_DESCRIPTION("Awinic Aw36518 Flash Driver");
MODULE_LICENSE("GPL");

