/*
 * Driver for zte misc functions
 * function1: used for translate hardware GPIO to SYS GPIO number
 * function2: update fingerprint status to kernel from fingerprintd,2016/01/18
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/sysfs.h>
#include <linux/of_address.h>
#include <linux/delay.h>
#include <linux/power_supply.h>
#ifdef CONFIG_ZTE_POWER_SUPPLY_COMMON
#include <linux/zte_power_supply.h>
#endif
#include <linux/version.h>
#include <vendor/common/zte_misc.h>

#if (KERNEL_VERSION(4, 1, 0) <= LINUX_VERSION_CODE)
#define KERNEL_ABOVE_4_1_0
#endif


struct zte_gpio_info {
	int sys_num;
	const char *name;
};

#define MAX_MODULE_NAME_LEN 64

#define MAX_SUPPORT_GPIOS 16
#define MODULE_NAME "zte_misc"

struct zte_gpio_info zte_gpios[MAX_SUPPORT_GPIOS];

static const struct of_device_id zte_misc_of_match[] = {
	{ .compatible = "zte-misc", },
	{ },
};
MODULE_DEVICE_TABLE(of, zte_misc_of_match);

int get_sysnumber_byname(char *name)
{
	int i;

	for (i = 0; i < MAX_SUPPORT_GPIOS; i++) {
		if (zte_gpios[i].name) {
			if (!strcmp(zte_gpios[i].name, name))
				return zte_gpios[i].sys_num;
		}
	}
	return 0;
}

static int get_devtree_pdata(struct device *dev)
{
	struct device_node *node, *pp;
	int count = -1;

	pr_info("zte_misc: translate hardware pin to system pin\n");
	node = dev->of_node;
	if (node == NULL)
		return -ENODEV;

	pp = NULL;
	while ((pp = of_get_next_child(node, pp))) {
		if (!of_find_property(pp, "label", NULL)) {
			dev_warn(dev, "Found without labels\n");
			continue;
		}
		count++;
		if (count >= MAX_SUPPORT_GPIOS) {
			pr_err("zte_gpio count out of range.\n");
			break;
		}
		zte_gpios[count].name = kstrdup(of_get_property(pp, "label", NULL),
								GFP_KERNEL);
		zte_gpios[count].sys_num = of_get_gpio(pp, 0);
		pr_info("zte_misc: sys_number=%d name=%s\n", zte_gpios[count].sys_num, zte_gpios[count].name);
	}
	return 0;
}

static int zte_misc_set_ship_mode(const char *val, const struct kernel_param *kp)
{
#ifdef CONFIG_ZTE_POWER_SUPPLY_COMMON
	struct zte_power_supply *batt_psy = NULL;
#else
	struct power_supply *batt_psy = NULL;
#endif
	const union power_supply_propval enable = {0,};
	int rc = 0;
#ifdef CONFIG_ZTE_POWER_SUPPLY_COMMON
	batt_psy = zte_power_supply_get_by_name("zte_battery");
#else
	batt_psy = power_supply_get_by_name("battery");
#endif
	if (!batt_psy) {
		pr_err("%s: batt_psy is NULL\n", __func__);
		rc = -EINVAL;
		goto Failed_loop;
	}
#ifdef CONFIG_ZTE_POWER_SUPPLY_COMMON
	rc = zte_power_supply_set_property(batt_psy,
					POWER_SUPPLY_PROP_SET_SHIP_MODE, &enable);
#else
	rc = power_supply_set_property(batt_psy,
					POWER_SUPPLY_PROP_SET_SHIP_MODE, &enable);
#endif
	if (rc) {
		pr_err("%s: set shipmode node error\n", __func__);
		rc = -EINVAL;
		goto Failed_loop;
	}

	pr_info("%s: enter into shipmode 10s later\n", __func__);

Failed_loop:
#ifdef KERNEL_ABOVE_4_1_0
	if (batt_psy)
#ifdef CONFIG_ZTE_POWER_SUPPLY_COMMON
		zte_power_supply_put(batt_psy);
#else
		power_supply_put(batt_psy);
#endif
#endif

	return rc;
}

static int zte_misc_get_ship_mode(char *val, const struct kernel_param *kp)
{
#ifdef CONFIG_ZTE_POWER_SUPPLY_COMMON
	struct zte_power_supply *batt_psy = NULL;
#else
	struct power_supply *batt_psy = NULL;
#endif
	union power_supply_propval pval = {0,};
	int rc = 0;
#ifdef CONFIG_ZTE_POWER_SUPPLY_COMMON
	batt_psy = zte_power_supply_get_by_name("zte_battery");
#else
	batt_psy = power_supply_get_by_name("battery");
#endif
	if (!batt_psy) {
		pr_err("%s: batt_psy is NULL\n", __func__);
		pval.intval = -1;
		goto Failed_loop;
	}
#ifdef CONFIG_ZTE_POWER_SUPPLY_COMMON
	rc = zte_power_supply_get_property(batt_psy,
				POWER_SUPPLY_PROP_SET_SHIP_MODE, &pval);
#else
	rc = power_supply_get_property(batt_psy,
				POWER_SUPPLY_PROP_SET_SHIP_MODE, &pval);
#endif
	if (rc) {
		pr_err("Get shipmode node error: %d\n", rc);
		pval.intval = -1;
		goto Failed_loop;
	}

Failed_loop:
#ifdef KERNEL_ABOVE_4_1_0
	if (batt_psy)
#ifdef CONFIG_ZTE_POWER_SUPPLY_COMMON
		zte_power_supply_put(batt_psy);
#else
		power_supply_put(batt_psy);
#endif
#endif

	pr_info("%s: shipmode status %d\n", __func__, pval.intval);

	return snprintf(val, PAGE_SIZE, "%d", pval.intval);
}

module_param_call(shipmode_zte, zte_misc_set_ship_mode, zte_misc_get_ship_mode, NULL, 0644);

/*
vendor_id    02    03    04    17    10   12    15    20      20
vendor_name  ATL   cos   BYD   BAK   LG   SONY  SANYO samsung samsung
resistance   10K   20K   33K   82K   180K 240K  330K  430K    470K
*/
#define BATTERY_VENDOR_NUM 9
int battery_vendor_id[BATTERY_VENDOR_NUM] = {02, 03, 04, 17, 10, 12, 15, 20, 20};
int resistance_kohm[BATTERY_VENDOR_NUM] = {10, 20, 33, 82, 180, 240, 330, 430, 470};
static int battery_module_pack_vendor_get(char *val, const struct kernel_param *kp)
{
#ifdef CONFIG_ZTE_POWER_SUPPLY_COMMON
	struct zte_power_supply *bms_psy = NULL;
#else
	struct power_supply *bms_psy = NULL;
#endif
	union power_supply_propval prop = {0,};
	int resistance = 0, i = 0, rc = 0;
	int battery_module_pack_vendor = 0;
#ifdef CONFIG_ZTE_POWER_SUPPLY_COMMON
	bms_psy = zte_power_supply_get_by_name("bms");
#else
	bms_psy = power_supply_get_by_name("bms");
#endif
	if (!bms_psy) {
		pr_err("bms_psy is NULL.\n");
		goto Failed_loop;
	}
#ifdef CONFIG_ZTE_POWER_SUPPLY_COMMON
	rc = zte_power_supply_get_property(bms_psy, POWER_SUPPLY_PROP_RESISTANCE_ID, &prop);
#else
	rc = power_supply_get_property(bms_psy, POWER_SUPPLY_PROP_RESISTANCE_ID, &prop);
#endif
	if (rc) {
		pr_err("failed to battery module pack vendor, error:%d.\n", rc);
		goto Failed_loop;
	}

	resistance = prop.intval / 1000;
	if (resistance > (resistance_kohm[BATTERY_VENDOR_NUM - 1] * 109 / 100)
		|| resistance < (resistance_kohm[0] * 91 / 100)) {
		pr_err("resistance is out of range, %dkohm\n", resistance);
		goto Failed_loop;
	}

	for (i = 0; i < (BATTERY_VENDOR_NUM - 1); i++) {
		if (resistance < resistance_kohm[i] * 109 / 100
			&& resistance > resistance_kohm[i] * 91 / 100) {
			battery_module_pack_vendor = battery_vendor_id[i];
			break;
		}
	}

	pr_info("battery resistance is %dkohm, battery_vendor_id = %2d\n",
		resistance, battery_module_pack_vendor);

Failed_loop:
#ifdef KERNEL_ABOVE_4_1_0
		if (bms_psy)
#ifdef CONFIG_ZTE_POWER_SUPPLY_COMMON
			zte_power_supply_put(bms_psy);
#else
			power_supply_put(bms_psy);
#endif
#endif

	return snprintf(val, PAGE_SIZE, "%2d", battery_module_pack_vendor);
}
module_param_call(battery_module_pack_vendor, NULL,
				battery_module_pack_vendor_get, NULL, 0644);


