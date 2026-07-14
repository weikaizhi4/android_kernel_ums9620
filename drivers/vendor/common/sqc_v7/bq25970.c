/*
 * Copyright (C) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */
#define pr_fmt(fmt)	"%s " fmt,  __func__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/notifier.h>
#include <linux/mutex.h>
#include <linux/raid/pq.h>
#include <linux/proc_fs.h>
#include <sqc_common.h>
#include <vendor/common/zte_misc.h>

#include "bq25970.h"
#include "sqc_netlink.h"
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
#include "zlog_common.h"
#endif

static void bq25970_dump_register(struct bq25970_device *chip, char *out_reg, int len);

static int tsbus_high_r_kohm = BQ2597X_RESISTORS_100KOHM;
static int tsbus_low_r_kohm = BQ2597X_RESISTORS_100KOHM;
static int switching_frequency = BQ2597X_SW_FREQ_550KHZ;
static int ibat_sns_resistor = 5;
static int cp1_init_finished = 0;
static int cp2_init_finished = 0;

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
static int cp_dump(void *data, int type)
{
       struct zlog_client *client = (struct zlog_client *)data;
       struct bq25970_device *chip = (struct bq25970_device *)client->private_data;
       char reg_val[BQ2597X_DEGLITCH_REG + 1] = {0, };

       pr_info("%s %d, called, type %d\n", __func__, __LINE__, type);

       bq25970_dump_register(chip, reg_val, sizeof(reg_val));

       zlog_client_notify(client, ZLOG_CHG_DUMP_NO);

       pr_info("%s %d, called, type %d\n", __func__, __LINE__, type);

       return 0;
}

static struct zlog_client_ops cp_ops = {
       .poll_state = NULL,
       .dump_func = cp_dump,
};

static struct zlog_mod_info zlog_cp_dev1 = {
	.module_no = ZLOG_MODULE_CHG,
	.name = "cp_dev1",
	.device_name = "CP2:1",
	.ic_name = "bq25970",
	.module_name = "TI",
	.fops = &cp_ops,
};

static struct zlog_mod_info zlog_cp_dev2 = {
	.module_no = ZLOG_MODULE_CHG,
	.name = "cp_dev2",
	.device_name = "CP2:1",
	.ic_name = "bq25970",
	.module_name = "TI",
	.fops = &cp_ops,
};
#endif

#define MSG_LEN                      (2)


