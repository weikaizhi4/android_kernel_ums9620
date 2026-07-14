/*
 * Driver for the TI bq2560x charger.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define pr_fmt(fmt)	"bq2560x: %s: " fmt, __func__

#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/power/charger-manager.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/usb/phy.h>
#include <uapi/linux/usb/charger.h>
#include "sqc_bq2560x.h"

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
#include <vendor/comdef/zlog_common_base.h>
#endif

#ifdef CONFIG_VENDOR_SQC_CHARGER
#include <sqc_common.h>
#include <vendor/common/zte_misc.h>
int sqc_notify_daemon_changed(int chg_id, int msg_type, int msg_val);
#endif

enum {
	BQ25601_MASTER,
	BQ25601_SLAVE,
};

#define SGM41513_I2C_ADDR			0x1a
#define BQ2560X_BATTERY_NAME			"sc27xx-fgu"
#define BIT_DP_DM_BC_ENB			BIT(0)
#define BQ2560X_OTG_ALARM_TIMER_S		15

#define	BQ2560X_REG_IINLIM_BASE			100

#define BQ2560X_REG_ICHG_LSB			60

#define BQ2560X_REG_ICHG_MASK			GENMASK(5, 0)

#define BQ2560X_REG_CHG_MASK			GENMASK(4, 4)


#define BQ2560X_REG_RESET_MASK			GENMASK(6, 6)

#define BQ2560X_REG_OTG_MASK			GENMASK(5, 5)

#define BQ2560X_REG_WATCHDOG_MASK		GENMASK(6, 6)

#define BQ2560X_REG_TERMINAL_VOLTAGE_MASK	GENMASK(7, 3)
#define BQ2560X_REG_TERMINAL_VOLTAGE_SHIFT	3

#define BQ2560X_REG_TERMINAL_CUR_MASK		GENMASK(3, 0)
#define BQ2560X_REG_VINDPM_VOLTAGE_MASK		GENMASK(3, 0)
#define BQ2560X_REG_OVP_MASK			GENMASK(7, 6)
#define BQ2560X_REG_OVP_SHIFT			6
#define BQ2560X_REG_EN_HIZ_MASK			GENMASK(7, 7)
#define BQ2560X_REG_EN_HIZ_SHIFT		7
#define BQ2560X_REG_LIMIT_CURRENT_MASK		GENMASK(4, 0)
#define BQ2560X_DISABLE_PIN_MASK_2730		BIT(0)
#define BQ2560X_DISABLE_PIN_MASK_2721		BIT(15)
#define BQ2560X_DISABLE_PIN_MASK_2720		BIT(0)
#define BQ2560X_DISABLE_PIN_MASK		BIT(0)

#define BQ2560X_OTG_VALID_MS			500
#define BQ2560X_FEED_WATCHDOG_VALID_MS		50
#define BQ2560X_OTG_RETRY_TIMES			10
#define BQ2560X_LIMIT_CURRENT_MAX		3200000

#define BQ2560X_ROLE_MASTER_DEFAULT		1
#define BQ2560X_ROLE_SLAVE			2

#define BQ2560X_FCHG_OVP_6V			6000
#define BQ2560X_FCHG_OVP_9V			9000
#define BQ2560X_FAST_CHARGER_VOLTAGE_MAX	10500000
#define BQ2560X_NORMAL_CHARGER_VOLTAGE_MAX	6500000

#define VCHG_CTRL_THRESHOLD_MV_072 4370

#define BQ2560X_REG_EN_BATFET_MASK			GENMASK(2, 2)
#define BQ2560X_REG_EN_BATFET_SHIFT		2

struct bq2560x_charger_info {
	struct i2c_client *client;
	struct device *dev;
	struct usb_phy *usb_phy;
	struct notifier_block usb_notify;
	struct power_supply *psy_usb;
	struct zte_power_supply *zte_psy_usb;
	struct power_supply_charge_current cur;
	struct mutex lock;
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	struct zlog_client *zlog_client;
#endif
	bool charging;
	u32 limit;
	struct delayed_work otg_work;
	struct delayed_work wdt_work;
	struct delayed_work vindpm_work;
	struct regmap *pmic;
	struct alarm otg_timer;
	u32 charger_detect;
	u32 charger_pd;
	u32 charger_pd_mask;
	bool otg_enable;
	struct gpio_desc *gpiod;
	struct extcon_dev *edev;
	int vindpm_value;
	int boost_limit;
	int chip_main_id;
	int chip_sub_id;
	struct wakeup_source *bq_wake_lock;
	bool suspended;
	int init_finished;
	int hw_mode;
	int chg_status;
	char *name;
};

static enum power_supply_usb_type bq2560x_charger_usb_types[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_C,
	POWER_SUPPLY_USB_TYPE_PD,
	POWER_SUPPLY_USB_TYPE_PD_DRP,
	POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID
};

static enum power_supply_property bq2560x_usb_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
};

static enum zte_power_supply_property zte_bq2560x_usb_props[] = {
	POWER_SUPPLY_PROP_CHARGE_ENABLED,
	POWER_SUPPLY_PROP_SET_SHIP_MODE,
	POWER_SUPPLY_PROP_TUNING_VINDPM,
	POWER_SUPPLY_PROP_RECHARGE_SOC,
	POWER_SUPPLY_PROP_SET_WATCHDOG_TIMER,
};

static enum zte_power_supply_property zte_bq2560x_slave_usb_props[] = {
	POWER_SUPPLY_PROP_SET_SHIP_MODE,
};

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
static struct zlog_mod_info zlog_bc_dev1 = {
	.module_no = ZLOG_MODULE_CHG,
	.name = "buck_charger1",
	.device_name = "buckcharger",
	.ic_name = "bq2560x",
	.module_name = "TI",
	.fops = NULL,
};

static struct zlog_mod_info zlog_bc_dev2 = {
	.module_no = ZLOG_MODULE_CHG,
	.name = "buck_charger2",
	.device_name = "buckcharger",
	.ic_name = "bq2560x",
	.module_name = "TI",
	.fops = NULL,
};
#endif

static int slave_chg_id = 0;

static int bq2560x_charger_tuning_vindpm_insert(struct bq2560x_charger_info *info);

static int
bq2560x_charger_set_limit_current(struct bq2560x_charger_info *info,
				  u32 limit_cur);
/*
static bool bq2560x_charger_is_bat_present(struct bq2560x_charger_info *info)
{
	struct power_supply *psy;
	union power_supply_propval val;
	bool present = false;
	int ret;

	psy = power_supply_get_by_name(BQ2560X_BATTERY_NAME);
	if (!psy) {
		bq_err("Failed to get psy of sc27xx_fgu\n");
		return present;
	}
	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_PRESENT,
					&val);
	if (ret == 0 && val.intval)
		present = true;
	power_supply_put(psy);

	if (ret)
		dev_err(info->dev,
			"Failed to get property of present:%d\n", ret);

	return present;
}
*/

