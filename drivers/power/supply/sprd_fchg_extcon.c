// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2011 Unisoc Co., Ltd.
 * Youxu.Zeng <Youxu.Zeng@unisoc.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/power/sprd_fchg_extcon.h>

#ifdef CONFIG_VENDOR_SQC_CHARGER
#include <sqc_common.h>
#include <vendor/common/zte_misc.h>

int sqc_notify_daemon_changed(int chg_id, int msg_type, int msg_val);
#endif

#if IS_ENABLED(CONFIG_FAST_CHARGER_SC27XX)
static int sprd_get_sfcp_fixed_current_max(struct sprd_fchg_info *info, u32 *max_cur)
{
	union power_supply_propval val;
	struct power_supply *psy_sfcp;
	int ret;

	psy_sfcp = power_supply_get_by_name(SPRD_FCHG_SFCP_NAME);
	if (!psy_sfcp) {
		dev_err(info->dev, "%s, psy_sfcp is NULL\n", __func__);
		return -ENODEV;
	}

	ret = power_supply_get_property(psy_sfcp, POWER_SUPPLY_PROP_CURRENT_MAX, &val);
	if (ret) {
		dev_err(info->dev, "%s, failed to get current property, ret=%d\n", __func__, ret);
		return ret;
	}

	*max_cur = val.intval;
	return ret;
}

static int sprd_get_sfcp_fixed_voltage_max(struct sprd_fchg_info *info, u32 *max_vol)
{
	union power_supply_propval val;
	struct power_supply *psy_sfcp;
	int ret;

	psy_sfcp = power_supply_get_by_name(SPRD_FCHG_SFCP_NAME);
	if (!psy_sfcp) {
		dev_err(info->dev, "%s, psy_sfcp is NULL\n", __func__);
		return -ENODEV;
	}

	ret = power_supply_get_property(psy_sfcp, POWER_SUPPLY_PROP_VOLTAGE_MAX, &val);
	if (ret) {
		dev_err(info->dev, "%s, failed to get vol property, ret=%d\n", __func__, ret);
		return ret;
	}

	*max_vol = val.intval;
	return ret;
}

static int sprd_sfcp_fixed_adjust_voltage(struct sprd_fchg_info *info, u32 input_vol)
{
	union power_supply_propval val;
	struct power_supply *psy_sfcp;
	int ret;

	psy_sfcp = power_supply_get_by_name(SPRD_FCHG_SFCP_NAME);
	if (!psy_sfcp) {
		dev_err(info->dev, "%s, psy_sfcp is NULL\n", __func__);
		return -ENODEV;
	}

	val.intval = input_vol;
	ret = power_supply_set_property(psy_sfcp, POWER_SUPPLY_PROP_VOLTAGE_MAX, &val);
	if (ret)
		dev_err(info->dev, "%s, failed to get vol property, ret=%d\n", __func__, ret);

	return ret;
}

static int sprd_enable_sfcp(struct sprd_fchg_info *info, bool enable)
{
	union power_supply_propval val;
	struct power_supply *psy_sfcp;
	static bool sfcp_active;
	int ret;

	dev_err(info->dev, "%s, handshake enable=%d, sfcp_enable=%d\n", __func__, enable, info->sfcp_enable);

	psy_sfcp = power_supply_get_by_name(SPRD_FCHG_SFCP_NAME);
	if (!psy_sfcp) {
		dev_err(info->dev, "%s, psy_sfcp is NULL\n", __func__);
		return -ENODEV;
	}

	if (enable && !info->sfcp_enable) {
		val.intval = true;
		ret = power_supply_set_property(psy_sfcp, POWER_SUPPLY_PROP_ONLINE, &val);
		if (ret) {
			dev_err(info->dev, "%s, failed to enable sfcp, ret=%d\n", __func__, ret);
			return ret;
		}

		sfcp_active = true;
	} else if (!enable && sfcp_active) {
		val.intval = false;
		ret = power_supply_set_property(psy_sfcp, POWER_SUPPLY_PROP_ONLINE, &val);
		if (ret) {
			dev_err(info->dev, "%s, failed to disable sfcp, ret=%d\n", __func__, ret);
			return ret;
		}

		sfcp_active = false;
		info->sfcp_enable = false;
	}

	return 0;
}
#else
static int sprd_get_sfcp_fixed_current_max(struct sprd_fchg_info *info, u32 *max_cur)
{
	return 0;
}

static int sprd_get_sfcp_fixed_voltage_max(struct sprd_fchg_info *info, u32 *max_vol)
{
	return 0;
}

static int sprd_sfcp_fixed_adjust_voltage(struct sprd_fchg_info *info, u32 input_vol)
{
	return 0;
}

static int sprd_enable_sfcp(struct sprd_fchg_info *info, bool enable)
{
	return 0;
}
#endif

#if IS_ENABLED(CONFIG_SPRD_TYPEC_TCPM)
static int sprd_get_pd_adapter_voltage_max(struct sprd_fchg_info *info, u32 *max_vol)
{
	struct power_supply *psy_tcpm;
	struct sprd_tcpm_port *port;
	enum sprd_pd_pdo_type pd_pdo_type = SPRD_PDO_TYPE_FIXED;
	int i, adptor_max_vbus = 0;

	psy_tcpm = power_supply_get_by_name(SPRD_FCHG_TCPM_PD_NAME);
	if (!psy_tcpm) {
		dev_err(info->dev, "%s, psy_tcpm is NULL\n", __func__);
		return -ENODEV;
	}

	port = power_supply_get_drvdata(psy_tcpm);
	if (!port) {
		dev_err(info->dev, "%s, failed to get tcpm port\n", __func__);
		return -EINVAL;
	}

	sprd_tcpm_get_source_capabilities(port, &info->pd_source_cap);
	if (!info->pd_source_cap.nr_source_caps) {
		dev_err(info->dev, "%s, failed to obtain the PD power supply capacity\n", __func__);
		return -EINVAL;
	}

	if (info->pps_enable && !info->pps_active)
		pd_pdo_type = SPRD_PDO_TYPE_APDO;

	for (i = 0; i < info->pd_source_cap.nr_source_caps; i++) {
		if (info->pd_source_cap.type[i] == pd_pdo_type &&
		    adptor_max_vbus < info->pd_source_cap.max_mv[i])
			adptor_max_vbus = info->pd_source_cap.max_mv[i];
	}

	*max_vol = adptor_max_vbus * 1000;

	return 0;
}