enum charger_types_oem charge_type_oem = CHARGER_TYPE_DEFAULT;

#if defined(CONFIG_VENDOR_ZTE_MISC_COMMON)
EXPORT_SYMBOL(charge_type_oem);
#endif

module_param_named(
	charge_type_oem, charge_type_oem, int, 0644
);

int zte_timezone = 0;

#ifdef CONFIG_ZTE_POWER_SUPPLY_COMMON
static int zte_get_timezone(void)
{
	struct device_node *np;
	const char *cmd_line, *s;
	int ret;
	char val_buf[128];
	long value;

	np = of_find_node_by_path("/chosen");
	if (!np)
		return 0;
	ret = of_property_read_string(np, "bootargs", &cmd_line);
	if (ret < 0)
		return ret;

	s = strstr(cmd_line, "androidboot.timezone=");
	if (!s) {
		pr_err("can't find androidboot.timezone\n");
		return -1;
	}

	sscanf(s, "androidboot.timezone=%s", val_buf);

	ret = kstrtol(val_buf, 0, &value);
	if (ret)
		return ret;

	zte_timezone = value;
	return 0;
}
#else
static int __init zte_timezone_setup(char *str)
{
	long value;
	int err;

	err = kstrtol(str, 0, &value);
	if (err)
		return err;
	zte_timezone = value;
	return 1;
}
__setup("androidboot.timezone=", zte_timezone_setup);
#endif

