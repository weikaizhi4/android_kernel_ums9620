/************************************************************************
*
* File Name: cts_common_interface.c
*
*  *   Version: v1.0
*
************************************************************************/
#define LOG_TAG			"Common"

#include "cts_common_interface.h"

char ini_file_name[MAX_FILE_NAME_LEN] = { 0 };
char cts_vendor_name[MAX_NAME_LEN_20] = { 0 };
char cts_fw_name[MAX_FILE_NAME_LEN] = { 0 };
int cts_test_result = 0;

extern void set_tp_suspend(struct chipone_ts_data *cts_data, bool enable);
extern int cts_suspend(struct chipone_ts_data *cts_data);
extern int cts_resume(struct chipone_ts_data *cts_data);

struct tpvendor_t cts_vendor_info[] = {
	{CTS_MODULE1_ID, CTS_MODULE1_LCD_NAME },
	{CTS_MODULE2_ID, CTS_MODULE2_LCD_NAME },
	{CTS_MODULE3_ID, CTS_MODULE3_LCD_NAME },
	{VENDOR_END, "Unknown"},
};

enum cts_sensibility_level {
	MIN_SENSI = 0,
	NORMAL_SENSI = 1,
	HIGER_SENSI = 2,
	HIGEST_SENSI = 3,
	MAX_SENSI = 1000,
};

#ifdef CTS_LCD_OPERATE_TP_RESET
static int rst_gpio;
#endif

int get_cts_module_info_from_lcd(void)
{
	int i = 0;

	for (i = 0 ; i < (ARRAY_SIZE(cts_vendor_info) - 1) ; i ++) {
		cts_info("%s:%d--->%s", __func__, i, cts_vendor_info[i].vendor_name);
		if (strnstr(lcd_name, cts_vendor_info[i].vendor_name, strlen(lcd_name))) {
			cts_info("%s:get_lcd_panel_name find", __func__);
			break;
		}
	}

	strlcpy(cts_vendor_name, cts_vendor_info[i].vendor_name, sizeof(cts_vendor_name));
	snprintf(cts_fw_name, sizeof(cts_fw_name), "chipone_firmware_%s.bin", cts_vendor_name);
	snprintf(ini_file_name, sizeof(ini_file_name), "cts_test_sensor_%s.cfg", cts_vendor_name);

	cts_info("cts_vendor_name:%s", cts_vendor_name);
	cts_info("cts_fw_name:%s", cts_fw_name);
	cts_info("ini_file_name:%s", ini_file_name);
	return cts_vendor_info[i].vendor_id;
}

static int cts_init_tpinfo(struct ztp_device *cdev)
{
	int retval = 0;
	int vendor_id;
	struct cts_device *cts_dev = (struct cts_device *)cdev->private;

	cts_dbg("%s enter", __func__);

	if (cts_dev->rtdata.suspended) {
		cts_err("%s:In suspended", __func__);
		return -EIO;
	}

	vendor_id = get_cts_module_info_from_lcd();
	strlcpy(cdev->ic_tpinfo.vendor_name, cts_vendor_name, sizeof(cdev->ic_tpinfo.vendor_name));
	snprintf(cdev->ic_tpinfo.tp_name, sizeof(cdev->ic_tpinfo.tp_name), "chipone");
	cdev->ic_tpinfo.chip_model_id = TS_CHIP_CHIPONE;
	cdev->ic_tpinfo.module_id = vendor_id;
	cdev->ic_tpinfo.firmware_ver = cts_dev->fwdata.version;
#ifdef USE_SPI_BUS
	cdev->ic_tpinfo.spi_num = SPI_NUM;
#else
	cdev->ic_tpinfo.i2c_addr = i2c->addr;
#endif

	cts_dbg("%s exit", __func__);
	return retval;
}

#ifdef CFG_CTS_HEADSET_DETECT
static int cts_get_headset_state(struct ztp_device *cdev)
{
	struct cts_device *cts_dev = (struct cts_device *)cdev->private;
	struct chipone_ts_data *cts_data = container_of(cts_dev, struct chipone_ts_data, cts_dev);
	cts_dbg("%s enter", __func__);

	cdev->headset_state = cts_data->headset_mode;

	cts_info("%s:headset_state=%d", __func__, cdev->headset_state);
	cts_dbg("%s exit", __func__);
	return cdev->headset_state;
}

