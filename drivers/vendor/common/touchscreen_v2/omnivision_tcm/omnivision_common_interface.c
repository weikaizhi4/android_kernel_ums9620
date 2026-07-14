/************************************************************************
*
* File Name: omnivision_common_interface.c
*
*  *   Version: v1.0
*
************************************************************************/

#include "omnivision_common_interface.h"

struct tpvendor_t ovt_tcm_vendor_info[] = {
	{OVT_TCM_MODULE1_ID, OVT_TCM_MODULE1_LCD_NAME },
	{OVT_TCM_MODULE2_ID, OVT_TCM_MODULE2_LCD_NAME },
	{OVT_TCM_MODULE3_ID, OVT_TCM_MODULE3_LCD_NAME },
	{VENDOR_END, "Unknown"},
};

/*tp test flag*/
#define TEST_ALL_INFO_LEN       (10 * 1024)
#define OVT_TCM_TEST_BEYOND_MAX_LIMIT		0x0001
#define OVT_TCM_TEST_BEYOND_MIN_LIMIT		0x0002
#define OVT_TCM_TEST_GT_OPEN			0x0200
#define OVT_TCM_TEST_GT_SHORT			0x0400

enum ovt_tcm_sensibility_level {
	MIN_SENSI = 0,
	NORMAL_SENSI = 1,
	HIGER_SENSI = 2,
	HIGEST_SENSI = 3,
	MAX_SENSI = 1000,
};

char ovt_tcm_vendor_name[MAX_NAME_LEN_50] = { 0 };
char ovt_tcm_save_file_path[MAX_NAME_LEN_50] = { 0 };
char ovt_tcm_save_file_name[MAX_NAME_LEN_50] = { 0 };

extern void ovt_tcm_resume_work_func(struct work_struct *work);
extern int ovt_tcm_resume(struct device *dev);
extern int ovt_tcm_suspend(struct device *dev);

extern struct zeroflash_hcd *zeroflash_hcd;

#ifdef OVT_TCM_USB_DETECT_GLOBAL
#include <linux/power_supply.h>
extern bool OVT_TCM_USB_detect_flag;
#endif

static int rst_gpio;

extern struct testing_hcd *testing_hcd;
#define GET_NOISE_DATA_TIMES 1

#ifdef OVT_TCM_PINCTRL_EN
#define OVT_TCM_PINCTRL_INIT_STATE "pmx_ts_init"
int ovt_tcm_pinctrl_init(struct spi_device *spi, struct ovt_tcm_board_data *bdata)
{
	int ret = 0;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);
	/* Get pinctrl if target uses pinctrl */
	bdata->ts_pinctrl = devm_pinctrl_get(&spi->dev);
	if (IS_ERR_OR_NULL(bdata->ts_pinctrl)) {
		ret = PTR_ERR(bdata->ts_pinctrl);
		ovt_info(ERR_LOG, "%s:devm_pinctrl_get failed, ret=%d\n", __func__, ret);
		goto err_pinctrl_get;
	} else {
		ovt_info(INFO_LOG, "%s:devm_pinctrl_get success\n", __func__);
	}

	bdata->pinctrl_state_init
	    = pinctrl_lookup_state(bdata->ts_pinctrl, OVT_TCM_PINCTRL_INIT_STATE);
	if (IS_ERR_OR_NULL(bdata->pinctrl_state_init)) {
		ret = PTR_ERR(bdata->pinctrl_state_init);
		ovt_info(ERR_LOG, "%s:pinctrl_lookup_state %s failed, ret=%d\n", __func__, OVT_TCM_PINCTRL_INIT_STATE, ret);
		goto err_pinctrl_lookup;
	} else {
		ovt_info(INFO_LOG, "%s:pinctrl_lookup_state %s success\n", __func__, OVT_TCM_PINCTRL_INIT_STATE);
	}

	ret = pinctrl_select_state(bdata->ts_pinctrl, bdata->pinctrl_state_init);
	if (ret < 0) {
		ovt_info(ERR_LOG, "%s:failed to select pin to init state, ret=%d\n", __func__, ret);
		goto err_select_init_state;
	} else {
		ovt_info(INFO_LOG, "%s:success to select pin to init state\n", __func__);
	}

	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return 0;

err_select_init_state:
err_pinctrl_lookup:
	devm_pinctrl_put(bdata->ts_pinctrl);
err_pinctrl_get:
	bdata->ts_pinctrl = NULL;
	return ret;
}
#endif