static int sprd_get_pd_adapter_current(struct sprd_fchg_info *info,
				       u32 vbus_max_vol,
				       u32 *adapter_cur)
{
	struct power_supply *psy_tcpm;
	struct sprd_tcpm_port *port;
	enum sprd_pd_pdo_type pd_pdo_type = SPRD_PDO_TYPE_FIXED;
	int i, ret = 0, adapter_ibus = 0;

	psy_tcpm = power_supply_get_by_name(SPRD_FCHG_TCPM_PD_NAME);
	if (!psy_tcpm) {
		dev_err(info->dev, "%s, psy_tcpm is NULL\n", __func__);
		ret = -ENODEV;
		goto done;
	}

	port = power_supply_get_drvdata(psy_tcpm);
	if (!port) {
		dev_err(info->dev, "%s, failed to get tcpm port\n", __func__);
		ret = -EINVAL;
		goto done;
	}

	sprd_tcpm_get_source_capabilities(port, &info->pd_source_cap);
	if (!info->pd_source_cap.nr_source_caps) {
		dev_err(info->dev, "%s, failed to obtain the PD power supply capacity\n", __func__);
		ret = -EINVAL;
		goto done;
	}

	if (info->pps_enable && !info->pps_active)
		pd_pdo_type = SPRD_PDO_TYPE_APDO;

	for (i = 0; i < info->pd_source_cap.nr_source_caps; i++) {
		if (info->pd_source_cap.type[i] == pd_pdo_type &&
		    vbus_max_vol >= info->pd_source_cap.max_mv[i])
			adapter_ibus = info->pd_source_cap.ma[i];
	}

done:
	*adapter_cur = adapter_ibus * 1000;

	return ret;
}

static int sprd_get_pps_voltage_max(struct sprd_fchg_info *info, u32 *max_vol)
{
	union power_supply_propval val;
	struct power_supply *psy_tcpm;
	int ret;

	psy_tcpm = power_supply_get_by_name(SPRD_FCHG_TCPM_PD_NAME);
	if (!psy_tcpm) {
		dev_err(info->dev, "%s, psy_tcpm is NULL\n", __func__);
		return -ENODEV;
	}

	ret = power_supply_get_property(psy_tcpm,
					POWER_SUPPLY_PROP_VOLTAGE_MAX,
					&val);
	if (ret) {
		dev_err(info->dev, "%s, failed to get online property, ret=%d\n", __func__, ret);
		return ret;
	}

	*max_vol = val.intval;

	return ret;
}

static int sprd_get_pps_current_max(struct sprd_fchg_info *info, u32 *max_cur)
{
	union power_supply_propval val;
	struct power_supply *psy_tcpm;
	int ret;

	psy_tcpm = power_supply_get_by_name(SPRD_FCHG_TCPM_PD_NAME);
	if (!psy_tcpm) {
		dev_err(info->dev, "%s, psy_tcpm is NULL\n", __func__);
		return -ENODEV;
	}

	ret = power_supply_get_property(psy_tcpm, POWER_SUPPLY_PROP_CURRENT_MAX, &val);
	if (ret) {
		dev_err(info->dev, "%s, failed to get pps current max, ret=%d\n", __func__, ret);
		return ret;
	}

	*max_cur = val.intval;

	return ret;
}

static int sprd_pd_fixed_adjust_voltage(struct sprd_fchg_info *info, u32 input_vol)
{
	struct sprd_tcpm_port *port;
	struct power_supply *psy_tcpm;
	int ret, i, index = -1;
	u32 pdo[SPRD_PDO_MAX_OBJECTS];
	unsigned int snk_uw;

	psy_tcpm = power_supply_get_by_name(SPRD_FCHG_TCPM_PD_NAME);
	if (!psy_tcpm) {
		dev_err(info->dev, "%s, psy_tcpm is NULL\n", __func__);
		return -ENODEV;
	}

	port = power_supply_get_drvdata(psy_tcpm);
	if (!port) {
		dev_err(info->dev, "%s, failed to get tcpm port\n", __func__);
		return -EINVAL;
	}

	sprd_tcpm_get_source_capabilities(port, &info->pd_source_cap);
	if (!info->pd_source_cap.nr_source_caps) {
		pdo[0] = SPRD_PDO_FIXED(5000, 2000, 0);
		snk_uw = SPRD_PD_DEFAULT_POWER_UW;
		index = 0;
		goto done;
	}

	for (i = 0; i < info->pd_source_cap.nr_source_caps; i++) {
		if ((info->pd_source_cap.max_mv[i] <= input_vol / 1000) &&
		    (info->pd_source_cap.type[i] == SPRD_PDO_TYPE_FIXED))
			index = i;
	}

	/*
	 * Ensure that index is within a valid range to prevent arrays
	 * from crossing bounds.
	 */
	if (index < 0 || index >= info->pd_source_cap.nr_source_caps) {
		dev_err(info->dev, "%s, Index is invalid!!!\n", __func__);
		return -EINVAL;
	}

	snk_uw = info->pd_source_cap.max_mv[index] * info->pd_source_cap.ma[index];
	if (snk_uw > info->pd_fixed_max_uw)
		snk_uw = info->pd_fixed_max_uw;

	for (i = 0; i < index + 1; i++) {
		pdo[i] = SPRD_PDO_FIXED(info->pd_source_cap.max_mv[i],
					info->pd_source_cap.ma[i],
					0);
		if (info->pd_source_cap.max_mv[i] * info->pd_source_cap.ma[i] > snk_uw)
			pdo[i] = SPRD_PDO_FIXED(info->pd_source_cap.max_mv[i],
						snk_uw / info->pd_source_cap.max_mv[i],
						0);
	}

done:
	ret = sprd_tcpm_update_sink_capabilities(port, pdo, index + 1, snk_uw / 1000);
	if (ret) {
		dev_err(info->dev, "%s, failed to set pd, ret = %d\n", __func__, ret);
		return ret;
	}

	return 0;
}

static int sprd_pd_update_source_capabilities(struct sprd_fchg_info *info,
					      const u32 *pdo,
					      unsigned int nr_pdo)
{
	struct sprd_tcpm_port *port;
	struct power_supply *psy_tcpm;
	int ret;

	psy_tcpm = power_supply_get_by_name(SPRD_FCHG_TCPM_PD_NAME);
	if (!psy_tcpm) {
		dev_err(info->dev, "%s, psy_tcpm is NULL\n", __func__);
		return -ENODEV;
	}

	port = power_supply_get_drvdata(psy_tcpm);
	if (!port) {
		dev_err(info->dev, "%s, failed to get tcpm port\n", __func__);
		return -EINVAL;
	}

	ret = sprd_tcpm_update_ext_source_capabilities(port, pdo, nr_pdo);
	if (ret) {
		dev_err(info->dev, "%s, failed to update src cap, ret = %d\n", __func__, ret);
		return ret;
	}

	return 0;
}

