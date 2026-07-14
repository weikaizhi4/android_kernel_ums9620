#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include "tlsc6x_main.h"
#include "ztp_common.h"
#include <linux/gpio.h>

#define TEST_TEMP_LENGTH 8
#define MAX_ALLOC_BUFF 256
#define TEST_PASS	0
#define TEST_BEYOND_MAX_LIMIT		0x0001
#define TEST_BEYOND_MIN_LIMIT		0x0002

#define TP_TEST_INIT		1
#define TP_TEST_START	2
#define TP_TEST_END		3
#define MAX_NAME_LEN_50  50
#define MAX_NAME_LEN_20  20
#define MAX_NODE_SIZE  48

#if defined(TLSC_TP_PROC_SELF_TEST)
extern char *g_tlsc_crtra_file;
extern unsigned short g_allch_num;
#endif
extern unsigned char real_suspend_flag;
extern void tlsc6x_tp_cfg_version(void);
extern int tlsc6x_do_suspend(void);
extern int tlsc6x_do_resume(void);

int tlsc6x_vendor_id = 0;
int tlsc6x_tptest_result = 0;
char tlsc6x_criteria_csv_name[MAX_NAME_LEN_50] = { 0 };
char tlsc6x_vendor_name[MAX_NAME_LEN_50] = { 0 };

struct tpvendor_t tlsc6x_vendor_l[] = {
	{0x030F, "YKL_YUYE"},
	{0x1B0A, "Lide_hxd"},
	{0x1b24, "huaxingda_24"},
	{0x1b26, "huaxingda_26"},
	{0x1b2a, "Huaxingda_2a"},
	{0x1111, "HLT"},
	{0x1912, "COE"},
	{0x191c, "COE_1c"},
	{0x191f, "COE_1f"},
	{0x1e29, "lead"},
	{0x1a47, "YKL_HONGZHAN_47"},
	{0x1D74, "dawosi_74"},
	{0x2704, "Jingtai_hfz"},
	{0x2806, "LCE_CTC"},
	{0x2809, "LCE_HS"},
	{0x280A, "LCE_BOE"},
	{0x280b, "LCE"},
	{0x2811, "LCE_lianchuang"},
	{0x2905, "YKL_SAIHUA"},
	{0x290c, "Skyworth_SAIHUA"},
	{0x2911, "YKL"},
	{0x2915, "YKL_SAIHUA_6440"},
	{0x2969, "YKL_SAIHUA69"},
	{0x4105, "dijing"},
	{0x411a, "YKL_1a"},
	{0x0000, "Unknown"},
};

typedef enum tlsc_chip_code {
	CHSCMIN  = 0,
	CHSC6440 = 6,
	CHSC6448 = 7,
	CHSC6540 = 13,
	CHSCMAX  = 1000,
} tlsc_chip_code_t;

static int tlsc6x_get_chip_vendor(int vid_pid)
{
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(tlsc6x_vendor_l); i++) {
		if ((tlsc6x_vendor_l[i].vendor_id == vid_pid) || (tlsc6x_vendor_l[i].vendor_id == 0X00)) {
			tlsc_info("vid_pid is 0x%x.\n", vid_pid);
			strlcpy(tlsc6x_vendor_name, tlsc6x_vendor_l[i].vendor_name, sizeof(tlsc6x_vendor_name));
			tlsc_info("tlsc6x_vendor_name: %s.\n", tlsc6x_vendor_name);
			break;
		}
	}
	return 0;
}

int tlsc6x_get_tp_vendor_info(void)
{
	unsigned int prject_id = 0;
	unsigned int vendor_sensor_id = 0;
	unsigned int vid_pid;

	prject_id = g_tlsc6x_cfg_ver & 0x1ff;
	vendor_sensor_id = (g_tlsc6x_cfg_ver >> 9) & 0x7f;
	vid_pid = (vendor_sensor_id << 8) | (prject_id & 0xff);

	tlsc_info("vendor_sensor_id =0x%x, prject_id=0x%x,vid_pid=0x%x\n", vendor_sensor_id, prject_id, vid_pid);
	tlsc6x_get_chip_vendor((int)vid_pid);
	snprintf(tlsc6x_criteria_csv_name, sizeof(tlsc6x_criteria_csv_name),
		"chsc_criteria_%s.bin", tlsc6x_vendor_name);
	g_tlsc_crtra_file = tlsc6x_criteria_csv_name;
	tlsc_info("g_tlsc_crtra_file=%s\n", g_tlsc_crtra_file);
	return 0;
}