int get_ovt_tcm_module_info_from_lcd(void)
{
	int i = 0;

	for (i = 0 ; i < (ARRAY_SIZE(ovt_tcm_vendor_info) - 1) ; i ++) {
		ovt_info(INFO_LOG, "%s:%d--->%s\n", __func__, i, ovt_tcm_vendor_info[i].vendor_name);
		if (strnstr(lcd_name, ovt_tcm_vendor_info[i].vendor_name, strlen(lcd_name))) {
			ovt_info(INFO_LOG, "%s:get_lcd_panel_name find\n", __func__);
			break;
		}
	}

	strlcpy(ovt_tcm_vendor_name, ovt_tcm_vendor_info[i].vendor_name, sizeof(ovt_tcm_vendor_name));

	ovt_info(INFO_LOG, "ovt_tcm_vendor_name:%s\n", ovt_tcm_vendor_name);
	return ovt_tcm_vendor_info[i].vendor_id;
}

static int ovt_tcm_init_tpinfo(struct ztp_device *cdev)
{
	int retval = 0;
	int vendor_id;
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;
#ifndef USE_SPI_BUS
	struct i2c_client *i2c = to_i2c_client(tcm_hcd->pdev->dev.parent);
#endif
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	if (tcm_hcd->in_suspend)
		return -EIO;

	mutex_lock(&tcm_hcd->extif_mutex);

	vendor_id = get_ovt_tcm_module_info_from_lcd();
	strlcpy(cdev->ic_tpinfo.vendor_name, ovt_tcm_vendor_name, sizeof(cdev->ic_tpinfo.vendor_name));
	snprintf(cdev->ic_tpinfo.tp_name, sizeof(cdev->ic_tpinfo.tp_name), "Omnivision");
	cdev->ic_tpinfo.chip_model_id = TS_CHIP_OMNIVISION;
	cdev->ic_tpinfo.module_id = vendor_id;
	cdev->ic_tpinfo.firmware_ver = zeroflash_hcd->tcm_hcd->zte_ctrl.fw_ver;
#ifdef USE_SPI_BUS
	cdev->ic_tpinfo.spi_num = SPI_NUM;
#else
	cdev->ic_tpinfo.i2c_addr = i2c->addr;
#endif

	mutex_unlock(&tcm_hcd->extif_mutex);

	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return retval;
}

/* static int ovt_tcm_test_cmd_store(struct ztp_device *cdev, const char *buf)
{
	ovt_info(INFO_LOG, "%s:enter, useless\n", __func__);
	return 0;
}

static int ovt_tcm_test_cmd_show(struct ztp_device *cdev, char *buf)
{
	int result = 0, flag = 0, retry = 0;
	ssize_t num_read_chars = 0;
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;

	ovt_info(DEBUG_LOG, "%s enter\n", __func__);
	if(tp_alloc_tp_firmware_data(10 * TEST_ALL_INFO_LEN)) {
		ovt_info(ERR_LOG, "%s alloc tp firmware data fai\n", __func__);
		return -ENOMEM;
	}

	while (retry < 3) {
		ovt_info(INFO_LOG, "ovt_tcm_test %d times\n", (retry + 1));
		ovt_info(INFO_LOG, "\n===============testing_pt07_dynamic_range===============\n");
		result = testing_pt07_dynamic_range();
		ovt_info(INFO_LOG, "%s:dynamic_range_test result=%d\n", __func__, result);
		if (result) {
			flag = flag | OVT_TCM_TEST_BEYOND_MIN_LIMIT | OVT_TCM_TEST_BEYOND_MAX_LIMIT;
		}

		ovt_info(INFO_LOG, "\n===============testing_pt10_noise===============\n");
		result = testing_pt10_noise();
		ovt_info(INFO_LOG, "%s:noise_test result=%d\n", __func__, result);
		if (result) {
			flag = flag | OVT_TCM_TEST_BEYOND_MIN_LIMIT | OVT_TCM_TEST_BEYOND_MAX_LIMIT;
		}

		ovt_info(INFO_LOG, "\n===============testing_pt11_open_detection===============\n");
		result = testing_pt11_open_detection();
		ovt_info(INFO_LOG, "%s:open_detection_test result=%d\n", __func__, result);
		if (result) {
			flag = flag | OVT_TCM_TEST_GT_OPEN | OVT_TCM_TEST_GT_SHORT;
		}

		if (flag) {
			ovt_info(INFO_LOG, "ovt_tcm_test %d times fail", (retry + 1));
			flag = 0;
			result = 0;
			retry ++;
			//ovt_info(INFO_LOG, "%s:rst_gpio=%d\n", __func__, rst_gpio);
			if (rst_gpio) {
				gpio_set_value(rst_gpio, 0);
				usleep_range(5000, 5001);
				gpio_set_value(rst_gpio, 1);
				ovt_info(INFO_LOG, "msleep 1s for rst in test fail\n");
				msleep(1000);
			}
		} else {
			ovt_info(INFO_LOG, "ovt_tcm_test %d times pass", (retry + 1));
			break;
		}

	}

	ovt_info(INFO_LOG, "%s:RAWDATA_COL:%d\n", __func__, tcm_hcd->zte_ctrl.rawdata_cols);
	ovt_info(INFO_LOG, "%s:RAWDATA_ROW:%d\n", __func__, tcm_hcd->zte_ctrl.rawdata_rows);
	num_read_chars = snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d", flag,
		tcm_hcd->zte_ctrl.rawdata_cols, tcm_hcd->zte_ctrl.rawdata_rows, 0);
	tpd_reset_fw_data_pos_and_size();
	ovt_info(INFO_LOG, "%s:ovt tcm test:%s\n", __func__, buf);
	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return num_read_chars;
} */

