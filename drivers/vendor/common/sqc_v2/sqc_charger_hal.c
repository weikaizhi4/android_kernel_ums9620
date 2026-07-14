/* Copyright (c) 2020-2025, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#define pr_fmt(fmt)	"SQC_CHARER_HAL: %s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <vendor/common/sqc_common.h>

#define SQC_CHG_DEVNAME "sqc_chg_node"

struct chg_dev_data {
	int ref_count;
	struct cdev main_dev;
	struct class *device_class;
	struct mutex file_mutex;
	struct sqc_pmic_chg_ops *chg_ops[SQC_MAX_CHARGER_NUM];
};

static struct chg_dev_data chg_hal_data;

static int sqc_hal_init_pmic_charger(int chg_type)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->init_pmic_charger != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->init_pmic_charger(pmic_ops->arg);
		}
	}

	return retval;
}

static int sqc_hal_charging_enable_set(int chg_type, bool enable)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->chg_enable != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->chg_enable(pmic_ops->arg, enable);
		}
	}

	return retval;
}

static int sqc_hal_charging_enable_get(int chg_type, unsigned int *enabled)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->chg_enable_get != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->chg_enable_get(pmic_ops->arg, enabled);
		}
	}

	return retval;
}

static int sqc_hal_set_chging_fcv(int chg_type, unsigned int mV)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->set_chging_fcv != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->set_chging_fcv(pmic_ops->arg, mV);
		}
	}

	return retval;
}

static int sqc_hal_get_chging_fcv(int chg_type, unsigned int *mV)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->get_chging_fcv != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->get_chging_fcv(pmic_ops->arg, mV);
		}
	}

	return retval;
}

static int sqc_hal_set_chging_fcc(int chg_type, unsigned int mA)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->set_chging_fcc != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->set_chging_fcc(pmic_ops->arg, mA);
		}
	}

	return retval;
}

static int sqc_hal_get_chging_fcc(int chg_type, unsigned int *mA)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->get_chging_fcc != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->get_chging_fcc(pmic_ops->arg, mA);
		}
	}

	return retval;
}

static int sqc_hal_set_chging_icl(int chg_type, unsigned int mA)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->set_chging_icl != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->set_chging_icl(pmic_ops->arg, mA);
		}
	}

	return retval;
}

static int sqc_hal_get_chging_icl(int chg_type, unsigned int *mA)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->get_chging_icl != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->get_chging_icl(pmic_ops->arg, mA);
		}
	}

	return retval;
}

static int sqc_hal_set_chging_topoff(int chg_type, unsigned int mA)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->set_chging_topoff != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->set_chging_topoff(pmic_ops->arg, mA);
		}
	}

	return retval;
}

static int sqc_hal_get_chging_topoff(int chg_type, unsigned int *mA)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->get_chging_topoff != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->get_chging_topoff(pmic_ops->arg, mA);
		}
	}

	return retval;
}

static int sqc_hal_set_rechg_soc(int chg_type, unsigned int rechg_soc)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->set_rechg_soc != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->set_rechg_soc(pmic_ops->arg, rechg_soc);
		}
	}

	return retval;
}

static int sqc_hal_get_rechg_soc(int chg_type, unsigned int *rechg_soc)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->get_rechg_soc != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->get_rechg_soc(pmic_ops->arg, rechg_soc);
		}
	}

	return retval;
}

static int sqc_hal_set_rechg_volt(int chg_type, unsigned int rechg_volt_mv)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->set_rechg_volt != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->set_rechg_volt(pmic_ops->arg, rechg_volt_mv);
		}
	}

	return retval;
}

static int sqc_hal_get_rechg_volt(int chg_type, unsigned int *rechg_volt_mv)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->get_rechg_volt != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->get_rechg_volt(pmic_ops->arg, rechg_volt_mv);
		}
	}

	return retval;
}

static int sqc_hal_get_charing_status(int chg_type, unsigned int *chg_status)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->get_chg_status != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->get_chg_status(pmic_ops->arg, chg_status);
		}
	}

	return retval;
}

static int sqc_hal_get_interrupt_status(int chg_type, unsigned int *int_status)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->get_int_status != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->get_int_status(pmic_ops->arg, int_status);
		}
	}

	return retval;
}

static int sqc_hal_charging_role_set(int chg_type, unsigned int master)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->chg_role_set != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->chg_role_set(pmic_ops->arg, master);
		}
	}

	return retval;
}

static int sqc_hal_charging_role_get(int chg_type, unsigned int *master)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->chg_role_get != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->chg_role_get(pmic_ops->arg, master);
		}
	}

	return retval;
}

static int sqc_hal_batt_ovp_voltage_set(int chg_type, unsigned int mV)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->batt_ovp_volt_set != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->batt_ovp_volt_set(pmic_ops->arg, mV);
		}
	}

	return retval;
}

static int sqc_hal_batt_ovp_voltage_get(int chg_type, unsigned int *mV)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->batt_ovp_volt_get != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->batt_ovp_volt_get(pmic_ops->arg, mV);
		}
	}

	return retval;
}

static int sqc_hal_batt_ovp_alarm_voltage_set(int chg_type, unsigned int mV)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->batt_ovp_alm_volt_set != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->batt_ovp_alm_volt_set(pmic_ops->arg, mV);
		}
	}

	return retval;
}

static int sqc_hal_batt_ovp_alarm_voltage_get(int chg_type, unsigned int *mV)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->batt_ovp_alm_volt_get != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->batt_ovp_alm_volt_get(pmic_ops->arg, mV);
		}
	}

	return retval;
}

static int sqc_hal_batt_ocp_current_set(int chg_type, unsigned int mA)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->batt_ocp_curr_set != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->batt_ocp_curr_set(pmic_ops->arg, mA);
		}
	}

	return retval;
}

static int sqc_hal_batt_ocp_current_get(int chg_type, unsigned int *mA)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->batt_ocp_curr_get != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->batt_ocp_curr_get(pmic_ops->arg, mA);
		}
	}

	return retval;
}

static int sqc_hal_batt_ocp_alarm_current_set(int chg_type, unsigned int mA)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->batt_ocp_alm_curr_set != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->batt_ocp_alm_curr_set(pmic_ops->arg, mA);
		}
	}

	return retval;
}

static int sqc_hal_batt_ocp_alarm_current_get(int chg_type, unsigned int *mA)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->batt_ocp_alm_curr_get != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->batt_ocp_alm_curr_get(pmic_ops->arg, mA);
		}
	}

	return retval;
}

static int sqc_hal_batt_ucp_alarm_current_set(int chg_type, unsigned int mA)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->batt_ucp_alm_curr_set != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->batt_ucp_alm_curr_set(pmic_ops->arg, mA);
		}
	}

	return retval;
}

static int sqc_hal_batt_ucp_alarm_current_get(int chg_type, unsigned int *mA)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->batt_ucp_alm_curr_get != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->batt_ucp_alm_curr_get(pmic_ops->arg, mA);
		}
	}

	return retval;
}

static int sqc_hal_ac_ovp_voltage_set(int chg_type, unsigned int mV)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->ac_ovp_volt_set != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->ac_ovp_volt_set(pmic_ops->arg, mV);
		}
	}

	return retval;
}

static int sqc_hal_ac_ovp_voltage_get(int chg_type, unsigned int *mV)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->ac_ovp_volt_get != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->ac_ovp_volt_get(pmic_ops->arg, mV);
		}
	}

	return retval;
}

static int sqc_hal_usb_ovp_voltage_set(int chg_type, unsigned int mV)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->usb_ovp_volt_set != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->usb_ovp_volt_set(pmic_ops->arg, mV);
		}
	}

	return retval;
}

static int sqc_hal_usb_ovp_voltage_get(int chg_type, unsigned int *mV)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->usb_ovp_volt_get != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->usb_ovp_volt_get(pmic_ops->arg, mV);
		}
	}

	return retval;
}

static int sqc_hal_usb_ovp_alarm_voltage_set(int chg_type, unsigned int mV)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->usb_ovp_alm_volt_set != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->usb_ovp_alm_volt_set(pmic_ops->arg, mV);
		}
	}

	return retval;
}

static int sqc_hal_usb_ovp_alarm_voltage_get(int chg_type, unsigned int *mV)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->usb_ovp_alm_volt_get != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->usb_ovp_alm_volt_get(pmic_ops->arg, mV);
		}
	}

	return retval;
}

static int sqc_hal_usb_ocp_current_set(int chg_type, unsigned int mA)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->usb_ocp_curr_set != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->usb_ocp_curr_set(pmic_ops->arg, mA);
		}
	}

	return retval;
}

static int sqc_hal_usb_ocp_current_get(int chg_type, unsigned int *mA)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->usb_ocp_curr_get != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->usb_ocp_curr_get(pmic_ops->arg, mA);
		}
	}

	return retval;
}

static int sqc_hal_usb_ocp_alarm_current_set(int chg_type, unsigned int mA)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->usb_ocp_alm_curr_set != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->usb_ocp_alm_curr_set(pmic_ops->arg, mA);
		}
	}

	return retval;
}

static int sqc_hal_usb_ocp_alarm_current_get(int chg_type, unsigned int *mA)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->usb_ocp_alm_curr_get != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->usb_ocp_alm_curr_get(pmic_ops->arg, mA);
		}
	}

	return retval;
}

static int sqc_hal_usb_ibus_get(int chg_type, unsigned int *mA)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->usb_ibus_get != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->usb_ibus_get(pmic_ops->arg, mA);
		}
	}

	return retval;
}

static int sqc_hal_usb_vbus_get(int chg_type, unsigned int *mV)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->usb_vbus_get != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->usb_vbus_get(pmic_ops->arg, mV);
		}
	}

	return retval;
}

static int sqc_hal_usb_ibat_get(int chg_type, unsigned int *mA)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->batt_ibat_get != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->batt_ibat_get(pmic_ops->arg, mA);
		}
	}

	return retval;
}

static int sqc_hal_usb_vbat_get(int chg_type, unsigned int *mV)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->batt_vbat_get != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->batt_vbat_get(pmic_ops->arg, mV);
		}
	}

	return retval;
}

static int sqc_hal_powerpath_set(int chg_type, int enabled)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->usb_ocp_curr_set != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->usb_ocp_curr_set(pmic_ops->arg, enabled);
		}
	}

	return retval;
}

static int sqc_hal_powerpath_get(int chg_type, int *enabled)
{
	struct sqc_pmic_chg_ops *pmic_ops = NULL;
	int i = 0, retval = 0;

	for (i = 0; i < SQC_MAX_CHARGER_NUM; i++) {
		pmic_ops = chg_hal_data.chg_ops[i];
		if ((chg_type & BIT(i)) &&
					(pmic_ops != NULL) &&
					(pmic_ops->usb_ocp_curr_get != NULL)) {
			pr_debug("chg_type: %d\n", i);
			retval |= pmic_ops->enable_path_get(pmic_ops->arg, enabled);
		}
	}

	return retval;
}

static loff_t sqc_chg_llseek(struct file *filp, loff_t off, int whence)
{
	loff_t newpos;

	mutex_lock(&(chg_hal_data.file_mutex));

	switch (whence) {
	case SEEK_SET:
		newpos = off;
		break;
	case SEEK_CUR:
		newpos = filp->f_pos + off;
		break;
	case SEEK_END:
		newpos = SQC_IFS_MAX + off;
		break;
	default:
		newpos = SQC_IFS_NONE;
	}

	filp->f_pos = newpos;

	mutex_unlock(&(chg_hal_data.file_mutex));

	return newpos;
}

/*
 * sqc_pd_read: read register data from RMI device
 *
 * @filp: pointer to file structure
 * @buf: pointer to user space buffer
 * @count: number of bytes to read
 * @f_pos: starting RMI register address
 */