static int tpd_init_tpinfo(struct ztp_device *cdev)
{

	unsigned int prject_id = 0;
	unsigned int vendor_sensor_id = 0;
	unsigned int vid_pid;
	unsigned int tp_name_id;

	tlsc_info("tpd_init_tpinfo\n");
	tlsc6x_tp_cfg_version();
	tlsc6x_set_nor_mode();
	strlcpy(cdev->ic_tpinfo.vendor_name, tlsc6x_vendor_name, sizeof(cdev->ic_tpinfo.vendor_name));
	/*if (g_mccode == 1) {
		snprintf(cdev->ic_tpinfo.tp_name, sizeof(cdev->ic_tpinfo.tp_name), "chsc6440");
	} else {
		snprintf(cdev->ic_tpinfo.tp_name, sizeof(cdev->ic_tpinfo.tp_name), "chsc6306");
	}*/
	tlsc_info("g_mccode=%d\n", g_mccode);

	tp_name_id = (g_tlsc6x_chip_code >> 8) & 0xf;
	tlsc_info("tp_name_id=%d\n", tp_name_id);
	switch (tp_name_id) {
		case CHSC6440:
			tlsc_info("tp_ic is chsc6440\n");
			snprintf(cdev->ic_tpinfo.tp_name, sizeof(cdev->ic_tpinfo.tp_name), "chsc6440");
			break;
		case CHSC6448:
			tlsc_info("tp_ic is chsc6448\n");
			snprintf(cdev->ic_tpinfo.tp_name, sizeof(cdev->ic_tpinfo.tp_name), "chsc6448");
			break;
		case CHSC6540:
			tlsc_info("tp_ic is chsc6540\n");
			snprintf(cdev->ic_tpinfo.tp_name, sizeof(cdev->ic_tpinfo.tp_name), "chsc6540");
			break;
		default:
			tlsc_err("tp_ic is unknown\n");
			snprintf(cdev->ic_tpinfo.tp_name, sizeof(cdev->ic_tpinfo.tp_name), "unknown");
			break;
	}

	cdev->ic_tpinfo.chip_model_id = TS_CHIP_TLSC;
	cdev->ic_tpinfo.firmware_ver = (g_tlsc6x_boot_ver >> 8) & 0xff;
	cdev->ic_tpinfo.config_ver = (g_tlsc6x_cfg_ver >> 26) & 0x3f;

	prject_id = g_tlsc6x_cfg_ver & 0x1ff;
	vendor_sensor_id = (g_tlsc6x_cfg_ver >> 9) & 0x7f;
	vid_pid = (vendor_sensor_id << 8) | (prject_id & 0xff);
	cdev->ic_tpinfo.module_id = vid_pid;

	cdev->ic_tpinfo.i2c_addr = 0x2e;
	return 0;
}

static int tlsc6x_tp_fw_upgrade(struct ztp_device *cdev, char *fw_name, int fwname_len)
{

	return tlsc6x_proc_cfg_update(NULL, 1);
}

static int tlsc6x_tp_suspend_show(struct ztp_device *cdev)
{
	cdev->tp_suspend = real_suspend_flag;
	return cdev->tp_suspend;
}

static int tlsc6x_set_tp_suspend(struct ztp_device *cdev, u8 suspend_node, int enable)
{
	if (enable)
		change_tp_state(LCD_OFF);
	else
		change_tp_state(LCD_ON);
	return 0;
}

int tlsc6x_tp_suspend(void *tlsc6x_data)
{
	tlsc6x_do_suspend();
	return 0;
}

int tlsc6x_tp_resume(void *tlsc6x_data)
{
	tlsc6x_do_resume();
	return 0;
}

static int tpd_tlsc6x_shutdown(struct ztp_device *cdev)
{
	struct tlsc6x_platform_data *pdata = g_tp_drvdata->platform_data;

	TLSC_FUNC_ENTER();
	tlsc_info("disable irq");
	tlsc_irq_disable();
#ifdef CONFIG_TLSC_POINT_REPORT_CHECK
	cancel_delayed_work_sync(&g_tp_drvdata->point_report_check_work);
#endif
	gpio_direction_output(pdata->reset_gpio_number, 0);
	return 0;
}

static int tpd_test_cmd_store(struct ztp_device *cdev)
{
	int val = 0;

	tlsc_info("%s:enter\n", __func__);
	val = tlsc6x_chip_self_test();
	if (val == 0) {
		tlsc6x_tptest_result = TEST_PASS;
		tlsc_info("Self_Test Pass\n");
	} else if (val >= 0x01 && val <= 0x03) {
		tlsc6x_tptest_result = tlsc6x_tptest_result | TEST_BEYOND_MAX_LIMIT | TEST_BEYOND_MIN_LIMIT;
		tlsc_err("Self_Test Fail\n");
	} else {
		tlsc6x_tptest_result = -EIO;
		tlsc_err("self test data init Fail\n");
	}
	return 0;
}

static int tpd_test_cmd_show(struct ztp_device *cdev, char *buf)
{
	int i_len;
	ssize_t num_read_chars = 0;

	tlsc_info("%s:enter\n", __func__);
	i_len = snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d", tlsc6x_tptest_result, 0, (g_allch_num&0xff), 0);
	tlsc_info("tpd test result:%d.\n", tlsc6x_tptest_result);
	tlsc_info("tpd  test:%s.\n", buf);
	num_read_chars = i_len;
	return num_read_chars;
}

void tlsc6x_tpd_register_fw_class(void)
{
	tlsc_info("tpd_register_fw_class\n");

	tpd_cdev->get_tpinfo = tpd_init_tpinfo;
	tpd_cdev->tp_fw_upgrade = tlsc6x_tp_fw_upgrade;
	tpd_cdev->tp_suspend_show = tlsc6x_tp_suspend_show;
	tpd_cdev->set_tp_suspend = tlsc6x_set_tp_suspend;
	tpd_cdev->tp_self_test = tpd_test_cmd_store;
	tpd_cdev->get_tp_self_test_result = tpd_test_cmd_show;
	tpd_cdev->tp_data = g_tp_drvdata;
	tpd_cdev->tp_resume_func = tlsc6x_tp_resume;
	tpd_cdev->tp_suspend_func = tlsc6x_tp_suspend;
	tpd_cdev->tpd_shutdown = tpd_tlsc6x_shutdown;

	tpd_cdev->max_x = g_tp_drvdata->platform_data->x_res_max;
	tpd_cdev->max_y = g_tp_drvdata->platform_data->y_res_max;
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	zlog_tp_dev.device_name = tlsc6x_vendor_name;
	zlog_tp_dev.ic_name = "tlsc_tp";
	TPD_ZLOG("device_name:%s, ic_name: %s.", zlog_tp_dev.device_name, zlog_tp_dev.ic_name);
#endif
}
