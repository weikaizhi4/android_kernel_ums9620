#include "semi_touch_custom.h"
#include "semi_touch_function.h"
#include "semi_touch_upgrade.h"
#include <linux/device.h>
#include <linux/proc_fs.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>

#if SEMI_TOUCH_FACTORY_TEST_EN
#include "semi_touch_test_5448.h"
#endif
enum entry_type{ chsc_version, chsc_tp_info, chsc_proximity, chsc_guesture, chsc_online_update, chsc_glove, chsc_suspend,
	 chsc_orientation, chsc_esd_check, chsc_rp_rate, tp_selftest, entry_max };

#if SEMI_TOUCH_MAKE_NODES_DIR == MAKE_NODE_UNDER_PROC
/*******************************************************************************************************/
/*make custom nodes under proc node*/
/*******************************************************************************************************/
static struct proc_dir_entry *custom_proc_entry[entry_max];

void semi_touch_create_nodes_dir(void)
{
	if (st_dev.chsc_nodes_dir == NULL) {
		st_dev.chsc_nodes_dir = proc_mkdir(SEMI_TOUCH_PROC_DIR, NULL);
	}
}

void semi_touch_release_nodes_dir(void)
{
	int index = 0;

	for (index = 0; index < entry_max; index++) {
		if (custom_proc_entry[index] != NULL) {
			proc_remove(custom_proc_entry[index]);
		}
	}
	if (st_dev.chsc_nodes_dir != NULL) {
		proc_remove((struct proc_dir_entry *)st_dev.chsc_nodes_dir);
	}
}

static int semi_touch_register_nodefun_imp(enum entry_type etype, char *node_name,
	const struct file_operations *opt_addr)
{
	struct proc_dir_entry *entry = proc_create(node_name, 0664, st_dev.chsc_nodes_dir, opt_addr);

	check_return_if_zero(entry, NULL);

	custom_proc_entry[etype] = entry;

	return 0;
}

#define semi_touch_register_nodefun(etype, fun_write, fun_read) \
{ \
	static const struct file_operations ops_##etype = { \
	.owner = NULL, \
	.write = fun_write, \
	.read  = fun_read, \
	}; \
	semi_touch_register_nodefun_imp(etype, #etype, &ops_##etype); \
}

#define kernel_buffer_prepare(copy, ker_buf, size) \
char ker_buf[size], *copy = ker_buf; \
do {memset(ker_buf, 0, sizeof(ker_buf)); if (*ppos) return 0; } while (0)

#define kernel_buffer_to_entry(ker_buf, size) \
*ppos = size; \
ret = copy_to_user(buff, ker_buf, size); \
check_return_if_fail(ret, NULL)

#define kernel_buffer_from_entry(ker_buf, size) \
ret = copy_from_user(ker_buf, buff, size); \
check_return_if_fail(ret, NULL)

