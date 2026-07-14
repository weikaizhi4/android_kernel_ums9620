#include "semi_touch_custom.h"
#include "semi_touch_function.h"
#include "semi_touch_upgrade.h"
#include "semi_config.h"
#include <linux/power_supply.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#if SEMI_TOUCH_FACTORY_TEST_EN
#include "semi_touch_test_5448.h"
#endif

#define MAX_NAME_LEN_20  20
#define MAX_NAME_LEN_50  50
#define MAX_FILE_NAME_LEN       64
#define MAX_ALLOC_BUFF 256
#define TP_TEST_START	2

extern const char *const mapping_ic_from_type(unsigned char ictype);
extern int semi_set_tp_suspend(struct ztp_device *cdev, u8 suspend_node, int enable);
extern int semi_touch_check_and_update(const unsigned char *udp, unsigned int len);
extern void semi_get_tpd_channel_info(unsigned char *rowsCnt, unsigned char *colsCnt);
extern void semi_tp_irq_enable(bool enable);

char semi_vendor_name[MAX_NAME_LEN_20] = { 0 };
char semi_firmware_name[MAX_FILE_NAME_LEN] = {0};
char g_semi_save_file_path[MAX_NAME_LEN_50] = { 0 };
char g_semi_save_file_name[MAX_NAME_LEN_50] = { 0 };

struct tpvendor_t semi_vendor_l[] = {
	{SEMI_VENDOR_ID_0, SEMI_VENDOR_0_NAME},
	{SEMI_VENDOR_ID_1, SEMI_VENDOR_1_NAME},
	{SEMI_VENDOR_ID_2, SEMI_VENDOR_2_NAME},
	{SEMI_VENDOR_ID_3, SEMI_VENDOR_3_NAME},
	{VENDOR_END, "Unknown"},
};

int semi_get_vendor_and_firmware(void)
{
	int i = 0;
	int ret = 0;

	for (i = 0; i < ARRAY_SIZE(semi_vendor_l); i++) {
		if (strnstr(lcd_name, semi_vendor_l[i].vendor_name, strlen(lcd_name))) {
			strlcpy(semi_vendor_name, semi_vendor_l[i].vendor_name, sizeof(semi_vendor_name));
			ret = 0;
			goto out;
		}
	}
	ret = -1;
	strlcpy(semi_vendor_name, SEMI_VENDOR_0_NAME, sizeof(semi_vendor_name));

out:
	snprintf(semi_firmware_name, sizeof(semi_firmware_name),
			"semi_firmware_%s.bin", semi_vendor_name);
	kernel_log_d("firmware name :%s\n", semi_firmware_name);
	return ret;
}

static int tpd_init_tpinfo(struct ztp_device *cdev)
{
	int ret = 0;
	unsigned char readBuffer[8] = { 0 };

	kernel_log_d("enter!");

	ret = semi_touch_read_bytes(0x20000000 + 0x80, readBuffer, 8);
	check_return_if_fail(ret, NULL);

	snprintf(cdev->ic_tpinfo.tp_name, MAX_VENDOR_NAME_LEN, "chsc_%s", mapping_ic_from_type(readBuffer[0]));
	cdev->ic_tpinfo.config_ver = readBuffer[1];
	cdev->ic_tpinfo.module_id = (((readBuffer[3] << 8) + readBuffer[2]) << 8) + readBuffer[4];
	ret = semi_touch_read_bytes(0x20000000 + 0x10, readBuffer, 8);
	check_return_if_fail(ret, NULL);
	cdev->ic_tpinfo.firmware_ver =(readBuffer[5] << 8) + readBuffer[4];
	cdev->ic_tpinfo.chip_model_id = TS_CHIP_SEMI;
	cdev->ic_tpinfo.i2c_addr = 0x2e;
	strlcpy(cdev->ic_tpinfo.vendor_name, semi_vendor_name, sizeof(cdev->ic_tpinfo.vendor_name));
	return 0;

}

static int semi_tp_fw_upgrade(struct ztp_device *cdev, char *fw_name, int fwname_len)
{

	if (cdev->tp_firmware == NULL || cdev->tp_firmware->data == NULL) {
		kernel_log_e("cdev->tp_firmware is NULL");
		return -EIO;
	}

	st_dev.fw_force_update = true;
	semi_touch_check_and_update(cdev->tp_firmware->data , (unsigned int)cdev->tp_firmware->size);
	st_dev.fw_force_update = false;
	semi_touch_reset_and_detect();
	if (st_dev.fw_is_updated) {
		semi_touch_mode_init(&st_dev);
		semi_touch_resolution_adaption(&st_dev);
		st_dev.fw_is_updated = false;
	}
	return 0;
}

static int tpd_get_wakegesture(struct ztp_device *cdev)
{

	cdev->b_gesture_enable = st_dev.is_double_tap;

	return 0;
}

