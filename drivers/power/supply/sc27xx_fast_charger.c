// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Spreadtrum Communications Inc.

#include <linux/extcon-provider.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/power/charger-manager.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#ifdef CONFIG_VENDOR_SQC_CHARGER
#include <sqc_common.h>
#include <vendor/common/zte_misc.h>

int sqc_notify_daemon_changed(int chg_id, int msg_type, int msg_val);
#endif

#define FCHG1_TIME1				0x0
#define FCHG1_TIME2				0x4
#define FCHG1_DELAY				0x8
#define FCHG2_DET_HIGH				0xc
#define FCHG2_DET_LOW				0x10
#define FCHG2_DET_LOW_CV			0x14
#define FCHG2_DET_HIGH_CV			0x18
#define FCHG2_DET_LOW_CC			0x1c
#define FCHG2_ADJ_TIME1				0x20
#define FCHG2_ADJ_TIME2				0x24
#define FCHG2_ADJ_TIME3				0x28
#define FCHG2_ADJ_TIME4				0x2c
#define FCHG_CTRL				0x30
#define FCHG_ADJ_CTRL				0x34
#define FCHG_INT_EN				0x38
#define FCHG_INT_CLR				0x3c
#define FCHG_INT_STS				0x40
#define FCHG_INT_STS0				0x44
#define FCHG_ERR_STS				0x48

#define SC2721_MODULE_EN0			0xC08
#define SC2721_CLK_EN0				0xC10
#define SC2721_IB_CTRL				0xEA4
#define SC2721_IB_TRIM_OFFSET			0x1e
#define SC2730_MODULE_EN0			0x1808
#define SC2730_CLK_EN0				0x1810
#define SC2730_IB_CTRL				0x1b84
#define SC2730_IB_TRIM_OFFSET			0x1e
#define UMP9620_MODULE_EN0			0x2008
#define UMP9620_CLK_EN0				0x2010
#define UMP9620_IB_CTRL				0x2384
#define UMP9620_IB_TRIM_OFFSET			0x1E

#define ANA_REG_IB_TRIM_MASK			GENMASK(6, 0)
#define ANA_REG_IB_TRIM_SHIFT			2
#define ANA_REG_IB_TRIM_MAX			0x7f
#define ANA_REG_IB_TRIM_EM_SEL_BIT		BIT(1)

#define FAST_CHARGE_MODULE_EN0_BIT		BIT(11)
#define FAST_CHARGE_RTC_CLK_EN0_BIT		BIT(4)

#define FCHG_ENABLE_BIT				BIT(0)
#define FCHG_INT_EN_BIT				BIT(1)
#define FCHG_INT_CLR_MASK			BIT(1)
#define FCHG_TIME1_MASK				GENMASK(10, 0)
#define FCHG_TIME2_MASK				GENMASK(11, 0)
#define FCHG_DET_VOL_MASK			GENMASK(1, 0)
#define FCHG_DET_VOL_SHIFT			3
#define FCHG_DET_VOL_EXIT_SFCP			3
#define FCHG_CALI_MASK				GENMASK(15, 9)
#define FCHG_CALI_SHIFT				9

#define FCHG_ERR0_BIT				BIT(1)
#define FCHG_ERR1_BIT				BIT(2)
#define FCHG_ERR2_BIT				BIT(3)
#define FCHG_OUT_OK_BIT				BIT(0)

#define FCHG_INT_STS_DETDONE			BIT(5)

/* FCHG1_TIME1_VALUE is used for detect the time of V > VT1 */
#define FCHG1_TIME1_VALUE			0x514
/* FCHG1_TIME2_VALUE is used for detect the time of V > VT2 */
#define FCHG1_TIME2_VALUE			0x9c4

#define FCHG_VOLTAGE_5V				5000000
#define FCHG_VOLTAGE_9V				9000000
#define FCHG_VOLTAGE_12V			12000000
#define FCHG_VOLTAGE_20V			20000000

#define FCHG_CURRENT_2A				2000000

#define SC27XX_FCHG_TIMEOUT			msecs_to_jiffies(5000)

struct sc27xx_fast_chg_data {
	u32 module_en;
	u32 clk_en;
	u32 ib_ctrl;
	u32 ib_trim_offset;
};

