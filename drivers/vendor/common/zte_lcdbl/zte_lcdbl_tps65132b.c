// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Inc.
 */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/slab.h>


/*zte_lcd_tps65132b_err:  -1,error;  0,ok; 1,no_write*/
static int zte_lcd_tps65132b_err = 1;
static int zte_lcd_tps65132b_enhance_value = 0;
static int zte_lcd_tps65132b_enhance_enable = 0;


struct i2c_client *i2c_tps65132b_client = NULL;
void tps65132b_set_vsp_vsn_level(u8 level);
/*bool ti65132b_true = false;*/
bool ti65132b_probe = false;

#define LCM_VSP_VSN_55V  0x0F
#define LCM_VSP_VSN_50V  0x0a
#define ZTE_LCD_VPOS_ADDRESS		0x00
#define ZTE_LCD_VNEG_ADDRESS		0x01

/*
static int tps65132b_read_reg(struct i2c_client *client, u8 *buf, u8 *data)
{
	struct i2c_msg msgs[2];
	int ret;
	u8 retries = 0;

	msgs[0].flags = !I2C_M_RD;
	msgs[0].addr  = client->addr;
	msgs[0].len   = 1;
	msgs[0].buf   = &buf[0];

	msgs[1].flags = I2C_M_RD;
	msgs[1].addr  = client->addr;
	msgs[1].len   = 1;
	msgs[1].buf   = data;

	while (retries < 3) {
		ret = i2c_transfer(client->adapter, msgs, 2);
		if (ret == 2)
			break;
		retries++;
		msleep_interruptible(5);
	}
	pr_info("tps65132b_read_reg retries=%d,ret=%d\n", retries, ret);
	if (ret != 2) {
		pr_err("tps65132b read transfer error\n");
		ret = -1;
	}

	return ret;
}*/

/*extern uint32_t zte_hw_boardid;*/
static int tps65132b_write_reg(struct i2c_client *client, u8 *buf, int len)
{
	int err;
	int tries = 0;

	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = len + 1,
			.buf = buf,
		},
	};

	do {
		err = i2c_transfer(client->adapter, msgs, 1);
		if (err != 1)
			msleep_interruptible(5);
	} while ((err != 1) && (++tries < 3));

	pr_info("%s tries=%d,ret=%d\n", __func__, tries, err);
	if (err != 1) {
		pr_err("tps65132b write transfer error\n");
		zte_lcd_tps65132b_err = -1;
		err = -1;
	} else {
		zte_lcd_tps65132b_err = 0;
	}

	return err;
}

/*
REGFFH:
0x0A 0x0B 0x0C 0x0D 0x0E 0x0F 0x10 0x11 0x12 0x13 0x14
5.00 5.10 5.20 5.30 5.40 5.50 5.60 5.70 5.80 5.90 6.00
*/
void tps65132b_set_vsp_vsn_level(u8 level)
{
	u8 buf_vsp[2] = {0x00, 0x00};
	u8 buf_vsn[2] = {0x01, 0x00};
#ifdef CONFIG_ZTE_LCDBL_I2C_CTRL_VSP_VSN_ENHANCE
	u8 buf_apps[2] = {0x03, 0x43};
#endif

	/*pr_info("tps65132b probe=%d\n", ti65132b_probe);*/
	if (ti65132b_probe) {
		/*err = tps65132b_read_reg(i2c_tps65132b_client, &addrvsn, &datavsn);
		if (err) {
			pr_info("tps65132b read fail\n");
			ti65132b_true = false;
			return;
		} else {
			pr_info("tps65132b read data=%x\n", datavsn);
			ti65132b_true = true;
		}*/
		buf_vsp[1] = level;
		tps65132b_write_reg(i2c_tps65132b_client, buf_vsp, 1);
		buf_vsn[1] = level;
		tps65132b_write_reg(i2c_tps65132b_client, buf_vsn, 1);
#ifdef CONFIG_ZTE_LCDBL_I2C_CTRL_VSP_VSN_ENHANCE
	if (!zte_lcd_tps65132b_enhance_enable) {
		pr_info("tps65132b zte_lcd_tps65132b_enhance_enable =%d\n", zte_lcd_tps65132b_enhance_enable);
	} else {
		if (1/*((int)zte_hw_boardid) > (zte_lcd_tps65132b_enhance_value)*/) {
			pr_info("%s: set enhance value as 80ma\n", __func__);
			tps65132b_write_reg(i2c_tps65132b_client, buf_apps, 1);
		}
	}
#endif
		usleep_range(5000, 5100);
	}
}
EXPORT_SYMBOL(tps65132b_set_vsp_vsn_level);