static int cts_set_headset_state(struct ztp_device *cdev, int enable)
{
	struct cts_device *cts_dev = (struct cts_device *)cdev->private;
	struct chipone_ts_data *cts_data = container_of(cts_dev, struct chipone_ts_data, cts_dev);
	cts_dbg("%s enter", __func__);

	cts_data->headset_mode = enable;
	cts_info("%s:headset_state=%d", __func__, cts_data->headset_mode);
	if (!cts_dev->rtdata.suspended) {
		if (cts_data->headset_mode) {
			cts_earphone_plugin(&cts_data->cts_dev);
		} else {
			cts_earphone_plugout(&cts_data->cts_dev);
		}
	}

	cts_dbg("%s exit", __func__);
	return cts_data->headset_mode;
}
#endif

static int cts_get_sensibility(struct ztp_device *cdev)
{
	int retval = 0;
	cts_dbg("%s enter", __func__);

	cts_info("%s:sensibility_level=%d", __func__, cdev->sensibility_enable);
	cts_dbg("%s exit", __func__);
	return retval;
}

static int cts_set_sensibility(struct ztp_device *cdev, u8 enable)
{
	int retval = 0;
	struct cts_device *cts_dev = (struct cts_device *)cdev->private;
	cts_dbg("%s enter", __func__);

	if (cts_dev->rtdata.suspended) {
		cts_err("%s:In suspended", __func__);
		return -EIO;
	}

	switch (enable) {
		case NORMAL_SENSI:
			cts_info("cts tp is normal sensibility");
			break;
		case HIGER_SENSI:
			cts_info("cts tp is higher sensibility");
			break;
		case HIGEST_SENSI:
			cts_info("cts tp is highest sensibility");
			break;
		default:
			cts_err("Unsupport tp sensibility level");
			break;
	}

	cts_info("%s:retval=%d, sensibility_level=%d", __func__, retval, enable);
	cts_dbg("%s exit", __func__);
	return retval;
}

static int cts_get_tp_suspend(struct ztp_device *cdev)
{
	struct cts_device *cts_dev = (struct cts_device *)cdev->private;
	cts_dbg("%s enter", __func__);

	cdev->tp_suspend = cts_is_device_suspended(cts_dev);

	cts_info("%s:tp_suspend=%d", __func__, cdev->tp_suspend);
	cts_dbg("%s exit", __func__);
	return cdev->tp_suspend;
}

static int cts_set_tp_suspend(struct ztp_device *cdev, u8 suspend_node, int enable)
{
	int retval = 0;
	struct cts_device *cts_dev = (struct cts_device *)cdev->private;
	struct chipone_ts_data *cts_data = container_of(cts_dev, struct chipone_ts_data, cts_dev);
	cts_dbg("%s enter", __func__);

	if (suspend_node == PROC_SUSPEND_NODE) {
		set_tp_suspend(cts_data, enable);
	} else {
		if (enable) {
			cts_suspend(cts_data);
		} else {
			queue_work(cts_data->workqueue, &cts_data->ts_resume_work);
		}
	}

	cts_info("%s:tp_suspend=%d", __func__, enable);
	cts_dbg("%s exit", __func__);
	return retval;
}

static int chipone_ts_resume(void * cts_dev)
{
	struct chipone_ts_data *cts_data = container_of(cts_dev, struct chipone_ts_data, cts_dev);

	cts_info("%s enter", __func__);

	return cts_resume(cts_data);
}

static int chipone_ts_suspend(void *cts_dev)
{
	struct chipone_ts_data *cts_data = container_of(cts_dev, struct chipone_ts_data, cts_dev);

	cts_info("%s enter", __func__);

	return cts_suspend(cts_data);
}

#ifdef CFG_CTS_GESTURE
static int cts_get_wakegesture(struct ztp_device *cdev)
{
	struct cts_device *cts_dev = (struct cts_device *)cdev->private;
	cts_dbg("%s enter", __func__);

	cdev->b_gesture_enable = cts_is_gesture_wakeup_enabled(cts_dev);

	cts_info("%s:gesture_enable=%d", __func__, cdev->b_gesture_enable);
	cts_dbg("%s exit", __func__);
	return 0;
}

static int cts_enable_wakegesture(struct ztp_device *cdev, int enable)
{
	struct cts_device *cts_dev = (struct cts_device *)cdev->private;
	cts_dbg("%s enter", __func__);

	if (cts_dev->rtdata.suspended) {
		cdev->tp_suspend_write_gesture = true;
	}

	if (enable) {
		cts_enable_gesture_wakeup(cts_dev);
	} else {
		cts_disable_gesture_wakeup(cts_dev);
	}

	cts_info("%s:gesture_enable=%d", __func__, enable);
	cts_dbg("%s exit", __func__);
	return enable;
}
#endif