static int ovt_tcm_get_headset_state(struct ztp_device *cdev)
{
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	cdev->headset_state = tcm_hcd->zte_ctrl.headset_state;

	ovt_info(INFO_LOG, "%s:headset_state=%d\n", __func__, cdev->headset_state);
	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return cdev->headset_state;
}

static int ovt_tcm_set_headset_state(struct ztp_device *cdev, int enable)
{
	int retval = 0;
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	if (!tcm_hcd->set_dynamic_config) {
		ovt_info(ERR_LOG, "%s:tcm_hcd->set_dynamic_config in null\n", __func__);
		return -EIO;
	}

	tcm_hcd->zte_ctrl.headset_state = enable;
	ovt_info(INFO_LOG, "%s: headset_state=%d\n", __func__, tcm_hcd->zte_ctrl.headset_state);
	if (!tcm_hcd->in_suspend) {
		if (tcm_hcd->zte_ctrl.headset_state)
			retval = tcm_hcd->set_dynamic_config(tcm_hcd, HEADSET_CMD, 1);
		else
			retval = tcm_hcd->set_dynamic_config(tcm_hcd, HEADSET_CMD, 0);
	}

	ovt_info(INFO_LOG, "%s:retval=%d, headset_state=%d\n", __func__, retval, enable);
	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return tcm_hcd->zte_ctrl.headset_state;
}

int ovt_tcm_resume_set_headset_status(struct ovt_tcm_hcd *tcm_hcd)
{
	int retval = 0;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	if (!tcm_hcd->set_dynamic_config) {
		ovt_info(ERR_LOG, "%s:tcm_hcd->set_dynamic_config in null\n", __func__);
		return -EIO;
	}

	if (tcm_hcd->zte_ctrl.headset_state) {
		retval = tcm_hcd->set_dynamic_config(tcm_hcd, HEADSET_CMD, 1);
		ovt_info(INFO_LOG, "%s:headset_state=1, retval=%d\n", __func__, retval);
	} else {
		ovt_info(INFO_LOG, "%s:headset_state=0\n", __func__);
	}

	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return retval;
}

static int ovt_tcm_get_sensibility(struct ztp_device *cdev)
{
	int retval = 0;
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	cdev->sensibility_enable = tcm_hcd->zte_ctrl.sensibility_level;

	ovt_info(INFO_LOG, "%s:sensibility_level=%d\n", __func__, cdev->sensibility_enable);
	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return retval;
}