#define chsc_version_node_write_declare() chsc_version_node_write(struct file *fp, const char __user *buff, size_t len, loff_t *ppos)
#define chsc_version_node_read_declare() chsc_version_node_read(struct file *fp, char __user *buff, size_t len, loff_t *ppos)
#define chsc_tp_info_node_write_declare() chsc_tp_info_node_write(struct file *fp, const char __user *buff, size_t len, loff_t *ppos)
#define chsc_tp_info_node_read_declare() chsc_tp_info_node_read(struct file *fp, char __user *buff, size_t len, loff_t *ppos)
#define chsc_proximity_node_write_declare() chsc_proximity_node_write(struct file *fp, const char __user *buff, size_t len, loff_t *ppos)
#define chsc_proximity_node_read_declare() chsc_proximity_node_read(struct file *fp, char __user *buff, size_t len, loff_t *ppos)
#define chsc_guesture_node_write_declare() chsc_guesture_node_write(struct file *fp, const char __user *buff, size_t len, loff_t *ppos)
#define chsc_guesture_node_read_declare() chsc_guesture_node_read(struct file *fp, char __user *buff, size_t len, loff_t *ppos)
#define chsc_glove_node_write_declare() chsc_glove_node_write(struct file *fp, const char __user *buff, size_t len, loff_t *ppos)
#define chsc_glove_node_read_declare() chsc_glove_node_read(struct file *fp, char __user *buff, size_t len, loff_t *ppos)
#define chsc_suspend_node_write_declare() chsc_suspend_node_write(struct file *fp, const char __user *buff, size_t len, loff_t *ppos)
#define chsc_suspend_node_read_declare() chsc_suspend_node_read(struct file *fp, char __user *buff, size_t len, loff_t *ppos)
#define chsc_online_update_node_write_declare() chsc_online_update_node_write(struct file *fp, const char __user *buff, size_t len, loff_t *ppos)
#define chsc_online_update_node_read_declare() chsc_online_update_node_read(struct file *fp, char __user *buff, size_t len, loff_t *ppos)
#define chsc_orientation_node_write_declare() chsc_orientation_node_write(struct file *fp, const char __user *buff, size_t len, loff_t *ppos)
#define chsc_orientation_node_read_declare() chsc_orientation_node_read(struct file *fp, char __user *buff, size_t len, loff_t *ppos)
#define chsc_esd_check_node_write_declare() chsc_esd_check_node_write(struct file *fp, const char __user *buff, size_t len, loff_t *ppos)
#define chsc_esd_check_node_read_declare() chsc_esd_check_node_read(struct file *fp, char __user *buff, size_t len, loff_t *ppos)
#define chsc_tp_selftest_node_write_declare() chsc_tp_selftest_node_write(struct file *fp, const char __user *buff, size_t len, loff_t *ppos)
#define chsc_tp_selftest_node_read_declare() chsc_tp_selftest_node_read(struct file *fp, char __user *buff, size_t len, loff_t *ppos)
#define chsc_rp_rate_node_write_declare() chsc_rp_rate_node_write(struct file* fp, const char __user *buff, size_t len, loff_t* ppos)
#define chsc_rp_rate_node_read_declare() chsc_rp_rate_node_read(struct file* fp, char __user *buff, size_t len, loff_t* ppos)
#elif SEMI_TOUCH_MAKE_NODES_DIR == MAKE_NDDE_UNDER_SYS
/******************************************************************************************************************************************/
/*make custom nodes under sys file system*/
/******************************************************************************************************************************************/
void semi_touch_create_nodes_dir(void)
{
	if (st_dev.chsc_nodes_dir == NULL) {
		st_dev.chsc_nodes_dir = kobject_create_and_add(SEMI_TOUCH_SYS_DIR, NULL);
	}
}

void semi_touch_release_nodes_dir(void)
{
	if (st_dev.chsc_nodes_dir != NULL) {
		kobject_put((struct kobject *)st_dev.chsc_nodes_dir);
	}
}

#define semi_touch_register_nodefun(etype, fun_store, fun_show) \
{ \
	static struct kobj_attribute etype = __ATTR(etype, 0664, fun_show, fun_store); \
	ret = sysfs_create_file((struct kobject *)st_dev->chsc_nodes_dir, &etype.attr); \
	check_return_if_fail(ret, NULL); \
}

#define kernel_buffer_prepare(copy, ker_buf, size) \
char ker_buf[size], *copy = ker_buf; \
do {memset(ker_buf, 0, sizeof(ker_buf)); } while (0)

#define kernel_buffer_to_entry(ker_buf, size) \
memcpy(buff, ker_buf, size); \
ret = 0

#define kernel_buffer_from_entry(ker_buf, size) \
memcpy(ker_buf, buff, size); \
ret = 0