static const struct sc27xx_fast_chg_data sc2721_info = {
	.module_en = SC2721_MODULE_EN0,
	.clk_en = SC2721_CLK_EN0,
	.ib_ctrl = SC2721_IB_CTRL,
	.ib_trim_offset = SC2721_IB_TRIM_OFFSET,
};

static const struct sc27xx_fast_chg_data sc2730_info = {
	.module_en = SC2730_MODULE_EN0,
	.clk_en = SC2730_CLK_EN0,
	.ib_ctrl = SC2730_IB_CTRL,
	.ib_trim_offset = SC2730_IB_TRIM_OFFSET,
};

static const struct sc27xx_fast_chg_data ump9620_info = {
	.module_en = UMP9620_MODULE_EN0,
	.clk_en = UMP9620_CLK_EN0,
	.ib_ctrl = UMP9620_IB_CTRL,
	.ib_trim_offset = UMP9620_IB_TRIM_OFFSET,
};

struct sc27xx_fchg_info {
	struct device *dev;
	struct regmap *regmap;
	struct usb_phy *usb_phy;
	struct power_supply *psy_usb;
	struct delayed_work work;
	struct delayed_work delay_notify_sqc;
	struct workqueue_struct *delay_notify_queue;
	struct mutex lock;
	struct completion completion;
	u32 state;
	u32 base;
	int input_vol;
	u32 charger_online;
	bool detected;
	bool shutdown_flag;
	bool sfcp_enable;
	const struct sc27xx_fast_chg_data *pdata;
};

static int sqc_chg_type = SQC_NONE_TYPE;
extern int zte_sqc_set_prop_by_name(const char *name, enum zte_power_supply_property psp, int data);
static int sc27xx_fchg_internal_cur_calibration(struct sc27xx_fchg_info *info)
{
	struct nvmem_cell *cell;
	int calib_data, calib_current, ret;
	void *buf;
	size_t len;
	const struct sc27xx_fast_chg_data *pdata = info->pdata;

	cell = nvmem_cell_get(info->dev, "fchg_cur_calib");
	if (IS_ERR_OR_NULL(cell))
		return PTR_ERR(cell);

	buf = nvmem_cell_read(cell, &len);
	nvmem_cell_put(cell);

	if (IS_ERR(buf))
		return PTR_ERR(buf);

	memcpy(&calib_data, buf, min(len, sizeof(u32)));
	kfree(buf);

	/*
	 * In the handshake protocol behavior of sfcp, the current source
	 * of the fast charge internal module is small, we improve it
	 * by set the register ANA_REG_IB_CTRL. Now we add 30 level compensation.
	 */
	calib_current = (calib_data & FCHG_CALI_MASK) >> FCHG_CALI_SHIFT;
	calib_current += pdata->ib_trim_offset;

	if (calib_current < 0 || calib_current > ANA_REG_IB_TRIM_MAX) {
		dev_info(info->dev, "The compensated calib_current exceeds the range of IB_TRIM,"
			 " calib_current=%d\n", calib_current);
		calib_current = (calib_data & FCHG_CALI_MASK) >> FCHG_CALI_SHIFT;
	}

	ret = regmap_update_bits(info->regmap,
				 pdata->ib_ctrl,
				 ANA_REG_IB_TRIM_MASK << ANA_REG_IB_TRIM_SHIFT,
				 (calib_current & ANA_REG_IB_TRIM_MASK) << ANA_REG_IB_TRIM_SHIFT);
	if (ret) {
		dev_err(info->dev, "failed to calibrate fast charger current.\n");
		return ret;
	}

	/*
	 * Fast charge dm current source calibration mode, enable soft calibration mode.
	 */
	ret = regmap_update_bits(info->regmap, pdata->ib_ctrl,
				 ANA_REG_IB_TRIM_EM_SEL_BIT,
				 0);
	if (ret) {
		dev_err(info->dev, "failed to select ib trim mode.\n");
		return ret;
	}

	return 0;
}

