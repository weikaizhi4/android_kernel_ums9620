/***********************
 * file : ztp_core.c
 **********************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include "ztp_core.h"
#include "ztp_common.h"

struct ztp_device *tpd_cdev = NULL;
struct proc_dir_entry *tpd_proc_dir = NULL;
char lcd_name[MAX_LCD_NAME_LEN] = { 0 };

struct tp_ic_vendor_info tp_ic_vendor_info_l[] = {
	{TS_CHIP_SYNAPTICS, "synaptics"},
	{TS_CHIP_FOCAL, "focal"},
	{TS_CHIP_GOODIX, "goodix"	},
	{TS_CHIP_HIMAX, "himax"},
	{TS_CHIP_NOVATEK, "novatek"},
	{TS_CHIP_ILITEK, "ilitek"},
	{TS_CHIP_TLSC, "tlsc"},
	{TS_CHIP_CHIPONE, "chipone"},
	{TS_CHIP_GCORE, "galaxycore"},
	{TS_CHIP_OMNIVISION, "omnivision"},
	{TS_CHIP_BTL, "btltp"},
	{TS_CHIP_SEMI, "semi"},
	{TS_CHIP_SITRONIX,"sitronix"},
	{TS_CHIP_MAX, "Unknown"},
};

struct ztp_algo_info ztp_algo_info_l[] = {
	{zte_algo_enable, "algo_open"},
	{tp_jitter_check_pixel, "jitter_pixel"},
	{tp_jitter_timer, "jitter_timer"},
	{tp_edge_click_suppression_pixel, "click_pixel"},
	{tp_long_press_enable, "long_press_open"},
	{tp_long_press_timer, "long_press_timer"},
	{tp_long_press_pixel, "long_press_pixel"},
};

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
struct zlog_mod_info zlog_tp_dev = {
	.module_no = ZLOG_MODULE_TP,
	.name = "touchscreen",
	.device_name = "Unknown",
	.ic_name = "Unknown",
	.module_name = "TP",
	.fops = NULL,
};
#endif

int get_tp_algo_item_id(char *buf)
{
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(ztp_algo_info_l); i++) {
		if (strnstr(buf, ztp_algo_info_l[i].ztp_algo_item_name, strlen(buf))) {
			TPD_DMESG("%s: ztp_algo_item_id:%d.\n", __func__, ztp_algo_info_l[i].ztp_algo_item_id);
			return ztp_algo_info_l[i].ztp_algo_item_id;
		}
	}
	return -EIO;
}

int get_tp_chip_id(void)
{
	int i = 0;
	struct ztp_device *cdev = tpd_cdev;

	TPD_DMESG("%s:\n", __func__);
	cdev->tp_chip_id = TS_CHIP_MAX;
	TPD_DMESG("%s: lcd_name %s.\n", __func__, lcd_name);
	for (i = 0; i < ARRAY_SIZE(tp_ic_vendor_info_l); i++) {
		if (strnstr(lcd_name, tp_ic_vendor_info_l[i].tp_ic_vendor_name, strlen(lcd_name))) {
			cdev->tp_chip_id = tp_ic_vendor_info_l[i].tp_chip_id;
			TPD_DMESG("%s: tp_chip_id is 0x%02x.\n", __func__, cdev->tp_chip_id);
			return 0;
		}
	}
	return -EIO;
}

int get_lcd_panel_name(void)
{
	struct device_node *cmdline_node;
	const char *cmd_line, *lcd_name_p;
	int ret= 0;

	memset(lcd_name, 0, sizeof(lcd_name));
	cmdline_node = of_find_node_by_path("/chosen");
	ret = of_property_read_string(cmdline_node, "bootargs", &cmd_line);
	if (!ret) {
		lcd_name_p = strstr(cmd_line, "lcd_name=");
		if (lcd_name_p) {
			sscanf(lcd_name_p, "lcd_name=%s",lcd_name);
			TPD_DMESG("%s:lcd name: %s\n", __func__, lcd_name);
		}
	} else {
		snprintf(lcd_name, sizeof(lcd_name), "Unknown_lcd");
		TPD_DMESG("%s:can't not parse bootargs property\n", __func__);
	}
	return ret;
}

void tp_free_tp_firmware_data(void)
{
	struct ztp_device *cdev = tpd_cdev;

	if (cdev->tp_firmware != NULL) {
		if (cdev->tp_firmware->data != NULL) {
			vfree(cdev->tp_firmware->data);
			cdev->tp_firmware->data = NULL;
			cdev->tp_firmware->size = 0;
		}
		kfree(cdev->tp_firmware);
		cdev->tp_firmware = NULL;
	}
	cdev->fw_data_pos = 0;
}


int tp_alloc_tp_firmware_data(int buf_size)
{
	struct ztp_device *cdev = tpd_cdev;

	tp_free_tp_firmware_data();
	cdev->tp_firmware = kzalloc(sizeof(struct firmware), GFP_KERNEL);
	if (cdev->tp_firmware == NULL) {
		TPD_DMESG("alloc struct firmware failed");
		return -ENOMEM;
	}
	cdev->tp_firmware->data = vmalloc(sizeof(*cdev->tp_firmware) + buf_size);
	if (!cdev->tp_firmware->data) {
		TPD_DMESG("alloc tp_firmware->data failed");
		kfree(cdev->tp_firmware);
		return -ENOMEM;
	}
	cdev->tp_firmware->size = buf_size;
	memset((char *)cdev->tp_firmware->data, 0x00, sizeof(*cdev->tp_firmware) + buf_size);
	return 0;
}

int  tpd_copy_to_tp_firmware_data(char *buf)
{
	struct ztp_device *cdev = tpd_cdev;
	int len = 0;

	if (!cdev->tp_firmware || !cdev->tp_firmware->data) {
		TPD_DMESG("Need set fw image size first");
		return -ENOMEM;
	}

	if (cdev->tp_firmware->size == 0) {
		TPD_DMESG("Invalid firmware size");
		return -EINVAL;
	}

	if (cdev->fw_data_pos >= cdev->tp_firmware->size)
		return 0;
	len = strlen(buf);
	if (cdev->fw_data_pos + len > cdev->tp_firmware->size)
		len = cdev->tp_firmware->size - cdev->fw_data_pos;
	memcpy((u8 *)&cdev->tp_firmware->data[cdev->fw_data_pos], buf, len);
	cdev->fw_data_pos += len;
	return len;
}

void  tpd_reset_fw_data_pos_and_size(void)
{
	struct ztp_device *cdev = tpd_cdev;

	cdev->tp_firmware->size = cdev->fw_data_pos;
	cdev->fw_data_pos = 0;
}
static ssize_t tp_module_info_read(struct file *file,
	char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t buffer_tpd[200];
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->get_tpinfo) {
	cdev->get_tpinfo(cdev);
	}

	len += snprintf(buffer_tpd + len, sizeof(buffer_tpd) - len, "TP module: %s(0x%x)\n",
			cdev->ic_tpinfo.vendor_name, cdev->ic_tpinfo.module_id);
	len += snprintf(buffer_tpd + len, sizeof(buffer_tpd) - len, "IC type : %s\n",
			cdev->ic_tpinfo.tp_name);
	if (cdev->ic_tpinfo.i2c_addr)
		len += snprintf(buffer_tpd + len, sizeof(buffer_tpd) - len, "I2C address: 0x%x\n",
			cdev->ic_tpinfo.i2c_addr);
	if (cdev->ic_tpinfo.spi_num)
		len += snprintf(buffer_tpd + len, sizeof(buffer_tpd) - len, "Spi num: %d\n",
			cdev->ic_tpinfo.spi_num);
	len += snprintf(buffer_tpd + len, sizeof(buffer_tpd) - len, "Firmware version : 0x%x\n",
			cdev->ic_tpinfo.firmware_ver);
	if (cdev->ic_tpinfo.config_ver)
		len += snprintf(buffer_tpd + len, sizeof(buffer_tpd) - len, "Config version:0x%x\n",
			cdev->ic_tpinfo.config_ver);
	if (cdev->ic_tpinfo.display_ver)
		len += snprintf(buffer_tpd + len, sizeof(buffer_tpd) - len, "Display version:0x%x\n",
			cdev->ic_tpinfo.display_ver);
	if (cdev->ic_tpinfo.chip_batch[0])
		len += snprintf(buffer_tpd + len, sizeof(buffer_tpd) - len, "Chip hard version:%s\n",
			cdev->ic_tpinfo.chip_batch);
	return simple_read_from_buffer(buffer, count, offset, buffer_tpd, len);
}

static ssize_t tp_wake_gesture_read(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->get_gesture) {
		cdev->get_gesture(cdev);
	}
	TPD_DMESG("%s val:%d.\n", __func__, cdev->b_gesture_enable);

	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->b_gesture_enable);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t tp_wake_gesture_write(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;
	input = input > 0 ? 1 : 0;
	TPD_DMESG("%s val %d.\n", __func__, input);
	if (cdev->wake_gesture) {
		cdev->wake_gesture(cdev, input);
	}
	return len;
}
static ssize_t tp_smart_cover_read(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->get_smart_cover) {
		cdev->get_smart_cover(cdev);
	}
	TPD_DMESG("%s val:%d.\n", __func__, cdev->b_smart_cover_enable);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->b_smart_cover_enable);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t tp_smart_cover_write(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;
	input = input > 0 ? 1 : 0;
	TPD_DMESG("%s val %d.\n", __func__, input);
	if (cdev->set_smart_cover) {
		cdev->set_smart_cover(cdev, input);
	}
	return len;
}
static ssize_t tp_glove_read(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->get_glove_mode) {
		cdev->get_glove_mode(cdev);
	}
	TPD_DMESG("%s val:%d.\n", __func__, cdev->b_glove_enable);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->b_glove_enable);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t tp_glove_write(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;
	input = input > 0 ? 1 : 0;
	TPD_DMESG("%s val %d.\n", __func__, input);
	if (cdev->set_glove_mode) {
		cdev->set_glove_mode(cdev, input);
	}
	return len;
}
static ssize_t tpfwupgrade_store(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int fw_size = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &fw_size);
	if (ret)
		return -EINVAL;
	TPD_DMESG("%s val %d.\n", __func__, fw_size);
	mutex_lock(&cdev->cmd_mutex);
	if (fw_size > 10) {
		if (cdev->tp_firmware != NULL) {
			if (cdev->tp_firmware->data != NULL)
				vfree(cdev->tp_firmware->data);
			kfree(cdev->tp_firmware);
		}
		cdev->fw_data_pos = 0;
		cdev->tp_firmware = kzalloc(sizeof(struct firmware), GFP_KERNEL);
		if (cdev->tp_firmware == NULL) {
			TPD_DMESG("alloc struct firmware failed");
			mutex_unlock(&cdev->cmd_mutex);
			return -ENOMEM;
		}
		cdev->tp_firmware->data = vmalloc(sizeof(*cdev->tp_firmware) + fw_size);
		if (!cdev->tp_firmware->data) {
			TPD_DMESG("alloc tp_firmware->data failed");
			kfree(cdev->tp_firmware);
			mutex_unlock(&cdev->cmd_mutex);
			return -ENOMEM;
		}
		cdev->tp_firmware->size = fw_size;
		memset((char *)cdev->tp_firmware->data, 0x00, sizeof(*cdev->tp_firmware) + fw_size);
	} else if (cdev->tp_firmware != NULL) {
		if (cdev->tp_fw_upgrade) {
			cdev->tp_fw_upgrade(cdev, NULL, 0);
		}
		if (cdev->tp_firmware->data != NULL) {
			vfree(cdev->tp_firmware->data);
			cdev->tp_firmware->data = NULL;
		}
		kfree(cdev->tp_firmware);
		cdev->tp_firmware = NULL;
		cdev->fw_data_pos = 0;
	}
	mutex_unlock(&cdev->cmd_mutex);
	return len;
}

static ssize_t suspend_show(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[30] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->tp_suspend_show) {
		cdev->tp_suspend_show(cdev);
	}
	TPD_DMESG("%s val:%d.\n", __func__, cdev->tp_suspend);
	len = snprintf(data_buf, sizeof(data_buf), "tp suspend is: %u\n", cdev->tp_suspend);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t suspend_store(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;
	input = input > 0 ? 1 : 0;
	TPD_DMESG("%s val %d.\n", __func__, input);

	mutex_lock(&cdev->cmd_mutex);
	if (cdev->sys_set_tp_suspend_flag == input) {
		TPD_DMESG("%s tp state don't need change.\n", __func__);
		mutex_unlock(&cdev->cmd_mutex);
		return len;
	}
	cdev->sys_set_tp_suspend_flag = input;
	if (cdev->set_tp_suspend) {
		cdev->set_tp_suspend(cdev, PROC_SUSPEND_NODE, input);
	}
	mutex_unlock(&cdev->cmd_mutex);
	return len;
}

static ssize_t tp_single_tap_read(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0)
		return 0;

	if (cdev->get_singletap)
		cdev->get_singletap(cdev);

	TPD_DMESG("%s val: %d.\n", __func__, cdev->b_single_tap_enable);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->b_single_tap_enable);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t tp_single_tap_write(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;

	input = input > 0 ? 5 : 0;
	TPD_DMESG("%s val = %d\n", __func__, input);

	if (cdev->set_singletap)
		cdev->set_singletap(cdev, input);

	return len;
}

static ssize_t tp_single_aod_read(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0)
		return 0;

	if (cdev->get_singleaod)
		cdev->get_singleaod(cdev);

	TPD_DMESG("%s val: %d.\n", __func__, cdev->b_single_aod_enable);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->b_single_aod_enable);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t tp_single_aod_write(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;

	input = input > 0 ? 5 : 0;
	TPD_DMESG("%s val = %d\n", __func__, input);

	if (cdev->set_singleaod)
		cdev->set_singleaod(cdev, input);

	return len;
}

static ssize_t tp_edge_report_limit_read(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	char *data_buf = NULL;
	int i = 0;
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0)
		return 0;
	data_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (data_buf == NULL) {
		TPD_DMESG("alloc data_buf failed");
		return -ENOMEM;
	}

	len += snprintf(data_buf + len, PAGE_SIZE - len, "#######################################");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "#######################################\n\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "algo_open, echo algo_open:1 > edge_report_limit\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "jitter_pixel, echo jitter_pixel:10 > edge_report_limit\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "jitter_timer, echo jitter_timer:100 > edge_report_limit\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "click_pixel, echo click_pixel:10 > edge_report_limit\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "long_press_open, echo long_press_open:1 > edge_report_limit\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "long_press_timer, echo long_press_timer:500 > edge_report_limit\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "pixel limit level,user setting. echo 5 > edge_report_limit\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "long_press_pixel, echo long_press_pixel:10,10,20,20 > edge_report_limit\n\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len,  "#######################################");
	len += snprintf(data_buf + len, PAGE_SIZE - len,  "#######################################\n\n");

	len += snprintf(data_buf + len, PAGE_SIZE - len, "algo_open:%5u\n", cdev->zte_tp_algo);
	len += snprintf(data_buf + len, PAGE_SIZE - len, "jitter_pixel:%5u\n", cdev->tp_jitter_check);
	len += snprintf(data_buf + len, PAGE_SIZE - len, "jitter_timer:%5u\n", cdev->tp_jitter_timer);
	len += snprintf(data_buf + len, PAGE_SIZE - len, "click_pixel:%5u\n", cdev->edge_click_sup_p);
	len += snprintf(data_buf + len, PAGE_SIZE - len, "long_press_open:%5u\n", cdev->edge_long_press_check);
	len += snprintf(data_buf + len, PAGE_SIZE - len, "long_press_timer:%5u\n", cdev->edge_long_press_timer);
	len += snprintf(data_buf + len, PAGE_SIZE - len, "pixel limit level:%5u\n", cdev->edge_limit_pixel_level);

	len += snprintf(data_buf + len, PAGE_SIZE - len, "click_pixel width:");
	for (i = 0; i < MAX_LIMIT_NUM; i++) {
		if (len >= (PAGE_SIZE - 5))
			break;
		len += snprintf(data_buf + len, PAGE_SIZE - len, "%5u", cdev->edge_report_limit[i]);
	}
	len += snprintf(data_buf + len, PAGE_SIZE - len, "\n long_press_pixel:");
	for (i = 0; i < MAX_LIMIT_NUM; i++) {
		if (len >= (PAGE_SIZE - 5))
			break;
		len += snprintf(data_buf + len, PAGE_SIZE - len, "%5u", cdev->long_pess_suppression[i]);
	}
	len += snprintf(data_buf + len, PAGE_SIZE - len, "\n");
	simple_read_from_buffer(buffer, count, offset, data_buf, len);
	kfree(data_buf);
	return len;
}

static ssize_t tp_edge_report_limit_write(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0, i = 0;
	u8 *token = NULL;
	char *cur = NULL;
	char buff[100] = { 0 };
	u16 count = 0;
	unsigned int  s_to_u8 = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;
	int algo_item = 0;

	len = (len < sizeof(buff)) ? len : sizeof(buff);
	if (buffer != NULL) {
		if (copy_from_user(buff, buffer, len)) {
			TPD_DMESG("Failed to copy data from user space\n");
			len = -EINVAL;
			goto out;
		}
	}
	algo_item = get_tp_algo_item_id(buff);
	if (algo_item < 0) {
		ret = kstrtouint_from_user(buffer, len, 10, &input);
		if (ret || input > 10)
			return -EINVAL;
		cdev->edge_limit_pixel_level = input;
		/* user set level  0-5: x_max x 1% increase
		     user set level  6-10:  x_max x 0.5% increase
		*/
		if (cdev->edge_limit_pixel_level <= 5)
			cdev->user_edge_limit[0] = cdev->max_x * cdev->edge_limit_pixel_level * 7 / 1000;
		else
			cdev->user_edge_limit[0] = cdev->max_x * 35  / 1000
				+ (cdev->max_x  * 4 / 1000) * (cdev->edge_limit_pixel_level - 5);
		cdev->user_edge_limit[1] = 0;
		TPD_DMESG("edge_limit_pixel_level = %d, limit[0,1] = [%d,%d]\n",
			cdev->edge_limit_pixel_level, cdev->user_edge_limit[0], cdev->user_edge_limit[1]);
	} else {
		cur = strchr(buff, ':');
		cur++;
		TPD_DMESG("cur = %s\n", cur);
		switch (algo_item) {
		case zte_algo_enable:
			ret = kstrtouint(cur, 10, &s_to_u8);
			if (ret == 0) {
				s_to_u8 = s_to_u8 > 0 ? 1 : 0;
				cdev->zte_tp_algo = s_to_u8;
				TPD_DMESG("zte_tp_algo = %d\n", cdev->zte_tp_algo);
			}
			break;
		case tp_jitter_check_pixel:
			ret = kstrtouint(cur, 10, &s_to_u8);
			if (ret == 0) {
				cdev->tp_jitter_check = s_to_u8;
				TPD_DMESG("tp_jitter_check = %d\n", cdev->tp_jitter_check);
			}
			break;
		case tp_jitter_timer:
			ret = kstrtouint(cur, 10, &s_to_u8);
			if (ret == 0) {
				cdev->tp_jitter_timer = s_to_u8;
				TPD_DMESG("tp_jitter_timer = %d\n", cdev->tp_jitter_timer);
			}
			break;
		case tp_edge_click_suppression_pixel:
			ret = kstrtouint(cur, 10, &s_to_u8);
			if (ret == 0) {
				cdev->edge_click_sup_p = s_to_u8;
				TPD_DMESG("tp_edge_click_suppression_pixel = %d\n", cdev->edge_click_sup_p);
				for (i = 0; i < 4; i++)
					cdev->edge_report_limit[i] = cdev->edge_click_sup_p;
			}
			break;
		case tp_long_press_enable:
			ret = kstrtouint(cur, 10, &s_to_u8);
			if (ret == 0) {
				s_to_u8 = s_to_u8 > 0 ? 1 : 0;
				cdev->edge_long_press_check = s_to_u8;
				TPD_DMESG("edge_long_press_check = %d\n", cdev->edge_long_press_check);
			}
			break;
		case tp_long_press_timer:
			ret = kstrtouint(cur, 10, &s_to_u8);
			if (ret == 0) {
				cdev->edge_long_press_timer = s_to_u8;
				TPD_DMESG("zte_tp_algo = %d\n", cdev->edge_long_press_timer);
			}
			break;
		case tp_long_press_pixel:
			while ((token = strsep(&cur, ",")) != NULL) {
			ret = kstrtouint(token, 10, &s_to_u8);
			if (ret == 0) {
				cdev->long_pess_suppression[count] = s_to_u8;
				TPD_DMESG("long_pess_suppression[%d] = %d\n", count, cdev->long_pess_suppression[count]);
				count++;
			}
			if (count >= MAX_LIMIT_NUM)
				break;
			}
			break;
		default:
			TPD_DMESG("ignore algo item");
			break;
		}
	}

