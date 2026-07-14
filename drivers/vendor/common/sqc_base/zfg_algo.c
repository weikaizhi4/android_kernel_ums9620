#define pr_fmt(fmt)	"ZFG_ALGO: %s: " fmt, __func__
#include <linux/alarmtimer.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/input.h>
#include <linux/proc_fs.h>
#include <linux/completion.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/power_supply.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/pmic-voter.h>
#include <zfg_hal.h>

#ifdef CONFIG_FB
#include <linux/notifier.h>
#include <linux/fb.h>
#endif

#include <sqc_common.h>

#define FORCE_UPDATE_TIMEOUT			300ULL
#define MA_MS_PER_PERCENT_RATIO		360 /* 60 * 60 * 1000 /10000 */

#define DISCHG_POLLING_RATIO			80
#define CHGING_POLLING_RATIO			4

#define MAX_RAW_CAP			10000
#define MAX_UI_CAP				100

#define ZFG_DEFAULT_VOTER			"ZFG_DEFAULT_VOTER"
#define ZFG_CURRENT_VOTER			"ZFG_CURRENT_VOTER"
#define ZFG_VOLTAGE_VOTER			"ZFG_VOLTAGE_VOTER"
#define ZFG_LOW_TEMP_VOTER		"ZFG_LOW_TEMP_VOTER"
#define ZFG_CHG_TYPE_VOTER		"ZFG_CHG_TYPE_VOTER"

enum {
	CAP_FORCE_DEC = 0,
	CAP_FORCE_INC,
	CAP_FORCE_FULL,
};

struct zfg_config_prop_t {
	u32			batt_rated_cap;
	u32			term_soc;
	u32			full_scale;
	u32			one_percent_volt;
	u32			zero_percent_volt;
};

struct zfg_status_t {
	int	uisoc;
	int	chg_status;
	int	raw_soc;
	int	high_acc_soc;
	int	base_acc_soc;
	int	dischg_base_soc;
	int	chg_base_soc;
	int	chg_acc_soc;
	int	last_decimal;
	int	show_decimal;
	int	equal_cnt;
	int	cap_status;
	int	tm_ms;
	int	soc_update_ms;
	int	cycle_count;
	int	charge_full;
	int	charge_full_design;
	int	charge_count;
	int	time_to_full_now;
	int	time_to_empty_now;
	int	batt_temp;
	int	batt_temp_backup;
	int	batt_health;
	int	chg_type;
	int	chg_fcc;
	int	chg_term;
	struct timespec last_update;
	bool	full_appeared;
	bool screen_on;
	bool need_notify;
	bool fast_chg;
	bool status_changed;
};

struct zfg_debug_t {
	int	debug_temp;
	int	debug_chg_full;
};


struct zfg_algo_info_t {
	struct device				*dev;
	struct power_supply *zfg_psy_pointer;
	struct notifier_block		nb;
	struct alarm				timeout_timer;
	struct workqueue_struct	*timeout_workqueue;
	struct delayed_work		timeout_work;
	struct workqueue_struct	*zfg_algo_probe_wq;
	struct delayed_work		zfg_algo_probe_work;
	struct zfg_config_prop_t		config_prop;
	struct wakeup_source	*policy_wake_lock;
	struct zfg_ops *fg_ops;
	struct zfg_status_t zfg_status;
	struct zfg_debug_t zfg_debug;
	struct votable		*timout_votable;
#ifdef CONFIG_FB
	struct notifier_block fb_notifier;
#endif
	bool init_finished;
};

static int zfg_algo_judge_cap_status(struct zfg_algo_info_t *zfg_algo_info);

#define OF_READ_PROPERTY(store, dt_property, retval, default_val)	\
do {											\
	retval = of_property_read_u32(np,			\
					"zfg," dt_property,		\
					&store);					\
											\
	if (retval == -EINVAL) {					\
		retval = 0;							\
		store = default_val;					\
	} else if (retval) {							\
		pr_info("Error reading " #dt_property	\
				" property rc = %d\n", retval);	\
	}										\
	pr_info("config: " #dt_property				\
				" property: [%d]\n", store);		\
} while (0)

#define OF_READ_PROPERTY_STRINGS(store, dt_property, retval, default_val)	\
do {											\
	retval = of_property_read_string(np,		\
					"zfg," dt_property,		\
					&(store));				\
											\
	if (retval == -EINVAL) {					\
		retval = 0;							\
		store = default_val;					\
	} else if (retval) {							\
		pr_info("Error reading " #dt_property	\
				" property rc = %d\n", retval);	\
		return retval;						\
	}										\
	pr_info("config: " #dt_property				\
				" property: [%s]\n", store);		\
} while (0)

#define OF_READ_ARRAY_PROPERTY(prop_data, prop_length, prop_size, dt_property, retval) \
do { \
	if (of_find_property(np, "zfg," dt_property, &prop_length)) { \
		prop_data = kzalloc(prop_length, GFP_KERNEL); \
		retval = of_property_read_u32_array(np, "zfg," dt_property, \
				 (u32 *)prop_data, prop_length / sizeof(u32)); \
		if (retval) { \
			retval = -EINVAL; \
			pr_info("Error reading " #dt_property \
				" property rc = %d\n", retval); \
			kfree(prop_data); \
			prop_data = NULL; \
			prop_length = 0; \
			return retval; \
		} else { \
			prop_length = prop_length / prop_size; \
		} \
	} else { \
		retval = -EINVAL; \
		prop_data = NULL; \
		prop_length = 0; \
		pr_info("Error geting " #dt_property \
				" property rc = %d\n", retval); \
		return retval; \
	} \
	pr_info("config: " #dt_property \
				" prop_length: [%d]\n", prop_length);\
} while (0)

static int zfg_algo_set_prop_by_name(const char *name, enum power_supply_property psp, int data)
{
	struct power_supply *psy = NULL;
	union power_supply_propval val = {0, };
	int rc = 0;

	if (name == NULL) {
		pr_err("psy name is NULL!!\n");
		goto failed_loop;
	}

	psy = power_supply_get_by_name(name);
	if (!psy) {
		pr_err("get %s psy failed!!\n", name);
		goto failed_loop;
	}

	val.intval = data;

	rc = power_supply_set_property(psy,
				psp, &val);
	if (rc < 0) {
		pr_err("Failed to set %s property:%d rc=%d\n", name, psp, rc);
		return rc;
	}

#ifdef KERNEL_ABOVE_4_1_0
	power_supply_put(psy);
#endif

	return 0;

failed_loop:
	return -EINVAL;
}