static irqreturn_t sc27xx_fchg_interrupt(int irq, void *dev_id)
{
	struct sc27xx_fchg_info *info = dev_id;
	u32 int_sts, int_sts0;
	int ret;

	ret = regmap_read(info->regmap, info->base + FCHG_INT_STS, &int_sts);
	if (ret)
		return IRQ_RETVAL(ret);

	ret = regmap_read(info->regmap, info->base + FCHG_INT_STS0, &int_sts0);
	if (ret)
		return IRQ_RETVAL(ret);

	ret = regmap_update_bits(info->regmap, info->base + FCHG_INT_EN,
				 FCHG_INT_EN_BIT, 0);
	if (ret) {
		dev_err(info->dev, "failed to disable fast charger irq.\n");
		return IRQ_RETVAL(ret);
	}

	ret = regmap_update_bits(info->regmap, info->base + FCHG_INT_CLR,
				 FCHG_INT_CLR_MASK, FCHG_INT_CLR_MASK);
	if (ret) {
		dev_err(info->dev, "failed to clear fast charger interrupts\n");
		return IRQ_RETVAL(ret);
	}

	if ((int_sts & FCHG_INT_STS_DETDONE) && !(int_sts0 & FCHG_OUT_OK_BIT))
		dev_warn(info->dev,
			 "met some errors, now status = 0x%x, status0 = 0x%x\n",
			 int_sts, int_sts0);

	if ((int_sts & FCHG_INT_STS_DETDONE) && (int_sts0 & FCHG_OUT_OK_BIT)) {
		info->state = POWER_SUPPLY_CHARGE_TYPE_FAST;
		dev_info(info->dev, "setting sfcp 1.0 to fast type\n");
	} else {
		info->state = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
	}

	complete(&info->completion);

	return IRQ_HANDLED;
}

static int sc27xx_fchg_get_detect_status(struct sc27xx_fchg_info *info)
{
	unsigned long timeout;
	int value, ret;
	const struct sc27xx_fast_chg_data *pdata = info->pdata;

	dev_info(info->dev, "%s handshake chg_type=%d, detected=%d, shutdown_flag=%d\n",
			__func__, info->usb_phy->chg_type, info->detected, info->shutdown_flag);

	if (info->shutdown_flag)
		return POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
	/*
	 * In cold boot phase, system will detect fast charger status,
	 * if charger is not plugged in, it will cost another 2s
	 * to detect fast charger status, so we detect fast charger
	 * status only when DCP charger is plugged in
	 */
	if (info->usb_phy->chg_type != DCP_TYPE)
		return POWER_SUPPLY_USB_TYPE_UNKNOWN;

	reinit_completion(&info->completion);

	if (info->input_vol < FCHG_VOLTAGE_9V)
		value = 0;
	else if (info->input_vol < FCHG_VOLTAGE_12V)
		value = 1;
	else if (info->input_vol < FCHG_VOLTAGE_20V)
		value = 2;
	else
		value = 3;

	/*
	 * Due to the current source of the fast charge internal module is small
	 * we need to dynamically calibrate it through the software during the process
	 * of identifying fast charge. After fast charge recognition is completed, we
	 * disable soft calibration compensate function, in order to prevent the dm current
	 * source from deviating in accuracy when used in other modules.
	 */
	ret = sc27xx_fchg_internal_cur_calibration(info);
	if (ret) {
		dev_err(info->dev, "failed to set fast charger calibration.\n");
		return ret;
	}

	ret = regmap_update_bits(info->regmap, pdata->module_en,
				 FAST_CHARGE_MODULE_EN0_BIT,
				 FAST_CHARGE_MODULE_EN0_BIT);
	if (ret) {
		dev_err(info->dev, "failed to enable fast charger.\n");
		return ret;
	}

	ret = regmap_update_bits(info->regmap, pdata->clk_en,
				 FAST_CHARGE_RTC_CLK_EN0_BIT,
				 FAST_CHARGE_RTC_CLK_EN0_BIT);
	if (ret) {
		dev_err(info->dev,
			"failed to enable fast charger clock.\n");
		return ret;
	}

	ret = regmap_update_bits(info->regmap, info->base + FCHG1_TIME1,
				 FCHG_TIME1_MASK, FCHG1_TIME1_VALUE);
	if (ret) {
		dev_err(info->dev, "failed to set fast charge time1\n");
		return ret;
	}

	ret = regmap_update_bits(info->regmap, info->base + FCHG1_TIME2,
				 FCHG_TIME2_MASK, FCHG1_TIME2_VALUE);
	if (ret) {
		dev_err(info->dev, "failed to set fast charge time2\n");
		return ret;
	}

	ret = regmap_update_bits(info->regmap, info->base + FCHG_CTRL,
			FCHG_DET_VOL_MASK << FCHG_DET_VOL_SHIFT,
			(value & FCHG_DET_VOL_MASK) << FCHG_DET_VOL_SHIFT);
	if (ret) {
		dev_err(info->dev,
			"failed to set fast charger detect voltage.\n");
		return ret;
	}

	ret = regmap_update_bits(info->regmap, info->base + FCHG_CTRL,
				 FCHG_ENABLE_BIT, FCHG_ENABLE_BIT);
	if (ret) {
		dev_err(info->dev, "failed to enable fast charger.\n");
		return ret;
	}

	ret = regmap_update_bits(info->regmap, info->base + FCHG_INT_EN,
				 FCHG_INT_EN_BIT, FCHG_INT_EN_BIT);
	if (ret) {
		dev_err(info->dev, "failed to enable fast charger irq.\n");
		return ret;
	}

	timeout = wait_for_completion_timeout(&info->completion, SC27XX_FCHG_TIMEOUT);
	if (!timeout) {
		dev_err(info->dev, "timeout to get fast charger status\n");
		return POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
	}

	/*
	 * Fast charge dm current source calibration mode, select efuse calibration
	 * as default.
	 */
	ret = regmap_update_bits(info->regmap, pdata->ib_ctrl,
				 ANA_REG_IB_TRIM_EM_SEL_BIT,
				 ANA_REG_IB_TRIM_EM_SEL_BIT);
	if (ret) {
		dev_err(info->dev, "failed to select ib trim mode.\n");
		return ret;
	}

	pr_err("%s handshake info->state =%d,info->usb_phy->chg_type=%d\n", __func__, info->state, info->usb_phy->chg_type);

	return info->state;
}