out:
	return len;
}

static ssize_t get_one_key(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->get_one_key) {
		cdev->get_one_key(cdev);
	}
	TPD_DMESG("%s val:%d.\n", __func__, cdev->one_key_enable);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->one_key_enable);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t set_one_key(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;

	input = input > 0 ? 1 : 0;

	TPD_DMESG("%s val = %d\n", __func__, input);

	if (cdev->set_one_key) {
		cdev->set_one_key(cdev, input);
	}
	cdev->one_key_enable = input;
	return len;
}

static ssize_t get_play_game(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->get_play_game) {
		cdev->get_play_game(cdev);
	}
	TPD_DMESG("%s val:%d.\n", __func__, cdev->play_game_enable);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->play_game_enable);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t set_play_game(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;

	/*input = input > 0 ? 1 : 0;*/

	TPD_DMESG("%s val = %d\n", __func__, input);

	if (cdev->set_play_game) {
		cdev->set_play_game(cdev, input);
	}

	return len;
}

static ssize_t get_tp_report_rate(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->get_tp_report_rate) {
		cdev->get_tp_report_rate(cdev);
	}
	TPD_DMESG("%s val:%d.\n", __func__, cdev->tp_report_rate);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->tp_report_rate);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t set_tp_report_rate(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;

	/*input = input > 0 ? 1 : 0;*/

	TPD_DMESG("%s val = %d\n", __func__, input);

	if (cdev->set_tp_report_rate) {
		cdev->set_tp_report_rate(cdev, input);
	}

	return len;
}

