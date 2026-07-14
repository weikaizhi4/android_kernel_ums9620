/*
 * Copyright (C) 2019 MediaTek Inc.
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/gpio/consumer.h>

#include <sqc_common.h>
#include "sqc_netlink.h"


/* Information */
#define RT9759_DRV_VERSION	"1.0.7_MTK"
#define RT9759_DEVID		0x08

/* Registers */
#define RT9759_REG_VBATOVP	0x00
#define RT9759_REG_VBATOVP_ALM	0x01
#define RT9759_REG_IBATOCP	0x02
#define RT9759_REG_IBATOCP_ALM	0x03
#define RT9759_REG_IBATUCP_ALM	0x04
#define RT9759_REG_ACPROTECT	0x05
#define RT9759_REG_VBUSOVP	0x06
#define RT9759_REG_VBUSOVP_ALM	0x07
#define RT9759_REG_IBUSOCUCP	0x08
#define RT9759_REG_IBUSOCP_ALM	0x09
#define RT9759_REG_CONVSTAT	0x0A
#define RT9759_REG_CHGCTRL0	0x0B
#define RT9759_REG_CHGCTRL1	0x0C
#define RT9759_REG_INTSTAT	0x0D
#define RT9759_REG_INTFLAG	0x0E
#define RT9759_REG_INTMASK	0x0F
#define RT9759_REG_FLTSTAT	0x10
#define RT9759_REG_FLTFLAG	0x11
#define RT9759_REG_FLTMASK	0x12
#define RT9759_REG_DEVINFO	0x13
#define RT9759_REG_ADCCTRL	0x14
#define RT9759_REG_ADCEN	0x15
#define RT9759_REG_IBUSADC1	0x16
#define RT9759_REG_IBUSADC0	0x17
#define RT9759_REG_VBUSADC1	0x18
#define RT9759_REG_VBUSADC0	0x19
#define RT9759_REG_VACADC1	0x1A
#define RT9759_REG_VACADC0	0x1B
#define RT9759_REG_VOUTADC1	0x1C
#define RT9759_REG_VOUTADC0	0x1D
#define RT9759_REG_VBATADC1	0x1E
#define RT9759_REG_VBATADC0	0x1F
#define RT9759_REG_IBATADC1	0x20
#define RT9759_REG_IBATADC0	0x21
#define RT9759_REG_TSBUSADC1	0x22
#define RT9759_REG_TSBUSADC0	0x23
#define RT9759_REG_TSBATADC1	0x24
#define RT9759_REG_TSBATADC0	0x25
#define RT9759_REG_TDIEADC1	0x26
#define RT9759_REG_TDIEADC0	0x27
#define RT9759_REG_TSBUSOTP	0x28
#define RT9759_REG_TSBATOTP	0x29
#define RT9759_REG_TDIEALM	0x2A
#define RT9759_REG_REGCTRL	0x2B
#define RT9759_REG_REGTHRES	0x2C
#define RT9759_REG_REGFLAGMASK	0x2D
#define RT9759_REG_BUSDEGLH	0x2E
#define RT9759_REG_OTHER1	0x30
#define RT9759_REG_SYSCTRL1	0x42
#define RT9759_REG_PASSWORD0	0x90
#define RT9759_REG_PASSWORD1	0x91

/* Control bits */
#define RT9759_CHGEN_MASK	BIT(7)

#define RT9759_ADCEN_MASK	BIT(7)
#define RT9759_WDTEN_MASK	BIT(2)

#define RT9759_VACOVP_MASK	0x07

#define RT9759_VBUSOVP_MASK	0x7F
#define RT9759_VBUSOVP_ALM_MASK	0x7F

#define RT9759_IBUSOCP_MASK	0x0F
#define RT9759_IBUSOCP_ALM_MASK	0x7F

#define RT9759_VBATOVP_MASK	0x3F
#define RT9759_VBATOVP_ALM_MASK	0x3F

#define RT9759_IBATOCP_MASK	0x7F
#define RT9759_IBATUCP_MASK	0x7F

#define RT9759_VBATOVP_ALMDIS_MASK	BIT(7)

#define RT9759_VBUSOVP_ALMDIS_MASK	BIT(7)

/*shift*/
#define RT9759_CHGEN_SHFT	7
#define RT9759_VBUSOVP_SHFT 0
#define RT9759_VBUSOVP_ALM_SHFT 0
#define RT9759_IBUSOVP_SHFT 0
#define RT9759_IBUSOVP_ALM_SHFT 0

#define RT9759_VABTOVP_SHFT 0
#define RT9759_VABTOVP_ALM_SHFT 0

#define RT9759_IABTOCP_SHFT 0
#define RT9759_IABTOCP_ALM_SHFT 0

#define RT9759_IABTUCP_SHFT 0
#define RT9759_IABTUCP_ALM_SHFT 0


#define RT9759_VACOVP_SHFT 0

#define RT9759_VBUSLOWERR_FLAG_SHFT	2
#define RT9759_VBUSLOWERR_STAT_SHFT	5

enum rt9759_irqidx {
	RT9759_IRQIDX_VACOVP = 0,
	RT9759_IRQIDX_IBUSUCPF,
	RT9759_IRQIDX_IBUSUCPR,
	RT9759_IRQIDX_CFLYDIAG,
	RT9759_IRQIDX_CONOCP,
	RT9759_IRQIDX_SWITCHING,
	RT9759_IRQIDX_IBUSUCPTOUT,
	RT9759_IRQIDX_VBUSHERR,
	RT9759_IRQIDX_VBUSLERR,
	RT9759_IRQIDX_TDIEOTP,
	RT9759_IRQIDX_WDT,
	RT9759_IRQIDX_ADCDONE,
	RT9759_IRQIDX_VOUTINSERT,
	RT9759_IRQIDX_VACINSERT,
	RT9759_IRQIDX_IBATUCPALM,
	RT9759_IRQIDX_IBUSOCPALM,
	RT9759_IRQIDX_VBUSOVPALM,
	RT9759_IRQIDX_IBATOCPALM,
	RT9759_IRQIDX_VBATOVPALM,
	RT9759_IRQIDX_TDIEOTPALM,
	RT9759_IRQIDX_TSBUSOTP,
	RT9759_IRQIDX_TSBATOTP,
	RT9759_IRQIDX_TSBUSBATOTPALM,
	RT9759_IRQIDX_IBUSOCP,
	RT9759_IRQIDX_VBUSOVP,
	RT9759_IRQIDX_IBATOCP,
	RT9759_IRQIDX_VBATOVP,
	RT9759_IRQIDX_VOUTOVP,
	RT9759_IRQIDX_VDROVP,
	RT9759_IRQIDX_IBATREG,
	RT9759_IRQIDX_VBATREG,
	RT9759_IRQIDX_MAX,
};

enum rt9759_notify {
	RT9759_NOTIFY_IBUSUCPF = 0,
	RT9759_NOTIFY_VBUSOVPALM,
	RT9759_NOTIFY_VBATOVPALM,
	RT9759_NOTIFY_IBUSOCP,
	RT9759_NOTIFY_VBUSOVP,
	RT9759_NOTIFY_IBATOCP,
	RT9759_NOTIFY_VBATOVP,
	RT9759_NOTIFY_VOUTOVP,
	RT9759_NOTIFY_VDROVP,
	RT9759_NOTIFY_MAX,
};

enum rt9759_statflag_idx {
	RT9759_SF_ACPROTECT = 0,
	RT9759_SF_IBUSOCUCP,
	RT9759_SF_CONVSTAT,
	RT9759_SF_CHGCTRL0,
	RT9759_SF_INTFLAG,
	RT9759_SF_INTSTAT,
	RT9759_SF_FLTFLAG,
	RT9759_SF_FLTSTAT,
	RT9759_SF_REGFLAGMASK,
	RT9759_SF_REGTHRES,
	RT9759_SF_OTHER1,
	RT9759_SF_MAX,
};

enum rt9759_type {
	RT9759_TYPE_STANDALONE = 0,
	RT9759_TYPE_SLAVE,
	RT9759_TYPE_MASTER,
	RT9759_TYPE_MAX,
};

static const char *rt9759_type_name[RT9759_TYPE_MAX] = {
	"standalone", "slave", "master",
};

