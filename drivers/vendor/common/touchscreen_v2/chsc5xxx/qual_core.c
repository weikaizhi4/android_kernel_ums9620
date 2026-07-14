#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/ide.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/i2c.h>
#include <linux/regulator/consumer.h>

#include "head_def.h"
#include "semi_touch_device.h"
#include "semi_touch_custom.h"
#include "semi_touch_function.h"

#define semi_io_free(pin)                   do { if (gpio_is_valid(pin)) gpio_free(pin); } while (0)

static const struct of_device_id sm_of_match[] = {
	{.compatible = "chipsemi,chsc_cap_touch",},
	{}
};

static const struct i2c_device_id sm_ts_id[] = {
	{CHSC_DEVICE_NAME, 0},
	{}
};

int semi_touch_get_int(void)
{
	int int_gpio_no = 0;
	struct device_node *of_node = NULL;

	/* of_node = of_find_node_by_name(NULL, "smtouch"); */
	/* check_return_if_zero(of_node, NULL); */
	of_node = of_find_matching_node(NULL, sm_of_match);
	check_return_if_zero(of_node, NULL);

	int_gpio_no = of_get_named_gpio(of_node, "chipsemi,int-gpio", 0);
	check_return_if_fail(int_gpio_no, NULL);

	gpio_request(int_gpio_no, "chsc_int_pin");

	return int_gpio_no;

	/* return of_get_named_gpio(of_node, "chipsemi,int-gpio", 0); */
}

int semi_touch_get_rst(void)
{
	int rst_gpio_no = 0;
	struct device_node *of_node = NULL;
	/* of_node = of_find_node_by_name(NULL, "smtouch"); */
	/* check_return_if_zero(of_node, NULL); */

	of_node = of_find_matching_node(NULL, sm_of_match);
	check_return_if_zero(of_node, NULL);

	rst_gpio_no = of_get_named_gpio(of_node, "chipsemi,rst-gpio", 0);
	check_return_if_fail(rst_gpio_no, NULL);

	gpio_request(rst_gpio_no, "chsc_rst_pin");

	return rst_gpio_no;
}

int semi_touch_get_iovdd_gpio(void)
{
	int iovdd_gpio_no = 0;
	struct device_node *of_node = NULL;
	/* of_node = of_find_node_by_name(NULL, "smtouch"); */
	/* check_return_if_zero(of_node, NULL); */

	of_node = of_find_matching_node(NULL, sm_of_match);
	check_return_if_zero(of_node, NULL);

	iovdd_gpio_no = of_get_named_gpio(of_node, "chipsemi,iovdd-gpio", 0);
	check_return_if_fail(iovdd_gpio_no, NULL);

	gpio_request(iovdd_gpio_no, "chsc_iovdd-gpio");

	return iovdd_gpio_no;
}

int semi_touch_get_irq(int rst_pin)
{
	int irq_no = 0;

	gpio_set_debounce(rst_pin, 50);

	irq_no = gpio_to_irq(rst_pin);

	return irq_no;
}

struct regulator *reg_vdd = NULL;
struct regulator *reg_vio = NULL;

int semi_touch_power_ctrl(unsigned char level)
{
	int ret = SEMI_DRV_ERR_OK;
	static unsigned char power_status = 0;	/* off */

	if (level == 1 && power_status == 0) {
		power_status = 1;
		if (!IS_ERR_OR_NULL(reg_vdd)) {
			ret = regulator_enable(reg_vdd);
			check_return_if_fail(ret, NULL);
		}
		if (!IS_ERR_OR_NULL(reg_vio)) {
			ret = regulator_enable(reg_vio);
			check_return_if_fail(ret, NULL);
		}

		if (st_dev.iovdd_pin > 0) {
			semi_io_direction_out(st_dev.iovdd_pin, 1);
		}

		if (st_dev.rst_pin > 0) {
			semi_io_direction_out(st_dev.rst_pin, 1);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
			tpd_cdev->tp_reset_timer = jiffies;
#endif
		}
		kernel_log_d("vdd power up...\n");
	} else if (level == 0 && power_status == 1) {
		power_status = 0;
		if (!IS_ERR_OR_NULL(reg_vdd)) {
			ret = regulator_disable(reg_vdd);
			check_return_if_fail(ret, NULL);
		}
		if (!IS_ERR_OR_NULL(reg_vio)) {
			ret = regulator_disable(reg_vio);
			check_return_if_fail(ret, NULL);
		}
		if (st_dev.iovdd_pin > 0) {
			semi_io_direction_out(st_dev.iovdd_pin, 0);
		}

		if (st_dev.rst_pin > 0) {
			semi_io_direction_out(st_dev.rst_pin, 0);
		}

		kernel_log_d("vdd power down...\n");
		enter_suspend_gate(st_dev.stc.ctp_run_status);
	} else {
		/* don't care */
	}

	return ret;
}