#define chsc_version_node_write_declare() chsc_version_node_write(struct kobject *dev, struct kobj_attribute *attr, const char *buff, size_t len)
#define chsc_version_node_read_declare() chsc_version_node_read(struct kobject *dev, struct kobj_attribute *attr, char *buff)
#define chsc_tp_info_node_write_declare() chsc_tp_info_node_write(struct kobject *dev, struct kobj_attribute *attr, const char *buff, size_t len)
#define chsc_tp_info_node_read_declare() chsc_tp_info_node_read(struct kobject *dev, struct kobj_attribute *attr, char *buff)
#define chsc_proximity_node_write_declare() chsc_proximity_node_write(struct kobject *dev, struct kobj_attribute *attr, const char *buff, size_t len)
#define chsc_proximity_node_read_declare() chsc_proximity_node_read(struct kobject *dev, struct kobj_attribute *attr, char *buff)
#define chsc_guesture_node_write_declare() chsc_guesture_node_write(struct kobject *dev, struct kobj_attribute *attr, const char *buff, size_t len)
#define chsc_guesture_node_read_declare() chsc_guesture_node_read(struct kobject *dev, struct kobj_attribute *attr, char *buff)
#define chsc_glove_node_write_declare() chsc_glove_node_write(struct kobject *dev, struct kobj_attribute *attr, const char *buff, size_t len)
#define chsc_glove_node_read_declare() chsc_glove_node_read(struct kobject *dev, struct kobj_attribute *attr, char *buff)
#define chsc_suspend_node_write_declare() chsc_suspend_node_write(struct kobject *dev, struct kobj_attribute *attr, const char *buff, size_t len)
#define chsc_suspend_node_read_declare() chsc_suspend_node_read(struct kobject *dev, struct kobj_attribute *attr, char *buff)
#define chsc_online_update_node_write_declare() chsc_online_update_node_write(struct kobject *dev, struct kobj_attribute *attr, const char *buff, size_t len)
#define chsc_online_update_node_read_declare() chsc_online_update_node_read(struct kobject *dev, struct kobj_attribute *attr, char *buff)
#define chsc_orientation_node_write_declare() chsc_orientation_node_write(struct kobject *dev, struct kobj_attribute *attr, const char *buff, size_t len)
#define chsc_orientation_node_read_declare() chsc_orientation_node_read(struct kobject *dev, struct kobj_attribute *attr, char *buff)
#define chsc_esd_check_node_write_declare() chsc_esd_check_node_write(struct kobject *dev, struct kobj_attribute *attr, const char *buff, size_t len)
#define chsc_esd_check_node_read_declare() chsc_esd_check_node_read(struct kobject *dev, struct kobj_attribute *attr, char *buff)
#define chsc_tp_selftest_node_write_declare() chsc_tp_selftest_node_write(struct kobject *dev, struct kobj_attribute *attr, const char *buff, size_t len)
#define chsc_tp_selftest_node_read_declare() chsc_tp_selftest_node_read(struct kobject *dev, struct kobj_attribute *attr, char *buff)
#define chsc_rp_rate_node_write_declare() chsc_rp_rate_node_write(struct kobject *dev, struct kobj_attribute *attr, const char *buff, size_t len)
#define chsc_rp_rate_node_read_declare() chsc_rp_rate_node_read(struct kobject* dev, struct kobj_attribute* attr, char* buff)
#endif /* SEMI_TOUCH_MAKE_NODES_DIR == MAKE_NDDE_UNDER_SYS */

const char *const mapping_ic_from_type(unsigned char ictype)
{
	static char *ic_name = "un-defined";

	switch (ictype) {
	case 0x00:
		ic_name = "CHSC5472";
		break;
	case 0x01:
		ic_name = "CHSC5448";
		break;
	case 0x02:
		ic_name = "CHSC5448A";
		break;
	case 0x03:
		ic_name = "CHSC5460";
		break;
	case 0x04:
		ic_name = "CHSC5468";
		break;
	case 0x10:
		ic_name = "CHSC5816";
		break;
	case 0x11:
		ic_name = "CHSC1716";
		break;
	default:
		break;
	}

	return ic_name;
}

static ssize_t chsc_version_node_write_declare()
{
	return -EPERM;
}

static ssize_t chsc_version_node_read_declare()
{
	int ret = 0, count = 0;
	unsigned char readBuffer[8] = { 0 };

	kernel_buffer_prepare(szCopy, szKernel, BUFFER_MAX_SIZE);

	ret = semi_touch_read_bytes(0x20000000 + 0x80, readBuffer, 8);
	check_return_if_fail(ret, NULL);

	count += snprintf(szCopy, BUFFER_MAX_SIZE, "Ic type %s\n", mapping_ic_from_type(readBuffer[0]));

	count += snprintf(szCopy + count, BUFFER_MAX_SIZE - count,  "config version is %02X\n", readBuffer[1]);
	count += snprintf(szCopy + count, BUFFER_MAX_SIZE - count, "vender id is %d, ", readBuffer[4]);
	count += snprintf(szCopy + count, BUFFER_MAX_SIZE - count, "product id is %d\n", (readBuffer[3] << 8) + readBuffer[2]);

	ret = semi_touch_read_bytes(0x20000000 + 0x10, readBuffer, 8);
	check_return_if_fail(ret, NULL);

	count += snprintf(szCopy + count, BUFFER_MAX_SIZE - count, "boot version is %04X\n", ((readBuffer[5] << 8) + readBuffer[4]));
	count += snprintf(szCopy + count, BUFFER_MAX_SIZE - count,  "driver version is %s\n", CHSC_DRIVER_VERSION);

	kernel_buffer_to_entry(szKernel, count);

	return count;
}