static const u8 rt9759_reg_sf[RT9759_SF_MAX] = {
	RT9759_REG_ACPROTECT,
	RT9759_REG_IBUSOCUCP,
	RT9759_REG_CONVSTAT,
	RT9759_REG_CHGCTRL0,
	RT9759_REG_INTFLAG,
	RT9759_REG_INTSTAT,
	RT9759_REG_FLTFLAG,
	RT9759_REG_FLTSTAT,
	RT9759_REG_REGFLAGMASK,
	RT9759_REG_REGTHRES,
	RT9759_REG_OTHER1,
};

struct rt9759_desc {
	const char *chg_name;
	const char *rm_name;
	u8 rm_slave_addr;
	u32 vbatovp;
	u32 vbatovp_alm;
	u32 ibatocp;
	u32 ibatocp_alm;
	u32 ibatucp_alm;
	u32 vbusovp;
	u32 vbusovp_alm;
	u32 ibusocp;
	u32 ibusocp_alm;
	u32 vacovp;
	u32 wdt;
	u32 ibat_rsense;
	u32 ibusucpf_deglitch;
	bool vbatovp_dis;
	bool vbatovp_alm_dis;
	bool ibatocp_dis;
	bool ibatocp_alm_dis;
	bool ibatucp_alm_dis;
	bool vbusovp_alm_dis;
	bool ibusocp_dis;
	bool ibusocp_alm_dis;
	bool wdt_dis;
	bool tsbusotp_dis;
	bool tsbatotp_dis;
	bool tdieotp_dis;
	bool reg_en;
	bool voutovp_dis;
	bool ibusadc_dis;
	bool vbusadc_dis;
	bool vacadc_dis;
	bool voutadc_dis;
	bool vbatadc_dis;
	bool ibatadc_dis;
	bool tsbusadc_dis;
	bool tsbatadc_dis;
	bool tdieadc_dis;
};

static const struct rt9759_desc rt9759_desc_defval = {
	.chg_name = "divider_charger",
	.rm_name = "rt9759",
	.rm_slave_addr = 0x66,
	.vbatovp = 4350000,
	.vbatovp_alm = 4200000,
	.ibatocp = 8100000,
	.ibatocp_alm = 8000000,
	.ibatucp_alm = 2000000,
	.vbusovp = 8900000,
	.vbusovp_alm = 8800000,
	.ibusocp = 4250000,
	.ibusocp_alm = 4000000,
	.vacovp = 11000000,
	.wdt = 500000,
	.ibat_rsense = 0,	/* 2mohm */
	.ibusucpf_deglitch = 0,	/* 10us */
	.vbatovp_dis = false,
	.vbatovp_alm_dis = false,
	.ibatocp_dis = false,
	.ibatocp_alm_dis = false,
	.ibatucp_alm_dis = false,
	.vbusovp_alm_dis = false,
	.ibusocp_dis = false,
	.ibusocp_alm_dis = false,
	.wdt_dis = false,
	.tsbusotp_dis = false,
	.tsbatotp_dis = false,
	.tdieotp_dis = false,
	.reg_en = false,
	.voutovp_dis = false,
};

struct rt9759_chip {
	struct device *dev;
	struct i2c_client *client;
	struct mutex io_lock;
	struct mutex adc_lock;
	struct mutex stat_lock;
	struct mutex hm_lock;
	struct mutex suspend_lock;
	struct rt9759_desc *desc;
	struct gpio_desc *irq_gpio;
	int irq;
	u8 revision;
	u32 flag;
	u32 stat;
	u32 hm_cnt;
	enum rt9759_type type;
	bool wdt_en;
	bool force_adc_en;
	struct workqueue_struct	*status_update_queue;
	struct delayed_work		status_update_work;

};

enum rt9759_adc_channel {
	RT9759_ADC_IBUS = 0,
	RT9759_ADC_VBUS,
	RT9759_ADC_VAC,
	RT9759_ADC_VOUT,
	RT9759_ADC_VBAT,
	RT9759_ADC_IBAT,
	RT9759_ADC_TSBUS,
	RT9759_ADC_TSBAT,
	RT9759_ADC_TDIE,
	RT9759_ADC_MAX,
	RT9759_ADC_NOTSUPP = RT9759_ADC_MAX,
};

static const u8 rt9759_adc_reg[RT9759_ADC_MAX] = {
	RT9759_REG_IBUSADC1,
	RT9759_REG_VBUSADC1,
	RT9759_REG_VACADC1,
	RT9759_REG_VOUTADC1,
	RT9759_REG_VBATADC1,
	RT9759_REG_IBATADC1,
	RT9759_REG_TSBUSADC1,
	RT9759_REG_TSBATADC1,
	RT9759_REG_TDIEADC1,
};

static const char *rt9759_adc_name[RT9759_ADC_MAX] = {
	"Ibus", "Vbus", "VAC", "Vout", "Vbat", "Ibat", "TSBus", "TSBat", "TDie",
};

static const u32 rt9759_adc_accuracy_tbl[RT9759_ADC_MAX] = {
	150000,	/* IBUS */
	35000,	/* VBUS */
	35000,	/* VAC */
	20000,	/* VOUT */
	20000,	/* VBAT */
	200000,	/* IBAT */
	1,	/* TSBUS */
	1,	/* TSBAT */
	4,	/* TDIE */
};

static int rt9759_read_device(void *client, u32 addr, int len, void *dst)
{
	int ret;
	struct i2c_client *i2c = (struct i2c_client *)client;
	struct rt9759_chip *chip = i2c_get_clientdata(i2c);

	pm_stay_awake(chip->dev);
	mutex_lock(&chip->suspend_lock);
	ret = i2c_smbus_read_i2c_block_data(i2c, addr, len, dst);
	mutex_unlock(&chip->suspend_lock);
	pm_relax(chip->dev);
	return ret;
}

static int rt9759_write_device(void *client, u32 addr, int len, const void *src)
{
	int ret;
	struct i2c_client *i2c = (struct i2c_client *)client;
	struct rt9759_chip *chip = i2c_get_clientdata(i2c);

	pm_stay_awake(chip->dev);
	mutex_lock(&chip->suspend_lock);
	ret = i2c_smbus_write_i2c_block_data(i2c, addr, len, src);
	mutex_unlock(&chip->suspend_lock);
	pm_relax(chip->dev);
	return ret;
}

#define I2C_ACCESS_MAX_RETRY	5
static inline int __rt9759_i2c_write8(struct rt9759_chip *chip, u8 reg, u8 data)
{
	int ret, retry = 0;

	do {
		ret = rt9759_write_device(chip->client, reg, 1, &data);

		retry++;
		if (ret < 0)
			usleep_range(10, 15);
	} while (ret < 0 && retry < I2C_ACCESS_MAX_RETRY);

	if (ret < 0) {
		dev_notice(chip->dev, "%s I2CW[0x%02X] = 0x%02X fail\n",
			   __func__, reg, data);
		return ret;
	}
	dev_dbg_ratelimited(chip->dev, "%s I2CW[0x%02X] = 0x%02X\n", __func__,
			    reg, data);
	return 0;
}
/*
static int rt9759_i2c_write8(struct rt9759_chip *chip, u8 reg, u8 data)
{
	int ret;

	mutex_lock(&chip->io_lock);
	ret = __rt9759_i2c_write8(chip, reg, data);
	mutex_unlock(&chip->io_lock);

	return ret;
}
*/
static inline int __rt9759_i2c_read8(struct rt9759_chip *chip, u8 reg, u8 *data)
{
	int ret, retry = 0;

	do {
		ret = rt9759_read_device(chip->client, reg, 1, data);
		retry++;
		if (ret < 0)
			usleep_range(10, 15);
	} while (ret < 0 && retry < I2C_ACCESS_MAX_RETRY);

	if (ret < 0) {
		dev_notice(chip->dev, "%s I2CR[0x%02X] fail\n", __func__, reg);
		return ret;
	}
	dev_dbg_ratelimited(chip->dev, "%s I2CR[0x%02X] = 0x%02X\n", __func__,
			    reg, *data);
	return 0;
}

static int rt9759_i2c_read8(struct rt9759_chip *chip, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&chip->io_lock);
	ret = __rt9759_i2c_read8(chip, reg, data);
	mutex_unlock(&chip->io_lock);

	return ret;
}

