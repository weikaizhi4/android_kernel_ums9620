/************************************************************************
*
* File Name: gcore_common_interface.c
*
*  *   Version: v1.0
*
************************************************************************/
#include "gcore_drv_common.h"
#include <linux/gpio.h>
#define MAX_FILE_NAME_LEN       64
#define MAX_FILE_PATH_LEN  64
#define MAX_NAME_LEN_20  20

char gcore_vendor_name[MAX_NAME_LEN_20] = { 0 };
char gcore_firmware_name[MAX_FILE_NAME_LEN] = {0};
char gcore_mp_firmware_name[MAX_FILE_NAME_LEN] = {0};
char gcore_mp_test_ini_path[MAX_FILE_NAME_LEN] = {0};
#ifdef GCORE_DEFAULT_FIRMWARE
char gcore_default_firmware_name[MAX_FILE_NAME_LEN] = {0};
#endif
int gcore_vendor_id = 0;
int gcore_tptest_result = 0;
struct ts_firmware *gcore_adb_upgrade_firmware = NULL;

extern void gcore_suspend(void);
extern void gcore_resume(void);
struct tpvendor_t gcore_vendor_l[] = {
	{GTP_VENDOR_ID_0, GTP_VENDOR_0_NAME},
	{GTP_VENDOR_ID_1, GTP_VENDOR_1_NAME},
	{GTP_VENDOR_ID_2, GTP_VENDOR_2_NAME},
	{GTP_VENDOR_ID_3, GTP_VENDOR_3_NAME},
	{VENDOR_END, "Unknown"},
};

int gcore_get_fw(void)
{
	int i = 0;
	int ret = 0;

	for (i = 0; i < ARRAY_SIZE(gcore_vendor_l); i++) {
		if (strnstr(lcd_name, gcore_vendor_l[i].vendor_name, strlen(lcd_name))) {
			gcore_vendor_id = gcore_vendor_l[i].vendor_id;
			strlcpy(gcore_vendor_name, gcore_vendor_l[i].vendor_name,
				sizeof(gcore_vendor_name));
			ret = 0;
			goto out;
		}
	}
	strlcpy(gcore_vendor_name, "Unknown", sizeof(gcore_vendor_name));
	ret = -EIO;
out:
	snprintf(gcore_firmware_name, sizeof(gcore_firmware_name),
			"gcore_firmware_%s.bin", gcore_vendor_name);
	snprintf(gcore_mp_firmware_name, sizeof(gcore_mp_firmware_name),
			"gcore_mp_firmware_%s.bin", gcore_vendor_name);
	snprintf(gcore_mp_test_ini_path, sizeof(gcore_mp_test_ini_path),
			"gcore_mp_test_%s.ini", gcore_vendor_name);
#ifdef GCORE_DEFAULT_FIRMWARE
	snprintf( gcore_default_firmware_name, sizeof(gcore_default_firmware_name),
			"%s_%s.bin", GCORE_DEFAULT_FIRMWARE, gcore_vendor_name);
#endif
	return ret;
}

int gcore_tp_requeset_firmware(void)
{
	struct ztp_device *cdev = tpd_cdev;


	if (cdev->tp_firmware == NULL || !cdev->tp_firmware->size) {
		GTP_ERROR("cdev->tp_firmware is NULL");
		goto err_free_firmware;
	}
		
	if (gcore_adb_upgrade_firmware)
		goto copy_firmware_data;
	gcore_adb_upgrade_firmware = kzalloc(sizeof(struct ts_firmware), GFP_KERNEL);
	if (gcore_adb_upgrade_firmware == NULL) {
		GTP_ERROR("Request firmware alloc ts_firmware failed");
		return -ENOMEM;
	}

	gcore_adb_upgrade_firmware->size = cdev->tp_firmware->size;
	gcore_adb_upgrade_firmware->data = vmalloc(gcore_adb_upgrade_firmware->size);
	if (gcore_adb_upgrade_firmware->data == NULL) {
		GTP_ERROR("Request form file alloc firmware data failed");
		goto err_free_firmware;
	}
copy_firmware_data:
	memcpy(gcore_adb_upgrade_firmware->data, (u8 *)cdev->tp_firmware->data, gcore_adb_upgrade_firmware->size);
	return 0;
err_free_firmware:
	kfree(gcore_adb_upgrade_firmware);
	gcore_adb_upgrade_firmware = NULL;
	return -ENOMEM;
}

static int gcore_tp_fw_upgrade(struct ztp_device *cdev, char *fw_name, int fwname_len)
{
	int ret = 0;

	if (gcore_tp_requeset_firmware() < 0) {
		GTP_ERROR("Request from file '%s' failed");
		goto error_fw_upgrade;
	}
	gcore_request_firmware_update_work(NULL);
	return ret;
error_fw_upgrade:
	return -EIO;
}

