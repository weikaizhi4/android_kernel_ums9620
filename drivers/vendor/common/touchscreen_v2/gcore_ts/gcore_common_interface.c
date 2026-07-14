/************************************************************************
*
* File Name: gcore_common_interface.c
*
*  *   Version: v1.0
*
************************************************************************/
#include "gcore_drv_common.h"
#include <linux/power_supply.h>
#define MAX_FILE_NAME_LEN       64
#define MAX_FILE_PATH_LEN  64
#define MAX_NAME_LEN_20  20

char gcore_vendor_name[MAX_NAME_LEN_20] = { 0 };
char gcore_firmware_name[MAX_FILE_NAME_LEN] = {0};
char gcore_mp_firmware_name[MAX_FILE_NAME_LEN] = {0};
char gcore_mp_test_ini_path[MAX_FILE_NAME_LEN] = {0};
int gcore_vendor_id = 0;
int gcore_tptest_result = 0;
extern int gcore_flashdownload_fspi_proc(void *fw_buf);
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
	return ret;
}

static int gcore_tp_fw_upgrade(struct ztp_device *cdev, char *fw_name, int fwname_len)
{
	struct gcore_dev *gdev = fn_data.gdev;

	if (cdev->tp_firmware == NULL || cdev->tp_firmware->data == NULL) {
		GTP_ERROR("cdev->tp_firmware is NULL");
		return -EIO;
	}

	gdev->tp_fw_update = true;
#if defined(CONFIG_ENABLE_CHIP_TYPE_GC7202)
	if (gcore_flashdownload_fspi_proc((u8 *)cdev->tp_firmware->data)) {
		GTP_ERROR("flashdownload fspi proc fail");
		goto fw_upgrade_fail;
	}
#endif
	gdev->tp_fw_update = false;
	return 0;
fw_upgrade_fail:
	gdev->tp_fw_update = false;
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
		if (suspend_node == PROC_SUSPEND_NODE)
			gcore_idm_close_tp();
		else
			gcore_suspend();
	} else {
		gcore_resume();
	}
	return 0;
}

#ifdef GTP_GET_NOISE
static int gcore_print_data2buffer(char *buff_arry[], unsigned int cols, unsigned int rows,
		 s16 *frame_data_words, int idex, int idx, char *name)
{
	int count = 0;
	unsigned int x = 0;
	unsigned int y = 0;

	count += snprintf(buff_arry[idex] + count, RT_DATA_LEN - count,
				"\n%s image[%d]:\n", name, idx);
	for (y = 0; y < rows; y++) {
		count += snprintf(buff_arry[idex] + count, RT_DATA_LEN - count, "[%2d]", (y + 1));
		for (x = 0; x < cols; x++) {
			count += snprintf(buff_arry[idex] + count, RT_DATA_LEN - count,
						"%5d,", frame_data_words[y * cols + x]);
		}
		count += snprintf(buff_arry[idex] + count, RT_DATA_LEN - count, "\n");
	}
	return count;
}

static int gcore_data_request(s16 *frame_data_words, enum tp_test_type  test_type)
{
	uint8_t *info_data = NULL;
	int index = 0, ret = 0, i = 0;
	int total_size = 0;
	unsigned int x = 0;
	unsigned int y = 0;
	unsigned int col = 0;
	unsigned int row = 0;

	row = RAWDATA_ROW;
	col = RAWDATA_COLUMN;

	total_size = (RAWDATA_ROW * RAWDATA_COLUMN) * 2;
	info_data = kzalloc((total_size * sizeof(uint8_t)), GFP_KERNEL);
	if (info_data == NULL) {
		ret = -ENOMEM;
		goto sub_end;
	}

	memset(info_data, 0, total_size * sizeof(uint8_t));
	switch (test_type) {
	case RAWDATA_TEST:
		ret = gcore_fw_read_rawdata(info_data, total_size);
		break;
	case DELTA_TEST:
		ret = gcore_fw_read_diffdata(info_data, total_size);
		break;
	default:
		GTP_ERROR("%s:the Para is error!\n", __func__);
		ret = -1;
		goto release_mem;
	}
	if (ret >= 0) {
		for (i = 0, index = 0; index < total_size/2; i += 2, index++) {
			frame_data_words[index] = ((info_data[i + 1] << 8) | info_data[i]);
		}
	}
	for (y = 0; y < row; y++) {
		pr_cont("GTP[%2d]", (y + 1));
		for (x = 0; x < col; x++) {
			pr_cont("%5d,", frame_data_words[y * col + x]);
		}
		pr_cont("\n");
	}
release_mem:
	if (info_data != NULL)
		kfree(info_data);
sub_end:
	return ret;
}