static inline int __rt9759_i2c_write_block(struct rt9759_chip *chip, u8 reg,
		u32 len, const u8 *data)
{
	int ret;

	ret = rt9759_write_device(chip->client, reg, len, data);

	return ret;
}
/*
static int rt9759_i2c_write_block(struct rt9759_chip *chip, u8 reg, u32 len,
				  const u8 *data)
{
	int ret;

	mutex_lock(&chip->io_lock);
	ret = __rt9759_i2c_write_block(chip, reg, len, data);
	mutex_unlock(&chip->io_lock);

	return ret;
}
*/
static inline int __rt9759_i2c_read_block(struct rt9759_chip *chip, u8 reg,
		u32 len, u8 *data)
{
	int ret;

	ret = rt9759_read_device(chip->client, reg, len, data);

	return ret;
}

static int rt9759_i2c_read_block(struct rt9759_chip *chip, u8 reg, u32 len,
				 u8 *data)
{
	int ret;

	mutex_lock(&chip->io_lock);
	ret = __rt9759_i2c_read_block(chip, reg, len, data);
	mutex_unlock(&chip->io_lock);

	return ret;
}

static int rt9759_i2c_test_bit(struct rt9759_chip *chip, u8 reg, u8 shft,
			       bool *one)
{
	int ret;
	u8 data;

	ret = rt9759_i2c_read8(chip, reg, &data);
	if (ret < 0) {
		*one = false;
		return ret;
	}

	*one = (data & (1 << shft)) ? true : false;
	return 0;
}

static int rt9759_i2c_update_bits(struct rt9759_chip *chip, u8 reg, u8 data,
				  u8 mask)
{
	int ret;
	u8 _data;

	mutex_lock(&chip->io_lock);
	ret = __rt9759_i2c_read8(chip, reg, &_data);
	if (ret < 0)
		goto out;
	_data &= ~mask;
	_data |= (data & mask);
	ret = __rt9759_i2c_write8(chip, reg, _data);
out:
	mutex_unlock(&chip->io_lock);
	return ret;
}

static inline int rt9759_set_bits(struct rt9759_chip *chip, u8 reg, u8 mask)
{
	return rt9759_i2c_update_bits(chip, reg, mask, mask);
}

static inline int rt9759_clr_bits(struct rt9759_chip *chip, u8 reg, u8 mask)
{
	return rt9759_i2c_update_bits(chip, reg, 0x00, mask);
}

static inline u8 rt9759_val_toreg(u32 min, u32 max, u32 step, u32 target,
				  bool ru)
{
	if (target <= min)
		return 0;

	if (target >= max)
		return (max - min) / step;

	if (ru)
		return (target - min + step) / step;
	return (target - min) / step;
}

static inline u32 rt9759_reg_toval(u8 reg_val, u32 min, u32 max, u32 step)
{
	u32 real_val = 0;

	real_val = reg_val * step + min;

	return (real_val < max) ? real_val : max;
}


static inline u8 rt9759_val_toreg_via_tbl(const u32 *tbl, int tbl_size,
		u32 target)
{
	int i;

	if (target < tbl[0])
		return 0;

	for (i = 0; i < tbl_size - 1; i++) {
		if (target >= tbl[i] && target < tbl[i + 1])
			return i;
	}

	return tbl_size - 1;
}

static u8 rt9759_vbatovp_toreg(u32 mV)
{
	return rt9759_val_toreg(3500, 5075, 25, mV, false);
}

static u32 rt9759_vbatovp_toval(u32 reg)
{
	return rt9759_reg_toval(reg, 3500, 5075, 25);
}

static u8 rt9759_ibatocp_toreg(u32 mA)
{
	return rt9759_val_toreg(2000, 10000, 100, mA, false);
}

static u32 rt9759_ibatocp_toval(u32 reg)
{
	return rt9759_reg_toval(reg, 2000, 10000, 100);
}

static u8 rt9759_ibatucp_toreg(u32 mA)
{
	return rt9759_val_toreg(0, 6350, 50, mA, false);
}

static u32 rt9759_ibatucp_toval(u32 reg)
{
	return rt9759_reg_toval(reg, 0, 6350, 50);
}

static u8 rt9759_vbusovp_toreg(u32 mV)
{
	return rt9759_val_toreg(6000, 12350, 50, mV, false);
}

static u32 rt9759_vbusovp_toval(u32 reg)
{
	return rt9759_reg_toval(reg, 6000, 12350, 50);
}

static u8 rt9759_ibusocp_toreg(u32 mA)
{
	return rt9759_val_toreg(1000, 4750, 250, mA, false);
}

static u32 rt9759_ibusocp_toval(u32 reg)
{
	return rt9759_reg_toval(reg, 1000, 4750, 250);
}

static u8 rt9759_ibusocp_alm_toreg(u32 mA)
{
	return rt9759_val_toreg(0, 6350, 50, mA, false);
}

static u32 rt9759_ibusocp_alm_toval(u32 reg)
{
	return rt9759_reg_toval(reg, 0, 6350, 50);
}

static u8 rt9759_vacovp_toreg(u32 mV)
{
	if (mV < 11000)
		return 0x07;
	return rt9759_val_toreg(11000, 17000, 1000, mV, false);
}

static u32 rt9759_vacovp_toval(u32 reg)
{
	return rt9759_reg_toval(reg, 11000, 17000, 1000);
}


static const u32 rt9759_wdt[] = {
	500000, 1000000, 5000000, 3000000,
};

static u8 rt9759_wdt_toreg(u32 uS)
{
	return rt9759_val_toreg_via_tbl(rt9759_wdt, ARRAY_SIZE(rt9759_wdt), uS);
}



/* Must be called while holding a lock */
static int rt9759_enable_wdt(struct rt9759_chip *chip, bool en)
{
	int ret;

	if (chip->wdt_en == en)
		return 0;
	ret = (en ? rt9759_clr_bits : rt9759_set_bits)
	      (chip, RT9759_REG_CHGCTRL0, RT9759_WDTEN_MASK);
	if (ret < 0)
		return ret;
	chip->wdt_en = en;
	return 0;
}

static int __rt9759_get_adc(struct rt9759_chip *chip,
			    enum rt9759_adc_channel chan, int *val)
{
	int ret = 0;
	u8 data[2];

	ret = rt9759_set_bits(chip, RT9759_REG_ADCCTRL, RT9759_ADCEN_MASK);
	if (ret < 0)
		goto out;
	usleep_range(12000, 15000);
	ret = rt9759_i2c_read_block(chip, rt9759_adc_reg[chan], 2, data);
	if (ret < 0)
		goto out_dis;
	switch (chan) {
	case RT9759_ADC_IBUS:
	case RT9759_ADC_VBUS:
	case RT9759_ADC_VAC:
	case RT9759_ADC_VOUT:
	case RT9759_ADC_VBAT:
	case RT9759_ADC_IBAT:
		*val = ((data[0] << 8) + data[1]);
		break;
	case RT9759_ADC_TDIE:
		*val = (data[0] << 7) + (data[1] >> 1);
		break;
	case RT9759_ADC_TSBAT:
	case RT9759_ADC_TSBUS:
	default:
		ret = -ENOTSUPP;
		break;
	}
	if (ret < 0)
		dev_notice(chip->dev, "%s %s fail(%d)\n", __func__,
			   rt9759_adc_name[chan], ret);

out_dis:
	if (!chip->force_adc_en)
		ret = rt9759_clr_bits(chip, RT9759_REG_ADCCTRL,
				      RT9759_ADCEN_MASK);
out:
	return ret;
}

static int  rt9759_get_ibus(void *arg, unsigned int *mA)
{
	int ret;
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;

	ret = __rt9759_get_adc(chip, RT9759_ADC_IBUS, mA);

	return ret;
}

static int  rt9759_get_vbus(void *arg, unsigned int *mV)
{
	int ret;
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;

	ret = __rt9759_get_adc(chip, RT9759_ADC_VBUS, mV);

	return ret;
}

static int  rt9759_get_ibat(void *arg, unsigned int *mA)
{
	int ret;
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;

	ret = __rt9759_get_adc(chip, RT9759_ADC_IBAT, mA);

	return ret;
}

static int  rt9759_get_vbat(void *arg, unsigned int *mV)
{
	int ret;
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;

	ret = __rt9759_get_adc(chip, RT9759_ADC_VBAT, mV);

	return ret;
}

static int rt9759_get_interrupt_status(void *arg, unsigned int *status)
{
	int ret;
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;

	*status = chip->stat;

	dev_info(chip->dev, "%s 0x%04X\n", __func__, *status);

	return ret;
}