static int ovt_tcm_set_sensibility(struct ztp_device *cdev, u8 enable)
{
	int retval = 0;
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	if (tcm_hcd->in_suspend) {
		ovt_info(ERR_LOG, "%s:error, ovt tp in suspend!\n", __func__);
		return -EIO;
	}

	if (!tcm_hcd->set_dynamic_config) {
		ovt_info(ERR_LOG, "%s:tcm_hcd->set_dynamic_config in null\n", __func__);
		return -EIO;
	}

	tcm_hcd->zte_ctrl.sensibility_level = enable;

	switch (enable) {
		case NORMAL_SENSI:
			ovt_info(INFO_LOG, "ovt tp is normal sensibility\n");
			retval = tcm_hcd->set_dynamic_config(tcm_hcd, SENSIBILITY_CMD, 0);
			break;
		case HIGER_SENSI:
			ovt_info(INFO_LOG, "ovt tp is higher sensibility\n");
			retval = tcm_hcd->set_dynamic_config(tcm_hcd, SENSIBILITY_CMD, 1);
			break;
		case HIGEST_SENSI:
			ovt_info(INFO_LOG, "ovt tp is highest sensibility\n");
			retval = tcm_hcd->set_dynamic_config(tcm_hcd, SENSIBILITY_CMD, 2);
			break;
		default:
			ovt_info(ERR_LOG, "Unsupport tp sensibility level\n");
			break;
	}

	ovt_info(INFO_LOG, "%s:retval=%d, sensibility_level=%d\n", __func__, retval, enable);
	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return retval;
}

static int ovt_tcm_get_tp_suspend(struct ztp_device *cdev)
{
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	cdev->tp_suspend = tcm_hcd->in_suspend;

	ovt_info(INFO_LOG, "%s:tp_suspend=%d\n", __func__, cdev->tp_suspend);
	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return cdev->tp_suspend;
}

static int ovt_tcm_set_tp_suspend(struct ztp_device *cdev, u8 suspend_node, int enable)
{
	int retval = 0;
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	if (enable)
		ovt_tcm_suspend(&tcm_hcd->pdev->dev);
	else
		queue_work(tcm_hcd->ovt_tcm_workqueue, &tcm_hcd->ovt_tcm_resume_work);

	ovt_info(INFO_LOG, "%s:tp_suspend=%d\n", __func__, enable);
	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return retval;
}

/* static int ovt_tcm_tp_resume(void *unused_tcm_hcd)
{
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)unused_tcm_hcd;

	ovt_info(INFO_LOG, "%s enter\n", __func__);
	return ovt_tcm_resume(&tcm_hcd->pdev->dev);
}

static int ovt_tcm_tp_suspend(void *unused_tcm_hcd)
{
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)unused_tcm_hcd;

	ovt_info(INFO_LOG, "%s enter\n", __func__);
	return ovt_tcm_suspend(&tcm_hcd->pdev->dev);
} */

static int ovt_tcm_resume_func(void *unused_tcm_hcd)
{
	ovt_info(INFO_LOG, "%s enter\n", __func__);

	if (zeroflash_hcd) {
		return ovt_tcm_resume(&zeroflash_hcd->tcm_hcd->pdev->dev);
	} else {
		ovt_info(ERR_LOG, "%s not implement\n", __func__);
		return 0;
	}
}

static int ovt_tcm_suspend_func(void *unused_tcm_hcd)
{
	ovt_info(INFO_LOG, "%s enter\n", __func__);

	if (zeroflash_hcd) {
		return ovt_tcm_suspend(&zeroflash_hcd->tcm_hcd->pdev->dev);
	} else {
		ovt_info(ERR_LOG, "%s not implement\n", __func__);
		return 0;
	}
}

#ifdef OVT_TCM_USB_DETECT_GLOBAL
static bool ovt_tcm_get_charger_status(void)
{
	static struct power_supply *batt_psy;
	union power_supply_propval val = { 0, };
	bool status = false;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	if (batt_psy == NULL)
		batt_psy = power_supply_get_by_name("battery");
	if (batt_psy) {
		batt_psy->desc->get_property(batt_psy, POWER_SUPPLY_PROP_STATUS, &val);
	}
	ovt_info(INFO_LOG, "%s:val.intval=%d\n", __func__, val.intval);
	/*1:charging 2:discharging 3:not charging 4:full*/
	if ((val.intval == POWER_SUPPLY_STATUS_CHARGING) ||
		(val.intval == POWER_SUPPLY_STATUS_FULL)) {
		status = true;
	} else {
		status = false;
	}

	ovt_info(INFO_LOG, "%s:charger status:%d\n", __func__, status);
	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return status;
}

