#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include "bl_ts.h"
#include "bl_test.h"

#define MAX_FILE_NAME_LEN       64
#define MAX_FILE_PATH_LEN  64
#define MAX_NAME_LEN_20  20

char btl_vendor_name[MAX_NAME_LEN_20] = { 0 };
char btl_firmware_name[MAX_FILE_NAME_LEN] = {0};
char btl_ini_filename[MAX_FILE_NAME_LEN] = {0};
int btl_vendor_id = 0;
int blt_tptest_result = 0;
extern void btl_ts_resume(struct btl_ts_data *ts);
extern int btl_exit_sleep(struct btl_ts_data *ts);
extern int btl_ts_suspend(struct btl_ts_data *ts);
extern void btl_irq_enable(struct btl_ts_data *ts);
extern void btl_irq_disable(struct btl_ts_data *ts);
extern int btl_update_fw(unsigned char fileType, unsigned char ctpType, unsigned char *pFwData,
			 unsigned int fwLen);

struct tpvendor_t btl_vendor_l[] = {
	{BTL_VENDOR_ID_0, BTL_VENDOR_0_NAME},
	{BTL_VENDOR_ID_1, BTL_VENDOR_1_NAME},
	{BTL_VENDOR_ID_2, BTL_VENDOR_2_NAME},
	{BTL_VENDOR_ID_3, BTL_VENDOR_3_NAME},
	{VENDOR_END, "Unknown"},
};

int btl_get_fw(void)
{
	int i = 0;
	int ret = 0;

	for (i = 0; i < ARRAY_SIZE(btl_vendor_l); i++) {
		if (strnstr(lcd_name, btl_vendor_l[i].vendor_name, strlen(lcd_name))) {
			btl_vendor_id = btl_vendor_l[i].vendor_id;
			strlcpy(btl_vendor_name, btl_vendor_l[i].vendor_name,
				sizeof(btl_vendor_name));
			ret = 0;
			goto out;
		}
	}
	strlcpy(btl_vendor_name, "Unknown", sizeof(btl_vendor_name));
	ret = -EIO;
out:
	snprintf(btl_firmware_name, sizeof(btl_firmware_name),
			"btl_firmware_%s.bin", btl_vendor_name);
	snprintf(btl_ini_filename, sizeof(btl_ini_filename),
			"btl_self_test_%s.ini", btl_vendor_name);
	return ret;
}

static int tpd_init_tpinfo(struct ztp_device *cdev)
{
	unsigned char fwVer[3] = {0x00, 0x00, 0x00};
	int ret = 0;

	BTL_DEBUG("tpd_init_tpinfo\n");
	btl_i2c_lock();
	if ((CTP_TYPE == SELF_CTP) || (CTP_TYPE == SELF_INTERACTIVE_CTP)) {
		ret = btl_get_fwArgPrj_id(fwVer);
		BTL_DEBUG("fwVer:%d\nargVer:%d\n",fwVer[0],fwVer[1]);
	} else {
		ret = btl_get_prj_id(fwVer);
		BTL_DEBUG("fwVer:%d\n", fwVer[0]);
	}
	btl_i2c_unlock();
	if(ret<0) {
		BTL_DEBUG("Read Version FAIL");
	}
	snprintf(cdev->ic_tpinfo.vendor_name, sizeof(cdev->ic_tpinfo.vendor_name), btl_vendor_name);
	snprintf(cdev->ic_tpinfo.tp_name, sizeof(cdev->ic_tpinfo.tp_name), "Betterlife_Ts");

	cdev->ic_tpinfo.chip_model_id = TS_CHIP_BTL;
	cdev->ic_tpinfo.firmware_ver = fwVer[0];
	cdev->ic_tpinfo.i2c_addr = 0x2c;
	return 0;
}

int  btl_tp_suspend(void *btl_data)
{
	struct btl_ts_data *ts = (struct btl_ts_data *)btl_data;

	btl_ts_suspend(ts);
	return 0;
}

int  btl_tp_resume(void *btl_data)
{
	struct btl_ts_data *ts = (struct btl_ts_data *)btl_data;

	btl_exit_sleep(ts);
	return 0;
}

#ifdef BTL_UPDATE_FIRMWARE_WITH_REQUEST_FIRMWARE
static void btl_fw_upgrade_work_func(struct work_struct *work)
{
	int ret = 0;

	BTL_DEBUG_FUNC();
	btl_i2c_lock();
	ret = btl_update_firmware_via_request_firmware();
	btl_i2c_unlock();
	if (ret < 0) {
		BTL_ERROR("Create update thread error.");
	}
}