static ssize_t chsc_tp_info_node_write_declare()
{
	return -EPERM;
}

static ssize_t chsc_tp_info_node_read_declare()
{
	int ret, count;
	struct i2c_client *client = st_dev.client;

	kernel_buffer_prepare(szCopy, szKernel, BUFFER_MAX_SIZE);

	count += snprintf(szCopy, BUFFER_MAX_SIZE, "Max finger number is %0d\n", SEMI_TOUCH_MAX_POINTS);
	count += snprintf(szCopy + count, BUFFER_MAX_SIZE - count, "Int irq is %d\n", (int)client->irq);
	count += snprintf(szCopy + count, BUFFER_MAX_SIZE - count, "I2c address is 0x%02X(0x%02X)\n", client->addr, (client->addr) << 1);
	count += snprintf(szCopy + count, BUFFER_MAX_SIZE - count, "Run status is 0x%08X\n", st_dev.stc.ctp_run_status);
	count += snprintf(szCopy + count, BUFFER_MAX_SIZE - count, "Fun enable is 0x%04X\n", st_dev.stc.custom_function_en);

	kernel_buffer_to_entry(szKernel, count);

	return count;
}

static ssize_t chsc_proximity_node_write_declare()
{
	int ret;

	kernel_buffer_prepare(szCopy, szKernel, 8);
	kernel_buffer_from_entry(szKernel, (len > 8) ? 8 : len);

	if (szCopy[0] == 'o') {
		open_proximity_function(st_dev.stc.custom_function_en);
	} else if (szCopy[0] == 'c') {
		close_proximity_function(st_dev.stc.custom_function_en);
	}

	if (is_proximity_function_en(st_dev.stc.custom_function_en)) {
		if (szCopy[0] == '0') {
			semi_touch_proximity_switch(0);
		} else if (szCopy[0] == '1') {
			semi_touch_proximity_switch(1);
		}
	}

	return len;
}

static ssize_t chsc_proximity_node_read_declare()
{
	int ret, count;

	kernel_buffer_prepare(szCopy, szKernel, BUFFER_MAX_SIZE);

	count += snprintf(szCopy, BUFFER_MAX_SIZE, "proximity switch is %d, status is %d.\n",
			  is_proximity_function_en(st_dev.stc.custom_function_en),
			  is_proximity_activate(st_dev.stc.ctp_run_status));

	kernel_buffer_to_entry(szKernel, count);

	return count;
}

static ssize_t chsc_guesture_node_write_declare()
{
	int ret;

	kernel_buffer_prepare(szCopy, szKernel, 8);
	kernel_buffer_from_entry(szKernel, (len > 8) ? 8 : len);

	if (szCopy[0] == 'o') {
		open_guesture_function(st_dev.stc.custom_function_en);
	} else if (szCopy[0] == 'c') {
		close_guesture_function(st_dev.stc.custom_function_en);
	}

	if (is_guesture_function_en(st_dev.stc.custom_function_en)) {
		semi_touch_guesture_switch((unsigned char)(szCopy[0] - '0'));
	}

	return len;
}

static ssize_t chsc_guesture_node_read_declare()
{
	int ret, count;

	kernel_buffer_prepare(szCopy, szKernel, BUFFER_MAX_SIZE);

	count += snprintf(szCopy, BUFFER_MAX_SIZE, "guesture switch is %d, status is %d.\n",
			  is_guesture_function_en(st_dev.stc.custom_function_en),
			  is_guesture_activate(st_dev.stc.ctp_run_status));

	kernel_buffer_to_entry(szKernel, count);

	return count;
}