static int rt9759_init_reg(void *arg)
{
	return 0;
}

static int rt9759_set_enable_chg(void *arg, unsigned int en)
{
	int ret;
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;

	dev_info(chip->dev, "%s %d\n", __func__, en);

	mutex_lock(&chip->adc_lock);
	chip->force_adc_en = en;
	if (!en) {
		ret = rt9759_clr_bits(chip, RT9759_REG_CHGCTRL1,
				      RT9759_CHGEN_MASK);
		if (ret < 0)
			goto out_unlock;
		ret = rt9759_clr_bits(chip, RT9759_REG_ADCCTRL,
				      RT9759_ADCEN_MASK);
		if (ret < 0)
			goto out_unlock;
		ret = rt9759_enable_wdt(chip, false);
		goto out_unlock;
	}
	/* Enable ADC to check status before enable charging */
	ret = rt9759_set_bits(chip, RT9759_REG_ADCCTRL, RT9759_ADCEN_MASK);
	if (ret < 0)
		goto out_unlock;
	mutex_unlock(&chip->adc_lock);

	if (!chip->desc->wdt_dis) {
		ret = rt9759_enable_wdt(chip, true);
		if (ret < 0)
			goto out;
	}
	ret = rt9759_set_bits(chip, RT9759_REG_CHGCTRL1, RT9759_CHGEN_MASK);
	goto out;
out_unlock:
	mutex_unlock(&chip->adc_lock);
out:
	return ret;
}

static int rt9759_get_enable_chg(void *arg, unsigned int *en)
{
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;
	int ret;
	bool enable = 0;

	ret = rt9759_i2c_test_bit(chip, RT9759_REG_CHGCTRL1, RT9759_CHGEN_SHFT,
				  &enable);
	*en = enable;

	dev_info(chip->dev, "%s %d, ret(%d)\n", __func__, *en, ret);

	return ret;
}

static int rt9759_set_vbusovp(void *arg, u32 mV)
{
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;
	u8 reg = rt9759_vbusovp_toreg(mV);

	dev_info(chip->dev, "%s %d(0x%02X)\n", __func__, mV, reg);
	return rt9759_i2c_update_bits(chip, RT9759_REG_VBUSOVP, reg,
				      RT9759_VBUSOVP_MASK);
}

static int rt9759_get_vbusovp(void *arg, u32 *mV)
{
	int ret;
	u8 reg_data = 0;
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;

	ret = rt9759_i2c_read8(chip, RT9759_REG_VBUSOVP, &reg_data);

	reg_data = (reg_data & RT9759_VBUSOVP_MASK) >> RT9759_VBUSOVP_SHFT;

	*mV = rt9759_vbusovp_toval(reg_data);

	dev_info(chip->dev, "%s %d, ret(%d)\n", __func__, *mV, ret);

	return ret;
}

static int rt9759_set_vbusovp_alarm(void *arg, u32 mV)
{
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;
	u8 reg = rt9759_vbusovp_toreg(mV);

	dev_info(chip->dev, "%s %d\n", __func__, mV);
	return rt9759_i2c_update_bits(chip, RT9759_REG_VBUSOVP_ALM, reg,
				      RT9759_VBUSOVP_ALM_MASK);
}

static int rt9759_get_vbusovp_alarm(void *arg, u32 *mV)
{
	int ret;
	u8 reg_data = 0;
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;

	ret = rt9759_i2c_read8(chip, RT9759_REG_VBUSOVP_ALM, &reg_data);

	reg_data = (reg_data & RT9759_VBUSOVP_ALM_MASK) >> RT9759_VBUSOVP_ALM_SHFT;

	*mV = rt9759_vbusovp_toval(reg_data);

	dev_info(chip->dev, "%s %d, ret(%d)\n", __func__, *mV, ret);

	return ret;
}


static int rt9759_set_ibusocp(void *arg, u32 mA)
{
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;
	u8 reg = rt9759_ibusocp_toreg(mA);

	dev_info(chip->dev, "%s %d(0x%02X)\n", __func__, mA, reg);
	return rt9759_i2c_update_bits(chip, RT9759_REG_IBUSOCUCP, reg,
				      RT9759_IBUSOCP_MASK);
}

static int rt9759_get_ibusocp(void *arg, u32 *mA)
{
	int ret;
	u8 reg_data = 0;
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;

	ret = rt9759_i2c_read8(chip, RT9759_REG_IBUSOCUCP, &reg_data);

	reg_data = (reg_data & RT9759_IBUSOCP_MASK) >> RT9759_IBUSOVP_SHFT;

	*mA = rt9759_ibusocp_toval(reg_data);

	dev_info(chip->dev, "%s %d, ret(%d)\n", __func__, *mA, ret);

	return ret;
}

static int rt9759_set_ibusocp_alarm(void *arg, u32 mA)
{
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;
	u8 reg = rt9759_ibusocp_alm_toreg(mA);

	dev_info(chip->dev, "%s %d(0x%02X)\n", __func__, mA, reg);
	return rt9759_i2c_update_bits(chip, RT9759_REG_IBUSOCP_ALM, reg,
				      RT9759_IBUSOCP_ALM_MASK);
}

static int rt9759_get_ibusocp_alarm(void *arg, u32 *mA)
{
	int ret;
	u8 reg_data = 0;
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;

	ret = rt9759_i2c_read8(chip, RT9759_REG_IBUSOCP_ALM, &reg_data);

	reg_data = (reg_data & RT9759_IBUSOCP_ALM_MASK) >> RT9759_IBUSOVP_ALM_SHFT;

	*mA = rt9759_ibusocp_alm_toval(reg_data);

	dev_info(chip->dev, "%s %d, ret(%d)\n", __func__, *mA, ret);

	return ret;
}

static int rt9759_set_vbatovp(void *arg, unsigned int mV)
{
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;
	u8 reg = rt9759_vbatovp_toreg(mV);

	dev_info(chip->dev, "%s %d(0x%02X)\n", __func__, mV, reg);
	return rt9759_i2c_update_bits(chip, RT9759_REG_VBATOVP, reg,
				      RT9759_VBATOVP_MASK);
}

static int rt9759_get_vbatovp(void *arg, unsigned int *mV)
{
	int ret;
	u8 reg_data = 0;
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;

	ret = rt9759_i2c_read8(chip, RT9759_REG_VBATOVP, &reg_data);

	reg_data = (reg_data & RT9759_VBATOVP_MASK) >> RT9759_VABTOVP_SHFT;

	*mV = rt9759_vbatovp_toval(reg_data);

	dev_info(chip->dev, "%s %d, ret(%d)\n", __func__, *mV, ret);

	return ret;
}

static int rt9759_set_vbatovp_alarm(void *arg, unsigned int mV)
{
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;
	u8 reg = rt9759_vbatovp_toreg(mV);

	dev_info(chip->dev, "%s %d\n", __func__, mV);
	return rt9759_i2c_update_bits(chip, RT9759_REG_VBATOVP_ALM, reg,
				      RT9759_VBATOVP_ALM_MASK);
}

static int rt9759_get_vbatovp_alarm(void *arg, unsigned int *mV)
{
	int ret;
	u8 reg_data = 0;
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;

	ret = rt9759_i2c_read8(chip, RT9759_REG_VBATOVP_ALM, &reg_data);

	reg_data = (reg_data & RT9759_VBATOVP_ALM_MASK) >> RT9759_VABTOVP_ALM_SHFT;

	*mV = rt9759_vbatovp_toval(reg_data);

	dev_info(chip->dev, "%s %d, ret(%d)\n", __func__, *mV, ret);

	return ret;
}

static int rt9759_set_ibatocp(void *arg, unsigned int mA)
{
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;
	u8 reg = rt9759_ibatocp_toreg(mA);

	dev_info(chip->dev, "%s %d(0x%02X)\n", __func__, mA, reg);
	return rt9759_i2c_update_bits(chip, RT9759_REG_IBATOCP, reg,
				      RT9759_IBATOCP_MASK);
}

static int rt9759_get_ibatocp(void *arg, unsigned int *mA)
{
	int ret;
	u8 reg_data = 0;
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;

	ret = rt9759_i2c_read8(chip, RT9759_REG_IBATOCP, &reg_data);

	reg_data = (reg_data & RT9759_IBATOCP_MASK) >> RT9759_IABTOCP_SHFT;

	*mA = rt9759_ibatocp_toval(reg_data);

	dev_info(chip->dev, "%s %d, ret(%d)\n", __func__, *mA, ret);

	return ret;
}