static ssize_t sqc_chg_read(struct file *filp, char __user *buf,
			    size_t count, loff_t *f_pos)
{
	ssize_t retval = 0;
	unsigned int chg_type = 0, file_pos = 0, temp_val = 0;

	if ((*f_pos == SQC_IFS_NONE) || (count == 0)) {
		pr_err("%s f_pos Error!\n", __func__, *f_pos);
		return -ENXIO;
	}

	chg_type = (*f_pos) / SQC_IFS_DIVISION;
	file_pos = (*f_pos) % SQC_IFS_DIVISION;

	mutex_lock(&(chg_hal_data.file_mutex));

	switch (file_pos) {
	case SQC_IFS_PMIC_STATUS_GET:
		retval = sqc_hal_get_charing_status(chg_type, &temp_val);
		break;
	case SQC_IFS_PMIC_INT_GET:
		retval = sqc_hal_get_interrupt_status(chg_type, &temp_val);
		break;
	case SQC_IFS_PMIC_ENABLE:
		retval = sqc_hal_charging_enable_get(chg_type, &temp_val);
		break;
	case SQC_IFS_PMIC_FCV:
		retval = sqc_hal_get_chging_fcv(chg_type, &temp_val);
		break;
	case SQC_IFS_PMIC_FCC:
		retval = sqc_hal_get_chging_fcc(chg_type, &temp_val);
		break;
	case SQC_IFS_PMIC_ICL:
		retval = sqc_hal_get_chging_icl(chg_type, &temp_val);
		break;
	case SQC_IFS_PMIC_TOPOFF:
		retval = sqc_hal_get_chging_topoff(chg_type, &temp_val);
		break;
	case SQC_IFS_PMIC_RECHG_SOC:
		retval = sqc_hal_get_rechg_soc(chg_type, &temp_val);
		break;
	case SQC_IFS_PMIC_RECHG_VOLT:
		retval = sqc_hal_get_rechg_volt(chg_type, &temp_val);
		break;
	case SQC_IFS_PMIC_ROLE:
		retval = sqc_hal_charging_role_get(chg_type, &temp_val);
		break;
	case SQC_IFS_PMIC_BATT_OVP:
		retval = sqc_hal_batt_ovp_voltage_get(chg_type, &temp_val);
		break;
	case SQC_IFS_PMIC_BATT_OVP_ALM:
		retval = sqc_hal_batt_ovp_alarm_voltage_get(chg_type, &temp_val);
		break;
	case SQC_IFS_PMIC_BATT_OCP:
		retval = sqc_hal_batt_ocp_current_get(chg_type, &temp_val);
		break;
	case SQC_IFS_PMIC_BATT_OCP_ALM:
		retval = sqc_hal_batt_ocp_alarm_current_get(chg_type, &temp_val);
		break;
	case SQC_IFS_PMIC_BATT_UCP_ALM:
		retval = sqc_hal_batt_ucp_alarm_current_get(chg_type, &temp_val);
		break;
	case SQC_IFS_PMIC_AC_OVP:
		retval = sqc_hal_ac_ovp_voltage_get(chg_type, &temp_val);
		break;
	case SQC_IFS_PMIC_USB_OVP:
		retval = sqc_hal_usb_ovp_voltage_get(chg_type, &temp_val);
		break;
	case SQC_IFS_PMIC_USB_OVP_ALM:
		retval = sqc_hal_usb_ovp_alarm_voltage_get(chg_type, &temp_val);
		break;
	case SQC_IFS_PMIC_USB_OCP:
		retval = sqc_hal_usb_ocp_current_get(chg_type, &temp_val);
		break;
	case SQC_IFS_PMIC_USB_OCP_ALM:
		retval = sqc_hal_usb_ocp_alarm_current_get(chg_type, &temp_val);
		break;
	case SQC_IFS_VBUS_NOW:
		retval = sqc_hal_usb_vbus_get(chg_type, &temp_val);
		break;
	case SQC_IFS_IBUS_NOW:
		retval = sqc_hal_usb_ibus_get(chg_type, &temp_val);
		break;
	case SQC_IFS_VBAT_NOW:
		retval = sqc_hal_usb_vbat_get(chg_type, &temp_val);
		break;
	case SQC_IFS_IBAT_NOW:
		retval = sqc_hal_usb_ibat_get(chg_type, &temp_val);
		break;
	case SQC_IFS_POWERPATH_ENABLE:
		retval = sqc_hal_powerpath_get(chg_type, &temp_val);
		break;
	default:
		retval = -ENOMEM;
		pr_err("%s f_pos default!\n", __func__, *f_pos);
		break;
	}

	if (copy_to_user(buf, &temp_val, sizeof(temp_val))) {
		pr_err("%s copy_to_user failed!\n", __func__);
		retval = -EFAULT;
	}

	mutex_unlock(&(chg_hal_data.file_mutex));

	/*pr_info("%s chg_type 0x%02X, file_pos %d, temp_val %d.!\n", __func__, chg_type, file_pos, temp_val);*/

	return (retval < 0) ? retval : sizeof(temp_val);
}