#define bq_err(fmt, ...)								\
do {											\
	if (info->hw_mode == BQ25601_MASTER) {					\
		pr_err("[bq2560x-MASTER]:%s:" fmt, __func__, ##__VA_ARGS__);	\
	} else { \
		pr_err("[bq2560x-SLAVE]:%s:" fmt, __func__, ##__VA_ARGS__);\
	} \
} while (0);

#define bq_info(fmt, ...)								\
do {											\
	if (info->hw_mode == BQ25601_MASTER) {					\
		pr_info("[bq2560x-MASTER]:%s:" fmt, __func__, ##__VA_ARGS__);	\
	} else {		\
		pr_info("[bq2560x-SLAVE]:%s:" fmt, __func__, ##__VA_ARGS__); \
	} \
} while (0);


#define bq_dbg(fmt, ...)								\
do {											\
	if (info->hw_mode == BQ25601_MASTER)	{					\
		pr_debug("[bq2560x-MASTER]:%s:" fmt, __func__, ##__VA_ARGS__);	\
	} else {			\
		pr_debug("[bq2560x-SLAVE]:%s:" fmt, __func__, ##__VA_ARGS__); \
	} \
} while (0);


static const struct i2c_device_id bq2560x_i2c_id[] = {
	{"ti,bq2560x_chg", BQ25601_MASTER},
	{"ti,bq2560x_chg2", BQ25601_SLAVE},
	{ }
};

static const struct of_device_id bq2560x_charger_of_match[] = {
	{
		.compatible = "ti,bq2560x_chg",
		.data = (void *)BQ25601_MASTER,
	},
	{
		.compatible = "ti,bq2560x_chg2",
		.data = (void *)BQ25601_SLAVE,
	},
	{},
};

MODULE_DEVICE_TABLE(of, bq2560x_charger_of_match);

static int bq2560x_read(struct bq2560x_charger_info *info, u8 reg, u8 *data)
{
	int ret = 0, retry_cnt = 3;

	if (info->suspended) {
		bq_err("bq2560x not iic read after system suspend!\n");
		return 0;
	}

	do {
		ret = i2c_smbus_read_byte_data(info->client, reg);
		if (ret < 0) {
			bq_err("bq2560x_read failed, ret=%d, retry_cnt=%d\n", ret, retry_cnt);
			usleep_range(5000, 5500);
		}
	} while ((ret < 0) && (retry_cnt-- > 0));

	if (ret < 0) {
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		zlog_client_record(info->zlog_client, "chg_name:%s, hwmode:%d i2c read err:%d\n", info->name, info->hw_mode, ret);
		zlog_client_notify(info->zlog_client, ZLOG_CHG_I2C_R_ERROR_NO);
#endif
		*data = 0;
		return ret;
	} else {
		*data = ret;
	}

	return 0;
}

static int bq2560x_write(struct bq2560x_charger_info *info, u8 reg, u8 data)
{
	int ret = 0, retry_cnt = 3;

	if (info->suspended) {
		bq_err("bq2560x not iic write after system suspend!\n");
		return 0;
	}

	do {
		ret = i2c_smbus_write_byte_data(info->client, reg, data);
		if (ret < 0) {
			bq_err("bq2560x_write failed, ret=%d, retry_cnt=%d\n", ret, retry_cnt);
			usleep_range(5000, 5500);
		}
	} while ((ret < 0) && (retry_cnt-- > 0));

	if (ret < 0) {
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		zlog_client_record(info->zlog_client, "chg_name:%s, hwmode:%d i2c write err:%d\n", info->name, info->hw_mode, ret);
		zlog_client_notify(info->zlog_client, ZLOG_CHG_I2C_W_ERROR_NO);
#endif
		return ret;
	}

	return 0;
}

static int bq2560x_update_bits(struct bq2560x_charger_info *info, u8 reg,
			       u8 mask, u8 data)
{
	u8 v;
	int ret;

	ret = bq2560x_read(info, reg, &v);
	if (ret < 0)
		return ret;

	v &= ~mask;
	v |= (data & mask);

	return bq2560x_write(info, reg, v);
}

static int
bq2560x_charger_set_termina_enable(struct bq2560x_charger_info *info, bool enable)
{
	return bq2560x_update_bits(info, BQ2560X_REG_05,
				   BQ2560X_REG05_EN_TERM_MASK,
				   enable << BQ2560X_REG05_EN_TERM_SHIFT);
}

static int
bq2560x_charger_set_vindpm(struct bq2560x_charger_info *info, u32 vol)
{
	u8 reg_val = 0;

	if (vol < BQ2560X_REG06_VINDPM_BASE)
		reg_val = 0x0;
	else if (vol > BQ2560X_REG06_VINDPM_MAX)
		reg_val = 0x0f;
	else
		reg_val = (vol - BQ2560X_REG06_VINDPM_BASE) / BQ2560X_REG06_VINDPM_LSB;

	return bq2560x_update_bits(info, BQ2560X_REG_06,
				   BQ2560X_REG06_VINDPM_MASK, reg_val << BQ2560X_REG06_VINDPM_SHIFT);
}

static int bq2560x_charger_set_ovp(struct bq2560x_charger_info *info, u32 vol)
{
	u8 reg_val;

	if (vol < 5500)
		reg_val = 0x0;
	else if (vol > 5500 && vol <= 6500)
		reg_val = 0x01;
	else if (vol > 6500 && vol <= 10500)
		reg_val = 0x02;
	else
		reg_val = 0x03;

	return bq2560x_update_bits(info, BQ2560X_REG_06,
				   BQ2560X_REG_OVP_MASK,
				   reg_val << BQ2560X_REG_OVP_SHIFT);
}

static int bq2560x_charger_get_ovp(struct bq2560x_charger_info *info, u32 *vol)
{
	u8 reg_val = 0;
	int ret = 0;

	ret = bq2560x_read(info, BQ2560X_REG_06, &reg_val);
	if (ret < 0) {
		bq_err("Failed to get ovp val, %d\n", ret);
		return ret;
	}

	reg_val = (reg_val & BQ2560X_REG_OVP_MASK) >> BQ2560X_REG_OVP_SHIFT;

	if (reg_val == 0x00) {
		*vol = 5500;
	} else if (reg_val == 0x01) {
		*vol = 6500;
	} else if (reg_val == 0x02) {
		*vol = 10500;
	} else if (reg_val == 0x03) {
		*vol = 14000;
	} else {
		*vol = 0;
	}

	return 0;
}


static int
bq2560x_charger_set_termina_vol(struct bq2560x_charger_info *info, u32 vol)
{
	u8 reg_val = 0;

	if (vol < BQ2560X_REG04_VREG_BASE)
		reg_val = 0x0;
	else if (vol >= BQ2560X_REG04_VREG_MAX)
		reg_val = 0x18;
	else
		reg_val = (vol - BQ2560X_REG04_VREG_BASE) / BQ2560X_REG04_VREG_LSB + 1;

	return bq2560x_update_bits(info, BQ2560X_REG_04,
				   BQ2560X_REG04_VREG_MASK,
				   reg_val << BQ2560X_REG04_VREG_SHIFT);
}

static int
bq2560x_charger_get_termina_vol(struct bq2560x_charger_info *info, u32 *vol)
{
	u8 reg_val = 0;
	int ret = 0;

	ret = bq2560x_read(info, BQ2560X_REG_04, &reg_val);
	if (ret < 0)
		return ret;

	reg_val &= BQ2560X_REG04_VREG_MASK;
	reg_val = reg_val >> BQ2560X_REG04_VREG_SHIFT;

	if (reg_val == BQ2560X_REG04_VREG_CODE)
		*vol = BQ2560X_REG04_VREG_EXCE_VOLTAGE * 1000;
	else
		*vol = (reg_val * BQ2560X_REG04_VREG_LSB +
					BQ2560X_REG04_VREG_BASE) * 1000;

	return 0;
}

static int
bq2560x_charger_set_termina_cur(struct bq2560x_charger_info *info, u32 cur)
{
	u8 reg_val = 0, offset = 0;

	if (info->client->addr == SGM41513_I2C_ADDR) {
		if (cur < 5) {
			reg_val = 0;
		} else if (cur <= 20) {
			offset = 0x0;
			reg_val = (cur - 5) / 5;
		} else if (cur <= 60) {
			offset = 0x3;
			reg_val = (cur - 20) / 10;
		} else if (cur < 80) {
			reg_val = 0x7;
		} else if (cur <= 240) {
			offset = 0x8;
			reg_val = (cur - 80) / 20;
		} else {
			reg_val = 0xF;
		}

		reg_val += offset;
		if (reg_val > 0xF)
			reg_val = 0xF;
	} else {
		if (cur <= BQ2560X_REG03_ITERM_BASE)
			reg_val = 0x0;
		else if (cur >= 960)
			reg_val = 0xF;
		else
			reg_val = (cur - BQ2560X_REG03_ITERM_BASE) / BQ2560X_REG03_ITERM_LSB;
	}
	return bq2560x_update_bits(info, BQ2560X_REG_03,
				   BQ2560X_REG03_ITERM_MASK,
				   reg_val << BQ2560X_REG03_ITERM_SHIFT);
}

static int
bq2560x_charger_get_termia_cur(struct bq2560x_charger_info *info, u32 *cur)
{
	u8 reg_val = 0, sign1 = 0, sign2 = 0, val = 0;
	int ret = 0;

	ret = bq2560x_read(info, BQ2560X_REG_03, &reg_val);
	if (ret < 0)
		return ret;

	reg_val &= BQ2560X_REG03_ITERM_MASK;
	sign1 = reg_val & BIT(3);
	sign2 = reg_val & BIT(2);
	val = reg_val & GENMASK(2, 0);
	reg_val = reg_val >> BQ2560X_REG03_ITERM_SHIFT;

	if (info->client->addr == SGM41513_I2C_ADDR) {
		if (sign1) {
			if (val == 0x7)
				*cur = 240 * 1000;
			else
				*cur = (val * 20 + 80) * 1000;
		} else if (sign2) {
			val = val - 0x3;
			*cur = (val * 10 + 20) * 1000;
		} else {
			*cur = (val * 5 + 5) * 1000;
		}
	}
	else
		*cur = (reg_val * BQ2560X_REG03_ITERM_LSB +
						BQ2560X_REG03_ITERM_BASE) * 1000;
	return 0;
}

static int
bq2560x_charger_set_recharge_voltage(struct bq2560x_charger_info *info,
						u32 recharge_voltage_uv)
{
	u8 reg_val = 0;
	int ret = 0;

	reg_val = (recharge_voltage_uv > 100000) ? BQ2560X_REG04_VRECHG_200MV : BQ2560X_REG04_VRECHG_100MV;
	ret = bq2560x_update_bits(info, BQ2560X_REG_04,
				   BQ2560X_REG04_VRECHG_MASK,
				   reg_val << BQ2560X_REG04_VRECHG_SHIFT);
	if (ret) {
		bq_err("set bq25601 recharge_voltage failed\n");
		return ret;
	}

	return 0;
}

static int
bq2560x_charger_get_recharge_voltage(struct bq2560x_charger_info *info,
						u32 *recharge_voltage_uv)
{
	u8 reg_val = 0;
	int ret = 0;

	ret = bq2560x_read(info, BQ2560X_REG_04, &reg_val);
	if (ret < 0)
		return ret;

	reg_val &= BQ2560X_REG04_VRECHG_MASK;
	reg_val = reg_val >> BQ2560X_REG04_VRECHG_SHIFT;

	*recharge_voltage_uv = reg_val ? 200000 : 100000;

	return 0;
}

static int bq2560x_charger_set_safety_timer(struct bq2560x_charger_info *info,
					u8 enable)
{
	return bq2560x_update_bits(info, BQ2560X_REG_05,
					BQ2560X_REG05_EN_TIMER_MASK,
					enable << BQ2560X_REG05_WDT_SHIFT);
}

static int bq2560x_charger_set_watchdog_timer(struct bq2560x_charger_info *info,
					u32 timer)
{
	u8 reg_val = 0;

	if (timer >= 160)
		reg_val = BQ2560X_REG05_WDT_160S;
	else if (timer <= 0)
		reg_val = BQ2560X_REG05_WDT_DISABLE;
	else
		reg_val = (timer - BQ2560X_REG05_WDT_BASE) / BQ2560X_REG05_WDT_LSB;
	return bq2560x_update_bits(info, BQ2560X_REG_05,
					BQ2560X_REG05_WDT_MASK,
					reg_val << BQ2560X_REG05_WDT_SHIFT);
}

static int bq2560x_charger_set_boost_limit(struct bq2560x_charger_info *info,
					u32 limit)
{
	u8 reg_val = 0;

	if (limit < 1200)
		reg_val = BQ2560X_REG02_BOOST_LIM_0P5A;
	else
		reg_val = BQ2560X_REG02_BOOST_LIM_1P2A;

	return bq2560x_update_bits(info, BQ2560X_REG_02,
					BQ2560X_REG02_BOOST_LIM_MASK,
					reg_val << BQ2560X_REG02_BOOST_LIM_SHIFT);
}

static int bq2560x_charger_hw_init(struct bq2560x_charger_info *info)
{
	struct sprd_battery_info bat_info = { };
	int voltage_max_microvolt, current_max_ua;
	int ret = 0;

	bat_info.batt_id_cha = NULL;

	if (info->psy_usb)
#ifdef CONFIG_VENDOR_SQC_CHARGER
		ret = sprd_battery_get_battery_info_sqc(info->psy_usb, &bat_info, 0);
#else
		ret = sprd_battery_get_battery_info(info->psy_usb, &bat_info);
#endif
	if (ret || !info->psy_usb) {
		bq_info("no battery information is supplied\n");

		/*
		 * If no battery information is supplied, we should set
		 * default charge termination current to 100 mA, and default
		 * charge termination voltage to 4.2V.
		 */
		info->cur.sdp_limit = 500000;
		info->cur.sdp_cur = 500000;
		info->cur.dcp_limit = 5000000;
		info->cur.dcp_cur = 500000;
		info->cur.cdp_limit = 5000000;
		info->cur.cdp_cur = 1500000;
		info->cur.unknown_limit = 5000000;
		info->cur.unknown_cur = 500000;
	} else {
		info->cur.sdp_limit = bat_info.cur.sdp_limit;
		info->cur.sdp_cur = bat_info.cur.sdp_cur;
		info->cur.dcp_limit = bat_info.cur.dcp_limit;
		info->cur.dcp_cur = bat_info.cur.dcp_cur;
		info->cur.cdp_limit = bat_info.cur.cdp_limit;
		info->cur.cdp_cur = bat_info.cur.cdp_cur;
		info->cur.unknown_limit = bat_info.cur.unknown_limit;
		info->cur.unknown_cur = bat_info.cur.unknown_cur;
		info->cur.fchg_limit = bat_info.cur.fchg_limit;
		info->cur.fchg_cur = bat_info.cur.fchg_cur;

		voltage_max_microvolt =
			bat_info.constant_charge_voltage_max_uv / 1000;
		current_max_ua = bat_info.constant_charge_current_max_ua / 1000;
		sprd_battery_put_battery_info(info->psy_usb, &bat_info);
	}

	ret = bq2560x_update_bits(info, BQ2560X_REG_0B,
			  BQ2560X_REG0B_RESET_MASK,
			  BQ2560X_REG0B_RESET << BQ2560X_REG0B_RESET_SHIFT);
	if (ret) {
		bq_err("reset bq2560x failed\n");
		return ret;
	}
	ret = bq2560x_charger_set_ovp(info, BQ2560X_FCHG_OVP_6V);
	if (ret) {
		bq_err("set bq2560x ovp failed\n");
		return ret;
	}

	ret = bq2560x_charger_set_vindpm(info, voltage_max_microvolt);
	if (ret) {
		bq_err("set bq2560x vindpm vol failed\n");
		return ret;
	}

	ret = bq2560x_charger_set_termina_vol(info,
					      voltage_max_microvolt);
	if (ret) {
		bq_err("set bq2560x terminal vol failed\n");
		return ret;
	}

	ret = bq2560x_charger_set_termina_cur(info, current_max_ua);
	if (ret) {
		bq_err("set bq2560x terminal cur failed\n");
		return ret;
	}

	ret = bq2560x_charger_set_termina_enable(info,
		info->hw_mode == BQ25601_SLAVE ? BQ2560X_REG05_TERM_DISABLE : BQ2560X_REG05_TERM_ENABLE);
	if (ret) {
		bq_err("set bq2560x terminal cur failed\n");
		return ret;
	}

	ret = bq2560x_charger_set_limit_current(info,
						info->cur.unknown_cur);
	if (ret)
		bq_err("set bq2560x limit current failed\n");

	ret = bq2560x_charger_set_safety_timer(info, BQ2560X_REG05_CHG_TIMER_DISABLE);
	if (ret)
		bq_err("set safety timer failed\n");

	ret = bq2560x_charger_set_watchdog_timer(info, 0);
	if (ret)
		bq_err("set watchdog timer failed\n");

	if (info->boost_limit !=0) {
		ret = bq2560x_charger_set_boost_limit(info, info->boost_limit);
		if (ret)
			bq_err("set boost limit failed\n");
	}

	bq2560x_update_bits(info, BQ2560X_REG_01,
					BQ2560X_REG01_PFM_DIS_MASK,
					BQ2560X_REG01_PFM_DISABLE << BQ2560X_REG01_PFM_DIS_SHIFT);

	ret = bq2560x_update_bits(info, BQ2560X_REG_07,
				  BQ2560X_REG_EN_BATFET_MASK, 0);
	bq_info("disable batfet_rst!\n");
	if (ret)
		bq_err("disable batfet_rst failed!\n");

	ret = bq2560x_update_bits(info, BQ2560X_REG_07, BQ2560X_REG07_BATFET_DLY_MASK,
			BQ2560X_REG07_BATFET_DLY_0S << BQ2560X_REG07_BATFET_DLY_SHIFT);
	if (ret)
		bq_err("set battfet delay 0s failed\n");

	return ret;
}

static int
bq2560x_charger_get_charge_voltage(struct bq2560x_charger_info *info,
				   u32 *charge_vol)
{
	struct power_supply *psy;
	union power_supply_propval val;
	int ret;

	psy = power_supply_get_by_name(BQ2560X_BATTERY_NAME);
	if (!psy) {
		bq_err("failed to get BQ2560X_BATTERY_NAME\n");
		return -ENODEV;
	}

	ret = power_supply_get_property(psy,
					POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
					&val);
	power_supply_put(psy);
	if (ret) {
		bq_err("failed to get CONSTANT_CHARGE_VOLTAGE\n");
		return ret;
	}

	*charge_vol = val.intval;

	return 0;
}

static int bq2560x_charger_start_charge(struct bq2560x_charger_info *info)
{
	int ret = 0;
	u8 reg_val = 0x01;

	bq_info("bq25601: start charge #####\n");

	ret = bq2560x_update_bits(info, BQ2560X_REG_01,
				BQ2560X_REG01_CHG_CONFIG_MASK,
				reg_val << BQ2560X_REG01_CHG_CONFIG_SHIFT);

	if (ret)
		bq_err("enable bq2560x charge failed\n");

	return ret;
}

static void bq2560x_charger_stop_charge(struct bq2560x_charger_info *info)
{
	int ret = 0;
	u8 reg_val = 0x00;

	bq_info("bq25601: stop charge #####\n");

	ret = bq2560x_update_bits(info, BQ2560X_REG_01,
				BQ2560X_REG01_CHG_CONFIG_MASK,
				reg_val << BQ2560X_REG01_CHG_CONFIG_SHIFT);

	if (ret)
		bq_err("disable bq2560x charge failed\n");
}

#ifdef ZTE_FEATURE_PV_AR
static void bq2560x_charger_enable_hiz(struct bq2560x_charger_info *info, bool enable)
{
	int ret = 0;
	u8 reg_val = BQ2560X_REG00_HIZ_DISABLE;

	if (enable) {
		bq_info("bq25601: enable hiz #####\n");
		reg_val |= BQ2560X_REG00_HIZ_ENABLE;
	} else {
		bq_info("bq25601: keep last hiz status #####\n");
	}
 
	ret = bq2560x_update_bits(info, BQ2560X_REG_00,
				BQ2560X_REG00_ENHIZ_MASK,
				reg_val << BQ2560X_REG00_ENHIZ_SHIFT);

	if (ret)
		bq_err("set HIZ mode  failed\n");
}
#endif

static int bq2560x_charger_get_charge_status(struct bq2560x_charger_info *info, unsigned int *en)
{
	int ret = 0;
	u8 reg_val = 0x00;

	ret = bq2560x_read(info, BQ2560X_REG_01, &reg_val);
	if (ret) {
		*en = 0;
		bq_err("get bq2560x charge status failed\n");
		return 0;
	}

	*en = (reg_val & BQ2560X_REG01_CHG_CONFIG_MASK) ? 1 : 0;

	return 0;
}

static int bq2560x_charger_set_current(struct bq2560x_charger_info *info,
				       u32 cur)
{
	u8 reg_val;
	u8 step = 0, mask = 0;

	cur = cur / 1000;

	if (info->client->addr == SGM41513_I2C_ADDR) {
		bq_info("sgm41513 goto");
		if (cur <= 35) {
			mask = 0;
			step = cur / 5;
		} else if (cur <= 120) {
			mask = 1;
			step = (cur - 40) / 10;
		} else if (cur <= 270) {
			mask = 2;
			step = (cur - 130) / 20;
		} else if (cur <= 510) {
			mask = 3;
			step = (cur - 300) / 30;
		} else if (cur <= 960) {
			mask = 4;
			step = (cur - 540) / 60;
		} else if (cur <= 1440) {
			mask = 5;
			step = (cur - 1020) / 60;
		} else if (cur <= 2340) {
			mask = 6;
			step = (cur - 1500) / 120;
		} else if (cur <= 3000) {
			mask = 7;
			step = (cur - 2460) / 120;
		} else {
			mask = 7;
			step = 0x07;
		}

		step = (step > 0x07) ? 0x07 : step;

		reg_val = mask << 3 | step;
	} else {
		bq_info("bq2560x goto");
		if (cur > 3000) {
			reg_val = 0x32;
		} else {
			reg_val = cur / BQ2560X_REG02_ICHG_LSB;
			reg_val &= BQ2560X_REG02_ICHG_MASK;
		}
	}

	return bq2560x_update_bits(info, BQ2560X_REG_02,
				   BQ2560X_REG02_ICHG_MASK,
				   reg_val);
}

static int bq2560x_charger_get_current(struct bq2560x_charger_info *info,
				       u32 *cur)
{
	u8 reg_val = 0, mask = 0, step = 0;
	int ret;

	ret = bq2560x_read(info, BQ2560X_REG_02, &reg_val);
	if (ret < 0)
		return ret;

	reg_val &= BQ2560X_REG02_ICHG_MASK;

	mask = reg_val >> 3;
	step = reg_val & GENMASK(2, 0);

	if (info->client->addr == SGM41513_I2C_ADDR) {
		bq_info("sgm41513 goto1");
		if (mask == 0) {
			*cur = (step * 5 + 0) * 1000;
		} else if (mask == 1) {
			*cur = (step * 10 + 40) * 1000;
		} else if (mask == 2) {
			*cur = (step * 20 + 130) * 1000;
		} else if (mask == 3) {
			*cur = (step * 30 + 300) * 1000;
		} else if (mask == 4) {
			*cur = (step * 60 + 540) * 1000;
		} else if (mask == 5) {
			*cur = (step * 60 + 1020) * 1000;
		} else if (mask == 6) {
			*cur = (step * 120 + 1500) * 1000;
		} else {
			*cur = (step * 120 + 2460) * 1000;
			*cur = (*cur  > 3000000) ? 3000000 : *cur;
		}
	} else {
		bq_info("bq2560x goto1");
		*cur = (reg_val * BQ2560X_REG02_ICHG_LSB + BQ2560X_REG02_ICHG_BASE) * 1000;
	}

	return 0;
}

static int
bq2560x_charger_set_limit_current(struct bq2560x_charger_info *info,
				  u32 limit_cur)
{
	u8 reg_val = 0;
	int ret = 0;

	limit_cur = limit_cur / 1000;

	if (limit_cur <= BQ2560X_REG00_IINLIM_BASE)
		reg_val = 0x0;
	else if (limit_cur >= BQ2560X_REG00_IINLIM_MAX)
		reg_val = 0x1F;
	else
		reg_val = (limit_cur - BQ2560X_REG00_IINLIM_BASE) / BQ2560X_REG00_IINLIM_LSB;

	ret = bq2560x_write(info, BQ2560X_REG_00, reg_val);
	if (ret)
		bq_err("set bq2560x limit cur failed\n");

	return ret;
}

static int
bq2560x_charger_get_limit_current(struct bq2560x_charger_info *info,
				  u32 *limit_cur)
{
	u8 reg_val = 0;
	int ret = 0;

	ret = bq2560x_read(info, BQ2560X_REG_00, &reg_val);
	if (ret < 0)
		return ret;

	reg_val &= BQ2560X_REG00_IINLIM_MASK;
	reg_val = reg_val >> BQ2560X_REG00_IINLIM_SHIFT;
	*limit_cur = (reg_val * BQ2560X_REG00_IINLIM_LSB + BQ2560X_REG00_IINLIM_BASE) * 1000;
	if (*limit_cur >= BQ2560X_REG00_IINLIM_MAX * 1000)
		*limit_cur = BQ2560X_REG00_IINLIM_MAX * 1000;

	return 0;
}

static int bq2560x_charger_get_health(struct bq2560x_charger_info *info,
				      u32 *health)
{
	*health = POWER_SUPPLY_HEALTH_GOOD;

	return 0;
}

static int bq2560x_charger_get_online(struct bq2560x_charger_info *info,
				      u32 *online)
{
	if (info->limit)
		*online = true;
	else
		*online = false;

	return 0;
}

static int bq2560x_charger_feed_watchdog(struct bq2560x_charger_info *info,
					 u32 val)
{
	int ret;

	ret = bq2560x_update_bits(info, BQ2560X_REG_01,
				  BQ2560X_REG01_WDT_RESET_MASK,
				  BQ2560X_REG01_WDT_RESET_MASK);
	if (ret)
		bq_err("reset bq2560x failed\n");

	return ret;
}

static int bq2560x_charger_set_shipmode(struct bq2560x_charger_info *info,
					  u32 val)
{
	int ret = 0;

	ret = bq2560x_update_bits(info, BQ2560X_REG_05, BQ2560X_REG05_WDT_MASK,
			BQ2560X_REG05_WDT_DISABLE << BQ2560X_REG05_WDT_SHIFT);
	if (ret)
		bq_err("disable bq2560x wdt failed\n");

	ret = bq2560x_update_bits(info, BQ2560X_REG_07, BQ2560X_REG07_BATFET_DIS_MASK,
			BQ2560X_REG07_BATFET_OFF << BQ2560X_REG07_BATFET_DIS_SHIFT);

	if (ret)
		bq_err("set_shipmode failed\n");
	bq_info("bq2560x: set shipmode #####\n");

	return ret;
}

static int bq2560x_charger_get_shipmode(struct bq2560x_charger_info *info, u32 *val)
{
	u8 data = 0;
	int ret = 0;

	ret = bq2560x_read(info, BQ2560X_REG_07, &data);
	if (ret < 0)
		return ret;
	data = (data & BQ2560X_REG07_BATFET_DIS_MASK) >> BQ2560X_REG07_BATFET_DIS_SHIFT;
	*val = !data;

	return 0;
}

static int bq2560x_charger_set_powerpath(struct bq2560x_charger_info *info,
					  int val)
{
	int ret = 0;
#if 0
	if (val) {
		ret = bq2560x_update_bits(info, BQ2560X_REG_07, BQ2560X_REG07_BATFET_DIS_MASK,
				BQ2560X_REG07_BATFET_OFF << BQ2560X_REG07_BATFET_DIS_SHIFT);
		if (ret)
			bq_err("enable bq2560x powerpath failed\n");
	} else {
		ret = bq2560x_update_bits(info, BQ2560X_REG_07, BQ2560X_REG07_BATFET_DIS_MASK,
				BQ2560X_REG07_BATFET_ON << BQ2560X_REG07_BATFET_DIS_SHIFT);
		if (ret)
			bq_err("disable bq2560x powerpath failed\n");
	}

	ret = bq2560x_update_bits(info, BQ2560X_REG_07, BQ2560X_REG07_BATFET_DLY_MASK,
			BQ2560X_REG07_BATFET_DLY_0S << BQ2560X_REG07_BATFET_DLY_SHIFT);
	if (ret)
		bq_err("set powerpath delay 0s failed\n");
#endif
	bq_info("bq2560x: set bq2560x powerpath val:%d  #####\n", val);

	return ret;
}

static int bq2560x_charger_get_powerpath(struct bq2560x_charger_info *info, u32 *val)
{
	u8 data = 0;
	int ret = 0;

	ret = bq2560x_read(info, BQ2560X_REG_07, &data);
	if (ret < 0)
		return ret;
	data = (data & BQ2560X_REG07_BATFET_DIS_MASK) >> BQ2560X_REG07_BATFET_DIS_SHIFT;
	*val = !!data;

	return 0;
}


static int bq2560x_charger_get_vindpm(struct bq2560x_charger_info *info,
					u32 *vol)
{
	u8 reg_val;
	int ret;

	ret = bq2560x_read(info, BQ2560X_REG_06, &reg_val);
	if (ret < 0)
		return ret;

	reg_val &= BQ2560X_REG06_VINDPM_MASK;
	reg_val = reg_val >> BQ2560X_REG06_VINDPM_SHIFT;

	*vol = reg_val * BQ2560X_REG06_VINDPM_LSB + BQ2560X_REG06_VINDPM_BASE;
	return 0;
}

static int bq2560x_charger_get_reg_status(struct bq2560x_charger_info *info,
					u32 *status)
{
	u8 reg_val = 0;
	int ret = 0;

	if (!info || !status) {
		bq_err("%s: Null ptr\n", __func__);
		return -EFAULT;
	}

	ret = bq2560x_read(info, BQ2560X_REG_08, &reg_val);
	if (ret < 0)
		return ret;

	reg_val &= BQ2560X_REG08_CHRG_STAT_MASK;
	reg_val = reg_val >> BQ2560X_REG08_CHRG_STAT_SHIFT;
	*status = reg_val;

	return 0;
}

static int bq2560x_charger_get_status(struct bq2560x_charger_info *info)
{
	u32 charger_status = 0;
	int ret = 0;

	ret = bq2560x_charger_get_reg_status(info, &charger_status);
	if (ret == 0) {
		if (charger_status == BQ2560X_REG08_CHRG_STAT_IDLE) {
			return POWER_SUPPLY_STATUS_NOT_CHARGING;
		} else if ((charger_status == BQ2560X_REG08_CHRG_STAT_PRECHG) ||
					(charger_status == BQ2560X_REG08_CHRG_STAT_FASTCHG)) {
			return POWER_SUPPLY_STATUS_CHARGING;
		} else if (charger_status == BQ2560X_REG08_CHRG_STAT_CHGDONE) {
			return POWER_SUPPLY_STATUS_FULL;
		} else {
			return POWER_SUPPLY_STATUS_CHARGING;
		}
	}
	bq_info("bq2560x: read status failed, ret = %d\n", ret);
	return POWER_SUPPLY_STATUS_UNKNOWN;
}

static int bq2560x_charger_set_status(struct bq2560x_charger_info *info,
				      int val)
{
	int ret = 0;
	u32 input_vol;

	/*Set the OVP by charger-manager when cmd larger than 1 */
	if (val > CM_FAST_CHARGE_NORMAL_CMD) {
		if (val == CM_FAST_CHARGE_ENABLE_CMD) {
			ret = bq2560x_charger_set_ovp(info, BQ2560X_FCHG_OVP_9V);
			if (ret) {
				bq_err("failed to set fast charge 9V ovp\n");
				return ret;
			}
		} else if (val == CM_FAST_CHARGE_DISABLE_CMD) {
			ret = bq2560x_charger_set_ovp(info, BQ2560X_FCHG_OVP_6V);
			if (ret) {
				bq_err("failed to set fast charge 5V ovp\n");
				return ret;
			}

			ret = bq2560x_charger_get_charge_voltage(info, &input_vol);
			if (ret) {
				bq_err("failed to get 9V charge voltage\n");
				return ret;
			}

		} else {
			bq_err("Failed Should not go here check code, set ovp to default.\n");
			ret = bq2560x_charger_set_ovp(info, BQ2560X_FCHG_OVP_6V);
			if (ret) {
				bq_err("failed to set fast charge 5V ovp\n");
				return ret;
			}

			ret = bq2560x_charger_get_charge_voltage(info, &input_vol);
			if (ret) {
				bq_err("failed to get 5V charge voltage\n");
				return ret;
			}

		}

		return 0;
	}

	if (!val) {
		bq2560x_charger_stop_charge(info);
		info->charging = false;

	} else if (val && !info->charging) {
		ret = bq2560x_charger_start_charge(info);
		if (ret) {
			bq_err("start charge failed\n");
		} else {
			info->charging = true;
		}
	}

	return ret;
}

static void bq2560x_print_regs(struct bq2560x_charger_info *info, char *buffer, unsigned int len)
{
	int i = 0;
	u8 value[12] = {0};
	char temp = 0;

	for (i = 0; i < ARRAY_SIZE(value); i++) {
		bq2560x_read(info, i, &(value[i]));
		if (i == ARRAY_SIZE(value) - 1) {
			bq_info("0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x\n",
				value[0], value[1], value[2], value[3], value[4], value[5],
				value[6], value[7], value[8], value[9], value[10], value[11]);
		}
	}

	temp = (value[8] & GENMASK(4, 3)) >> 3;

	switch (temp) {
	case 0:
		snprintf(buffer + strlen(buffer), len - strlen(buffer), "CHG_STAT: NOT CHARGING, ");
		break;
	case 1:
		snprintf(buffer + strlen(buffer), len - strlen(buffer), "CHG_STAT: PRE CHARGING, ");
		break;
	case 2:
		snprintf(buffer + strlen(buffer), len - strlen(buffer), "CHG_STAT: FAST CHARGING, ");
		break;
	case 3:
		snprintf(buffer + strlen(buffer), len - strlen(buffer), "CHG_STAT: TERM CHARGING, ");
		break;
	default:
		break;
	}

	if (value[9] & BIT(7)) {
		snprintf(buffer + strlen(buffer), len - strlen(buffer), "WT_FAULT: 1, ");
	}

	if (value[9] & BIT(6)) {
		snprintf(buffer + strlen(buffer), len- strlen(buffer), "BOOST_FAULT: 1, ");
	}

	temp = (value[9] & GENMASK(5, 4)) >> 4;

	switch (temp) {
	case 1:
		snprintf(buffer + strlen(buffer), len - strlen(buffer), "VAC OVP or VBUS UVP: 1, ");
		break;
	case 2:
		snprintf(buffer + strlen(buffer), len - strlen(buffer), "THERMAL SHUTDOWN: 1, ");
		break;
	case 3:
		snprintf(buffer + strlen(buffer), len - strlen(buffer), "SAFETY TIMEOUT: 1, ");
		break;
	default:
		break;
	}

	if (value[9] & BIT(3)) {
		snprintf(buffer + strlen(buffer), len - strlen(buffer), "BATOVP: 1, ");
	}

	if (value[10] & BIT(6)) {
		snprintf(buffer + strlen(buffer), len - strlen(buffer), "VINDPM: 1, ");
		info->chg_status |= BIT(SQC_CHG_STATUS_AICL);
	} else {
		snprintf(buffer + strlen(buffer), len - strlen(buffer), "VINDPM: 0, ");
		info->chg_status &= ~ BIT(SQC_CHG_STATUS_AICL);
	}

	if (value[10] & BIT(5)) {
		snprintf(buffer + strlen(buffer), len - strlen(buffer), "IINDPM: 1, ");
	}
	
}

static int bq2560x_charger_dumper_reg(struct bq2560x_charger_info *info)
{
	int usb_icl = 0, fcc = 0, fcv = 0, topoff = 0, recharge_voltage = 0;
	char buffer[512] = {0, };

	bq2560x_charger_get_termina_vol(info, &fcv);

	bq2560x_charger_get_limit_current(info, &usb_icl);

	bq2560x_charger_get_current(info, &fcc);

	bq2560x_charger_get_termia_cur(info, &topoff);

	bq2560x_charger_get_recharge_voltage(info, &recharge_voltage);
	
	bq2560x_print_regs(info, buffer, sizeof(buffer));

	bq_info("bq2560x: charging[%d], fcv[%d], usb_icl[%d], fcc[%d], topoff[%d], rechg_volt[%d], %s",
				info->charging, fcv / 1000, usb_icl / 1000, fcc / 1000,
				topoff / 1000, recharge_voltage / 1000, buffer);
	return 0;
}


static int bq2560x_charger_usb_change(struct notifier_block *nb,
				      unsigned long limit, void *data)
{
	struct bq2560x_charger_info *info =
		container_of(nb, struct bq2560x_charger_info, usb_notify);

	bq_info("bq25601: bq25601_charger_usb_change: %d\n", limit);
	info->limit = limit;

#ifdef CONFIG_VENDOR_SQC_CHARGER
	sqc_notify_daemon_changed(SQC_NOTIFY_USB,
					SQC_NOTIFY_USB_STATUS_CHANGED, !!limit);
#endif

	return NOTIFY_OK;
}

#define VINDPM_LOW_THRESHOLD_UV 4150000
static int bq2560x_charger_tuning_vindpm_insert(struct bq2560x_charger_info *info)
{
	union power_supply_propval val;
	union power_supply_propval batt_vol_uv;

	struct power_supply *fuel_gauge;
	int ret1 = 0, ret2 = 0,  vchg = 0, vindpm = 0;

	fuel_gauge = power_supply_get_by_name(BQ2560X_BATTERY_NAME);
	if (!fuel_gauge)
		return -ENODEV;

	ret1 = power_supply_get_property(fuel_gauge,
				POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, &val);

	ret2 = power_supply_get_property(fuel_gauge,
				POWER_SUPPLY_PROP_VOLTAGE_NOW, &batt_vol_uv);

	power_supply_put(fuel_gauge);
	if (ret1) {
		bq_err("%s: get POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE failed!\n", __func__);
		return ret1;
	}

	if (ret2) {
		bq_err("%s: get POWER_SUPPLY_PROP_VOLTAGE_NOW failed!\n", __func__);
		return ret2;
	}

	vchg = val.intval / 1000;

	bq2560x_charger_get_vindpm(info, &vindpm);
	bq_info("%s: get CHARGE_VOLTAGE %d, vindpm %d\n", __func__, vchg, vindpm);

	if (vchg > info->vindpm_value) {
		bq2560x_charger_set_vindpm(info, vchg > info->vindpm_value);
		bq_info(" vchg %d, batt_vol_uv=%d, now vindpm %d, set vindpm to %d\n",
			 vchg, batt_vol_uv.intval, vindpm, vchg > info->vindpm_value);
		bq2560x_charger_dumper_reg(info);
	} else {
		if (vchg > batt_vol_uv.intval / 1000 + 200) {
			bq2560x_charger_set_vindpm(info, batt_vol_uv.intval / 1000 + 200);
			bq_info("%s: vchg %d, batt_vol_uv=%d,  now vindpm %d, set vindpm1 to %d\n",
				__func__, vchg, batt_vol_uv.intval, vindpm, batt_vol_uv.intval / 1000 + 200);
		} else {
			bq2560x_charger_set_vindpm(info, vchg + 100);
			bq_info("%s: vchg %d, batt_vol_uv=%d,  now vindpm %d, set vindpm2 to %d\n",
				__func__, vchg, batt_vol_uv.intval, vindpm, vchg + 100);
		}
	}

	return 0;
}

static int bq2560x_charger_tuning_vindpm(struct bq2560x_charger_info *info)
{
	union power_supply_propval val;
	struct power_supply *fuel_gauge;
	int ret = 0, vchg = 0, vindpm = 0;
	union power_supply_propval batt_vol_uv;
	int diff_vbat_vindpm = 0;

	fuel_gauge = power_supply_get_by_name(BQ2560X_BATTERY_NAME);
	if (!fuel_gauge)
		return -ENODEV;

	if (!info->suspended) {
		ret = power_supply_get_property(fuel_gauge,
					POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, &val);

		ret = power_supply_get_property(fuel_gauge,
					POWER_SUPPLY_PROP_VOLTAGE_NOW, &batt_vol_uv);
		power_supply_put(fuel_gauge);
		if (ret)
			return ret;

		vchg = val.intval / 1000;

		bq2560x_charger_get_vindpm(info, &vindpm);

		diff_vbat_vindpm = vindpm - batt_vol_uv.intval / 1000;
		bq_info("%s: get CHARGE_VOLTAGE %d, vindpm %d batt_vol_uv %d diff_vbat_vindpm=%d\n",
			__func__, vchg, vindpm,  batt_vol_uv.intval, diff_vbat_vindpm);

		if (diff_vbat_vindpm < 200) {
			vindpm = vindpm + 100;
			if (vindpm > BQ2560X_REG06_VINDPM_NORMAL)
				vindpm = BQ2560X_REG06_VINDPM_NORMAL;
			bq2560x_charger_set_vindpm(info, vindpm);
			bq_info("%s: vchg %d, now vindpm %d\n", __func__, vchg, vindpm);
		}
	} else {
			pr_info("%s: system suspend,not adjust vindpm\n", __func__);
	}

	return 0;
}

static void tuning_vindpm_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct bq2560x_charger_info *info = container_of(dwork,
							  struct bq2560x_charger_info,
							  vindpm_work);
	__pm_stay_awake(info->bq_wake_lock);
	bq2560x_charger_tuning_vindpm(info);
	__pm_relax(info->bq_wake_lock);
}

static int bq2560x_charger_config_is_enabled(struct bq2560x_charger_info *info)
{
	int ret;
	u8 val;

	ret = bq2560x_read(info, BQ2560X_REG_01, &val);
	if (ret) {
		bq_err("failed to get bq2560x otg status\n");
		return ret;
	}

	val &= BQ2560X_REG01_CHG_CONFIG_MASK;

	return val;
}

static int bq2560x_charger_usb_get_property(struct power_supply *psy,
					    enum power_supply_property psp,
					    union power_supply_propval *val)
{
	struct bq2560x_charger_info *info = power_supply_get_drvdata(psy);
	u32 cur, online, health;
	enum usb_charger_type type;
	int ret = 0;

	if (!info->init_finished) {
		val->intval = 0;
		bq_info("%s bq2560x is not inited", __func__);
		return 0;
	}

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (info->limit)
			val->intval = bq2560x_charger_get_status(info);
		else
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		if (!info->charging) {
			val->intval = 0;
		} else {
			ret = bq2560x_charger_get_current(info, &cur);
			if (ret)
				goto out;

			val->intval = cur;
		}
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		if (!info->charging) {
			val->intval = 0;
		} else {
			ret = bq2560x_charger_get_limit_current(info, &cur);
			if (ret)
				goto out;

			val->intval = cur;
		}
		break;

	case POWER_SUPPLY_PROP_ONLINE:
		ret = bq2560x_charger_get_online(info, &online);
		if (ret)
			goto out;

		val->intval = online;

		break;

	case POWER_SUPPLY_PROP_HEALTH:
		if (info->charging) {
			val->intval = 0;
		} else {
			ret = bq2560x_charger_get_health(info, &health);
			if (ret)
				goto out;

			val->intval = health;
		}
		break;

	case POWER_SUPPLY_PROP_USB_TYPE:
		type = info->usb_phy->chg_type;

		switch (type) {
		case SDP_TYPE:
			val->intval = POWER_SUPPLY_USB_TYPE_SDP;
			break;

		case DCP_TYPE:
			val->intval = POWER_SUPPLY_USB_TYPE_DCP;
			break;

		case CDP_TYPE:
			val->intval = POWER_SUPPLY_USB_TYPE_CDP;
			break;

		default:
			val->intval = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		}

		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		bq2560x_charger_get_termina_vol(info, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		ret = bq2560x_charger_get_termia_cur(info, &val->intval);
		if (ret < 0)
			bq_err("get topoff failed\n");
		break;
	default:
		ret = -EINVAL;
	}

out:
	mutex_unlock(&info->lock);
	return ret;
}

static int bq2560x_charger_usb_set_property(struct power_supply *psy,
				enum power_supply_property psp,
				const union power_supply_propval *val)
{
	struct bq2560x_charger_info *info = power_supply_get_drvdata(psy);
	int ret = 0;
	u32 cur = 0;

	if (!info->init_finished) {
		bq_info("%s bq2560x is not inited", __func__);
		return 0;
	}

#ifdef CONFIG_VENDOR_SQC_CHARGER
	return 0;
#endif

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		bq_info("bq2560x[REG]: set fcc %d", val->intval);
		ret = bq2560x_charger_set_current(info, val->intval);
		if (ret < 0)
			bq_err("set charge current failed\n");
		bq2560x_charger_get_current(info, &cur);
		bq_info("bq2560x[REG]: get fcc %d", cur);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		bq_info("bq2560x[REG]: set topoff %d", val->intval);
		ret = bq2560x_charger_set_termina_cur(info, val->intval);
		if (ret < 0)
			bq_err("set charge voltage failed\n");
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		bq_info("bq2560x[REG]: set usb icl %d", val->intval);
		ret = bq2560x_charger_set_limit_current(info, val->intval);
		if (ret < 0)
			bq_err("set input current limit failed\n");
		bq2560x_charger_get_limit_current(info, &cur);
		bq_info("bq2560x[REG]: get usb icl %d", cur);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		bq_info("bq2560x[REG]: set enable %d", val->intval);

		ret = bq2560x_charger_set_status(info, val->intval);
		if (ret < 0)
			bq_err("set charge status failed\n");
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		ret = bq2560x_charger_set_termina_vol(info, val->intval / 1000);
		if (ret < 0)
			bq_err("failed to set terminate voltage\n");
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&info->lock);
	return ret;
}

static int bq2560x_charger_property_is_writeable(struct power_supply *psy,
						 enum power_supply_property psp)
{
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
	case POWER_SUPPLY_PROP_STATUS:

		ret = 1;
		break;

	default:
		ret = 0;
	}

	return ret;
}

static const struct power_supply_desc bq2560x_charger_desc = {
	.name			= "bq2560x_charger",
	.type			= POWER_SUPPLY_TYPE_UNKNOWN,
	.properties		= bq2560x_usb_props,
	.num_properties		= ARRAY_SIZE(bq2560x_usb_props),
	.get_property		= bq2560x_charger_usb_get_property,
	.set_property		= bq2560x_charger_usb_set_property,
	.property_is_writeable	= bq2560x_charger_property_is_writeable,
	.usb_types		= bq2560x_charger_usb_types,
	.num_usb_types		= ARRAY_SIZE(bq2560x_charger_usb_types),
};

static int zte_bq2560x_charger_usb_get_property(struct zte_power_supply *psy,
					    enum zte_power_supply_property psp,
					    union power_supply_propval *val)
{
	struct bq2560x_charger_info *info = zte_power_supply_get_drvdata(psy);
	u32 enabled = 0;
	int ret = 0;

	if (!info->init_finished) {
		val->intval = 0;
		bq_info("%s bq2560x is not inited", __func__);
		return 0;
	}

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_ENABLED:
		enabled = bq2560x_charger_config_is_enabled(info);
		val->intval = !!enabled;
		break;
	case POWER_SUPPLY_PROP_TUNING_VINDPM:
		ret = bq2560x_charger_get_vindpm(info, &val->intval);
		if (ret < 0)
			bq_err("get vindpm failed\n");
		bq_info("bq2560x_1#[REG]: get vindpm: %d!", val->intval);
		break;
	case POWER_SUPPLY_PROP_RECHARGE_SOC:
		ret = bq2560x_charger_get_recharge_voltage(info, &val->intval);
		if (ret < 0)
			bq_err("get charge recharge_voltage failed\n");
		break;
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		ret = bq2560x_charger_get_shipmode(info, &val->intval);
		if (ret < 0)
			bq_err("get shipmode failed\n");
		bq_info("bq2560x[REG]: get shipmode: %d!", val->intval);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&info->lock);

	return ret;
}

static int zte_bq2560x_charger_usb_set_property(struct zte_power_supply *psy,
				enum zte_power_supply_property psp,
				const union power_supply_propval *val)
{
	struct bq2560x_charger_info *info = zte_power_supply_get_drvdata(psy);
	int ret = 0;

	if (!info->init_finished) {
		bq_info("%s bq2560x is not inited", __func__);
		return 0;
	}

#ifdef CONFIG_ZTE_POWER_SUPPLY_COMMON
	if (psp != POWER_SUPPLY_PROP_SET_SHIP_MODE)
		return 0;
#endif

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_RECHARGE_SOC:
		bq_info("bq2560x[REG]: set recharge_voltage %d", val->intval);

		ret = bq2560x_charger_set_recharge_voltage(info, val->intval);
		if (ret < 0)
			bq_err("set charge recharge_voltage failed\n");
		break;
	case POWER_SUPPLY_PROP_FEED_WATCHDOG:
		bq_info("bq2560x[REG]: feed watchdog\n");
		ret = bq2560x_charger_feed_watchdog(info, val->intval);
		if (ret < 0)
			bq_err("feed charger watchdog failed\n");
		schedule_delayed_work(&info->vindpm_work, HZ * 3);
		bq2560x_charger_dumper_reg(info);
		break;
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		if (val->intval == 0) {
			bq_info("bq2560x[REG]: set shipmode %d", val->intval);
			ret = bq2560x_charger_set_shipmode(info, val->intval);
			if (ret < 0)
				bq_err("set shipmode failed\n");
		} else
			bq_info("bq2560x[REG]: set shipmode invalid val %d!", val->intval);
		break;
	case POWER_SUPPLY_PROP_TUNING_VINDPM:
		bq_info("bq2560x_2[REG]: tuning vindpm %d", val->intval);
		ret = bq2560x_charger_tuning_vindpm_insert(info);
		if (ret < 0)
			bq_err("failed to set tuning vindpm\n");
		break;
	case POWER_SUPPLY_PROP_CHARGE_ENABLED:
		if (val->intval == true) {
			ret = bq2560x_charger_start_charge(info);
			if (ret)
				bq_err("start charge failed\n");
		} else if (val->intval == false) {
			bq2560x_charger_stop_charge(info);
		}
		break;
	case POWER_SUPPLY_PROP_SET_WATCHDOG_TIMER:
		bq_info("bq2560x[REG]: set dog %d", val->intval);
		ret = bq2560x_charger_set_watchdog_timer(info, val->intval);
		if (ret < 0)
			bq_err("failed to set watchdog timer\n");
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&info->lock);
	return ret;
}

static int zte_bq2560x_charger_property_is_writeable(struct zte_power_supply *psy,
						 enum zte_power_supply_property psp)
{
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_ENABLED:
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
	case POWER_SUPPLY_PROP_TUNING_VINDPM:
	case POWER_SUPPLY_PROP_RECHARGE_SOC:
	case POWER_SUPPLY_PROP_FEED_WATCHDOG:
	case POWER_SUPPLY_PROP_SET_WATCHDOG_TIMER:
		ret = 1;
		break;

	default:
		ret = 0;
	}

	return ret;
}