module_param_named(
	zte_timezone, zte_timezone, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH
);

int zte_poweroff_charging = 0;

#ifdef CONFIG_ZTE_POWER_SUPPLY_COMMON
int zte_poweroff_charging_status(void)
{
	struct device_node *np;
	const char *cmd_line, *s;
	int ret;
	char val_buf[128];

	np = of_find_node_by_path("/chosen");
	if (!np) {
		pr_err("%s find chosen failed!\n",__func__);
		return 0;

	}
	ret = of_property_read_string(np, "bootargs", &cmd_line);
	if (ret < 0)
		pr_err("%s get bootargs failed!\n",__func__);

	s = strstr(cmd_line, "androidboot.mode=");
	if (!s) {
		pr_err("can't find androidboot.mode\n");
		return -1;
	}

	sscanf(s, "androidboot.mode=%s", val_buf);

	if (!strncmp(val_buf, "charger", strlen("charger"))) {
		zte_poweroff_charging = 1;
	} else {
		zte_poweroff_charging = 0;
	}

	pr_info("zte_misc: poweroff_charging: %d, val_buf=%s\n", zte_poweroff_charging, val_buf);

	return zte_poweroff_charging;
}
#else
static int __init zte_poweroff_charging_handle(char *str)
{
	if (!strncmp(str, "charger", strlen("charger"))) {
		zte_poweroff_charging = true;
	} else {
		zte_poweroff_charging = false;
	}

	pr_info("zte_misc: setup mode, %s[%d]\n", str, zte_poweroff_charging);

	return 0;
}
__setup("androidboot.mode=", zte_poweroff_charging_handle);

int zte_poweroff_charging_status(void)
{
	pr_info("zte_misc: poweroff_charging: %d\n", zte_poweroff_charging);

	return zte_poweroff_charging;
}
#endif

EXPORT_SYMBOL_GPL(zte_poweroff_charging_status);