static int sprd_pps_adjust_voltage(struct sprd_fchg_info *info, u32 input_vol)
{
	union power_supply_propval val;
	struct power_supply *psy_tcpm;
	int ret;

	psy_tcpm = power_supply_get_by_name(SPRD_FCHG_TCPM_PD_NAME);
	if (!psy_tcpm) {
		dev_err(info->dev, "%s, psy_tcpm is NULL\n", __func__);
		return -ENODEV;
	}

	if (!info->pps_active) {
		val.intval = SPRD_ENABLE_PPS;
		ret = power_supply_set_property(psy_tcpm, POWER_SUPPLY_PROP_ONLINE, &val);
		if (ret) {
			dev_err(info->dev, "%s, failed to set online property ret = %d\n",
				__func__, ret);
			return ret;
		}
		info->pps_active = true;
	}

	val.intval = input_vol;
	ret = power_supply_set_property(psy_tcpm, POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
	if (ret)
		dev_err(info->dev, "%s, failed to set vol property, ret=%d\n", __func__, ret);

	return ret;
}

static int sprd_pps_adjust_current(struct sprd_fchg_info *info, u32 input_current)
{
	union power_supply_propval val;
	struct power_supply *psy_tcpm;
	int ret;

	psy_tcpm = power_supply_get_by_name(SPRD_FCHG_TCPM_PD_NAME);
	if (!psy_tcpm) {
		dev_err(info->dev, "%s, psy_tcpm is NULL\n", __func__);
		return -ENODEV;
	}

	if (!info->pps_active) {
		val.intval = SPRD_ENABLE_PPS;
		ret = power_supply_set_property(psy_tcpm, POWER_SUPPLY_PROP_ONLINE, &val);
		if (ret) {
			dev_err(info->dev, "%s, failed to set online property, ret=%d\n",
				__func__, ret);
			return ret;
		}
		info->pps_active = true;
	}

	val.intval = input_current;
	ret = power_supply_set_property(psy_tcpm, POWER_SUPPLY_PROP_CURRENT_NOW, &val);
	if (ret)
		dev_err(info->dev, "%s, failed to set current property, ret=%d\n",
			__func__, ret);

	return ret;
}

static int sprd_enable_pps(struct sprd_fchg_info *info, bool enable)
{
	union power_supply_propval val;
	struct power_supply *psy_tcpm;
	int ret;

	psy_tcpm = power_supply_get_by_name(SPRD_FCHG_TCPM_PD_NAME);
	if (!psy_tcpm) {
		dev_err(info->dev, "%s, psy_tcpm is NULL\n", __func__);
		return -ENODEV;
	}

	if (info->pps_active && !enable) {
		val.intval = SPRD_DISABLE_PPS;
		ret = power_supply_set_property(psy_tcpm, POWER_SUPPLY_PROP_ONLINE, &val);
		if (ret) {
			dev_err(info->dev, "%s, failed to disbale pps, ret = %d\n", __func__, ret);
			return ret;
		}
		info->pps_active = false;
	} else if (!info->pps_active && enable) {
		val.intval = SPRD_ENABLE_PPS;
		ret = power_supply_set_property(psy_tcpm, POWER_SUPPLY_PROP_ONLINE, &val);
		if (ret) {
			dev_err(info->dev, "%s, failed to enable pps, ret = %d\n", __func__, ret);
			return ret;
		}
		info->pps_active = true;
	}

	return 0;
}

static void sprd_detect_pd_online_work(struct work_struct *data)
{
	struct sprd_fchg_info *info = container_of(data, struct sprd_fchg_info, pd_online_work);
	union power_supply_propval val;
	struct power_supply *psy_tcpm;
	int ret = 0;

	mutex_lock(&info->lock);
	psy_tcpm = power_supply_get_by_name(SPRD_FCHG_TCPM_PD_NAME);
	if (!psy_tcpm) {
		dev_err(info->dev, "%s, psy_tcpm is NULL\n", __func__);
		goto out;
	}

	ret = power_supply_get_property(psy_tcpm, POWER_SUPPLY_PROP_USB_TYPE, &val);
	if (ret) {
		dev_err(info->dev, "%s, failed to get usb type property, ret=%d\n",
			__func__, ret);
		goto out;
	}

	if (val.intval == POWER_SUPPLY_USB_TYPE_PD ||
	    (!info->support_pd_pps && val.intval == POWER_SUPPLY_USB_TYPE_PD_PPS)) {
		info->pd_enable = true;
		info->pps_enable = false;
		info->pps_active = false;
		info->fchg_type = POWER_SUPPLY_CHARGE_TYPE_FAST;
		mutex_unlock(&info->lock);
		cm_notify_event(info->psy, CM_EVENT_FAST_CHARGE, NULL);
		goto out1;
	} else if (val.intval == POWER_SUPPLY_USB_TYPE_PD_PPS) {
		info->pps_enable = true;
		info->pd_enable = false;
		info->fchg_type = POWER_SUPPLY_CHARGE_TYPE_ADAPTIVE;
		mutex_unlock(&info->lock);
		cm_notify_event(info->psy, CM_EVENT_FAST_CHARGE, NULL);
		goto out1;
	}

out:
	mutex_unlock(&info->lock);

out1:
	dev_info(info->dev, "%s, fchg type = %d\n", __func__, info->fchg_type);
}
#else
static int sprd_get_pd_adapter_voltage_max(struct sprd_fchg_info *info, u32 *max_vol)
{
	return 0;
}

static int sprd_get_pd_adapter_current(struct sprd_fchg_info *info,
				       u32 vbus_max_vol,
				       u32 *adapter_cur)
{
	return 0;
}

static int sprd_get_pps_voltage_max(struct sprd_fchg_info *info, u32 *max_vol)
{
	return 0;
}

static int sprd_get_pps_current_max(struct sprd_fchg_info *info, u32 *max_cur)
{
	return 0;
}

static int sprd_pd_fixed_adjust_voltage(struct sprd_fchg_info *info, u32 input_vol)
{
	return 0;
}

static int sprd_pd_update_source_capabilities(struct sprd_fchg_info *info,
					      const u32 *pdo,
					      unsigned int nr_pdo)
{
	return 0;
}

static int sprd_pps_adjust_voltage(struct sprd_fchg_info *info, u32 input_vol)
{
	return 0;
}

static int sprd_pps_adjust_current(struct sprd_fchg_info *info, u32 input_current)
{
	return 0;
}

static int sprd_enable_pps(struct sprd_fchg_info *info, bool enable)
{
	return 0;
}

static void sprd_detect_pd_online_work(struct work_struct *data)
{

}
#endif

static int sprd_get_customized_fchg_current_max(struct sprd_fchg_info *info, u32 *max_cur)
{
	union power_supply_propval val;
	struct power_supply *psy_customized_fchg;
	int ret;

	if (!info->customized_fchg_psy)
		return 0;

	psy_customized_fchg = power_supply_get_by_name(info->customized_fchg_psy);
	if (!psy_customized_fchg) {
		dev_err(info->dev, "%s, psy_customized_fchg is NULL\n", __func__);
		return -ENODEV;
	}

	ret = power_supply_get_property(psy_customized_fchg, POWER_SUPPLY_PROP_CURRENT_MAX, &val);
	if (ret) {
		dev_err(info->dev, "%s, failed to get current property, ret=%d\n", __func__, ret);
		return ret;
	}

	*max_cur = val.intval;
	return ret;
}

static int sprd_get_customized_fchg_voltage_max(struct sprd_fchg_info *info, u32 *max_vol)
{
	union power_supply_propval val;
	struct power_supply *psy_customized_fchg;
	int ret;

	if (!info->customized_fchg_psy)
		return 0;

	psy_customized_fchg = power_supply_get_by_name(info->customized_fchg_psy);
	if (!psy_customized_fchg) {
		dev_err(info->dev, "%s, psy_customized_fchg is NULL\n", __func__);
		return -ENODEV;
	}

	ret = power_supply_get_property(psy_customized_fchg, POWER_SUPPLY_PROP_VOLTAGE_MAX, &val);
	if (ret) {
		dev_err(info->dev, "%s, failed to get vol property, ret=%d\n", __func__, ret);
		return ret;
	}

	*max_vol = val.intval;
	return ret;
}

static int sprd_customized_fchg_adjust_voltage(struct sprd_fchg_info *info, u32 input_vol)
{
	union power_supply_propval val;
	struct power_supply *psy_customized_fchg;
	int ret;

	if (!info->customized_fchg_psy)
		return 0;

	psy_customized_fchg = power_supply_get_by_name(info->customized_fchg_psy);
	if (!psy_customized_fchg) {
		dev_err(info->dev, "%s, psy_customized_fchg is NULL\n", __func__);
		return -ENODEV;
	}

	val.intval = input_vol;
	ret = power_supply_set_property(psy_customized_fchg, POWER_SUPPLY_PROP_VOLTAGE_MAX, &val);
	if (ret)
		dev_err(info->dev, "%s, failed to get vol property, ret=%d\n", __func__, ret);

	return ret;
}

static int sprd_enable_customized_fchg(struct sprd_fchg_info *info, bool enable)
{
	union power_supply_propval val;
	struct power_supply *psy_customized_fchg;
	static bool customized_fchg_active;
	int ret;

	if (!info->customized_fchg_psy)
		return 0;

	psy_customized_fchg = power_supply_get_by_name(info->customized_fchg_psy);
	if (!psy_customized_fchg) {
		dev_err(info->dev, "%s, psy_customized_fchg is NULL\n", __func__);
		return -ENODEV;
	}

	if (enable && !info->customized_fchg_enable) {
		val.intval = true;
		ret = power_supply_set_property(psy_customized_fchg, POWER_SUPPLY_PROP_ONLINE, &val);
		if (ret) {
			dev_err(info->dev, "%s, failed to enable customized fchg, ret=%d\n",
				__func__, ret);
			return ret;
		}

		customized_fchg_active = true;
	} else if (!enable && customized_fchg_active) {
		val.intval = false;
		ret = power_supply_set_property(psy_customized_fchg, POWER_SUPPLY_PROP_ONLINE, &val);
		if (ret) {
			dev_err(info->dev, "%s, failed to disable customized fchg, ret=%d\n",
				__func__, ret);
			return ret;
		}

		customized_fchg_active = false;
		info->customized_fchg_enable = false;
	}

	return 0;
}

static void sprd_fchg_work(struct work_struct *data)
{
	struct sprd_fchg_info *info =
		container_of(data, struct sprd_fchg_info, fchg_work);
	union power_supply_propval val;
	int ret = 0;
#ifdef CONFIG_VENDOR_SQC_CHARGER
	static bool pd_enable = 0, pps_enable = 0;
#endif

	if (!info->psy_fchg) {
		dev_err(info->dev, "%s, psy_fchg is NULL!!!\n", __func__);
		return;
	}

	mutex_lock(&info->lock);
	if (info->pd_extcon)
		ret = power_supply_get_property(info->psy_fchg, POWER_SUPPLY_PROP_USB_TYPE, &val);
	else if (info->sfcp_extcon)
		ret = power_supply_get_property(info->psy_fchg, POWER_SUPPLY_PROP_CHARGE_TYPE,
						&val);
	else if (info->customized_fchg_extcon)
		ret = power_supply_get_property(info->psy_fchg, POWER_SUPPLY_PROP_CHARGE_TYPE,
						&val);

	if (ret) {
		dev_err(info->dev, "%s, failed to get fchg type\n", __func__);
		goto out;
	}

	if (info->pd_extcon && (val.intval == POWER_SUPPLY_USB_TYPE_PD ||
	    (!info->support_pd_pps && val.intval == POWER_SUPPLY_USB_TYPE_PD_PPS))) {
		info->pd_enable = true;
		info->pps_enable = false;
		info->pps_active = false;
		info->sfcp_enable = false;
		info->customized_fchg_enable = false;
		info->fchg_type = POWER_SUPPLY_CHARGE_TYPE_FAST;
		mutex_unlock(&info->lock);
		cm_notify_event(info->psy, CM_EVENT_FAST_CHARGE, NULL);
		goto out1;
	} else if (info->pd_extcon && val.intval == POWER_SUPPLY_USB_TYPE_PD_PPS) {
		info->pps_enable = true;
		info->pd_enable = false;
		info->sfcp_enable = false;
		info->customized_fchg_enable = false;
		info->fchg_type = POWER_SUPPLY_CHARGE_TYPE_ADAPTIVE;
		mutex_unlock(&info->lock);
		cm_notify_event(info->psy, CM_EVENT_FAST_CHARGE, NULL);
		goto out1;
	} else if (info->sfcp_extcon && val.intval == POWER_SUPPLY_CHARGE_TYPE_FAST) {
		info->sfcp_enable = true;
		info->pps_enable = false;
		info->pps_active = false;
		info->pd_enable = false;
		info->customized_fchg_enable = false;
		info->fchg_type = POWER_SUPPLY_CHARGE_TYPE_FAST;
		mutex_unlock(&info->lock);
		cm_notify_event(info->psy, CM_EVENT_FAST_CHARGE, NULL);
		goto out1;
	} else if (info->customized_fchg_extcon && val.intval == POWER_SUPPLY_CHARGE_TYPE_FAST) {
		info->customized_fchg_enable = true;
		info->pps_enable = false;
		info->pps_active = false;
		info->pd_enable = false;
		info->sfcp_enable = false;
		info->fchg_type = POWER_SUPPLY_CHARGE_TYPE_FAST;
		mutex_unlock(&info->lock);
		cm_notify_event(info->psy, CM_EVENT_FAST_CHARGE, NULL);
		goto out1;
	} else if (info->pd_extcon && val.intval == POWER_SUPPLY_USB_TYPE_C) {
		if (info->pd_enable)
			sprd_pd_fixed_adjust_voltage(info, SPRD_FCHG_VOLTAGE_5V);

		info->pd_enable = false;
		info->pps_enable = false;
		info->pps_active = false;
		if (!info->sfcp_enable && !info->customized_fchg_enable)
			info->fchg_type = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
	}

out:
	mutex_unlock(&info->lock);

out1:
#ifdef CONFIG_VENDOR_SQC_CHARGER
	dev_info(info->dev, "[begin]pd type = %d, pd_enable=%d, pps_enable=%d, pps_active=%d\n",
			val.intval, info->pd_enable, info->pps_enable, info->pps_active);

	if (info->pps_enable != pps_enable) {
		if (info->pps_enable) {
			queue_delayed_work(info->delay_notify_queue,
											&info->delay_notify_sqc,
											msecs_to_jiffies(1500));
		}
	} else if (info->pd_enable != pd_enable) {
		if (info->pd_enable) {
			queue_delayed_work(info->delay_notify_queue,
											&info->delay_notify_sqc,
											msecs_to_jiffies(1500));
		}
	}

	if (!info->pd_enable && !info->pps_enable) {
		flush_delayed_work(&info->delay_notify_sqc);
		cancel_delayed_work_sync(&info->delay_notify_sqc);
		queue_delayed_work(info->delay_notify_queue,
										&info->delay_notify_sqc,
										msecs_to_jiffies(0));
	}

	pd_enable = info->pd_enable;
	pps_enable = info->pps_enable;

#endif
	dev_info(info->dev, "%s, pd_extcon = %d, sfcp_extcon = %d, customized_extcon = %d, "
		 "fchg type = %d\n",
		 __func__, info->pd_extcon, info->sfcp_extcon, info->customized_fchg_extcon,
		 info->fchg_type);
}

static int sprd_fchg_change(struct notifier_block *nb,
			    unsigned long event, void *data)
{
	struct sprd_fchg_info *info =
		container_of(nb, struct sprd_fchg_info, fchg_notify);
	struct power_supply *psy = data;

	if (strcmp(psy->desc->name, SPRD_FCHG_TCPM_PD_NAME) == 0 &&
	    event == PSY_EVENT_PROP_CHANGED) {
		dev_info(info->dev, "%s, pps or pd extcon\n", __func__);
		info->pd_extcon = true;
		info->sfcp_extcon = false;
		info->customized_fchg_extcon = false;
		info->psy_fchg = data;
		schedule_work(&info->fchg_work);
		goto out;
	}

	if (strcmp(psy->desc->name, SPRD_FCHG_SFCP_NAME) == 0 &&
	    event == PSY_EVENT_PROP_CHANGED) {
		dev_info(info->dev, "%s, sfcp extcon\n", __func__);
		info->sfcp_extcon = true;
		info->pd_extcon = false;
		info->customized_fchg_extcon = false;
		info->psy_fchg = data;
		schedule_work(&info->fchg_work);
		goto out;
	}

	if (info->customized_fchg_psy &&
	    strcmp(psy->desc->name, info->customized_fchg_psy) == 0 &&
	    event == PSY_EVENT_PROP_CHANGED) {
		dev_info(info->dev, "%s, customized fchg extcon\n", __func__);
		info->customized_fchg_extcon = true;
		info->sfcp_extcon = false;
		info->pd_extcon = false;
		info->psy_fchg = data;
		schedule_work(&info->fchg_work);
		goto out;
	}

out:
	return NOTIFY_OK;
}

static void sprd_fixed_fchg_handshake_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct sprd_fchg_info *info = container_of(dwork,
						   struct sprd_fchg_info,
						   fixed_fchg_handshake_work);
	int ret;

	pr_err("%s[%d]: chg_online=%d, detected=%d, pd_enable=%d, pps_enable=%d!!!\n", __func__, __LINE__,
		info->chg_online, info->detected, info->pd_enable, info->pps_enable);

	mutex_lock(&info->lock);
	if (!info->chg_online) {
		if (!info->pps_enable && !info->pd_enable)
			info->fchg_type = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;

		info->detected = false;
		ret = sprd_enable_sfcp(info, false);
		if (ret)
			dev_err(info->dev, "%s, failed to disable sfcp, ret=%d\n",
				__func__, ret);

		ret = sprd_enable_customized_fchg(info, false);
		if (ret)
			dev_err(info->dev, "%s, failed to disable customized fchg, ret=%d\n",
				__func__, ret);
	} else if (!info->detected) {
		info->detected = true;
		if (info->pd_enable || info->pps_enable) {
			ret = sprd_enable_sfcp(info, false);
			if (ret)
				dev_err(info->dev, "%s, failed to disable sfcp, ret=%d\n",
					__func__, ret);

			ret = sprd_enable_customized_fchg(info, false);
			if (ret)
				dev_err(info->dev, "%s, failed to disable customized fchg, ret=%d\n",
					__func__, ret);
		} else {
			ret = sprd_enable_sfcp(info, true);
			if (ret)
				dev_err(info->dev, "%s, failed to enable sfcp, ret=%d\n",
					__func__, ret);

			ret = sprd_enable_customized_fchg(info, true);
			if (ret)
				dev_err(info->dev, "%s, failed to enable customized fchg, ret=%d\n",
					__func__, ret);
		}
	}

	mutex_unlock(&info->lock);
}

