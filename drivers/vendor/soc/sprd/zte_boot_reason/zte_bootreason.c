#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/of.h>

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
#include "zlog_common.h"
#define PWROFF_REASON_MAX_LEN 32
#define BOOT_MODE_MAX_LEN 20
#define DELAY_BOOT_MODE_WORK_TIME 3000

static char pwroff_reason[PWROFF_REASON_MAX_LEN] = "NONE";
static char boot_mode[BOOT_MODE_MAX_LEN] = "NONE";
static struct delayed_work boot_mode_work;
#endif

#define BOOT_REASON_MAX_LEN 20
static char boot_reason[BOOT_REASON_MAX_LEN] = "NONE";
static int zte_get_bootreason(void)
{
	struct device_node *np = NULL;
	const char *cmd_line = NULL, *s = NULL;
	int ret = 0;

	np = of_find_node_by_path("/chosen");
	if (!np) {
		pr_err("%s: find chosen failed\n", __func__);
		return 0;
	}

	ret = of_property_read_string(np, "bootargs", &cmd_line);
	if (ret < 0) {
		pr_err("%s: read bootargs failed\n", __func__);
		return 0;
	}

	s = strstr(cmd_line, "bootcause=");
	if (!s) {
		pr_err("%s: find bootcause failed\n", __func__);
		return 0;
	}

	sscanf(s, "bootcause=%s", boot_reason);
	pr_info("%s: zte bootcause is %s\n", __func__, boot_reason);
	return 0;
}

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
static int zte_get_pwroffreason(void)
{
	struct device_node *np = NULL;
	const char *cmd_line = NULL, *s = NULL;
	int ret = 0;

	np = of_find_node_by_path("/chosen");
	if (!np) {
		pr_err("%s: find chosen failed\n", __func__);
		return 0;
	}

	ret = of_property_read_string(np, "bootargs", &cmd_line);
	if (ret < 0) {
		pr_err("%s: read bootargs failed\n", __func__);
		return 0;
	}

	s = strstr(cmd_line, "pwroffcause=");
	if (!s) {
		pr_err("%s: find pwroffcause failed\n", __func__);
		return 0;
	}

	sscanf(s, "pwroffcause=%s", pwroff_reason);
	pr_info("%s: zte pwroffcause is %s\n", __func__, pwroff_reason);
	return 0;
}

static int zte_get_bootmode(void)
{
	struct device_node *np = NULL;
	const char *cmd_line = NULL, *s = NULL;
	int ret = 0;

	np = of_find_node_by_path("/chosen");
	if (!np) {
		pr_err("%s: find chosen failed\n", __func__);
		return 0;
	}

	ret = of_property_read_string(np, "bootargs", &cmd_line);
	if (ret < 0) {
		pr_err("%s: read bootargs failed\n", __func__);
		return 0;
	}

	s = strstr(cmd_line, "androidboot.mode=");
	if (!s) {
		pr_err("%s: find androidboot.mode failed\n", __func__);
		return 0;
	}

	sscanf(s, "androidboot.mode=%s", boot_mode);
	pr_info("%s: zte androidboot.mode is %s\n", __func__, boot_mode);
	return 0;
}

static struct zlog_client *zlog_bootmode_client = NULL;
static struct zlog_mod_info zlog_bootmode_dev = {
	.module_no = ZLOG_MODULE_BOOT,
	.name = "BSP",
	.device_name = "sprd_bsp",
	.ic_name = "dummy_bootmode",
	.module_name = "SPRD",
	.fops = NULL,
};