static const struct zte_power_supply_desc zte_bq2560x_charger_desc = {
	.name			= "zte_bq2560x_charger",
	.type			= POWER_SUPPLY_TYPE_UNKNOWN,
	.properties		= zte_bq2560x_usb_props,
	.num_properties		= ARRAY_SIZE(zte_bq2560x_usb_props),
	.get_property		= zte_bq2560x_charger_usb_get_property,
	.set_property		= zte_bq2560x_charger_usb_set_property,
	.property_is_writeable	= zte_bq2560x_charger_property_is_writeable,
	.usb_types		= bq2560x_charger_usb_types,
	.num_usb_types		= ARRAY_SIZE(bq2560x_charger_usb_types),
};

static int zte_bq2560x_slave_charger_usb_get_property(struct zte_power_supply *psy,
					    enum zte_power_supply_property psp,
					    union power_supply_propval *val)
{
	struct bq2560x_charger_info *info = zte_power_supply_get_drvdata(psy);
	int ret = 0;

	if (!info->init_finished) {
		val->intval = 0;
		bq_info("%s bq2560x is not inited", __func__);
		return 0;
	}

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		ret = bq2560x_charger_get_shipmode(info, &val->intval);
		if (ret < 0)
			bq_err("get shipmode failed\n");
		bq_info("bq2560x[REG]: get shipmode: %d!", val->intval);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&info->lock);

	return ret;
}