static void sc27xx_fchg_disable(struct sc27xx_fchg_info *info)
{
	const struct sc27xx_fast_chg_data *pdata = info->pdata;
	int ret;

	/*
	 * must exit SFCP mode, otherwise the next BC1.2
	 * recognition will be affected.
	 */
	ret = regmap_update_bits(info->regmap, info->base + FCHG_CTRL,
				 FCHG_DET_VOL_MASK << FCHG_DET_VOL_SHIFT,
				 (FCHG_DET_VOL_EXIT_SFCP & FCHG_DET_VOL_MASK) << FCHG_DET_VOL_SHIFT);
	if (ret)
		dev_err(info->dev, "failed to set fast charger detect voltage.\n");

	ret = regmap_update_bits(info->regmap, info->base + FCHG_CTRL,
				 FCHG_ENABLE_BIT, 0);
	if (ret)
		dev_err(info->dev, "failed to disable fast charger.\n");

	/*
	 * Adding delay is to make sure writing the control register
	 * successfully firstly, then disable the module and clock.
	 */
	msleep(100);

	ret = regmap_update_bits(info->regmap, pdata->module_en,
				 FAST_CHARGE_MODULE_EN0_BIT, 0);
	if (ret)
		dev_err(info->dev, "failed to disable fast charger module.\n");

	ret = regmap_update_bits(info->regmap, pdata->clk_en,
				 FAST_CHARGE_RTC_CLK_EN0_BIT, 0);
	if (ret)
		dev_err(info->dev, "failed to disable charger clock.\n");
}

static int sc27xx_fchg_sfcp_adjust_voltage(struct sc27xx_fchg_info *info, u32 input_vol)
{
	int ret, value;

	if (input_vol < FCHG_VOLTAGE_9V)
		value = 0;
	else if (input_vol < FCHG_VOLTAGE_12V)
		value = 1;
	else if (input_vol < FCHG_VOLTAGE_20V)
		value = 2;
	else
		value = 3;

	ret = regmap_update_bits(info->regmap, info->base + FCHG_CTRL,
				 FCHG_DET_VOL_MASK << FCHG_DET_VOL_SHIFT,
				 (value & FCHG_DET_VOL_MASK) << FCHG_DET_VOL_SHIFT);
	if (ret) {
		dev_err(info->dev,
			"failed to set fast charger detect voltage.\n");
		return ret;
	}

	return 0;
}

static int sc27xx_fchg_usb_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct sc27xx_fchg_info *info = power_supply_get_drvdata(psy);
	int ret = 0;

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = info->state;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = FCHG_VOLTAGE_9V;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = FCHG_CURRENT_2A;
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&info->lock);
	return ret;
}