static ssize_t chsc_glove_node_write_declare()
{
	int ret;

	kernel_buffer_prepare(szCopy, szKernel, 8);
	kernel_buffer_from_entry(szKernel, (len > 8) ? 8 : len);

	if (szCopy[0] == 'o') {
		open_glove_function(st_dev.stc.custom_function_en);
		semi_touch_glove_switch(1);
	} else if (szCopy[0] == 'c') {
		semi_touch_glove_switch(0);
		close_glove_function(st_dev.stc.custom_function_en);
	}

	if (is_glove_function_en(st_dev.stc.custom_function_en)) {
		if (szCopy[0] == '0') {
			semi_touch_glove_switch(0);
		} else if (szCopy[0] == '1') {
			semi_touch_glove_switch(1);
		}
	}

	return len;
}

static ssize_t chsc_glove_node_read_declare()
{
	int ret, count;

	kernel_buffer_prepare(szCopy, szKernel, BUFFER_MAX_SIZE);

	count += snprintf(szCopy, BUFFER_MAX_SIZE, "glove switch is %d, status is %d.\n",
			  is_glove_function_en(st_dev.stc.custom_function_en), is_glove_activate(st_dev.stc.ctp_run_status));

	kernel_buffer_to_entry(szKernel, count);

	return count;
}

/*semi_touch_report_rate_switch(host_val)
180 - 180hz
210 - 210hz
240 - 240hz
*/
static ssize_t chsc_rp_rate_node_write_declare()
{
	int ret, host_val = 0;
	kernel_buffer_prepare(szCopy, szKernel, 8);
	kernel_buffer_from_entry(szKernel, (len > 8) ? 8 : len);

	szCopy[3] = 0;
	sscanf(szCopy, "%d", &host_val);
	semi_touch_report_rate_switch((unsigned short)host_val);

	return len;
}

static ssize_t chsc_rp_rate_node_read_declare()
{
	return -EPERM;
}

static ssize_t chsc_suspend_node_write_declare()
{
	int ret;

	kernel_buffer_prepare(szCopy, szKernel, 8);
	kernel_buffer_from_entry(szKernel, (len > 8) ? 8 : len);

	if (szCopy[0] == '0') {
		change_tp_state(LCD_ON);;	/* semi_touch_suspend_ctrl(0); */
	} else if (szCopy[0] == '1') {
		change_tp_state(LCD_OFF);;	/* semi_touch_suspend_ctrl(1); */
	}

	return len;
}

static ssize_t chsc_suspend_node_read_declare()
{
	int ret, count;

	kernel_buffer_prepare(szCopy, szKernel, BUFFER_MAX_SIZE);

	count += snprintf(szCopy, BUFFER_MAX_SIZE, "suspend switch is %d, status is %d.\n", 1, is_suspend_activate(st_dev.stc.ctp_run_status));
	kernel_buffer_to_entry(szKernel, count);

	return count;
}

static ssize_t chsc_orientation_node_write_declare()
{
	int ret;

	kernel_buffer_prepare(szCopy, szKernel, 8);
	kernel_buffer_from_entry(szKernel, (len > 8) ? 8 : len);

	semi_touch_orientation_switch((unsigned char)(szCopy[0] - '0'));

	return len;
}

static ssize_t chsc_orientation_node_read_declare()
{
	int ret, count;

	kernel_buffer_prepare(szCopy, szKernel, BUFFER_MAX_SIZE);

	count +=
	    snprintf(szCopy, BUFFER_MAX_SIZE, "orientation status is %d.\n",
		    orientation_activity(st_dev.stc.ctp_run_status));

	kernel_buffer_to_entry(szKernel, count);

	return count;
}

static ssize_t chsc_esd_check_node_read_declare()
{
	int ret, count;

	kernel_buffer_prepare(szCopy, szKernel, BUFFER_MAX_SIZE);

	count += snprintf(szCopy, BUFFER_MAX_SIZE, "esd check switch is %d.\n", is_esd_function_en(st_dev.stc.custom_function_en));

	kernel_buffer_to_entry(szKernel, count);

	return count;
}

static ssize_t chsc_esd_check_node_write_declare()
{
	int ret;

	kernel_buffer_prepare(szCopy, szKernel, 8);
	kernel_buffer_from_entry(szKernel, (len > 8) ? 8 : len);

	if (szCopy[0] == 'o') {
		open_esd_function(st_dev.stc.custom_function_en);
	} else if (szCopy[0] == 'c') {
		close_esd_function(st_dev.stc.custom_function_en);
	}

	return len;
}