static int tpd_init_tpinfo(struct ztp_device *cdev)
{
	u8 read_data[4] = { 0 };

	gcore_read_fw_version_retry(read_data, sizeof(read_data));
	strlcpy(cdev->ic_tpinfo.tp_name, "gcore_ts", sizeof(cdev->ic_tpinfo.tp_name));
	strlcpy(cdev->ic_tpinfo.vendor_name, gcore_vendor_name, sizeof(cdev->ic_tpinfo.vendor_name));
	cdev->ic_tpinfo.chip_model_id = TS_CHIP_GCORE;
	cdev->ic_tpinfo.firmware_ver = read_data[1] << 24 | read_data[0] << 16 | read_data[3] << 8 | read_data[2];
	cdev->ic_tpinfo.module_id = gcore_vendor_id;
	cdev->ic_tpinfo.i2c_type = 1;
	cdev->ic_tpinfo.i2c_addr = 0x26;
	return 0;
}

static int gcore_tp_suspend_show(struct ztp_device *cdev)
{
	struct gcore_dev *gdev = fn_data.gdev;

	cdev->tp_suspend = gdev->tp_suspend;
	return cdev->tp_suspend;
}

static int gcore_set_tp_suspend(struct ztp_device *cdev, u8 suspend_node, int enable)
{
	if (enable) {
		gcore_suspend();
	} else {
		gcore_resume();
	}
	return 0;
}

static int tpd_test_cmd_store(struct ztp_device *cdev)
{
	int ret = 0;
	int retry = 0;

	GTP_INFO("%s:enter, useless\n", __func__);
	do {
		gcore_tptest_result = 0;
		ret = gcore_start_mp_test();
		if (ret) {
			retry++;
			GTP_ERROR("rawdata test failed, retry:%d", retry);
			msleep(20);
		} else {
			break;
		}
	} while (retry < 3);
	if (retry == 3) {
		GTP_ERROR("selftest failed!");
	} else {
		GTP_INFO("selftest success!");
	}
	return 0;
}

static int tpd_test_cmd_show(struct ztp_device *cdev, char *buf)
{
	ssize_t num_read_chars = 0;
	int i_len = 0;

	GTP_INFO("%s", __func__);
	i_len = snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d", gcore_tptest_result, RAWDATA_COLUMN, RAWDATA_ROW, 0);
	GTP_INFO("tpd  test:%s.\n", buf);

	num_read_chars = i_len;
	return num_read_chars;
}

static int gcore_set_display_rotation(struct ztp_device *cdev, int mrotation)
{
	struct gcore_dev *gdev = fn_data.gdev;

	gdev->display_rotation = mrotation;
	if (gdev->tp_suspend)
		return 0;
	GTP_INFO("%s: display_rotation = %d.\n", __func__, gdev->display_rotation);
	switch (gdev->display_rotation) {
	case mRotatin_0:
		gcore_fw_event_notify(FW_EDGE_0);
		break;
	case mRotatin_90:
		gcore_fw_event_notify(FW_EDGE_90);
		break;
	case mRotatin_180:
		gcore_fw_event_notify(FW_EDGE_0);
		break;
	case mRotatin_270:
		gcore_fw_event_notify(FW_EDGE_90);
		break;
	default:
		break;
	};

	return gdev->display_rotation;
}


static int gcore_headset_state_show(struct ztp_device *cdev)
{
	struct gcore_dev *gdev = fn_data.gdev;

	cdev->headset_state = gdev->headset_mode;
	return cdev->headset_state;
}

static int gcore_set_headset_state(struct ztp_device *cdev, int enable)
{
	struct gcore_dev *gdev = fn_data.gdev;

	gdev->headset_mode = enable;
	GTP_INFO("%s: headset_state = %d.\n", __func__, gdev->headset_mode);
	if (!gdev->tp_suspend) {
		if (gdev->headset_mode)
			gcore_fw_event_notify(FW_HEADSET_PLUG);
		else
			gcore_fw_event_notify(FW_HEADSET_UNPLUG);
	}
	return gdev->headset_mode;
}

int gcore_ex_mode_recovery(struct ztp_device *cdev)
{
	struct gcore_dev *gdev = fn_data.gdev;

	if (gdev->charger_mode) {
		gcore_fw_event_save(FW_CHARGER_PLUG);
	} else {
		gcore_fw_event_save(FW_CHARGER_UNPLUG);
	}
	if (gdev->headset_mode) {
		gcore_fw_event_save(FW_HEADSET_PLUG);
	} else {
		gcore_fw_event_save(FW_HEADSET_UNPLUG);
	}
	gcore_fw_event_resume();
	return 0;
}

static int tpd_get_wakegesture(struct ztp_device *cdev)
{
	struct gcore_dev *gdev = fn_data.gdev;

	cdev->b_gesture_enable = gdev->gesture_wakeup_en;
	return 0;
}