static int zte_bq2560x_slave_charger_usb_set_property(struct zte_power_supply *psy,
				enum zte_power_supply_property psp,
				const union power_supply_propval *val)
{
	struct bq2560x_charger_info *info = zte_power_supply_get_drvdata(psy);
	int ret = 0;

	if (!info->init_finished) {
		bq_info("%s bq2560x is not inited", __func__);
		return 0;
	}

#ifdef CONFIG_ZTE_POWER_SUPPLY_COMMON
	if (psp != POWER_SUPPLY_PROP_SET_SHIP_MODE)
		return 0;
#endif

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		if (val->intval == 0) {
			bq_info("bq2560x[REG]: set shipmode %d", val->intval);
			ret = bq2560x_charger_set_shipmode(info, val->intval);
			if (ret < 0)
				bq_err("set shipmode failed\n");
		} else
			bq_info("bq2560x[REG]: set shipmode invalid val %d!", val->intval);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&info->lock);
	return ret;
}

static int zte_bq2560x_slave_charger_property_is_writeable(struct zte_power_supply *psy,
						 enum zte_power_supply_property psp)
{
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		ret = 1;
		break;

	default:
		ret = 0;
	}

	return ret;
}

static const struct zte_power_supply_desc bq2560x_slave_charger_desc = {
	.name			= "zte_bq2560x_slave_charger",
	.type			= POWER_SUPPLY_TYPE_UNKNOWN,
	.properties		= zte_bq2560x_slave_usb_props,
	.num_properties		= ARRAY_SIZE(zte_bq2560x_slave_usb_props),
	.get_property		= zte_bq2560x_slave_charger_usb_get_property,
	.set_property		= zte_bq2560x_slave_charger_usb_set_property,
	.property_is_writeable	= zte_bq2560x_slave_charger_property_is_writeable,
	.usb_types		= bq2560x_charger_usb_types,
	.num_usb_types		= ARRAY_SIZE(bq2560x_charger_usb_types),
};