int semi_touch_power_exit(void)
{
	int ret = SEMI_DRV_ERR_OK;

	semi_touch_power_ctrl(0);

	if (!IS_ERR_OR_NULL(reg_vdd)) {
		ret = regulator_set_voltage(reg_vdd, 0, 0);
		check_return_if_fail(ret, NULL);
		regulator_put(reg_vdd);
	}
	if (!IS_ERR_OR_NULL(reg_vio)) {
		ret = regulator_set_voltage(reg_vio, 0, 0);
		check_return_if_fail(ret, NULL);
		regulator_put(reg_vio);
	}
	if (st_dev.iovdd_pin > 0) {
		semi_io_free(st_dev.iovdd_pin);
	}
	return ret;
}

int semi_touch_power_init(struct i2c_client *client)
{
	int ret = SEMI_DRV_ERR_OK;

	reg_vdd = regulator_get(&client->dev, "vdd");
	if (IS_ERR_OR_NULL(reg_vdd)) {
		kernel_log_d("vdd regulator dts not match\n");
	}

	reg_vio = regulator_get(&client->dev, "vio");
	if (IS_ERR_OR_NULL(reg_vio)) {
		kernel_log_d("vio regulator dts not match\n");
	}

	return ret;
}

/********************************************************************************************************************************/
/*virtual key*/
#if SEMI_TOUCH_VKEY_MAPPING == 0
struct kobject *sm_properties_kobj = NULL;
static ssize_t virtual_keys_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int index = 0, iter = 0;
	char *vkey_buf = buf;

	for (index = 0; (index < st_dev.stc.vkey_num) && (index < MAX_VKEY_NUMBER); index++) {
		iter += sprintf(vkey_buf + iter, "%s:%d:%d:%d:%d:%d%s",
				__stringify(EV_KEY), st_dev.stc.vkey_evt_arr[index],
				st_dev.stc.vkey_dim_map[index][0], st_dev.stc.vkey_dim_map[index][1], 50, 50,
				(index == st_dev.stc.vkey_num - 1) ? "\n" : ":");
	}

	return iter;
}

static struct kobj_attribute virtual_keys_attr = {
	.attr = {
		 .name = "virtualkeys.chsc_cap_touch",
		 .mode = S_IRUGO,
		 },
	.show = &virtual_keys_show,
};

static struct attribute *properties_attrs[] = {
	&virtual_keys_attr.attr,
	NULL
};

static struct attribute_group properties_attr_group = {
	.attrs = properties_attrs,
};

int semi_touch_vkey_initialize(void)
{
	int ret = 0;

	sm_properties_kobj = kobject_create_and_add("board_properties", NULL);
	check_return_if_zero(sm_properties_kobj, NULL);

	ret = sysfs_create_group(sm_properties_kobj, &properties_attr_group);
	check_return_if_fail(ret, NULL);

	return ret;
}
#else
#define semi_touch_vkey_initialize()    0
#endif /* SEMI_TOUCH_VKEY_MAPPING */
/********************************************************************************************************************************/
/*proximity support*/
#if SEMI_TOUCH_PROXIMITY_OPEN
#include <linux/input.h>
#define PROXIMITY_CLASS_NAME            "chsc_tpd"
#define PROXIMITY_DEVICE_NAME           "device"

/* default cmd interface(refer to sensor HAL):"/sys/class/chsc-tpd/device/proximity" */

struct chsc_proximity {
	struct class *proximity_cls;
	struct device *proximity_dev;
	struct input_dev *proximity_input;
};

static struct chsc_proximity proximity_obj;

int semi_touch_proximity_init(void)
{
	int ret = 0;

	proximity_obj.proximity_cls = class_create(THIS_MODULE, PROXIMITY_CLASS_NAME);
	check_return_if_fail(proximity_obj.proximity_cls, NULL);

	proximity_obj.proximity_dev = device_create(proximity_obj.proximity_cls, NULL, 0, NULL, PROXIMITY_DEVICE_NAME);
	check_return_if_fail(proximity_obj.proximity_cls, NULL);

	proximity_obj.proximity_input = input_allocate_device();
	check_return_if_zero(proximity_obj.proximity_input, NULL);

	proximity_obj.proximity_input->name = "proximity_tp";
	set_bit(EV_ABS, proximity_obj.proximity_input->evbit);
	input_set_capability(proximity_obj.proximity_input, EV_ABS, ABS_DISTANCE);
	input_set_abs_params(proximity_obj.proximity_input, ABS_DISTANCE, 0, 1, 0, 0);
	ret = input_register_device(proximity_obj.proximity_input);
	check_return_if_fail(ret, NULL);

	open_proximity_function(st_dev.stc.custom_function_en);

	return ret;
}

