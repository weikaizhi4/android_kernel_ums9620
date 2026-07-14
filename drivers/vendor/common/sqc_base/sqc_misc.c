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
#define pr_fmt(fmt)	"SQC_MISC_HAL: %s: " fmt, __func__

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
#include <linux/power_supply.h>
#include <linux/pmic-voter.h>
#include <linux/kobject.h>

#include <sqc_common.h>
#include <vendor/common/zte_misc.h>
#include <mt-plat/mtk_boot.h>
#include <linux/proc_fs.h>

#ifdef CONFIG_FB
#include <linux/notifier.h>
#include <linux/fb.h>
#endif

#define SQC_MISC_DEVNAME "sqc_misc_node"

#define SQC_CAS_SETTING_VOTER			"CAS_SETTING_VOTER"
#define SQC_POLICY_SETTING_VOTER		"POLICY_SETTING_VOTER"
#define SQC_ADB_SETTING_VOTER			"ADB_SETTING_VOTER"
#define SQC_THERMAL_SETTING_VOTER		"THERMAL_SETTING_VOTER"
#define SQC_ZTE_THERMAL_VOTER		    "ZTE_THERMAL_VOTER"
#define SQC_MTK_FLASH_VOTER		    	"SQC_MTK_FLASH_VOTER"
#define SQC_MTK_QC3P_VOTER		    	"SQC_MTK_QC3P_VOTER"

enum charger_type {
	CHARGER_UNKNOWN = 0,
	STANDARD_HOST,		/* USB : 450mA */
	CHARGING_HOST,
	NONSTANDARD_CHARGER,	/* AC : 450mA~1A */
	STANDARD_CHARGER,	/* AC : ~1A */
	APPLE_2_1A_CHARGER, /* 2.1A apple charger */
	APPLE_1_0A_CHARGER, /* 1A apple charger */
	APPLE_0_5A_CHARGER, /* 0.5A apple charger */
	WIRELESS_CHARGER,
};

struct misc_dev_data {
	int ref_count;
	struct cdev main_dev;
	struct class *device_class;
	struct mutex file_mutex;
	struct platform_device *uevent_device;
	int chg_status;
	int chg_type;
	int health_status;
	int thermal_ibus_limit;
	int thermal_vbat_limit;
	int thermal_ibat_limit;
	int thermal_topoff_limit;
	int thermal_rechg_soc;
	int thermal_rechg_volt;
	int deamon_pps_icl;
	int deamon_fcc;
	int deamon_topoff;
	struct power_supply	*interface_psy;
	struct votable		*fcc_votable;
	struct votable		*fcv_votable;
	struct votable		*topoff_votable;
	struct votable		*recharge_soc_votable;
	struct votable		*recharge_voltage_votable;
	struct votable		*usb_icl_votable;
	#ifdef CONFIG_FB
	struct notifier_block fb_notifier;
	#endif
	bool screen_on;
	bool fast_chg;
	bool raw_soc_enabled;

	atomic_t			init_finished;
};

static struct misc_dev_data misc_hal_data;

#ifdef CONFIG_VENDOR_ZTE_MISC
extern enum charger_types_oem charge_type_oem;
#else
static enum charger_types_oem charge_type_oem = CHARGER_TYPE_DEFAULT;
#endif

/*
static int sqc_power_supply_changed(void)
{
	struct power_supply *psy = NULL;

	return 0;

	psy = power_supply_get_by_name("battery");
	if (!psy) {
		pr_info("Get battery psy failed!!\n");
		return -EINVAL;
	}

	power_supply_changed(psy);

	power_supply_put(psy);

	pr_info("sqc notify battery changed!!!!\n");

	return 0;
}
*/

#define EVENT_STRING_LENGTH 64
static int sqc_power_supply_changed(void)
{
	struct power_supply *psy = NULL;

	psy = power_supply_get_by_name("battery");
	if (!psy) {
		pr_info("get battery psy failed!!\n");
		goto failed_loop;
	}

	power_supply_changed(psy);

	power_supply_put(psy);

	return 0;

failed_loop:
	return -EINVAL;
}


void sqc_send_raw_capacity_event(int fast_capacity)
{
	char event_string[EVENT_STRING_LENGTH];
	char *envp[2] = {event_string, NULL};

	if (misc_hal_data.uevent_device == NULL) {
		pr_info("fast_capacity send failed\n");
		return;
	}

	pr_info("fast_capacity=%d\n", fast_capacity);

	snprintf(event_string, EVENT_STRING_LENGTH, "capacity=%d", fast_capacity);
	kobject_uevent_env(&misc_hal_data.uevent_device->dev.kobj, KOBJ_CHANGE, envp);
}
EXPORT_SYMBOL_GPL(sqc_send_raw_capacity_event);

static int sqc_set_prop_by_name(const char *name, enum power_supply_property psp, int data)
{
	struct power_supply *psy = NULL;
	union power_supply_propval val = {0, };
	int rc = 0;

	if (name == NULL) {
		pr_info("psy name is NULL!!\n");
		goto failed_loop;
	}

	psy = power_supply_get_by_name(name);
	if (!psy) {
		pr_info("get %s psy failed!!\n", name);
		goto failed_loop;
	}

	val.intval = data;

	rc = power_supply_set_property(psy,
				psp, &val);
	if (rc < 0) {
		pr_info("Failed to set %s property:%d rc=%d\n", name, psp, rc);
		return rc;
	}

	power_supply_put(psy);

	return 0;

failed_loop:
	return -EINVAL;
}