static int sc27xx_fchg_usb_set_property(struct power_supply *psy,
					enum power_supply_property psp,
					const union power_supply_propval *val)
{
	struct sc27xx_fchg_info *info = power_supply_get_drvdata(psy);
	int ret = 0;

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		pr_info("%s:handshake set online %d!!!\n", __func__, val->intval);
		if (val->intval == true) {
			info->charger_online = 1;
			schedule_delayed_work(&info->work, 0);
			break;
		} else if (val->intval == false) {
			info->charger_online = 0;
			complete(&info->completion);
			cancel_delayed_work(&info->work);
			schedule_delayed_work(&info->work, 0);
			break;
		}
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		ret = sc27xx_fchg_sfcp_adjust_voltage(info, val->intval);
		if (ret)
			dev_err(info->dev, "failed to adjust sfcp vol\n");
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:

		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&info->lock);
	return ret;
}

static int sc27xx_fchg_property_is_writeable(struct power_supply *psy,
					     enum power_supply_property psp)
{
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		ret = 1;
		break;
	default:
		ret = 0;
	}

	return ret;
}

static enum power_supply_property sc27xx_fchg_usb_props[] = {
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
};

static const struct power_supply_desc sc27xx_fchg_desc = {
	.name			= "sc27xx_fast_charger",
	.type			= POWER_SUPPLY_TYPE_UNKNOWN,
	.properties		= sc27xx_fchg_usb_props,
	.num_properties		= ARRAY_SIZE(sc27xx_fchg_usb_props),
	.get_property		= sc27xx_fchg_usb_get_property,
	.set_property		= sc27xx_fchg_usb_set_property,
	.property_is_writeable	= sc27xx_fchg_property_is_writeable,
};


#ifdef CONFIG_VENDOR_SQC_CHARGER_V8
extern struct sqc_bc1d2_proto_ops sqc_sfcp_ops_node;
static int sfcp_mode = SQC_SFCP_1P0_5V_TYPE;

static int sfcp_status_init(void)
{
	struct sc27xx_fchg_info *info =
		(struct sc27xx_fchg_info *)sqc_sfcp_ops_node.arg;

	if (!info) {
		pr_err("[SQC-HW]: info is null\n", __func__);
		return SQC_ADAPTER_ERROR;
	}

	sfcp_mode = SQC_SFCP_1P0_5V_TYPE;

	return SQC_ADAPTER_OK;
}

static int sfcp_status_end(void)
{
	struct sc27xx_fchg_info *info =
		(struct sc27xx_fchg_info *)sqc_sfcp_ops_node.arg;

	if (!info) {
		pr_err("[SQC-HW]: info is null\n", __func__);
		return SQC_ADAPTER_ERROR;
	}

	sfcp_mode = SQC_SFCP_1P0_5V_TYPE;

	pr_err("[SQC-HW]: %s\n", __func__);

	return SQC_ADAPTER_OK;
}

static int sfcp_get_charger_type(int *chg_type)
{
	struct sc27xx_fchg_info *info =
		(struct sc27xx_fchg_info *)sqc_sfcp_ops_node.arg;

	if (!info) {
		pr_err("[SQC-HW]: [%s] info is null\n", __func__);
		*chg_type = SQC_NONE_TYPE;
		return 0;
	}

	if (!info->charger_online && !info->usb_phy->chg_type) {
		*chg_type = SQC_NONE_TYPE;
		goto out_loop;
	}

	if (info->usb_phy->chg_type == DCP_TYPE) {
		if (info->sfcp_enable) {
			*chg_type = SQC_SFCP_1P0_TYPE;
		} else {
			*chg_type = SQC_DCP_TYPE;
		}
	} else if (info->usb_phy->chg_type == SDP_TYPE) {
		*chg_type = SQC_SDP_TYPE;
	} else if (info->usb_phy->chg_type == CDP_TYPE) {
		*chg_type = SQC_CDP_TYPE;
	} else if (info->usb_phy->chg_type == ACA_TYPE || info->usb_phy->chg_type == NON_DCP_TYPE) {
		*chg_type = SQC_FLOAT_TYPE;
	} else {
		*chg_type = SQC_FLOAT_TYPE;
	}

	if ((sqc_chg_type == SQC_SLEEP_MODE_TYPE)
			&& (*chg_type == SQC_NONE_TYPE)) {
		sqc_chg_type = *chg_type;
		zte_sqc_set_prop_by_name("zte_battery", POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, 1);
	} else if (sqc_chg_type == SQC_SLEEP_MODE_TYPE) {
		*chg_type = sqc_chg_type;
	}

out_loop:
	pr_info("[SQC-HW]: [%s] charger_online: %d, sprd_type: %d, chg_type: %d\n",
		__func__, info->charger_online, info->usb_phy->chg_type, *chg_type);

	return 0;
}