static bool cts_suspend_need_awake(struct ztp_device *cdev)
{
	struct cts_device *cts_dev = (struct cts_device *)cdev->private;
	cts_dbg("%s enter", __func__);

#ifdef CFG_CTS_GESTURE
	if (!cdev->tp_suspend_write_gesture &&
		(cts_dev->rtdata.updating || cts_dev->rtdata.gesture_wakeup_enabled)) {
		cts_info("tp suspend need awake");
		return true;
	}
#else
	if (cts_dev->rtdata.updating) {
		cts_info("tp suspend need awake");
		return true;
	}
#endif
	else {
		cdev->tp_suspend_write_gesture = false;
		cts_info("tp suspend dont need awake");
		return false;
	}
}

#ifdef CFG_CTS_ROTATION
static int cts_set_display_rotation(struct ztp_device *cdev, int mrotation)
{
	int ret = -1;
	struct cts_device *cts_dev = (struct cts_device *)cdev->private;
	cts_dbg("%s enter", __func__);

	if (cts_dev->rtdata.suspended) {
		cts_err("%s:In suspended", __func__);
		return -EIO;
	}

	cdev->display_rotation = mrotation;
	cts_info("%s:display_rotation=%d", __func__, cdev->display_rotation);
	switch (cdev->display_rotation) {
	case mRotatin_0:
		cts_info("mRotatin_0 00");
		ret = cts_dev->ops->set_panel_direction(cts_dev,0x00);
		break;
	case mRotatin_90:/*USB on right*/
		cts_info("mRotatin_90 01");
		ret = cts_dev->ops->set_panel_direction(cts_dev,0x01);
		break;
	case mRotatin_180:
		cts_info("mRotatin_180 00");
		ret = cts_dev->ops->set_panel_direction(cts_dev,0x00);
		break;
	case mRotatin_270:/*USB on left*/
		cts_info("mRotatin_270 02");
		ret = cts_dev->ops->set_panel_direction(cts_dev,0x02);
		break;
	default:
		break;
	}

	cts_info("%s:ret=%d", __func__, ret);
	if (ret) {
		cts_err("%s:Set display rotation failed!", __func__);
	} else {
		cts_info("%s:Set display rotation success!", __func__);
	}

	cts_dbg("%s exit", __func__);
	return cdev->display_rotation;
}
#endif

struct cts_firmware *cts_get_firmware(void)
{
	struct cts_firmware *firmware;
	struct ztp_device *cdev = tpd_cdev;

	if (cdev->tp_firmware == NULL) {
		cts_err("cdev->tp_firmware is NULL");
		return NULL;
	}
	firmware = kzalloc(sizeof(struct cts_firmware), GFP_KERNEL);
	if (firmware == NULL) {
		cts_err("Request firmware alloc cts_firmware failed");
		return NULL;
	}
	if (!cdev->tp_firmware->size)
		goto err_free_firmware;
	firmware->size = cdev->tp_firmware->size;
	firmware->data = vmalloc(firmware->size);
	if (firmware->data == NULL) {
		cts_err("Request form file alloc firmware data failed");
		goto err_free_firmware;
	}
	memcpy(firmware->data, (u8 *)cdev->tp_firmware->data, firmware->size);
	return firmware;
err_free_firmware:
	kfree(firmware);
	return NULL;
}

static int cts_fw_upgrade(struct ztp_device *cdev, char *fw_name, int fwname_len)
{
	struct cts_firmware *firmware;
	struct cts_device *cts_dev = (struct cts_device *)cdev->private;
	int ret = 0;

	firmware = cts_get_firmware();
	if (firmware == NULL) {
		cts_err("Request firmware failed");
		return -ENOENT;
	}
	ret = cts_stop_device(cts_dev);
	if (ret) {
		cts_err("Stop device failed %d",	ret);
		goto err_release_firmware;
	}
	cts_lock_device(cts_dev);
	ret = cts_update_firmware(cts_dev, firmware, false);
	cts_unlock_device(cts_dev);
	if (ret) {
		cts_err("Update firmware failed %d",	ret);
		goto err_release_firmware;
	}

	ret = cts_start_device(cts_dev);
	if (ret) {
		cts_err("Start device failed %d", 	ret);
		goto err_release_firmware;
	}
err_release_firmware:
	cts_release_firmware(firmware);
	return ret;
}