static int  gcore_testing_delta_raw_report(char *buff_arry[], unsigned int num_of_reports)
{

	s16 *frame_data_words = NULL;
	unsigned int col = 0;
	unsigned int row = 0;
	unsigned int idx = 0, idex = 0;
	int retval = 0;

	row = RAWDATA_ROW;
	col = RAWDATA_COLUMN;
	GTP_INFO("get tp delta raw data startt!\n");
	frame_data_words = kcalloc((row * col), sizeof(s16), GFP_KERNEL);
	if (frame_data_words ==  NULL) {
		GTP_ERROR("Failed to allocate frame_data_words mem\n");
		retval = -1;
		goto MEM_ALLOC_FAILED;
	}
	for (idx = 0; idx < num_of_reports; idx++) {
		retval = gcore_data_request(frame_data_words, RAWDATA_TEST);
		if (retval < 0) {
			GTP_ERROR("data_request failed!\n");
			goto DATA_REQUEST_FAILED;
		}

		idex = idx << 1;
		retval = gcore_print_data2buffer(buff_arry, col, row,  frame_data_words, idex, idx, "Rawdata");
		if (retval <= 0) {
			GTP_ERROR("print_data2buffer rawdata failed!\n");
			goto DATA_REQUEST_FAILED;
		}
		retval = gcore_data_request(frame_data_words, DELTA_TEST);
		if (retval < 0) {
			GTP_ERROR("data_request failed!\n");
			goto DATA_REQUEST_FAILED;
		}
		idex += 1;
		retval = gcore_print_data2buffer(buff_arry, col, row,  frame_data_words, idex, idx, "Delta");
		if (retval <= 0) {
			GTP_ERROR("print_data2buffer Delta failed!\n");
			goto DATA_REQUEST_FAILED;
		}
	}

	retval = 0;
	msleep(20);
	GTP_INFO("get tp delta raw data end!\n");
DATA_REQUEST_FAILED:
	kfree(frame_data_words);
	frame_data_words = NULL;
MEM_ALLOC_FAILED:
	return retval;
}

static int gcore_tpd_get_noise(struct ztp_device *cdev, struct list_head *head)
{
	int retval;
	int i = 0;
	char *buf_arry[RT_DATA_NUM];
	struct tp_runtime_data *tp_rt;
	struct gcore_dev *gdev = fn_data.gdev;

	if (gdev->tp_suspend)
		return -EIO;

	list_for_each_entry(tp_rt, head, list) {
		buf_arry[i++] = tp_rt->rt_data;
		tp_rt->is_empty = false;
	}
	retval = gcore_testing_delta_raw_report(buf_arry, RT_DATA_NUM >> 2);
	if (retval < 0) {
		GTP_ERROR("%s: get_raw_noise failed!\n",  __func__);
		return retval;
	}
	return 0;
}
#endif

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

void gcore_ex_mode_recovery(void)
{
	struct gcore_dev *gdev = fn_data.gdev;

	if (gdev->charger_mode) {
		gcore_fw_event_notify(FW_CHARGER_PLUG);
	}
	if (gdev->headset_mode) {
		gcore_fw_event_notify(FW_HEADSET_PLUG);
	}
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

static bool gcore_get_charger_ststus(void)
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
	GTP_INFO("charger status:%d", status);
	return status;
}