static void ovt_tcm_work_charger_detect_work(struct work_struct *work)
{
	int ret = -EINVAL;
	struct delayed_work *charger_work_delay = container_of(work, struct delayed_work, work);
	struct ovt_tcm_hcd *tcm_hcd = container_of(charger_work_delay, struct ovt_tcm_hcd, charger_work);
	static int status = 0;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	OVT_TCM_USB_detect_flag = ovt_tcm_get_charger_status();
	ovt_info(INFO_LOG, "%s:OVT_TCM_USB_detect_flag=%d\n", __func__, OVT_TCM_USB_detect_flag);
	if (OVT_TCM_USB_detect_flag && !(tcm_hcd->in_suspend) && !status) {
		ret = tcm_hcd->set_dynamic_config(tcm_hcd, DC_CHARGER_CONNECTED, 1);
		ovt_info(INFO_LOG, "%s:enter_charger, ret=%d\n", __func__, ret);
		status = 1;
	} else if (!OVT_TCM_USB_detect_flag && !(tcm_hcd->in_suspend) && status) {
		ret = tcm_hcd->set_dynamic_config(tcm_hcd, DC_CHARGER_CONNECTED, 0);
		ovt_info(INFO_LOG, "%s:leave_charger, ret=%d\n", __func__, ret);
		status = 0;
	} else if (!OVT_TCM_USB_detect_flag && (tcm_hcd->in_suspend) && status) {
		ovt_info(INFO_LOG, "%s:leave_charger and tcm_hcd in_suspend\n", __func__);
		status = 0;
	} else if (OVT_TCM_USB_detect_flag && (tcm_hcd->in_suspend) && !status) {
		ovt_info(INFO_LOG, "%s:enter_charger and tcm_hcd in_suspend\n", __func__);
		status = 1;
	}

	ovt_info(INFO_LOG, "%s:charger status:%d\n", __func__, status);
	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
}

static int ovt_tcm_charger_notify_call(struct notifier_block *nb, unsigned long event, void *data)
{
	struct power_supply *psy = data;
	struct ovt_tcm_hcd *tcm_hcd = container_of(nb, struct ovt_tcm_hcd, charger_notifier);
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	if (event != PSY_EVENT_PROP_CHANGED) {
		return NOTIFY_DONE;
	}
	if ((tcm_hcd == NULL) || (tcm_hcd->charger_wq == NULL))
		return NOTIFY_DONE;
	ovt_info(INFO_LOG, "%s:psy->desc->name=%s\n", __func__, psy->desc->name);
	if ((strcmp(psy->desc->name, "usb") == 0)
	    || (strcmp(psy->desc->name, "ac") == 0)) {
		ovt_info(INFO_LOG, "%s:usb or ac\n", __func__);
		queue_delayed_work(tcm_hcd->charger_wq, &tcm_hcd->charger_work, msecs_to_jiffies(2000));
	}

	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return NOTIFY_DONE;
}

static int ovt_tcm_init_charger_notifier(struct ovt_tcm_hcd *tcm_hcd)
{
	int ret = 0;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	tcm_hcd->charger_notifier.notifier_call = ovt_tcm_charger_notify_call;
	ret = power_supply_reg_notifier(&tcm_hcd->charger_notifier);

	ovt_info(INFO_LOG, "%s:ret=%d\n", __func__, ret);
	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return ret;
}

int ovt_tcm_resume_set_charger_status(struct ovt_tcm_hcd *tcm_hcd)
{
	int retval = 0;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	if (!tcm_hcd->set_dynamic_config) {
		ovt_info(ERR_LOG, "%s:tcm_hcd->set_dynamic_config in null\n", __func__);
		return -EIO;
	}

	if (OVT_TCM_USB_detect_flag) {
		retval = tcm_hcd->set_dynamic_config(tcm_hcd, DC_CHARGER_CONNECTED, 1);
		ovt_info(INFO_LOG, "%s:enter_charger, retval=%d\n", __func__, retval);
	} else {
		ovt_info(INFO_LOG, "%s:leave_charger\n", __func__);
	}

	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return retval;
}
#endif

static int ovt_tcm_get_wakegesture(struct ztp_device *cdev)
{
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	cdev->b_gesture_enable = tcm_hcd->wakeup_gesture_enabled;

	ovt_info(INFO_LOG, "%s:gesture_enable=%d\n", __func__, cdev->b_gesture_enable);
	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return 0;
}