static int sfcp_set_charger_type(int type)
{
	int ret = 0;
	struct sc27xx_fchg_info *info =
		(struct sc27xx_fchg_info *)sqc_sfcp_ops_node.arg;

	if (!info) {
		pr_err("[SQC-HW]: [%s] info is null\n", __func__);
		return 0;
	}

	if (sfcp_mode == type) {
		pr_err("[SQC-HW]: [%s] sfcp_mode & type are equal, return\n", __func__);
		return 0;
	}

	sfcp_mode = type;

	switch (type) {
	case SQC_SFCP_1P0_5V_TYPE:
		ret = sc27xx_fchg_sfcp_adjust_voltage(info, FCHG_VOLTAGE_5V);
		if (ret)
			dev_err(info->dev, "failed to adjust sfcp vol\n");
		break;
	case SQC_SFCP_1P0_9V_TYPE:
		ret = sc27xx_fchg_sfcp_adjust_voltage(info, FCHG_VOLTAGE_9V);
		if (ret)
			dev_err(info->dev, "failed to adjust sfcp vol\n");
		break;
	case SQC_SFCP_1P0_12V_TYPE:
		ret = sc27xx_fchg_sfcp_adjust_voltage(info, FCHG_VOLTAGE_12V);
		if (ret)
			dev_err(info->dev, "failed to adjust sfcp vol\n");
		break;
	default:
		pr_err("[SQC-HW]: [%s] charing type: UNKNOWN\n", __func__);
		return 0;
	}

	pr_info("[SQC-HW]: [%s] sfcp_set_charger_type %d\n", __func__, type);

	return 0;
}

static int sfcp_get_protocol_status(unsigned int *status)
{
	return 0;
}

static int sfcp_get_chip_vendor_id(unsigned int *vendor_id)
{
	return 0;
}

static int sfcp_set_1p0_dp(unsigned int dp_cnt)
{
	return 0;
}

static int sfcp_set_1p0_dm(unsigned int dm_cnt)
{
	return 0;
}

static int sfcp_set_1p0_plus_dp(unsigned int dp_cnt)
{
	return 0;
}

static int sfcp_set_1p0_plus_dm(unsigned int dm_cnt)
{
	return 0;
}

struct sqc_bc1d2_proto_ops sqc_sfcp_ops_node = {
	.status_init = sfcp_status_init,
	.status_remove = sfcp_status_end,
	.get_charger_type = sfcp_get_charger_type,
	.set_charger_type = sfcp_set_charger_type,
	.get_protocol_status = sfcp_get_protocol_status,
	.get_chip_vendor_id = sfcp_get_chip_vendor_id,
	.set_qc3d0_dp = sfcp_set_1p0_dp,
	.set_qc3d0_dm = sfcp_set_1p0_dm,
	.set_qc3d0_plus_dp = sfcp_set_1p0_plus_dp,
	.set_qc3d0_plus_dm = sfcp_set_1p0_plus_dm,

};

static void sqc_delay_notify_daemon(struct work_struct *data)
{
	struct delayed_work *dwork = to_delayed_work(data);
	struct sc27xx_fchg_info *info =
		container_of(dwork, struct sc27xx_fchg_info, delay_notify_sqc);

	if (info->sfcp_enable) {
		pr_err("[SQC-HW]: [%s] notify 1\n", __func__);
		sqc_notify_daemon_changed(SQC_NOTIFY_USB,
					SQC_NOTIFY_USB_STATUS_CHANGED, 1);
	} else {
		pr_err("[SQC-HW]: [%s] notify 0\n", __func__);
		sqc_notify_daemon_changed(SQC_NOTIFY_USB,
					SQC_NOTIFY_USB_STATUS_CHANGED, 0);
	}

}
#endif