bool semi_touch_proximity_report(unsigned char proximity)
{
	kernel_log_d("proximity = %d\n", proximity);
	if (is_proximity_function_en(st_dev.stc.custom_function_en)) {
		input_report_abs(proximity_obj.proximity_input, ABS_DISTANCE, proximity);
		input_mt_sync(proximity_obj.proximity_input);
		input_sync(proximity_obj.proximity_input);
	}

	return true;
}

int semi_touch_proximity_stop(void)
{
	if (proximity_obj.proximity_input) {
		input_unregister_device(proximity_obj.proximity_input);
		input_free_device(proximity_obj.proximity_input);
	}
	if (proximity_obj.proximity_dev) {
		device_destroy(proximity_obj.proximity_cls, 0);
	}
	if (proximity_obj.proximity_cls) {
		class_destroy(proximity_obj.proximity_cls);
	}

	return 0;
}
#endif

int semi_touch_platform_variety(void)
{
	semi_touch_power_exit();

	if (st_dev.int_pin) {
		semi_io_free(st_dev.int_pin);
	}

	if (st_dev.rst_pin) {
		semi_io_free(st_dev.rst_pin);
	}
#if SEMI_TOUCH_VKEY_MAPPING == 0
	if (sm_properties_kobj != NULL) {
		sysfs_remove_group(sm_properties_kobj, &properties_attr_group);
		kobject_put(sm_properties_kobj);
	}
#endif

	return 0;
}

/*************************************************************************************************/
#if (!defined(CONFIG_FB) && !defined(CONFIG_HAS_EARLYSUSPEND) && !defined(CONFIG_DRM))
static const struct dev_pm_ops semi_touch_dev_pm_ops = {
	.suspend = semi_touch_suspend_entry,
	.resume = semi_touch_resume_entry,
};
#else
static const struct dev_pm_ops semi_touch_dev_pm_ops = {

};
#endif

#if defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
struct notifier_block sm_fb_notify;
static int semi_touch_fb_notifier_callback(struct notifier_block *self, unsigned long event, void *data)
{
	int *blank;
	struct fb_event *evdata = data;

	if (evdata && evdata->data && event == FB_EVENT_BLANK && st_dev.client) {
		blank = evdata->data;
		if (*blank == FB_BLANK_UNBLANK)
			change_tp_state(LCD_ON);
		else if (*blank == FB_BLANK_POWERDOWN)
			change_tp_state(LCD_OFF);
	}

	return 0;
}

int semi_touch_work_done(void)
{
	int ret = 0;

	ret = semi_touch_vkey_initialize();

	check_return_if_fail(ret, NULL);

	sm_fb_notify.notifier_call = semi_touch_fb_notifier_callback;
	ret = fb_register_client(&sm_fb_notify);
	check_return_if_fail(ret, NULL);

	return ret;
}

int semi_touch_resource_release(void)
{
	fb_unregister_client(&sm_fb_notify);
	return semi_touch_platform_variety();
}
#elif defined(CONFIG_DRM)
#include <linux/notifier.h>
#include <drm/drm_panel.h>
struct drm_panel *active_panel = NULL;
struct notifier_block sm_fb_notify;
static int semi_touch_drm_notifier_callback(struct notifier_block *self, unsigned long event, void *data)
{
	int *blank;
	struct drm_panel_notifier *evdata = (struct drm_panel_notifier *)data;

	if (evdata && evdata->data && event == DRM_PANEL_EVENT_BLANK && st_dev.client) {
		blank = evdata->data;
		if (*blank == DRM_PANEL_BLANK_UNBLANK)
			change_tp_state(LCD_ON);
		else if (*blank == DRM_PANEL_BLANK_POWERDOWN)
			change_tp_state(LCD_OFF);

		/* kernel_log_d("drm event = %lu, blank = %d\n", event, *blank); */
	}

	return 0;
}