static int cts_data_request(struct ztp_device *cdev, s16 *frame_data_words, enum tp_test_type  test_type)
{
	int  ret = 0;
	unsigned int x = 0;
	unsigned int y = 0;
	unsigned int col = 0;
	unsigned int row = 0;
	struct cts_device *cts_dev = (struct cts_device *)cdev->private;

	row = cts_dev->fwdata.rows;
	col = cts_dev->fwdata.cols;
	ret = cts_enable_get_rawdata(cts_dev);
	if (ret) {
		cts_err("Enable read raw data failed %d", ret);
		goto out;
	}

	ret = cts_send_command(cts_dev, CTS_CMD_QUIT_GESTURE_MONITOR);
	if (ret) {
		cts_err("Send cmd QUIT_GESTURE_MONITOR failed %d", ret);
		goto out;
	}
	msleep(50);
	switch (test_type) {
	case RAWDATA_TEST:
		ret = cts_get_rawdata(cts_dev, frame_data_words);
		if (ret) {
			cts_err("Get raw data failed %d", ret);
			goto out;
		}
		break;
	case DELTA_TEST:
		ret = cts_get_diffdata(cts_dev, frame_data_words);
		if (ret) {
			cts_err("Get diff data failed %d", ret);
			goto out;
		}
		break;
	default:
		cts_err("%s:the Para is error!", __func__);
		ret = -1;
		goto out;
	}

	ret = cts_disable_get_rawdata(cts_dev);
	if (ret) {
		cts_err("Disable read raw data failed %d", ret);
		goto out;
	}
	for (y = 0; y < row; y++) {
		pr_cont("CTP[%2d]", (y + 1));
		for (x = 0; x < col; x++) {
			pr_cont("%5d,", frame_data_words[y * col + x]);
		}
		pr_cont("\n");
	}

out:
	return ret;
}

static int  cts_testing_delta_raw_report(struct ztp_device *cdev, unsigned int num_of_reports)
{
	s16 *frame_data_words = NULL;
	unsigned int col = 0;
	unsigned int row = 0;
	unsigned int idx = 0;
	int retval = 0;
	int len = 0;
	int i = 0;
	struct cts_device *cts_dev = (struct cts_device *)cdev->private;

	if (cts_dev->rtdata.suspended) {
		cts_err("%s:In suspended", __func__);
		return -EIO;
	}

	row = cts_dev->fwdata.rows;
	col = cts_dev->fwdata.cols;
	cts_plat_disable_irq(cts_dev->pdata);
	frame_data_words = kcalloc((row * col), sizeof(s16), GFP_KERNEL);
	if (frame_data_words ==  NULL) {
		cts_err("Failed to allocate frame_data_words mem");
		retval = -1;
		goto MEM_ALLOC_FAILED;
	}
	for (idx = 0; idx < num_of_reports; idx++) {
		len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len,
				"frame: %d, TX:%d  RX:%d\n", idx, row, col);
		retval = cts_data_request(cdev, frame_data_words, RAWDATA_TEST);
		if (retval < 0) {
			cts_err("data_request failed!");
			goto DATA_REQUEST_FAILED;
		}
		len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len,
				"RawData:\n");
		for (i = 0; i < row * row; i++) {
			len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len,
				"%5d,", frame_data_words[i]);
			if ((i + 1) % row == 0)
				len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len, "\n");
		}
		retval = cts_data_request(cdev, frame_data_words, DELTA_TEST);
		if (retval < 0) {
			cts_err("data_request failed!");
			goto DATA_REQUEST_FAILED;
		}
		len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len,
				"DiffData:\n");
		for (i = 0; i < row * row; i++) {
			len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len,
				"%5d,", frame_data_words[i]);
			if ((i + 1) % row == 0)
				len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len, "\n");
		}
	}
DATA_REQUEST_FAILED:
	len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len, "\n\n");
	retval = cts_plat_reset_device(cts_dev->pdata);
	if (retval) {
		cts_err("get tp rawdata reset chip failed %d", retval);
	}
	msleep(20);
	cts_info("get tp delta raw data end!");
	kfree(frame_data_words);
	frame_data_words = NULL;
MEM_ALLOC_FAILED:
	cts_plat_enable_irq(cts_dev->pdata);
	return retval;
}

static int cts_get_noise(struct ztp_device *cdev)
{
	int ret =0;

	if(tp_alloc_tp_firmware_data(10 * RT_DATA_LEN)) {
		cts_err(" alloc tp firmware data fail");
		return -ENOMEM;
	}
	ret = cts_testing_delta_raw_report(cdev, 5);
	if (ret) {
		cts_err( "%s:get_noise failed\n",  __func__);
		return ret;
	} else {
		cts_info("%s:get_noise success\n",  __func__);
	}
	return 0;
}