int sqc_get_property(enum power_supply_property psp,
	union power_supply_propval *val)
{
	int retval = 0;

	if (!atomic_read(&misc_hal_data.init_finished)) {
		val->intval = 0;
		pr_err("sqc misc Uninitialized!!!\n");
		return 0;
	}

	mutex_lock(&(misc_hal_data.file_mutex));

	switch (psp) {
		case POWER_SUPPLY_PROP_STATUS:
			retval = misc_hal_data.chg_status;
			break;
		case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
			retval = get_client_vote(misc_hal_data.fcc_votable, SQC_ADB_SETTING_VOTER);
			retval = (retval < 0) ? 1 : 0;
			break;
		case POWER_SUPPLY_PROP_CHARGING_ENABLED:
			retval = get_client_vote(misc_hal_data.usb_icl_votable, SQC_ADB_SETTING_VOTER);
			retval = (retval < 0) ? 1 : 0;
			break;
		default:
			pr_debug("psp type: UNKNOWN\n");
			retval = -EINVAL;
	}

	mutex_unlock(&(misc_hal_data.file_mutex));

	if (val)
		val->intval = retval;

	return retval;
}
EXPORT_SYMBOL_GPL(sqc_get_property);

int sqc_set_property(enum power_supply_property psp,
	const union power_supply_propval *val)
{
	int retval = 0;

	if (!atomic_read(&misc_hal_data.init_finished)) {
		pr_err("sqc misc Uninitialized!!!\n");
		return 0;
	}

	mutex_lock(&(misc_hal_data.file_mutex));

	switch (psp) {
		case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
			vote(misc_hal_data.fcc_votable, SQC_ADB_SETTING_VOTER, !val->intval, 0);
			break;
		case POWER_SUPPLY_PROP_CHARGING_ENABLED:
			vote(misc_hal_data.usb_icl_votable, SQC_ADB_SETTING_VOTER, !val->intval, 0);
			break;
		case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
			if (val->intval >= 0)
				vote(misc_hal_data.fcc_votable, SQC_THERMAL_SETTING_VOTER, true, val->intval);
			else
				vote(misc_hal_data.fcc_votable, SQC_THERMAL_SETTING_VOTER, false, 0);
			break;
		case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
			if (val->intval >= 0)
				vote(misc_hal_data.usb_icl_votable, SQC_THERMAL_SETTING_VOTER, true, val->intval);
			else
				vote(misc_hal_data.usb_icl_votable, SQC_THERMAL_SETTING_VOTER, false, 0);
			break;
		case POWER_SUPPLY_PROP_FLASH_CURRENT_MAX:
			if (val->intval >= 0)
				vote(misc_hal_data.fcc_votable, SQC_MTK_FLASH_VOTER, true, val->intval);
			else
				vote(misc_hal_data.fcc_votable, SQC_MTK_FLASH_VOTER, false, 0);
			break;
		case POWER_SUPPLY_PROP_HW_CURRENT_MAX:
			if (val->intval >= 0)
				vote(misc_hal_data.fcc_votable, SQC_MTK_QC3P_VOTER, true, val->intval);
			else
				vote(misc_hal_data.fcc_votable, SQC_MTK_QC3P_VOTER, false, 0);
			break;
		default:
			pr_debug("psp type: UNKNOWN\n");
			retval = -EINVAL;
	}

	mutex_unlock(&(misc_hal_data.file_mutex));

	return retval;
}
EXPORT_SYMBOL_GPL(sqc_set_property);

static int sqc_set_raw_soc_statue(bool flag)
{
	if ((flag)&&(misc_hal_data.raw_soc_enabled != true)) {
		sqc_set_prop_by_name("battery", POWER_SUPPLY_PROP_CAPACITY_RAW, true);
		sqc_set_prop_by_name("usb", POWER_SUPPLY_PROP_PRESENT, true);
		misc_hal_data.raw_soc_enabled = true;
		pr_info("%s real enable flag:%d, raw_soc_enabled:%d\n", __func__, flag, misc_hal_data.raw_soc_enabled);
	}

	if ((!flag)&&(misc_hal_data.raw_soc_enabled != false)) {
		sqc_set_prop_by_name("battery", POWER_SUPPLY_PROP_CAPACITY_RAW, false);
		sqc_set_prop_by_name("usb", POWER_SUPPLY_PROP_PRESENT, false);
		misc_hal_data.raw_soc_enabled = false;
		pr_info("%s real disable flag:%d, raw_soc_enabled:%d\n", __func__, flag, misc_hal_data.raw_soc_enabled);
	}

	return 0;
}