static int semi_touch_drm_get_panel(struct device_node *np)
{
	int index, count;
	struct device_node *node = NULL;
	struct drm_panel *panel = NULL;

	count = of_count_phandle_with_args(np, "panel", NULL);
	if (count <= 0)
		return -SEMI_DRV_INVALID_PARAM;

	for (index = 0; index < count; index++) {
		node = of_parse_phandle(np, "panel", index);
		panel = of_drm_find_panel(node);
		of_node_put(node);
		if (!IS_ERR(panel)) {
			active_panel = panel;
			return SEMI_DRV_ERR_OK;
		}
	}

	return -SEMI_DRV_ERR_NOT_MATCH;
}

int semi_touch_work_done(void)
{
	int ret = 0;

	ret = semi_touch_vkey_initialize();
	check_return_if_fail(ret, NULL);

	ret = semi_touch_drm_get_panel(st_dev.client->dev.of_node);
	check_return_if_fail(ret, NULL);

	kernel_log_d("register drm notify, active = %x\n", active_panel);

	sm_fb_notify.notifier_call = semi_touch_drm_notifier_callback;
	ret = drm_panel_notifier_register(active_panel, &sm_fb_notify);
	check_return_if_fail(ret, NULL);

	return ret;
}

int semi_touch_resource_release(void)
{
	if (active_panel != NULL) {
		drm_panel_notifier_unregister(active_panel, &sm_fb_notify);
	}
	return semi_touch_platform_variety();
}
#elif defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
struct early_suspend esp;
static void semi_touch_early_suspend(struct early_suspend *h)
{
	if (h == NULL)
		return;

	change_tp_state(LCD_OFF);
}

static void semi_touch_late_resume(struct early_suspend *h)
{
	if (h == NULL)
		return;

	change_tp_state(LCD_ON);
}

int semi_touch_work_done(void)
{
	ret = semi_touch_vkey_initialize();
	check_return_if_fail(ret, NULL);

	esp.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	esp.suspend = semi_touch_early_suspend;
	esp.resume = semi_touch_late_resume;
	register_early_suspend(&esp);

	return 0;
}

int semi_touch_resource_release(void)
{
	unregister_early_suspend(&esp);
	return semi_touch_platform_variety();
}
#else
int semi_touch_work_done(void)
{
	return 0;
}

int semi_touch_resource_release(void)
{
	return semi_touch_platform_variety();
}
#endif

static int semi_touch_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;

	semi_touch_power_init(client);
	semi_touch_power_ctrl(1);

	ret = semi_touch_init(client);
	if (ret == -SEMI_DRV_ERR_HAL_IO) {
		semi_touch_deinit(client);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	if (tpd_cdev->tp_chip_id == TS_CHIP_ILITEK)
		tpd_cdev->ztp_probe_fail_chip_id = TS_CHIP_ILITEK;
#endif
		check_return_if_fail(ret, NULL);
	}
	tpd_cdev->TP_have_registered = true;
	tpd_cdev->tp_chip_id = TS_CHIP_SEMI;
	kernel_log_d("probe finished(result:%d) driver ver(%s)\r\n", ret, CHSC_DRIVER_VERSION);

	return ret;
}

static int semi_touch_remove(struct i2c_client *client)
{
	int ret = 0;

	ret = semi_touch_deinit(client);

	return ret;
}

void semi_tp_irq_enable(bool enable)
{
	if (enable && !st_dev.irq_enabled) {
		enable_irq(st_dev.client->irq);
		st_dev.irq_enabled = true;
		kernel_log_d("enable irq");
	} else if (!enable && st_dev.irq_enabled) {
		disable_irq_nosync(st_dev.client->irq);
		st_dev.irq_enabled= false;
		kernel_log_d("disable irq");
	}
}

void semi_tp_irq_wake(bool enable)
{
	if (enable && !st_dev.irq_wake_enabled) {
		enable_irq_wake(st_dev.client->irq);
		st_dev.irq_wake_enabled = true;
		kernel_log_d("enable_irq_wake");
	} else if (!enable && st_dev.irq_wake_enabled) {
		disable_irq_wake(st_dev.client->irq);
		st_dev.irq_wake_enabled= false;
		kernel_log_d("disable_irq_wake");
	}
}