static void bq2560x_charger_detect_status(struct bq2560x_charger_info *info)
{
	unsigned int min, max;

	/*
	 * If the USB charger status has been USB_CHARGER_PRESENT before
	 * registering the notifier, we should start to charge with getting
	 * the charge current.
	 */
	bq_info("bq25601: charger_detect_status %d", info->usb_phy->chg_state);
	if (info->usb_phy->chg_state != USB_CHARGER_PRESENT)
		return;

	usb_phy_get_charger_current(info->usb_phy, &min, &max);
	info->limit = min;
	bq_info("bq25601: limit %d", min);
}

static void
bq2560x_charger_feed_watchdog_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct bq2560x_charger_info *info = container_of(dwork,
							 struct bq2560x_charger_info,
							 wdt_work);
	int ret;

	ret = bq2560x_update_bits(info, BQ2560X_REG_01,
				  BQ2560X_REG01_WDT_RESET_MASK,
				  BQ2560X_REG01_WDT_RESET_MASK);
	if (ret) {
		bq_err("reset bq2560x failed\n");
		return;
	}
	schedule_delayed_work(&info->wdt_work, HZ * 15);
}

#ifdef CONFIG_REGULATOR
static void bq2560x_charger_otg_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct bq2560x_charger_info *info = container_of(dwork,
			struct bq2560x_charger_info, otg_work);
	bool otg_valid = extcon_get_state(info->edev, EXTCON_USB);
	int ret, retry = 0;

	if (otg_valid)
		goto out;

	do {
		ret = bq2560x_update_bits(info, BQ2560X_REG_01,
					  BQ2560X_REG01_OTG_CONFIG_MASK,
					  BQ2560X_REG01_OTG_ENABLE << BQ2560X_REG01_OTG_CONFIG_SHIFT);
		if (ret)
			bq_err("restart bq2560x charger otg failed\n");

		otg_valid = extcon_get_state(info->edev, EXTCON_USB);
	} while (!otg_valid && retry++ < BQ2560X_OTG_RETRY_TIMES);

	if (retry >= BQ2560X_OTG_RETRY_TIMES) {
		bq_err("Restart OTG failed\n");
		return;
	}