static ssize_t chsc_online_update_node_write_declare()
{
	int ret;

	kernel_buffer_prepare(szCopy, szKernel, BUFFER_MAX_SIZE);
	kernel_buffer_from_entry(szKernel, (len > BUFFER_MAX_SIZE) ? BUFFER_MAX_SIZE : len);

	if (szCopy[0] == '1') {
		snprintf(szCopy, BUFFER_MAX_SIZE, "%s", CHSC_AUTO_UPDATE_PACKET_BIN);
	} else if (len > 1) {
		szCopy[len - 1] = 0;
	}

	ret = semi_touch_online_update_check((char *)szCopy);

	return len;
}

static ssize_t chsc_online_update_node_read_declare()
{
	int ret, count;

	kernel_buffer_prepare(szCopy, szKernel, BUFFER_MAX_SIZE);

	count += snprintf(szCopy, BUFFER_MAX_SIZE, "online update is %s\n", SEMI_TOUCH_ONLINE_UPDATE_EN ? "enabled" : "disabled");

	kernel_buffer_to_entry(szKernel, count);

	return count;
}

static ssize_t chsc_tp_selftest_node_write_declare()
{
	return -EPERM;
}

static ssize_t chsc_tp_selftest_node_read_declare()
{
#if SEMI_TOUCH_FACTORY_TEST_EN
	int ret, count;

	kernel_buffer_prepare(szCopy, szKernel, 128);

	ret = semi_touch_start_factory_test();

	szCopy += sprintf(szCopy, "TestResult is %s\n", (ret != 0) ? "Failed" : "Pass");

	count = szCopy - szKernel;

	kernel_buffer_to_entry(szKernel, count);

	return count;
#else
	return -EPERM;
#endif
}


int semi_touch_custom_work(struct sm_touch_dev *st_dev)
{
	int ret = 0;
	/* unsigned char readBuffer[8]; */

	semi_touch_create_nodes_dir();
	semi_touch_register_nodefun(chsc_version, chsc_version_node_write, chsc_version_node_read);
	semi_touch_register_nodefun(chsc_tp_info, chsc_tp_info_node_write, chsc_tp_info_node_read);
	semi_touch_register_nodefun(chsc_proximity, chsc_proximity_node_write, chsc_proximity_node_read);
	semi_touch_register_nodefun(chsc_guesture, chsc_guesture_node_write, chsc_guesture_node_read);
	semi_touch_register_nodefun(chsc_glove, chsc_glove_node_write, chsc_glove_node_read);
	semi_touch_register_nodefun(chsc_suspend, chsc_suspend_node_write, chsc_suspend_node_read);
	semi_touch_register_nodefun(chsc_online_update, chsc_online_update_node_write, chsc_online_update_node_read);
	semi_touch_register_nodefun(chsc_orientation, chsc_orientation_node_write, chsc_orientation_node_read);
	semi_touch_register_nodefun(chsc_esd_check, chsc_esd_check_node_write, chsc_esd_check_node_read);

	semi_touch_register_nodefun(tp_selftest, chsc_tp_selftest_node_write, chsc_tp_selftest_node_read);
	semi_touch_register_nodefun(chsc_rp_rate, chsc_rp_rate_node_write, chsc_rp_rate_node_read);
	ret = semi_touch_proximity_init();
	check_return_if_fail(ret, NULL);

	ret = semi_touch_gesture_prepare();
	check_return_if_fail(ret, NULL);

	ret = semi_touch_esd_check_prepare();
	check_return_if_fail(ret, NULL);

	ret = semi_touch_glove_prepare();
	check_return_if_fail(ret, NULL);

	/* this code tell us how to get tp infomation
	   ret = semi_touch_read_bytes(0x20000000 + 0x80, readBuffer, sizeof(readBuffer));
	   check_return_if_fail(ret, NULL);

	   strcat(TP_NAME, mapping_ic_from_type(readBuffer[0]));
	   TP_FW_VER  = readBuffer[1];
	   TP_VENDOR  = readBuffer[4];
	   TP_PRODUCT = readBuffer[3] << 8) + readBuffer[2]; */

	return ret;
}

int semi_touch_custom_clean_up(void)
{
	int ret;

	semi_touch_release_nodes_dir();

	ret = semi_touch_proximity_stop();
	check_return_if_fail(ret, NULL);

	ret = semi_touch_gesture_stop();
	check_return_if_fail(ret, NULL);

	ret = semi_touch_esd_check_stop();

	return ret;
}