static int zte_misc_get_design_capacity(char *val, const struct kernel_param *kp)
{
	int rc = 0;
	int zte_design_capacity = 0;
	struct power_supply *battery_psy = NULL;
	union power_supply_propval prop = {0,};

	battery_psy = power_supply_get_by_name("battery");
	if (!battery_psy) {
		pr_err("bms_psy is NULL.\n");
		goto Failed_loop;
	}

	rc = power_supply_get_property(battery_psy,
			POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, &prop);
	if (rc) {
		pr_err("failed to battery design capacity, error:%d.\n", rc);
		goto Failed_loop;
	}

Failed_loop:
#ifdef KERNEL_ABOVE_4_1_0
	if (battery_psy)
		power_supply_put(battery_psy);
#endif

	/* convert unit from uAh to mAh if need*/
	zte_design_capacity = (prop.intval >= 1000000) ? (prop.intval / 1000) : prop.intval;

	pr_info("battery design capacity = %dmAh\n", zte_design_capacity);

	return snprintf(val, PAGE_SIZE, "%d", zte_design_capacity);
}
module_param_call(design_capacity, NULL, zte_misc_get_design_capacity, NULL, 0644);

struct zte_misc_ops node_ops_list[] = {
	/*common node*/
	{"enable_to_shutdown", NULL, NULL, NULL, NULL},
	{"enable_to_dump_reg", NULL, NULL, NULL, NULL},
	/*charge policy node*/
	{"demo_charging_policy", NULL, NULL, NULL, NULL},
	{"expired_charging_policy", NULL, NULL, NULL, NULL},
	{"charging_time_sec", NULL, NULL, NULL, NULL},
	{"force_disching_sec", NULL, NULL, NULL, NULL},
	{"policy_cap_min", NULL, NULL, NULL, NULL},
	{"policy_cap_max", NULL, NULL, NULL, NULL},
	{"policy_enable", NULL, NULL, NULL, NULL},
	{"bcl_demon_switch", NULL, NULL, NULL, NULL},
	{"slave_chg_id", NULL, NULL, NULL, NULL},
	{"protocol_chg_id", NULL, NULL, NULL, NULL},
	/*QC3+ node*/
	{"qc3dp_sleep_mode", NULL, NULL, NULL, NULL},
	/*fast charger report raw_soc*/
	{"enable_raw_soc", NULL, NULL, NULL, NULL},
	/*vendor node*/
	{"screen_on", NULL, NULL, NULL, NULL},
	{"wireless_charging_signal_good", NULL, NULL, NULL, NULL},
	{"wireless_efficiency", NULL, NULL, NULL, NULL},
	{"wireless_signal_strength", NULL, NULL, NULL, NULL},
	{"wireless_tx_rx", NULL, NULL, NULL, NULL},
	{"wireless_debug", NULL, NULL, NULL, NULL},
	{"pe1_debug", NULL, NULL, NULL, NULL},
	{"pd_debug", NULL, NULL, NULL, NULL},
	{"super_charge_mode", NULL, NULL, NULL, NULL},
	/*VZW node*/
	{"charge_remain_time", NULL, NULL, NULL, NULL},
	/*battery temperature transation point debug*/
	{"batt_temp_debug", NULL, NULL, NULL, NULL},
	/*capacity transation point debug*/
	{"cap_debug", NULL, NULL, NULL, NULL},
	{"charger_power", NULL, NULL, NULL, NULL},
	{"thermal_control_en", NULL, NULL, NULL, NULL},
	{"typec_temp", NULL, NULL, NULL, NULL},
	{"typec_temp_pmic", NULL, NULL, NULL, NULL},
	{"typec_temp_debug", NULL, NULL, NULL, NULL},
	/*breath led boardtest*/
	{"is_breath_led_exist", NULL, NULL, NULL, NULL},
	{"zlog_enable_test", NULL, NULL, NULL, NULL},
};

static int zte_misc_common_callback_set(const char *val, const struct kernel_param *kp)
{
	int i = 0, ret = 0;
	char node_name[MAX_MODULE_NAME_LEN] = {0, };

	memset(node_name, 0, sizeof(node_name));

	if (sscanf(kp->name, MODULE_PARAM_PREFIX"%s", node_name) != 1) {
		pr_info("sscanf node_name failed\n");
		return -EINVAL;
	}

	/*pr_err("%s: val %s, node name %s!!!\n", __func__, val, node_name);*/

	for (i = 0; i < ARRAY_SIZE(node_ops_list); i++) {
		if (!strncmp(node_ops_list[i].node_name, node_name, strlen(node_name))
					&& (strlen(node_ops_list[i].node_name) == strlen(node_name))
					&& (node_ops_list[i].set != NULL)) {
			ret = node_ops_list[i].set(val, node_ops_list[i].arg);
			if (ret < 0) {
				return ret;
			}
		}
	}

	return ret;
}