int btl_touch_fw_update_check(void)
{
	int ret = 0;

	g_btl_ts->btl_fw_upgrade_wq = create_singlethread_workqueue("btl_fw_wq");
	if (!g_btl_ts->btl_fw_upgrade_wq) {
		BTL_ERROR("Creat workqueue failed.");
		ret = -EINVAL;
	} else {
		INIT_DELAYED_WORK(&g_btl_ts->fw_work, btl_fw_upgrade_work_func);
		queue_delayed_work(g_btl_ts->btl_fw_upgrade_wq, &g_btl_ts->fw_work, msecs_to_jiffies(1000));
	}
	return ret;
}
#endif

static int btl_tp_fw_upgrade(struct ztp_device *cdev, char *fw_name, int fwname_len)
{
	int ret = 0;
	struct btl_ts_data *ts = g_btl_ts;
	unsigned char *pbt_buf = NULL;
	int fwsize = 0;

	if (cdev->tp_firmware == NULL || cdev->tp_firmware->data == NULL) {
		BTL_ERROR("cdev->tp_firmware is NULL");
		return -EIO;
	}

	pbt_buf = (unsigned char *)cdev->tp_firmware->data;
	fwsize = cdev->tp_firmware->size;

	btl_i2c_lock();
	btl_irq_disable(ts);
#if defined(BTL_ESD_PROTECT_SUPPORT)
	btl_esd_switch(ts, SWITCH_OFF);
#endif
#if defined(BTL_CHARGE_PROTECT_SUPPORT)
	btl_charge_switch(ts, SWITCH_OFF);
#endif

	/* ret = btl_fw_upgrade_with_bin_file(BTL_FIRMWARE_BIN_PATH); */
	ret = btl_update_fw(BIN_FILE_UPDATE, CTP_TYPE, pbt_buf, fwsize);
#if defined(BTL_CHARGE_PROTECT_SUPPORT)
	btl_charge_switch(ts, SWITCH_ON);
#endif
#if defined(BTL_ESD_PROTECT_SUPPORT)
	btl_esd_switch(ts, SWITCH_ON);
#endif
	btl_irq_enable(ts);
	btl_i2c_unlock();
	return ret;
}

#ifdef	BTL_FACTORY_TEST_EN
static int tpd_test_cmd_store(struct ztp_device *cdev)
{
	struct btl_ts_data *ts = g_btl_ts;

	if (ts->bl_is_suspend) {
		BTL_ERROR("In suspend, no test, return now");
		return -EINVAL;
	}
	blt_tptest_result = 0;
	btl_i2c_lock();
	btl_irq_disable(ts);

#if defined(BTL_ESD_PROTECT_SUPPORT)
	btl_esd_switch(ts, SWITCH_OFF);
#endif

#if defined(BTL_CHARGE_PROTECT_SUPPORT)
	btl_charge_switch(ts, SWITCH_OFF);
#endif

#if defined(RESET_PIN_WAKEUP)
	btl_ts_reset_wakeup();
#endif

	MDELAY(200);
	btl_test_entry(btl_ini_filename);

#if defined(RESET_PIN_WAKEUP)
	btl_ts_reset_wakeup();
#endif

#if defined(BTL_ESD_PROTECT_SUPPORT)
	btl_esd_switch(ts, SWITCH_ON);
#endif

#if defined(BTL_CHARGE_PROTECT_SUPPORT)
	btl_esd_switch(ts, SWITCH_ON);
#endif

	MDELAY(200);
	btl_irq_enable(ts);
	btl_i2c_unlock();
	return 0;

}

static int tpd_test_cmd_show(struct ztp_device *cdev, char *buf)
{
	ssize_t num_read_chars = 0;
	int i_len = 0;

	BTL_ERROR("%s:enter\n", __func__);
	i_len = snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d", blt_tptest_result, btl_ftest->sc_node.node_num, 0, 0);
	num_read_chars = i_len;
	return num_read_chars;
}
#endif

void blt_tpd_register_fw_class(void)
{
	BTL_DEBUG_FUNC();

	tpd_cdev->get_tpinfo = tpd_init_tpinfo;
	tpd_cdev->tp_data = g_btl_ts;
	tpd_cdev->tp_resume_func = btl_tp_resume;
	tpd_cdev->tp_suspend_func = btl_tp_suspend;
	tpd_cdev->tp_fw_upgrade = btl_tp_fw_upgrade;
#ifdef	BTL_FACTORY_TEST_EN
	tpd_cdev->tp_self_test = tpd_test_cmd_store;
	tpd_cdev->get_tp_self_test_result = tpd_test_cmd_show;
#endif
	tpd_cdev->max_x = g_btl_ts->TP_MAX_X;
	tpd_cdev->max_y = g_btl_ts->TP_MAX_Y;
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	zlog_tp_dev.device_name = btl_vendor_name;
	zlog_tp_dev.ic_name = "btl_tp";
	TPD_ZLOG("device_name:%s, ic_name: %s.", zlog_tp_dev.device_name, zlog_tp_dev.ic_name);
#endif
}
