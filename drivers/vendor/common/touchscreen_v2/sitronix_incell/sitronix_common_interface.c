/************************************************************************
*
* File Name: sitronix_common_interface.c
*
*  *   Version: v1.0
*
************************************************************************/

#include "sitronix_ts.h"
#include <linux/power_supply.h>
#include "sitronix_st7123.h"

#define MAX_FILE_NAME_LEN       64
#define MAX_FILE_PATH_LEN  64
#define MAX_NAME_LEN_20  20

char sitronix_vendor_name[MAX_NAME_LEN_20] = { 0 };
char sitronix_firmware_name[MAX_FILE_NAME_LEN] = {0};
int sitronix_vendor_id = 0;
int sitronix_tptest_result = 0;
const struct ts_firmware *adb_upgrade_firmware = NULL;
extern int sitronix_ts_suspend(struct device *dev);
extern int sitronix_ts_resume(struct device *dev);

struct tpvendor_t sitronix_vendor_l[] = {
	{STP_VENDOR_ID_0, STP_VENDOR_0_NAME},
	{STP_VENDOR_ID_1, STP_VENDOR_1_NAME},
	{STP_VENDOR_ID_2, STP_VENDOR_2_NAME},
	{STP_VENDOR_ID_3, STP_VENDOR_3_NAME},
	{VENDOR_END, "Unknown"},
};

int sitronix_get_fw(void)
{
	int i = 0;
	int ret = 0;

	for (i = 0; i < ARRAY_SIZE(sitronix_vendor_l); i++) {
		if (strnstr(lcd_name, sitronix_vendor_l[i].vendor_name, strlen(lcd_name))) {
			sitronix_vendor_id = sitronix_vendor_l[i].vendor_id;
			strlcpy(sitronix_vendor_name, sitronix_vendor_l[i].vendor_name,
				sizeof(sitronix_vendor_name));
			ret = 0;
			goto out;
		}
	}
	strlcpy(sitronix_vendor_name, "Unknown", sizeof(sitronix_vendor_name));
	ret = -EIO;
out:
	snprintf(sitronix_firmware_name, sizeof(sitronix_firmware_name),
			"sitronix_firmware_%s.dump", sitronix_vendor_name);
	return ret;
}

static int sitronix_tp_fw_upgrade(struct ztp_device *cdev, char *fw_name, int fwname_len)
{
	int ret = 0;
	return ret;
}

static int tpd_init_tpinfo(struct ztp_device *cdev)
{
	struct sitronix_ts_data *ts = gts;

	mutex_lock(&gts->mutex);
	sitronix_ts_get_device_info(gts);
	mutex_unlock(&gts->mutex);
	strlcpy(cdev->ic_tpinfo.tp_name, "sitronix_ts", sizeof(cdev->ic_tpinfo.tp_name));
	strlcpy(cdev->ic_tpinfo.vendor_name, sitronix_vendor_name, sizeof(cdev->ic_tpinfo.vendor_name));
	cdev->ic_tpinfo.chip_model_id = TS_CHIP_SITRONIX;
	cdev->ic_tpinfo.firmware_ver = ts->ts_dev_info.fw_version;
	cdev->ic_tpinfo.module_id = sitronix_vendor_id;
	cdev->ic_tpinfo.i2c_type = 1;
	return 0;
}

static int sitronix_tp_suspend_show(struct ztp_device *cdev)
{
	return cdev->tp_suspend;
}

static int sitronix_set_tp_suspend(struct ztp_device *cdev, u8 suspend_node, int enable)
{
	if (enable) {
		change_tp_state(LCD_OFF);
	} else {
		change_tp_state(LCD_ON);
	}
	cdev->tp_suspend = enable;
	return cdev->tp_suspend;
}

static int sitronix_tp_resume(void *dev)
{
	struct sitronix_ts_data *ts = gts;

	stmsg("%s enter", __func__);

	return sitronix_ts_resume(&ts->pdev->dev);
}

static int sitronix_tp_suspend(void *dev)
{
	struct sitronix_ts_data *ts = gts;

	stmsg("%s enter", __func__);

	return sitronix_ts_suspend(&ts->pdev->dev);
}

static int tpd_test_cmd_store(struct ztp_device *cdev)
{
	stmsg("%s:enter, useless\n", __func__);
	return 0;
}

static int tpd_test_cmd_show(struct ztp_device *cdev, char *buf)
{
	ssize_t num_read_chars = 0;
	int i_len = 0;

	stmsg("%s", __func__);
	i_len = snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d", sitronix_tptest_result, gts->ts_dev_info.x_chs, gts->ts_dev_info.y_chs, 0);
	stmsg("tpd  test:%s.\n", buf);

	num_read_chars = i_len;
	return num_read_chars;
}

static bool sitronix_get_charger_status(void)
{
	static struct power_supply *batt_psy;
	union power_supply_propval val = { 0, };
	bool status = false;

	if (batt_psy == NULL)
		batt_psy = power_supply_get_by_name("battery");
	if (batt_psy) {
		batt_psy->desc->get_property(batt_psy, POWER_SUPPLY_PROP_STATUS, &val);
	}
	if ((val.intval == POWER_SUPPLY_STATUS_CHARGING) ||
		(val.intval == POWER_SUPPLY_STATUS_FULL)) {
		status = true;
	} else {
		status = false;
	}
	stmsg("charger status:%d", status);
	return status;
}