static void sprd_enable_fixed_fchg(struct sprd_fchg_info *info, bool enable)
{
	if (!info) {
		pr_err("%s[%d]: info is NULL pointer!!!\n", __func__, __LINE__);
		return;
	}

	if (!info->support_fchg) {
		pr_err("%s[%d]: support_fchg is disable!!!\n", __func__);
		return;
	}

	pr_err("%s[%d]: handshake enable %d!!!\n", __func__, __LINE__, enable);

	info->chg_online = enable;
	if (!info->chg_online) {
		cancel_delayed_work(&info->fixed_fchg_handshake_work);
		schedule_delayed_work(&info->fixed_fchg_handshake_work, 0);
	} else {
		/*
		 * There is a confilt between charger detection and fast charger
		 * detection, and BC1.2 detection time consumption is <300ms,
		 * so we delay fast charger detection to avoid this issue.
		 */
		schedule_delayed_work(&info->fixed_fchg_handshake_work,
				      SPRD_FIXED_FCHG_DETECT_MS);
	}
}

static int sprd_enable_dynamic_fchg(struct sprd_fchg_info *info, bool enable)
{
	int ret = 0;

	if (!info) {
		pr_err("%s[%d]: info is NULL pointer!!!\n", __func__, __LINE__);
		return -ENOMEM;
	}

	if (!info->support_fchg)
		return -ENOMEM;

	mutex_lock(&info->lock);
	if (info->pps_enable) {
		ret = sprd_enable_pps(info, enable);
		if (ret)
			dev_err(info->dev, "%s, failed to %s pps, ret = %d\n",
				__func__, enable ? "enable" : "disable", ret);
	}

	mutex_unlock(&info->lock);
	return ret;
}