static void sc27xx_fchg_work(struct work_struct *data)
{
	struct delayed_work *dwork = to_delayed_work(data);
	struct sc27xx_fchg_info *info = container_of(dwork, struct sc27xx_fchg_info, work);

#ifdef CONFIG_VENDOR_SQC_CHARGER_V8
	static bool sfcp_enable = 0;
#endif
	dev_info(info->dev, "handshake charger_online=%d, detected=%d, shutdown_flag=%d\n",
			info->charger_online, info->detected, info->shutdown_flag);

	mutex_lock(&info->lock);
	if (!info->charger_online) {
		info->state = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
		info->detected = false;
		info->sfcp_enable = false;
		sc27xx_fchg_disable(info);
	} else if (!info->detected && !info->shutdown_flag) {
		info->detected = true;

		if (sc27xx_fchg_get_detect_status(info) == POWER_SUPPLY_CHARGE_TYPE_FAST) {
			/*
			 * Must release info->lock before send fast charge event
			 * to charger manager, otherwise it will cause deadlock.
			 */
			info->sfcp_enable = true;
			mutex_unlock(&info->lock);
			power_supply_changed(info->psy_usb);
			dev_info(info->dev, "handshake sfcp_enable\n");

#ifdef CONFIG_VENDOR_SQC_CHARGER_V8
				if ((info->sfcp_enable != sfcp_enable)) {
					sqc_notify_daemon_changed(SQC_NOTIFY_USB,
								SQC_NOTIFY_USB_STATUS_CHANGED, 1);
					pr_err("[SQC-HW]: [%s] notify 1\n", __func__);
				} else {
					sqc_notify_daemon_changed(SQC_NOTIFY_USB,
								SQC_NOTIFY_USB_STATUS_CHANGED, 0);
					pr_err("[SQC-HW]: [%s] notify 0\n", __func__);
				}
				sfcp_enable = info->sfcp_enable;
#endif
			return;
		}

		sc27xx_fchg_disable(info);
	}

	mutex_unlock(&info->lock);

	dev_info(info->dev, "sfcp_enable = %d\n", info->sfcp_enable);
}

#ifdef CONFIG_ZTE_POWER_SUPPLY_COMMON
int sqc_sleep_node_set(const char *val, const void *arg)
{
	int sleep_mode_enable = 0;

	sscanf(val, "%d", &sleep_mode_enable);

	pr_info("%s, line:%d sleep_mode_enable = %d\n", __func__, __LINE__, sleep_mode_enable);

	if (sleep_mode_enable) {
		if (sqc_chg_type != SQC_SLEEP_MODE_TYPE) {
			pr_info("%s, line:%d sleep on status!\n", __func__, __LINE__);

			/*disabel sqc-daemon*/
			sqc_chg_type = SQC_SLEEP_MODE_TYPE;
			sqc_notify_daemon_changed(SQC_NOTIFY_USB, SQC_NOTIFY_USB_STATUS_CHANGED, 1);
#ifdef CONFIG_VENDOR_SQC_BQ2560X
			pr_info("%s, line:%d disable bq2660x watchdog!\n", __func__, __LINE__);
			zte_sqc_set_prop_by_name("zte_bq2560x_charger", POWER_SUPPLY_PROP_FEED_WATCHDOG, 0);
#endif
			/*mtk enter sleep mode*/
			zte_sqc_set_prop_by_name("zte_battery", POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, 0);
		}
	} else {
		if (sqc_chg_type != SQC_SLEEP_MODE_TYPE) {
			pr_info("%s, line:%d sleep off status!\n", __func__, __LINE__);
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

static int sc27xx_fchg_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct sc27xx_fchg_info *info;
	struct power_supply_config charger_cfg = { };
	int irq, ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	mutex_init(&info->lock);
	info->dev = &pdev->dev;
	info->state = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
	info->pdata = of_device_get_match_data(info->dev);
	if (!info->pdata) {
		dev_err(info->dev, "no matching driver data found\n");
		return -EINVAL;
	}

	INIT_DELAYED_WORK(&info->work, sc27xx_fchg_work);
#ifdef CONFIG_VENDOR_SQC_CHARGER
	info->delay_notify_queue = create_singlethread_workqueue("sqc_notify_queue");
	INIT_DELAYED_WORK(&info->delay_notify_sqc, sqc_delay_notify_daemon);
#endif
	init_completion(&info->completion);

	info->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!info->regmap) {
		dev_err(&pdev->dev, "failed to get charger regmap\n");
		return -ENODEV;
	}

	ret = of_property_read_u32(np, "reg", &info->base);
	if (ret) {
		dev_err(&pdev->dev, "failed to get register address\n");
		return -ENODEV;
	}

	ret = device_property_read_u32(&pdev->dev,
				       "sprd,input-voltage-microvolt",
				       &info->input_vol);
	if (ret) {
		dev_err(&pdev->dev, "failed to get fast charger voltage.\n");
		return ret;
	}

	platform_set_drvdata(pdev, info);

	info->usb_phy = devm_usb_get_phy_by_phandle(&pdev->dev, "phys", 0);
	if (IS_ERR(info->usb_phy)) {
		dev_err(&pdev->dev, "failed to find USB phy\n");
		return PTR_ERR(info->usb_phy);
	}
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq resource specified\n");
		return irq;
	}
	ret = devm_request_threaded_irq(info->dev, irq, NULL,
					sc27xx_fchg_interrupt,
					IRQF_NO_SUSPEND | IRQF_ONESHOT,
					pdev->name, info);
	if (ret) {
		dev_err(&pdev->dev, "failed to request irq.\n");
		return ret;
	}

	charger_cfg.drv_data = info;
	charger_cfg.of_node = np;

	info->psy_usb = devm_power_supply_register(&pdev->dev,
						   &sc27xx_fchg_desc,
						   &charger_cfg);
	if (IS_ERR(info->psy_usb)) {
		dev_err(&pdev->dev, "failed to register power supply\n");
		return PTR_ERR(info->psy_usb);
	}

#ifdef CONFIG_VENDOR_SQC_CHARGER_V8
	sqc_sfcp_ops_node.arg = (void *)info;
	dev_err(&pdev->dev, "regist bc1d2\n");
	sqc_hal_bc1d2_register(&sqc_sfcp_ops_node);
	zte_misc_register_callback(&qc3dp_sleep_mode_node, info);
#endif
	return 0;
}