static int ovt_tcm_enable_wakegesture(struct ztp_device *cdev, int enable)
{
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	tcm_hcd->enter_gesture = enable;
	if (tcm_hcd->in_suspend) {
		cdev->tp_suspend_write_gesture = true;
		ovt_info(ERR_LOG, "%s:error, ovt tcm in suspend!\n", __func__);
	} else {
		ovt_info(INFO_LOG, "%s:ovt tcm in resume!\n", __func__);
		tcm_hcd->wakeup_gesture_enabled = enable;
	}

	ovt_info(INFO_LOG, "%s:gesture_enable=%d\n", __func__, enable);
	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return 0;
}

static bool ovt_tcm_suspend_need_awake(struct ztp_device *cdev)
{
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	if (!cdev->tp_suspend_write_gesture && tcm_hcd->wakeup_gesture_enabled) {
		ovt_info(INFO_LOG, "%s:ovt tcm suspend need awake\n", __func__);
		return true;
	}
	cdev->tp_suspend_write_gesture = false;
	ovt_info(INFO_LOG, "%s:ovt tcm suspend dont need awake\n", __func__);
	return false;
}

static int ovt_tcm_set_display_rotation(struct ztp_device *cdev, int mrotation)
{
	int ret = -1;
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	if (tcm_hcd->in_suspend) {
		ovt_info(ERR_LOG, "%s:error, ovt tp in suspend!\n", __func__);
		return -EIO;
	}

	if (!tcm_hcd->set_dynamic_config) {
		ovt_info(ERR_LOG, "%s:tcm_hcd->set_dynamic_config in null\n", __func__);
		return -EIO;
	}

	cdev->display_rotation = mrotation;
	ovt_info(INFO_LOG, "%s:display_rotation=%d\n", __func__, cdev->display_rotation);
	switch (cdev->display_rotation) {
		case mRotatin_0:
			ovt_info(INFO_LOG, "mRotatin_0\n");
			ret = tcm_hcd->set_dynamic_config(tcm_hcd, VERTICAL_CMD, 0);
			break;
		case mRotatin_90:/*USB on right*/
			ovt_info(INFO_LOG, "mRotatin_90\n");
			ret = tcm_hcd->set_dynamic_config(tcm_hcd, VERTICAL_CMD, 1);
			ret = tcm_hcd->set_dynamic_config(tcm_hcd, HORIZONTAL_CMD, 3);
			break;
		case mRotatin_180:
			ovt_info(INFO_LOG, "mRotatin_180\n");
			ret = tcm_hcd->set_dynamic_config(tcm_hcd, VERTICAL_CMD, 0);
			break;
		case mRotatin_270:/*USB on left*/
			ovt_info(INFO_LOG, "mRotatin_270\n");
			ret = tcm_hcd->set_dynamic_config(tcm_hcd, VERTICAL_CMD, 1);
			ret = tcm_hcd->set_dynamic_config(tcm_hcd, HORIZONTAL_CMD, 7);
			break;
		default:
			break;
	}
	if (ret) {
		ovt_info(ERR_LOG, "Set display rotation failed!\n");
	} else {
		ovt_info(INFO_LOG, "Set display rotation success!\n");
	}

	ovt_info(INFO_LOG, "%s:ret=%d\n", __func__, ret);
	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return cdev->display_rotation;
}

static int ovt_tcm_fw_upgrade(struct ztp_device *cdev, char *fw_name, int fwname_len)
{
	int result = 0;

	return result;
}

static int ovt_tcm_noise_data_request(unsigned int cols, unsigned int rows, s16 *frame_data_words)
{
	uint8_t *info_data = NULL;
	short data;
	int ret = 0, i = 0, j = 0, idx = 0;
	int total_size = 0;
	unsigned char *buf;
	unsigned int frame_size_words;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	total_size = (rows * cols) * 2;
	info_data = kzalloc((total_size * sizeof(uint8_t)), GFP_KERNEL);
	if (info_data == NULL) {
		ret = -ENOMEM;
		goto mem_end;
	}

	memset(info_data, 0, total_size * sizeof(uint8_t));

	testing_get_frame_size_words(&frame_size_words, true);

	ret = testing_run_prod_test_item(TEST_PT10_DELTA_NOISE);
	if (ret < 0) {
		ovt_info(ERR_LOG, "Failed to run test\n");
		goto test_end;
	} else {
		ovt_info(INFO_LOG, "Success to run test\n");
	}

	LOCK_BUFFER(testing_hcd->resp);

	buf = testing_hcd->resp.buf;

	for (i = 0; i < rows; i++) {
		for (j = 0; j < cols; j++) {
			data = (short)le2_to_uint(&buf[idx * 2]);
			ovt_info(DEBUG_LOG, "(%2d, %2d) data = %5d\n", i, j, data);
			frame_data_words[idx] = data;
			idx++;
		}
	}

	UNLOCK_BUFFER(testing_hcd->resp);
	testing_standard_frame_output(false);

	if (info_data != NULL)
		kfree(info_data);

	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return 0;

test_end:
	kfree(info_data);

mem_end:
	return ret;
}