static int sprd_adjust_fchg_voltage(struct sprd_fchg_info *info, u32 input_vol)
{
	int ret = 0;

	if (!info) {
		pr_err("%s[%d]: info is NULL pointer!!!\n", __func__, __LINE__);
		return -ENOMEM;
	}

	if (!info->support_fchg)
		return -ENOMEM;

	mutex_lock(&info->lock);
	if (info->pps_enable) {
		ret = sprd_pps_adjust_voltage(info, input_vol);
		if (ret)
			dev_err(info->dev, "%s, failed to adjust pps voltage, ret=%d\n",
				__func__, ret);
	} else if (info->pd_enable) {
		if (sprd_enable_pps(info, false))
			dev_err(info->dev, "%s, failed to disable pps\n", __func__);

		ret = sprd_pd_fixed_adjust_voltage(info, input_vol);
		if (ret)
			dev_err(info->dev, "%s, failed to adjust pd fixed voltage, ret=%d\n",
				__func__, ret);
	} else if (info->sfcp_enable) {
		ret = sprd_sfcp_fixed_adjust_voltage(info, input_vol);
		if (ret)
			dev_err(info->dev, "%s, failed to adjust sfcp fixed voltage, ret=%d\n",
				__func__, ret);
	} else if (info->customized_fchg_enable) {
		ret = sprd_customized_fchg_adjust_voltage(info, input_vol);
		if (ret)
			dev_err(info->dev, "%s, failed to adjust customized fchg voltage, ret=%d\n",
				__func__, ret);
	}

	mutex_unlock(&info->lock);
	return ret;
}