/*
 * sqc_pd_write: write register data to RMI device
 *
 * @filp: pointer to file structure
 * @buf: pointer to user space buffer
 * @count: number of bytes to write
 * @f_pos: starting RMI register address
 */
static ssize_t sqc_chg_write(struct file *filp, const char __user *buf,
			     size_t count, loff_t *f_pos)
{
	ssize_t retval = 0;
	unsigned int *handle_buf = NULL;
	unsigned int chg_type = 0, file_pos = 0, temp_val = 0;

	if ((*f_pos == SQC_IFS_NONE) || (count == 0)) {
		pr_err("%s f_pos Error!\n", __func__, *f_pos);
		return -ENXIO;
	}

	chg_type = (*f_pos) / SQC_IFS_DIVISION;
	file_pos = (*f_pos) % SQC_IFS_DIVISION;

	handle_buf = kzalloc(count, GFP_KERNEL);
	if (!handle_buf) {
		pr_err("%s kzalloc Error!\n", __func__, *f_pos);
		return -ENOMEM;
	}

	if (copy_from_user(handle_buf, buf, count)) {
		pr_err("%s copy_from_user Error!\n", __func__, *f_pos);
		kfree(handle_buf);
		return -EFAULT;
	}

	mutex_lock(&(chg_hal_data.file_mutex));

	temp_val = handle_buf[0];

	/*pr_info("%s chg_type 0x%02X, file_pos %d, temp_val %d.!\n", __func__, chg_type, file_pos, temp_val);*/

	switch (file_pos) {
	case SQC_IFS_PMIC_INIT:
		retval = sqc_hal_init_pmic_charger(chg_type);
		break;
	case SQC_IFS_PMIC_ENABLE:
		retval = sqc_hal_charging_enable_set(chg_type, temp_val);
		break;
	case SQC_IFS_PMIC_FCV:
		retval = sqc_hal_set_chging_fcv(chg_type, temp_val);
		break;
	case SQC_IFS_PMIC_FCC:
		retval = sqc_hal_set_chging_fcc(chg_type, temp_val);
		break;
	case SQC_IFS_PMIC_ICL:
		retval = sqc_hal_set_chging_icl(chg_type, temp_val);
		break;
	case SQC_IFS_PMIC_TOPOFF:
		retval = sqc_hal_set_chging_topoff(chg_type, temp_val);
		break;
	case SQC_IFS_PMIC_RECHG_SOC:
		retval = sqc_hal_set_rechg_soc(chg_type, temp_val);
		break;
	case SQC_IFS_PMIC_RECHG_VOLT:
		retval = sqc_hal_set_rechg_volt(chg_type, temp_val);
		break;
	case SQC_IFS_PMIC_ROLE:
		retval = sqc_hal_charging_role_set(chg_type, temp_val);
		break;
	case SQC_IFS_PMIC_BATT_OVP:
		retval = sqc_hal_batt_ovp_voltage_set(chg_type, temp_val);
		break;
	case SQC_IFS_PMIC_BATT_OVP_ALM:
		retval = sqc_hal_batt_ovp_alarm_voltage_set(chg_type, temp_val);
		break;
	case SQC_IFS_PMIC_BATT_OCP:
		retval = sqc_hal_batt_ocp_current_set(chg_type, temp_val);
		break;
	case SQC_IFS_PMIC_BATT_OCP_ALM:
		retval = sqc_hal_batt_ocp_alarm_current_set(chg_type, temp_val);
		break;
	case SQC_IFS_PMIC_BATT_UCP_ALM:
		retval = sqc_hal_batt_ucp_alarm_current_set(chg_type, temp_val);
		break;
	case SQC_IFS_PMIC_AC_OVP:
		retval = sqc_hal_ac_ovp_voltage_set(chg_type, temp_val);
		break;
	case SQC_IFS_PMIC_USB_OVP:
		retval = sqc_hal_usb_ovp_voltage_set(chg_type, temp_val);
		break;
	case SQC_IFS_PMIC_USB_OVP_ALM:
		retval = sqc_hal_usb_ovp_alarm_voltage_set(chg_type, temp_val);
		break;
	case SQC_IFS_PMIC_USB_OCP:
		retval = sqc_hal_usb_ocp_current_set(chg_type, temp_val);
		break;
	case SQC_IFS_PMIC_USB_OCP_ALM:
		retval = sqc_hal_usb_ocp_alarm_current_set(chg_type, temp_val);
		break;
	case SQC_IFS_POWERPATH_ENABLE:
		retval = sqc_hal_powerpath_set(chg_type, temp_val);
		break;
	default:
		retval = -ENOMEM;
		pr_err("%s f_pos default!\n", __func__, *f_pos);
		break;
	}

	mutex_unlock(&(chg_hal_data.file_mutex));

	kfree(handle_buf);

	return (retval < 0) ? retval : count;
}

