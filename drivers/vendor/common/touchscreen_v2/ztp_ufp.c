#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "ztp_common.h"

#define SINGLE_TAP_DELAY	600

#ifdef ZTE_ONE_KEY
#define MAX_POINTS_SUPPORT 10
#define FP_GESTURE_DOWN	"fp_gesture_down=true"
#define FP_GESTURE_UP	"fp_gesture_up=true"

static char *one_key_finger_id[] = {
	"finger_id=0",
	"finger_id=1",
	"finger_id=2",
	"finger_id=3",
	"finger_id=4",
	"finger_id=5",
	"finger_id=6",
	"finger_id=7",
	"finger_id=8",
	"finger_id=9",
};
#endif

static char *tppower_to_str[] = {
	"TP_POWER_STATUS=2",		/* TP_POWER_ON */
	"TP_POWER_STATUS=1",		/* TP_POWER_OFF */
	"TP_POWER_STATUS=3",		/* TP_POWER_AOD */
};

struct ufp_ops ufp_tp_ops;

extern atomic_t current_lcd_state;

int ufp_get_lcdstate(void)
{
	return atomic_read(&current_lcd_state);
}

void ufp_report_gesture_uevent(char *str)
{
	char *envp[2];

	envp[0] = str;
	envp[1] = NULL;
	kobject_uevent_env(&(ufp_tp_ops.uevent_pdev->dev.kobj), KOBJ_CHANGE, envp);

	UFP_INFO("%s", str);
}

static inline void __report_ufp_uevent(char *str)
{
	char *envp[3];

	if (!ufp_tp_ops.uevent_pdev) {
		UFP_ERR("uevent pdev is null!\n");
		return;
	}

	if (!strcmp(str, AOD_AREAMEET_DOWN))
		ufp_report_gesture_uevent(SINGLE_TAP_GESTURE);

	envp[0] = str;
	envp[1] = tppower_to_str[atomic_read(&current_lcd_state)];
	envp[2] = NULL;
	kobject_uevent_env(&(ufp_tp_ops.uevent_pdev->dev.kobj), KOBJ_CHANGE, envp);
	UFP_INFO("%s", str);
}

void report_ufp_uevent(int enable)
{
	static int area_meet_down = 0;

	if (enable && !area_meet_down) {
		area_meet_down = 1;
		if (atomic_read(&current_lcd_state) == SCREEN_ON) {/* fp func enable is guaranted by user*/
			__report_ufp_uevent(AREAMEET_DOWN);
		 } else {
			__report_ufp_uevent(AOD_AREAMEET_DOWN);
			ufp_tp_ops.aod_fp_down = true;
		}
	} else if (!enable && area_meet_down) {
			area_meet_down = 0;
			__report_ufp_uevent(AREAMEET_UP);
			if (ufp_tp_ops.aod_fp_down && ufp_tp_ops.wait_completion) {
				complete(&ufp_tp_ops.ufp_completion);
			}
			ufp_tp_ops.aod_fp_down = false;
	}
}

static inline int zte_in_zeon(int x, int y)
{
	int ret = 0;
	struct ztp_device *cdev = tpd_cdev;

	if ((cdev->ufp_circle_center_x - cdev->ufp_circle_radius < x) &&
		(cdev->ufp_circle_center_x  + cdev->ufp_circle_radius > x) &&
		(cdev->ufp_circle_center_y  - cdev->ufp_circle_radius < y) &&
		(cdev->ufp_circle_center_y + cdev->ufp_circle_radius > y)) {
		ret = 1;
	}

	return ret;
}

#ifdef ZTE_ONE_KEY
static inline void report_one_key_uevent(char *str, int i)
{
	char *envp[3];

	envp[0] = str;
	envp[1] = one_key_finger_id[i];
	envp[2] = NULL;
	kobject_uevent_env(&(ufp_tp_ops.uevent_pdev->dev.kobj), KOBJ_CHANGE, envp);
	UFP_INFO("%s", str);
}

/* We only track the first finger in zeon */
void one_key_report(int is_down, int x, int y, int finger_id)
{
	int retval;
	static char one_key_finger[MAX_POINTS_SUPPORT] = {0};
	static int one_key_down = 0;

	if (is_down) {
		retval = zte_in_zeon(x, y);
		if (retval && !one_key_finger[finger_id] && !one_key_down) {
			one_key_finger[finger_id] = 1;
			one_key_down = 1;
			report_one_key_uevent(FP_GESTURE_DOWN, finger_id);
		}
	} else if (one_key_finger[finger_id]) {
			one_key_finger[finger_id] = 0;
			one_key_down = 0;
			report_one_key_uevent(FP_GESTURE_UP, finger_id);
	}
}
#endif

#ifdef CONFIG_TOUCHSCREEN_POINT_SIMULAT_UF
/* We only track the first finger in zeon */
void uf_touch_report(int x, int y, int finger_id)
{
	int retval;
	static int fp_finger[MAX_POINTS_SUPPORT] = { 0 };
	static int area_meet_down = 0;

	retval = zte_in_zeon(x, y);
	if (retval) {
		if (!fp_finger[finger_id] && !area_meet_down) {
			fp_finger[finger_id] = 1;
			area_meet_down = 1;
			__report_ufp_uevent(AREAMEET_DOWN);
		}
	} else if (fp_finger[finger_id]) {
			fp_finger[finger_id] = 0;
			area_meet_down = 0;
			__report_ufp_uevent(AREAMEET_UP);
	}
}
#endif

static inline void report_lcd_uevent(struct kobject *kobj, char **envp)
{
	int retval;

	envp[0] = "aod=true";
	envp[1] = NULL;
	retval = kobject_uevent_env(kobj, KOBJ_CHANGE, envp);
	if (retval != 0)
		UFP_ERR("lcd state uevent send failed!\n");
}

void ufp_report_lcd_state(void)
{
	char *envp[2];

	if (!ufp_tp_ops.uevent_pdev) {
		UFP_ERR("uevent pdev is null!\n");
		return;
	}

	report_lcd_uevent(&(ufp_tp_ops.uevent_pdev->dev.kobj), envp);
}
EXPORT_SYMBOL(ufp_report_lcd_state);

/*for lcd low power mode*/
int ufp_notifier_cb(int in_lp)
{
	int retval = 0;

	UFP_INFO("in lp %d!\n", in_lp);

	if (in_lp)
		change_tp_state(ENTER_LP);
	else
		change_tp_state(EXIT_LP);

	return retval;
}
EXPORT_SYMBOL(ufp_notifier_cb);

int ufp_mac_init(void)
{

	if (tpd_cdev->zte_touch_pdev)
		ufp_tp_ops.uevent_pdev = tpd_cdev->zte_touch_pdev;
	init_completion(&ufp_tp_ops.ufp_completion);
	ufp_tp_ops.aod_fp_down = false;
	ufp_tp_ops.wait_completion = false;
	return 0;
}

void  ufp_mac_exit(void)
{

	ufp_tp_ops.uevent_pdev = NULL;
}