#define bq_err(fmt, ...)								\
do {											\
	if (chip->hw_mode == BQ25970_MASTER)						\
		pr_err("[bq2597x-MASTER]:%s:" fmt, __func__, ##__VA_ARGS__);	\
	else if (chip->hw_mode == BQ25970_SLAVE)					\
		pr_err("[bq2597x-SLAVE]:%s:" fmt, __func__, ##__VA_ARGS__);	\
	else										\
		pr_err("[bq2597x-STANDALONE]:%s:" fmt, __func__, ##__VA_ARGS__);\
} while (0);

#define bq_info(fmt, ...)								\
do {											\
	if (chip->hw_mode == BQ25970_MASTER)						\
		pr_info("[bq2597x-MASTER]:%s:" fmt, __func__, ##__VA_ARGS__);	\
	else if (chip->hw_mode == BQ25970_SLAVE)					\
		pr_info("[bq2597x-SLAVE]:%s:" fmt, __func__, ##__VA_ARGS__);	\
	else										\
		pr_info("[bq2597x-STANDALONE]:%s:" fmt, __func__, ##__VA_ARGS__);\
} while (0);


#define bq_dbg(fmt, ...)								\
do {											\
	if (chip->hw_mode == BQ25970_MASTER)						\
		pr_debug("[bq2597x-MASTER]:%s:" fmt, __func__, ##__VA_ARGS__);	\
	else if (chip->hw_mode == BQ25970_SLAVE)					\
		pr_debug("[bq2597x-SLAVE]:%s:" fmt, __func__, ##__VA_ARGS__);	\
	else										\
		pr_debug("[bq2597x-STANDALONE]:%s:" fmt, __func__, ##__VA_ARGS__);\
} while (0);


static const struct of_device_id bq25970_of_match[] = {
	{
		.compatible = "bq25970-standalone",
		.data = (void *)BQ25970_STDALONE,
	},
	{
		.compatible = "bq25970-master",
		.data = (void *)BQ25970_MASTER,
	},
	{
		.compatible = "bq25970-slave",
		.data = (void *)BQ25970_SLAVE,
	},
	{},
};

static const struct i2c_device_id bq25970_i2c_id[] = {
	{"bq25970-standalone", BQ25970_STDALONE},
	{"bq25970-master", BQ25970_MASTER},
	{"bq25970-slave", BQ25970_SLAVE},
	{},
};

static bool bq25970_update_status_flag(struct bq25970_device *chip, int flag, int status_code)
{
	int chg_id = SQC_NOTIFY_CHG_CP1;

	if (chip->hw_mode == BQ25970_SLAVE)
		chg_id = SQC_NOTIFY_CHG_CP2;

	if (flag) {
		sqc_notify_daemon_changed(chg_id, status_code, 1);
		bq_info("%s status_code %d set to 1\n", __func__, status_code);
		return true;
	}

	return false;
}

static int bq25970_write_block(struct bq25970_device *chip,
	u8 *value, u8 reg, unsigned int num_bytes)
{
	struct i2c_msg msg[1];
	int ret = 0;

	if (chip == NULL || value == NULL) {
		bq_err("chip is null or value is null\n");
		return -EIO;
	}

	if (chip->chip_already_init == 0) {
		bq_err("chip not init\n");
		return -EIO;
	}

	*value = reg;

	msg[0].addr = chip->client->addr;
	msg[0].flags = 0;
	msg[0].buf = value;
	msg[0].len = num_bytes + 1;

	ret = i2c_transfer(chip->client->adapter, msg, 1);

	/* i2c_transfer returns number of messages transferred */
	if (ret != 1) {
		bq_err("write_block failed[%x]\n", reg);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		zlog_client_record(chip->zlog_client, "i2c write err:%d\n", ret);
		zlog_client_notify(chip->zlog_client, ZLOG_CHG_I2C_W_ERROR_NO);
#endif
		if (ret < 0)
			return ret;
		else
			return -EIO;
	} else {
		return 0;
	}
}

static int bq25970_read_block(struct bq25970_device *chip,
	u8 *value, u8 reg, unsigned int num_bytes)
{
	struct i2c_msg msg[MSG_LEN];
	u8 buf = 0;
	int ret = 0;

	if (chip == NULL || value == NULL) {
		bq_err("chip is null or value is null\n");
		return -EIO;
	}

	if (chip->chip_already_init == 0) {
		bq_err("chip not init\n");
		return -EIO;
	}

	buf = reg;

	msg[0].addr = chip->client->addr;
	msg[0].flags = 0;
	msg[0].buf = &buf;
	msg[0].len = 1;

	msg[1].addr = chip->client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = value;
	msg[1].len = num_bytes;

	ret = i2c_transfer(chip->client->adapter, msg, MSG_LEN);

	/* i2c_transfer returns number of messages transferred */
	if (ret != MSG_LEN) {
		bq_err("read_block failed[%x]\n", reg);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		zlog_client_record(chip->zlog_client, "i2c read err:%d\n", ret);
		zlog_client_notify(chip->zlog_client, ZLOG_CHG_I2C_R_ERROR_NO);
#endif
		if (ret < 0)
			return ret;
		else
			return -EIO;
	} else {
		return 0;
	}
}

static int bq25970_write_byte(struct bq25970_device *chip, u8 reg, u8 value)
{
	 /* 2 bytes offset 1 contains the data offset 0 is used by i2c_write */
	u8 temp_buffer[MSG_LEN] = {0};

	/* offset 1 contains the data */
	temp_buffer[1] = value;
	return bq25970_write_block(chip, temp_buffer, reg, 1);
}

static int bq25970_read_byte(struct bq25970_device *chip, u8 reg, u8 *value)
{
	return bq25970_read_block(chip, value, reg, 1);
}

static int bq25970_write_mask(struct bq25970_device *chip, u8 reg, u8 mask, u8 shift, u8 value)
{
	int ret = 0;
	u8 val = 0;

	ret = bq25970_read_byte(chip, reg, &val);
	if (ret < 0)
		return ret;

	val &= ~mask;
	val |= ((value << shift) & mask);

	ret = bq25970_write_byte(chip, reg, val);

	return ret;
}

static inline void bq25970_print_mem(struct bq25970_device *chip, char *buffer, unsigned int len)
{
	unsigned int i = 0;
	char buf[256] = {0,};

	memset(buf, 0, sizeof(buf));
	snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "BQ25970-0x00: ");

	for (i = 0; i < len; i++) {
		snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "0x%02X ", buffer[i]);

		if ((i != 0) && ((i + 1) % 8 == 0)) {
			snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "	");
		}

		if ((i != 0) && ((i + 1) % 16 == 0)) {
			bq_info("%s\n", buf);

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
			zlog_client_record(chip->zlog_client, "%s\n", buf);
#endif

			memset(buf, 0, sizeof(buf));
			snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "BQ25970-0x%02X: ", (i + 1));
		}
	}

	bq_info("%s\n", buf);

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	zlog_client_record(chip->zlog_client, "%s\n", buf);
#endif
}

static void bq25970_dump_register(struct bq25970_device *chip, char *out_reg, int len)
{
	u8 i = 0;
	int ret = 0;
	char val[BQ2597X_DEGLITCH_REG + 1] = {0, };

	for (i = 0; i < BQ2597X_DEGLITCH_REG; ++i) {
		ret = bq25970_read_byte(chip, i, val + i);
		if (ret)
			bq_err("dump_register read fail\n");
	}

	if (out_reg)
		memcpy(out_reg, val, (sizeof(val) > len) ? len : sizeof(val));

	bq25970_print_mem(chip, val, sizeof(val));
}

static int bq25970_reg_reset(struct bq25970_device *chip)
{
	int ret;
	u8 reg = 0;

	ret = bq25970_write_mask(chip, BQ2597X_CONTROL_REG,
		BQ2597X_REG_RST_MASK, BQ2597X_REG_RST_SHIFT,
		BQ2597X_REG_RST_ENABLE);
	if (ret)
		return -1;

	ret = bq25970_read_byte(chip, BQ2597X_CONTROL_REG, &reg);
	if (ret)
		return -1;

	bq_info("reg_reset [%x]=0x%x\n", BQ2597X_CONTROL_REG, reg);
	return 0;
}


static int bq25970_set_enable_chg(void * arg, unsigned int en)
{
	int ret = 0, en_now = 0;
	u8 reg = 0;
	u8 value = en ? 0x1 : 0x0;
	struct bq25970_device *chip = (struct bq25970_device *)arg;

	/*check is already enable?*/
	ret = bq25970_read_byte(chip, BQ2597X_CHRG_CTL_REG, &reg);
	if (ret) {
		bq_err("bq25970_get_enable_chg error\n");
		return ret;
	}

	en_now = (reg & BQ2597X_CHARGE_EN_MASK) >> BQ2597X_CHARGE_EN_SHIFT;

	if (!!en_now == !!en) {
		chip->chip_enabled = !!en;
		bq_info("charger is already %d, reg[0x%02X]=0x%02X\n", en, BQ2597X_CHRG_CTL_REG, reg);
		return 0;
	}

	ret = bq25970_write_mask(chip, BQ2597X_CHRG_CTL_REG,
		BQ2597X_CHARGE_EN_MASK, BQ2597X_CHARGE_EN_SHIFT,
		value);
	if (ret) {
		bq_err("set CHARGE_EN failed ret=%d\n", ret);
	}

	ret = bq25970_read_byte(chip, BQ2597X_CHRG_CTL_REG, &reg);
	if (ret) {
		bq_err("read CHARGE_EN failed ret=%d\n", ret);
	}

	bq_info("charge_enable [%x]=0x%x\n", BQ2597X_CHRG_CTL_REG, reg);

	if (en) {
		ret = bq25970_write_byte(chip, BQ2597X_ADC_CTRL_REG,
			BQ2597X_ADC_CTRL_REG_INIT);

		if (chip->hw_mode == BQ25970_SLAVE)
			ret = bq25970_write_byte(chip, BQ2597X_ADC_FN_DIS_REG,
				BQ2597X_ADC_FN_DIS_REG_INIT_SLAVE);
		else
			ret = bq25970_write_byte(chip, BQ2597X_ADC_FN_DIS_REG,
				BQ2597X_ADC_FN_DIS_REG_INIT_MASTER);
	} else {
		ret = bq25970_write_byte(chip, BQ2597X_ADC_CTRL_REG,
			BQ2597X_ADC_CTRL_REG_EXIT);
	}

	chip->chip_enabled = en_now;

	return 0;
}

static int bq25970_get_enable_chg(void * arg, unsigned int *en)
{
	int ret;
	u8 reg;
	struct bq25970_device *chip = (struct bq25970_device *)arg;

	ret = bq25970_read_byte(chip, BQ2597X_CHRG_CTL_REG, &reg);
	if (ret) {
		bq_err("bq25970_get_enable_chg error\n");
		return -1;
	}
	*en = (reg & BQ2597X_CHARGE_EN_MASK) >> BQ2597X_CHARGE_EN_SHIFT;

	dev_info(chip->dev, "%s %d, ret(%d)\n", __func__, *en, ret);
	return ret;
}

static int bq25970_discharge(struct bq25970_device *chip, int enable)
{
	int ret;
	u8 reg = 0;
	u8 value = enable ? 0x1 : 0x0;

	ret = bq25970_write_mask(chip, BQ2597X_BUS_OVP_REG,
		BQ2597X_VBUS_PD_EN_MASK, BQ2597X_VBUS_PD_EN_SHIFT,
		value);
	if (ret)
		return -1;

	ret = bq25970_read_byte(chip, BQ2597X_CONTROL_REG, &reg);
	if (ret)
		return -1;

	bq_info("discharge [%x]=0x%x\n", BQ2597X_CONTROL_REG, reg);
	return 0;
}

static int bq25970_is_device_close(struct bq25970_device *chip)
{
	u8 reg = 0;
	int ret = 0;

	ret = bq25970_read_byte(chip, BQ2597X_CHRG_CTL_REG, &reg);
	if (ret)
		return 1;

	if (reg & BQ2597X_CHARGE_EN_MASK)
		return 0;

	return 1;
}

static int bq25970_get_device_id(struct bq25970_device *chip)
{
	u8 part_info = 0;
	int ret = 0;

	if (chip == NULL) {
		bq_err("chip is null\n");
		return -1;
	}

	ret = bq25970_read_byte(chip, BQ2597X_PART_INFO_REG, &part_info);
	if (ret) {
		bq_err("get_device_id read fail\n");
		return -1;
	}

	bq_info("get_device_id [%x]=0x%x\n", BQ2597X_PART_INFO_REG, part_info);

	part_info = part_info & BQ2597X_DEVICE_ID_MASK;
	switch (part_info) {
	case BQ2597X_DEVICE_ID_BQ25970:
		chip->device_id = 1;
		break;
	case BQ2597X_DEVICE_ID_SY6537C:
		chip->device_id = 2;
		break;
	default:
		chip->device_id = -1;
		bq_err("switchcap get dev_id fail\n");
		break;
	}

	return chip->device_id;
}

static int bq25970_get_vbat_mv(void *arg, unsigned int *vbat)
{
	struct bq25970_device *chip = (struct bq25970_device *)arg;
	u8 reg_high = 0;
	u8 reg_low = 0;
	s16 voltage = 0;
	int ret = 0;

	if (!chip->chip_enabled) {
		*vbat = 0;
		return 0;
	}

	ret = bq25970_read_byte(chip, BQ2597X_VBAT_ADC1_REG, &reg_high);
	ret |= bq25970_read_byte(chip, BQ2597X_VBAT_ADC0_REG, &reg_low);
	if (ret)
		return -1;

	voltage = (reg_high << BQ2597X_LENTH_OF_BYTE) + reg_low;
	*vbat = (int)(voltage);

	if (chip->device_id == 2)
		*vbat = *vbat / 2;
	
	bq_info("VBAT_ADC1=0x%x, VBAT_ADC0=0x%x, vbat=%d\n", reg_high, reg_low, *vbat);

	return 0;
}

static int bq25970_get_ibat_ma(void *arg, unsigned int *ibat)
{
	struct bq25970_device *chip = (struct bq25970_device *)arg;
	int ret = 0;
	u8 reg_high = 0;
	u8 reg_low = 0;
	s16 curr = 0;

	if (!chip->chip_enabled) {
		*ibat = 0;
		return 0;
	}

	*ibat = 0;
	return 0;

	ret = bq25970_read_byte(chip, BQ2597X_IBAT_ADC1_REG, &reg_high);
	ret |= bq25970_read_byte(chip, BQ2597X_IBAT_ADC0_REG, &reg_low);
	if (ret)
		return -1;

	curr = (reg_high << BQ2597X_LENTH_OF_BYTE) + reg_low;
	*ibat = (int)(curr);

	if (chip->device_id == 2)
		*ibat = *ibat * 5 / 8;

	bq_info("IBAT_ADC1=0x%x, IBAT_ADC0=0x%x ibat=%d\n", reg_high, reg_low, *ibat);

	return 0;
}

static int bq25970_get_ibus_ma(void *arg, unsigned int *ibus)
{
	struct bq25970_device *chip = (struct bq25970_device *)arg;
	u8 reg_high = 0;
	u8 reg_low = 0;
	int ret;
	int curr;

	if (!chip->chip_enabled) {
		*ibus = 0;
		return 0;
	}

	ret = bq25970_read_byte(chip, BQ2597X_IBUS_ADC1_REG, &reg_high);
	ret |= bq25970_read_byte(chip, BQ2597X_IBUS_ADC0_REG, &reg_low);
	if (ret)
		return -1;

	curr = (reg_high << BQ2597X_LENTH_OF_BYTE) + reg_low;
	*ibus = (int)(curr);

	if (chip->device_id == 2)
		*ibus = *ibus * 5 / 16;

	bq_info("IBUS_ADC1=0x%x, IBUS_ADC0=0x%x, ibus=%d\n", reg_high, reg_low, *ibus);

	return 0;
}

static int bq25970_get_vbus_mv(void *arg, unsigned int *vbus)
{
	struct bq25970_device *chip = (struct bq25970_device *)arg;
	int ret = 0;
	u8 reg_high = 0;
	u8 reg_low = 0;
	s16 voltage = 0;

	if (!chip->chip_enabled) {
		*vbus = 0;
		return 0;
	}

	ret = bq25970_read_byte(chip,BQ2597X_VBUS_ADC1_REG, &reg_high);
	ret |= bq25970_read_byte(chip,BQ2597X_VBUS_ADC0_REG, &reg_low);
	if (ret)
		return -1;

	voltage = (reg_high << BQ2597X_LENTH_OF_BYTE) + reg_low;
	*vbus = (int)(voltage);

	bq_info("VBUS_ADC1=0x%x, VBUS_ADC0=0x%x, vbus=%d\n", reg_high, reg_low, *vbus);

	return 0;
}

static int bq25970_get_tsbus_percentage(struct bq25970_device *chip, long *tsbus_per)
{
	int ret = 0;
	u8 reg_high = 0;
	u8 reg_low = 0;
	s16 adc_value = 0;

	ret = bq25970_read_byte(chip, BQ2597X_TSBUS_ADC1_REG, &reg_high);
	ret |= bq25970_read_byte(chip,BQ2597X_TSBUS_ADC0_REG, &reg_low);
	if (ret)
		return -1;

	bq_info("TSBUS_ADC1=0x%x, TSBUS_ADC0=0x%x\n", reg_high, reg_low);

	adc_value = (((reg_high & BQ2597X_TSBUS_ADC1_MASK) <<
		BQ2597X_LENTH_OF_BYTE) + reg_low);
	*tsbus_per = (long)(adc_value * BQ2597X_TSBUS_ADC_STEP);

	return 0;
}

static int bq25970_get_tsbus_ntc_resistor(struct bq25970_device *chip,
		int adc_channel, long *data)
{
	int ret = 0;
	long tsbus_per = 0;
	long r_temp = 0;

	ret = bq25970_get_tsbus_percentage(chip, &tsbus_per);
	if (ret)
		return -1;

	/*
	 * Rt = 1/((1 / Rntc) + (1 / Rlow))
	 * Vtsbus/Vout =  Rt/(Rhigh + Rt)
	 * r_temp = (tsbus_per * tsbus_high_r_kohm) /
	 * (BQ2597X_TSBUS_PER_MAX - tsbus_per);
	 * data = (r_temp * tsbus_low_r_kohm) / (tsbus_low_r_kohm - r_temp);
	 */
	r_temp = ((BQ2597X_TSBUS_PER_MAX * tsbus_low_r_kohm) -
		tsbus_per * (tsbus_low_r_kohm + tsbus_high_r_kohm));
	if (r_temp <= 0) {
		bq_err("get tsbus ntc resistor failed\n");
		return -1;
	}

	*data = ((tsbus_high_r_kohm * tsbus_low_r_kohm * tsbus_per) /
		r_temp * BQ2597X_RESISTORS_KILO);

	return 0;
}

static int bq25970_is_tsbat_disabled(struct bq25970_device *chip)
{
	u8 reg = 0;
	int ret = 0;

	ret = bq25970_read_byte(chip,BQ2597X_CHRG_CTL_REG, &reg);
	if (ret)
		return -1;

	bq_info("is_tsbat_disabled [%x]=0x%x\n", BQ2597X_CHRG_CTL_REG, reg);

	if (reg & BQ2597X_TSBAT_DIS_MASK)
		return 0;

	return -1;
}

static int bq25970_get_device_temp(struct bq25970_device *chip, int *temp)
{
	u8 reg_high = 0;
	u8 reg_low = 0;
	s16 temperature;
	int ret;

	ret = bq25970_read_byte(chip, BQ2597X_TDIE_ADC1_REG, &reg_high);
	ret |= bq25970_read_byte(chip, BQ2597X_TDIE_ADC0_REG, &reg_low);
	if (ret)
		return -1;

	bq_info("TDIE_ADC1=0x%x, TDIE_ADC0=0x%x\n", reg_high, reg_low);

	temperature = (((reg_high & BQ2597X_TDIE_ADC1_MASK) <<
		BQ2597X_LENTH_OF_BYTE) + reg_low);
	/* bq25970 tdie adc is not working correctly , return 0 */
	/* *temp = (int)(temperature / BQ2597X_TDIE_SCALE); */
	*temp = 0;

	return 0;
}

static int bq25970_config_watchdog(struct bq25970_device *chip, int enable)
{
	u8 reg;
	int ret;
	u8 value = enable ? 0x0 : 0x1;

	ret = bq25970_write_mask(chip, BQ2597X_CONTROL_REG,
		BQ2597X_WATCHDOG_DIS_MASK, BQ2597X_WATCHDOG_DIS_SHIFT,
		value);
	if (ret)
		return -1;

	ret = bq25970_read_byte(chip, BQ2597X_CONTROL_REG, &reg);
	if (ret)
		return -1;

	bq_info("config_watchdog [%x]=0x%x\n",
		BQ2597X_CONTROL_REG, reg);

	return 0;
}


static int bq25970_config_watchdog_ms(struct bq25970_device *chip, int time)
{
	u8 val;
	u8 reg;
	int ret;

	if (time >= BQ2597X_WTD_CONFIG_TIMING_30000MS)
		val = 3;
	else if (time >= BQ2597X_WTD_CONFIG_TIMING_5000MS)
		val = 2;
	else if (time >= BQ2597X_WTD_CONFIG_TIMING_1000MS)
		val = 1;
	else
		val = 0;

	ret = bq25970_write_mask(chip, BQ2597X_CONTROL_REG,
		BQ2597X_WATCHDOG_CONFIG_MASK, BQ2597X_WATCHDOG_CONFIG_SHIFT,
		val);
	if (ret)
		return -1;

	ret = bq25970_read_byte(chip, BQ2597X_CONTROL_REG, &reg);
	if (ret)
		return -1;

	bq_info("config_watchdog_ms [%x]=0x%x\n",
		BQ2597X_CONTROL_REG, reg);

	return 0;
}

static int bq25970_set_ibat_sns_res(void * arg, unsigned int isns_res_mo)
{
	struct bq25970_device *chip = (struct bq25970_device *)arg;
	int ret = 0, sns_res = 0;

	if (chip->device_id == 1) {
		sns_res =  (isns_res_mo == 2) ?
			BQ2597X_IBAT_SNS_RESISTOR_2MOHM	: BQ2597X_IBAT_SNS_RESISTOR_5MOHM;
		bq_err("set bq ibat_sns_res %d(1:5MOHM,0:2MOHM)\n", sns_res);
		ret = bq25970_write_mask(chip, BQ2597X_PULSE_MODE_REG,
			BQ2597X_IBAT_SNS_RES_MASK, BQ2597X_IBAT_SNS_RES_SHIFT,
			sns_res);
		if (ret) {
			bq_err("set_ibat_sns_res error ret=%d\n", ret);
			return -1;
		}
	} else if (chip->device_id == 2) {
		if (isns_res_mo == 5) {
			bq_err("set sy ibat_sns_res 5MOHM\n");
	 		ret = bq25970_write_byte(chip, 0x4D, 0x10);
			if (ret) {
				bq_err("set_ibat_sns_res error ret=%d\n", ret);
				return -1;
			}
		} else {
			sns_res =  (isns_res_mo == 2) ?
			BQ2597X_IBAT_SNS_RESISTOR_2MOHM	: BQ2597X_IBAT_SNS_RESISTOR_1MOHM;
			bq_err("set sy ibat_sns_res %d(1:1MOHM,0:2MOHM)\n", sns_res);
			ret = bq25970_write_mask(chip, BQ2597X_PULSE_MODE_REG,
				BQ2597X_IBAT_SNS_RES_MASK, BQ2597X_IBAT_SNS_RES_SHIFT,
				sns_res);
			if (ret) {
				bq_err("set_ibat_sns_res error ret=%d\n", ret);
				return -1;
			}
		}
	}

	return 0;
}

static int bq25970_config_regulation(struct bq25970_device *chip, unsigned int enable)
{
	int ret = 0;

	ret = bq25970_write_mask(chip, BQ2597X_REG_THRESHOLD_REG,
		BQ2597X_IBAT_REG_SET_MASK, BQ2597X_IBAT_REG_SET_SHIFT,
		BQ2597X_IBAT_OVP_REG_200MA);
	if (ret) {
		bq_err("set_ibat_sns_res error ret=%d\n", ret);
		return -1;
	}

	ret = bq25970_write_mask(chip, BQ2597X_REG_THRESHOLD_REG,
		BQ2597X_VBAT_REG_SET_MASK, BQ2597X_VBAT_REG_SET_SHIFT,
		BQ2597X_VBAT_OVP_REG_100MV);
	if (ret) {
		bq_err("set_ibat_sns_res error ret=%d\n", ret);
		return -1;
	}

	ret = bq25970_write_mask(chip, BQ2597X_PULSE_MODE_REG,
		BQ2597X_EN_REGULATION_MASK, BQ2597X_EN_REGULATION_SHIFT,
		enable);
	if (ret) {
		bq_err("set_ibat_sns_res error ret=%d\n", ret);
		return -1;
	}

	return 0;
}


static int bq25970_set_vbatovp(void * arg, unsigned int mV)
{
	struct bq25970_device *chip = (struct bq25970_device *)arg;
	u8 value;
	int ret = 0;

	if (mV < BQ2597X_BAT_OVP_BASE_3500MV)
		mV = BQ2597X_BAT_OVP_BASE_3500MV;

	if (mV > BQ2597X_BAT_OVP_MAX_5075MV)
		mV = BQ2597X_BAT_OVP_MAX_5075MV;

	value = (u8)((mV - BQ2597X_BAT_OVP_BASE_3500MV) /
		BQ2597X_BAT_OVP_STEP);
	ret = bq25970_write_mask(chip, BQ2597X_BAT_OVP_REG,
		BQ2597X_BAT_OVP_MASK, BQ2597X_BAT_OVP_SHIFT,
		value);
	if (ret) {
		bq_err("config_vbat_ovp_threshold_mv error ret=%d\n", ret);
		return -1;
	}

	bq_info("config_vbat_ovp_threshold_mv [0x%02X]=0x%02X, set %d mV\n",
		BQ2597X_BAT_OVP_REG, value, mV);

	return 0;

}

static int bq25970_get_vbatovp(void * arg, unsigned int *mV)
{
	struct bq25970_device *chip = (struct bq25970_device *)arg;
	u8 value = 0;
	int ret = 0;

	ret = bq25970_read_byte(chip, BQ2597X_BAT_OVP_REG, &value);
	if (ret) {
		bq_err("get_vbatovp error ret=%d\n", ret);
		return -1;
	}

	value = (value & BQ2597X_BAT_OVP_MASK) >> BQ2597X_BAT_OVP_SHIFT;

	*mV = value * BQ2597X_BAT_OVP_STEP + BQ2597X_BAT_OVP_BASE_3500MV;

	bq_info("reg(0x%02X)=0x%02X, vbatovp=%d(mV)\n", BQ2597X_BAT_OVP_REG, value, *mV);

	return 0;
}

static int bq25970_set_vbatovp_alarm(void * arg, unsigned int mV)
{
	struct bq25970_device *chip = (struct bq25970_device *)arg;
	
	u8 value;
	int ret = 0;

	if (mV < BQ2597X_BAT_OVP_ALM_BASE)
		mV = BQ2597X_BAT_OVP_ALM_BASE;

	if (mV > BQ2597X_BAT_OVP_ALM_MAX)
		mV = BQ2597X_BAT_OVP_ALM_MAX;

	value = (u8)((mV - BQ2597X_BAT_OVP_ALM_BASE) /
		BQ2597X_BAT_OVP_ALM_STEP);
	ret = bq25970_write_mask(chip, BQ2597X_BAT_OVP_ALM_REG,
		BQ2597X_BAT_OVP_ALM_MASK, BQ2597X_BAT_OVP_ALM_SHIFT,
		value);
	if (ret) {
		bq_err("bq25970_set_vbatovp_alarm error ret=%d\n", ret);
		return -1;
	}

	bq_info("set_vbatovp_alarm_mv [0x%02X]=0x%02X, set %d mV\n",
		BQ2597X_BAT_OVP_ALM_REG, value, mV);

	return 0;
}

static int bq25970_get_vbatovp_alarm(void * arg, unsigned int *mV)
{
	struct bq25970_device *chip = (struct bq25970_device *)arg;
	u8 value = 0;
	int ret = 0;
	
	ret = bq25970_read_byte(chip, BQ2597X_BAT_OVP_ALM_REG, &value);
	if (ret) {
		bq_err("bq25970_get_vbatovp_alarm error ret=%d\n", ret);
		return -1;
	}

	value = (value & BQ2597X_BAT_OVP_ALM_MASK) >> BQ2597X_BAT_OVP_ALM_SHIFT;

	*mV = value * BQ2597X_BAT_OVP_ALM_STEP + BQ2597X_BAT_OVP_ALM_BASE;

	bq_info("reg(0x%02X)=0x%02X, vbatovp_alarm=%d(mV)\n", BQ2597X_BAT_OVP_ALM_REG, value, *mV);

	return 0;
}

static int bq25970_set_ibatocp(void * arg, unsigned int mA)
{
	struct bq25970_device *chip = (struct bq25970_device *)arg;
	u8 value;
	int ret = 0;

	if (mA < BQ2597X_BAT_OCP_BASE_2000MA)
		mA = BQ2597X_BAT_OCP_BASE_2000MA;

	if (mA > BQ2597X_BAT_OCP_MAX_14700MA)
		mA = BQ2597X_BAT_OCP_MAX_14700MA;

	value = (u8)((mA - BQ2597X_BAT_OCP_BASE_2000MA) /
		BQ2597X_BAT_OCP_STEP);
	ret = bq25970_write_mask(chip, BQ2597X_BAT_OCP_REG,
		BQ2597X_BAT_OCP_MASK, BQ2597X_BAT_OCP_SHIFT,
		value);
	if (ret) {
		bq_err("config_ibat_ocp_threshold_ma error ret=%d", ret);
		return -1;
	}

	bq_info("config_ibat_ocp_threshold_ma [0x%02X]=0x%02X, set %d mA\n",
		BQ2597X_BAT_OCP_REG, value, mA);

	return 0;
}

static int bq25970_get_ibatocp(void * arg, unsigned int *mA)
{
	struct bq25970_device *chip = (struct bq25970_device *)arg;
	u8 value = 0;
	int ret = 0;

	ret = bq25970_read_byte(chip, BQ2597X_BAT_OCP_REG, &value);
	if (ret) {
		bq_err("get_ibatocp error ret=%d\n", ret);
		return -1;
	}

	value = (value & BQ2597X_BAT_OVP_MASK) >> BQ2597X_BAT_OVP_SHIFT;

	*mA = value * BQ2597X_BAT_OCP_STEP + BQ2597X_BAT_OCP_BASE_2000MA;

	bq_info("reg(0x%02X)=0x%02X, ibatocp=%d(mA)\n", BQ2597X_BAT_OCP_REG, value, *mA);

	return 0;
}


static int bq25970_set_ibatocp_alarm(void * arg, unsigned int mA)
{
	struct bq25970_device *chip = (struct bq25970_device *)arg;
	u8 value;
	int ret = 0;

	if (mA < BQ2597X_BAT_OCP_ALM_BASE)
		mA = BQ2597X_BAT_OCP_ALM_BASE;

	if (mA > BQ2597X_BAT_OCP_ALM_MAX)
		mA = BQ2597X_BAT_OCP_ALM_MAX;

	value = (u8)((mA - BQ2597X_BAT_OCP_ALM_BASE) /
		BQ2597X_BAT_OCP_ALM_STEP);
	ret = bq25970_write_mask(chip, BQ2597X_BAT_OCP_ALM_REG,
		BQ2597X_BAT_OCP_ALM_MASK, BQ2597X_BAT_OCP_ALM_SHIFT,
		value);
	if (ret) {
		bq_err("set_ibatocp_alarm error ret=%d\n", ret);
		return -1;
	}

	bq_info("set_ibatocp_alarm [0x%02X]=0x%02X, set %d mA\n",
		BQ2597X_BAT_OCP_ALM_REG, value, mA);

	return 0;

}

static int bq25970_get_ibatocp_alarm(void * arg, unsigned int *mA)
{
	struct bq25970_device *chip = (struct bq25970_device *)arg;
	u8 value = 0;
	int ret = 0;
	
	ret = bq25970_read_byte(chip, BQ2597X_BAT_OCP_ALM_REG, &value);
	if (ret) {
		bq_err("get_ibatocp_alarm error ret=%d\n", ret);
		return -1;
	}

	value = (value & BQ2597X_BAT_OCP_ALM_MASK) >> BQ2597X_BAT_OCP_ALM_SHIFT;

	*mA = value * BQ2597X_BAT_OCP_ALM_STEP + BQ2597X_BAT_OCP_ALM_BASE;

	bq_info("reg(0x%02X)=0x%02X, ibatocp_alarm=%d(mA)\n", BQ2597X_BAT_OCP_ALM_REG, value, *mA);

	return 0;
}

static int bq25970_set_vacovp(void * arg, unsigned int mV)
{
	struct bq25970_device *chip = (struct bq25970_device *)arg;
	u8 value;
	int ret = 0;

	if (mV < BQ2597X_AC_OVP_BASE_11000MV)
		mV = BQ2597X_AC_OVP_BASE_11000MV;

	if (mV > BQ2597X_AC_OVP_MAX_17000MV)
		mV = BQ2597X_AC_OVP_MAX_17000MV;

	value = (u8)((mV - BQ2597X_AC_OVP_BASE_11000MV) /
		BQ2597X_AC_OVP_STEP);
	ret = bq25970_write_mask(chip, BQ2597X_AC_OVP_REG,
		BQ2597X_AC_OVP_MASK, BQ2597X_AC_OVP_SHIFT,
		value);
	if (ret) {
		bq_err("config_ac_ovp_threshold_mv error ret=%d\n", ret);
		return -1;
	}

	bq_info("config_ac_ovp_threshold_mv [0x%02X]=0x%02X, set %d mV\n",
		BQ2597X_AC_OVP_REG, value, mV);

	return 0;

}

static int bq25970_get_vacovp(void * arg, unsigned int *mV)
{
	u8 value = 0;
	int ret = 0;
	struct bq25970_device *chip = (struct bq25970_device *)arg;

	ret = bq25970_read_byte(chip, BQ2597X_AC_OVP_REG, &value);
	if (ret) {
		bq_err("get_vacovp error ret=%d\n", ret);
		return -1;
	}
	
	value = (value & BQ2597X_AC_OVP_MASK) >> BQ2597X_AC_OVP_SHIFT;

	*mV = value * BQ2597X_AC_OVP_STEP + BQ2597X_AC_OVP_BASE_11000MV;

	bq_info("reg(0x%02X)=0x%02X, vacovp=%d(mV)\n", BQ2597X_AC_OVP_REG, value, *mV);

	return 0;
}

static int bq25970_set_vbusovp(void * arg, u32 mV)
{
	struct bq25970_device *chip = (struct bq25970_device *)arg;
	u8 value;
	int ret = 0;

	if (mV < BQ2597X_BUS_OVP_BASE_6000MV)
		mV = BQ2597X_BUS_OVP_BASE_6000MV;

	if (mV > BQ2597X_BUS_OVP_MAX_12350MV)
		mV = BQ2597X_BUS_OVP_MAX_12350MV;

	value = (u8)((mV - BQ2597X_BUS_OVP_BASE_6000MV) /
		BQ2597X_BUS_OVP_STEP);
	ret = bq25970_write_mask(chip, BQ2597X_BUS_OVP_REG,
		BQ2597X_BUS_OVP_MASK, BQ2597X_BUS_OVP_SHIFT,
		value);
	if (ret) {
		bq_err("config_vbus_ovp_threshold_mv error ret=%d\n", ret);
		return -1;
	}

	bq_info("config_vbus_ovp_threshold_mv [0x%02X]=0x%02X, set %d mV\n",
		BQ2597X_BUS_OVP_REG, value, mV);

	return 0;

}

static int bq25970_get_vbusovp(void * arg, u32 *mV)
{
	u8 value = 0;
	int ret = 0;
	struct bq25970_device *chip = (struct bq25970_device *)arg;

	ret = bq25970_read_byte(chip, BQ2597X_BUS_OVP_REG, &value);
	if (ret) {
		bq_err("get_vbusovp error ret=%d\n", ret);
		return -1;
	}
	
	value = (value & BQ2597X_BUS_OVP_MASK) >> BQ2597X_BUS_OVP_SHIFT;

	*mV = value * BQ2597X_BUS_OVP_STEP + BQ2597X_BUS_OVP_BASE_6000MV;

	bq_info("reg(0x%02X)=0x%02X, vbusovp=%d(mV)\n", BQ2597X_BUS_OVP_REG, value, *mV);

	return 0;
}

static int bq25970_set_vbusovp_alarm(void * arg, u32 mV)
{
	struct bq25970_device *chip = (struct bq25970_device *)arg;
	
	u8 value;
	int ret = 0;

	if (mV < BQ2597X_BUS_OVP_ALM_BASE)
		mV = BQ2597X_BUS_OVP_ALM_BASE;

	if (mV > BQ2597X_BUS_OVP_ALM_MAX)
		mV = BQ2597X_BUS_OVP_ALM_MAX;

	value = (u8)((mV - BQ2597X_BUS_OVP_ALM_BASE) /
		BQ2597X_BUS_OVP_ALM_STEP);
	ret = bq25970_write_mask(chip, BQ2597X_BUS_OVP_ALM_REG,
		BQ2597X_BUS_OVP_ALM_MASK, BQ2597X_BUS_OVP_ALM_SHIFT,
		value);
	if (ret) {
		bq_err("set_vbusovp_alarm error ret=%d\n", ret);
		return -1;
	}

	bq_info("set_vbusovp_alarm [0x%02X]=0x%02X, set %d mV\n",
		BQ2597X_BUS_OVP_ALM_REG, value, mV);

	return 0;
}

static int bq25970_get_vbusovp_alarm(void * arg, u32 *mV)
{
	struct bq25970_device *chip = (struct bq25970_device *)arg;
	u8 value = 0;
	int ret = 0;
	
	ret = bq25970_read_byte(chip, BQ2597X_BUS_OVP_ALM_REG, &value);
	if (ret) {
		bq_err("get_vbusovp_alarm error ret=%d\n", ret);
		return -1;
	}

	value = (value & BQ2597X_BUS_OVP_ALM_MASK) >> BQ2597X_BUS_OVP_ALM_SHIFT;

	*mV = value * BQ2597X_BUS_OVP_ALM_STEP + BQ2597X_BUS_OVP_ALM_BASE;

	bq_info("reg(0x%02X)=0x%02X, vbusovp_alarm=%d(mV)\n", BQ2597X_BUS_OVP_ALM_REG, value, *mV);

	return 0;
}

static int bq25970_set_ibusocp(void * arg, u32 mA)
{
	struct bq25970_device *chip = (struct bq25970_device *)arg;
	u8 value;
	int ret = 0;

	if (mA < BQ2597X_BUS_OCP_BASE_1000MA)
		mA = BQ2597X_BUS_OCP_BASE_1000MA;

	if (mA > BQ2597X_BUS_OCP_MAX_4750MA)
		mA = BQ2597X_BUS_OCP_MAX_4750MA;

	value = (u8)((mA - BQ2597X_BUS_OCP_BASE_1000MA) /
		BQ2597X_BUS_OCP_STEP);
	ret = bq25970_write_mask(chip, BQ2597X_BUS_OCP_UCP_REG,
		BQ2597X_BUS_OCP_MASK, BQ2597X_BUS_OCP_SHIFT,
		value);
	if (ret) {
		bq_info("config_ibus_ocp_threshold_ma error ret=%d\n", ret);
		return -1;
	}

	bq_info("config_ibus_ocp_threshold_ma [0x%02X]=0x%02X, set %d mA\n",
		BQ2597X_BUS_OCP_UCP_REG, value, mA);

	return 0;

}

static int bq25970_get_ibusocp(void * arg, u32 *mA)
{
	u8 value = 0;
	int ret = 0;
	struct bq25970_device *chip = (struct bq25970_device *)arg;

	ret = bq25970_read_byte(chip, BQ2597X_BUS_OCP_UCP_REG, &value);
	if (ret) {
		bq_err("get_ibusocp error ret=%d\n", ret);
		return -1;
	}
	
	value = (value & BQ2597X_BUS_OCP_MASK) >> BQ2597X_BUS_OCP_SHIFT;

	*mA = value * BQ2597X_BUS_OCP_STEP + BQ2597X_BUS_OCP_BASE_1000MA;

	bq_info("reg(0x%02X)=0x%02X, ibusocp=%d(mA)\n", BQ2597X_BUS_OCP_UCP_REG, value, *mA);

	return 0;
}

static int bq25970_set_ibusocp_alarm(void * arg, u32 mA)
{
	struct bq25970_device *chip = (struct bq25970_device *)arg;
	
	u8 value;
	int ret = 0;

	if (mA < BQ2597X_BUS_OCP_ALM_BASE)
		mA = BQ2597X_BUS_OCP_ALM_BASE;

	if (mA > BQ2597X_BUS_OCP_ALM_MAX)
		mA = BQ2597X_BUS_OCP_ALM_MAX;

	value = (u8)((mA - BQ2597X_BUS_OCP_ALM_BASE) /
		BQ2597X_BUS_OCP_ALM_STEP);
	ret = bq25970_write_mask(chip, BQ2597X_BUS_OCP_ALM_REG,
		BQ2597X_BUS_OCP_ALM_MASK, BQ2597X_BUS_OCP_ALM_SHIFT,
		value);
	if (ret) {
		bq_err("set_ibusocp_alarm error ret=%d\n", ret);
		return -1;
	}

	bq_info("set_ibusocp_alarm [0x%02X]=0x%02X, set %d mA\n",
		BQ2597X_BUS_OCP_ALM_REG, value, mA);

	return 0;
}

static int bq25970_get_ibusocp_alarm(void * arg, u32 *mA)
{
	struct bq25970_device *chip = (struct bq25970_device *)arg;
	u8 value = 0;
	int ret = 0;
	
	ret = bq25970_read_byte(chip, BQ2597X_BUS_OCP_ALM_REG, &value);
	if (ret) {
		bq_err("get_vbusovp_alarm error ret=%d\n", ret);
		return -1;
	}

	value = (value & BQ2597X_BUS_OCP_ALM_MASK) >> BQ2597X_BUS_OCP_ALM_SHIFT;

	*mA = value * BQ2597X_BUS_OCP_ALM_STEP + BQ2597X_BUS_OCP_ALM_BASE;

	bq_info("reg(0x%02X)=0x%02X, ibusocp_alarm=%d(mA)\n", BQ2597X_BUS_OCP_ALM_REG, value, *mA);

	return 0;
}

static int bq25970_config_switching_frequency(struct bq25970_device *chip, int data)
{
	int freq = 0;
	int freq_shift = 0;
	int ret = 0;

	switch (data) {
	case BQ2597X_SW_FREQ_450KHZ:
		freq = BQ2597X_FSW_SET_SW_FREQ_500KHZ;
		freq_shift = BQ2597X_SW_FREQ_SHIFT_M_P10;
		break;

	case BQ2597X_SW_FREQ_500KHZ:
		freq = BQ2597X_FSW_SET_SW_FREQ_500KHZ;
		freq_shift = BQ2597X_SW_FREQ_SHIFT_NORMAL;
		break;

	case BQ2597X_SW_FREQ_550KHZ:
		freq = BQ2597X_FSW_SET_SW_FREQ_500KHZ;
		freq_shift = BQ2597X_SW_FREQ_SHIFT_P_P10;
		break;

	case BQ2597X_SW_FREQ_675KHZ:
		freq = BQ2597X_FSW_SET_SW_FREQ_750KHZ;
		freq_shift = BQ2597X_SW_FREQ_SHIFT_M_P10;
		break;

	case BQ2597X_SW_FREQ_750KHZ:
		freq = BQ2597X_FSW_SET_SW_FREQ_750KHZ;
		freq_shift = BQ2597X_SW_FREQ_SHIFT_NORMAL;
		break;

	case BQ2597X_SW_FREQ_825KHZ:
		freq = BQ2597X_FSW_SET_SW_FREQ_750KHZ;
		freq_shift = BQ2597X_SW_FREQ_SHIFT_P_P10;
		break;

	default:
		freq = BQ2597X_FSW_SET_SW_FREQ_500KHZ;
		freq_shift = BQ2597X_SW_FREQ_SHIFT_P_P10;
		break;
	}

	ret = bq25970_write_mask(chip, BQ2597X_CONTROL_REG,
		BQ2597X_FSW_SET_MASK, BQ2597X_FSW_SET_SHIFT,
		freq);
	if (ret)
		return -1;

	ret = bq25970_write_mask(chip, BQ2597X_CHRG_CTL_REG,
		BQ2597X_FREQ_SHIFT_MASK, BQ2597X_FREQ_SHIFT_SHIFT,
		freq_shift);
	if (ret)
		return -1;

	bq_info("config_switching_frequency [%x]=0x%x\n",
		BQ2597X_CONTROL_REG, freq);
	bq_info("config_switching_frequency [%x]=0x%x\n",
		BQ2597X_CHRG_CTL_REG, freq_shift);

	return 0;
}

static int bq25970_get_interrupt_status(void * arg, unsigned int *status)
{
	struct bq25970_device *chip = (struct bq25970_device *)arg;
	int ret = 0;

	dev_info(chip->dev, "%s 0x%04X\n", __func__, *status);
	*status = chip->stat;
	return ret;
}

static int bq25970_chip_init(void)
{
	return 0;
}

static int bq25970_init_reg(void * arg)
{
	struct bq25970_device *chip = (struct bq25970_device *)arg;
	int ret = 0;

	ret = bq25970_write_byte(chip, BQ2597X_CONTROL_REG,
		BQ2597X_CONTROL_REG_INIT);
	ret |= bq25970_write_byte(chip, BQ2597X_CHRG_CTL_REG,
		BQ2597X_CHRG_CTL_REG_INIT);
	ret |= bq25970_write_byte(chip, BQ2597X_INT_MASK_REG,
		BQ2597X_INT_MASK_REG_INIT);
	ret |= bq25970_write_byte(chip, BQ2597X_FLT_MASK_REG,
		BQ2597X_FLT_MASK_REG_INIT);
	ret |= bq25970_write_byte(chip, BQ2597X_ADC_CTRL_REG,
		BQ2597X_ADC_CTRL_REG_EXIT);

	if (chip->hw_mode == BQ25970_SLAVE) {
		ret |= bq25970_write_byte(chip, BQ2597X_ADC_FN_DIS_REG,
				BQ2597X_ADC_FN_DIS_REG_INIT_SLAVE);
		ret |= bq25970_write_mask(chip, BQ2597X_BAT_OVP_ALM_REG,
					BQ2597X_BAT_OVP_ALM_DIS_MASK, BQ2597X_BAT_OVP_ALM_DIS_SHIFT,
					BQ2597X_ALM_DISABLE);
		ret |= bq25970_write_mask(chip, BQ2597X_BAT_OCP_ALM_REG,
					BQ2597X_BAT_OCP_ALM_DIS_MASK, BQ2597X_BAT_OCP_ALM_DIS_SHIFT,
					BQ2597X_ALM_DISABLE);
		ret |= bq25970_write_mask(chip, BQ2597X_BAT_UCP_ALM_REG,
					BQ2597X_BAT_UCP_ALM_DIS_MASK, BQ2597X_BAT_UCP_ALM_DIS_SHIFT,
					BQ2597X_ALM_DISABLE);
	} else {
		ret |= bq25970_write_byte(chip, BQ2597X_ADC_FN_DIS_REG,
				BQ2597X_ADC_FN_DIS_REG_INIT_MASTER);
		ret |= bq25970_write_mask(chip, BQ2597X_BAT_OVP_ALM_REG,
					BQ2597X_BAT_OVP_ALM_DIS_MASK, BQ2597X_BAT_OVP_ALM_DIS_SHIFT,
					BQ2597X_ALM_ENABLE);
		ret |= bq25970_write_mask(chip, BQ2597X_BAT_OCP_ALM_REG,
					BQ2597X_BAT_OCP_ALM_DIS_MASK, BQ2597X_BAT_OCP_ALM_DIS_SHIFT,
					BQ2597X_ALM_ENABLE);
		ret |= bq25970_write_mask(chip, BQ2597X_BAT_UCP_ALM_REG,
					BQ2597X_BAT_UCP_ALM_DIS_MASK, BQ2597X_BAT_UCP_ALM_DIS_SHIFT,
					BQ2597X_ALM_ENABLE);
	}

	ret |= bq25970_write_mask(chip, BQ2597X_BUS_OVP_ALM_REG,
				BQ2597X_BUS_OVP_ALM_DIS_MASK, BQ2597X_BUS_OVP_ALM_DIS_SHIFT,
				BQ2597X_ALM_ENABLE);
	ret |= bq25970_write_mask(chip, BQ2597X_BUS_OCP_ALM_REG,
				BQ2597X_BUS_OCP_ALM_DIS_MASK, BQ2597X_BUS_OCP_ALM_DIS_SHIFT,
				BQ2597X_ALM_ENABLE);
	ret |= bq25970_set_ibat_sns_res(chip, ibat_sns_resistor);
	ret |= bq25970_set_ibatocp_alarm(chip, BQ2597X_IBAT_OCP_ALARM_THRESHOLD_INIT);
	ret |= bq25970_set_vbatovp(chip, BQ2597X_VBAT_OVP_THRESHOLD_INIT);
	ret |= bq25970_set_ibatocp(chip, BQ2597X_IBAT_OCP_THRESHOLD_INIT);
	ret |= bq25970_set_vacovp(chip, BQ2597X_AC_OVP_THRESHOLD_INIT);
	ret |= bq25970_set_vbusovp(chip, BQ2597X_VBUS_OVP_THRESHOLD_INIT);
	ret |= bq25970_set_ibusocp(chip, BQ2597X_IBUS_OCP_THRESHOLD_INIT);
	ret |= bq25970_config_switching_frequency(chip, switching_frequency);
	ret |= bq25970_config_regulation(chip, false);

	ret |= bq25970_write_mask(chip, BQ2597X_BAT_OCP_REG,
				BQ2597X_BAT_OCP_DIS_MASK, BQ2597X_BAT_OCP_DIS_SHIFT,
				BQ2597X_ALM_DISABLE);

	ret |= bq25970_write_mask(chip, BQ2597X_BAT_OCP_ALM_REG,
				BQ2597X_BAT_OCP_ALM_DIS_MASK, BQ2597X_BAT_OCP_ALM_DIS_SHIFT,
				BQ2597X_ALM_DISABLE);

	ret |= bq25970_write_mask(chip, BQ2597X_BAT_UCP_ALM_REG,
				BQ2597X_BAT_UCP_ALM_DIS_MASK, BQ2597X_BAT_UCP_ALM_DIS_SHIFT,
				BQ2597X_ALM_DISABLE);

	if (ret) {
		bq_err("reg_init fail\n");
		return -1;
	}

	return 0;
}

static struct sqc_pmic_chg_ops bq25970_chg_ops = {

	.init_pmic_charger = bq25970_init_reg,

	.chg_enable = bq25970_set_enable_chg,
	.chg_enable_get = bq25970_get_enable_chg,

	.get_chg_status = NULL,
	.get_int_status = bq25970_get_interrupt_status,

	.chg_role_set = NULL,
	.chg_role_get = NULL,

	/*battery*/
	.batt_ovp_volt_set = bq25970_set_vbatovp,
	.batt_ovp_volt_get = bq25970_get_vbatovp,
	.batt_ovp_alm_volt_set = bq25970_set_vbatovp_alarm,
	.batt_ovp_alm_volt_get = bq25970_get_vbatovp_alarm,
	.batt_ocp_curr_set = bq25970_set_ibatocp,
	.batt_ocp_curr_get = bq25970_get_ibatocp,
	.batt_ocp_alm_curr_set = bq25970_set_ibatocp_alarm,
	.batt_ocp_alm_curr_get = bq25970_get_ibatocp_alarm,
	.batt_ibat_get = bq25970_get_ibat_ma,
	.batt_vbat_get = bq25970_get_vbat_mv,

	/*ac*/
	.ac_ovp_volt_set = bq25970_set_vacovp,
	.ac_ovp_volt_get = bq25970_get_vacovp,

	/*usb bus*/
	.usb_ovp_volt_set = bq25970_set_vbusovp,
	.usb_ovp_volt_get = bq25970_get_vbusovp,
	.usb_ovp_alm_volt_set = bq25970_set_vbusovp_alarm,
	.usb_ovp_alm_volt_get = bq25970_get_vbusovp_alarm,
	.usb_ocp_curr_set = bq25970_set_ibusocp,
	.usb_ocp_curr_get = bq25970_get_ibusocp,
	.usb_ocp_alm_curr_set = bq25970_set_ibusocp_alarm,
	.usb_ocp_alm_curr_get = bq25970_get_ibusocp_alarm,
	.usb_ibus_get = bq25970_get_ibus_ma,
	.usb_vbus_get = bq25970_get_vbus_mv,
};

static int bq25970_charge_init(struct bq25970_device *chip)
{
	if (chip == NULL) {
		bq_err("chip is null\n");
		return -1;
	}

	chip->device_id = bq25970_get_device_id(chip);
	if (chip->device_id == -1)
		return -1;

	bq_info("switchcap bq25970 device id is %d\n", chip->device_id);

	return 0;
}

static int bq25970_charge_exit(struct bq25970_device *chip)
{
	int ret = 0;

	if (chip == NULL) {
		bq_err("chip is null\n");
		return -1;
	}

	ret = bq25970_set_enable_chg(chip, BQ2597X_SWITCHCAP_DISABLE);

	usleep_range(10000, 11000); /* sleep 10ms */

	return ret;
}

static int bq25970_batinfo_exit(void)
{
	return 0;
}

static int bq25970_batinfo_init(void)
{
	int ret = 0;

	ret = bq25970_chip_init();
	if (ret) {
		pr_err("batinfo init fail\n");
		return -1;
	}

	return ret;
}

static void bq25970_interrupt_work(struct work_struct *work)
{
	struct bq25970_device *chip = NULL;
	u8 converter_state = 0, fault_flag = 0, ac_protection = 0;
	u8 ibus_ucp = 0, alarm_flag = 0, watchdog = 0;
	char reg_val[BQ2597X_DEGLITCH_REG + 1] = {0, };
	int ret = 0;
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	int record = 0;
#endif

	chip = container_of(work, struct bq25970_device, irq_work);
	pm_stay_awake(chip->dev);

	bq25970_dump_register(chip, reg_val, sizeof(reg_val));

	ac_protection = reg_val[BQ2597X_AC_OVP_REG];
	bq_info("ac_ovp_reg [%x]=0x%x\n", BQ2597X_AC_OVP_REG, ac_protection);

	ibus_ucp = reg_val[BQ2597X_BUS_OCP_UCP_REG];
	bq_info("bus_ocp_ucp_reg [%x]=0x%x\n", BQ2597X_BUS_OCP_UCP_REG, ibus_ucp);

	fault_flag = reg_val[BQ2597X_FLT_FLAG_REG];
	bq_info("flt_flag_reg [%x]=0x%x\n",	BQ2597X_FLT_FLAG_REG, fault_flag);

	converter_state = reg_val[BQ2597X_CONVERTER_STATE_REG];
	bq_info("converter_state_reg [%x]=0x%x\n", BQ2597X_CONVERTER_STATE_REG, converter_state);

	alarm_flag = reg_val[BQ2597X_INT_STAT_REG];
	bq_info("alarm_flag [%x]=0x%x\n", BQ2597X_INT_STAT_REG, alarm_flag);

	watchdog = reg_val[BQ2597X_CONTROL_REG];
	bq_info("alarm_flag [%x]=0x%x\n", BQ2597X_INT_STAT_REG, alarm_flag);

	if (reg_val[BQ2597X_REG_THRESHOLD_REG] & BQ2597X_VBAT_REG_ACTIVE_MASK) {
		bq_err("### VBAT_REG_ACTIVE\n");
	}

	if (reg_val[BQ2597X_REG_THRESHOLD_REG] & BQ2597X_IBAT_REG_ACTIVE_MASK) {
		bq_err("### IBAT_REG_ACTIVE\n");
	}

	if (reg_val[BQ2597X_REG_THRESHOLD_REG] & BQ2597X_VDROP_OVP_STAT_MASK) {
		bq_err("### VDROP_OVP_STAT\n");
	}

	if (reg_val[BQ2597X_REG_THRESHOLD_REG] & BQ2597X_VOUT_OVP_STAT_MASK) {
		bq_err("### VOUT_OVP_STAT\n");
	}

	if (reg_val[BQ2597X_BUS_OCP_UCP_REG] & BQ2597X_IBUS_UCP_FALL_FLAG_MASK) {
		bq_err("### IBUS UCP FALL\n");
	}

	if (reg_val[BQ2597X_BUS_OCP_UCP_REG] & BQ2597X_IBUS_UCP_RISE_FLAG_MASK) {
		bq_err("### IBUS UCP RISE\n");
	}

	if (watchdog & BQ2597X_WATCHDOG_TIMEOUT_MASK) {
		bq_err("### WATCHDOG TIMEOUT\n");

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		record = 1;
		zlog_client_notify(chip->zlog_client, ZLOG_CHG_WT_TIMEOUT);
#endif
	}

	if (converter_state & BQ2597X_TSHUT_STAT_MASK) {
		bq_err("### THERMAL SHUTDOWN\n");
	}

	if (converter_state & BQ2597X_VBUS_ERRORLO_STAT_MASK) {
		bq_err("### VBUS TOO LO TO CONVERTER\n");
	}

	if (converter_state & BQ2597X_VBUS_ERRORHI_STAT_MASK) {
		bq_err("### VBUS TOO HI TO CONVERTER\n");

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		record = 1;
		zlog_client_notify(chip->zlog_client, ZLOG_CHG_VBUS_TO_HIGH);
#endif
	}

	if (converter_state & BQ2597X_SS_TIMEOUT_FLAG_MASK) {
		bq_err("### SOFT START TIMEOUT\n");
	}

	if (converter_state & BQ2597X_CONV_STAT_FLAG_MASK) {
		bq_err("### CONVERTER RUNNING\n");
	} else {
		bq_err("### CONVERTER STOPING\n");
	}

	if (converter_state & BQ2597X_CONV_OCP_FLAG_MASK) {
		bq_err("### CONVERTER OCP\n");

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		record = 1;
		zlog_client_notify(chip->zlog_client, ZLOG_CHG_CONV_OCP);
#endif
	}

	if (converter_state & BQ2597X_PIN_DIAG_FAIL_FLAG_MASK) {
		bq_err("### PIN DIAG FAILED\n");
	}

	ret = bq25970_update_status_flag(chip, (fault_flag & BQ2597X_BAT_OVP_ALM_STAT_MASK), SQC_ERR_VBAT_OVP_ALM);
	if (ret) {
		bq_err("### SQC_ERR_VBAT_OVP_ALM\n");
	}

	ret = bq25970_update_status_flag(chip, (fault_flag & BQ2597X_BAT_OCP_ALM_STAT_MASK), SQC_ERR_IBAT_OCP_ALM);
	if (ret) {
		bq_err("### SQC_ERR_IBAT_OCP_ALM\n");
	}

	ret = bq25970_update_status_flag(chip, (fault_flag & BQ2597X_BUS_OVP_ALM_STAT_MASK), SQC_ERR_VBUS_OVP_ALM);
	if (ret) {
		bq_err("### SQC_ERR_VBUS_OVP_ALM\n");
	}

	ret = bq25970_update_status_flag(chip, (fault_flag & BQ2597X_BUS_OCP_ALM_STAT_MASK), SQC_ERR_IBUS_OCP_ALM);
	if (ret) {
		bq_err("### SQC_ERR_IBUS_OCP_ALM\n");
	}

	ret = bq25970_update_status_flag(chip, (fault_flag & BQ2597X_BAT_OVP_FLT_FLAG_MASK), SQC_ERR_VBAT_OVP);
	if (ret) {
		bq_err("### SQC_ERR_VBAT_OVP\n");

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		record = 1;
		zlog_client_notify(chip->zlog_client, ZLOG_CHG_VBAT_OVP);
#endif
	}

	ret = bq25970_update_status_flag(chip, (fault_flag & BQ2597X_BAT_OCP_FLT_FLAG_MASK), SQC_ERR_IBAT_OCP);
	if (ret) {
		bq_err("### SQC_ERR_IBAT_OCP\n");

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		record = 1;
		zlog_client_notify(chip->zlog_client, ZLOG_CHG_IBAT_OCP);
#endif
	}

	ret = bq25970_update_status_flag(chip, (fault_flag & BQ2597X_BUS_OVP_FLT_FLAG_MASK), SQC_ERR_VBUS_OVP);
	if (ret) {
		bq_err("### SQC_ERR_VBUS_OVP\n");

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		record = 1;
		zlog_client_notify(chip->zlog_client, ZLOG_CHG_VBUS_OVP);
#endif
	}

	ret = bq25970_update_status_flag(chip, (fault_flag & BQ2597X_BUS_OCP_FLT_FLAG_MASK), SQC_ERR_IBUS_OCP);
	if (ret) {
		bq_err("### SQC_ERR_IBUS_OCP\n");

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		record = 1;
		zlog_client_notify(chip->zlog_client, ZLOG_CHG_IBUS_OCP);
#endif
	}

	ret = bq25970_update_status_flag(chip, (fault_flag & BQ2597X_TSBUS_TSBAT_ALM_FLAG_MASK), SQC_ERR_TSBAT_TSBUS_OTP_ALM);
	if (ret) {
		bq_err("### SQC_ERR_TSBAT_TSBUS_OTP_ALM\n");
	}

/*
	ret = bq25970_update_status_flag(chip, (fault_flag & BQ2597X_TSBAT_FLT_FLAG_MASK), SQC_ERR_TSBAT_OTP);
*/

	if (fault_flag & BQ2597X_TSBAT_FLT_FLAG_MASK) {
		bq_err("### SQC_ERR_TSBAT_OTP\n");
	}
/*
	ret = bq25970_update_status_flag(chip, (fault_flag & BQ2597X_TSBUS_FLT_FLAG_MASK), SQC_ERR_TSBUS_OTP);
*/

	if (fault_flag & BQ2597X_TSBUS_FLT_FLAG_MASK) {
		bq_err("### SQC_ERR_TSBUS_OTP\n");
	}

	ret = bq25970_update_status_flag(chip, (fault_flag & BQ2597X_TDIE_ALM_FLAG_MASK), SQC_ERR_TSBAT_TSBUS_OTP_ALM);
	if (ret) {
		bq_err("### SQC_ERR_TSBAT_TSBUS_OTP_ALM\n");
	}

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	if (!record)
		zlog_reset_client(chip->zlog_client);
#endif

	/* clear irq */
	enable_irq(chip->irq_int);
	pm_relax(chip->dev);
}

static irqreturn_t bq25970_interrupt(int irq, void *_chip)
{
	struct bq25970_device *chip = _chip;

	if (chip == NULL) {
		bq_err("chip is null\n");
		return -1;
	}

	if (chip->chip_already_init == 0)
		bq_err("chip not init\n");

	disable_irq_nosync(chip->irq_int);
	schedule_work(&chip->irq_work);

	return IRQ_HANDLED;
}

static void bq25970_parse_dts(struct device_node *np,
	struct bq25970_device *chip)
{
	int ret = 0;

	ret = of_property_read_u32(np, "tsbus_high_r_kohm",
		&tsbus_high_r_kohm);
	if (ret) {
		bq_err("tsbus_high_r_kohm dts read failed\n");
		tsbus_high_r_kohm = BQ2597X_RESISTORS_100KOHM;
	}
	bq_info("tsbus_high_r_kohm=%d\n", tsbus_high_r_kohm);

	ret = of_property_read_u32(np, "tsbus_low_r_kohm",
		&tsbus_low_r_kohm);
	if (ret) {
		bq_err("tsbus_low_r_kohm dts read failed\n");
		tsbus_low_r_kohm = BQ2597X_RESISTORS_100KOHM;
	}
	bq_info("tsbus_low_r_kohm=%d\n", tsbus_low_r_kohm);

	ret = of_property_read_u32(np, "switching_frequency",
		&switching_frequency);
	if (ret) {
		bq_err("switching_frequency dts read failed\n");
		switching_frequency = BQ2597X_SW_FREQ_550KHZ;
	}
	bq_info("switching_frequency=%d\n", switching_frequency);

	ret = of_property_read_u32(np, "ibat_sns_resistor",
		&ibat_sns_resistor);
	if (ret) {
		bq_err("ibat_sns_resistor dts read failed\n");
		ibat_sns_resistor = 5;
	}
	bq_info("switching_frequency=%d\n", ibat_sns_resistor);
}

static int bq25970_get_hw_mode(struct bq25970_device *chip)
{
	int ret = 0, hw_mode = 0;
	u8 val = 0;

	ret = bq25970_read_byte(chip, BQ2597X_CHRG_CTL_REG, &val);

	if (ret) {
		bq_err("Failed to read operation mode register\n");
		return ret;
	}

	val = (val & BQ2597X_MS_MASK) >> BQ2597X_MS_SHIFT;
	if (val == 0x00)
		hw_mode = BQ25970_STDALONE;
	else if (val == 0x01)
		hw_mode = BQ25970_SLAVE;
	else
		hw_mode = BQ25970_MASTER;

	bq_info("work mode:%s\n", hw_mode == BQ25970_STDALONE ? "Standalone" :
			(hw_mode == BQ25970_SLAVE ? "Slave" : "Master"));

	return hw_mode;
}

static int bq25970_slave_chg_id_get(char *val, const void *arg)
{
	struct bq25970_device *chip = (struct bq25970_device *)arg;
	int slave_chg_id = 0;

	slave_chg_id = (cp1_init_finished && cp2_init_finished) ? 1 : 0;

	bq_info("cp1_init_finished: %d, cp1_init_finished: %d, slave_chg_id: %d\n",
				cp1_init_finished, cp2_init_finished, slave_chg_id);

	return snprintf(val, PAGE_SIZE, "%u", slave_chg_id);
}

static struct zte_misc_ops slave_chg_id_node = {
	.node_name = "slave_chg_id",
	.set = NULL,
	.get = bq25970_slave_chg_id_get,
	.free = NULL,
	.arg = NULL,
};

static int bq25970_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int ret = 0;
	struct bq25970_device *chip = NULL;
	struct device_node *np = NULL;
	const struct of_device_id *match_table = NULL;
	struct sqc_pmic_chg_ops *sqc_ops = NULL;

	pr_info("bq25970 probe begin\n");

	if (client == NULL || id == NULL) {
		pr_err("bq25970 client or id is null\n");
		return -ENOMEM;
	}

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (chip == NULL) {
		pr_err("bq25970 devm_kzalloc failed\n");
		return -ENOMEM;
	}

	match_table = of_match_node(bq25970_of_match, client->dev.of_node);
	if (match_table == NULL) {
		pr_err("bq25970 device tree match not found!\n");
		goto bq25970_fail_1;
	}

	chip->hw_mode = (int)match_table->data;

	bq_info("dtsmode=%d, addr=0x%02X\n", chip->hw_mode, client->addr);

	chip->dev = &client->dev;
	np = chip->dev->of_node;
	chip->client = client;
	i2c_set_clientdata(client, chip);
	INIT_WORK(&chip->irq_work, bq25970_interrupt_work);

	bq25970_parse_dts(np, chip);

	chip->gpio_int = of_get_named_gpio(np, "gpio_int", 0);
	bq_info("gpio_int=%d\n", chip->gpio_int);

	if (!gpio_is_valid(chip->gpio_int)) {
		bq_err("gpio(gpio_int) is not valid\n");
		ret = -EINVAL;
		goto bq25970_fail_0;
	}

	ret = gpio_request(chip->gpio_int, "bq25970_gpio_int");
	if (ret < 0) {
		bq_err("gpio(gpio_int) request fail\n");
		goto bq25970_fail_0;
	}

	ret = gpio_direction_input(chip->gpio_int);
	if (ret) {
		bq_err("gpio(gpio_int) set input fail\n");
		goto bq25970_fail_1;
	}

	chip->irq_int = gpio_to_irq(chip->gpio_int);
	if (chip->irq_int < 0) {
		bq_err("gpio(gpio_int) map to irq fail\n");
		ret = -EINVAL;
		goto bq25970_fail_1;
	}

	ret = request_irq(chip->irq_int, bq25970_interrupt,
		IRQF_TRIGGER_FALLING, "bq25970_int_irq", chip);
	if (ret) {
		bq_err("gpio(gpio_int) irq request fail\n");
		chip->irq_int = -1;
		goto bq25970_fail_1;
	}

	chip->chip_already_init = 1;

	if (bq25970_get_hw_mode(chip) != chip->hw_mode) {
		bq_err("bq25970 compare device hw mode failed!\n");
		goto bq25970_fail_1;
	}

	ret = bq25970_reg_reset(chip);
	if (ret) {
		bq_err("bq25970 reg reset fail\n");
		chip->chip_already_init = 0;
		goto bq25970_fail_2;
	}
	
	bq25970_charge_init(chip);

	ret = bq25970_init_reg(chip);
	if (ret) {
		bq_err("bq25970_init_reg fail\n");
		chip->chip_already_init = 0;
		goto bq25970_fail_2;
	}

	bq25970_config_watchdog(chip, 0);

	if (chip->hw_mode == BQ25970_SLAVE) {
		sqc_ops = kzalloc(sizeof(struct sqc_pmic_chg_ops), GFP_KERNEL);
		memcpy(sqc_ops, &bq25970_chg_ops, sizeof(struct sqc_pmic_chg_ops));
		sqc_ops->arg = (void *)chip;
		ret = sqc_hal_charger_register(sqc_ops, SQC_CHARGER_CP2);
		if (ret < 0) {
			bq_err("%s register sqc hal fail(%d)\n", __func__, ret);
			goto bq25970_fail_2;
		}

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		chip->zlog_client = zlog_register_client(&zlog_cp_dev2);
		if (!chip->zlog_client)
			bq_err("%s zlog register client zlog_cp_dev2 fail\n", __func__);
		chip->zlog_client->private_data = chip;
#endif
	} else {
		sqc_ops = kzalloc(sizeof(struct sqc_pmic_chg_ops), GFP_KERNEL);
		memcpy(sqc_ops, &bq25970_chg_ops, sizeof(struct sqc_pmic_chg_ops));
		sqc_ops->arg = (void *)chip;
		ret = sqc_hal_charger_register(sqc_ops, SQC_CHARGER_CP1);
		if (ret < 0) {
			bq_err("%s register sqc hal fail(%d)\n", __func__, ret);
			goto bq25970_fail_2;
		}

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		chip->zlog_client = zlog_register_client(&zlog_cp_dev1);
		if (!chip->zlog_client)
			bq_err("%s zlog register client zlog_cp_dev1 fail\n", __func__);
		chip->zlog_client->private_data = chip;
#endif
	}

	if (chip->hw_mode == BQ25970_STDALONE) {
		cp1_init_finished = 1;
		cp2_init_finished = 1;
		zte_misc_register_callback(&slave_chg_id_node, chip);
	} else if (chip->hw_mode == BQ25970_MASTER) {
		cp1_init_finished = 1;
		zte_misc_register_callback(&slave_chg_id_node, chip);
	} else if (chip->hw_mode == BQ25970_SLAVE) {
		cp2_init_finished = 1;
	} else {
		cp1_init_finished = 0;
		cp2_init_finished = 0;
	}

	bq_info("%s hw_mode:%d probe end\n", __func__, chip->hw_mode);
	return 0;

bq25970_fail_2:
	free_irq(chip->irq_int, chip);
bq25970_fail_1:
	gpio_free(chip->gpio_int);
bq25970_fail_0:
	devm_kfree(&client->dev, chip);
	np = NULL;
	return ret;
}

static int bq25970_remove(struct i2c_client *client)
{
	struct bq25970_device *chip = i2c_get_clientdata(client);


	if (chip->irq_int)
		free_irq(chip->irq_int, chip);

	if (chip->gpio_int)
		gpio_free(chip->gpio_int);

	return 0;
}

static void bq25970_shutdown(struct i2c_client *client)
{
	struct bq25970_device *chip = i2c_get_clientdata(client);

	bq25970_reg_reset(chip);
}

MODULE_DEVICE_TABLE(i2c, bq25970);

static struct i2c_driver bq25970_driver = {
	.probe = bq25970_probe,
	.remove = bq25970_remove,
	.shutdown = bq25970_shutdown,
	.id_table = bq25970_i2c_id,
	.driver = {
		.owner = THIS_MODULE,
		.name = "bq25970",
		.of_match_table = of_match_ptr(bq25970_of_match),
	},
};

static int __init bq25970_init(void)
{
	int ret = 0;

	ret = i2c_add_driver(&bq25970_driver);
	if (ret)
		pr_err("i2c_add_driver error\n");

	return ret;
}

static void __exit bq25970_exit(void)
{
	i2c_del_driver(&bq25970_driver);
}

module_init(bq25970_init);
module_exit(bq25970_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("bq25970 module driver");
MODULE_AUTHOR("ztecharger Technologies Co., Ltd.");