int semi_touch_suspend_entry(struct device *dev)
{
	/* struct i2c_client *client = st_dev.client; */

	if (is_proximity_function_en(st_dev.stc.custom_function_en)) {
		if (is_proximity_activate(st_dev.stc.ctp_run_status)) {
			kernel_log_d("proximity is active, so fake suspend...");
			return SEMI_DRV_ERR_OK;
		}
	}
	if (is_guesture_function_en(st_dev.stc.custom_function_en)) {
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
		if (st_dev.is_single_tap) {
			if (tpd_cdev->one_key_enable)
				semi_touch_reset_and_detect();
			semi_touch_guesture_switch(1);
		}
		if (st_dev.is_double_tap)
			semi_touch_guesture_switch(2);
		kernel_log_d(" st_dev.is_double_tap=%d",  st_dev.is_double_tap);
		kernel_log_d(" st_dev.is_single_tap=%d",  st_dev.is_single_tap);
#else
		semi_touch_guesture_switch(1);
#endif
		semi_tp_irq_wake(true);
	} else {
#if SEMI_TOUCH_SUSPEND_BY_TPCMD
		semi_touch_suspend_ctrl(1);
#else
		semi_touch_power_ctrl(0);
#endif

		/* disable_irq(client->irq); */
		semi_tp_irq_enable(false);
		kernel_log_d("tpd real suspend...\n");
	}
	st_dev.suspended = true;
#ifdef CONFIG_TOUCHSCREEN_POINT_REPORT_CHECK
	cancel_delayed_work_sync(&tpd_cdev->point_report_check_work);
#endif
	semi_touch_clear_report();
	kernel_log_d("tpd  suspend...\n");

	return SEMI_DRV_ERR_OK;
}

int semi_touch_resume_entry(struct device *dev)
{
	unsigned char bootCheckOk = 0;
	unsigned char glove_activity = is_glove_activate(st_dev.stc.ctp_run_status);

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	if (ufp_tp_ops.aod_fp_down) {
		kernel_log_d("tpd:aod finger down!");
		ufp_tp_ops.wait_completion = true;
		if (!wait_for_completion_timeout(&ufp_tp_ops.ufp_completion, msecs_to_jiffies(1000))) {
			kernel_log_d("tpd:aod finger down timeout!");
		}
		ufp_tp_ops.aod_fp_down = false;
	}
	ufp_tp_ops.wait_completion = false;
#endif
	if (is_proximity_function_en(st_dev.stc.custom_function_en)) {
		if (is_proximity_activate(st_dev.stc.ctp_run_status)) {
			kernel_log_d("proximity is active, so fake resume...");
			return SEMI_DRV_ERR_OK;
		}
	}
	if (is_guesture_function_en(st_dev.stc.custom_function_en)) {
		semi_tp_irq_wake(false);
	} else {
#if SEMI_TOUCH_SUSPEND_BY_TPCMD == 0
		semi_touch_power_ctrl(1);
#endif
	}
	/* reset tp + iic detected */
	if (tpd_cdev->one_key_enable)
		semi_touch_guesture_switch(0);
	else
		semi_touch_reset_and_detect();

	/* enable_irq(client->irq); */
	semi_tp_irq_enable(true);

	if (glove_activity) {
		semi_touch_start_up_check(&bootCheckOk);
		if (bootCheckOk) {
			semi_touch_glove_switch(1);
		}
	}
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	st_dev.is_single_tap = (st_dev.is_single_aod || st_dev.is_single_fp) ? 5 : 0;
	 if (st_dev.is_single_tap || st_dev.is_double_tap)
		open_guesture_function(st_dev.stc.custom_function_en);
	else
		close_guesture_function(st_dev.stc.custom_function_en);	
#else
	 if (st_dev.is_double_tap)
		open_guesture_function(st_dev.stc.custom_function_en);
	else
		close_guesture_function(st_dev.stc.custom_function_en);
#endif
	st_dev.suspended = false;
	semi_touch_clear_report();
	kernel_log_d("tpd_resume...\r\n");

	return SEMI_DRV_ERR_OK;
}

static struct i2c_driver sm_touch_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "semi_touch",
		   .of_match_table = of_match_ptr(sm_of_match),
#if CONFIG_PM
		   .pm = &semi_touch_dev_pm_ops,
#endif
		   },
	.id_table = sm_ts_id,
	.probe = semi_touch_probe,
	.remove = semi_touch_remove,
};

int  semi_i2c_device_init(void)
{
	int ret = 0;

	if (get_tp_chip_id() == 0) {
		if ((tpd_cdev->tp_chip_id != TS_CHIP_MAX) && (tpd_cdev->tp_chip_id != TS_CHIP_SEMI)) {
			kernel_log_d("this tp is not used,return.\n");
			return -EPERM;
		}
	}
	if (tpd_cdev->TP_have_registered) {
		kernel_log_d("TP have registered by other TP.\n");
		return -EPERM;
	}
	ret = i2c_add_driver(&sm_touch_driver);
	check_return_if_fail(ret, NULL);

	return ret;
}

void  semi_i2c_device_exit(void)
{
	i2c_del_driver(&sm_touch_driver);
}