static void zte_log_bootmode(void)
{
	int bootmode = -1;

	if (!zlog_bootmode_client) {
		pr_err("%s zlog register client zlog_bootmode_dev fail2\n", __func__);
		return;
	}

	pr_info("enter %s!\n", __func__);

    if (strcmp(boot_mode, "normal") == 0)
		bootmode = ZLOG_BOOT_MODE_NORMAL_BOOT;
	else if (strcmp(boot_mode, "cali") == 0)
		bootmode = ZLOG_BOOT_MODE_META_BOOT;
	else if (strcmp(boot_mode, "recovery") == 0)
		bootmode = ZLOG_BOOT_MODE_RECOVERY_BOOT;
	else if (strcmp(boot_mode, "apwdgreboot") == 0)
		bootmode = ZLOG_BOOT_MODE_SW_REBOOT;
	else if (strcmp(boot_mode, "factorytest") == 0)
		bootmode = ZLOG_BOOT_MODE_FACTORY_BOOT;
	else if (strcmp(boot_mode, "alarm") == 0)
		bootmode = ZLOG_BOOT_MODE_ALARM_BOOT;
	else if (strcmp(boot_mode, "charger") == 0)
		bootmode = ZLOG_BOOT_MODE_KERNEL_POWER_OFF_CHARGING_BOOT;
	else if (strcmp(boot_mode, "fastboot") == 0)
		bootmode = ZLOG_BOOT_MODE_FASTBOOT;
	else if (strcmp(boot_mode, "special") == 0)
		bootmode = ZLOG_BOOT_MODE_SPECIAL;
	else if (strcmp(boot_mode, "iq") == 0)
		bootmode = ZLOG_BOOT_MODE_IQ;
	else if (strcmp(boot_mode, "wdgreboot") == 0)
		bootmode = ZLOG_BOOT_MODE_WDGREBOOT;
	else if (strcmp(boot_mode, "abnormalreboot") == 0)
		bootmode = ZLOG_BOOT_MODE_ABNORMALREBOOT;
	else if (strcmp(boot_mode, "panic") == 0)
		bootmode = ZLOG_BOOT_MODE_PANIC;
	else if (strcmp(boot_mode, "engtest") == 0)
		bootmode = ZLOG_BOOT_MODE_ENGTEST;
	else if (strcmp(boot_mode, "sprdisk") == 0)
		bootmode = ZLOG_BOOT_MODE_SPRDISK;
	else if (strcmp(boot_mode, "apkmmi_mode") == 0)
		bootmode = ZLOG_BOOT_MODE_APKMMI_MODE;
	else if (strcmp(boot_mode, "upt_mode") == 0)
		bootmode = ZLOG_BOOT_MODE_UPT_MODE;
	else if (strcmp(boot_mode, "apkmmi_auto_mode") == 0)
		bootmode = ZLOG_BOOT_MODE_APKMMI_AUTO_MODE;
	else
		bootmode = ZLOG_BOOT_MODE_UNKNOWN_BOOT;

	if (ZLOG_BOOT_MODE_NORMAL_BOOT != bootmode) {
		zlog_client_record(zlog_bootmode_client, "bootmode=%d\n", bootmode);
		zlog_client_notify(zlog_bootmode_client, bootmode);
	}

	return;
}