out:
	schedule_delayed_work(&info->otg_work, msecs_to_jiffies(1500));
}

static int bq2560x_charger_enable_otg(struct regulator_dev *dev)
{
	struct bq2560x_charger_info *info = rdev_get_drvdata(dev);
	int ret;

	if (!info->init_finished) {
		bq_info("%s bq2560x is not inited", __func__);
		return 0;
	}

	bq_info("%s into", __func__);

	/*
	 * Disable charger detection function in case
	 * affecting the OTG timing sequence.
	 */
	ret = regmap_update_bits(info->pmic, info->charger_detect,
				 BIT_DP_DM_BC_ENB, BIT_DP_DM_BC_ENB);
	if (ret) {
		bq_err("failed to disable bc1.2 detect function.\n");
		return ret;
	}

	ret = bq2560x_update_bits(info, BQ2560X_REG_01,
				  BQ2560X_REG01_OTG_CONFIG_MASK,
				  BQ2560X_REG01_OTG_ENABLE << BQ2560X_REG01_OTG_CONFIG_SHIFT);
	if (ret) {
		bq_err("enable bq2560x otg failed\n");
		regmap_update_bits(info->pmic, info->charger_detect,
				   BIT_DP_DM_BC_ENB, 0);
		return ret;
	}

	info->otg_enable = true;
	schedule_delayed_work(&info->wdt_work,
			      msecs_to_jiffies(BQ2560X_FEED_WATCHDOG_VALID_MS));
	schedule_delayed_work(&info->otg_work,
			      msecs_to_jiffies(BQ2560X_OTG_VALID_MS));

	return 0;
}

static int bq2560x_charger_disable_otg(struct regulator_dev *dev)
{
	struct bq2560x_charger_info *info = rdev_get_drvdata(dev);
	int ret;

	if (!info->init_finished) {
		bq_info("%s bq2560x is not inited", __func__);
		return 0;
	}

	bq_info("%s into", __func__);

	info->otg_enable = false;
	cancel_delayed_work_sync(&info->wdt_work);
	cancel_delayed_work_sync(&info->otg_work);
	ret = bq2560x_update_bits(info, BQ2560X_REG_01,
				  BQ2560X_REG01_OTG_CONFIG_MASK,
				  BQ2560X_REG01_OTG_DISABLE << BQ2560X_REG01_OTG_CONFIG_SHIFT);
	if (ret) {
		bq_err("disable bq2560x otg failed\n");
		return ret;
	}

	/* Enable charger detection function to identify the charger type */
	return regmap_update_bits(info->pmic, info->charger_detect,
				  BIT_DP_DM_BC_ENB, 0);
}

static int bq2560x_charger_vbus_is_enabled(struct regulator_dev *dev)
{
	struct bq2560x_charger_info *info = rdev_get_drvdata(dev);
	int ret;
	u8 val;

	ret = bq2560x_read(info, BQ2560X_REG_01, &val);
	if (ret) {
		bq_err("failed to get bq2560x otg status\n");
		return ret;
	}

	val &= BQ2560X_REG01_OTG_CONFIG_MASK;

	return val;
}

static const struct regulator_ops bq2560x_charger_vbus_ops = {
	.enable = bq2560x_charger_enable_otg,
	.disable = bq2560x_charger_disable_otg,
	.is_enabled = bq2560x_charger_vbus_is_enabled,
};

static const struct regulator_desc bq2560x_charger_vbus_desc = {
	.name = "otg-vbus",
	.of_match = "otg-vbus",
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.ops = &bq2560x_charger_vbus_ops,
	.fixed_uV = 5000000,
	.n_voltages = 1,
};

static int
bq2560x_charger_register_vbus_regulator(struct bq2560x_charger_info *info)
{
	struct regulator_config cfg = { };
	struct regulator_dev *reg;
	int ret = 0;

	cfg.dev = info->dev;
	cfg.driver_data = info;
	reg = devm_regulator_register(info->dev,
				      &bq2560x_charger_vbus_desc, &cfg);
	if (IS_ERR(reg)) {
		ret = PTR_ERR(reg);
		bq_err("Can't register regulator:%d\n", ret);
	}

	return ret;
}

#else
static int
bq2560x_charger_register_vbus_regulator(struct bq2560x_charger_info *info)
{
	return 0;
}
#endif

#ifdef CONFIG_ZTE_POWER_SUPPLY_COMMON
#ifndef CONFIG_FAST_CHARGER_SC27XX
static int zte_sqc_set_prop_by_name(const char *name, enum zte_power_supply_property psp, int data)
{
	struct zte_power_supply *psy = NULL;
	union power_supply_propval val = {0, };
	int rc = 0;

	if (name == NULL) {
		pr_info("bq2560x:psy name is NULL!!\n");
		goto failed_loop;
	}

	psy = zte_power_supply_get_by_name(name);
	if (!psy) {
		pr_info("bq2560x:get %s psy failed!!\n", name);
		goto failed_loop;
	}

	val.intval = data;

	rc = zte_power_supply_set_property(psy,
				psp, &val);
	if (rc < 0) {
		pr_info("bq2560x:Failed to set %s property:%d rc=%d\n", name, psp, rc);
		return rc;
	}

	zte_power_supply_put(psy);

	return 0;

failed_loop:
	return -EINVAL;
}
#endif

static int sqc_mp_get_chg_type(void *arg, unsigned int *chg_type)
{
	arg = arg ? arg : NULL;

	*chg_type = SQC_PMIC_TYPE_BUCK;

	return 0;
}

static int sqc_mp_get_chg_status(void *arg, unsigned int *charing_status)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;

	*charing_status = info->chg_status;

	return 0;
}

static int sqc_mp_set_enbale_chging(void *arg, unsigned int en)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;

	if (en) {
		if (info->hw_mode == BQ25601_SLAVE) {
			if (info->gpiod)
				gpiod_set_value_cansleep(info->gpiod, 1);
#ifdef ZTE_FEATURE_PV_AR
			bq_info("bq2560x[REG]: set battery hiz false\n");
			bq2560x_charger_enable_hiz(info, false);
#endif
		}

		bq2560x_charger_start_charge(info);

		bq2560x_charger_set_vindpm(info, 4600);

		info->charging = true;

		bq2560x_charger_dumper_reg(info);
	} else {
		bq2560x_charger_stop_charge(info);
		bq2560x_charger_dumper_reg(info);
		if (info->hw_mode == BQ25601_SLAVE) {
#ifdef ZTE_FEATURE_PV_AR
			bq_info("bq2560x[REG]: set battery hiz true\n");
			bq2560x_charger_enable_hiz(info, true);
#endif
			if (info->gpiod)
				gpiod_set_value_cansleep(info->gpiod, 0);
		}

		info->charging = false;
	}

	bq_info("bq2560x %s %d\n", __func__, en);

	return 0;
}

static int sqc_mp_get_enbale_chging(void *arg, unsigned int *en)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_get_charge_status(info, en);

	bq_info("bq2560x %s %d\n", __func__, *en);

	return ret;
}

static int sqc_mp_get_ichg(void *arg, u32 *ichg_ma)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_get_current(info, ichg_ma);

	*ichg_ma = *ichg_ma / 1000;

	bq_info("bq2560x %s %d\n", __func__, *ichg_ma);

	return ret;
}

static int sqc_mp_set_ichg(void *arg, u32 mA)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_set_current(info, mA * 1000);

	bq_info("bq2560x %s %d\n", __func__, mA);

	return ret;
}

static int sqc_mp_set_ieoc(void *arg, u32 mA)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_set_termina_cur(info, mA);

	bq_info("bq2560x %s %d\n", __func__, mA);

	return ret;
}

static int sqc_mp_get_ieoc(void *arg, u32 *ieoc)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_get_termia_cur(info, ieoc);

	*ieoc = *ieoc / 1000;

	bq_info("bq2560x %s %d\n", __func__, *ieoc);

	return ret;
}

static int sqc_mp_get_aicr(void *arg, u32 *aicr_ma)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_get_limit_current(info, aicr_ma);

	bq_info("bq2560x %s %d\n", __func__, *aicr_ma);

	return ret;
}

static int sqc_mp_set_aicr(void *arg, u32 aicr_ma)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_set_limit_current(info, aicr_ma * 1000);

	bq_info("bq2560x %s %d\n", __func__, aicr_ma);

	return ret;
}

static int sqc_mp_get_cv(void *arg, u32 *cv_mv)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_get_termina_vol(info, cv_mv);

	bq_info("bq2560x %s %d\n", __func__, *cv_mv);

	return ret;
}

static int sqc_mp_set_cv(void *arg, u32 cv_mv)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_set_termina_vol(info, cv_mv);

	bq_info("bq2560x %s %d\n", __func__, cv_mv);

	return ret;
}

static int sqc_mp_get_rechg_voltage(void *arg, u32 *rechg_volt_mv)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_get_recharge_voltage(info, rechg_volt_mv);

	*rechg_volt_mv = *rechg_volt_mv / 1000;

	bq_info("bq2560x %s %d\n", __func__, *rechg_volt_mv);

	return ret;
}

static int sqc_mp_set_rechg_voltage(void *arg, u32 rechg_volt_mv)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_set_recharge_voltage(info, rechg_volt_mv * 1000);

	bq_info("bq2560x %s %d\n", __func__, rechg_volt_mv);

	return ret;
}