static void sitronix_work_charger_detect_work(struct work_struct *work)
{
	bool charger_mode_old = gts->charger_mode;

	gts->charger_mode = sitronix_get_charger_status();
	if (!gts->in_suspend  && (gts->charger_mode != charger_mode_old)) {
		stmsg("write charger mode:%d", gts->charger_mode);
		if (gts->charger_mode)
			sitronix_mode_switch(ST_MODE_CHARGE, true);
		else
			sitronix_mode_switch(ST_MODE_CHARGE, false);
	}
}

static int sitronix_charger_notify_call(struct notifier_block *nb, unsigned long event, void *data)
{
	struct power_supply *psy = data;

	if (event != PSY_EVENT_PROP_CHANGED) {
		return NOTIFY_DONE;
	}

	if ((strcmp(psy->desc->name, "usb") == 0)
	    || (strcmp(psy->desc->name, "ac") == 0)) {
		queue_delayed_work(gts->charger_workqueue, &gts->charger_work, msecs_to_jiffies(500));
	}

	return NOTIFY_DONE;
}

static int sitronix_init_charger_notifier(void)
{
	int ret = 0;

	stmsg("Init Charger notifier");

	gts->charger_notifier.notifier_call = sitronix_charger_notify_call;
	ret = power_supply_reg_notifier(&gts->charger_notifier);
	return ret;
}

static int sitronix_headset_state_show(struct ztp_device *cdev)
{
	return cdev->headset_state;
}

static int sitronix_set_headset_state(struct ztp_device *cdev, int enable)
{
	cdev->headset_state = enable;
	stmsg("%s: headset_state = %d.\n", __func__, cdev->headset_state);
	if (!gts->in_suspend) {
		if (enable)
			sitronix_mode_switch(ST_MODE_HEADPHONE, true);
		else
			sitronix_mode_switch(ST_MODE_HEADPHONE, false);
	}
	return cdev->headset_state;
}

static int sitronix_set_display_rotation(struct ztp_device *cdev, int mrotation)
{
	cdev->display_rotation = mrotation;
	if (gts->in_suspend)
		return 0;
	stmsg("%s: display_rotation = %d.\n", __func__, cdev->display_rotation);
	switch (cdev->display_rotation) {
		case mRotatin_0:
			sitronix_mode_switch_value(ST_MODE_GRIP, true, ST_MODE_GRIP_ROTATE_0);
			break;
		case mRotatin_90:
			sitronix_mode_switch_value(ST_MODE_GRIP, true, ST_MODE_GRIP_ROTATE_90);
			break;
		case mRotatin_180:
			sitronix_mode_switch_value(ST_MODE_GRIP, true, ST_MODE_GRIP_ROTATE_180);
			break;
		case mRotatin_270:
			sitronix_mode_switch_value(ST_MODE_GRIP, true, ST_MODE_GRIP_ROTATE_270);
			break;
		default:
			break;
	}
	return cdev->display_rotation;
}

int sitronix_register_fw_class(void)
{
	struct sitronix_ts_data *ts = gts;

	sitronix_get_fw();
	tpd_cdev->get_tpinfo = tpd_init_tpinfo;
	tpd_cdev->tp_fw_upgrade = sitronix_tp_fw_upgrade;
	tpd_cdev->tp_suspend_show = sitronix_tp_suspend_show;
	tpd_cdev->set_tp_suspend = sitronix_set_tp_suspend;

	tpd_cdev->tp_data = ts;
	tpd_cdev->tp_resume_func = sitronix_tp_resume;
	tpd_cdev->tp_suspend_func = sitronix_tp_suspend;

	tpd_cdev->tp_self_test = tpd_test_cmd_store;
	tpd_cdev->get_tp_self_test_result = tpd_test_cmd_show;
	tpd_cdev->set_display_rotation = sitronix_set_display_rotation;
	tpd_cdev->headset_state_show = sitronix_headset_state_show;
	tpd_cdev->set_headset_state = sitronix_set_headset_state;

	tpd_cdev->max_x = ts->ts_dev_info.x_res;
	tpd_cdev->max_y = ts->ts_dev_info.y_res;

	gts->charger_workqueue = create_singlethread_workqueue("sitronix_ts_charger_workqueue");
	if (!gts->charger_workqueue) {
		sterr(" allocate gts->charger_workqueue failed\n");
	} else  {
		gts->charger_mode = false;
		INIT_DELAYED_WORK(&gts->charger_work, sitronix_work_charger_detect_work);
		queue_delayed_work(gts->charger_workqueue, &gts->charger_work, msecs_to_jiffies(1000));
		sitronix_init_charger_notifier();
	}
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	zlog_tp_dev.device_name = sitronix_vendor_name;
	zlog_tp_dev.ic_name = "sitronix_tp";
	TPD_ZLOG("device_name:%s, ic_name: %s.", zlog_tp_dev.device_name, zlog_tp_dev.ic_name);
#endif
	return 0;
}