static void gcore_work_charger_detect_work(struct work_struct *work)
{
	struct gcore_dev *gdev = fn_data.gdev;
	bool charger_mode_old = gdev->charger_mode;

	gdev->charger_mode = gcore_get_charger_ststus();
	if (!gdev->tp_suspend && (gdev->charger_mode != charger_mode_old)) {
		if (gdev->charger_mode)
			gcore_fw_event_notify(FW_CHARGER_PLUG);
		else
			gcore_fw_event_notify(FW_CHARGER_UNPLUG);

	}
}

static int gcore_charger_notify_call(struct notifier_block *nb, unsigned long event, void *data)
{
	struct power_supply *psy = data;
	struct gcore_dev *gdev = fn_data.gdev;

	if (event != PSY_EVENT_PROP_CHANGED) {
		return NOTIFY_DONE;
	}

	if ((strcmp(psy->desc->name, "usb") == 0)
	    || (strcmp(psy->desc->name, "ac") == 0)) {
		queue_delayed_work(gdev->gtp_workqueue, &gdev->charger_work, msecs_to_jiffies(500));
	}

	return NOTIFY_DONE;
}

static int gcore_init_charger_notifier(void)
{
	int ret = 0;
	struct gcore_dev *gdev = fn_data.gdev;

	GTP_INFO("Init Charger notifier");

	gdev->charger_notifier.notifier_call = gcore_charger_notify_call;
	ret = power_supply_reg_notifier(&gdev->charger_notifier);
	return ret;
}

#ifdef GCORE_WDT_RECOVERY_ENABLE
static bool gts_esd_check(struct ztp_device *cdev)
{
	struct gcore_dev *gdev = fn_data.gdev;

	if (gdev->tp_suspend)
		return 0;
	return gdev->tp_esd_check_error;
}
#endif

#ifdef GTP_LCD_FPS_NOTIFY
int gcore_lcd_fps_notify(struct ztp_device *cdev, u8 lcd_fps)
{
	int ret = 0;

	if (lcd_fps == 60) {
		ret = gcore_fw_event_notify(FW_REPORT_RATE_60);
		GTP_INFO("LCD fps 60HZ");
	} else if (lcd_fps == 90) {
		ret = gcore_fw_event_notify(FW_REPORT_RATE_90);
		GTP_INFO("LCD fps 90HZ");
	} else {
		GTP_INFO("not support LCD fps");
	}
	return ret;
}
#endif

int gcore_register_fw_class(void)
{
	struct gcore_dev *gdev = fn_data.gdev;

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
#ifdef GTP_GET_NOISE
	tpd_cdev->get_noise = gcore_tpd_get_noise;
#endif
#ifdef GTP_LCD_FPS_NOTIFY
	tpd_cdev->lcd_fps_notify = gcore_lcd_fps_notify;
#endif
	tpd_cdev->tpd_suspend_need_awake = tpd_suspend_need_awake;
	tpd_cdev->tp_self_test = tpd_test_cmd_store;
	tpd_cdev->get_tp_self_test_result = tpd_test_cmd_show;
	tpd_cdev->max_x = TOUCH_SCREEN_X_MAX;
	tpd_cdev->max_y = TOUCH_SCREEN_Y_MAX;
#ifdef GCORE_WDT_RECOVERY_ENABLE
	tpd_cdev->tpd_esd_check = gts_esd_check;
	gdev->ts_stat = 0;
	gdev->tp_esd_check_error = false;
#endif
	gdev->gtp_workqueue = create_singlethread_workqueue("gtp workqueque");
	if (!gdev->gtp_workqueue) {
		GTP_ERROR(" allocate gdev->gtp_workqueue failed\n");
	} else  {
		INIT_DELAYED_WORK(&gdev->charger_work, gcore_work_charger_detect_work);
		gcore_init_charger_notifier();
		queue_delayed_work(gdev->gtp_workqueue, &gdev->charger_work, msecs_to_jiffies(500));
	}
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	zlog_tp_dev.device_name = gcore_vendor_name;
	zlog_tp_dev.ic_name = "gcore_tp";
	TPD_ZLOG("device_name:%s, ic_name: %s.", zlog_tp_dev.device_name, zlog_tp_dev.ic_name);
#endif
	return 0;
}