static int sqc_mp_ovp_volt_get(void *arg, u32 *ac_ovp_mv)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_get_ovp(info, ac_ovp_mv);

	bq_info("bq2560x %s %d\n", __func__, *ac_ovp_mv);

	return ret;
}

static int sqc_mp_ovp_volt_set(void *arg, u32 ac_ovp_mv)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_set_ovp(info, ac_ovp_mv);

	bq_info("bq2560x %s %d\n", __func__, ac_ovp_mv);

	return ret;
}

static int sqc_mp_get_vbat(void *arg, u32 *mV)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	union power_supply_propval batt_vol_uv;
	struct power_supply *fuel_gauge = NULL;
	int ret1 = 0;

	fuel_gauge = power_supply_get_by_name(BQ2560X_BATTERY_NAME);
	if (!fuel_gauge)
		return -ENODEV;

	ret1 = power_supply_get_property(fuel_gauge,
				POWER_SUPPLY_PROP_VOLTAGE_NOW, &batt_vol_uv);

	power_supply_put(fuel_gauge);
	if (ret1) {
		bq_err("%s: get POWER_SUPPLY_PROP_VOLTAGE_NOW failed!\n", __func__);
		return ret1;
	}

	*mV = batt_vol_uv.intval / 1000;

	return 0;
}

static int sqc_mp_get_ibat(void *arg, u32 *mA)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	union power_supply_propval val;
	struct power_supply *fuel_gauge = NULL;
	int ret1 = 0;

	fuel_gauge = power_supply_get_by_name(BQ2560X_BATTERY_NAME);
	if (!fuel_gauge)
		return -ENODEV;

	ret1 = power_supply_get_property(fuel_gauge,
				POWER_SUPPLY_PROP_CURRENT_NOW, &val);

	power_supply_put(fuel_gauge);
	if (ret1) {
		bq_err("%s: get POWER_SUPPLY_PROP_CURRENT_NOW failed!\n", __func__);
		return ret1;
	}

	*mA = val.intval / 1000;

	return 0;
}

static int sqc_mp_get_vbus(void *arg, u32 *mV)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	union power_supply_propval val;
	struct power_supply *fuel_gauge = NULL;
	int ret1 = 0;

	fuel_gauge = power_supply_get_by_name(BQ2560X_BATTERY_NAME);
	if (!fuel_gauge)
		return -ENODEV;

	ret1 = power_supply_get_property(fuel_gauge,
				POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, &val);

	power_supply_put(fuel_gauge);
	if (ret1) {
		bq_err("%s: get POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE failed!\n", __func__);
		return ret1;
	}

	*mV = val.intval / 1000;

	return 0;
}

static int sqc_mp_get_ibus(void *arg, u32 *mA)
{
	arg = arg ? arg : NULL;

	*mA = 0;

	return 0;
}

static int sqc_enable_powerpath_set(void *arg, int enabled)
{
	struct bq2560x_charger_info *chg_dev = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_set_powerpath(chg_dev, enabled);

	return ret;
}

static int sqc_enable_powerpath_get(void *arg, int *enabled)
{
	struct bq2560x_charger_info *chg_dev = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_get_powerpath(chg_dev, enabled);

	return ret;
}

static struct sqc_pmic_chg_ops bq2560x_sqc_chg_ops = {

	.init_pmic_charger = NULL,

	.get_chg_type = sqc_mp_get_chg_type,
	.get_chg_status = sqc_mp_get_chg_status,


	.chg_enable = sqc_mp_set_enbale_chging,
	.chg_enable_get = sqc_mp_get_enbale_chging,

	.set_chging_fcv = sqc_mp_set_cv,
	.get_chging_fcv = sqc_mp_get_cv,
	.set_chging_fcc = sqc_mp_set_ichg,
	.get_chging_fcc = sqc_mp_get_ichg,

	.set_chging_icl = sqc_mp_set_aicr,
	.get_chging_icl = sqc_mp_get_aicr,

	.set_chging_topoff = sqc_mp_set_ieoc,
	.get_chging_topoff = sqc_mp_get_ieoc,

	.set_rechg_volt = sqc_mp_set_rechg_voltage,
	.get_rechg_volt = sqc_mp_get_rechg_voltage,

	.ac_ovp_volt_set = sqc_mp_ovp_volt_set,
	.ac_ovp_volt_get = sqc_mp_ovp_volt_get,

	.batt_ibat_get = sqc_mp_get_ibat,
	.batt_vbat_get = sqc_mp_get_vbat,

	.usb_ibus_get = sqc_mp_get_ibus,
	.usb_vbus_get = sqc_mp_get_vbus,

	.enable_path_set = sqc_enable_powerpath_set,
	.enable_path_get = sqc_enable_powerpath_get,
};
#ifndef CONFIG_FAST_CHARGER_SC27XX
extern struct sqc_bc1d2_proto_ops sqc_bc1d2_proto_node;
static int sqc_chg_type = SQC_NONE_TYPE;

static int sqc_bc1d2_get_charger_type(int *chg_type)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)sqc_bc1d2_proto_node.arg;

	if (!info) {
		pr_err("[SQC-HW]: [%s] info is null\n", __func__);
		*chg_type = SQC_NONE_TYPE;
		return 0;
	}

	if (!info->limit) {
		*chg_type = SQC_NONE_TYPE;
		goto out_loop;
	}

	switch (info->usb_phy->chg_type) {
	case SDP_TYPE:
		*chg_type = SQC_SDP_TYPE;
		break;
	case DCP_TYPE:
		*chg_type = SQC_DCP_TYPE;
		break;
	case CDP_TYPE:
		*chg_type = SQC_CDP_TYPE;
		break;
	case ACA_TYPE:
	default:
		*chg_type = SQC_FLOAT_TYPE;
	}

	bq_info("sqc_chg_type: %d, chg_type: %d\n", sqc_chg_type, *chg_type);

	if ((sqc_chg_type == SQC_SLEEP_MODE_TYPE)
			&& (*chg_type == SQC_NONE_TYPE)) {
		sqc_chg_type = *chg_type;
		zte_sqc_set_prop_by_name("zte_battery", POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, 1);
	} else if (sqc_chg_type == SQC_SLEEP_MODE_TYPE) {
		*chg_type = sqc_chg_type;
	}


out_loop:
	pr_info("[SQC-HW]: [%s] limit: %d, sprd_type: %d, chg_type: %d\n",
		__func__, info->limit, info->usb_phy->chg_type, *chg_type);

	return 0;
}


struct sqc_bc1d2_proto_ops sqc_bc1d2_proto_node = {
	.status_init = NULL,
	.status_remove = NULL,
	.get_charger_type = sqc_bc1d2_get_charger_type,
	.set_charger_type = NULL,
	.get_protocol_status = NULL,
	.get_chip_vendor_id = NULL,
	.set_qc3d0_dp = NULL,
	.set_qc3d0_dm = NULL,
	.set_qc3d0_plus_dp = NULL,
	.set_qc3d0_plus_dm = NULL,
};

int sqc_sleep_node_set(const char *val, const void *arg)
{
	int sleep_mode_enable = 0, ret = 0;
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)sqc_bc1d2_proto_node.arg;

	sscanf(val, "%d", &sleep_mode_enable);

	bq_info("sleep_mode_enable = %d\n", sleep_mode_enable);

	if (sleep_mode_enable) {
		if (sqc_chg_type != SQC_SLEEP_MODE_TYPE) {
			bq_info("sleep on status");

			/*disabel sqc-daemon*/
			sqc_chg_type = SQC_SLEEP_MODE_TYPE;
			sqc_notify_daemon_changed(SQC_NOTIFY_USB, SQC_NOTIFY_USB_STATUS_CHANGED, 1);

			ret = bq2560x_charger_set_watchdog_timer(info, 0);
			if (ret < 0)
				bq_err("failed to set watchdog timer\n");

			/*mtk enter sleep mode*/
			zte_sqc_set_prop_by_name("zte_battery", POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, 0);

		}
	} else {
		if (sqc_chg_type != SQC_SLEEP_MODE_TYPE) {
			bq_info("sleep off status");
			sqc_chg_type = SQC_NONE_TYPE;
		}
	}

	return 0;
}

int sqc_sleep_node_get(char *val, const void *arg)
{
	int sleep_mode = 0;

	if (sqc_chg_type == SQC_SLEEP_MODE_TYPE)
		sleep_mode = 1;

	return snprintf(val, PAGE_SIZE, "%u", sleep_mode);
}

static struct zte_misc_ops qc3dp_sleep_mode_node = {
	.node_name = "qc3dp_sleep_mode",
	.set = sqc_sleep_node_set,
	.get = sqc_sleep_node_get,
	.free = NULL,
	.arg = NULL,
};
#endif

#endif

static int bq2560x_chip_name_show(struct seq_file *m, void *v)
{
	struct bq2560x_charger_info *pinfo = m->private;

	if (pinfo) {
		seq_printf(m, "%s\n", pinfo->name);
	} else {
		seq_printf(m, "%s\n", "unkown");
	}
	return 0;
}

static int bq2560x_chip_name_open(struct inode *inode, struct file *file)
{
	return single_open(file, bq2560x_chip_name_show, PDE_DATA(inode));
}

static const struct file_operations bq2560x_chip_name_node = {
	.owner = THIS_MODULE,
	.open = bq2560x_chip_name_open,
	.read = seq_read,
	.llseek = seq_lseek,
};

static int bq2560x_get_chip_vendor_id(struct bq2560x_charger_info *info)
{
	u8 reg_value = 0;
	u8 pn = 0;
	int ret = -1;
	u8 sub_id = 0;

	ret = bq2560x_read(info, BQ2560X_REG_0B, &reg_value);

	pn = (reg_value & BQ2560X_REG0B_PN_MASK) >> BQ2560X_REG0B_PN_SHIFT;
	sub_id = (reg_value & BQ2560X_REG0B_VENDOR_ID_MASK) >> BQ2560X_REG0B_VENDOR_ID_SHIFT;

	if (ret < 0) {
		bq_err("%s: failed to get vendor id ret=%d\n", __func__, ret);
		info->chip_main_id = -1;
		info->chip_sub_id = -1;
		info->name = "unkown";
	} else {
		info->chip_main_id = pn;
		info->chip_sub_id = sub_id;
	}

	if (info->chip_main_id == 0) {
		info->name = "sgm41513";
	}  else if (info->chip_main_id == 1) {
		info->name = "sgm41513A";
	} else if (info->chip_main_id == 2) {
		if (info->chip_sub_id == 1)
			info->name = "sgm41511";
		else
			info->name = "bq25601";
	} else if (info->chip_main_id == 9) {
		info->name = "sy6974";
	} else {
		info->name = "unkown";
	}
	bq_info("%s reg=0x%x, value=0x%x pn=0x%x sub_id=0x%x chg_IC_name=%s\n",
		__func__, BQ2560X_REG_0B, reg_value, pn, sub_id, info->name);

	return ret;
}

static int bq2560x_slave_chg_id_get(char *val, const void *arg)
{
	pr_info("%s,slave_chg_id: %d\n", __func__, slave_chg_id);

	return snprintf(val, PAGE_SIZE, "%u", slave_chg_id);
}

static struct zte_misc_ops slave_chg_id_node = {
	.node_name = "slave_chg_id",
	.set = NULL,
	.get = bq2560x_slave_chg_id_get,
	.free = NULL,
	.arg = NULL,
};