static ssize_t get_tp_noise_show(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	int retval = -1;
	uint8_t data_buf[30] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}

	mutex_lock(&cdev->cmd_mutex);
	if (cdev->get_noise)
		retval = cdev->get_noise(cdev);
	if (cdev->tp_firmware != NULL) {
		len = snprintf(data_buf, sizeof(data_buf), "%d\n", cdev->tp_firmware->size);
		TPD_DMESG("get tp noise size:%d.\n", cdev->tp_firmware->size);
	}

	mutex_unlock(&cdev->cmd_mutex);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t get_tp_noise_store(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	struct ztp_device *cdev = tpd_cdev;

	mutex_lock(&cdev->cmd_mutex);
	if (cdev->tp_firmware != NULL) {
		if (cdev->tp_firmware->data != NULL) {
			vfree(cdev->tp_firmware->data);
			cdev->tp_firmware->data = NULL;
		}
		kfree(cdev->tp_firmware);
		cdev->tp_firmware = NULL;
	}
	cdev->fw_data_pos = 0;
	mutex_unlock(&cdev->cmd_mutex);
	return len;
}

static ssize_t headset_state_show(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[30] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->headset_state_show) {
		cdev->headset_state_show(cdev);
	}
	TPD_DMESG("%s val:%d.\n", __func__, cdev->headset_state);
	len = snprintf(data_buf, sizeof(data_buf), "headset state: %u\n", cdev->headset_state);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t headset_state_store(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	char data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	len = len >= sizeof(data_buf) ? sizeof(data_buf) - 1 : len;
	ret = copy_from_user(data_buf, buffer, len);
	if (ret)
		return -EINVAL;
	ret = kstrtouint(data_buf, 0, &input);
	if (ret)
		return -EINVAL;
	input = input > 0 ? 1 : 0;
	TPD_DMESG("headset_state: %s val %d.\n", __func__, input);
	if (cdev->set_headset_state) {
		cdev->set_headset_state(cdev, input);
	}
	return len;
}

static ssize_t display_rotation_show(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[30] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}

	TPD_DMESG("%s val:%d.\n", __func__, cdev->display_rotation);
	len = snprintf(data_buf, sizeof(data_buf), "display rotation: %d\n", cdev->display_rotation);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t set_display_rotation(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	char data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	len = len >= sizeof(data_buf) ? sizeof(data_buf) - 1 : len;
	ret = copy_from_user(data_buf, buffer, len);
	if (ret)
		return -EINVAL;
	ret = kstrtouint(data_buf, 0, &input);
	if (ret)
		return -EINVAL;
	cdev->display_rotation = input;
	TPD_DMESG("display rotation: %s val %d.\n", __func__, cdev->display_rotation);
	if (cdev->set_display_rotation) {
		cdev->set_display_rotation(cdev, input);
	}
	return len;
}

static ssize_t tp_sensibility_level_read(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->get_sensibility) {
		cdev->get_sensibility(cdev);
	}
	TPD_DMESG("%s:ensibility level:val %d.\n", __func__, cdev->sensibility_level);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->sensibility_level);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t tp_sensibility_level_write(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	char data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	len = len >= sizeof(data_buf) ? sizeof(data_buf) - 1 : len;
	ret = copy_from_user(data_buf, buffer, len);
	if (ret)
		return -EINVAL;

	ret = kstrtouint(data_buf, 0, &input);
	if (ret)
		return -EINVAL;

	cdev->sensibility_level = input;
	TPD_DMESG("%s:ensibility level:val %d.\n", __func__, cdev->sensibility_level);
	if (cdev->set_sensibility) {
		cdev->set_sensibility(cdev, input);
	}

	return len;
}

static ssize_t tp_pen_only_read(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->get_pen_only_mode) {
		cdev->get_pen_only_mode(cdev);
	}
	TPD_DMESG("%s:pen only model: %d.\n", __func__, cdev->pen_only_mode);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->pen_only_mode);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t tp_pen_only_write(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	char data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	len = len >= sizeof(data_buf) ? sizeof(data_buf) - 1 : len;
	ret = copy_from_user(data_buf, buffer, len);
	if (ret)
		return -EINVAL;

	ret = kstrtouint(data_buf, 0, &input);
	if (ret)
		return -EINVAL;
       input = input > 0 ? 1 : 0;
	cdev->pen_only_mode = input;
	TPD_DMESG("%s:pen only mode:%d.\n", __func__, cdev->pen_only_mode);
	if (cdev->set_pen_only_mode) {
		cdev->set_pen_only_mode(cdev, input);
	}

	return len;
}