/********************************************************************************************************************************/
#if SEMI_TOUCH_GLOVE_OPEN
int semi_touch_glove_prepare(void)
{
	open_glove_function(st_dev.stc.custom_function_en);

	return 0;
}
#endif
/********************************************************************************************************************************/

/********************************************************************************************************************************/
/*guesture support*/
#if SEMI_TOUCH_GESTURE_OPEN
/* #include <linux/wakelock.h> */
#define FINGER_DOWN                          0x01
#define FINGER_UP                            0x02
#define GESTURE_LEFT                         0x20
#define GESTURE_RIGHT                        0x21
#define GESTURE_UP                           0x22
#define GESTURE_DOWN                         0x23
#define GESTURE_DOUBLECLICK                  0x24
#define GESTURE_SINGLECLICK                  0x25
#define GESTURE_O                            0x30
#define GESTURE_W                            0x31
#define GESTURE_M                            0x32
#define GESTURE_E                            0x33
#define GESTURE_C                            0x34
#define GESTURE_S                            0x46
#define GESTURE_V                            0x54
#define GESTURE_Z                            0x65
#define GESTURE_L                            0x44
/* static struct wake_lock gesture_timeout_wakelock; */
int semi_touch_gesture_prepare(void)
{
	/* open_guesture_function(st_dev.stc.custom_function_en); */
	/* wake_lock_init(&gesture_timeout_wakelock, WAKE_LOCK_SUSPEND, "gesture_timeout_wakelock"); */

	input_set_capability(st_dev.input, EV_KEY, KEY_POWER);
	input_set_capability(st_dev.input, EV_KEY, KEY_U);
	input_set_capability(st_dev.input, EV_KEY, KEY_LEFT);
	input_set_capability(st_dev.input, EV_KEY, KEY_RIGHT);
	input_set_capability(st_dev.input, EV_KEY, KEY_UP);
	input_set_capability(st_dev.input, EV_KEY, KEY_DOWN);
	input_set_capability(st_dev.input, EV_KEY, KEY_D);
	input_set_capability(st_dev.input, EV_KEY, KEY_O);
	input_set_capability(st_dev.input, EV_KEY, KEY_W);
	input_set_capability(st_dev.input, EV_KEY, KEY_M);
	input_set_capability(st_dev.input, EV_KEY, KEY_E);
	input_set_capability(st_dev.input, EV_KEY, KEY_C);
	input_set_capability(st_dev.input, EV_KEY, KEY_S);
	input_set_capability(st_dev.input, EV_KEY, KEY_V);
	input_set_capability(st_dev.input, EV_KEY, KEY_Z);

	__set_bit(KEY_POWER, st_dev.input->keybit);
	__set_bit(KEY_U, st_dev.input->keybit);
	__set_bit(KEY_LEFT, st_dev.input->keybit);
	__set_bit(KEY_RIGHT, st_dev.input->keybit);
	__set_bit(KEY_UP, st_dev.input->keybit);
	__set_bit(KEY_DOWN, st_dev.input->keybit);
	__set_bit(KEY_D, st_dev.input->keybit);
	__set_bit(KEY_O, st_dev.input->keybit);
	__set_bit(KEY_W, st_dev.input->keybit);
	__set_bit(KEY_M, st_dev.input->keybit);
	__set_bit(KEY_E, st_dev.input->keybit);
	__set_bit(KEY_C, st_dev.input->keybit);
	__set_bit(KEY_S, st_dev.input->keybit);
	__set_bit(KEY_V, st_dev.input->keybit);
	__set_bit(KEY_Z, st_dev.input->keybit);

	return 0;
}

int semi_touch_gesture_stop(void)
{
	/* if(is_guesture_function_en(st_dev.stc.custom_function_en))
	   {
	   wake_lock_destroy(&gesture_timeout_wakelock);
	   } */

	return 0;
}

int semi_touch_wake_lock(void)
{
	/* if(is_guesture_activate(st_dev.stc.ctp_run_status))
	   {
	   irq_set_irq_type(st_dev.client->irq, IRQF_TRIGGER_FALLING);
	   wake_lock_timeout(&gesture_timeout_wakelock, msecs_to_jiffies(2500));
	   kernel_log_d("int interrupts, guesture on\n");
	   } */
	return SEMI_DRV_ERR_OK;
}