static void zte_log_bootreason(void)
{
	int bootreason = -1;

	if (!zlog_bootmode_client) {
		pr_err("%s zlog register client zlog_bootmode_dev fail2\n", __func__);
		return;
	}
	pr_info("boot_reason %s!\n", boot_reason);
	if (strcmp(boot_reason, "Sudden momentary power loss") == 0)
		bootreason = ZLOG_BOOT_REASON_POWER_LOSS;
	else if (strcmp(boot_reason, "Pbint triggered") == 0)
		bootreason = ZLOG_BOOT_REASON_POWER_KEY;
	else if (strcmp(boot_reason, "Reboot into normal") == 0)
		bootreason = ZLOG_BOOT_REASON_REBOOT;
	else if (strcmp(boot_reason, "Reboot into alarm") == 0)
		bootreason = ZLOG_BOOT_REASON_RTC;
	else if (strcmp(boot_reason, "Reboot into panic") == 0)
		bootreason = ZLOG_BOOT_REASON_KE;
	else if (strcmp(boot_reason, "Reboot into reocovery") == 0)
		bootreason = ZLOG_BOOT_REASON_RECOVERY;
	else if (strcmp(boot_reason, "Reboot into normal2") == 0)
		bootreason = ZLOG_BOOT_REASON_WDT;
	else if (strcmp(boot_reason, "Reboot into normal3") == 0)
		bootreason = ZLOG_BOOT_REASON_WDT_SW;
	else if (strcmp(boot_reason, "Reboot into sleep") == 0)
		bootreason = ZLOG_BOOT_REASON_SLEEP;
	else if (strcmp(boot_reason, "Reboot into calibration") == 0)
		bootreason = ZLOG_BOOT_REASON_CALIBRATION;
	else if (strcmp(boot_reason, "Reboot into special") == 0)
		bootreason = ZLOG_BOOT_REASON_SPECIAL;
	else if (strcmp(boot_reason, "Reboot into iqmode") == 0)
		bootreason = ZLOG_BOOT_REASON_IQMODE;
	else if (strcmp(boot_reason, "Reboot into sprdisk") == 0)
		bootreason = ZLOG_BOOT_REASON_SPRDISK;
	else if (strcmp(boot_reason, "Reboot into tos_panic") == 0)
		bootreason = ZLOG_BOOT_REASON_SECURITY_REBOOT;
	else if (strcmp(boot_reason, "7s reset for systemdump") == 0)
		bootreason = ZLOG_BOOT_REASON_MRDUMP;
	else if (strcmp(boot_reason, "Software extern reset status") == 0)
		bootreason = ZLOG_BOOT_REASON_LONG_POWKEY;
	else if (strcmp(boot_reason, "7s reset") == 0)
		bootreason = ZLOG_BOOT_REASON_7S_RESET;
	else if (strcmp(boot_reason, "Reboot into abnormal") == 0)
		bootreason = ZLOG_BOOT_REASON_ABNORMAL;
	else if (strcmp(boot_reason, "STATUS_NORMAL2 without watchdog pending") == 0)
		bootreason = ZLOG_BOOT_REASON_UNKNOWN_REBOOT;
	else
		bootreason = ZLOG_BOOT_REASON_UNKNOWN;

	if (ZLOG_BOOT_REASON_REBOOT != bootreason || ZLOG_BOOT_REASON_LONG_POWKEY != bootreason) {
		zlog_client_record(zlog_bootmode_client, "bootreason=%s\n", boot_reason);
		zlog_client_notify(zlog_bootmode_client, bootreason);
	}

	return;
}

static void zte_log_pwroffreason(void)
{
	int pwroffreason = -1;

	if (!zlog_bootmode_client) {
		pr_err("%s zlog register client zlog_bootmode_dev fail2\n", __func__);
		return;
	}
	pr_info("pwroff_reason %s!\n", pwroff_reason);
	if (strcmp(pwroff_reason, "device power down") == 0)
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_POWER_KEY;
	else if (strcmp(pwroff_reason, "otp pwroff") == 0)
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_OTP;
	else if (strcmp(pwroff_reason, "write pwroff") == 0)
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_WRITE;
	else if (strcmp(pwroff_reason, "otp & write pwroff") == 0)
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_OTP_WRITE;
	else if (strcmp(pwroff_reason, "7s pwroff") == 0)
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_7S;
	else if (strcmp(pwroff_reason, "7s & otp pwroff") == 0)
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_7S_OTP;
	else if (strcmp(pwroff_reason, "7s & write pwroff") == 0)
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_7S_WRITE;
	else if (strcmp(pwroff_reason, "7s & write & otp pwroff") == 0)
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_7S_WRITE_OTP;
	else if (strcmp(pwroff_reason, "ovlo pwroff") == 0)
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_OVLO;
	else if (strcmp(pwroff_reason, "ovlo & otp pwroff") == 0)
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_OVLO_OTP;
	else if (strcmp(pwroff_reason, "ovlo & write pwroff") == 0)
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_OVLO_WRITE;
	else if (strcmp(pwroff_reason, "ovlo & write & otp pwroff") == 0)
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_OVLO_WRITE_OTP;
	else if (strcmp(pwroff_reason, "ovlo and 7s pwroff") == 0)
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_OVLO_7S;
	else if (strcmp(pwroff_reason, "ovlo & 7s & otp pwroff") == 0)
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_OVLO_7S_OTP;
	else if (strcmp(pwroff_reason, "ovlo & 7s & write pwroff") == 0)
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_OVLO_7S_WRITE;
	else if (strcmp(pwroff_reason, "ovlo & 7s & write & otp pwroff") == 0)
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_OVLO_7S_WRITE_OTP;
	else if (strcmp(pwroff_reason, "uvlo pwroff") == 0)
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_UVLO;
	else if (strcmp(pwroff_reason, "uvlo & otp pwroff") == 0)
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_UVLo_OTP;
	else if (strcmp(pwroff_reason, "uvlo & write pwroff") == 0)
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_UVLO_WRITE;
	else if (strcmp(pwroff_reason, "uvlo & write & otp pwroff") == 0)
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_UVLO_WRITE_OTP;
	else if (strcmp(pwroff_reason, "uvlo and 7s pwroff") == 0)
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_UVLO_7S;
	else if (strcmp(pwroff_reason, "uvlo & 7s & otp pwroff") == 0)
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_UVLO_7S_OTP;
	else if (strcmp(pwroff_reason, "uvlo & 7s & write pwroff") == 0)
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_UVLO_7S_WRITE;
	else if (strcmp(pwroff_reason, "uvlo & 7s & write & otp pwroff") == 0)
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_UVLO_7S_WRITE_OTP;
	else
		pwroffreason = ZLOG_BOOT_PWROFF_REASON_UNKNOWN;
	if (ZLOG_BOOT_PWROFF_REASON_POWER_KEY != pwroffreason
			|| ZLOG_BOOT_PWROFF_REASON_UVLO != pwroffreason
			|| ZLOG_BOOT_PWROFF_REASON_7S != pwroffreason
			|| ZLOG_BOOT_PWROFF_REASON_WRITE != pwroffreason
			|| ZLOG_BOOT_PWROFF_REASON_7S_WRITE != pwroffreason) {
		zlog_client_record(zlog_bootmode_client, "pwroffreason=%s\n", pwroff_reason);
		zlog_client_notify(zlog_bootmode_client, pwroffreason);
	}
	return;
}