static int sqc_chg_open(struct inode *inp, struct file *filp)
{
	int retval = 0;

	mutex_lock(&(chg_hal_data.file_mutex));

	if (chg_hal_data.ref_count < 1)
		chg_hal_data.ref_count++;
	else
		retval = -EBUSY;

	mutex_unlock(&(chg_hal_data.file_mutex));

	return retval;
}

static int sqc_chg_release(struct inode *inp, struct file *filp)
{
	mutex_lock(&(chg_hal_data.file_mutex));

	chg_hal_data.ref_count--;
	if (chg_hal_data.ref_count < 0)
		chg_hal_data.ref_count = 0;

	mutex_unlock(&(chg_hal_data.file_mutex));

	return 0;
}

static const struct file_operations sqc_chg_fops = {
	.owner = THIS_MODULE,
	.llseek = sqc_chg_llseek,
	.read = sqc_chg_read,
	.write = sqc_chg_write,
	.open = sqc_chg_open,
	.release = sqc_chg_release,

};

int sqc_hal_charger_register(struct sqc_pmic_chg_ops *ops, int chg_type)
{
	int ret = 0, sqc_pd_major = 0;
	dev_t sqc_chg_devno;
	struct device *device_ptr = NULL;

	if ((ops == NULL) || (chg_hal_data.chg_ops[chg_type] != NULL)) {
		pr_err("sqc pd_phy register fail!\n");
		return -EPERM;
	}

	mutex_init(&chg_hal_data.file_mutex);

	if (chg_hal_data.device_class == NULL) {
		ret = alloc_chrdev_region(&sqc_chg_devno, 0, 1, SQC_CHG_DEVNAME);
		if (ret) {
			pr_err("Error: Can't Get Major number for adc_cali\n");
			return ret;
		}

		cdev_init(&(chg_hal_data.main_dev), &sqc_chg_fops);

		ret = cdev_add(&(chg_hal_data.main_dev), sqc_chg_devno, 1);
		if (ret) {
			pr_err("adc_cali Error: cdev_add\n");
			goto err_cdev_add;
		}

		sqc_pd_major = MAJOR(sqc_chg_devno);
		chg_hal_data.device_class = class_create(THIS_MODULE, SQC_CHG_DEVNAME);
		if (!chg_hal_data.device_class) {
			pr_err("%s: Failed to create sqc_pd char device\n",	__func__);
			goto err_device_class;
		}

		device_ptr = device_create(chg_hal_data.device_class, NULL,
					   sqc_chg_devno, NULL, SQC_CHG_DEVNAME);
		if (IS_ERR(device_ptr)) {
			pr_err("%s: Failed to create sqc_pd char device\n",	__func__);
			goto err_device_region;
		}
	}

	chg_hal_data.chg_ops[chg_type] = ops;

	pr_info("sqc_pd_fops init done!!!\n ");

	return 0;
err_device_region:
	if (chg_hal_data.device_class != NULL) {
		class_destroy(chg_hal_data.device_class);
		chg_hal_data.device_class = NULL;
	}
err_device_class:
	cdev_del(&chg_hal_data.main_dev);
err_cdev_add:
	unregister_chrdev_region(sqc_chg_devno, 1);

	return -ENODEV;
}
EXPORT_SYMBOL_GPL(sqc_hal_charger_register);