static int zfg_algo_get_prop_by_name(const char *name, enum power_supply_property psp, int *data)
{
	struct power_supply *psy = NULL;
	union power_supply_propval val = {0, };
	int rc = 0;

	if (name == NULL) {
		pr_err("psy name is NULL!!\n");
		goto failed_loop;
	}

	psy = power_supply_get_by_name(name);
	if (!psy) {
		pr_err("get %s psy failed!!\n", name);
		goto failed_loop;
	}

	rc = power_supply_get_property(psy,
				psp, &val);
	if (rc < 0) {
		pr_err("Failed to set %s property:%d rc=%d\n", name, psp, rc);
		return rc;
	}

	*data = val.intval;

#ifdef KERNEL_ABOVE_4_1_0
	power_supply_put(psy);
#endif

	return 0;

failed_loop:
	return -EINVAL;
}

static int zfg_psy_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *pval)
{
	struct zfg_algo_info_t *zfg_algo_info = power_supply_get_drvdata(psy);
	int rc = 0;

	if (!zfg_algo_info || !zfg_algo_info->fg_ops || !zfg_algo_info->init_finished) {
		pr_err("zfg_algo_info is null!!!\n");
		return -ENODATA;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (zfg_algo_info->zfg_debug.debug_chg_full) {
			pval->intval = POWER_SUPPLY_STATUS_FULL;
		} else {
			pval->intval = zfg_algo_info->zfg_status.chg_status;
		}
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		pval->intval = zfg_algo_info->zfg_status.uisoc;
		pval->intval = (pval->intval > MAX_UI_CAP) ? MAX_UI_CAP : pval->intval;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_RAW:
		pval->intval = zfg_algo_info->zfg_status.raw_soc;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		pval->intval = zfg_algo_info->fg_ops->zfg_get_bat_voltage();
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		pval->intval = zfg_algo_info->fg_ops->zfg_get_bat_current();
#ifdef ZTE_FEATURE_PV_AR
		pval->intval *= (-1);
#endif
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		pval->intval = zfg_algo_info->fg_ops->zfg_get_bat_avg_current();
#ifdef ZTE_FEATURE_PV_AR
		pval->intval *= (-1);
#endif
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		pval->intval = zfg_algo_info->zfg_status.batt_health;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		pval->intval = zfg_algo_info->fg_ops->zfg_get_bat_present();
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		pval->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		pval->intval = zfg_algo_info->zfg_status.cycle_count;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		if (zfg_algo_info->zfg_debug.debug_chg_full) {
			pval->intval =zfg_algo_info->zfg_debug.debug_chg_full;
		} else {
			pval->intval = zfg_algo_info->zfg_status.charge_full;
		}
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		pval->intval = zfg_algo_info->zfg_status.charge_full_design;
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		pval->intval = zfg_algo_info->zfg_status.charge_count;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		if (zfg_algo_info->zfg_debug.debug_temp)
			pval->intval = zfg_algo_info->zfg_debug.debug_temp;
		else
			pval->intval = zfg_algo_info->zfg_status.batt_temp;
		break;
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
		sqc_get_property(psp, pval);
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		sqc_get_property(psp, pval);
		break;
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		zfg_algo_get_prop_by_name("charger", POWER_SUPPLY_PROP_SET_SHIP_MODE, &pval->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_DONE:
		pval->intval = (zfg_algo_info->zfg_status.chg_status == POWER_SUPPLY_STATUS_FULL) ? true : false;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		pval->intval = zfg_algo_info->zfg_status.time_to_full_now;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
		pval->intval = zfg_algo_info->zfg_status.time_to_empty_now;
		break;
	case POWER_SUPPLY_PROP_RESISTANCE_ID:
		pval->intval = 10000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		pval->intval = zfg_algo_info->zfg_status.chg_type;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		pval->intval = zfg_algo_info->zfg_status.chg_fcc;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		pval->intval = zfg_algo_info->zfg_status.chg_term;
		break;
	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int zfg_psy_set_property(struct power_supply *psy,
				enum power_supply_property psp,
				const union power_supply_propval *pval)
{
	struct zfg_algo_info_t *zfg_algo_info = power_supply_get_drvdata(psy);
	int rc = 0, need_update = 0;

	if (!zfg_algo_info || !zfg_algo_info->fg_ops ||  !zfg_algo_info->init_finished) {
		pr_err("policy_info is null!!!\n");
		return -ENODATA;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		zfg_algo_info->zfg_status.chg_status = pval->intval;
		need_update = true;
		break;
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
		zfg_algo_set_prop_by_name("charger", POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, pval->intval);
		sqc_set_property(POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, pval);
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		zfg_algo_set_prop_by_name("charger", POWER_SUPPLY_PROP_CHARGING_ENABLED, pval->intval);
		sqc_set_property(POWER_SUPPLY_PROP_CHARGING_ENABLED, pval);
		break;
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		zfg_algo_set_prop_by_name("charger", POWER_SUPPLY_PROP_SET_SHIP_MODE, pval->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_DONE:
		break;
	case POWER_SUPPLY_PROP_TEMP:
		zfg_algo_info->zfg_debug.debug_temp = pval->intval;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_RAW:
		if (zfg_algo_info->zfg_status.fast_chg != pval->intval) {
			zfg_algo_info->zfg_status.fast_chg = pval->intval;
			need_update = true;
			pr_info("fast charge: %d.\n", pval->intval);
		}
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		zfg_algo_info->zfg_status.chg_type = pval->intval;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		zfg_algo_info->zfg_status.chg_fcc = pval->intval;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		zfg_algo_info->zfg_status.chg_term = pval->intval;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		pr_info("debug_chg_full: %d.\n", pval->intval);
		zfg_algo_info->zfg_debug.debug_chg_full = pval->intval;
		power_supply_changed(zfg_algo_info->zfg_psy_pointer);
		break;
	default:
		pr_info("default property\n");
		rc = -EINVAL;
	}

	if (need_update) {
		flush_delayed_work(&zfg_algo_info->timeout_work);
		cancel_delayed_work(&zfg_algo_info->timeout_work);
		queue_delayed_work(zfg_algo_info->timeout_workqueue,
			&zfg_algo_info->timeout_work, msecs_to_jiffies(100));
	}

	return rc;
}

static int zfg_property_is_writeable(struct power_supply *psy,
					enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
	case POWER_SUPPLY_PROP_CHARGE_DONE:
	case POWER_SUPPLY_PROP_CAPACITY_RAW:
	case POWER_SUPPLY_PROP_TEMP:
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		return 1;
	default:
		break;
	}

	return 0;
}

static void zfg_external_power_changed(struct power_supply *psy)
{
	pr_debug("power supply changed\n");
}


static enum power_supply_property zfg_psy_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_SET_SHIP_MODE,
	/*POWER_SUPPLY_PROP_CURRENT_COUNTER_ZTE,*/
	POWER_SUPPLY_PROP_RESISTANCE_ID,
	POWER_SUPPLY_PROP_CAPACITY_RAW,
	/*POWER_SUPPLY_PROP_VOLTAGE_MAX,*/
	/*POWER_SUPPLY_PROP_VOLTAGE_OCV,*/
	/*POWER_SUPPLY_PROP_CAPACITY_LEVEL,*/
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_CHARGE_DONE,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};


static const struct power_supply_desc zfg_psy_desc = {
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = zfg_psy_props,
	.num_properties = ARRAY_SIZE(zfg_psy_props),
	.get_property = zfg_psy_get_property,
	.set_property = zfg_psy_set_property,
	.external_power_changed = zfg_external_power_changed,
	.property_is_writeable = zfg_property_is_writeable,
};

static int zfg_algo_raw_soc_scaling(struct zfg_algo_info_t *zfg_algo_info)
{
	int hw_soc = zfg_algo_info->fg_ops->zfg_get_bat_high_accuracy_soc();
	int bat_volt = zfg_algo_info->fg_ops->zfg_get_bat_voltage() / 1000;
	int base_acc_soc = 0, high_acc_soc = 0, raw_soc = 0;
	int cap_status = zfg_algo_info->zfg_status.cap_status;
	int uisoc = zfg_algo_info->zfg_status.uisoc;
	bool switch_curve = false;

	base_acc_soc = hw_soc * MAX_UI_CAP / zfg_algo_info->config_prop.term_soc;
	zfg_algo_info->zfg_status.base_acc_soc = base_acc_soc;

	if (zfg_algo_info->zfg_status.status_changed) {
		if (zfg_algo_info->zfg_status.cap_status == CAP_FORCE_DEC) {
			uisoc = (uisoc) ? uisoc : 1;
			if (uisoc >= MAX_UI_CAP)
				zfg_algo_info->zfg_status.dischg_base_soc = base_acc_soc;
			else
				zfg_algo_info->zfg_status.dischg_base_soc =
					(zfg_algo_info->config_prop.full_scale - 1) * base_acc_soc / uisoc;

			pr_info("[### update dischg_base] %d, base_acc %d, uisoc %d\n",
				zfg_algo_info->zfg_status.dischg_base_soc, base_acc_soc, uisoc);
		} else if (zfg_algo_info->zfg_status.cap_status == CAP_FORCE_INC) {
			switch_curve = true;
			pr_info("[### update switch_curve]\n");
		}
	}

	/*Boot up for the first time, update dischg_base_soc*/
	if (!zfg_algo_info->zfg_status.dischg_base_soc)
		zfg_algo_info->zfg_status.dischg_base_soc = base_acc_soc;

	if ((cap_status == CAP_FORCE_DEC) || switch_curve) {
		high_acc_soc = base_acc_soc * (zfg_algo_info->config_prop.full_scale * MAX_UI_CAP)
				/ zfg_algo_info->zfg_status.dischg_base_soc;
		if (bat_volt < zfg_algo_info->config_prop.zero_percent_volt) {
			pr_info("bat_volt %dmv is less than %dmv, forced to 0%%\n", bat_volt, zfg_algo_info->config_prop.zero_percent_volt);
			raw_soc = 0;
		} else if (bat_volt < zfg_algo_info->config_prop.one_percent_volt) {
			pr_info("bat_volt %dmv is less than %dmv, forced to 1%%\n", bat_volt, zfg_algo_info->config_prop.one_percent_volt);
			raw_soc = 1;
		} else {
			raw_soc = high_acc_soc / MAX_UI_CAP;
		}

		zfg_algo_info->zfg_status.chg_base_soc = base_acc_soc;
		zfg_algo_info->zfg_status.chg_acc_soc = (uisoc) ? (uisoc * MAX_UI_CAP) : 1;
	} else {
		pr_info("(base_acc_soc(%d) - zfg_algo_info->zfg_status.uisoc(%d)) > 100 && chg_base %d && chg_acc %d && uisoc < 100\n",
				base_acc_soc, uisoc * MAX_UI_CAP,
				zfg_algo_info->zfg_status.chg_base_soc, zfg_algo_info->zfg_status.chg_acc_soc);
		if (((base_acc_soc - (uisoc * MAX_UI_CAP)) > MAX_UI_CAP)
				&& zfg_algo_info->zfg_status.chg_base_soc
				&& zfg_algo_info->zfg_status.chg_acc_soc
				&& (uisoc < MAX_UI_CAP)) {
			high_acc_soc = zfg_algo_info->zfg_status.chg_acc_soc + ((base_acc_soc - zfg_algo_info->zfg_status.chg_base_soc) * 2);

			pr_info("high_acc_soc(%d) = chg_acc(%d) + (base_acc_soc(%d) - chg_base_soc(%d)) *2\n",
				high_acc_soc,	zfg_algo_info->zfg_status.chg_acc_soc,
				base_acc_soc, zfg_algo_info->zfg_status.chg_base_soc);

			raw_soc = high_acc_soc / MAX_UI_CAP;
		} else {
			raw_soc = base_acc_soc / MAX_UI_CAP;
			high_acc_soc = base_acc_soc;
		}

		if ((bat_volt >= zfg_algo_info->config_prop.one_percent_volt) && (raw_soc == 0)) {
			pr_info("bat_volt %dmv is more than %dmv, forced to 1%%\n", bat_volt, zfg_algo_info->config_prop.one_percent_volt);
			raw_soc = 1;
		}
	}

	if (high_acc_soc > MAX_RAW_CAP) {
		high_acc_soc = MAX_RAW_CAP;
	} else if (high_acc_soc < 0) {
		high_acc_soc = 1;
	}

	pr_info("hw_soc %d, base_acc_soc %d, high_acc_soc %d, [%s]raw_soc %d, chg_base %d, dischg_base %d, uisoc %d\n",
			hw_soc, base_acc_soc, high_acc_soc,
			(cap_status == CAP_FORCE_DEC) ? "D" : "C", raw_soc,
			zfg_algo_info->zfg_status.chg_base_soc,
			zfg_algo_info->zfg_status.dischg_base_soc,
			zfg_algo_info->zfg_status.uisoc);

	zfg_algo_info->zfg_status.high_acc_soc = high_acc_soc;

	zfg_algo_info->zfg_status.raw_soc = raw_soc;

	return  raw_soc;
}

static int zfg_algo_notifier_switch(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct power_supply *psy = data;
	struct zfg_algo_info_t *zfg_algo_info = container_of(nb, struct zfg_algo_info_t, nb);
	const char *name = NULL;

	if (event != PSY_EVENT_PROP_CHANGED) {
		return NOTIFY_DONE;
	}

	name = psy->desc->name;

	if (strcmp(name, "usb") == 0) {
		/* pr_policy("Notify, update status\n"); */
		flush_delayed_work(&zfg_algo_info->timeout_work);
		cancel_delayed_work(&zfg_algo_info->timeout_work);
		queue_delayed_work(zfg_algo_info->timeout_workqueue,
			&zfg_algo_info->timeout_work, msecs_to_jiffies(200));
	}

	return NOTIFY_OK;
}
/*
int zfg_algo_screen_on_handle(struct zfg_algo_info_t *zfg_algo_info)
{
	int raw_soc = 0;

	flush_delayed_work(&zfg_algo_info->timeout_work);

	cancel_delayed_work(&zfg_algo_info->timeout_work);

	zfg_algo_judge_cap_status(zfg_algo_info);

	raw_soc = zfg_algo_raw_soc_scaling(zfg_algo_info);

	pr_info("screen_on: raw_soc %d, old uisoc %d +++++\n", raw_soc, zfg_algo_info->zfg_status.uisoc);

	zfg_algo_info->zfg_status.uisoc = (raw_soc > 100) ? 100 : raw_soc;

	pr_info("screen_on: raw_soc %d, new uisoc %d +++++\n", raw_soc, zfg_algo_info->zfg_status.uisoc);

	power_supply_changed(zfg_algo_info->zfg_psy_pointer);

	queue_delayed_work(zfg_algo_info->timeout_workqueue,
		&zfg_algo_info->timeout_work, msecs_to_jiffies(200));

	return 0;
}

int zfg_algo_fb_notifier_callback(struct notifier_block *self,
	unsigned long event, void *data)
{
	struct zfg_algo_info_t *zfg_algo_info = container_of(self,
				struct zfg_algo_info_t, fb_notifier);
	struct fb_event *fb_event = data;
	int *blank = fb_event->data;

	if (fb_event && fb_event->data) {
		if (event == FB_EARLY_EVENT_BLANK) {
			blank = fb_event->data;
			if (*blank == FB_BLANK_UNBLANK) {
				zfg_algo_info->zfg_status.screen_on = true;
				pr_info("fb blank on: %d\n", zfg_algo_info->zfg_status.screen_on);
			}
		} else if (event == FB_EVENT_BLANK) {
			blank = fb_event->data;
			 if (*blank == FB_BLANK_POWERDOWN) {
				zfg_algo_info->zfg_status.screen_on = false;
				pr_info("fb blank off: %d\n", zfg_algo_info->zfg_status.screen_on);
			}
		}
	}

	return 0;
}
*/
static int zfg_algo_register_notifier(struct zfg_algo_info_t *zfg_algo_info)
{
	int rc = 0;

	if (!zfg_algo_info) {
		return -EINVAL;
	}

	zfg_algo_info->nb.notifier_call = zfg_algo_notifier_switch;
	rc = power_supply_reg_notifier(&zfg_algo_info->nb);
	if (rc < 0) {
		pr_info("Couldn't register psy notifier rc = %d\n", rc);
		return -EINVAL;
	}
/*
#ifdef CONFIG_FB
	zfg_algo_info->fb_notifier.notifier_call = zfg_algo_fb_notifier_callback;
	rc = fb_register_client(&zfg_algo_info->fb_notifier);
	if (rc) {
		pr_err("Failed to register fb notifier client:%d", rc);
		return -EINVAL;
	}
#endif
*/
	return 0;
}

static int zfg_algo_judge_cap_status(struct zfg_algo_info_t *zfg_algo_info)
{
	int currnet_avg = 0, cap_status = CAP_FORCE_DEC;

	currnet_avg = zfg_algo_info->fg_ops->zfg_get_bat_avg_current();

	pr_info("chging_status %d, currnet_avg %d\n", zfg_algo_info->zfg_status.chg_status, currnet_avg);

	switch (zfg_algo_info->zfg_status.chg_status) {
	case POWER_SUPPLY_STATUS_CHARGING:
		if (currnet_avg > 0) {
			cap_status = CAP_FORCE_INC;
		} else {
			cap_status = CAP_FORCE_DEC;
		}
		break;
	case POWER_SUPPLY_STATUS_FULL:
		zfg_algo_info->zfg_status.full_appeared = true;
		break;
	default:
		cap_status = CAP_FORCE_DEC;
		zfg_algo_info->zfg_status.full_appeared = false;
		break;
	}

	if (zfg_algo_info->zfg_status.cap_status != cap_status) {
		zfg_algo_info->zfg_status.status_changed = true;
		zfg_algo_info->zfg_status.cap_status = cap_status;
	} else {
		zfg_algo_info->zfg_status.status_changed = false;
	}

	pr_info("cap_status %d, full_appeared %d, status_changed %d\n",
		zfg_algo_info->zfg_status.cap_status,
		zfg_algo_info->zfg_status.full_appeared,
		zfg_algo_info->zfg_status.status_changed);

	return 0;
}

/*static int zfg_algo_calc_delta_time(struct zfg_algo_info_t *zfg_algo_info)
{
	struct timespec now;

	get_monotonic_boottime(&now);

	pr_info("now %d, last %d +++++\n", now.tv_sec, zfg_algo_info->zfg_status.last_update.tv_sec);

	return (now.tv_sec - zfg_algo_info->zfg_status.last_update.tv_sec);
}*/

static int zfg_algo_soc_update(struct zfg_algo_info_t *zfg_algo_info, bool inc)
{
	int uisoc = zfg_algo_info->zfg_status.uisoc;

	if (inc) {
		if (uisoc < 100) {
			zfg_algo_info->zfg_status.uisoc++;
			zfg_algo_info->zfg_status.need_notify = true;
			get_monotonic_boottime(&zfg_algo_info->zfg_status.last_update);
		}
	} else {
		if (uisoc > 0) {
			zfg_algo_info->zfg_status.uisoc--;
			zfg_algo_info->zfg_status.need_notify = true;
			get_monotonic_boottime(&zfg_algo_info->zfg_status.last_update);
		}
	}

	return 0;
}

static int zfg_algo_soc_tracking_inc(struct zfg_algo_info_t *zfg_algo_info)
{
	int raw_soc = zfg_algo_info->zfg_status.raw_soc;

	if (raw_soc > zfg_algo_info->zfg_status.uisoc) {
		pr_info("raw_soc %d, uisoc %d +++++\n", raw_soc, zfg_algo_info->zfg_status.uisoc);
		zfg_algo_soc_update(zfg_algo_info, true);
	}

	return 0;
}

static int zfg_algo_soc_tracking_dec(struct zfg_algo_info_t *zfg_algo_info)
{
	int raw_soc = zfg_algo_info->zfg_status.raw_soc;

	 if (raw_soc < zfg_algo_info->zfg_status.uisoc) {
		pr_info("raw_soc %d, uisoc %d -----\n", raw_soc, zfg_algo_info->zfg_status.uisoc);
		zfg_algo_soc_update(zfg_algo_info, false);
	}

	return 0;
}

static int zfg_algo_soc_tracking_full(struct zfg_algo_info_t *zfg_algo_info)
{
	int raw_soc = 0;

	raw_soc = zfg_algo_info->zfg_status.raw_soc;

	if (zfg_algo_info->zfg_status.uisoc < 100) {
		pr_info("raw_soc %d, uisoc %d +++++\n", raw_soc, zfg_algo_info->zfg_status.uisoc);
		zfg_algo_soc_update(zfg_algo_info, true);
	}

	return 0;
}

static int zfg_algo_soc_smooth_tracking(struct zfg_algo_info_t *zfg_algo_info)
{

	switch (zfg_algo_info->zfg_status.cap_status) {
	case CAP_FORCE_INC:
		zfg_algo_soc_tracking_inc(zfg_algo_info);
		break;
	case CAP_FORCE_DEC:
		zfg_algo_soc_tracking_dec(zfg_algo_info);
		break;
	case CAP_FORCE_FULL:
		zfg_algo_soc_tracking_full(zfg_algo_info);
		break;
	default:
		break;
	}

	return 0;
}

static int zfg_algo_current_polling_time(struct zfg_algo_info_t *zfg_algo_info)
{
	int currnet_avg = 0;
	unsigned int cnt = 0, batt_rated_cap = 0, ma_ms_per_percent = 0;

	currnet_avg = zfg_algo_info->fg_ops->zfg_get_bat_avg_current() / 1000;

	batt_rated_cap = zfg_algo_info->config_prop.batt_rated_cap;

	ma_ms_per_percent = batt_rated_cap * MA_MS_PER_PERCENT_RATIO;

	pr_info("batt_rated_cap %d, ma_ms_per_percent %d ms, currnet_avg %d\n",
			batt_rated_cap, ma_ms_per_percent, currnet_avg);

	cnt = ma_ms_per_percent / abs(currnet_avg);

	pr_info("cnt(%d) = ma_per(%d) / currnet_avg(%d)\n", cnt, ma_ms_per_percent, currnet_avg);

	zfg_algo_info->zfg_status.soc_update_ms = cnt * 100 / 2;

	if (zfg_algo_info->zfg_status.cap_status == CAP_FORCE_DEC) {
		cnt = cnt * DISCHG_POLLING_RATIO;
		pr_info("cnt(%d) *= ratio(%d)\n", cnt, DISCHG_POLLING_RATIO);
	} else if (!zfg_algo_info->zfg_status.fast_chg) {
		cnt = cnt * DISCHG_POLLING_RATIO;
		pr_info("non-fastchg: cnt(%d) *= 100\n", cnt);
	} else {
		cnt = cnt * CHGING_POLLING_RATIO / 10;
		pr_info("cnt(%d) *= ratio(%d/10)\n", cnt, CHGING_POLLING_RATIO);
	}

	if (cnt > 0) {
		vote(zfg_algo_info->timout_votable, ZFG_CURRENT_VOTER, true, cnt);
		pr_info("current vote timeout %dms\n", cnt);
	} else {
		vote(zfg_algo_info->timout_votable, ZFG_CURRENT_VOTER, false, 0);
		pr_info("current vote timeout %dms\n", -1);
	}

	return 0;
}

static int zfg_algo_voltage_polling_time(struct zfg_algo_info_t *zfg_algo_info)
{
	int batt_volt = 0, currnet_avg = 0;

	batt_volt = zfg_algo_info->fg_ops->zfg_get_bat_voltage()  / 1000;

	currnet_avg = zfg_algo_info->fg_ops->zfg_get_bat_avg_current()  / 1000;

	if ((currnet_avg < 0) && (batt_volt < 3600)) {
		vote(zfg_algo_info->timout_votable, ZFG_VOLTAGE_VOTER, true, 1000);
		pr_info("dischg voltage < 3600 vote timeout %dms\n", 1000);
	} else {
		vote(zfg_algo_info->timout_votable, ZFG_VOLTAGE_VOTER, false, 0);
		pr_info("voltage vote timeout %dms\n", -1);
	}

	return 0;
}

static int zfg_algo_low_temp_polling_time(struct zfg_algo_info_t *zfg_algo_info)
{
	int batt_volt = 0, currnet_avg = 0, batt_temp = 0;

	batt_temp = zfg_algo_info->fg_ops->zfg_get_bat_temperature();

	currnet_avg = zfg_algo_info->fg_ops->zfg_get_bat_avg_current() / 1000;

	batt_volt = zfg_algo_info->fg_ops->zfg_get_bat_voltage() / 1000;

	if ((currnet_avg < 0) && (batt_temp < 100)) {
		vote(zfg_algo_info->timout_votable, ZFG_LOW_TEMP_VOTER, true, 1000);
		pr_info("dischg batt_temp < 100 vote timeout %dms\n", 1000);
	} else if ((currnet_avg < 0) && (batt_volt < 3600) && (batt_temp < 100)) {
		vote(zfg_algo_info->timout_votable, ZFG_LOW_TEMP_VOTER, true, 500);
		pr_info("dischg voltage < 3600 batt_temp < 100 vote timeout %dms\n", 500);
	} else {
		vote(zfg_algo_info->timout_votable, ZFG_LOW_TEMP_VOTER, false, 0);
		pr_info("voltage vote timeout %dms\n", -1);
	}

	return 0;
}

static int zfg_algo_chg_type_polling_time(struct zfg_algo_info_t *zfg_algo_info)
{
	if (zfg_algo_info->zfg_status.fast_chg) {
		pr_info("chg_type vote timeout %dms\n", 1000);
		vote(zfg_algo_info->timout_votable, ZFG_CHG_TYPE_VOTER, true, 1000);
	} else {
		pr_info("chg_type vote timeout %dms\n", -1);
		vote(zfg_algo_info->timout_votable, ZFG_CHG_TYPE_VOTER, false, 0);
	}

	return 0;
}

static int zfg_algo_select_polling_time(struct zfg_algo_info_t *zfg_algo_info)
{
	vote(zfg_algo_info->timout_votable, ZFG_DEFAULT_VOTER, true, 60000);

	zfg_algo_current_polling_time(zfg_algo_info);

	zfg_algo_voltage_polling_time(zfg_algo_info);

	zfg_algo_low_temp_polling_time(zfg_algo_info);

	zfg_algo_chg_type_polling_time(zfg_algo_info);

	return 0;
}

static int zfg_algo_send_high_acc_soc(struct zfg_algo_info_t *zfg_algo_info)
{
	int cap = 0, decimal_now = 0, display_decimal = 0;

	if (!zfg_algo_info->zfg_status.fast_chg) {
		return 0;
	}

	cap = zfg_algo_info->zfg_status.uisoc * 100;

	decimal_now = zfg_algo_info->zfg_status.high_acc_soc % 100;

	pr_info("decimal_now %d, last_decimal %d, show_decimal %d\n", decimal_now,
		 zfg_algo_info->zfg_status.last_decimal, zfg_algo_info->zfg_status.show_decimal);

	if (decimal_now == zfg_algo_info->zfg_status.last_decimal) {
		if (zfg_algo_info->zfg_status.equal_cnt >= 2) {
			zfg_algo_info->zfg_status.equal_cnt = 0;
			display_decimal = (decimal_now >= 99) ? 99 : decimal_now++;
			pr_info("[=] display_decimal %d[++]\n", display_decimal);
		} else {
			zfg_algo_info->zfg_status.equal_cnt++;
			display_decimal = zfg_algo_info->zfg_status.show_decimal;
			pr_info("[=] equal_cnt %d[last]\n", zfg_algo_info->zfg_status.equal_cnt);
		}
	} else if (decimal_now < zfg_algo_info->zfg_status.last_decimal) {
		display_decimal = decimal_now;
		pr_info("[<] display_decimal[now] %d\n", display_decimal);
	} else {
		if (decimal_now >= zfg_algo_info->zfg_status.show_decimal) {
			display_decimal = decimal_now;
			pr_info("[>] display_decimal[now] %d\n", display_decimal);
		} else {
			display_decimal = zfg_algo_info->zfg_status.show_decimal;
			pr_info("[>] display_decimal[last] %d\n", display_decimal);
		}
		zfg_algo_info->zfg_status.equal_cnt = 0;
	}

	zfg_algo_info->zfg_status.last_decimal = decimal_now;

	zfg_algo_info->zfg_status.show_decimal = display_decimal;

	cap = cap + display_decimal;

	cap = (cap > 10000) ? 10000 : cap;

	pr_info("cap %d, uisoc %d, high_acc_soc %d\n", cap,
		zfg_algo_info->zfg_status.uisoc, zfg_algo_info->zfg_status.high_acc_soc);

	sqc_send_raw_capacity_event(cap);

	return 0;
}

static int zfg_algo_calc_time_to_full(struct zfg_algo_info_t *zfg_algo_info)
{
	int max_sec = 0, real_sec = 0;
	int avg_current = 0, batt_rm = 0, batt_full = 0;

	avg_current = zfg_algo_info->fg_ops->zfg_get_bat_current() / 1000;

	batt_rm = zfg_algo_info->fg_ops->zfg_get_bat_remain_cap();

	batt_full = zfg_algo_info->fg_ops->zfg_get_bat_full_chg_cap();

	pr_info("batt_rm %d, batt_full %d, avg_current %d, chg_term %d\n",
		batt_rm, batt_full, avg_current, zfg_algo_info->zfg_status.chg_term);

	max_sec = (batt_full - batt_rm) * 3600 * 2 / zfg_algo_info->zfg_status.chg_fcc;

	if (avg_current > 0)
		avg_current = abs(avg_current - zfg_algo_info->zfg_status.chg_term) / 2;
	else
		avg_current = zfg_algo_info->zfg_status.chg_term;

	real_sec = (batt_full - batt_rm) * 3600 / avg_current;

	pr_info("max_sec %d, real_sec %d, avg_current %d, chg_fcc %d, chg_term %d\n",
		max_sec, real_sec, avg_current,  zfg_algo_info->zfg_status.chg_fcc, zfg_algo_info->zfg_status.chg_term);

	real_sec = (real_sec > max_sec) ? max_sec : real_sec;

	return real_sec;
}

static int zfg_algo_update_parameter(struct zfg_algo_info_t *zfg_algo_info)
{
	int temp_val = 0;

	zfg_algo_info->zfg_status.cycle_count = zfg_algo_info->fg_ops->zfg_get_bat_cycle_count();
	zfg_algo_info->zfg_status.charge_full = zfg_algo_info->fg_ops->zfg_get_bat_charge_full();
	zfg_algo_info->zfg_status.charge_full_design = zfg_algo_info->fg_ops->zfg_get_bat_charge_full_design();
	zfg_algo_info->zfg_status.charge_count = zfg_algo_info->zfg_status.charge_full * zfg_algo_info->zfg_status.uisoc / 100;
	/*zfg_algo_info->zfg_status.time_to_full_now = zfg_algo_info->fg_ops->zfg_get_bat_time_to_full_now();*/
	zfg_algo_info->zfg_status.time_to_full_now = zfg_algo_calc_time_to_full(zfg_algo_info);

	zfg_algo_info->zfg_status.time_to_empty_now = zfg_algo_info->fg_ops->zfg_get_bat_time_to_empty_now();

	temp_val = zfg_algo_info->fg_ops->zfg_get_bat_temperature();

	if (((temp_val / 10) != (zfg_algo_info->zfg_status.batt_temp_backup / 10))
			&& (abs(zfg_algo_info->zfg_status.batt_temp_backup - temp_val) > 10)) {
		zfg_algo_info->zfg_status.batt_temp_backup = temp_val;
		zfg_algo_info->zfg_status.need_notify = true;
		pr_info("need_notify batt_temp, %d %d\n", temp_val, zfg_algo_info->zfg_status.batt_temp_backup);
	}

	zfg_algo_info->zfg_status.batt_temp = temp_val;

	if (temp_val < 0) {
		zfg_algo_info->zfg_status.batt_health = POWER_SUPPLY_HEALTH_COLD;
	} else if ((temp_val >= 0) && (temp_val < 100)) {
		zfg_algo_info->zfg_status.batt_health = POWER_SUPPLY_HEALTH_COOL;
	} else if ((temp_val >= 100) && (temp_val < 450)) {
		zfg_algo_info->zfg_status.batt_health = POWER_SUPPLY_HEALTH_GOOD;
	} else if ((temp_val >= 450) && (temp_val < 600)) {
		zfg_algo_info->zfg_status.batt_health = POWER_SUPPLY_HEALTH_WARM;
	} else if (temp_val >= 600) {
		zfg_algo_info->zfg_status.batt_health= POWER_SUPPLY_HEALTH_HOT;
	}

	return 0;
}

static int zfg_algo_notify_changed(struct zfg_algo_info_t *zfg_algo_info)
{
	if (zfg_algo_info->zfg_status.need_notify) {
		power_supply_changed(zfg_algo_info->zfg_psy_pointer);
		zfg_algo_info->zfg_status.need_notify = false;
		pr_info("******** power_supply_changed zfg_psy_pointer\n");
	}

	return 0;
}

static int zfg_algo_notifier_handler(struct zfg_algo_info_t *zfg_algo_info)
{
	pr_info("into\n");

	__pm_stay_awake(zfg_algo_info->policy_wake_lock);

	zfg_algo_judge_cap_status(zfg_algo_info);

	zfg_algo_raw_soc_scaling(zfg_algo_info);

	zfg_algo_soc_smooth_tracking(zfg_algo_info);

	zfg_algo_send_high_acc_soc(zfg_algo_info);

	zfg_algo_update_parameter(zfg_algo_info);

	zfg_algo_select_polling_time(zfg_algo_info);

	zfg_algo_notify_changed(zfg_algo_info);

	__pm_relax(zfg_algo_info->policy_wake_lock);

	pr_info("exit\n");

	return 0;
}

static void zfg_algo_timeout_handler_work(struct work_struct *work)
{
	struct zfg_algo_info_t *zfg_algo_info =
			container_of(work, struct zfg_algo_info_t, timeout_work.work);

	zfg_algo_notifier_handler(zfg_algo_info);

	pr_info("##### Scheduling time %dms\n", zfg_algo_info->zfg_status.tm_ms);

	queue_delayed_work(zfg_algo_info->timeout_workqueue,
			&zfg_algo_info->timeout_work, msecs_to_jiffies(zfg_algo_info->zfg_status.tm_ms));
}

static int zfg_algo_timeout_callback(struct votable *votable,
			void *data, int tm_ms, const char *client)
{
	struct zfg_algo_info_t *zfg_algo_info = data;

	tm_ms = (tm_ms > 0) ? tm_ms : -1;

	pr_info("client: %s, tm_ms: %d\n", client, tm_ms);

	zfg_algo_info->zfg_status.tm_ms = tm_ms;

	return 0;
}

static int zfg_algo_restore_uisoc(struct zfg_algo_info_t *zfg_algo_info)
{
	u8 backup_data = 0;
	int uisoc = 0, chging_stat = 0;
	int hw_soc = zfg_algo_info->fg_ops->zfg_get_bat_high_accuracy_soc();
	int base_acc_soc = hw_soc * MAX_UI_CAP / zfg_algo_info->config_prop.term_soc;

	zfg_algo_info->fg_ops->zfg_restore_uisoc(&backup_data);

	pr_info("[%s]uisoc: %d, base_acc_soc %d\n", (backup_data & 0x80) ? "C" : "D", backup_data & 0x7F, base_acc_soc);

	uisoc = backup_data & 0x7F;
	chging_stat = backup_data & 0x80;

	if (abs((base_acc_soc / MAX_UI_CAP) - uisoc) > 10) {
		pr_info("[R] delta soc more than 10%, exit\n");
		zfg_algo_info->zfg_status.uisoc = base_acc_soc / MAX_UI_CAP;
		goto eixt_loop;
	}

	if (chging_stat) {
		pr_info("[C] base_acc_soc %d, uisoc %d\n", (base_acc_soc / MAX_UI_CAP), uisoc);
		if (abs((base_acc_soc / MAX_UI_CAP) - uisoc) > 2) {
			pr_info("[C] delta soc more than 2%, update\n");
			zfg_algo_info->zfg_status.uisoc = base_acc_soc / MAX_UI_CAP;
		} else {
			pr_info("[C] delta soc is normal\n");
			zfg_algo_info->zfg_status.uisoc = uisoc;
			zfg_algo_info->zfg_status.cap_status = CAP_FORCE_INC;
		}
	} else {
		uisoc = (uisoc) ? uisoc : 1;
		zfg_algo_info->zfg_status.dischg_base_soc = (zfg_algo_info->config_prop.full_scale - 1) * base_acc_soc / uisoc;
		zfg_algo_info->zfg_status.uisoc = uisoc;
		zfg_algo_info->zfg_status.cap_status = CAP_FORCE_DEC;
		pr_info("[D]dischg_base_soc %d\n", zfg_algo_info->zfg_status.dischg_base_soc);
	}

eixt_loop:
	zfg_algo_info->zfg_status.uisoc = (zfg_algo_info->zfg_status.uisoc > MAX_UI_CAP) ? MAX_UI_CAP : zfg_algo_info->zfg_status.uisoc;

	zfg_algo_info->fg_ops->zfg_backup_uisoc(1);

	pr_info("[E] restore_uisoc %d\n", zfg_algo_info->zfg_status.uisoc);

	return 0;
}

static void zfg_algo_probe_work(struct work_struct *work)
{
	struct zfg_algo_info_t *zfg_algo_info =
			container_of(work, struct zfg_algo_info_t, zfg_algo_probe_work.work);
	struct power_supply_config zfg_psy_cfg;

	pr_info("driver init begin\n");

	/*alarm_init(&zfg_algo_info->timeout_timer, ALARM_BOOTTIME, charger_policy_timeout_alarm_cb);*/

	zfg_algo_info->timeout_workqueue = create_singlethread_workqueue("zfg_algo_tmwork");
	INIT_DELAYED_WORK(&zfg_algo_info->timeout_work, zfg_algo_timeout_handler_work);

	zfg_algo_info->timout_votable = create_votable("TIMEOUT", VOTE_MIN,
					zfg_algo_timeout_callback,
					zfg_algo_info);
	if (IS_ERR(zfg_algo_info->timout_votable)) {
		pr_err("Create TIMEOUT votable failed\n");
		goto destroy_votable;
	}

	if (zfg_algo_register_notifier(zfg_algo_info) < 0) {
		pr_info("init register notifier info failed\n");
		goto register_notifier_failed;
	}

	zfg_algo_info->policy_wake_lock = wakeup_source_register(zfg_algo_info->dev ,"zfg_algo_wakelock");
	if (!zfg_algo_info->policy_wake_lock) {
		pr_info("wakelock register failed\n");
		goto register_power_supply_failed;
	}

	memset(&zfg_psy_cfg, 0, sizeof(struct power_supply_config));
	zfg_psy_cfg.drv_data = zfg_algo_info;
	zfg_psy_cfg.of_node = NULL;
	zfg_psy_cfg.supplied_to = NULL;
	zfg_psy_cfg.num_supplicants = 0;
	zfg_algo_info->zfg_psy_pointer = devm_power_supply_register(zfg_algo_info->dev,
				&zfg_psy_desc, &zfg_psy_cfg);
	if (IS_ERR(zfg_algo_info->zfg_psy_pointer)) {
		pr_err("failed to register zfg_psy rc = %ld\n",
				PTR_ERR(zfg_algo_info->zfg_psy_pointer));
		goto register_power_supply_failed;
	}

	do {
		zfg_algo_info->fg_ops = zfg_ops_get();
		if (zfg_algo_info->fg_ops != NULL) {
			pr_info("get zfg ic ops %p\n", zfg_algo_info->fg_ops);
			break;
		} else {
			pr_info("get zfg ic ops failed\n");
			msleep(100);
		}
	} while (1);

	zfg_algo_restore_uisoc(zfg_algo_info);

	queue_delayed_work(zfg_algo_info->timeout_workqueue, &zfg_algo_info->timeout_work, msecs_to_jiffies(100));

	zfg_algo_info->init_finished = true;
	zfg_algo_info->zfg_status.screen_on = true;

	pr_info("driver init finished\n");

	return;

destroy_votable:
register_power_supply_failed:
	power_supply_unreg_notifier(&zfg_algo_info->nb);
register_notifier_failed:
	devm_kfree(zfg_algo_info->dev, zfg_algo_info);

	pr_info("Driver Init Failed!!!\n");
}

static int zfg_algo_parse_dt(struct zfg_algo_info_t *zfg_algo_info)
{
	int retval = 0;
	struct device_node *np = zfg_algo_info->dev->of_node;

	OF_READ_PROPERTY(zfg_algo_info->config_prop.batt_rated_cap,
			"batt-rated-cap", retval, 5000);

	OF_READ_PROPERTY(zfg_algo_info->config_prop.term_soc,
			"term-soc", retval, 92);

	OF_READ_PROPERTY(zfg_algo_info->config_prop.full_scale,
			"full-scale", retval, 101);

	OF_READ_PROPERTY(zfg_algo_info->config_prop.one_percent_volt,
			"one-percent-volt", retval, 3400);

	OF_READ_PROPERTY(zfg_algo_info->config_prop.zero_percent_volt,
			"zero-percent-volt", retval, 3300);

	pr_info("batt_rated_cap %d, term_soc %d, full_scale %d, one_percent_volt %d, zero_percent_volt %d\n",
		zfg_algo_info->config_prop.batt_rated_cap,
		zfg_algo_info->config_prop.term_soc,
		zfg_algo_info->config_prop.full_scale,
		zfg_algo_info->config_prop.one_percent_volt,
		zfg_algo_info->config_prop.zero_percent_volt);

	return 0;
}


static int zfg_algo_probe(struct platform_device *pdev)
{
	struct zfg_algo_info_t *zfg_algo_info = NULL;


	zfg_algo_info = devm_kzalloc(&pdev->dev, sizeof(*zfg_algo_info), GFP_KERNEL);
	if (!zfg_algo_info) {
		pr_info("devm_kzalloc failed\n");
		return -ENOMEM;
	}

	zfg_algo_info->dev = &pdev->dev;

	platform_set_drvdata(pdev, zfg_algo_info);

	if (zfg_algo_parse_dt(zfg_algo_info) < 0) {
		pr_info("Parse dts failed\n");
		goto parse_dt_failed;
	}

	zfg_algo_info->zfg_algo_probe_wq = create_singlethread_workqueue("zfg_algo_probe_wq");

	INIT_DELAYED_WORK(&zfg_algo_info->zfg_algo_probe_work, zfg_algo_probe_work);

	queue_delayed_work(zfg_algo_info->zfg_algo_probe_wq, &zfg_algo_info->zfg_algo_probe_work, msecs_to_jiffies(20));

	pr_info("driver probe finished\n");

	return 0;

parse_dt_failed:
	devm_kfree(zfg_algo_info->dev, zfg_algo_info);
	zfg_algo_info = NULL;

	pr_info("driver probe failed\n");

	return 0;
}

static void zfg_algo_shutdown(struct platform_device *pdev)
{
	struct zfg_algo_info_t *zfg_algo_info = platform_get_drvdata(pdev);
	u8 backup_data = 0;

	pr_info("driver shutdown begin\n");

	if (zfg_algo_info == NULL) {
		goto ExitLoop;
	}

	flush_delayed_work(&zfg_algo_info->timeout_work);
	cancel_delayed_work(&zfg_algo_info->timeout_work);

	backup_data = zfg_algo_info->zfg_status.uisoc;

	if (zfg_algo_info->zfg_status.cap_status == CAP_FORCE_INC) {
		backup_data = backup_data | 0x80;
	} else {
		backup_data = backup_data & 0x7F;
	}

	zfg_algo_info->fg_ops->zfg_backup_uisoc(backup_data);

	pr_info("[W][%s]uisoc: %d\n", (backup_data & 0x80) ? "C" : "D", backup_data & 0x7F);

	zfg_algo_info->fg_ops->zfg_restore_uisoc(&backup_data);

	pr_info("[R][%s]uisoc: %d\n", (backup_data & 0x80) ? "C" : "D", backup_data & 0x7F);

	devm_kfree(zfg_algo_info->dev, zfg_algo_info);
	zfg_algo_info = NULL;

ExitLoop:
	pr_info("driver shutdown finished\n");
}

static int zfg_algo_remove(struct platform_device *pdev)
{
	struct zfg_algo_info_t *zfg_algo_info = platform_get_drvdata(pdev);

	pr_info("driver remove begin\n");

	if (zfg_algo_info == NULL) {
		goto ExitLoop;
	}

	power_supply_unreg_notifier(&zfg_algo_info->nb);

	wakeup_source_unregister(zfg_algo_info->policy_wake_lock);

	devm_kfree(zfg_algo_info->dev, zfg_algo_info);
	zfg_algo_info = NULL;

ExitLoop:
	pr_info("driver remove finished\n");

	return 0;
}

static const struct of_device_id match_table[] = {
	{ .compatible = "zte,zfg-algo", },
	{ },
};

static struct platform_driver zfg_algo_driver = {
	.driver		= {
		.name		= "zte,zfg-algo",
		.owner		= THIS_MODULE,
		.of_match_table	= match_table,
	},
	.probe		= zfg_algo_probe,
	.shutdown	= zfg_algo_shutdown,
	.remove		= zfg_algo_remove,
};

module_platform_driver(zfg_algo_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zte.charger <zte.charger@zte.com>");
MODULE_DESCRIPTION("Charge policy Service Driver");