static int rt9759_set_ibatocp_alarm(void *arg, unsigned int mA)
{
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;
	u8 reg = rt9759_ibatocp_toreg(mA);

	dev_info(chip->dev, "%s %d(0x%02X)\n", __func__, mA, reg);
	return rt9759_i2c_update_bits(chip, RT9759_REG_IBATOCP_ALM, reg,
				      RT9759_IBATOCP_MASK);
}

static int rt9759_get_ibatocp_alarm(void *arg, unsigned int *mA)
{
	int ret;
	u8 reg_data = 0;
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;

	ret = rt9759_i2c_read8(chip, RT9759_REG_IBATOCP_ALM, &reg_data);

	reg_data = (reg_data & RT9759_IBATOCP_MASK) >> RT9759_IABTOCP_ALM_SHFT;

	*mA = rt9759_ibatocp_toval(reg_data);

	dev_info(chip->dev, "%s %d, ret(%d)\n", __func__, *mA, ret);

	return ret;
}

static int rt9759_set_ibatucp_alarm(void *arg, unsigned int mA)
{
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;
	u8 reg = rt9759_ibatucp_toreg(mA);

	dev_info(chip->dev, "%s %d(0x%02X)\n", __func__, mA, reg);
	return rt9759_i2c_update_bits(chip, RT9759_REG_IBATUCP_ALM, reg,
				      RT9759_IBATUCP_MASK);
}

static int rt9759_get_ibatucp_alarm(void *arg, unsigned int *mA)
{
	int ret;
	u8 reg_data = 0;
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;

	ret = rt9759_i2c_read8(chip, RT9759_REG_IBATUCP_ALM, &reg_data);

	reg_data = (reg_data & RT9759_IBATUCP_MASK) >> RT9759_IABTUCP_ALM_SHFT;

	*mA = rt9759_ibatucp_toval(reg_data);

	dev_info(chip->dev, "%s %d, ret(%d)\n", __func__, *mA, ret);

	return ret;
}


static int rt9759_set_vacovp(void *arg, unsigned int mV)
{
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;
	u8 reg = rt9759_vacovp_toreg(mV);

	dev_info(chip->dev, "%s %d(0x%02X)\n", __func__, mV, reg);
	return rt9759_i2c_update_bits(chip, RT9759_REG_ACPROTECT, reg,
				      RT9759_VACOVP_MASK);
}

static int rt9759_get_vacovp(void *arg, unsigned int *mV)
{
	int ret;
	u8 reg_data = 0;
	struct rt9759_chip *chip = (struct rt9759_chip *)arg;

	ret = rt9759_i2c_read8(chip, RT9759_REG_ACPROTECT, &reg_data);

	reg_data = (reg_data & RT9759_VACOVP_MASK) >> RT9759_VACOVP_SHFT;

	*mV = rt9759_vacovp_toval(reg_data);

	dev_info(chip->dev, "%s %d, ret(%d)\n", __func__, *mV, ret);

	return ret;
}

static irqreturn_t rt9759_irq_handler(int irq, void *data)
{
	struct rt9759_chip *chip = data;

	queue_delayed_work(chip->status_update_queue, &chip->status_update_work, msecs_to_jiffies(100));

	return IRQ_HANDLED;
}

static struct sqc_pmic_chg_ops rt9759_chg_ops = {

	.init_pmic_charger = rt9759_init_reg,

	.chg_enable = rt9759_set_enable_chg,
	.chg_enable_get = rt9759_get_enable_chg,

	.get_chg_status = NULL,
	.get_int_status = rt9759_get_interrupt_status,

	.chg_role_set = NULL,
	.chg_role_get = NULL,

	/*battery*/
	.batt_ovp_volt_set = rt9759_set_vbatovp,
	.batt_ovp_volt_get = rt9759_get_vbatovp,
	.batt_ovp_alm_volt_set = rt9759_set_vbatovp_alarm,
	.batt_ovp_alm_volt_get = rt9759_get_vbatovp_alarm,
	.batt_ocp_curr_set = rt9759_set_ibatocp,
	.batt_ocp_curr_get = rt9759_get_ibatocp,
	.batt_ocp_alm_curr_set = rt9759_set_ibatocp_alarm,
	.batt_ocp_alm_curr_get = rt9759_get_ibatocp_alarm,
	.batt_ucp_alm_curr_set = rt9759_set_ibatucp_alarm,
	.batt_ucp_alm_curr_get = rt9759_get_ibatucp_alarm,
	.batt_ibat_get = rt9759_get_ibat,
	.batt_vbat_get = rt9759_get_vbat,

	/*ac*/
	.ac_ovp_volt_set = rt9759_set_vacovp,
	.ac_ovp_volt_get = rt9759_get_vacovp,

	/*usb bus*/
	.usb_ovp_volt_set = rt9759_set_vbusovp,
	.usb_ovp_volt_get = rt9759_get_vbusovp,
	.usb_ovp_alm_volt_set = rt9759_set_vbusovp_alarm,
	.usb_ovp_alm_volt_get = rt9759_get_vbusovp_alarm,
	.usb_ocp_curr_set = rt9759_set_ibusocp,
	.usb_ocp_curr_get = rt9759_get_ibusocp,
	.usb_ocp_alm_curr_set = rt9759_set_ibusocp_alarm,
	.usb_ocp_alm_curr_get = rt9759_get_ibusocp_alarm,
	.usb_ibus_get = rt9759_get_ibus,
	.usb_vbus_get = rt9759_get_vbus,
};