static int sprd_adjust_fchg_current(struct sprd_fchg_info *info, u32 input_current)
{
	int ret = 0;

	if (!info) {
		pr_err("%s[%d]: info is NULL pointer!!!\n", __func__, __LINE__);
		return -ENOMEM;
	}

	if (!info->support_fchg)
		return -ENOMEM;

	mutex_lock(&info->lock);
	if (info->pps_enable) {
		ret = sprd_pps_adjust_current(info, input_current);
		if (ret)
			dev_err(info->dev, "%s, failed to adjust pps current, ret=%d\n",
				__func__, ret);
	}

	mutex_unlock(&info->lock);
	return ret;
}

static int sprd_update_source_capabilities(struct sprd_fchg_info *info, const u32 *pdo,
				       unsigned int nr_pdo)
{
	int ret = 0;

	if (!info) {
		pr_err("%s[%d]: info is NULL pointer!!!\n", __func__, __LINE__);
		return -ENOMEM;
	}

	if (!info->support_fchg)
		return -EINVAL;

	mutex_lock(&info->lock);
	dev_info(info->dev, "%s:%d, pd_extcon = %d\n", __func__, __LINE__, info->pd_extcon);
	ret = sprd_pd_update_source_capabilities(info, pdo, nr_pdo);
	if (ret)
		dev_err(info->dev, "%s, failed to update src cap, ret=%d\n", __func__, ret);
	mutex_unlock(&info->lock);

	return ret;
}

static int sprd_get_fchg_type(struct sprd_fchg_info *info, u32 *type)
{
	if (!info) {
		pr_err("%s[%d]: info is NULL pointer!!!\n", __func__, __LINE__);
		return -ENOMEM;
	}

	if (!info->support_fchg)
		return -ENOMEM;

	*type = info->fchg_type;

	return 0;
}

static int sprd_get_fchg_voltage_max(struct sprd_fchg_info *info, int *voltage_max)
{
	int ret = 0;

	*voltage_max = 0;
	if (!info) {
		pr_err("%s[%d]: info is NULL pointer!!!\n", __func__, __LINE__);
		return -ENOMEM;
	}

	if (!info->support_fchg)
		return -ENOMEM;

	mutex_lock(&info->lock);
	if (info->pps_enable && info->pps_active) {
		ret = sprd_get_pps_voltage_max(info, voltage_max);
		if (ret)
			dev_err(info->dev, "%s, failed to get pps vol max, ret=%d\n",
				__func__, ret);
	} else if (info->pps_enable || info->pd_enable) {
		ret = sprd_get_pd_adapter_voltage_max(info, voltage_max);
		if (ret)
			dev_err(info->dev, "%s, failed to get pd adapter vol max, ret=%d\n",
				__func__, ret);
	} else if (info->sfcp_enable) {
		ret = sprd_get_sfcp_fixed_voltage_max(info, voltage_max);
		if (ret)
			dev_err(info->dev, "%s, failed to get sfcp fixed vol max, ret=%d\n",
				__func__, ret);
	} else if (info->customized_fchg_enable) {
		ret = sprd_get_customized_fchg_voltage_max(info, voltage_max);
		if (ret)
			dev_err(info->dev, "%s, failed to get customized fchg vol max, ret=%d\n",
				__func__, ret);
	}

	mutex_unlock(&info->lock);
	return ret;
}

static int sprd_get_fchg_current_max(struct sprd_fchg_info *info, int input_vol, int *current_max)
{
	int ret = 0;

	*current_max = 0;
	if (!info) {
		pr_err("%s[%d]: info is NULL pointer!!!\n", __func__, __LINE__);
		return -ENOMEM;
	}

	if (!info->support_fchg)
		return -ENOMEM;

	mutex_lock(&info->lock);
	if (info->pps_enable && !input_vol) {
		ret = sprd_get_pps_current_max(info, current_max);
		if (ret)
			dev_err(info->dev, "%s, failed to get pps current max, ret=%d\n",
				__func__, ret);
	} else if (info->pps_enable || info->pd_enable) {
		ret = sprd_get_pd_adapter_current(info, input_vol, current_max);
		if (ret)
			dev_err(info->dev, "%s, failed to get pd adapter current, ret=%d\n",
				__func__, ret);
	} else if (info->sfcp_enable) {
		ret = sprd_get_sfcp_fixed_current_max(info, current_max);
		if (ret)
			dev_err(info->dev, "%s, failed to get sfcp fixed current max, ret=%d\n",
				__func__, ret);
	} else if (info->customized_fchg_enable) {
		ret = sprd_get_customized_fchg_current_max(info, current_max);
		if (ret)
			dev_err(info->dev, "%s,failed to get customized fchg current max, ret=%d\n",
				__func__, ret);
	}

	mutex_unlock(&info->lock);
	return ret;
}