static int sc27xx_fchg_remove(struct platform_device *pdev)
{
	return 0;
}

static void sc27xx_fchg_shutdown(struct platform_device *pdev)
{
	struct sc27xx_fchg_info *info = platform_get_drvdata(pdev);
	int ret;
	u32 value = FCHG_DET_VOL_EXIT_SFCP;
	const struct sc27xx_fast_chg_data *pdata = info->pdata;

	info->shutdown_flag = true;
	cancel_delayed_work_sync(&info->work);

	/*
	 * SFCP will handsharke failed from charging in shut down
	 * to charging in power up, because SFCP is not exit before
	 * shut down. Set bit3:4 to 2b'11 to exit SFCP.
	 */

	ret = regmap_update_bits(info->regmap, info->base + FCHG_CTRL,
				 FCHG_DET_VOL_MASK << FCHG_DET_VOL_SHIFT,
				 (value & FCHG_DET_VOL_MASK) << FCHG_DET_VOL_SHIFT);
	if (ret)
		dev_err(info->dev,
			"failed to set fast charger detect voltage.\n");

	/*
	 * Fast charge dm current source calibration mode, select efuse calibration
	 * as default.
	 */
	ret = regmap_update_bits(info->regmap, pdata->ib_ctrl,
				 ANA_REG_IB_TRIM_EM_SEL_BIT,
				 ANA_REG_IB_TRIM_EM_SEL_BIT);
	if (ret)
		dev_err(info->dev, "%s, failed to select ib trim mode.\n", __func__);
}

static const struct of_device_id sc27xx_fchg_of_match[] = {
	{ .compatible = "sprd,sc2730-fast-charger", .data = &sc2730_info },
	{ .compatible = "sprd,ump9620-fast-chg", .data = &ump9620_info },
	{ .compatible = "sprd,sc2721-fast-charger", .data = &sc2721_info },
	{ }
};

static struct platform_driver sc27xx_fchg_driver = {
	.driver = {
		.name = "sc27xx-fast-charger",
		.of_match_table = sc27xx_fchg_of_match,
	},
	.probe = sc27xx_fchg_probe,
	.remove = sc27xx_fchg_remove,
	.shutdown = sc27xx_fchg_shutdown,
};

module_platform_driver(sc27xx_fchg_driver);

MODULE_DESCRIPTION("Spreadtrum SC27XX Fast Charger Driver");
MODULE_LICENSE("GPL v2");