static int sqc_set_oem_type(struct misc_dev_data *pdata)
{
	enum charger_type mtk_chg_type = 0;

	if ((pdata->chg_type == SQC_SLEEP_MODE_TYPE)
			|| (pdata->chg_type == SQC_DOZE_MODE_TYPE)) {
		pr_info("sleep mode return\n");
		return 0;
	}

	switch (pdata->chg_type) {
	case SQC_SDP_TYPE:
		mtk_chg_type = STANDARD_HOST;
		break;
	case SQC_CDP_TYPE:
		mtk_chg_type = CHARGING_HOST;
		break;
	case SQC_FLOAT_TYPE:
		mtk_chg_type = STANDARD_CHARGER;
		break;
	case SQC_DCP_TYPE:
	case SQC_QC2D0_5V_TYPE:
	case SQC_QC2D0_9V_TYPE:
	case SQC_QC2D0_12V_TYPE:
	case SQC_QC3D0_TYPE:
	case SQC_QC3D0_PLUS_18W_TYPE:
	case SQC_QC3D0_PLUS_27W_TYPE:
	case SQC_QC3D0_PLUS_45W_TYPE:
	case SQC_PD3D0_APDO_TYPE:
	case SQC_PD3D0_BASE_TYPE:
	case SQC_PD3D0_RESET_TYPE:
		mtk_chg_type = STANDARD_CHARGER;
		break;
	default:
		mtk_chg_type = CHARGER_UNKNOWN;
		break;
	}

	pr_info("sqc_chg_type %d, mtk_chg_type %d\n", pdata->chg_type, mtk_chg_type);

	sqc_set_prop_by_name("charger", POWER_SUPPLY_PROP_ONLINE,
				(mtk_chg_type == CHARGER_UNKNOWN) ? false : true);

	sqc_set_prop_by_name("charger", POWER_SUPPLY_PROP_CHARGE_TYPE, mtk_chg_type);

	switch (pdata->chg_type) {
	case SQC_PD3D0_APDO_TYPE:
		pr_info("charing type: CHARGER_TYPE_SQC, ICL %d\n", misc_hal_data.deamon_pps_icl);
		if (misc_hal_data.deamon_pps_icl >= 5000) {
			charge_type_oem = CHARGER_TYPE_SQC;
		} else {
			charge_type_oem = CHARGER_TYPE_PD;
		}
		break;
	case SQC_PD3D0_BASE_TYPE:
		pr_info("charing type: CHARGER_TYPE_PD\n");
		charge_type_oem = CHARGER_TYPE_PD;
		break;
	case SQC_PD3D0_RESET_TYPE:
	case SQC_SDP_TYPE:
	case SQC_CDP_TYPE:
	case SQC_DCP_TYPE:
	case SQC_FLOAT_TYPE:
		pr_info("charing type: CHARGER_TYPE_5V_ADAPTER\n");
		charge_type_oem = CHARGER_TYPE_5V_ADAPTER;
		break;
	case SQC_QC2D0_5V_TYPE:
	case SQC_QC2D0_9V_TYPE:
	case SQC_QC2D0_12V_TYPE:
	case SQC_QC3D0_TYPE:
		pr_info("charing type: CHARGER_TYPE_5V_ADAPTER(QC2D0/QC3D0)\n");
		charge_type_oem = CHARGER_TYPE_5V_ADAPTER;
		break;
	case SQC_QC3D0_PLUS_18W_TYPE:
	case SQC_QC3D0_PLUS_27W_TYPE:
	case SQC_QC3D0_PLUS_45W_TYPE:
		pr_info("charing type: CHARGER_TYPE_QC\n");
		charge_type_oem = CHARGER_TYPE_QC;
		break;
	default:
		pr_info("charing type: CHARGER_TYPE_UNKNOWN\n");
		charge_type_oem = CHARGER_TYPE_UNKNOWN;
	}

	misc_hal_data.fast_chg = ((charge_type_oem == CHARGER_TYPE_FAST_CHARGER)
								|| (charge_type_oem == CHARGER_TYPE_SQC)) ? true : false;
	pr_info("%s charge_type_oem %d, battery_get_boot_mode %d screen_on:%d fast_chg:%d\n",
		__func__, charge_type_oem, battery_get_boot_mode(), misc_hal_data.screen_on, misc_hal_data.fast_chg);

	if (battery_get_boot_mode() == NORMAL_BOOT) {
		if (misc_hal_data.fast_chg && misc_hal_data.screen_on) {
			sqc_set_raw_soc_statue(true);
		} else {
			sqc_set_raw_soc_statue(false);
		}
	}

	return 0;
}

int sqc_misc_fb_notifier_callback(struct notifier_block *self,
	unsigned long event, void *data)
{
	struct fb_event *fb_event = data;

	if (fb_event && fb_event->data) {
		if (event == FB_EARLY_EVENT_BLANK) {
			/* before fb blank */
		} else if (event == FB_EVENT_BLANK) {
			int *blank = fb_event->data;

			if (*blank == FB_BLANK_UNBLANK) {
				misc_hal_data.screen_on = true;
			} else if (*blank == FB_BLANK_POWERDOWN) {
				misc_hal_data.screen_on = false;
			}
		}
	}

	pr_info("%s screen_on: %d, fast_chg: %d\n",
		__func__, misc_hal_data.screen_on, misc_hal_data.fast_chg);

	if (misc_hal_data.fast_chg && misc_hal_data.screen_on) {
		sqc_set_raw_soc_statue(true);
	} else {
		sqc_set_raw_soc_statue(false);
	}

	return 0;
}