#ifdef CONFIG_VENDOR_SQC_CHARGER
static int sqc_shutdown = 0;
extern struct sqc_pd_proto_ops sqc_pd_ops_node;

static int pd_status_init(void)
{
	struct sprd_fchg_info *info =
		(struct sprd_fchg_info *)sqc_pd_ops_node.arg;

	if (!info) {
		pr_err("[SQC-HW]: info is null\n", __func__);
		return SQC_ADAPTER_ERROR;
	}


	return SQC_ADAPTER_OK;
}

static int pd_status_end(void)
{
	struct sprd_fchg_info *info =
		(struct sprd_fchg_info *)sqc_pd_ops_node.arg;

	if (!info) {
		pr_err("[SQC-HW]: info is null\n", __func__);
		return SQC_ADAPTER_ERROR;
	}

	pr_err("[SQC-HW]: %s\n", __func__);

	if (sprd_enable_pps(info, false)) {
		dev_err(info->dev, "failed to disable pps\n");
		return SQC_ADAPTER_ERROR;
	}

	return SQC_ADAPTER_OK;
}

static int pd_get_charger_type(int *type)
{
	struct sprd_fchg_info *info =
		(struct sprd_fchg_info *)sqc_pd_ops_node.arg;

	if (!info) {
		pr_err("[SQC-HW]: [%s] info is null\n", __func__);
		*type = SQC_NONE_TYPE;
		return SQC_NONE_TYPE;
	}

	if (info->pps_enable) {
		*type = SQC_PD3D0_APDO_TYPE;
	} else if (info->pd_enable) {
		*type = SQC_PD3D0_BASE_TYPE;
	} else {
		*type = SQC_NONE_TYPE;
	}

	pr_info("[SQC-HW]: [%s] type: %d\n", __func__, *type);

	return *type;
}

static int pd_get_cable_type(int *type)
{
	struct sprd_fchg_info *info =
		(struct sprd_fchg_info *)sqc_pd_ops_node.arg;

	if (!info) {
		pr_err("[SQC-HW]: [%s] info is null\n", __func__);
		return SQC_NONE_TYPE;
	}

	*type = SQC_CABLE_TYPE_NONE;

	return 0;
}

static int pd_set_sqc_cap(int mV, int mA)
{
	struct sprd_fchg_info *info =
		(struct sprd_fchg_info *)sqc_pd_ops_node.arg;
	int ret = 0;

	if (!info || sqc_shutdown) {
		pr_err("[SQC-HW]: info is null|sqc_shutdown\n", __func__);
		return SQC_ADAPTER_ERROR;
	}

	pr_info("[SQC-HW]: [%s] mV %d, mA %d\n", __func__, mV, mA);

	if (info->pps_enable) {
		ret = sprd_pps_adjust_current(info, mA * 1000);
		if (ret)
			dev_err(info->dev, "[SQC-HW]%s sprd_pps_adjust_current failed, ret:%d\n",
				__func__, ret);

		ret = sprd_pps_adjust_voltage(info, mV * 1000);
		if (ret)
			dev_err(info->dev, "[SQC-HW]%s sprd_pps_adjust_voltage failed, ret:%d\n",
				__func__, ret);
	} else if (info->pd_enable) {
		ret = sprd_enable_pps(info, false);
		if (ret)
			dev_err(info->dev, "[SQC-HW]%s sprd_enable_pps failed disable, ret:%d\n",
				__func__, ret);

		ret = sprd_pd_fixed_adjust_voltage(info, mV * 1000);
		if (ret)
			dev_err(info->dev, "[SQC-HW]%s sprd_pd_fixed_adjust_voltage failed, ret:%d\n",
				__func__, ret);
	}

	/* According to sprd's suggestions, ignore the error requests.
	Let the PD protocol to control the state machine. */
	if (ret == -ETIMEDOUT)
		return SQC_ADAPTER_TIMEOUT;
	else if (ret < 0)
		return SQC_ADAPTER_ERROR;

	return SQC_ADAPTER_OK;
}

static int pd_get_sqc_cap(struct sqc_pd_adapter_cap *pdos)
{
	struct sprd_fchg_info *info =
		(struct sprd_fchg_info *)sqc_pd_ops_node.arg;
	struct power_supply *psy_tcpm = NULL;
	struct sprd_tcpm_port *port = NULL;
	unsigned char i = 0, j = 0;

	psy_tcpm = power_supply_get_by_name(SPRD_FCHG_TCPM_PD_NAME);
	if (!psy_tcpm) {
		dev_err(info->dev, "%s, psy_tcpm is NULL\n", __func__);
		return -ENODEV;
	}

	port = power_supply_get_drvdata(psy_tcpm);
	if (!port) {
		dev_err(info->dev, "%s, failed to get tcpm port\n", __func__);
		return -EINVAL;
	}

	sprd_tcpm_get_source_capabilities(port, &info->pd_source_cap);

	power_supply_put(psy_tcpm);

	if (!info->pd_source_cap.nr_source_caps) {
		dev_err(info->dev, "%s, failed to obtain the PD power supply capacity\n", __func__);
		return 0;
	}

	for (i = 0; i < info->pd_source_cap.nr_source_caps; i++) {
		if ((info->pd_source_cap.type[i] == SPRD_PDO_TYPE_FIXED)
			|| (info->pd_source_cap.type[i] == SPRD_PDO_TYPE_APDO)) {
			pdos->type[j] = (info->pd_source_cap.type[i] == SPRD_PDO_TYPE_FIXED) ?
					SQC_PDOS_TYPE_FIXED : SQC_PDOS_TYPE_APDO;
			pdos->max_mv[j] = info->pd_source_cap.max_mv[i];
			pdos->min_mv[j] = info->pd_source_cap.min_mv[i];
			pdos->ma[j] = info->pd_source_cap.ma[i];
			pdos->pwr_mw_limit[j] = info->pd_source_cap.pwr_mw_limit[i];
			pdos->nr_source_caps++;
			j++;
		}
	}

	return 0;
}

struct sqc_pd_proto_ops sqc_pd_ops_node = {
	.status_init = pd_status_init,
	.status_remove = pd_status_end,
	.get_charger_type = pd_get_charger_type,
	.get_cable_type = pd_get_cable_type,
	.set_apdo_cap = pd_set_sqc_cap,
	.get_apdo_cap = pd_get_sqc_cap,
};