static int tpd_enable_wakegesture(struct ztp_device *cdev, int enable)
{

	kernel_log_d("%s: enter!, enable=%d", __func__, enable);
	st_dev.is_double_tap = enable;
	if (st_dev.suspended) {
		kernel_log_e("%s: error, change set in suspend!", __func__);
	} else {
		if (st_dev.is_single_tap || st_dev.is_double_tap)
			open_guesture_function(st_dev.stc.custom_function_en);
		else
			close_guesture_function(st_dev.stc.custom_function_en);
	}

	return 0;

}

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
static int tpd_get_singleaodgesture(struct ztp_device *cdev)
{
	cdev->b_single_aod_enable = st_dev.is_single_aod;
	kernel_log_d("%s: enter!,  st_dev.is_single_aod=%d", __func__,  st_dev.is_single_aod);
	kernel_log_d("%s: enter!, cdev->b_single_aod_enable=%d", __func__, cdev->b_single_aod_enable);
	return 0;
}

static int tpd_set_singleaodgesture(struct ztp_device *cdev, int enable)
{

	kernel_log_d("%s: enter!, enable=%d", __func__, enable);
	st_dev.is_single_aod = enable;
	if (st_dev.suspended) {
		kernel_log_e("%s: error, change set in suspend!", __func__);
	} else {
		 st_dev.is_single_aod = enable;
		 st_dev.is_single_tap = (st_dev.is_single_aod || st_dev.is_single_fp) ? 5 : 0;
		 if (st_dev.is_single_tap || st_dev.is_double_tap)
			open_guesture_function(st_dev.stc.custom_function_en);
		else
			close_guesture_function(st_dev.stc.custom_function_en);
	}
	kernel_log_d(" st_dev.is_single_fp=%d",  st_dev.is_single_fp);
	kernel_log_d(" st_dev.is_single_aod=%d",  st_dev.is_single_aod);
	kernel_log_d(" st_dev.is_single_tap=%d",  st_dev.is_single_tap);
	return 0;
}

static int tpd_get_singlefpgesture(struct ztp_device *cdev)
{
	cdev->b_single_tap_enable =  st_dev.is_single_fp;
	kernel_log_d("%s: enter!,  st_dev.is_single_fp=%d", __func__,  st_dev.is_single_fp);
	kernel_log_d("%s: enter!, cdev->b_single_tap_enable=%d", __func__, cdev->b_single_tap_enable);
	return 0;
}

static int tpd_set_singlefpgesture(struct ztp_device *cdev, int enable)
{
	kernel_log_d("%s: enter!, enable=%d", __func__, enable);
	st_dev.is_single_fp = enable;
	if (st_dev.suspended) {
		kernel_log_e("%s: error, change set in suspend!", __func__);
	} else {
		 st_dev.is_single_fp = enable;
		 st_dev.is_single_tap = ( st_dev.is_single_aod ||  st_dev.is_single_fp) ? 5 : 0;
		 if (st_dev.is_single_tap || st_dev.is_double_tap)
			open_guesture_function(st_dev.stc.custom_function_en);
		else
			close_guesture_function(st_dev.stc.custom_function_en);
	}
	kernel_log_d(" st_dev.is_single_fp=%d",  st_dev.is_single_fp);
	kernel_log_d(" st_dev.is_single_aod=%d",  st_dev.is_single_aod);
	kernel_log_d(" st_dev.is_single_tap=%d",  st_dev.is_single_tap);
	return 0;
}
#endif

int semi_set_tp_suspend(struct ztp_device *cdev, u8 suspend_node, int enable)
{
	if (enable)
		change_tp_state(LCD_OFF);
	else
		change_tp_state(LCD_ON);
	return 0;
}

int semi_tp_suspend(void *semi_data)
{
	struct sm_touch_dev *st_dev = (struct sm_touch_dev *)semi_data;

	semi_touch_suspend_entry(&st_dev->client->dev);
	return 0;
}

int semi_tp_resume(void *semi_data)
{
	struct sm_touch_dev *st_dev = (struct sm_touch_dev *)semi_data;

	semi_touch_resume_entry(&st_dev->client->dev);
	return 0;
}

#if SEMI_TOUCH_FACTORY_TEST_EN
static int tpd_test_cmd_store(struct ztp_device *cdev)
{
	int retval = 0;
	int retry = 0;

	kernel_log_d("%s:start TP test.\n", __func__);
	do {
		st_dev.tp_self_test_result = 0;
		retval = semi_touch_start_factory_test();
		if (retval) {
			retry++;
			kernel_log_e("tp test failed, retry:%d", retry);
			msleep(20);
		} else {
			break;
		}
	} while (retry < 3);
	if (retry == 3) {
		kernel_log_e("Self_Test Fail\n");
	} else {
		kernel_log_d("Self_Test Pass\n");
	}
	return 0;
}