static ssize_t tps65132b_show(struct device *d, struct device_attribute *attr, char *buf)
{
	if (zte_lcd_tps65132b_err == 1)
		return snprintf(buf, 80, "%s\n", "NULL");
	else if (zte_lcd_tps65132b_err == 0)
		return snprintf(buf, 80, "%s\n", "ok");
	else
		return snprintf(buf, 80, "%s\n", "error");
}
static ssize_t tps65132b_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{

	tps65132b_set_vsp_vsn_level(0x14);

	return count;
}
static DEVICE_ATTR_RW(tps65132b);

static struct attribute *zte_lcd_tps65132b_attrs[] = {
	&dev_attr_tps65132b.attr,
	NULL,
};
static struct attribute_group zte_tps65132b_attrs_group = {
	.attrs = zte_lcd_tps65132b_attrs,
};

static int tps65132b_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	int ret = 0;

	pr_info("zte tps65132b probe version 20210525_v003 addr=0x%x\n", client->addr);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("tps65132b probe,client not i2c capable\n");
		return -EIO;
	}
	i2c_tps65132b_client = client;

	zte_lcd_tps65132b_enhance_enable = of_property_read_bool(client->dev.of_node,
		"zte,lcd-bl-vsp-vsn-enhance-enable");
	pr_info("zte tps65132b probe enhance_enable=%d\n", zte_lcd_tps65132b_enhance_enable);


	ret = of_property_read_u32(client->dev.of_node, "zte,tps65132b_enhance_value",
		&zte_lcd_tps65132b_enhance_value);
	if (ret) {
		zte_lcd_tps65132b_enhance_value  = 0;/*default value*/
	}
	/*if set tps65132b_enhance_value,means bigger than this board_id will enhance*/
	if (zte_lcd_tps65132b_enhance_value == 0xffff)
		zte_lcd_tps65132b_enhance_value  = -1;

	ti65132b_probe = true;

	ret = sysfs_create_group(&client->dev.kobj, &zte_tps65132b_attrs_group);
	if (ret) {
		pr_err("tps65132b sysfs creation failed, ret=%d\n", ret);
		return ret;
	}

	/*tps65132b_set_vsp_vsn_level(0xf);*/
	pr_info("zte ti65132b probe ok,zte_lcd_tps65132b_enhance_value=%d\n",
		zte_lcd_tps65132b_enhance_value);
	return 0;
}

static int tps65132b_remove(struct i2c_client *client)
{
	kfree(i2c_get_clientdata(client));
	return 0;
}

static const struct i2c_device_id tps65132b_id_table[] = {
	{"ti65132b", 0},
	{ },
};
MODULE_DEVICE_TABLE(i2c, tps65132b_id_table);

static const struct of_device_id tps65132b_of_id_table[] = {
	{.compatible = "tps,ti65132b"},
	{ },
};

static struct i2c_driver tps65132b_i2c_driver = {
	.driver = {
		.name = "ti65132b",
		.owner = THIS_MODULE,
		.of_match_table = tps65132b_of_id_table,
	},
	.probe = tps65132b_probe,
	.remove = tps65132b_remove,
	.id_table = tps65132b_id_table,
};

module_i2c_driver(tps65132b_i2c_driver);

MODULE_DESCRIPTION("tps65132b chip driver");
MODULE_LICENSE("GPL");