static void sqc_delay_notify_daemon(struct work_struct *data)
{
	struct delayed_work *dwork = to_delayed_work(data);
	struct sprd_fchg_info *info =
		container_of(dwork, struct sprd_fchg_info, delay_notify_sqc);

	if (info->pd_enable || info->pps_enable) {
		pr_err("[SQC-HW]: [%s] notify 1\n", __func__);
		sqc_notify_daemon_changed(SQC_NOTIFY_USBPD,
					SQC_NOTIFY_USB_STATUS_CHANGED, 1);
	} else {
		pr_err("[SQC-HW]: [%s] notify 0\n", __func__);
		sqc_notify_daemon_changed(SQC_NOTIFY_USBPD,
					SQC_NOTIFY_USB_STATUS_CHANGED, 0);
	}

}

#endif

static int sprd_fchg_extcon_init(struct sprd_fchg_info *info,
				 struct power_supply *psy)
{
	int ret;

	pr_err("%s[%d]\n", __func__, __LINE__);

	if (!info) {
		pr_err("%s[%d], info is NULL pointer!!!\n", __func__, __LINE__);
		return -ENOMEM;
	}

	if (!psy) {
		pr_err("%s[%d], psy is NULL pointer!!!\n", __func__, __LINE__);
		return -ENOMEM;
	}

	info->psy = psy;
	info->fchg_type = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
	info->support_fchg = device_property_read_bool(info->dev, "sprd,support-fchg");
	if (!info->support_fchg) {
		pr_err("%s[%d], get sprd,support-fchg failed!!!\n", __func__, __LINE__);
		return 0;
	}

	ret = device_property_read_u32(info->dev,
				       "sprd,pd-fixed-max-microwatt",
				       &info->pd_fixed_max_uw);
	if (ret) {
		dev_info(info->dev, "%s, failed to get pd fixed max uw\n", __func__);
		/* If this parameter is not defined in DTS, the default power is 10W */
		info->pd_fixed_max_uw = SPRD_PD_DEFAULT_POWER_UW;
	}

	info->support_pd_pps = device_property_read_bool(info->dev, "sprd,support-pd-pps");

	ret = of_property_read_string(info->dev->of_node,
				      "sprd,customized-fchg-psy",
				      &info->customized_fchg_psy);
	if (!ret)
		dev_info(info->dev, "%s, support customized fchg psy: %s\n",
			__func__, info->customized_fchg_psy);

	mutex_init(&info->lock);
	INIT_DELAYED_WORK(&info->fixed_fchg_handshake_work, sprd_fixed_fchg_handshake_work);
	INIT_WORK(&info->pd_online_work, sprd_detect_pd_online_work);
	INIT_WORK(&info->fchg_work, sprd_fchg_work);

#ifdef CONFIG_VENDOR_SQC_CHARGER
	info->delay_notify_queue = create_singlethread_workqueue("sqc_notify_queue");
	INIT_DELAYED_WORK(&info->delay_notify_sqc, sqc_delay_notify_daemon);
#endif

	info->fchg_notify.notifier_call = sprd_fchg_change;
	ret = power_supply_reg_notifier(&info->fchg_notify);
	if (ret)
		dev_err(info->dev, "%s, failed to register fchg notifier, ret = %d\n",
			__func__, ret);


#ifdef CONFIG_VENDOR_SQC_CHARGER
	sqc_pd_ops_node.arg = (void *)info;

	sqc_hal_pd_register(&sqc_pd_ops_node);
#endif

	pr_err("%s[%d]\n", __func__, __LINE__);

	return ret;
}

static void sprd_fchg_detect(struct sprd_fchg_info *info)
{
	if (!info) {
		pr_err("%s[%d], info is NULL pointer!!!\n", __func__, __LINE__);
		return;
	}

	pr_info("%s support_fchg=%d, pd_enable=%d, pps_enable=%d\n",
		__func__, info->support_fchg, info->pd_enable, info->pps_enable);

	if (!info->support_fchg)
		return;

	if (!info->pd_enable && !info->pps_enable)
		schedule_work(&info->pd_online_work);
}

static void sprd_fchg_suspend(struct sprd_fchg_info *info)
{
	if (!info) {
		pr_err("%s[%d], info is NULL pointer!!!\n", __func__, __LINE__);
		return;
	}

	pr_err("%s[%d]\n", __func__, __LINE__);

	if (!info->support_fchg)
		return;
}

static void sprd_fchg_resume(struct sprd_fchg_info *info)
{
	if (!info) {
		pr_err("%s[%d], info is NULL pointer!!!\n", __func__, __LINE__);
		return;
	}

	pr_err("%s[%d]\n", __func__, __LINE__);

	if (!info->support_fchg)
		return;
}

static void sprd_fchg_remove(struct sprd_fchg_info *info)
{
	if (!info) {
		pr_err("%s[%d], info is NULL pointer!!!\n", __func__, __LINE__);
		return;
	}

	pr_err("%s[%d]\n", __func__, __LINE__);

	if (!info->support_fchg)
		return;
}

static void sprd_fchg_shutdown(struct sprd_fchg_info *info)
{
	if (!info) {
		pr_err("%s[%d], info is NULL pointer!!!\n", __func__, __LINE__);
		return;
	}

	pr_err("%s[%d]\n", __func__, __LINE__);

	if (!info->support_fchg)
		return;
}

struct sprd_fchg_info *sprd_fchg_info_register(struct device *dev)
{
	struct sprd_fchg_info *info = NULL;

	pr_err("%s[%d]\n", __func__, __LINE__);

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info) {
		pr_err("%s[%d]: info is NULL pointer!!!\n", __func__, __LINE__);
		return ERR_PTR(-ENOMEM);
	}

	info->ops = devm_kzalloc(dev, sizeof(*info->ops), GFP_KERNEL);
	if (!info->ops) {
		pr_err("%s[%d]: ops is NULL pointer!!!\n", __func__, __LINE__);
		return ERR_PTR(-ENOMEM);
	}

	info->dev = dev;

	info->ops->extcon_init = sprd_fchg_extcon_init;
	info->ops->fchg_detect = sprd_fchg_detect;
	info->ops->get_fchg_type = sprd_get_fchg_type;
	info->ops->get_fchg_vol_max = sprd_get_fchg_voltage_max;
	info->ops->get_fchg_cur_max = sprd_get_fchg_current_max;
	info->ops->enable_fixed_fchg = sprd_enable_fixed_fchg;
	info->ops->enable_dynamic_fchg = sprd_enable_dynamic_fchg;
	info->ops->adj_fchg_vol = sprd_adjust_fchg_voltage;
	info->ops->adj_fchg_cur = sprd_adjust_fchg_current;
	info->ops->update_src_cap = sprd_update_source_capabilities;
	info->ops->suspend = sprd_fchg_suspend;
	info->ops->resume = sprd_fchg_resume;
	info->ops->remove = sprd_fchg_remove;
	info->ops->shutdown = sprd_fchg_shutdown;

	pr_err("%s[%d]\n", __func__, __LINE__);

	return info;
}
EXPORT_SYMBOL_GPL(sprd_fchg_info_register);