static loff_t sqc_misc_llseek(struct file *filp, loff_t off, int whence)
{
	loff_t newpos;

	mutex_lock(&(misc_hal_data.file_mutex));

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

	if (newpos < 0 || newpos > SQC_IFS_MAX) {
		pr_err("%s: New position 0x%04x is invalid\n",
		       __func__, (unsigned int)newpos);
		newpos = SQC_IFS_NONE;
	}

	filp->f_pos = newpos;

	mutex_unlock(&(misc_hal_data.file_mutex));

	return newpos;
}

/*
 * sqc_misc_read: read register data from RMI device
 *
 * @filp: pointer to file structure
 * @buf: pointer to user space buffer
 * @count: number of bytes to read
 * @f_pos: starting RMI register address
 */
static ssize_t sqc_misc_read(struct file *filp, char __user *buf,
			   size_t count, loff_t *f_pos)
{
	ssize_t retval = 0, read_cnt = 0;
	unsigned int temp_val[2] = {0,};

	if ((*f_pos >= SQC_IFS_MAX) || (*f_pos == SQC_IFS_NONE) || (count == 0)) {
		pr_err("%s f_pos Error!\n", __func__, *f_pos);
		return -ENXIO;
	}

	mutex_lock(&(misc_hal_data.file_mutex));

	switch (*f_pos) {
	case SQC_IFS_PTL_COM_STATUS:
		temp_val[0] = misc_hal_data.chg_status;
		temp_val[1] = misc_hal_data.health_status;
		read_cnt = sizeof(temp_val);
		pr_info("read chg_status: %d, health_status: %d\n",
			misc_hal_data.chg_status, misc_hal_data.health_status);
		break;
	case SQC_IFS_PMIC_ICL:
		temp_val[0] = misc_hal_data.thermal_ibus_limit;
		read_cnt = sizeof(temp_val[0]);
		pr_info("read thermal_ibus_limit: %d, %s\n",
					misc_hal_data.thermal_ibus_limit, get_effective_client(misc_hal_data.usb_icl_votable));
		break;
	case SQC_IFS_PMIC_FCV:
		temp_val[0] = misc_hal_data.thermal_vbat_limit;
		read_cnt = sizeof(temp_val[0]);
		pr_info("read thermal_vbat_limit: %d, %s\n",
					misc_hal_data.thermal_vbat_limit, get_effective_client(misc_hal_data.fcv_votable));
		break;
	case SQC_IFS_PMIC_FCC:
		temp_val[0] = misc_hal_data.thermal_ibat_limit;
		read_cnt = sizeof(temp_val[0]);
		pr_info("read thermal_ibat_limit: %d, %s\n",
					misc_hal_data.thermal_ibat_limit, get_effective_client(misc_hal_data.fcc_votable));
		break;
	case SQC_IFS_PMIC_TOPOFF:
		temp_val[0] = misc_hal_data.thermal_topoff_limit;
		read_cnt = sizeof(temp_val[0]);
		pr_info("read thermal_topoff_limit: %d, %s\n",
					misc_hal_data.thermal_topoff_limit, get_effective_client(misc_hal_data.topoff_votable));
		break;
	case SQC_IFS_PMIC_RECHG_SOC:
		temp_val[0] = misc_hal_data.thermal_rechg_soc;
		read_cnt = sizeof(temp_val[0]);
		pr_info("read thermal_rechg_soc: %d, %s\n",
					misc_hal_data.thermal_rechg_soc, get_effective_client(misc_hal_data.recharge_soc_votable));
		break;
	case SQC_IFS_PMIC_RECHG_VOLT:
		temp_val[0] = misc_hal_data.thermal_rechg_volt;
		read_cnt = sizeof(temp_val[0]);
		pr_info("read thermal_rechg_volt: %d, %s\n",
					misc_hal_data.thermal_rechg_volt, get_effective_client(misc_hal_data.recharge_voltage_votable));
		break;
	default:
		retval = -ENOMEM;
		pr_err("%s f_pos default!\n", __func__, *f_pos);
		break;
	}

	if (retval >= 0) {
		if (copy_to_user(buf, temp_val, read_cnt)) {
			pr_err("%s copy_to_user failed!\n", __func__);
			retval = -EFAULT;
		}
	}

	mutex_unlock(&(misc_hal_data.file_mutex));

	return (retval < 0) ? retval : read_cnt;
}

/*
 * sqc_misc_write: write register data to RMI device
 *
 * @filp: pointer to file structure
 * @buf: pointer to user space buffer
 * @count: number of bytes to write
 * @f_pos: starting RMI register address
 */