static ssize_t tp_self_test_read(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	int len = 0;
	char *data_buf = NULL;
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0)
		return 0;
	data_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (data_buf == NULL) {
		TPD_DMESG("alloc data_buf failed");
		return -ENOMEM;
	}

	if (*offset != 0) {
		return 0;
	}
	if (cdev->get_tp_self_test_result) {
		len = cdev->get_tp_self_test_result(cdev, data_buf);
	}
	simple_read_from_buffer(buffer, count, offset, data_buf, len);
	kfree(data_buf);
	tp_free_tp_firmware_data();
	return len;
}

static ssize_t tp_self_test_write(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	struct ztp_device *cdev = tpd_cdev;

	if(tp_alloc_tp_firmware_data(TP_TEST_FILE_SIZE)) {
		TPD_DMESG(" alloc tp firmware data fail");
		return -ENOMEM;
	}
	if (cdev->tp_self_test) {
		cdev->tp_self_test(cdev);
	}
	tpd_reset_fw_data_pos_and_size();
	return len;
}

static ssize_t get_finger_lock_flag(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	TPD_DMESG("%s val:%d.\n", __func__, cdev->finger_lock_flag);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->finger_lock_flag);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t set_finger_lock_flag(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;

	input = input > 0 ? 1 : 0;

	TPD_DMESG("%s val = %d\n", __func__, input);
	cdev->finger_lock_flag = input;
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	if (cdev->finger_lock_flag) {
		if (ufp_tp_ops.aod_fp_down && ufp_tp_ops.wait_completion) {
			complete(&ufp_tp_ops.ufp_completion);
			ufp_tp_ops.aod_fp_down = false;
		}
	}
#endif
	return len;
}

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
static ssize_t tp_zlog_debug_read(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[50] = {0};

	if (*offset != 0) {
		return 0;
	}
	len = snprintf(data_buf, sizeof(data_buf), "this no use");
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t tp_zlog_debug_write(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	char data_buf[10] = {0};

	len = len >= sizeof(data_buf) ? sizeof(data_buf) - 1 : len;
	ret = copy_from_user(data_buf, buffer, len);
	if (ret)
		return -EINVAL;

	ret = kstrtouint(data_buf, 0, &input);
	if (ret)
		return -EINVAL;
  	switch (input) {
	case TP_I2C_R_ERROR_NO:
		tpd_zlog_record_notify(TP_I2C_R_ERROR_NO);
		break;
	case TP_I2C_W_ERROR_NO:
		tpd_zlog_record_notify(TP_I2C_W_ERROR_NO);
		break;
	case TP_SPI_R_ERROR_NO:
		tpd_zlog_record_notify(TP_SPI_R_ERROR_NO);
		break;
	case TP_SPI_W_ERROR_NO:
		tpd_zlog_record_notify(TP_SPI_W_ERROR_NO);
		break;
	case TP_CRC_ERROR_NO:
		tpd_zlog_record_notify(TP_CRC_ERROR_NO);
		break;
	case TP_FW_UPGRADE_ERROR_NO:
		tpd_zlog_record_notify(TP_FW_UPGRADE_ERROR_NO);
		break;
	case TP_REQUEST_FIRMWARE_ERROR_NO:
		tpd_zlog_record_notify(TP_REQUEST_FIRMWARE_ERROR_NO);
		break;
	case TP_ESD_CHECK_ERROR_NO:
		tpd_zlog_record_notify(TP_ESD_CHECK_ERROR_NO);
		break;
	case TP_PROBE_ERROR_NO:
		tpd_zlog_record_notify(TP_PROBE_ERROR_NO);
		break;
	case TP_SUSPEND_GESTURE_OPEN_NO:
		tpd_zlog_record_notify(TP_SUSPEND_GESTURE_OPEN_NO);
		break;
	default:
		break;
	}

	return len;
}

#endif

static const struct file_operations proc_ops_tp_module_Info = {
	.owner = THIS_MODULE,
	.read = tp_module_info_read,
};
static const struct file_operations proc_ops_wake_gesture = {
	.owner = THIS_MODULE,
	.read = tp_wake_gesture_read,
	.write = tp_wake_gesture_write,
};
static const struct file_operations proc_ops_smart_cover = {
	.owner = THIS_MODULE,
	.read = tp_smart_cover_read,
	.write = tp_smart_cover_write,
};

static const struct file_operations proc_ops_glove = {
	.owner = THIS_MODULE,
	.read = tp_glove_read,
	.write = tp_glove_write,
};

static const struct file_operations proc_ops_tpfwupgrade = {
	.owner = THIS_MODULE,
	.write = tpfwupgrade_store,
};

static const struct file_operations proc_ops_suspend = {
	.owner = THIS_MODULE,
	.read = suspend_show,
	.write = suspend_store,
};

static const struct file_operations proc_ops_headset_state = {
	.owner = THIS_MODULE,
	.read = headset_state_show,
	.write = headset_state_store,
};

static const struct file_operations proc_ops_mrotation = {
	.owner = THIS_MODULE,
	.read = display_rotation_show,
	.write = set_display_rotation,
};

static const struct file_operations proc_ops_single_tap = {
	.owner = THIS_MODULE,
	.read = tp_single_tap_read,
	.write = tp_single_tap_write,
};

static const struct file_operations proc_ops_single_aod = {
	.owner = THIS_MODULE,
	.read = tp_single_aod_read,
	.write = tp_single_aod_write,
};

static const struct file_operations proc_ops_get_noise = {
	.owner = THIS_MODULE,
	.read = get_tp_noise_show,
	.write = get_tp_noise_store,
};

static const struct file_operations proc_ops_edge_report_limit = {
	.owner = THIS_MODULE,
	.read = tp_edge_report_limit_read,
	.write = tp_edge_report_limit_write,
};

static const struct file_operations proc_ops_onekey = {
	.owner = THIS_MODULE,
	.read = get_one_key,
	.write = set_one_key,
};

static const struct file_operations proc_ops_playgame = {
	.owner = THIS_MODULE,
	.read = get_play_game,
	.write = set_play_game,
};

static const struct file_operations proc_ops_tp_report_rate = {
	.owner = THIS_MODULE,
	.read = get_tp_report_rate,
	.write = set_tp_report_rate,
};

static const struct file_operations proc_ops_sensibility_level = {
	.owner = THIS_MODULE,
	.read = tp_sensibility_level_read,
	.write = tp_sensibility_level_write,
};

static const struct file_operations proc_ops_pen_only = {
	.owner = THIS_MODULE,
	.read = tp_pen_only_read,
	.write = tp_pen_only_write,
};

static const struct file_operations proc_ops_tp_self_test = {
	.owner = THIS_MODULE,
	.read = tp_self_test_read,
	.write = tp_self_test_write,
};

static const struct file_operations proc_ops_finger_lock_flag = {
	.owner = THIS_MODULE,
	.read = get_finger_lock_flag,
	.write = set_finger_lock_flag,
};

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
static const struct file_operations proc_ops_zlog_debug = {
	.owner = THIS_MODULE,
	.read = tp_zlog_debug_read,
	.write = tp_zlog_debug_write,
};
#endif

static void create_tpd_proc_entry(void)
{
	struct proc_dir_entry *tpd_proc_entry = NULL;

	TPD_DMESG(" %s, enter\n", __func__);
	tpd_proc_dir = proc_mkdir(PROC_TOUCH_DIR, NULL);
	if (tpd_proc_dir == NULL) {
		TPD_DMESG("%s: mkdir touchscreen failed!\n",  __func__);
		return;
	}
	tpd_proc_entry = proc_create(PROC_TOUCH_INFO, 0664, tpd_proc_dir, &proc_ops_tp_module_Info);
	if (tpd_proc_entry == NULL)
		TPD_DMESG("proc_create ts_information failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_WAKE_GESTURE, 0664,  tpd_proc_dir, &proc_ops_wake_gesture);
	if (tpd_proc_entry == NULL)
		TPD_DMESG("proc_create wake_gesture failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_SMART_COVER, 0664, tpd_proc_dir, &proc_ops_smart_cover);
	if (tpd_proc_entry == NULL)
		TPD_DMESG("proc_create smart_cover failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_GLOVE, 0664, tpd_proc_dir, &proc_ops_glove);
	if (tpd_proc_entry == NULL)
		TPD_DMESG("proc_create glove mode failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_FW_UPGRADE, 0664,  tpd_proc_dir, &proc_ops_tpfwupgrade);
	if (tpd_proc_entry == NULL)
		TPD_DMESG("proc_create FW_upgrade failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_SUSPEND, 0664,  tpd_proc_dir, &proc_ops_suspend);
	if (tpd_proc_entry == NULL)
		TPD_DMESG("proc_create suspend failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_HEADSET_STATE, 0664,  tpd_proc_dir, &proc_ops_headset_state);
	if (tpd_proc_entry == NULL)
		TPD_DMESG("proc_create headset_state failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_MROTATION, 0664,  tpd_proc_dir, &proc_ops_mrotation);
	if (tpd_proc_entry == NULL)
		TPD_DMESG("proc_create mRotation failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_TP_SINGLETAP, 0664, tpd_proc_dir, &proc_ops_single_tap);
	if (tpd_proc_entry == NULL)
		TPD_DMESG("proc_create single_tap failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_TP_SINGLEAOD, 0664, tpd_proc_dir, &proc_ops_single_aod);
	if (tpd_proc_entry == NULL)
		TPD_DMESG("proc_create single_aod failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_GET_NOISE, 0664, tpd_proc_dir, &proc_ops_get_noise);
	if (tpd_proc_entry == NULL)
		TPD_DMESG("proc_create get_noise failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_EDGE_REPORT_LIMIT, 0664, tpd_proc_dir, &proc_ops_edge_report_limit);
	if (tpd_proc_entry == NULL)
		TPD_DMESG("proc_create edge_report_limit failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_ONEKEY, 0664,  tpd_proc_dir, &proc_ops_onekey);
	if (tpd_proc_entry == NULL)
		TPD_DMESG("proc_create one_key failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_PLAY_GAME, 0664,  tpd_proc_dir, &proc_ops_playgame);
	if (tpd_proc_entry == NULL)
		TPD_DMESG("proc_create play_game failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_TP_REPORT_RATE, 0664,  tpd_proc_dir, &proc_ops_tp_report_rate);
	if (tpd_proc_entry == NULL)
		TPD_DMESG("proc_create tp report rate failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_SENSIBILITY, 0664, tpd_proc_dir, &proc_ops_sensibility_level);
	if (tpd_proc_entry == NULL)
		TPD_DMESG("proc_create sensilibity failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_PEN_ONLY, 0664, tpd_proc_dir, &proc_ops_pen_only);
	if (tpd_proc_entry == NULL)
		TPD_DMESG("proc_create pen only failed!\n");
		tpd_proc_entry = proc_create(PROC_TOUCH_TP_SELF_TEST, 0664, tpd_proc_dir, &proc_ops_tp_self_test);
	if (tpd_proc_entry == NULL)
		TPD_DMESG("proc_create tp self test failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_FINGER_LOCK_FLAG, 0664,  tpd_proc_dir, &proc_ops_finger_lock_flag);
	if (tpd_proc_entry == NULL)
		TPD_DMESG("proc_create finger_lock_flag failed!\n");
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	tpd_proc_entry = proc_create(PROC_ZLOG_DEBUG, 0664, tpd_proc_dir, &proc_ops_zlog_debug);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create zlog_debug failed!\n");
#endif

}

void tpd_proc_deinit(void)
{
	if (tpd_proc_dir == NULL) {
		TPD_DMESG("%s: proc/touchscreen is NULL!\n",  __func__);
		return;
	}
	remove_proc_entry(PROC_TOUCH_INFO, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_WAKE_GESTURE, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_SMART_COVER, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_GLOVE, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_FW_UPGRADE, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_SUSPEND, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_HEADSET_STATE, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_MROTATION, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_TP_SINGLETAP, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_TP_SINGLEAOD, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_GET_NOISE, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_EDGE_REPORT_LIMIT, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_ONEKEY, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_PLAY_GAME, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_TP_REPORT_RATE, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_SENSIBILITY, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_PEN_ONLY, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_TP_SELF_TEST, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_FINGER_LOCK_FLAG, tpd_proc_dir);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	remove_proc_entry(PROC_ZLOG_DEBUG, tpd_proc_dir);
#endif
	remove_proc_entry(PROC_TOUCH_DIR, NULL);
}

static ssize_t tpd_sysfs_fwimage_store(struct file *file,
		struct kobject *kobj, struct bin_attribute *attr,
		char *buf, loff_t pos, size_t count)
{
	struct ztp_device *cdev = tpd_cdev;

	if (!cdev->tp_firmware || !cdev->tp_firmware->data) {
		TPD_DMESG("Need set fw image size first");
		return -ENOMEM;
	}

	if (cdev->tp_firmware->size == 0) {
		TPD_DMESG("Invalid firmware size");
		return -EINVAL;
	}
	if (cdev->fw_data_pos >= cdev->tp_firmware->size) {
		cdev->fw_data_pos = 0;
		return -EINVAL;
	}
	if (cdev->fw_data_pos + count > cdev->tp_firmware->size)
		count = cdev->tp_firmware->size - cdev->fw_data_pos;
	TPD_DMESG("cdev->fw_data_pos: %d, count:%d\n", cdev->fw_data_pos, count);
	mutex_lock(&cdev->cmd_mutex);
	memcpy((u8 *)&cdev->tp_firmware->data[cdev->fw_data_pos], buf, count);
	cdev->fw_data_pos += count;
	mutex_unlock(&cdev->cmd_mutex);
	return count;
}

static ssize_t tpd_sysfs_fwimage_show(struct file *file,
		struct kobject *kobj, struct bin_attribute *attr,
		char *buf, loff_t pos, size_t count)
{
	struct ztp_device *cdev = tpd_cdev;

	if (!cdev->tp_firmware || !cdev->tp_firmware->data) {
		TPD_DMESG("Need set fw image size first");
		return -ENOMEM;
	}

	if (cdev->tp_firmware->size == 0) {
		TPD_DMESG("Invalid firmware size");
		return -EINVAL;
	}
	mutex_lock(&cdev->cmd_mutex);
	if (cdev->fw_data_pos >= cdev->tp_firmware->size) {
		cdev->fw_data_pos = 0;
		vfree(cdev->tp_firmware->data);
		cdev->tp_firmware->data = NULL;
		kfree(cdev->tp_firmware);
		cdev->tp_firmware = NULL;
		TPD_DMESG("tpd, tp_firmware free.\n");
		mutex_unlock(&cdev->cmd_mutex);
		return 0;
	}
	if (cdev->fw_data_pos + count > cdev->tp_firmware->size)
		count = cdev->tp_firmware->size - cdev->fw_data_pos;
	TPD_DMESG("cdev->fw_data_pos: %d, count:%d\n", cdev->fw_data_pos, count);
	memcpy(buf, (u8 *)&cdev->tp_firmware->data[cdev->fw_data_pos], count);
	cdev->fw_data_pos += count;
	mutex_unlock(&cdev->cmd_mutex);
	return count;
}

static const struct bin_attribute fwimage_attr = {
	.attr = {
		.name = "fwimage",
		.mode = 0666,
	},
	.size = 0,
	.write = tpd_sysfs_fwimage_store,
	.read = tpd_sysfs_fwimage_show,
};

static int tpd_fw_sysfs_init(void)
{
	int ret = 0;
	struct ztp_device *cdev = tpd_cdev;

	TPD_DMESG(" %s, enter\n", __func__);
	if (!cdev->zte_touch_pdev){
		TPD_DMESG("zte_touch_pdev is NULL.");
		return -EINVAL;
	}
	cdev->zte_touch_kobj = kobject_create_and_add("fwupdate",
					&cdev->zte_touch_pdev->dev.kobj);
	if (!cdev->zte_touch_kobj) {
		TPD_DMESG("failed create sub dir for fwupdate");
		return -EINVAL;
	}
	ret = sysfs_create_bin_file(cdev->zte_touch_kobj, &fwimage_attr);
	if (ret) {
		TPD_DMESG("failed create fwimage bin node, %d", ret);
		kobject_put(cdev->zte_touch_kobj);
	}

	return ret;
}

static void tpd_fw_sysfs_remove(void)
{
	struct ztp_device *cdev = tpd_cdev;

	if (!cdev->zte_touch_kobj)
		return;
	sysfs_remove_bin_file(cdev->zte_touch_kobj, &fwimage_attr);
	kobject_put(cdev->zte_touch_kobj);
}

int lcd_fps_notify(u8 lcd_fps)
{
	struct ztp_device *cdev = tpd_cdev;

	if (cdev->lcd_fps_notify) {
		return cdev->lcd_fps_notify(cdev,lcd_fps);
	}
	return 0;
}

static void tpd_report_uevent(u8 gesture_key)
{
	char *envp[2] = {NULL};
	struct ztp_device *cdev = tpd_cdev;

	switch (gesture_key) {
	case single_tap:
		TPD_DMESG("%s single tap gesture", __func__);
		envp[0] = "single_tap=true";
		break;
	case double_tap:
		TPD_DMESG("%s double tap gesture", __func__);
		envp[0] = "double_tap=true";
		break;
	case pen_low_batt:
		TPD_DMESG("%s pen low batt", __func__);
		envp[0] = "pen_capacity_low=true";
		break;
	default:
		TPD_DMESG("%s no such gesture key(%d)", __func__, gesture_key);
		return;
	}

	kobject_uevent_env(&(cdev->zte_touch_pdev->dev.kobj), KOBJ_CHANGE, envp);
}

int zte_touch_pdev_register(void)
{
	int ret = 0;
	struct ztp_device *cdev = tpd_cdev;

	TPD_DMESG("%s", __func__);
	cdev->zte_touch_pdev = platform_device_alloc("zte_touch", -1);
	if (!cdev->zte_touch_pdev) {
		TPD_DMESG("%s failed to allocate platform device", __func__);
		ret = -ENOMEM;
		goto alloc_failed;
	}

	ret = platform_device_add(cdev->zte_touch_pdev);
	if (ret < 0) {
		TPD_DMESG("%s failed to add platform device ret=%d", __func__, ret);
		goto register_failed;
	}

	cdev->tpd_report_uevent = tpd_report_uevent;
	return 0;

register_failed:
	platform_device_put(cdev->zte_touch_pdev);
alloc_failed:
	cdev->tpd_report_uevent = NULL;

	return ret;
}

void zte_touch_pdev_unregister(void)
{
	struct ztp_device *cdev = tpd_cdev;

	if (cdev->zte_touch_pdev) {
		TPD_DMESG("%s device put", __func__);
		platform_device_unregister(cdev->zte_touch_pdev);
	}
}

static int ztp_parse_dt(struct device_node *node, struct ztp_device *cdev)
{
	int ret = 0;
	int i = 0;
	u32 value = 0;

	if (!cdev) {
		TPD_DMESG("invalid tp_dev");
		return -EINVAL;
	}

	cdev->zte_tp_algo = of_property_read_bool(node, "zte,tp_algo");
	if (cdev->zte_tp_algo)
		TPD_DMESG("zte_tp_algo enabled");
	cdev->edge_long_press_check = of_property_read_bool(node, "zte,tp_long_press");
	if (cdev->edge_long_press_check) {
		TPD_DMESG("edge_long_press_check enabled");
		ret = of_property_read_u32(node, "zte,tp_long_press_timer", &value);
		if (!ret) {
			cdev->edge_long_press_timer = value;
			TPD_DMESG("tp_long_press_timer is %d", cdev->edge_long_press_timer);
		}
		ret = of_property_read_u32(node, "zte,tp_long_press_left_v", &value);
		if (!ret) {
			cdev->long_pess_suppression[0] = value;
			TPD_DMESG("tp_long_press_left_v is %d", cdev->long_pess_suppression[0]);
		}
		ret = of_property_read_u32(node, "zte,tp_long_press_right_v", &value);
		if (!ret) {
			cdev->long_pess_suppression[1] = value;
			TPD_DMESG("tp_long_press_right_v is %d", cdev->long_pess_suppression[1]);
		}
		ret = of_property_read_u32(node, "zte,tp_long_press_left_h", &value);
		if (!ret) {
			cdev->long_pess_suppression[2] = value;
			TPD_DMESG("tp_long_press_left_h is %d", cdev->long_pess_suppression[2]);
		}
		ret = of_property_read_u32(node, "zte,tp_long_press_right_h", &value);
		if (!ret) {
			cdev->long_pess_suppression[3] = value;
			TPD_DMESG("tp_long_press_right_h is %d", cdev->long_pess_suppression[3]);
		}
	}

	ret = of_property_read_u32(node, "zte,tp_jitter_check", &value);
	if (!ret) {
		cdev->tp_jitter_check = value;
		TPD_DMESG("tp_jitter_check is %d", cdev->tp_jitter_check);
		if (cdev->tp_jitter_check) {
			ret = of_property_read_u32(node, "zte,tp_jitter_timer", &value);
			if (!ret) {
				cdev->tp_jitter_timer = value;
				TPD_DMESG("tp_jitter_timer is %d", cdev->tp_jitter_timer);
			}
		}
	}
	ret = of_property_read_u32(node, "zte,tp_edge_click_suppression_pixel", &value);
	if (!ret) {
		cdev->edge_click_sup_p= value;
		TPD_DMESG("tp_edge_click_suppression_pixel is %d", cdev->edge_click_sup_p);
		for (i = 0; i < 4; i++)
			cdev->edge_report_limit[i] = cdev->edge_click_sup_p;
	}
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	cdev->ufp_enable = of_property_read_bool(node, "zte,ufp_enable");
	if (cdev->ufp_enable) {
		TPD_DMESG("ufp_enable enabled");
		ret = of_property_read_u32(node, "zte,ufp_circle_center_x", &value);
		if (!ret) {
			cdev->ufp_circle_center_x = value;
			TPD_DMESG("ufp_circle_center_x is %d", cdev->ufp_circle_center_x);
		}
		ret = of_property_read_u32(node, "zte,ufp_circle_center_y", &value);
		if (!ret) {
			cdev->ufp_circle_center_y = value;
			TPD_DMESG("ufp_circle_center_y is %d", cdev->ufp_circle_center_y);
		}
		ret = of_property_read_u32(node, "zte,ufp_circle_radius", &value);
		if (!ret) {
			cdev->ufp_circle_radius = value;
			TPD_DMESG("ufp_circle_radius is %d", cdev->ufp_circle_radius);
		}
	}
#endif
	return 0;
}

static void ztp_probe_work(struct work_struct *work)
{
#ifdef CONFIG_TOUCHSCREEN_ILITEK_TDDI_V3
	ilitek_plat_dev_init();
#endif
#ifdef CONFIG_TOUCHSCREEN_HIMAX_COMMON
	himax_common_init();
#endif
#ifdef CONFIG_TOUCHSCREEN_CHIPONE
	cts_i2c_driver_init();
#endif
#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_V2
	goodix_ts_core_init();
#endif
#ifdef CONFIG_TOUCHSCREEN_CHIPONE_V2
	cts_driver_init();
#endif
#ifdef CONFIG_TOUCHSCREEN_CHIPONE_V3
	cts_driver_init();
#endif
#ifdef CONFIG_TOUCHSCREEN_OMNIVISION_TCM
	ovt_tcm_module_init();
#endif
#if defined(CONFIG_TOUCHSCREEN_FTS_V3_3) || defined(CONFIG_TOUCHSCREEN_FTS_UFP)
	fts_ts_init();
#endif
#ifdef CONFIG_TOUCHSCREEN_CHSC5XXX
	semi_i2c_device_init();
#endif
#ifdef CONFIG_TOUCHSCREEN_TLSC6X_V3
	tlsc6x_init();
#endif
#if defined(CONFIG_TOUCHSCREEN_GCORE_TS) || defined(CONFIG_TOUCHSCREEN_GCORE_TS_V3)
	gcore_touch_driver_init();
#endif
#ifdef CONFIG_TOUCHSCREEN_BETTERLIFE_TS
	btl_ts_init();
#endif
#ifdef CONFIG_TOUCHSCREEN_SITRONIX_INCELL
	sitronix_ts_init();
#endif

}

void tpd_probe_work_init(void)
{
	struct ztp_device *cdev = tpd_cdev;

	TPD_DMESG("%s enter", __func__);
	INIT_DELAYED_WORK(&cdev->tpd_probe_work, ztp_probe_work);

}

void tpd_probe_work_deinit(void)
{
	struct ztp_device *cdev = tpd_cdev;

	TPD_DMESG("%s enter", __func__);
	cancel_delayed_work_sync(&cdev->tpd_probe_work);

}

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
void tpd_zlog_register(struct ztp_device *cdev)
{
	if (cdev->zlog_client) {
		TPD_ZLOG("ztp zlog already registered, no need register again!");
		return;
	}

	cdev->zlog_client = zlog_register_client(&zlog_tp_dev);
	if (!cdev->zlog_client) {
		TPD_ZLOG("%s zlog register client zlog_tp_dev fail\n", __func__);
	} else {
		cdev->ztp_zlog_buffer = vmalloc(ZLOG_INFO_LEN);
		if (!cdev->ztp_zlog_buffer) {
			TPD_ZLOG("ztp_zlog_buffer malloc fail");
			return;
		}
		memset(cdev->ztp_zlog_buffer, 0, ZLOG_INFO_LEN);
		if (cdev->ztp_probe_fail_chip_id != 0xFF) {
			tpd_print_zlog("tp probe fail, chip id:%d",cdev->ztp_probe_fail_chip_id);
			tpd_zlog_record_notify(TP_PROBE_ERROR_NO);
		}
	}
	cdev->zlog_regisered = true;
}

int tpd_zlog_check(zlog_error_no error_no)
{
	struct ztp_device *cdev = tpd_cdev;
	int ret = 0;

	if ((cdev->zlog_item.count[error_no] > 0)
		&& (jiffies_to_msecs(jiffies - cdev->zlog_item.timer[error_no])) < 60000) {
		TPD_ZLOG("zlog error repeated notify, timer:%d, no:%d",
			jiffies_to_msecs(jiffies - cdev->zlog_item.timer[error_no]) ,error_no);
		ret = -EIO;
	}
	cdev->zlog_item.count[error_no]++;
	return ret;
}

void tpd_zlog_record_notify(zlog_error_no error_no)
{
	struct ztp_device *cdev = tpd_cdev;
	int len = 0;
	unsigned long after_reset_time = 0;

	if(!cdev->zlog_regisered)
		tpd_zlog_register(cdev);

	if ((cdev->zlog_client == NULL) || (cdev->ztp_zlog_buffer == NULL)) {
		TPD_ZLOG("zlog unregistered.\n");
		return;
	}
	after_reset_time = jiffies_to_msecs(jiffies - cdev->tp_reset_timer);
	len = strlen(cdev->ztp_zlog_buffer);
	snprintf(cdev->ztp_zlog_buffer + len, ZLOG_INFO_LEN - len, " IC name: %s,module name:%s, Firmware version: 0x%x",
		zlog_tp_dev.ic_name, zlog_tp_dev.device_name, cdev->ic_tpinfo.firmware_ver);
	switch (error_no) {
	case TP_I2C_R_ERROR_NO:
		if ((tpd_zlog_check(error_no) < 0) || (after_reset_time < 200))
			break;

		cdev->zlog_item.timer[error_no] = jiffies;
		TPD_ZLOG("tpd i2c read err,count:%d. %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd i2c read err,count:%d\n %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		if (cdev->zlog_item.count[error_no] % 10)
			zlog_client_notify(cdev->zlog_client,  ZLOG_TP_I2C_R_WARN_NO);
		else
			zlog_client_notify(cdev->zlog_client,  ZLOG_TP_I2C_R_ERROR_NO);
		break;
	case TP_I2C_W_ERROR_NO:
		if ((tpd_zlog_check(error_no) < 0) || (after_reset_time < 200))
			break;
		cdev->zlog_item.timer[error_no] = jiffies;
		TPD_ZLOG("tpd i2c write err,count:%d. %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd i2c write err,count:%d.\n %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		if (cdev->zlog_item.count[error_no] % 10)
			zlog_client_notify(cdev->zlog_client,  ZLOG_TP_I2C_W_WARN_NO);
		else
			zlog_client_notify(cdev->zlog_client,  ZLOG_TP_I2C_W_ERROR_NO);
		break;
	case TP_SPI_R_ERROR_NO:
		if ((tpd_zlog_check(error_no) < 0) || (after_reset_time < 200))
			break;
		cdev->zlog_item.timer[error_no] = jiffies;
		TPD_ZLOG("tpd SPI read err,count:%d.%s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd SPI read err,count:%d\n %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		if (cdev->zlog_item.count[error_no] % 10)
			zlog_client_notify(cdev->zlog_client,  ZLOG_TP_SPI_R_WARN_NO);
		else
			zlog_client_notify(cdev->zlog_client,  ZLOG_TP_SPI_R_ERROR_NO);
		break;
	case TP_SPI_W_ERROR_NO:
		if ((tpd_zlog_check(error_no) < 0) || (after_reset_time < 200))
			break;
		cdev->zlog_item.timer[error_no] = jiffies;
		TPD_ZLOG("tpd SPI write err,count:%d.%s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd SPI write err,count:%d\n %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		if (cdev->zlog_item.count[error_no] % 10)
			zlog_client_notify(cdev->zlog_client,  ZLOG_TP_SPI_W_WARN_NO);
		else
			zlog_client_notify(cdev->zlog_client,  ZLOG_TP_SPI_W_ERROR_NO);
		break;
	case TP_CRC_ERROR_NO:
		if ((tpd_zlog_check(error_no) < 0) || (after_reset_time < 200))
			break;
		cdev->zlog_item.timer[error_no] = jiffies;
		TPD_ZLOG("tpd crc check err,count:%d. %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd crc check err,count:%d.\n %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_notify(cdev->zlog_client,  ZLOG_TP_CRC_ERROR_NO);
		break;
	case TP_FW_UPGRADE_ERROR_NO:
		if (tpd_zlog_check(error_no) < 0)
			break;
		cdev->zlog_item.timer[error_no] = jiffies;
		TPD_ZLOG("tpd firmware upgrade err,count:%d. %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd firmware upgrade err,count:%d. \n %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_notify(cdev->zlog_client,  ZLOG_TP_FW_UPGRADE_ERROR_NO);
		break;
	case TP_REQUEST_FIRMWARE_ERROR_NO:
		if (tpd_zlog_check(error_no) < 0)
			break;
		cdev->zlog_item.timer[error_no] = jiffies;
		TPD_ZLOG("tpd request firmware upgrade err,count:%d. %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd request firmware upgrade err,count:%d.\n %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_notify(cdev->zlog_client,  ZLOG_TP_FW_UPGRADE_ERROR_NO);
		break;
	case TP_ESD_CHECK_ERROR_NO:
		if (tpd_zlog_check(error_no) < 0)
			break;
		cdev->zlog_item.timer[error_no] = jiffies;
		TPD_ZLOG("tpd esd check err,count:%d. %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd esd check err,count:%d.\n %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		if (cdev->zlog_item.count[error_no] % 10)
			zlog_client_notify(cdev->zlog_client,  ZLOG_TP_ESD_CHECK_WARN_NO);
		else
			zlog_client_notify(cdev->zlog_client,  ZLOG_TP_ESD_CHECK_ERROR_NO);
		break;
	case TP_PROBE_ERROR_NO:
		TPD_ZLOG("tpd probe err. %s\n",cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd probe err.\n %s\n",cdev->ztp_zlog_buffer);
		zlog_client_notify(cdev->zlog_client,  ZLOG_TP_ESD_CHECK_ERROR_NO);
		break;
	case TP_SUSPEND_GESTURE_OPEN_NO:
		TPD_ZLOG("tpd gesture open when suspend. %s\n",cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd tp gesture open when suspend.\n %s\n",cdev->ztp_zlog_buffer);
		zlog_client_notify(cdev->zlog_client,  ZLOG_TP_SUSPEND_GESTURE_OPEN_NO);
		break;
	case TP_GHOST_ERROR_NO:
		if (tpd_zlog_check(error_no) < 0)
			break;
		cdev->zlog_item.timer[error_no] = jiffies;
		TPD_ZLOG("tpd ghost err,count:%d. %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd ghost err,count:%d.\n %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_notify(cdev->zlog_client,  ZLOG_TP_GHOST_ERROR_NO);
		break;
	default:
		break;
	}
	memset(cdev->ztp_zlog_buffer, 0, ZLOG_INFO_LEN);
}

static void zlog_register_work(struct work_struct *work)
{
	struct ztp_device *cdev = tpd_cdev;

	if(!cdev->zlog_regisered)
		tpd_zlog_register(cdev);
}

void zlog_register_work_init(void)
{
	struct ztp_device *cdev = tpd_cdev;

	TPD_DMESG("%s enter", __func__);
	INIT_DELAYED_WORK(&cdev->zlog_register_work, zlog_register_work);

}

void zlog_register_work_deinit(void)
{
	struct ztp_device *cdev = tpd_cdev;

	TPD_DMESG("%s enter", __func__);
	cancel_delayed_work_sync(&cdev->zlog_register_work);
	vfree(cdev->ztp_zlog_buffer);
	cdev->ztp_zlog_buffer = NULL;
}

void tpd_zlog_init(void)
{
	struct ztp_device *cdev = tpd_cdev;
	int i = 0;

	cdev->ztp_zlog_buffer = NULL;
	cdev->zlog_regisered = false;
	cdev->ztp_probe_fail_chip_id = 0xFF;
	cdev->tp_reset_timer = jiffies;
	for (i = 0; i < TP_ZLOG_ERROR_MAX; i++){
		cdev->zlog_item.timer[i] = jiffies;
	}
}
#endif

static bool tpd_get_charger_ststus(void)
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
	TPD_DMESG("charger status:%d", status);
	return status;
}

static void tpd_charger_detect_work(struct work_struct *work)
{
	struct ztp_device *cdev = tpd_cdev;

	cdev->charger_mode = tpd_get_charger_ststus();
	if(cdev->charger_state_notify)
		cdev->charger_state_notify(cdev);
}

static int tpd_charger_notify_call(struct notifier_block *nb, unsigned long event, void *data)
{
	struct ztp_device *cdev = tpd_cdev;
	struct power_supply *psy = data;

	if ((cdev== NULL) || (cdev->tpd_wq == NULL))
		return NOTIFY_DONE;
	if (event != PSY_EVENT_PROP_CHANGED) {
		return NOTIFY_DONE;
	}

	if ((strcmp(psy->desc->name, "usb") == 0)
	    || (strcmp(psy->desc->name, "ac") == 0)) {
		queue_delayed_work(cdev->tpd_wq, &cdev->charger_work, msecs_to_jiffies(500));
	}

	return NOTIFY_DONE;
}

static int tpd_init_charger_notifier(void)
{
	int ret = 0;
	struct ztp_device *cdev = tpd_cdev;

	TPD_DMESG("Init Charger notifier");

	cdev->charger_notifier.notifier_call = tpd_charger_notify_call;
	ret = power_supply_reg_notifier(&cdev->charger_notifier);
	return ret;
}

void tpd_charger_work_init(void)
{
	struct ztp_device *cdev = tpd_cdev;

	TPD_DMESG("%s enter", __func__);
	INIT_DELAYED_WORK(&cdev->charger_work, tpd_charger_detect_work);
	tpd_init_charger_notifier();
}

void tpd_charger_work_deinit(void)
{
	struct ztp_device *cdev = tpd_cdev;

	TPD_DMESG("%s enter", __func__);
	cancel_delayed_work_sync(&cdev->charger_work);
	power_supply_unreg_notifier(&cdev->charger_notifier);
}

static void send_cmd_work(struct work_struct *work)
{
	struct ztp_device *cdev = tpd_cdev;

	if (cdev->tpd_send_cmd)
		cdev->tpd_send_cmd(cdev);
}

static void tp_ghost_check_work(struct work_struct *work)
{
	struct ztp_device *cdev = tpd_cdev;

	if (tp_ghost_check()){
		TPD_DMESG("%s may be ghost point", __func__);
	}
	ghost_check_reset();
	cdev->start_ghost_check_timer = false;

}

int tpd_workqueue_init(void)
{
	struct ztp_device *cdev = tpd_cdev;

	TPD_DMESG("%s enter", __func__);
	cdev->tpd_wq = create_singlethread_workqueue("tpd_wq");

	if (!cdev->tpd_wq) {
		goto err_create_tpd_report_wq_failed;
	}
	if (tpd_report_work_init())
		goto err_tpd_report_work_init_failed;
	tpd_probe_work_init();
	tpd_resume_work_init();
	tpd_charger_work_init();
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	zlog_register_work_init();
#endif
	INIT_DELAYED_WORK(&cdev->send_cmd_work, send_cmd_work);
	INIT_DELAYED_WORK(&cdev->ghost_check_work, tp_ghost_check_work);

	return 0;
err_tpd_report_work_init_failed:
	if (!cdev->tpd_wq) {
		destroy_workqueue(cdev->tpd_wq);
	}
err_create_tpd_report_wq_failed:
	TPD_DMESG("%s: create tpd workqueue failed\n", __func__);
	return -ENOMEM;
}

void tpd_workqueue_deinit(void)
{
	struct ztp_device *cdev = tpd_cdev;

	TPD_DMESG("%s enter", __func__);
	tpd_report_work_deinit();
	tpd_resume_work_deinit();
	tpd_probe_work_deinit();
	tpd_charger_work_deinit();
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	zlog_register_work_deinit();
#endif
	cancel_delayed_work_sync(&cdev->send_cmd_work);
	cancel_delayed_work_sync(&cdev->ghost_check_work);
}

static void  zte_touch_deinit(void)
{
	static bool ztp_release = false;
	struct ztp_device *cdev = tpd_cdev;

	if (cdev == NULL || ztp_release) {
		TPD_DMESG("zte touch deinit, return\n", __func__, __LINE__);
		return;
	}
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	ufp_mac_exit();
#endif
	tpd_proc_deinit();
	tpd_workqueue_deinit();
	if (!cdev->tpd_wq) {
		destroy_workqueue(cdev->tpd_wq);
	}
	tpd_fw_sysfs_remove();
	zte_touch_pdev_unregister();
	ztp_release = true;
}

static int zte_touch_probe(struct platform_device *pdev)
{
	struct ztp_device *ztp_dev = NULL;

	TPD_DMESG("enter %s, %d\n", __func__, __LINE__);

	ztp_dev = devm_kzalloc(&pdev->dev, sizeof(struct ztp_device), GFP_KERNEL);
	if (!ztp_dev) {
		TPD_DMESG("Failed to allocate memory for ztp dev");
		return -ENOMEM;
	}
	tpd_cdev = ztp_dev;
	ztp_dev->pdev =  pdev;
	platform_set_drvdata(pdev, ztp_dev);
	zte_touch_pdev_register();
	ztp_parse_dt(pdev->dev.of_node, ztp_dev);
	get_lcd_panel_name();
	mutex_init(&ztp_dev->cmd_mutex);
	mutex_init(&ztp_dev->report_mutex);
	mutex_init(&ztp_dev->tp_resume_mutex);
#ifdef CONFIG_TOUCHSCREEN_LCD_NOTIFY
	lcd_notify_register();
#endif
	create_tpd_proc_entry();
	tpd_fw_sysfs_init();
	tpd_clean_all_event();
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	ufp_mac_init();
#endif
	if(tpd_workqueue_init())
		return -ENOMEM;
	queue_delayed_work(ztp_dev->tpd_wq, &ztp_dev->tpd_probe_work, msecs_to_jiffies(3000));
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	tpd_zlog_init();
	queue_delayed_work(ztp_dev->tpd_wq, &ztp_dev->zlog_register_work, msecs_to_jiffies(5000));
#endif
	TPD_DMESG("end %s, %d\n", __func__, __LINE__);
	return 0;
}

static int  zte_touch_remove(struct platform_device *pdev)
{
	TPD_DMESG("end %s, %d\n", __func__, __LINE__);
	zte_touch_deinit();
	return 0;
}

static void zte_touch_shutdown(struct platform_device *pdev)
{
	struct ztp_device *cdev = tpd_cdev;

	TPD_DMESG("end %s, %d\n", __func__, __LINE__);
	if (cdev->tpd_shutdown)
		cdev->tpd_shutdown(cdev);
	tpd_workqueue_deinit();
#ifdef CONFIG_TOUCHSCREEN_LCD_NOTIFY
	lcd_notify_unregister();
#endif
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	zlog_register_work_deinit();
#endif
}

static const struct of_device_id zte_touch_of_match[] = {
	{ .compatible = "zte_tp", },
	{ },
};

static struct platform_driver zte_touch_device_driver = {
	.probe		= zte_touch_probe,
	.remove		= zte_touch_remove,
	.shutdown	= zte_touch_shutdown,
	.driver		= {
		.name	= "zte_tp",
		.owner	= THIS_MODULE,
		.of_match_table = zte_touch_of_match,
	}
};

int __init zte_touch_init(void)
{
	TPD_DMESG("%s into\n", __func__);

	return platform_driver_register(&zte_touch_device_driver);
}

static void __exit zte_touch_exit(void)
{
#ifdef CONFIG_TOUCHSCREEN_ILITEK_TDDI_V3
	if (tpd_cdev->tp_chip_id == TS_CHIP_ILITEK)
		ilitek_plat_dev_exit();
#endif
#ifdef CONFIG_TOUCHSCREEN_HIMAX_COMMON
	if (tpd_cdev->tp_chip_id == TS_CHIP_HIMAX)
		himax_common_exit();
#endif
#ifdef CONFIG_TOUCHSCREEN_CHIPONE
	if (tpd_cdev->tp_chip_id == TS_CHIP_CHIPONE)
		cts_i2c_driver_exit();
#endif
#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_V2
	if (tpd_cdev->tp_chip_id == TS_CHIP_GOODIX)
		goodix_ts_core_exit();
#endif
#ifdef CONFIG_TOUCHSCREEN_OMNIVISION_TCM
	if (tpd_cdev->tp_chip_id == TS_CHIP_OMNIVISION)
		ovt_tcm_module_exit();
#endif
#ifdef CONFIG_TOUCHSCREEN_CHIPONE_V2
	if (tpd_cdev->tp_chip_id == TS_CHIP_CHIPONE)
		cts_driver_exit();
#endif
#ifdef CONFIG_TOUCHSCREEN_CHIPONE_V3
	if (tpd_cdev->tp_chip_id == TS_CHIP_CHIPONE)
		cts_driver_exit();
#endif
#if defined(CONFIG_TOUCHSCREEN_FTS_V3_3) || defined(CONFIG_TOUCHSCREEN_FTS_UFP)
	if (tpd_cdev->tp_chip_id == TS_CHIP_FOCAL)
		fts_ts_exit();
#endif
#ifdef CONFIG_TOUCHSCREEN_CHSC5XXX
	if (tpd_cdev->tp_chip_id == TS_CHIP_SEMI)
		semi_i2c_device_exit();
#endif
#ifdef CONFIG_TOUCHSCREEN_TLSC6X_V3
	if (tpd_cdev->tp_chip_id == TS_CHIP_TLSC)
		tlsc6x_exit();
#endif
#if defined(CONFIG_TOUCHSCREEN_GCORE_TS) || defined(CONFIG_TOUCHSCREEN_GCORE_TS_V3)
	if (tpd_cdev->tp_chip_id == TS_CHIP_GCORE)
		gcore_touch_driver_exit();
#endif
#ifdef CONFIG_TOUCHSCREEN_BETTERLIFE_TS
	if (tpd_cdev->tp_chip_id == TS_CHIP_BTL)
		btl_ts_exit();
#endif
#ifdef CONFIG_TOUCHSCREEN_SITRONIX_INCELL
	if (tpd_cdev->tp_chip_id == TS_CHIP_SITRONIX)
		sitronix_ts_exit();
#endif
#ifdef CONFIG_TOUCHSCREEN_LCD_NOTIFY
		lcd_notify_unregister();
#endif
	zte_touch_deinit();
	platform_driver_unregister(&zte_touch_device_driver);
}

late_initcall(zte_touch_init);
module_exit(zte_touch_exit);

MODULE_AUTHOR("zte");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("zte tp");