static int tpd_test_cmd_show(struct ztp_device *cdev, char *buf)
{
	int i_len;
	unsigned char rowsCnt = 0;
	unsigned char colsCnt = 0;
	ssize_t num_read_chars = 0;

	kernel_log_d("%s:enter\n", __func__);
	semi_get_tpd_channel_info(&rowsCnt, &colsCnt);
	i_len = snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d",	st_dev.tp_self_test_result, rowsCnt, colsCnt, 0);
	kernel_log_d("tpd test result:%d.\n", st_dev.tp_self_test_result);
	kernel_log_d("tpd  test:%s.\n", buf);
	num_read_chars = i_len;
	return num_read_chars;
}
#endif

static int semi_set_tp_report_rate(struct ztp_device *cdev, int tp_report_rate_level)
{
	int ret = 0;
	unsigned short rate = 180;

	if (tp_report_rate_level > 2)
		tp_report_rate_level = 1;

	cdev->tp_report_rate  = tp_report_rate_level;
	if (st_dev.suspended || st_dev.fw_updating) {
		kernel_log_e("%s: error, change set in suspend!", __func__);
	} else {
		/*0:in tp report mode->in 120Hz;
		  1:in tp report mode->in 180Hz;
		  2:in tp report mode->in 240Hz;*/
		switch (tp_report_rate_level) {
		case 0:
			rate = 120;
			kernel_log_d("tp report rate 120HZ");
			break;
		case 1:
			rate = 180;
			kernel_log_d("tp report rate 180HZ");
			break;
		case 2:
			rate = 240;
			kernel_log_d("tp report rate 240HZ");
			break;
		default:
			kernel_log_d("Unsupport report ratel");
			return -EINVAL;
		}
		ret = semi_touch_report_rate_switch(rate);
		if (!ret)
			kernel_log_d("set report rate mode success");
		else
			kernel_log_e("set report rate mode failed!");
	}

	return 0;
}

static int semi_set_display_rotation(struct ztp_device *cdev, int mrotation)
{

	cdev->display_rotation = mrotation;
	if (st_dev.suspended || st_dev.fw_updating)
		return -EINVAL;
	kernel_log_d("%s: display_rotation = %d.\n", __func__, cdev->display_rotation);
	semi_touch_orientation_switch(mrotation);
	return 0;
}

static int tpd_semi_shutdown(struct ztp_device *cdev)
{
	kernel_log_d("disable irq");
	semi_tp_irq_enable(false);
#if SEMI_TOUCH_SUSPEND_BY_TPCMD
	semi_touch_suspend_ctrl(1);
#else
	semi_touch_power_ctrl(0);
#endif
	semi_io_direction_out(st_dev.rst_pin, 0);
	return 0;
}

void semi_tpd_register_fw_class(struct sm_touch_dev *st_dev)
{

	kernel_log_d("entry");
	semi_get_vendor_and_firmware();
	tpd_cdev->private = (void *)st_dev;
	tpd_cdev->get_tpinfo = tpd_init_tpinfo;
	tpd_cdev->set_tp_suspend = semi_set_tp_suspend;
	tpd_cdev->tp_fw_upgrade = semi_tp_fw_upgrade;
	tpd_cdev->get_gesture = tpd_get_wakegesture;
	tpd_cdev->wake_gesture = tpd_enable_wakegesture;
	tpd_cdev->tp_data = st_dev;
	tpd_cdev->tp_resume_func = semi_tp_resume;
	tpd_cdev->tp_suspend_func = semi_tp_suspend;
	tpd_cdev->set_display_rotation = semi_set_display_rotation;
	tpd_cdev->set_tp_report_rate = semi_set_tp_report_rate;
#if SEMI_TOUCH_FACTORY_TEST_EN
	tpd_cdev->tp_self_test = tpd_test_cmd_store;
	tpd_cdev->get_tp_self_test_result = tpd_test_cmd_show;
#endif
	tpd_cdev->max_x = SEMI_TOUCH_SOLUTION_X;
	tpd_cdev->max_y = SEMI_TOUCH_SOLUTION_Y;
	tpd_cdev->tp_report_rate = 1;
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	tpd_cdev->get_singletap = tpd_get_singlefpgesture;
	tpd_cdev->set_singletap = tpd_set_singlefpgesture;

	tpd_cdev->get_singleaod = tpd_get_singleaodgesture;
	tpd_cdev->set_singleaod = tpd_set_singleaodgesture;
	tpd_cdev->one_key_enable = false;
#endif
	tpd_cdev->tpd_shutdown = tpd_semi_shutdown;
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	zlog_tp_dev.device_name = semi_vendor_name;
	zlog_tp_dev.ic_name = "semi_tp";
	TPD_ZLOG("device_name:%s, ic_name: %s.", zlog_tp_dev.device_name, zlog_tp_dev.ic_name);
#endif
	kernel_log_d("end");
}