#ifdef CTS_LCD_OPERATE_TP_RESET
static void cts_reset_gpio_output(bool value)
{
	cts_info("%s:value=%d, rst_gpio=%d", __func__, value, rst_gpio);

	if (rst_gpio) {
		gpio_set_value(rst_gpio, value);
	}
}
#endif

void cts_tpd_register_fw_class(struct cts_device *cts_dev)
{
	cts_dbg("%s enter", __func__);

	tpd_cdev->private = (void *)cts_dev;
	tpd_cdev->get_tpinfo = cts_init_tpinfo;
#ifdef CFG_CTS_HEADSET_DETECT
	tpd_cdev->headset_state_show = cts_get_headset_state;
	tpd_cdev->set_headset_state = cts_set_headset_state;
#endif

	tpd_cdev->get_sensibility = cts_get_sensibility;
	tpd_cdev->set_sensibility = cts_set_sensibility;

	tpd_cdev->tp_suspend_show = cts_get_tp_suspend;
	tpd_cdev->set_tp_suspend = cts_set_tp_suspend;

	tpd_cdev->tp_data = cts_dev;
	tpd_cdev->tp_resume_func = chipone_ts_resume;
	tpd_cdev->tp_suspend_func = chipone_ts_suspend;

	/* ufp_tp_ops.tp_data = cts_dev;
	ufp_tp_ops.tp_resume_func = chipone_ts_resume;
	ufp_tp_ops.tp_suspend_func = chipone_ts_suspend; */

#ifdef CFG_CTS_GESTURE
	tpd_cdev->get_gesture = cts_get_wakegesture;
	tpd_cdev->wake_gesture = cts_enable_wakegesture;
#endif

	tpd_cdev->tpd_suspend_need_awake = cts_suspend_need_awake;

#ifdef CFG_CTS_ROTATION
	tpd_cdev->set_display_rotation = cts_set_display_rotation;
#endif

	tpd_cdev->tp_fw_upgrade = cts_fw_upgrade;
	tpd_cdev->get_noise = cts_get_noise;

#ifdef CTS_LCD_OPERATE_TP_RESET
	rst_gpio = cts_dev->pdata->rst_gpio;
	cts_info("%s:rst_gpio=%d", __func__, rst_gpio);
	tpd_cdev->tp_reset_gpio_output = cts_reset_gpio_output;
#endif

	/* tpd_cdev->tpd_test_set_cmd = tpd_test_cmd_store;
	tpd_cdev->tpd_test_get_cmd = tpd_test_cmd_show;

	strlcpy(ini_file_path, DEFAULT_INI_FILE_PATH, sizeof(ini_file_path));
	strlcpy(save_file_path, DEFAULT_SAVE_FILE_PATH, sizeof(ini_file_path));
	strlcpy(save_test_result_file_name, DEFAULT_RESULT_FILE_NAME, sizeof(save_test_result_file_name));
	strlcpy(save_test_info_file_name, DEFAULT_INFO_FILE_NAME, sizeof(save_test_info_file_name)); */

#ifdef CTS_REPORT_BY_ZTE_ALGO
	tpd_cdev->max_x = cts_dev->pdata->res_x;
	tpd_cdev->max_y = cts_dev->pdata->res_y;
	cts_info("%s:PANEL_MAX_X:%d, PANEL_MAX_Y:%d", __func__, tpd_cdev->max_x, tpd_cdev->max_y);
	tpd_cdev->edge_report_limit[0] = cts_left_edge_limit_v;
	tpd_cdev->edge_report_limit[1] = cts_right_edge_limit_v;
	tpd_cdev->edge_report_limit[2] = cts_left_edge_limit_h;
	tpd_cdev->edge_report_limit[3] = cts_right_edge_limit_h;
	tpd_cdev->long_pess_suppression[0] = cts_left_edge_long_pess_v;
	tpd_cdev->long_pess_suppression[1] = cts_right_edge_long_pess_v;
	tpd_cdev->long_pess_suppression[2] = cts_left_edge_long_pess_h;
	tpd_cdev->long_pess_suppression[3] = cts_right_edge_long_pess_h;
	tpd_cdev->long_press_max_count = cts_long_press_max_count;
	tpd_cdev->edge_long_press_check = cts_edge_long_press_check;
#endif

	cts_dbg("%s exit", __func__);
}