static ssize_t sqc_misc_write(struct file *filp, const char __user *buf,
			    size_t count, loff_t *f_pos)
{
	ssize_t retval = 0;
	unsigned int *handle_buf = NULL;
	int need_update = false;

	if ((*f_pos >= SQC_IFS_MAX) || (*f_pos == SQC_IFS_NONE) || (count == 0)) {
		pr_err("%s f_pos Error!\n", __func__, *f_pos);
		return -ENXIO;
	}

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

	mutex_lock(&(misc_hal_data.file_mutex));

	switch (*f_pos) {
	case SQC_IFS_PTL_COM_STATUS:
		if (misc_hal_data.chg_status != handle_buf[0]) {
			misc_hal_data.chg_status = handle_buf[0];
			pr_info("write chg_status: %d\n", misc_hal_data.chg_status);
			sqc_set_prop_by_name("battery", POWER_SUPPLY_PROP_STATUS, misc_hal_data.chg_status);
			need_update = true;
		}
		break;
	case SQC_IFS_PTL_COM_CHG_TYPE:
		if (misc_hal_data.chg_type != handle_buf[0]) {
			misc_hal_data.chg_type = handle_buf[0];
			pr_info("write chg_type: %d\n", misc_hal_data.chg_type);
			sqc_set_oem_type(&misc_hal_data);
			sqc_set_prop_by_name("battery",
				POWER_SUPPLY_PROP_CHARGE_TYPE, misc_hal_data.chg_type);
			need_update = true;
		}
		break;
	case SQC_IFS_PMIC_ICL:
		if (misc_hal_data.deamon_pps_icl != handle_buf[0]) {
			misc_hal_data.deamon_pps_icl = handle_buf[0];
			pr_info("write deamon_pps_icl: %d\n", misc_hal_data.deamon_fcc);
		}
		break;
	case SQC_IFS_PMIC_FCC:
		if (misc_hal_data.deamon_fcc != handle_buf[0]) {
			misc_hal_data.deamon_fcc = handle_buf[0];
			pr_info("write deamon_fcc: %d\n", misc_hal_data.deamon_fcc);
			sqc_set_prop_by_name("battery",
				POWER_SUPPLY_PROP_CURRENT_MAX, misc_hal_data.deamon_fcc);
		}
		break;
	case SQC_IFS_PMIC_TOPOFF:
		if (misc_hal_data.deamon_topoff != handle_buf[0]) {
			misc_hal_data.deamon_topoff = handle_buf[0];
			pr_info("write deamon_topoff: %d\n", misc_hal_data.deamon_topoff);
			sqc_set_prop_by_name("battery",
				POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT, misc_hal_data.deamon_topoff);
		}
		break;
	default:
		retval = -ENOMEM;
		pr_err("%s f_pos default!\n", __func__, *f_pos);
		break;
	}

	mutex_unlock(&(misc_hal_data.file_mutex));

	kfree(handle_buf);

	if (need_update) {
		sqc_power_supply_changed();
		power_supply_changed(misc_hal_data.interface_psy);
	}

	return (retval < 0) ? retval : count;
}

static int sqc_misc_open(struct inode *inp, struct file *filp)
{
	int retval = 0;

	mutex_lock(&(misc_hal_data.file_mutex));

	if (misc_hal_data.ref_count < 1)
		misc_hal_data.ref_count++;
	else
		retval = -EBUSY;

	mutex_unlock(&(misc_hal_data.file_mutex));

	return retval;
}

static int sqc_misc_release(struct inode *inp, struct file *filp)
{
	mutex_lock(&(misc_hal_data.file_mutex));

	misc_hal_data.ref_count--;
	if (misc_hal_data.ref_count < 0)
		misc_hal_data.ref_count = 0;

	mutex_unlock(&(misc_hal_data.file_mutex));

	return 0;
}

static const struct file_operations sqc_misc_fops = {
	.owner = THIS_MODULE,
	.llseek = sqc_misc_llseek,
	.read = sqc_misc_read,
	.write = sqc_misc_write,
	.open = sqc_misc_open,
	.release = sqc_misc_release,
};

static int sqc_fcv_vote_callback(struct votable *votable,
			void *data, int fcv_uv, const char *client)
{
	struct misc_dev_data *pdata = data;

	fcv_uv = (fcv_uv >= 0) ? (fcv_uv / 1000) : -1;

	pr_info("client: %s, fcv_uv: %d\n", client, fcv_uv);

	pdata->thermal_vbat_limit = fcv_uv;

	return 0;
}

static int sqc_fcc_vote_callback(struct votable *votable,
			void *data, int fcc_ua, const char *client)
{
	struct misc_dev_data *pdata = data;

	fcc_ua = (fcc_ua >= 0) ? (fcc_ua / 1000) : -1;

	pr_info("client: %s, fcc_ua: %d\n", client, fcc_ua);

	pdata->thermal_ibat_limit = fcc_ua;

	return 0;
}

static int sqc_topoff_vote_callback(struct votable *votable,
			void *data, int topoff_ua, const char *client)
{
	struct misc_dev_data *pdata = data;

	topoff_ua = (topoff_ua >= 0) ? (topoff_ua / 1000) : -1;

	pr_info("client: %s, topoff_ua: %d\n", client, topoff_ua);

	pdata->thermal_topoff_limit = topoff_ua;

	return 0;
}

static int sqc_recharge_soc_vote_callback(struct votable *votable,
			void *data, int recharge_soc, const char *client)
{
	struct misc_dev_data *pdata = data;

	recharge_soc = (recharge_soc >= 0) ? recharge_soc : -1;

	pr_info("client: %s, recharge_soc: %d\n", client, recharge_soc);

	pdata->thermal_rechg_soc = recharge_soc;

	return 0;
}