static int bq2560x_charger_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct device *dev = &client->dev;
	struct bq2560x_charger_info *info = NULL;
	struct device_node *regmap_np = NULL;
	struct platform_device *regmap_pdev = NULL;
	struct sqc_pmic_chg_ops *sqc_ops = NULL;
	struct power_supply_config charger_cfg= { };
	int ret = 0, cnt = 5;

	pr_info("bq2560x:%s enter\n", __func__);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		bq_err("No support for SMBUS_BYTE_DATA\n");
		return -ENODEV;
	}

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -EPROBE_DEFER;
	info->name = "unkown";
	info->client = client;
	info->dev = dev;
	info->init_finished = 0;
	mutex_init(&info->lock);

	do {
		pr_err("%s:client addr is %d, cnt %d\n", __func__, info->client->addr, cnt);
		ret = bq2560x_get_chip_vendor_id(info);
		usleep_range(5000, 5500);
	} while ((ret < 0) && (cnt-- > 0));

	if ((ret <= 0) && (cnt-- <= 0)) {
		pr_err("%s: try to load driver by 41513 i2c addr %d\n", __func__, SGM41513_I2C_ADDR);
		info->client->addr = SGM41513_I2C_ADDR;
	}

	ret = device_property_read_bool(dev, "role-slave");
	if (ret)
		info->hw_mode = BQ25601_SLAVE;
	else
		info->hw_mode = BQ25601_MASTER;

	bq_info("dtsmode=%d, addr=0x%02X\n", info->hw_mode, client->addr);


	i2c_set_clientdata(client, info);

	info->usb_phy = devm_usb_get_phy_by_phandle(dev, "phys", 0);
	if (IS_ERR(info->usb_phy)) {
		bq_err("failed to find USB phy\n");
		goto GET_USB_PHY_FAILED;
	}

	info->edev = extcon_get_edev_by_phandle(info->dev, 0);
	if (IS_ERR(info->edev)) {
		bq_err("failed to find vbus extcon device.\n");
		return -EPROBE_DEFER;
	}

	ret = bq2560x_charger_register_vbus_regulator(info);
	if (ret) {
		bq_err("failed to register vbus regulator.\n");
		return -EPROBE_DEFER;
	}

	regmap_np = of_find_compatible_node(NULL, NULL, "sprd,sc27xx-syscon");
	if (!regmap_np)
		regmap_np = of_find_compatible_node(NULL, NULL, "sprd,ump962x-syscon");

	if (regmap_np) {
		if (of_device_is_compatible(regmap_np->parent, "sprd,sc2721"))
			info->charger_pd_mask = BQ2560X_DISABLE_PIN_MASK_2721;
		else
			info->charger_pd_mask = BQ2560X_DISABLE_PIN_MASK;
	} else {
		bq_err("unable to get syscon node\n");
		return -ENODEV;
	}

	ret = of_property_read_u32_index(regmap_np, "reg", 1,
					 &info->charger_detect);
	if (ret) {
		bq_err("failed to get charger_detect\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_index(regmap_np, "reg", 2,
					 &info->charger_pd);
	if (ret) {
		bq_err("failed to get charger_pd reg\n");
		return -EINVAL;
	}

	if (of_property_read_u32(dev->of_node, "vindpm-value", &info->vindpm_value) >= 0) {
		bq_info("vindpm-value use value is %d\n", info->vindpm_value);
	} else {
		bq_err("failed to get vindpm-value use default value %d\n", VCHG_CTRL_THRESHOLD_MV_072);
		info->vindpm_value = VCHG_CTRL_THRESHOLD_MV_072;
	}

	if (of_property_read_u32(dev->of_node, "boost-limit-value", &info->boost_limit) >= 0) {
		bq_info("boost-limit-value use value is %d\n", info->boost_limit);
	}

	info->gpiod = devm_gpiod_get(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(info->gpiod)) {
		bq_err("failed to get enable gpiod\n");
		info->gpiod = NULL;
	}

	regmap_pdev = of_find_device_by_node(regmap_np);
	if (!regmap_pdev) {
		of_node_put(regmap_np);
		bq_err("unable to get syscon device\n");
		return -ENODEV;
	}

	of_node_put(regmap_np);
	info->pmic = dev_get_regmap(regmap_pdev->dev.parent, NULL);
	if (!info->pmic) {
		bq_err("unable to get pmic regmap device\n");
		return -ENODEV;
	}

	info->usb_notify.notifier_call = bq2560x_charger_usb_change;
	ret = usb_register_notifier(info->usb_phy, &info->usb_notify);
	if (ret) {
		bq_err("failed to register notifier:%d\n", ret);
		goto GET_USB_PHY_FAILED;
	}

	if (info->hw_mode == BQ25601_MASTER) {
		charger_cfg.drv_data = info;
		charger_cfg.of_node = dev->of_node;
		info->psy_usb = devm_power_supply_register(dev,
							   &bq2560x_charger_desc,
							   &charger_cfg);
		if (IS_ERR(info->psy_usb)) {
			bq_err("failed to register power supply\n");
			goto USB_REGISTER_NOTIFIER_FAILED;
		}

		info->zte_psy_usb = zte_devm_power_supply_register(dev,
						   &zte_bq2560x_charger_desc,
						   &charger_cfg);
		if (IS_ERR(info->zte_psy_usb)) {
			dev_err(dev, "failed to register zte power supply\n");
		}
	}

	if (info->hw_mode == BQ25601_SLAVE) {
		charger_cfg.drv_data = info;
		charger_cfg.of_node = dev->of_node;

		info->zte_psy_usb = zte_devm_power_supply_register(dev,
						   &bq2560x_slave_charger_desc,
						   &charger_cfg);
		if (IS_ERR(info->zte_psy_usb)) {
			dev_err(dev, "failed to register zte power supply\n");
		}
	}


	ret = bq2560x_charger_hw_init(info);
	if (ret) {
		goto USB_REGISTER_NOTIFIER_FAILED;
	}

	if (info->hw_mode == BQ25601_MASTER) {
		proc_create_data("driver/chg_name", 0664, NULL,
			&bq2560x_chip_name_node, info);
	}

	bq2560x_charger_detect_status(info);
	alarm_init(&info->otg_timer, ALARM_BOOTTIME, NULL);
	INIT_DELAYED_WORK(&info->otg_work, bq2560x_charger_otg_work);
	INIT_DELAYED_WORK(&info->wdt_work,
			  bq2560x_charger_feed_watchdog_work);
	INIT_DELAYED_WORK(&info->vindpm_work, tuning_vindpm_work);

#ifdef CONFIG_VENDOR_SQC_CHARGER
	if (info->hw_mode == BQ25601_SLAVE) {
		sqc_ops = kzalloc(sizeof(struct sqc_pmic_chg_ops), GFP_KERNEL);
		memcpy(sqc_ops, &bq2560x_sqc_chg_ops, sizeof(struct sqc_pmic_chg_ops));
		sqc_ops->arg = (void *)info;
		ret = sqc_hal_charger_register(sqc_ops, SQC_CHARGER_PARALLEL1);
		if (ret < 0) {
			bq_err("%s register sqc hal fail(%d)\n", __func__, ret);
			goto USB_REGISTER_NOTIFIER_FAILED;
		}
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		info->zlog_client = zlog_register_client(&zlog_bc_dev2);
		if (!info->zlog_client)
			pr_err("%s zlog register client zlog_bc_dev2 fail\n", __func__);
		else
			pr_err("%s zlog register client zlog_bc_dev2 success\n", __func__);
#endif
	} else {

		sqc_ops = kzalloc(sizeof(struct sqc_pmic_chg_ops), GFP_KERNEL);
		memcpy(sqc_ops, &bq2560x_sqc_chg_ops, sizeof(struct sqc_pmic_chg_ops));
		sqc_ops->arg = (void *)info;
		ret = sqc_hal_charger_register(sqc_ops, SQC_CHARGER_PRIMARY);
		if (ret < 0) {
			bq_err("%s register sqc hal fail(%d)\n", __func__, ret);
			goto USB_REGISTER_NOTIFIER_FAILED;
		}
#ifndef CONFIG_FAST_CHARGER_SC27XX
		sqc_bc1d2_proto_node.arg = (void *)info;
		sqc_hal_bc1d2_register(&sqc_bc1d2_proto_node);

		zte_misc_register_callback(&qc3dp_sleep_mode_node, info);
#endif

	}
#endif

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		info->zlog_client = zlog_register_client(&zlog_bc_dev1);
		if (!info->zlog_client)
			pr_err("%s zlog register client zlog_bc_dev1 fail\n", __func__);
		else
			pr_err("%s zlog register client zlog_bc_dev1 success\n", __func__);
#endif

	if (info->hw_mode == BQ25601_SLAVE) {
		slave_chg_id = 1;
		bq_info("regist slave_id node!\n");
		zte_misc_register_callback(&slave_chg_id_node, info);
	}

	info->suspended = false;
	info->bq_wake_lock = wakeup_source_register(info->dev, "bq2560x_wake_lock");
	if (!info->bq_wake_lock) {
		pr_err("bq2560x wakelock register failed!\n");
		goto USB_REGISTER_NOTIFIER_FAILED;
	}

	bq_err("%s ok\n", __func__);

	info->init_finished = 1;

	return 0;
USB_REGISTER_NOTIFIER_FAILED:
	usb_unregister_notifier(info->usb_phy, &info->usb_notify);
GET_USB_PHY_FAILED:
	mutex_destroy(&info->lock);
	if (info)
		devm_kfree(info->dev, info);

	return -EPROBE_DEFER;
}

static void bq2560x_charger_shutdown(struct i2c_client *client)
{
	struct bq2560x_charger_info *info = i2c_get_clientdata(client);
	int ret = 0;

	dev_info(info->dev, "%s enter!\n", __func__);

	cancel_delayed_work_sync(&info->wdt_work);

	if (info->otg_enable) {
		info->otg_enable = false;
		cancel_delayed_work_sync(&info->otg_work);
		ret = bq2560x_update_bits(info, BQ2560X_REG_01,
					  BQ2560X_REG_OTG_MASK,
					  0);
		if (ret)
			dev_err(info->dev, "disable bq2560x otg failed ret = %d\n", ret);

		/* Enable charger detection function to identify the charger type */
		ret = regmap_update_bits(info->pmic, info->charger_detect,
					 BIT_DP_DM_BC_ENB, 0);

		if (ret)
			dev_err(info->dev,
				"enable charger detection function failed ret = %d\n", ret);
	}
}

static int bq2560x_charger_remove(struct i2c_client *client)
{
	struct bq2560x_charger_info *info = i2c_get_clientdata(client);

	info->init_finished = 0;

	usb_unregister_notifier(info->usb_phy, &info->usb_notify);

	wakeup_source_unregister(info->bq_wake_lock);
	return 0;
}


#if IS_ENABLED(CONFIG_PM_SLEEP)
static int bq2560x_charger_alarm_prepare(struct device *dev)
{
	struct bq2560x_charger_info *info = dev_get_drvdata(dev);
	ktime_t now, add;

	if (!info) {
		pr_err("%s: info is null!\n", __func__);
		return 0;
	}

	if (!info->otg_enable)
		return 0;

	now = ktime_get_boottime();
	add = ktime_set(BQ2560X_OTG_ALARM_TIMER_S, 0);
	alarm_start(&info->otg_timer, ktime_add(now, add));
	return 0;
}

static void bq2560x_charger_alarm_complete(struct device *dev)
{
	struct bq2560x_charger_info *info = dev_get_drvdata(dev);

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return;
	}

	if (!info->otg_enable)
		return;

	alarm_cancel(&info->otg_timer);
}

static int bq2560x_charger_suspend(struct device *dev)
{
	struct bq2560x_charger_info *info = dev_get_drvdata(dev);

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}
	if (info->otg_enable)
		bq2560x_charger_feed_watchdog(info, 1);

	info->suspended = true;

	if (!info->otg_enable)
		return 0;

	cancel_delayed_work_sync(&info->wdt_work);
	return 0;
}

static int bq2560x_charger_resume(struct device *dev)
{
	struct bq2560x_charger_info *info = dev_get_drvdata(dev);

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (info->otg_enable)
		bq2560x_charger_feed_watchdog(info, 1);

	info->suspended = false;

	if (!info->otg_enable)
		return 0;

	schedule_delayed_work(&info->wdt_work, HZ * 15);

	return 0;
}
#endif

static const struct dev_pm_ops bq2560x_charger_pm_ops = {
	.prepare = bq2560x_charger_alarm_prepare,
	.complete = bq2560x_charger_alarm_complete,
	SET_SYSTEM_SLEEP_PM_OPS(bq2560x_charger_suspend,
				bq2560x_charger_resume)
};
static struct i2c_driver bq2560x_charger_driver = {
	.driver = {
		.name = "bq2560x_chg",
		.of_match_table = bq2560x_charger_of_match,
		.pm = &bq2560x_charger_pm_ops,
	},
	.probe = bq2560x_charger_probe,
	.shutdown = bq2560x_charger_shutdown,
	.remove = bq2560x_charger_remove,
	.id_table = bq2560x_i2c_id,
};

module_i2c_driver(bq2560x_charger_driver);
MODULE_DESCRIPTION("BQ2560X Charger Driver");
MODULE_LICENSE("GPL v2");