static int zte_misc_common_callback_get(char *val, const struct kernel_param *kp)
{
	int i = 0, ret = 0;
	char node_name[MAX_MODULE_NAME_LEN] = {0, };

	memset(node_name, 0, sizeof(node_name));

	if (sscanf(kp->name, MODULE_PARAM_PREFIX"%s", node_name) != 1) {
		pr_info("sscanf node_name failed\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(node_ops_list); i++) {
		if (!strncmp(node_ops_list[i].node_name, node_name, strlen(node_name))
					&& (strlen(node_ops_list[i].node_name) == strlen(node_name))
					&& (node_ops_list[i].get != NULL)) {
			ret = node_ops_list[i].get(val, node_ops_list[i].arg);
			if (ret < 0) {
				return ret;
			}
		}
	}

	/*pr_err("%s: val %s, node name %s!!!\n", __func__, val, node_name);*/

	return ret;
}

static struct kernel_param_ops zte_misc_common_callback = {
	.set = zte_misc_common_callback_set,
	.get = zte_misc_common_callback_get,
};

/*
  *Emode function to enable/disable 0% shutdown
  */
module_param_cb(enable_to_shutdown, &zte_misc_common_callback, NULL, 0644);
module_param_cb(enable_to_dump_reg, &zte_misc_common_callback, NULL, 0644);

/*charge policy node*/
module_param_cb(demo_charging_policy, &zte_misc_common_callback, NULL, 0644);
module_param_cb(expired_charging_policy, &zte_misc_common_callback, NULL, 0644);
module_param_cb(charging_time_sec, &zte_misc_common_callback, NULL, 0644);
module_param_cb(force_disching_sec, &zte_misc_common_callback, NULL, 0644);
module_param_cb(policy_cap_min, &zte_misc_common_callback, NULL, 0644);
module_param_cb(policy_cap_max, &zte_misc_common_callback, NULL, 0644);
module_param_cb(policy_enable, &zte_misc_common_callback, NULL, 0664);
module_param_cb(bcl_demon_switch, &zte_misc_common_callback, NULL, 0664);


/*
P855A02 node
*/
module_param_cb(screen_on, &zte_misc_common_callback, NULL, 0644);
module_param_cb(wireless_charging_signal_good, &zte_misc_common_callback, NULL, 0644);
module_param_cb(super_charge_mode, &zte_misc_common_callback, NULL, 0644);
module_param_cb(wireless_efficiency, &zte_misc_common_callback, NULL, 0644);
module_param_cb(wireless_signal_strength, &zte_misc_common_callback, NULL, 0644);
module_param_cb(wireless_tx_rx, &zte_misc_common_callback, NULL, 0644);
module_param_cb(wireless_debug, &zte_misc_common_callback, NULL, 0644);
module_param_cb(pe1_debug, &zte_misc_common_callback, NULL, 0644);
module_param_cb(pd_debug, &zte_misc_common_callback, NULL, 0644);

module_param_cb(slave_chg_id, &zte_misc_common_callback, NULL, 0644);
module_param_cb(protocol_chg_id, &zte_misc_common_callback, NULL, 0644);

/* VZW node */
module_param_cb(charge_remain_time, &zte_misc_common_callback, NULL, 0644);

/*battery temperature transation point debug*/
module_param_cb(batt_temp_debug, &zte_misc_common_callback, NULL, 0644);

/*capacity transation point debug
* usage:
* echo 10 20 > /sys/module/zte_misc/parameters/cap_debug
* 10: delay 10s
* 20: force set 20% as capacity
*/
module_param_cb(cap_debug, &zte_misc_common_callback, NULL, 0644);
module_param_cb(charger_power, &zte_misc_common_callback, NULL, 0644);
module_param_cb(thermal_control_en, &zte_misc_common_callback, NULL, 0644);
module_param_cb(typec_temp, &zte_misc_common_callback, NULL, 0644);
module_param_cb(typec_temp_pmic, &zte_misc_common_callback, NULL, 0644);
module_param_cb(typec_temp_debug, &zte_misc_common_callback, NULL, 0644);
module_param_cb(qc3dp_sleep_mode, &zte_misc_common_callback, NULL, 0644);
module_param_cb(enable_raw_soc, &zte_misc_common_callback, NULL, 0664);

/*breath led boardtest*/
module_param_cb(is_breath_led_exist, &zte_misc_common_callback, NULL, 0664);
module_param_cb(zlog_enable_test, &zte_misc_common_callback, NULL, 0664);

int zte_misc_register_callback(struct zte_misc_ops * node_ops, void * arg)
{
	int i = 0, result = 0;

	if ((node_ops == NULL) || (node_ops->node_name == NULL)) {
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(node_ops_list); i++) {
		if (!strncmp(node_ops_list[i].node_name, node_ops->node_name, strlen(node_ops->node_name))
					&& (strlen(node_ops_list[i].node_name) == strlen(node_ops->node_name))) {

			pr_info("%s: name[%d]: %s\n", __func__, i, node_ops_list[i].node_name);

			if ((!node_ops_list[i].get)	&& (!node_ops_list[i].set)
						&& (!node_ops_list[i].free) && (!node_ops_list[i].arg)) {
				node_ops_list[i].get = node_ops->get;
				node_ops_list[i].set = node_ops->set;
				node_ops_list[i].free = node_ops->free;
				node_ops_list[i].arg = (arg) ? (arg) : (node_ops->arg);
				result = 0;
			} else {
				pr_info("%s: register name[%d]: %s failed!!!\n", __func__, i,
														node_ops_list[i].node_name);
				result = -EFAULT;
			}

			break;
		}
	}

	result = (i >= ARRAY_SIZE(node_ops_list)) ? (-EINVAL) : (result);

	return result;
}
EXPORT_SYMBOL_GPL(zte_misc_register_callback);

int zte_misc_get_node_val(const char *node_name)
{
	int i = 0, result = 0, ret = 0;
	char val[MAX_MODULE_NAME_LEN] = {0, };

	if ((node_name == NULL) || (!strlen(node_name))) {
		return -EINVAL;
	}

	memset(val, 0, sizeof(val));

	for (i = 0; i < ARRAY_SIZE(node_ops_list); i++) {
		if (!strncmp(node_ops_list[i].node_name, node_name, strlen(node_name))
					&& (strlen(node_ops_list[i].node_name) == strlen(node_name))
					&& (node_ops_list[i].get != NULL)) {
			ret = node_ops_list[i].get(val, node_ops_list[i].arg);
			if (ret < 0) {
				return -EINVAL;
			}
			break;
		}
	}

	if (i >= ARRAY_SIZE(node_ops_list)) {
		pr_err("find node_name failed\n");
		return -EINVAL;
	}

	if (sscanf(val, "%d", &result) != 1) {
		pr_err("sscanf result failed\n");
		return -EINVAL;
	}

	return result;
}
EXPORT_SYMBOL_GPL(zte_misc_get_node_val);


static int zte_misc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int error;
	pr_info("%s into\n", __func__);

#ifdef CONFIG_ZTE_POWER_SUPPLY_COMMON
	zte_get_timezone();
#endif

	error = get_devtree_pdata(dev);
	if (error)
		return error;

	pr_info("%s end\n", __func__);
	return 0;
}

static int  zte_misc_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver zte_misc_device_driver = {
	.probe		= zte_misc_probe,
	.remove		= zte_misc_remove,
	.driver		= {
		.name	= "zte-misc",
		.owner	= THIS_MODULE,
		.of_match_table = zte_misc_of_match,
	}
};

int __init zte_misc_init(void)
{
	pr_info("%s into\n", __func__);

	return platform_driver_register(&zte_misc_device_driver);
}

static void __exit zte_misc_exit(void)
{
	platform_driver_unregister(&zte_misc_device_driver);
}

fs_initcall(zte_misc_init);
module_exit(zte_misc_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Misc driver for zte");
MODULE_ALIAS("platform:zte-misc");