static int sqc_recharge_voltage_vote_callback(struct votable *votable,
			void *data, int recharge_voltage, const char *client)
{
	struct misc_dev_data *pdata = data;

	recharge_voltage = (recharge_voltage >= 0) ? (recharge_voltage / 1000) : -1;

	pr_info("client: %s, recharge_voltage: %d\n", client, recharge_voltage);

	pdata->thermal_rechg_volt = recharge_voltage;

	return 0;
}

static int sqc_usb_icl_vote_callback(struct votable *votable,
			void *data, int max_icl_ua, const char *client)
{
	struct misc_dev_data *pdata = data;

	max_icl_ua = (max_icl_ua >= 0) ? (max_icl_ua / 1000) : -1;

	pr_info("client: %s, max_icl_ua: %d\n", client, max_icl_ua);

	pdata->thermal_ibus_limit = max_icl_ua;

	return 0;
}

static int interface_psy_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *pval)
{
	struct misc_dev_data *pdata = power_supply_get_drvdata(psy);
	int rc = 0;

	if (!pdata || !atomic_read(&pdata->init_finished)) {
		pr_err("interface Uninitialized!!!\n");
		return -ENODATA;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		pval->intval = get_client_vote(pdata->fcc_votable, SQC_CAS_SETTING_VOTER);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		pval->intval = get_client_vote(pdata->fcv_votable, SQC_CAS_SETTING_VOTER);
		break;
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
		pval->intval = get_client_vote(pdata->usb_icl_votable, SQC_CAS_SETTING_VOTER);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		pval->intval = get_client_vote(pdata->topoff_votable, SQC_CAS_SETTING_VOTER);
		break;
	case POWER_SUPPLY_PROP_RECHARGE_SOC:
		pval->intval = get_client_vote(pdata->recharge_voltage_votable, SQC_CAS_SETTING_VOTER);
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		pval->intval = get_client_vote(pdata->usb_icl_votable, SQC_POLICY_SETTING_VOTER);
		pval->intval = (pval->intval < 0) ? 1 : 0;
		break;
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
		pval->intval = get_client_vote(pdata->fcc_votable, SQC_POLICY_SETTING_VOTER);
		pval->intval = (pval->intval < 0) ? 1 : 0;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		pval->intval = get_client_vote(pdata->fcc_votable, SQC_ZTE_THERMAL_VOTER);
		pval->intval = (pval->intval < 0) ? -1 : pval->intval;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		pval->intval = get_client_vote(pdata->usb_icl_votable, SQC_THERMAL_SETTING_VOTER);
		pval->intval = (pval->intval < 0) ? -1 : pval->intval;
		break;
	default:
		pr_info("interface unsupported property %d\n", psp);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int interface_psy_set_property(struct power_supply *psy,
				enum power_supply_property psp,
				const union power_supply_propval *pval)
{
	struct misc_dev_data *pdata = power_supply_get_drvdata(psy);
	int rc = 0;

	if (!pdata || !atomic_read(&pdata->init_finished)) {
		pr_err("interface Uninitialized!!!\n");
		return -ENODATA;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		if (pval->intval > 0)
			vote(pdata->fcc_votable, SQC_CAS_SETTING_VOTER, true, pval->intval);
		else
			vote(pdata->fcc_votable, SQC_CAS_SETTING_VOTER, false, 0);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		if (pval->intval > 0)
			vote(pdata->fcv_votable, SQC_CAS_SETTING_VOTER, true, pval->intval);
		else
			vote(pdata->fcv_votable, SQC_CAS_SETTING_VOTER, false, 0);
		break;
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
		vote(pdata->usb_icl_votable, SQC_CAS_SETTING_VOTER,
								!!pval->intval, pval->intval ? 100000 : 0);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		if (pval->intval > 0)
			vote(pdata->topoff_votable, SQC_CAS_SETTING_VOTER, true, pval->intval * 1000);
		else
			vote(pdata->topoff_votable, SQC_CAS_SETTING_VOTER, false, 0);
		break;
	case POWER_SUPPLY_PROP_RECHARGE_SOC:
		if (pval->intval > 0)
			vote(pdata->recharge_voltage_votable, SQC_CAS_SETTING_VOTER, true, pval->intval);
		else
			vote(pdata->recharge_voltage_votable, SQC_CAS_SETTING_VOTER, false, 0);
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		vote(pdata->usb_icl_votable, SQC_POLICY_SETTING_VOTER,
								(pval->intval ? false : true), 0);
		break;
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
		vote(pdata->fcc_votable, SQC_POLICY_SETTING_VOTER,
								(pval->intval ? false : true), 0);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		if (pval->intval >= 0)
			vote(pdata->fcc_votable, SQC_ZTE_THERMAL_VOTER, true, pval->intval);
		else
			vote(pdata->fcc_votable, SQC_ZTE_THERMAL_VOTER, false, 0);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		if (pval->intval >= 0)
			vote(pdata->usb_icl_votable, SQC_THERMAL_SETTING_VOTER, true, pval->intval);
		else
			vote(pdata->usb_icl_votable, SQC_THERMAL_SETTING_VOTER, false, 0);
		break;
	default:
		pr_info("interface unsupported property %d\n", psp);
		rc = -EINVAL;
		break;
	}


	return rc;
}

static int interface_property_is_writeable(struct power_supply *psy,
					enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_RECHARGE_SOC:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return 1;
	default:
		break;
	}

	return 0;
}

static void interface_external_power_changed(struct power_supply *psy)
{
	pr_info("power supply changed\n");
}

static int sqc_enable_raw_soc_set(const char *val, const void *arg)
{
	int raw_soc_enable = 1;

	if (val == NULL) {
		pr_info("%s invalid parameters \n",	__func__);
		return -EINVAL;
	}

	sscanf(val, "%d", &raw_soc_enable);

	pr_info("%s screen_on:%d, fast_chg:%d, raw_soc_enable:%d\n",
		__func__, misc_hal_data.screen_on, misc_hal_data.fast_chg, raw_soc_enable);

	if ((raw_soc_enable)&&(misc_hal_data.fast_chg)) {
		sqc_set_raw_soc_statue(true);
	} else {
		sqc_set_raw_soc_statue(false);
	}

	return 0;
}

static int sqc_enable_raw_soc_get(char *val, const void *arg)
{
	return snprintf(val, PAGE_SIZE, "%u", misc_hal_data.raw_soc_enabled);
}

static struct zte_misc_ops enable_raw_soc_node = {
	.node_name = "enable_raw_soc",
	.set = sqc_enable_raw_soc_set,
	.get = sqc_enable_raw_soc_get,
	.free = NULL,
	.arg = NULL,
};

static int charger_manager_chg_type_show(struct seq_file *m, void *v)
{
	int chg_val = 0;

	switch (charge_type_oem) {
	case CHARGER_TYPE_SQC:
	case CHARGER_TYPE_PD:
		chg_val = 1;
		break;
	default:
		chg_val = 0;
		break;
	}

	seq_printf(m, "%d", chg_val);

	return 0;
}

static int charger_manager_chg_type_open(struct inode *inode, struct file *file)
{
	return single_open(file, charger_manager_chg_type_show, NULL);
}


static const struct file_operations charger_manager_chg_type_node = {
	.owner = THIS_MODULE,
	.open = charger_manager_chg_type_open,
	.read = seq_read,
	.llseek	= seq_lseek,
};

static enum power_supply_property interface_psy_props[] = {
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_INPUT_SUSPEND,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_RECHARGE_SOC,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,

};

static const struct power_supply_desc interface_psy_desc = {
	.name = "interface",
	.type = POWER_SUPPLY_TYPE_UNKNOWN,
	.properties = interface_psy_props,
	.num_properties = ARRAY_SIZE(interface_psy_props),
	.get_property = interface_psy_get_property,
	.set_property = interface_psy_set_property,
	.external_power_changed = interface_external_power_changed,
	.property_is_writeable = interface_property_is_writeable,
};

static int sqc_misc_init(void)
{
	int ret = 0, sqc_misc_major = 0;
	dev_t sqc_misc_devno;
	struct device *device_ptr = NULL;
	struct power_supply_config interface_psy_cfg;
	int rc = 0;

	atomic_set(&misc_hal_data.init_finished, 0);

	/* Register the power supply */
	interface_psy_cfg.drv_data = &misc_hal_data;
	interface_psy_cfg.of_node = NULL;
	interface_psy_cfg.supplied_to = NULL;
	interface_psy_cfg.num_supplicants = 0;
	misc_hal_data.interface_psy = power_supply_register(NULL, &interface_psy_desc,
					&interface_psy_cfg);
	if (IS_ERR(misc_hal_data.interface_psy)) {
		rc = PTR_ERR(misc_hal_data.interface_psy);
		pr_err("failed to register bcl_psy rc = %ld\n",
				PTR_ERR(misc_hal_data.interface_psy));
		goto register_power_supply_failed;
	}

	misc_hal_data.thermal_ibus_limit = -1;
	misc_hal_data.thermal_vbat_limit = -1;
	misc_hal_data.thermal_ibat_limit = -1;
	misc_hal_data.thermal_topoff_limit = -1;
	misc_hal_data.thermal_rechg_soc = -1;
	misc_hal_data.thermal_rechg_volt = -1;

	misc_hal_data.fcc_votable = create_votable("FCC", VOTE_MIN,
					sqc_fcc_vote_callback,
					&misc_hal_data);
	if (IS_ERR(misc_hal_data.fcc_votable)) {
		rc = PTR_ERR(misc_hal_data.fcc_votable);
		pr_err("create %s failed\n", "fcc_votable");
		goto destroy_votable;
	}

	misc_hal_data.fcv_votable = create_votable("FCV", VOTE_MIN,
					sqc_fcv_vote_callback,
					&misc_hal_data);
	if (IS_ERR(misc_hal_data.fcv_votable)) {
		rc = PTR_ERR(misc_hal_data.fcv_votable);
		pr_err("create %s failed\n", "fcv_votable");
		goto destroy_votable;
	}

	misc_hal_data.topoff_votable = create_votable("TOPOFF", VOTE_MAX,
					sqc_topoff_vote_callback,
					&misc_hal_data);
	if (IS_ERR(misc_hal_data.topoff_votable)) {
		rc = PTR_ERR(misc_hal_data.topoff_votable);
		pr_err("create %s failed\n", "topoff_votable");
		goto destroy_votable;
	}

	misc_hal_data.recharge_soc_votable = create_votable("RECH_SOC", VOTE_MIN,
					sqc_recharge_soc_vote_callback,
					&misc_hal_data);
	if (IS_ERR(misc_hal_data.recharge_soc_votable)) {
		rc = PTR_ERR(misc_hal_data.recharge_soc_votable);
		pr_err("create %s failed\n", "recharge_soc_votable");
		goto destroy_votable;
	}

	misc_hal_data.recharge_voltage_votable = create_votable("RECH_VOLTAGE", VOTE_MAX,
					sqc_recharge_voltage_vote_callback,
					&misc_hal_data);
	if (IS_ERR(misc_hal_data.recharge_voltage_votable)) {
		rc = PTR_ERR(misc_hal_data.recharge_voltage_votable);
		pr_err("create %s failed\n", "recharge_voltage_votable");
		goto destroy_votable;
	}

	misc_hal_data.usb_icl_votable = create_votable("USB_ICL", VOTE_MIN,
					sqc_usb_icl_vote_callback,
					&misc_hal_data);
	if (IS_ERR(misc_hal_data.usb_icl_votable)) {
		rc = PTR_ERR(misc_hal_data.usb_icl_votable);
		pr_err("create %s failed\n", "usb_icl_votable");
		goto destroy_votable;
	}

	mutex_init(&misc_hal_data.file_mutex);

	ret = alloc_chrdev_region(&sqc_misc_devno, 0, 1, SQC_MISC_DEVNAME);
	if (ret) {
		pr_err("Error: Can't Get Major number for adc_cali\n");
		return ret;
	}

	cdev_init(&(misc_hal_data.main_dev), &sqc_misc_fops);

	ret = cdev_add(&misc_hal_data.main_dev, sqc_misc_devno, 1);
	if (ret) {
		pr_err("adc_cali Error: cdev_add\n");
		goto err_cdev_add;
	}

	sqc_misc_major = MAJOR(sqc_misc_devno);
	misc_hal_data.device_class = class_create(THIS_MODULE, SQC_MISC_DEVNAME);
	if (!misc_hal_data.device_class) {
		pr_err("%s: Failed to create sqc_misc char device\n",	__func__);
		goto err_device_class;
	}

	device_ptr = device_create(misc_hal_data.device_class, NULL,
				   sqc_misc_devno, NULL, SQC_MISC_DEVNAME);
	if (IS_ERR(device_ptr)) {
		pr_err("%s: Failed to create sqc_misc char device\n",	__func__);
		goto err_device_region;
	}

	misc_hal_data.uevent_device = platform_device_alloc("sqc_chg", -1);
	if (!misc_hal_data.uevent_device) {
		pr_err("%s failed to allocate platform device", __func__);
		goto err_device_region;
	}

	ret = platform_device_add(misc_hal_data.uevent_device);
	if (ret < 0) {
		pr_err("%s failed to add platform device ret=%d", __func__, ret);
		goto err_device_region;
	}

	zte_misc_register_callback(&enable_raw_soc_node, &misc_hal_data);

	misc_hal_data.fb_notifier.notifier_call = sqc_misc_fb_notifier_callback;
	ret = fb_register_client(&misc_hal_data.fb_notifier);
	if (ret) {
		pr_err("Failed to register fb notifier client:%d", ret);
		goto err_device_region;
	}

	proc_create_data("driver/chg_type", 0664, NULL,
			&charger_manager_chg_type_node, NULL);

	misc_hal_data.screen_on = true;
	misc_hal_data.fast_chg = false;
	misc_hal_data.raw_soc_enabled = false;

	atomic_set(&misc_hal_data.init_finished, 1);

	pr_info("sqc_misc_fops init done!!!\n ");

	return 0;

err_device_region:
	if (misc_hal_data.device_class != NULL) {
		class_destroy(misc_hal_data.device_class);
		misc_hal_data.device_class = NULL;
	}
err_device_class:
	cdev_del(&misc_hal_data.main_dev);
err_cdev_add:
	unregister_chrdev_region(sqc_misc_devno, 1);
destroy_votable:
	atomic_set(&misc_hal_data.init_finished, 0);
	destroy_votable(misc_hal_data.fcc_votable);
	destroy_votable(misc_hal_data.fcv_votable);
	destroy_votable(misc_hal_data.topoff_votable);
	destroy_votable(misc_hal_data.recharge_soc_votable);
	destroy_votable(misc_hal_data.recharge_voltage_votable);
	destroy_votable(misc_hal_data.usb_icl_votable);
register_power_supply_failed:

	return -ENODEV;
}

static void sqc_misc_exit(void)
{
	if (misc_hal_data.device_class != NULL) {
		class_destroy(misc_hal_data.device_class);
		misc_hal_data.device_class = NULL;
	}

	cdev_del(&misc_hal_data.main_dev);

	pr_info("sqc_netlink_exit!\n");
}

module_init(sqc_misc_init);
module_exit(sqc_misc_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("zte.charger");
MODULE_DESCRIPTION("sqc charger misc daemon");