static void zte_boot_mode_work(struct work_struct *work)
{
	zlog_bootmode_client = zlog_register_client(&zlog_bootmode_dev);
	if (!zlog_bootmode_client) {
		pr_err("%s - zlog register client zlog_bootmode_dev fail.\n", __func__);
	}

	zte_log_bootmode();
	zte_log_bootreason();
	zte_log_pwroffreason();

	return;
}

#endif

static ssize_t zte_power_reason_show(struct kobject *kobj, struct kobj_attribute *attr,
			     char *buf)
{
	int ret = 0;
	ret = snprintf(buf, BOOT_REASON_MAX_LEN, "%s\n", boot_reason);
	return ret;
}

static struct kobj_attribute zte_poweron_reason_attr =
__ATTR_RO(zte_power_reason);

static struct attribute *zte_power_reason_attributes[] = {
	&zte_poweron_reason_attr.attr,
	NULL
};

static const struct attribute_group zte_power_reason_attribute_group = {
        .attrs  = zte_power_reason_attributes,
};

static int __init zte_boot_reason_init(void)
{
	int err = 0;
	struct kobject *zte_power_reason_kobj;
	
	zte_get_bootreason();
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	zte_get_pwroffreason();
	zte_get_bootmode();
#endif

	zte_power_reason_kobj = kobject_create_and_add("power_reason", NULL);
	if (!zte_power_reason_kobj) {
		pr_err("%s() - Unable to create zte_power_reason_kobj.\n",  __func__);
		return -ENOMEM;
	}

	err = sysfs_create_group(zte_power_reason_kobj, &zte_power_reason_attribute_group);
	if (err != 0) {
		pr_err("%s - zte_power_reason_attribute_group failed.\n", __func__);
		kobject_put(zte_power_reason_kobj);
		return err;
	}
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	INIT_DELAYED_WORK(&boot_mode_work, zte_boot_mode_work);
	schedule_delayed_work(&boot_mode_work, msecs_to_jiffies(DELAY_BOOT_MODE_WORK_TIME));
#endif

	return err;
}

module_init(zte_boot_reason_init);
MODULE_DESCRIPTION("ZTE Bootreason Driver");
MODULE_LICENSE("GPL");