static int  ovt_tcm_get_noise_data(struct ztp_device *cdev, struct ovt_tcm_hcd *tcm_hcd, unsigned int num_of_reports)
{
	s16 *frame_data_words = NULL;
	unsigned int col = 0, row = 0;
	unsigned int idx = 0;
	int retval = 0;
	int len = 0;
	int i = 0;

	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	row = le2_to_uint(tcm_hcd->app_info.num_of_image_rows);
	col = le2_to_uint(tcm_hcd->app_info.num_of_image_cols);

	ovt_info(INFO_LOG, "%s:RAWDATA_ROW:%d\n", __func__, row);
	ovt_info(INFO_LOG, "%s:RAWDATA_COL:%d\n", __func__, col);

	frame_data_words = kcalloc((row * col), sizeof(s16), GFP_KERNEL);
	if (frame_data_words ==  NULL) {
		ovt_info(ERR_LOG, "Failed to allocate frame_data_words mem\n");
		retval = -1;
		goto MEM_ALLOC_FAILED;
	}
	for (idx = 0; idx < num_of_reports; idx++) {
		retval = ovt_tcm_noise_data_request(col, row, frame_data_words);
		if (retval < 0) {
			ovt_info(ERR_LOG, "---->%d times data_request failed!\n", (idx + 1));
			goto DATA_REQUEST_FAILED;
		} else {
			ovt_info(INFO_LOG, "---->%d times data_request success!\n", (idx + 1));
		}
		len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len,
				"frame: %d, TX:%d  RX:%d\n", idx, row, col);
		len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len,
				"NoiseData:\n");
		for (i = 0; i < row * row; i++) {
			len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len,
				"%5d,",frame_data_words[i]);
			if ((i + 1) % row == 0)
				len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len, "\n");
		}
	}
	retval = 0;
	msleep(20);
	ovt_info(DEBUG_LOG, "%s exit\n", __func__);

DATA_REQUEST_FAILED:
	kfree(frame_data_words);
	frame_data_words = NULL;
MEM_ALLOC_FAILED:
	return retval;
}

static int ovt_tcm_get_noise(struct ztp_device *cdev)
{
	int ret =0;
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;

	if(tp_alloc_tp_firmware_data(10 * RT_DATA_LEN)) {
		ovt_info(ERR_LOG, "%s alloc tp firmware data fai\n", __func__);
		return -ENOMEM;
	}
	ret = ovt_tcm_get_noise_data(cdev, tcm_hcd, 2);
	if (ret) {
		ovt_info(ERR_LOG, "%s:get_noise failed\n",  __func__);
		return ret;
	} else {
		ovt_info(INFO_LOG, "%s:get_noise success\n",  __func__);
	}
	return 0;
}

#ifdef OVT_TCM_LCD_OPERATE_TP_RESET
static void ovt_tcm_reset_gpio_output(bool value)
{

	ovt_info(INFO_LOG, "%s:value=%d, rst_gpio=%d\n", __func__, value, rst_gpio);

	if (rst_gpio) {
		gpio_set_value(rst_gpio, value);
	}
}
#endif