static int tpd_enable_wakegesture(struct ztp_device *cdev, int enable)
{
	struct gcore_dev *gdev = fn_data.gdev;

	if (gdev->tp_suspend) {
		cdev->tp_suspend_write_gesture = true;
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		tpd_zlog_record_notify(TP_SUSPEND_GESTURE_OPEN_NO);
#endif
	}
	gdev->gesture_wakeup_en = enable;
	return enable;
}

static bool tpd_suspend_need_awake(struct ztp_device *cdev)
{
	struct gcore_dev *gdev = fn_data.gdev;

	if ((!cdev->tp_suspend_write_gesture && gdev->gesture_wakeup_en) || gdev->tp_fw_update) {
		GTP_INFO("tp suspend need awake.\n");
		return true;
	}
	GTP_INFO("tp suspend dont need awake.\n");
	return false;
}

#ifdef GTP_LCD_FPS_NOTIFY
int gcore_lcd_fps_notify(struct ztp_device *cdev, u8 lcd_fps)
{
	int ret = 0;

	if (lcd_fps == 60) {
		ret = gcore_fw_event_notify(FW_REPORT_RATE_120);
		GTP_INFO("LCD fps 60HZ");
	} else if (lcd_fps == 90) {
		ret = gcore_fw_event_notify(FW_REPORT_RATE_180);
		GTP_INFO("LCD fps 90HZ");
	} else {
		GTP_INFO("not support LCD fps");
	}
	return ret;
}
#endif

int gcore_ts_resume(void *data)
{
	gcore_resume();
	return 0;
}

int gcore_ts_suspend(void *data)
{
	gcore_suspend();
	return 0;
}

static int gcore_charger_state_notify(struct ztp_device *cdev)
{
	struct gcore_dev *gdev = fn_data.gdev;
	bool charger_mode_old = gdev->charger_mode;

	gdev->charger_mode = cdev->charger_mode;
	if (!gdev->tp_suspend && (gdev->charger_mode != charger_mode_old)) {
		if (gdev->charger_mode) {
			GTP_INFO("charger in");
			gcore_fw_event_notify(FW_CHARGER_PLUG);
		} else {
			GTP_INFO("charger out");
			gcore_fw_event_notify(FW_CHARGER_UNPLUG);
		}

	}
	return 0;
}

static int tpd_gtp_shutdown(struct ztp_device *cdev)
{
	struct gcore_dev *gdev = fn_data.gdev;

	gcore_suspend();
	gpio_direction_output(gdev->rst_gpio, 0);
	return 0;
}

int gcore_register_fw_class(void)
{
	gcore_get_fw();
	tpd_cdev->get_tpinfo = tpd_init_tpinfo;
	tpd_cdev->tp_fw_upgrade = gcore_tp_fw_upgrade;
	tpd_cdev->tp_suspend_show = gcore_tp_suspend_show;
	tpd_cdev->set_tp_suspend = gcore_set_tp_suspend;
	tpd_cdev->headset_state_show = gcore_headset_state_show;
	tpd_cdev->set_headset_state = gcore_set_headset_state;
	tpd_cdev->set_display_rotation = gcore_set_display_rotation;
	tpd_cdev->get_gesture = tpd_get_wakegesture;
	tpd_cdev->wake_gesture = tpd_enable_wakegesture;
	tpd_cdev->tpd_send_cmd = gcore_ex_mode_recovery;
	tpd_cdev->charger_state_notify = gcore_charger_state_notify;
	queue_delayed_work(tpd_cdev->tpd_wq, &tpd_cdev->charger_work, msecs_to_jiffies(5000));
#ifdef GTP_LCD_FPS_NOTIFY
	tpd_cdev->lcd_fps_notify = gcore_lcd_fps_notify;
#endif
	tpd_cdev->tp_data = fn_data.gdev;;
	tpd_cdev->tp_resume_func = gcore_ts_resume;
	tpd_cdev->tp_suspend_func = gcore_ts_suspend;
	tpd_cdev->tpd_suspend_need_awake = tpd_suspend_need_awake;
	tpd_cdev->tp_self_test = tpd_test_cmd_store;
	tpd_cdev->get_tp_self_test_result = tpd_test_cmd_show;
	tpd_cdev->tpd_shutdown = tpd_gtp_shutdown;
	tpd_cdev->max_x = TOUCH_SCREEN_X_MAX;
	tpd_cdev->max_y = TOUCH_SCREEN_Y_MAX;
	tpd_cdev->tp_resume_before_lcd_cmd = true;

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	zlog_tp_dev.device_name = gcore_vendor_name;
	zlog_tp_dev.ic_name = "gcore_tp";
	TPD_ZLOG("device_name:%s, ic_name: %s.", zlog_tp_dev.device_name, zlog_tp_dev.ic_name);
#endif
	return 0;
}