bool semi_touch_gesture_report(unsigned char gesture_id)
{
	int keycode = 0;

	/* wake_lock_timeout(&gesture_timeout_wakelock, msecs_to_jiffies(2000)); */

	switch (gesture_id) {
	case GESTURE_LEFT:
		keycode = KEY_LEFT;
		break;
	case GESTURE_RIGHT:
		keycode = KEY_RIGHT;
		break;
	case GESTURE_UP:
		keycode = KEY_UP;
		break;
	case GESTURE_DOWN:
		keycode = KEY_DOWN;
		break;
	case GESTURE_SINGLECLICK:
		if (tpd_cdev->tpd_report_uevent != NULL) {
			kernel_log_d("single Click Uevent\n");
			tpd_cdev->tpd_report_uevent(single_tap);
		}
		break;
	case GESTURE_DOUBLECLICK:
		if (tpd_cdev->tpd_report_uevent != NULL) {
			kernel_log_d("Double Click Uevent\n");
			tpd_cdev->tpd_report_uevent(double_tap);
		}
		break;
	case FINGER_DOWN:
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
		report_ufp_uevent(UFP_FP_DOWN);
#endif
		break;
	case FINGER_UP:
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
		report_ufp_uevent(UFP_FP_UP);
#endif
		break;
	case GESTURE_O:
		keycode = KEY_O;
		break;
	case GESTURE_W:
		keycode = KEY_W;
		break;
	case GESTURE_M:
		keycode = KEY_M;
		break;
	case GESTURE_E:
		keycode = KEY_E;
		break;
	case GESTURE_C:
		keycode = KEY_C;
		break;
	case GESTURE_S:
		keycode = KEY_S;
		break;
	case GESTURE_V:
		keycode = KEY_V;
		break;
	case GESTURE_Z:
		keycode = KEY_UP;
		break;
	case GESTURE_L:
		keycode = KEY_L;
		break;
	default:
		break;
	}
	return true;
}
#endif /* SEMI_TOUCH_GESTURE_OPEN */

/********************************************************************************************************************************/
/*esd support*/
#if SEMI_TOUCH_ESD_CHECK_OPEN
/* struct esd_check_waller
	{
		struct task_struct* check_task;
		unsigned char esd_check_flag;
		unsigned char esd_thread_loop;
	}; */
/* static struct esd_check_waller esk_waller; */
static struct hrtimer esd_check_timer;
/* static DECLARE_WAIT_QUEUE_HEAD(esd_wait_object); */
static enum hrtimer_restart esd_timer_callback(struct hrtimer *timer);
static void semi_touch_esd_work_fun(struct work_struct *work);
/* static int esd_check_thread_callback(void *unused); */

int semi_touch_esd_check_prepare(void)
{
	ktime_t ktime = ktime_set(30, 0);

	hrtimer_init(&esd_check_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	esd_check_timer.function = esd_timer_callback;
	hrtimer_start(&esd_check_timer, ktime, HRTIMER_MODE_REL);
	/* esk_waller.esd_thread_loop = 1; */
	/* esk_waller.check_task = kthread_run(esd_check_thread_callback, 0, CHSC_DEVICE_NAME); */

	open_esd_function(st_dev.stc.custom_function_en);

	return 0;
}

static enum hrtimer_restart esd_timer_callback(struct hrtimer *timer)
{
	/* esk_waller.esd_check_flag = 1; */
	/* wake_up_interruptible(&esd_wait_object); */
	ktime_t ktime;

	semi_touch_queue_asyn_work(work_queue_custom_work, semi_touch_esd_work_fun);

	ktime = ktime_set(4, 0);
	hrtimer_start(&esd_check_timer, ktime, HRTIMER_MODE_REL);

	return HRTIMER_NORESTART;
}

static void semi_touch_esd_work_fun(struct work_struct *work)
{
	semi_touch_heart_beat();
}

int semi_touch_esd_check_stop(void)
{
	if (is_esd_function_en(st_dev.stc.custom_function_en) && NULL != esd_check_timer.function) {
		hrtimer_cancel(&esd_check_timer);
	}

	return 0;
}
#endif /* SEMI_TOUCH_ESD_CHECK_OPEN */