void ominivision_tpd_register_fw_class(struct ovt_tcm_hcd *tcm_hcd)
{
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	tpd_cdev->private = (void *)tcm_hcd;
	tpd_cdev->get_tpinfo = ovt_tcm_init_tpinfo;
/* 	tpd_cdev->tpd_test_set_cmd = ovt_tcm_test_cmd_store;
	tpd_cdev->tpd_test_get_cmd = ovt_tcm_test_cmd_show; */

	tpd_cdev->headset_state_show = ovt_tcm_get_headset_state;
	tpd_cdev->set_headset_state = ovt_tcm_set_headset_state;

	tpd_cdev->get_sensibility = ovt_tcm_get_sensibility;
	tpd_cdev->set_sensibility = ovt_tcm_set_sensibility;

	tpd_cdev->tp_suspend_show = ovt_tcm_get_tp_suspend;
	tpd_cdev->set_tp_suspend = ovt_tcm_set_tp_suspend;

	tpd_cdev->tp_data = (void *)tcm_hcd;
	tpd_cdev->tp_resume_func = ovt_tcm_resume_func;
	tpd_cdev->tp_suspend_func = ovt_tcm_suspend_func;

/* 	ufp_tp_ops.tp_data = tcm_hcd;
	ufp_tp_ops.tp_resume_func = ovt_tcm_resume_func;
	ufp_tp_ops.tp_suspend_func = ovt_tcm_suspend_func; */

	tpd_cdev->get_gesture = ovt_tcm_get_wakegesture;
	tpd_cdev->wake_gesture = ovt_tcm_enable_wakegesture;

	tpd_cdev->tpd_suspend_need_awake = ovt_tcm_suspend_need_awake;

	tpd_cdev->set_display_rotation = ovt_tcm_set_display_rotation;

	tpd_cdev->tp_fw_upgrade = ovt_tcm_fw_upgrade;
	tpd_cdev->get_noise = ovt_tcm_get_noise;

#ifdef OVT_TCM_LCD_OPERATE_TP_RESET
	//rst_gpio = tcm_hcd->zte_ctrl.ovt_rst_gpio;
	rst_gpio = tcm_hcd->hw_if->bdata->reset_gpio;
	ovt_info(INFO_LOG, "%s:rst_gpio=%d\n", __func__, rst_gpio);
	tpd_cdev->tp_reset_gpio_output = ovt_tcm_reset_gpio_output;
#endif

	tcm_hcd->ovt_tcm_workqueue = create_singlethread_workqueue("ovt tcm workqueue");
	if (!tcm_hcd->ovt_tcm_workqueue) {
		ovt_info(ERR_LOG, "%s:allocate ovt tcm workqueue failed\n", __func__);
	} else {
		ovt_info(INFO_LOG, "%s:allocate ovt tcm workqueue success\n", __func__);
		INIT_WORK(&tcm_hcd->ovt_tcm_resume_work, ovt_tcm_resume_work_func);
	}

#ifdef OVT_TCM_REPORT_BY_ZTE_ALGO
	tpd_cdev->max_x = tcm_hcd->zte_ctrl.panel_max_x;
	tpd_cdev->max_y = tcm_hcd->zte_ctrl.panel_max_y;
	ovt_info(INFO_LOG, "%s:PANEL_MAX_X:%d, PANEL_MAX_Y:%d\n", __func__, tpd_cdev->max_x, tpd_cdev->max_y);
	tpd_cdev->edge_report_limit[0] = ovt_tcm_left_edge_limit_v;
	tpd_cdev->edge_report_limit[1] = ovt_tcm_right_edge_limit_v;
	tpd_cdev->edge_report_limit[2] = ovt_tcm_left_edge_limit_h;
	tpd_cdev->edge_report_limit[3] = ovt_tcm_right_edge_limit_h;
	tpd_cdev->long_pess_suppression[0] = ovt_tcm_left_edge_long_pess_v;
	tpd_cdev->long_pess_suppression[1] = ovt_tcm_right_edge_long_pess_v;
	tpd_cdev->long_pess_suppression[2] = ovt_tcm_left_edge_long_pess_h;
	tpd_cdev->long_pess_suppression[3] = ovt_tcm_right_edge_long_pess_h;
	tpd_cdev->long_press_max_count = ovt_tcm_long_press_max_count;
	tpd_cdev->edge_long_press_check = ovt_tcm_edge_long_press_check;
#endif

#ifdef OVT_TCM_USB_DETECT_GLOBAL
	tcm_hcd->charger_wq = create_singlethread_workqueue("OVT_TCM_charger_detect");
	if (!tcm_hcd->charger_wq) {
		ovt_info(ERR_LOG, "%s:ovt tcm charger_wq create failed\n", __func__);
	} else  {
		ovt_info(INFO_LOG, "%s:ovt tcm charger_wq create success\n", __func__);
		OVT_TCM_USB_detect_flag = ovt_tcm_get_charger_status();
		ovt_info(INFO_LOG, "%s:OVT_TCM_USB_detect_flag=%d\n", __func__, OVT_TCM_USB_detect_flag);
		INIT_DELAYED_WORK(&tcm_hcd->charger_work, ovt_tcm_work_charger_detect_work);
		ovt_tcm_init_charger_notifier(tcm_hcd);
	}
#endif

	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
}