static int rt9759_clearall_irq(struct rt9759_chip *chip)
{
	int i, ret;
	u8 data;

	for (i = 0; i < RT9759_SF_MAX; i++) {
		ret = rt9759_i2c_read8(chip, rt9759_reg_sf[i], &data);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int rt9759_init_irq(struct rt9759_chip *chip)
{
	int ret = 0, len = 0;
	char *name = NULL;

	dev_info(chip->dev, "%s\n", __func__);
	ret = rt9759_clearall_irq(chip);
	if (ret < 0) {
		dev_notice(chip->dev, "%s clr all irq fail(%d)\n",
			   __func__, ret);
		return ret;
	}
	if (chip->type == RT9759_TYPE_SLAVE)
		return 0;

	len = strlen(chip->desc->chg_name);
	chip->irq = gpiod_to_irq(chip->irq_gpio);
	if (chip->irq < 0) {
		dev_notice(chip->dev, "%s irq mapping fail(%d)\n", __func__,
			   chip->irq);
		return ret;
	}
	dev_info(chip->dev, "%s irq = %d\n", __func__, chip->irq);

	/* Request threaded IRQ */
	name = devm_kzalloc(chip->dev, len + 5, GFP_KERNEL);
	snprintf(name, len + 5, "%s_irq", chip->desc->chg_name);
	ret = devm_request_threaded_irq(chip->dev, chip->irq, NULL,
					rt9759_irq_handler, IRQF_TRIGGER_FALLING | IRQF_ONESHOT, name,
					chip);
	if (ret < 0) {
		dev_notice(chip->dev, "%s request thread irq fail(%d)\n",
			   __func__, ret);
		return ret;
	}
	device_init_wakeup(chip->dev, true);
	return 0;
}

#define RT9759_DT_VALPROP(name, reg, shft, mask, func, base) \
	{#name, offsetof(struct rt9759_desc, name), reg, shft, mask, func, base}

struct rt9759_dtprop {
	const char *name;
	size_t offset;
	u8 reg;
	u8 shft;
	u8 mask;
	u8(*toreg)(u32 val);
	u8 base;
};

static inline void rt9759_parse_dt_u32(struct device_node *np, void *desc,
				       const struct rt9759_dtprop *props,
				       int prop_cnt)
{
	int i;

	for (i = 0; i < prop_cnt; i++) {
		if (unlikely(!props[i].name))
			continue;
		of_property_read_u32(np, props[i].name, desc + props[i].offset);
	}
}

static inline void rt9759_parse_dt_bool(struct device_node *np, void *desc,
					const struct rt9759_dtprop *props,
					int prop_cnt)
{
	int i;

	for (i = 0; i < prop_cnt; i++) {
		if (unlikely(!props[i].name))
			continue;
		*((bool *)(desc + props[i].offset)) =
			of_property_read_bool(np, props[i].name);
	}
}

static inline int rt9759_apply_dt(struct rt9759_chip *chip, void *desc,
				  const struct rt9759_dtprop *props,
				  int prop_cnt)
{
	int i, ret;
	u32 val;

	for (i = 0; i < prop_cnt; i++) {
		val = *(u32 *)(desc + props[i].offset);
		if (props[i].toreg)
			val = props[i].toreg(val);
		val += props[i].base;
		ret = rt9759_i2c_update_bits(chip, props[i].reg,
					     val << props[i].shft,
					     props[i].mask);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static const struct rt9759_dtprop rt9759_dtprops_u32[] = {
	RT9759_DT_VALPROP(vbatovp, RT9759_REG_VBATOVP, 0, 0x3f,
	rt9759_vbatovp_toreg, 0),
	RT9759_DT_VALPROP(vbatovp_alm, RT9759_REG_VBATOVP_ALM, 0, 0x3f,
	rt9759_vbatovp_toreg, 0),
	RT9759_DT_VALPROP(ibatocp, RT9759_REG_IBATOCP, 0, 0x7f,
	rt9759_ibatocp_toreg, 0),
	RT9759_DT_VALPROP(ibatocp_alm, RT9759_REG_IBATOCP_ALM, 0, 0x7f,
	rt9759_ibatocp_toreg, 0),
	RT9759_DT_VALPROP(ibatucp_alm, RT9759_REG_IBATUCP_ALM, 0, 0x7f,
	rt9759_ibatucp_toreg, 0),
	RT9759_DT_VALPROP(vbusovp, RT9759_REG_VBUSOVP, 0, 0x7f,
	rt9759_vbusovp_toreg, 0),
	RT9759_DT_VALPROP(vbusovp_alm, RT9759_REG_VBUSOVP_ALM, 0, 0x7f,
	rt9759_vbusovp_toreg, 0),
	RT9759_DT_VALPROP(ibusocp, RT9759_REG_IBUSOCUCP, 0, 0x0f,
	rt9759_ibusocp_toreg, 0),
	RT9759_DT_VALPROP(ibusocp_alm, RT9759_REG_IBUSOCP_ALM, 0, 0x7f,
	rt9759_ibusocp_alm_toreg, 0),
	RT9759_DT_VALPROP(wdt, RT9759_REG_CHGCTRL0, 0, 0x03,
	rt9759_wdt_toreg, 0),
	RT9759_DT_VALPROP(vacovp, RT9759_REG_ACPROTECT, 0, 0x07,
	rt9759_vacovp_toreg, 0),
	RT9759_DT_VALPROP(ibat_rsense, RT9759_REG_REGCTRL, 1, 0x02, NULL, 0),
	RT9759_DT_VALPROP(ibusucpf_deglitch, RT9759_REG_BUSDEGLH, 3, 0x08, NULL,
	0),
};

static const struct rt9759_dtprop rt9759_dtprops_bool[] = {
	RT9759_DT_VALPROP(vbatovp_dis, RT9759_REG_VBATOVP, 7, 0x80, NULL, 0),
	RT9759_DT_VALPROP(vbatovp_alm_dis, RT9759_REG_VBATOVP_ALM, 7, 0x80,
	NULL, 0),
	RT9759_DT_VALPROP(ibatocp_dis, RT9759_REG_IBATOCP, 7, 0x80, NULL, 0),
	RT9759_DT_VALPROP(ibatocp_alm_dis, RT9759_REG_IBATOCP_ALM, 7, 0x80,
	NULL, 0),
	RT9759_DT_VALPROP(ibatucp_alm_dis, RT9759_REG_IBATUCP_ALM, 7, 0x80,
	NULL, 0),
	RT9759_DT_VALPROP(vbusovp_alm_dis, RT9759_REG_VBUSOVP_ALM, 7, 0x80,
	NULL, 0),
	RT9759_DT_VALPROP(ibusocp_dis, RT9759_REG_IBUSOCUCP, 7, 0x80, NULL, 0),
	RT9759_DT_VALPROP(ibusocp_alm_dis, RT9759_REG_IBUSOCP_ALM, 7, 0x80,
	NULL, 0),
	RT9759_DT_VALPROP(wdt_dis, RT9759_REG_CHGCTRL0, 2, 0x04, NULL, 0),
	RT9759_DT_VALPROP(tsbusotp_dis, RT9759_REG_CHGCTRL1, 2, 0x04, NULL, 0),
	RT9759_DT_VALPROP(tsbatotp_dis, RT9759_REG_CHGCTRL1, 1, 0x02, NULL, 0),
	RT9759_DT_VALPROP(tdieotp_dis, RT9759_REG_CHGCTRL1, 0, 0x01, NULL, 0),
	RT9759_DT_VALPROP(reg_en, RT9759_REG_REGCTRL, 4, 0x10, NULL, 0),
	RT9759_DT_VALPROP(voutovp_dis, RT9759_REG_REGCTRL, 3, 0x08, NULL, 0),
	RT9759_DT_VALPROP(ibusadc_dis, RT9759_REG_ADCCTRL, 0, 0x01, NULL, 0),
	RT9759_DT_VALPROP(tdieadc_dis, RT9759_REG_ADCEN, 0, 0x01, NULL, 0),
	RT9759_DT_VALPROP(tsbatadc_dis, RT9759_REG_ADCEN, 1, 0x02, NULL, 0),
	RT9759_DT_VALPROP(tsbusadc_dis, RT9759_REG_ADCEN, 2, 0x04, NULL, 0),
	RT9759_DT_VALPROP(ibatadc_dis, RT9759_REG_ADCEN, 3, 0x08, NULL, 0),
	RT9759_DT_VALPROP(vbatadc_dis, RT9759_REG_ADCEN, 4, 0x10, NULL, 0),
	RT9759_DT_VALPROP(voutadc_dis, RT9759_REG_ADCEN, 5, 0x20, NULL, 0),
	RT9759_DT_VALPROP(vacadc_dis, RT9759_REG_ADCEN, 6, 0x40, NULL, 0),
	RT9759_DT_VALPROP(vbusadc_dis, RT9759_REG_ADCEN, 7, 0x80, NULL, 0),
};

static int rt9759_parse_dt(struct rt9759_chip *chip)
{
	struct rt9759_desc *desc;
	struct device_node *np = chip->dev->of_node;
	struct device_node *child_np;

	if (!np)
		return -ENODEV;

	if (chip->type == RT9759_TYPE_SLAVE)
		goto ignore_intr;

	chip->irq_gpio = devm_gpiod_get(chip->dev, "rt9759,intr", GPIOD_IN);
	if (IS_ERR(chip->irq_gpio))
		return PTR_ERR(chip->irq_gpio);

ignore_intr:
	desc = devm_kzalloc(chip->dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	memcpy(desc, &rt9759_desc_defval, sizeof(*desc));
	if (of_property_read_string(np, "rm_name", &desc->rm_name) < 0)
		dev_info(chip->dev, "%s no rm name\n", __func__);

	if (of_property_read_u8(np, "rm_slave_addr", &desc->rm_slave_addr) < 0)
		dev_info(chip->dev, "%s no regmap slave addr\n", __func__);

	child_np = of_get_child_by_name(np, rt9759_type_name[chip->type]);
	if (!child_np) {
		dev_notice(chip->dev, "%s no node(%s) found\n", __func__,
			   rt9759_type_name[chip->type]);
		return -ENODEV;
	}
	if (of_property_read_string(child_np, "chg_name", &desc->chg_name) < 0)
		dev_info(chip->dev, "%s no chg name\n", __func__);
	rt9759_parse_dt_u32(child_np, (void *)desc, rt9759_dtprops_u32,
			    ARRAY_SIZE(rt9759_dtprops_u32));
	rt9759_parse_dt_bool(child_np, (void *)desc, rt9759_dtprops_bool,
			     ARRAY_SIZE(rt9759_dtprops_bool));
	chip->desc = desc;
	return 0;
}

static int rt9759_init_chip(struct rt9759_chip *chip)
{
	int ret;

	dev_info(chip->dev, "%s\n", __func__);
	ret = rt9759_apply_dt(chip, (void *)chip->desc, rt9759_dtprops_u32,
			      ARRAY_SIZE(rt9759_dtprops_u32));
	if (ret < 0)
		return ret;
	ret = rt9759_apply_dt(chip, (void *)chip->desc, rt9759_dtprops_bool,
			      ARRAY_SIZE(rt9759_dtprops_bool));
	if (ret < 0)
		return ret;
	chip->wdt_en = !chip->desc->wdt_dis;
	return chip->wdt_en ? rt9759_enable_wdt(chip, false) : 0;
}

static int rt9759_check_devinfo(struct i2c_client *client, u8 *chip_rev,
				enum rt9759_type *type)
{
	int ret;

	ret = i2c_smbus_read_byte_data(client, RT9759_REG_DEVINFO);
	if (ret < 0) {
		dev_info(&client->dev, "%s: read devinfo failed. (%d)\n", __func__, ret);
		return ret;
	}
	if ((ret & 0x0f) != RT9759_DEVID) {
		dev_info(&client->dev, "%s: check rt9759 failed. (%d)\n", __func__, -ENODEV);
		return -ENODEV;
	}

	*chip_rev = (ret & 0xf0) >> 4;

	ret = i2c_smbus_read_byte_data(client, RT9759_REG_CHGCTRL1);
	if (ret < 0) {
		dev_info(&client->dev, "%s: read ctrl1 failed. (%d)\n", __func__, ret);
		return ret;
	}

	*type = (ret & 0x60) >> 5;

	dev_info(&client->dev, "%s rev(0x%02X), type(%s)\n", __func__,
		 *chip_rev, rt9759_type_name[*type]);

	return 0;
}

static inline void rt9759_print_mem(char *buffer, unsigned int len)
{
	unsigned int i = 0;
	char buf[256] = {0,};

	memset(buf, 0, sizeof(buf));
	snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "RT9759-0x00: ");

	for (i = 0; i < len; i++) {
		snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "0x%02X ", buffer[i]);

		if ((i != 0) && ((i + 1) % 8 == 0)) {
			snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "	");
		}

		if ((i != 0) && ((i + 1) % 16 == 0)) {
			pr_info("%s\n", buf);
			memset(buf, 0, sizeof(buf));
			snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "RT9759-0x%02X: ", (i + 1));
		}
	}

	pr_info("%s\n", buf);

}


static void rt9759_dump_register(struct rt9759_chip *chip)
{
	char reg[46] = {0,};

	rt9759_i2c_read_block(chip, 0x00, sizeof(reg), reg);

	rt9759_print_mem(reg, sizeof(reg));
}

static bool rt9759_update_status_flag(int flag, int status_code)
{
	if (flag) {
		sqc_notify_daemon_changed(SQC_CHARGER_CP1, status_code, 1);
		pr_info("%s status_code %d set to 1\n", __func__, status_code);
		return true;
	}

	return false;
}

static void rt9759_status_update_work(struct work_struct *work)
{
	struct rt9759_chip *chip =
		container_of(work, struct rt9759_chip, status_update_work.work);
	u8 protect_stat = 0, err_stat = 0, int_stat = 0, vac_stat = 0, vout_stat = 0;
	bool flag_on = false;
	unsigned int temp_val = 0;

	rt9759_dump_register(chip);

	pm_stay_awake(chip->dev);

	rt9759_i2c_read8(chip, RT9759_REG_FLTSTAT, &protect_stat);/*0x10*/

	dev_info(&chip->client->dev, "%s RT9759_REG_FLTSTAT: 0x%02X val: 0x%02X\n",
									__func__, RT9759_REG_FLTSTAT, protect_stat);

	flag_on = rt9759_update_status_flag((protect_stat & BIT(7)), SQC_ERR_VBAT_OVP);
	if (flag_on) {
		dev_info(&chip->client->dev, "%s SQC_ERR_VBAT_OVP\n", __func__);
		rt9759_get_vbatovp(rt9759_chg_ops.arg, &temp_val);
	}

	flag_on = rt9759_update_status_flag((protect_stat & BIT(6)), SQC_ERR_IBAT_OCP);
	if (flag_on) {
		dev_info(&chip->client->dev, "%s SQC_ERR_IBAT_OCP\n", __func__);
		rt9759_get_ibatocp(rt9759_chg_ops.arg, &temp_val);
	}

	flag_on = rt9759_update_status_flag((protect_stat & BIT(5)), SQC_ERR_VBUS_OVP);
	if (flag_on) {
		dev_info(&chip->client->dev, "%s SQC_ERR_VBUS_OVP\n", __func__);
		rt9759_get_vbusovp(rt9759_chg_ops.arg, &temp_val);
	}

	flag_on = rt9759_update_status_flag((protect_stat & BIT(4)), SQC_ERR_IBUS_OCP);
	if (flag_on) {
		dev_info(&chip->client->dev, "%s SQC_ERR_IBUS_OCP\n", __func__);
		rt9759_get_ibusocp(rt9759_chg_ops.arg, &temp_val);
	}

	flag_on = rt9759_update_status_flag((protect_stat & BIT(3)), SQC_ERR_TSBAT_TSBUS_OTP_ALM);
	if (flag_on) {
		dev_info(&chip->client->dev, "%s SQC_ERR_TSBAT_TSBUS_OTP_ALM\n", __func__);
	}

	flag_on = rt9759_update_status_flag((protect_stat & BIT(2)), SQC_ERR_TSBAT_OTP);
	if (flag_on) {
		dev_info(&chip->client->dev, "%s SQC_ERR_TSBAT_OTP\n", __func__);
	}

	flag_on = rt9759_update_status_flag((protect_stat & BIT(1)), SQC_ERR_TSBUS_OTP);
	if (flag_on) {
		dev_info(&chip->client->dev, "%s SQC_ERR_TSBUS_OTP\n", __func__);
	}

	flag_on = rt9759_update_status_flag((protect_stat & BIT(0)), SQC_ERR_ENGINE_OTP_ALM);
	if (flag_on) {
		dev_info(&chip->client->dev, "%s SQC_ERR_ENGINE_OTP_ALM\n", __func__);
	}

	rt9759_i2c_read8(chip, RT9759_REG_INTSTAT, &int_stat);/*0x0D*/

	dev_info(&chip->client->dev, "%s RT9759_REG_INTSTAT: 0x%02X val: 0x%02X\n",
									__func__, RT9759_REG_INTSTAT, int_stat);

	flag_on = rt9759_update_status_flag((int_stat & BIT(7)), SQC_ERR_VBAT_OVP_ALM);
	if (flag_on) {
		dev_info(&chip->client->dev, "%s SQC_ERR_VBAT_OVP_ALM\n", __func__);
		rt9759_get_vbatovp_alarm(rt9759_chg_ops.arg, &temp_val);
	}

	flag_on = rt9759_update_status_flag((int_stat & BIT(6)), SQC_ERR_IBAT_OCP_ALM);
	if (flag_on) {
		dev_info(&chip->client->dev, "%s SQC_ERR_IBAT_OCP_ALM\n", __func__);
		rt9759_get_ibatocp_alarm(rt9759_chg_ops.arg, &temp_val);
	}

	flag_on = rt9759_update_status_flag((int_stat & BIT(5)), SQC_ERR_VBUS_OVP_ALM);
	if (flag_on) {
		dev_info(&chip->client->dev, "%s SQC_ERR_VBUS_OVP_ALM\n", __func__);
		rt9759_get_vbusovp_alarm(rt9759_chg_ops.arg, &temp_val);
	}

	flag_on = rt9759_update_status_flag((int_stat & BIT(4)), SQC_ERR_IBUS_OCP_ALM);
	if (flag_on) {
		dev_info(&chip->client->dev, "%s SQC_ERR_IBUS_OCP_ALM\n", __func__);
		rt9759_get_ibusocp_alarm(rt9759_chg_ops.arg, &temp_val);
	}

	flag_on = rt9759_update_status_flag((int_stat & BIT(3)), SQC_ERR_IBAT_UCP_ALM);
	if (flag_on) {
		dev_info(&chip->client->dev, "%s SQC_ERR_IBAT_UCP_ALM\n", __func__);
		rt9759_get_ibatocp_alarm(rt9759_chg_ops.arg, &temp_val);
	}

	/*UPDATE_STATUS_FLAG(chip->stat, (protect_stat & BIT(2)), VAC_INSERT);*/
	/*UPDATE_STATUS_FLAG(chip->stat, (protect_stat & BIT(1)), VOUT_INSERT);*/
	/*UPDATE_STATUS_FLAG(chip->stat, (protect_stat & BIT(0)), ADC_DONE);*/

	rt9759_i2c_read8(chip, RT9759_REG_CONVSTAT, &err_stat);/*0x0A*/

	dev_info(&chip->client->dev, "%s RT9759_REG_CONVSTAT: 0x%02X val: 0x%02X\n",
									__func__, RT9759_REG_CONVSTAT, err_stat);

	/*UPDATE_STATUS_FLAG(chip->stat, (err_stat & BIT(7)), SQC_ERR_VBAT_OVP_ALM);*/
	flag_on = rt9759_update_status_flag((err_stat & BIT(6)), SQC_ERR_ENGINE_OTP);
	flag_on = rt9759_update_status_flag((err_stat & BIT(5)), SQC_ERR_VBUS_LOW);
	flag_on = rt9759_update_status_flag((err_stat & BIT(4)), SQC_ERR_VBUS_HIHG);
	/*flag_on = rt9759_update_status_flag(&chip->stat, (err_stat & BIT(2)), SQC_NOTIFY_ENGINE_START);*/

	rt9759_i2c_read8(chip, RT9759_REG_ACPROTECT, &vac_stat);/*0x05*/

	dev_info(&chip->client->dev, "%s RT9759_REG_ACPROTECT: 0x%02X val: 0x%02X\n",
									__func__, RT9759_REG_ACPROTECT, vac_stat);

	flag_on = rt9759_update_status_flag((vac_stat & BIT(7)), SQC_ERR_VAC_OVP);

	rt9759_i2c_read8(chip, RT9759_REG_REGTHRES, &vout_stat);/*0x2C*/

	dev_info(&chip->client->dev, "%s RT9759_REG_REGTHRES: 0x%02X val: 0x%02X\n",
									__func__, RT9759_REG_REGTHRES, vout_stat);

	flag_on = rt9759_update_status_flag((vout_stat & BIT(1)), SQC_ERR_VDR_OVP);

	flag_on = rt9759_update_status_flag((vout_stat & BIT(0)), SQC_ERR_VOUT_OVP);

	pm_relax(chip->dev);

	dev_info(&chip->client->dev, "%s irq status 0x%08X\n", __func__, chip->stat);

}


static int rt9759_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	int ret;
	struct rt9759_chip *chip;
	u8 chip_rev;
	enum rt9759_type type;

	dev_info(&client->dev, "%s(%s)\n", __func__, RT9759_DRV_VERSION);

	ret = rt9759_check_devinfo(client, &chip_rev, &type);
	if (ret < 0) {
		dev_info(&client->dev, "%s: check_devinfo failed. (%d)\n", __func__, ret);
		return ret;
	}

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;
	chip->dev = &client->dev;
	chip->client = client;
	chip->revision = chip_rev;
	chip->type = type;
	mutex_init(&chip->io_lock);
	mutex_init(&chip->adc_lock);
	mutex_init(&chip->stat_lock);
	mutex_init(&chip->hm_lock);
	mutex_init(&chip->suspend_lock);
	i2c_set_clientdata(client, chip);

	ret = rt9759_parse_dt(chip);
	if (ret < 0) {
		dev_notice(chip->dev, "%s parse dt fail(%d)\n", __func__, ret);
		goto err;
	}

	ret = rt9759_init_chip(chip);
	if (ret < 0) {
		dev_notice(chip->dev, "%s init chip fail(%d)\n", __func__, ret);
		goto err_initchip;
	}

	chip->status_update_queue = create_singlethread_workqueue("rt9759_status_update_work");
	INIT_DELAYED_WORK(&chip->status_update_work, rt9759_status_update_work);


	ret = rt9759_init_irq(chip);
	if (ret < 0) {
		dev_notice(chip->dev, "%s init irq fail(%d)\n", __func__, ret);
		goto err_initirq;
	}

	rt9759_chg_ops.arg = (void *)chip;
	ret = sqc_hal_charger_register(&rt9759_chg_ops, SQC_CHARGER_CP1);
	if (ret < 0) {
		dev_notice(chip->dev, "%s init irq fail(%d)\n", __func__, ret);
		goto err_initirq;
	}

	dev_info(chip->dev, "%s successfully\n", __func__);
	return 0;
err_initirq:
err_initchip:
err:
	mutex_destroy(&chip->suspend_lock);
	mutex_destroy(&chip->hm_lock);
	mutex_destroy(&chip->stat_lock);
	mutex_destroy(&chip->adc_lock);
	mutex_destroy(&chip->io_lock);
	return ret;
}

static void rt9759_i2c_shutdown(struct i2c_client *client)
{
	dev_info(&client->dev, "%s\n", __func__);
}

static int rt9759_i2c_remove(struct i2c_client *client)
{
	struct rt9759_chip *chip = i2c_get_clientdata(client);

	dev_info(&client->dev, "%s\n", __func__);
	if (!chip)
		return 0;

	mutex_destroy(&chip->suspend_lock);
	mutex_destroy(&chip->hm_lock);
	mutex_destroy(&chip->stat_lock);
	mutex_destroy(&chip->adc_lock);
	mutex_destroy(&chip->io_lock);
	return 0;
}

static int __maybe_unused rt9759_i2c_suspend(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct rt9759_chip *chip = i2c_get_clientdata(i2c);

	dev_info(dev, "%s\n", __func__);
	mutex_lock(&chip->suspend_lock);
	if (device_may_wakeup(dev))
		enable_irq_wake(chip->irq);
	return 0;
}

static int __maybe_unused rt9759_i2c_resume(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct rt9759_chip *chip = i2c_get_clientdata(i2c);

	dev_info(dev, "%s\n", __func__);
	mutex_unlock(&chip->suspend_lock);
	if (device_may_wakeup(dev))
		disable_irq_wake(chip->irq);
	return 0;
}

static SIMPLE_DEV_PM_OPS(rt9759_pm_ops, rt9759_i2c_suspend, rt9759_i2c_resume);

static const struct of_device_id rt9759_of_id[] = {
	{ .compatible = "richtek,rt9759" },
	{},
};
MODULE_DEVICE_TABLE(of, rt9759_of_id);

static const struct i2c_device_id rt9759_i2c_id[] = {
	{ "rt9759", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, rt9759_i2c_id);

static struct i2c_driver rt9759_i2c_driver = {
	.driver = {
		.name = "rt9759",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(rt9759_of_id),
		.pm = &rt9759_pm_ops,
	},
	.probe = rt9759_i2c_probe,
	.shutdown = rt9759_i2c_shutdown,
	.remove = rt9759_i2c_remove,
	.id_table = rt9759_i2c_id,
};
module_i2c_driver(rt9759_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Richtek RT9759 Charger Driver");
MODULE_AUTHOR("ShuFan Lee<shufan_lee@richtek.com>");
MODULE_VERSION(RT9759_DRV_VERSION);

/*
 * 1.0.7_MTK
 * (1) Add ibusucpf_deglitch in dtsi
 *
 * 1.0.6_MTK
 * (1) Add ibat_rsense in dtsi
 *
 * 1.0.5_MTK
 * (1) Modify IBUS ADC accuracy to 150mA
 * (2) Move adc_lock from __rt9759_get_adc to rt9759_get_adc
 * (3) Check force_adc_en before disabling adc in rt9759_is_vbuslowerr
 *
 * 1.0.4_MTK
 * (1) Add get_adc_accuracy ops
 *
 * 1.0.3_MTK
 * (1) Modify xxx_to_reg to support round up/down
 * (2) Show register value when set protection
 *
 * 1.0.2_MTK
 * (1) Notify ibusucpf/vbusovpalm/vbatovpalm/ibusocp/vbusovp/ibatocp/vbatovp/
 *     voutovp/vdrovp event
 * (2) Add checking vbuslowerr ops
 * (3) Create a thread to handle notification
 *
 * 1.0.1_MTK
 * (1) Modify maximum IBUSOCP from 3750 to 4750mA
 * (2) Remove operation of enabling sBase before enabling charging and ADC
 * (3) Add Master/Slave/Standalone mode's operation
 * (4) Add RSSO flag/state description and handle state only event
 *     only if state has changed
 * (5) If WDT is enabled in dtsi, only enable it right before enabling CHG_EN
 *     and disable it right after disabling CHG_EN
 *
 * 1.0.0_MTK
 * Initial release
 */
