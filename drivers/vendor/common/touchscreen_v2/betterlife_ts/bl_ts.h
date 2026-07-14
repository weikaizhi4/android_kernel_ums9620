/*
 * Goodix GT9xx touchscreen driver
 *
 * Copyright  (C)  2010 - 2014 Goodix. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be a reference
 * to you, when you are integrating the GOODiX's CTP IC into your system,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * Version: 2.4
 * Release Date: 2014/11/28
 */

#ifndef __BL_TS_H__
#define __BL_TS_H__

#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/ioctl.h>
#include <linux/time.h>
#include <linux/firmware.h>

#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#endif
#ifdef CONFIG_FB
#include <linux/notifier.h>
#include <linux/fb.h>
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include "bl_chip_common.h"

#if defined(BTL_VIRTRUAL_KEY_SUPPORT)
struct btl_key_data {
	int code;
	int x;
	int y;
};
#endif

struct btl_chip_info {
	unsigned char chipID;
	unsigned char rxChannel;
	unsigned char txChannel;
	unsigned char keyChannel;
};

struct btl_ts_data {
	spinlock_t irq_lock;
	struct i2c_client *client;
	struct mutex i2c_lock;
	struct input_dev *input_dev;
	struct hrtimer timer;
	struct workqueue_struct *btl_wq;
	struct workqueue_struct *btl_fw_upgrade_wq;
	struct work_struct work;
	struct delayed_work fw_work;
	struct workqueue_struct *btl_assist_wq;
#if defined(BTL_WAIT_QUEUE)
	struct task_struct *btl_thread;
	int tpd_flag;
	struct wait_queue_head waiter;
#endif
#if defined(BTL_CONFIG_OF)
	int reset_gpio_number;
	unsigned char reset_gpio_level;
	int irq_gpio_number;
	unsigned char irq_gpio_level;
	unsigned char irq_gpio_dir;
#if defined(BTL_CUSTOM_VCC_LDO_SUPPORT)
	int vcc_gpio_number;
#endif
#if defined(BTL_VCC_LDO_SUPPORT)
	const char *vcc_name;
	struct regulator *vcc;
#endif
#if defined(BTL_CUSTOM_IOVCC_LDO_SUPPORT)
	int iovcc_gpio_number;
#endif
#if defined(BTL_IOVCC_LDO_SUPPORT)
	const char *iovcc_name;
	struct regulator *iovcc;
#endif
#if defined(BTL_VCC_SUPPORT)
	unsigned char vcc_status;
#endif
#if defined(BTL_IOVCC_SUPPORT)
	unsigned char iovcc_status;
#endif
	int virtualkeys[12];
	int TP_MAX_X;
	int TP_MAX_Y;
#endif
	s32 irq_is_disable;
	s32 use_irq;
	u8 max_touch_num;
	u8 int_trigger_type;
	u8 enter_update;
	u8 bl_is_suspend;
#if defined(BTL_PROXIMITY_SUPPORT)
	struct input_dev *ps_input_dev;
	u8 proximity_enable;
	u8 proximity_state;
#endif
#if defined(BTL_ESD_PROTECT_SUPPORT)
	struct delayed_work esd_work;
	spinlock_t esd_lock;
	u8 esd_running;
	u32 clk_tick_cnt_esd;
	u8 esd_need_block;
	u8 esd_value[4];
#endif
#if defined(BTL_CHARGE_PROTECT_SUPPORT)
	struct delayed_work charge_work;
	spinlock_t charge_lock;
	u8 charge_running;
	u32 clk_tick_cnt_charge;
	u8 charge_need_block;
#endif
#if defined(BTL_SUSPEND_MODE)
	struct workqueue_struct *btl_resume_wq;
	struct work_struct resume_work;
#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(BTL_EARLYSUSPEND_SUPPORT)
	struct early_suspend early_suspend;
#endif
#if defined(CONFIG_FB) && defined(BTL_FB_SUPPORT)
	struct notifier_block notifier;
#endif
#if defined(CONFIG_ADF) && defined(BTL_ADF_SUPPORT)
	struct notifier_block notifier;
#endif
#endif
#if defined(BTL_DEBUGFS_SUPPORT)
	struct kobject *sysfs_obj;
	char *data;
	int datalen;
#endif
#if defined(BTL_SYSFS_VIRTRUAL_KEY_SUPPORT)
	struct kobject *sysfs_key_obj;
#endif
#if defined(BTL_VIRTRUAL_KEY_SUPPORT)
	struct btl_key_data btl_key_data[BTL_VIRTRUAL_KEY_NUM];
#endif
#if defined(BTL_FACTORY_TEST_EN)
	int btl_log_level;
#endif
#if defined(BTL_APK_SUPPORT)
	struct proc_dir_entry *proc_entry;
	int is_apk_debug;
	int is_entry_factory;
	wait_queue_head_t debug_queue;
	int debug_sync_flag;
	unsigned char *apk_data;
#endif
	struct btl_chip_info chipInfo;
};

#define BTL_GPIO_AS_INPUT(pin) gpio_direction_input(pin)
#define BTL_GPIO_AS_INT(pin) BTL_GPIO_AS_INPUT(pin)
#define BTL_GPIO_GET_VALUE(pin)         gpio_get_value(pin)
#define BTL_GPIO_OUTPUT(pin, level)      gpio_direction_output(pin, level)
#define BTL_GPIO_REQUEST(pin, label)    gpio_request(pin, label)
#define BTL_GPIO_FREE(pin)              gpio_free(pin)
#define BTL_IRQ_TAB    {IRQF_TRIGGER_FALLING|IRQF_ONESHOT|IRQF_NO_SUSPEND, IRQF_TRIGGER_RISING|IRQF_ONESHOT|IRQF_NO_SUSPEND, IRQF_TRIGGER_HIGH, IRQF_TRIGGER_LOW}

/* ***************************PART3:OTHER define********************************* */
#define BTL_DRIVER_VERSION          "V1.2<2022/02/28>"
#define BTL_I2C_NAME                "betterlife_ts"
#define BTL_POLL_TIME         10

extern struct btl_ts_data *g_btl_ts;

#if defined(BTL_FACTORY_TEST_EN)
extern int rawdata_tested_flag;
extern int btl_log_level;
#endif
int btl_i2c_write_read(struct i2c_client *client, unsigned char addr, unsigned char *writebuf, int writelen,
		       unsigned char *readbuf, int readlen);
int btl_i2c_write(struct i2c_client *client, unsigned char addr, unsigned char *writebuf, int writelen);
int btl_i2c_read(struct i2c_client *client, unsigned char addr, unsigned char *readbuf, int readlen);
int btl_get_chip_id(unsigned char *buf);
int btl_get_fwArgPrj_id(unsigned char *buf);
int btl_get_cob_id(unsigned char *buf);
int btl_get_prj_id(unsigned char *buf);
void btl_i2c_lock(void);
void btl_i2c_unlock(void);
#if defined(BTL_ESD_PROTECT_SUPPORT)
void btl_esd_switch(struct btl_ts_data *ts, s32 on);
#endif

#if defined(BTL_FACTORY_TEST_EN)
int btl_test_init(void);
int btl_test_exit(void);
int btl_test_entry(char *ini_file_name);
#endif

#if defined(BTL_CHARGE_PROTECT_SUPPORT)
void btl_charge_switch(struct btl_ts_data *ts, s32 on);
#endif

#if defined(RESET_PIN_WAKEUP)
void btl_ts_reset_wakeup(void);
#endif
#if (UPDATE_MODE == I2C_UPDATE_MODE)
void btl_enter_update_with_i2c(void);
void btl_exit_update_with_i2c(void);
#endif
#if (UPDATE_MODE == INT_UPDATE_MODE)
void btl_enter_update_with_int(void);
void btl_exit_update_with_int(void);
#endif

void btl_ts_set_intup(char level);
void btl_ts_set_intmode(char mode);

#if defined(BTL_CHARGE_PROTECT_SUPPORT)
unsigned char sprdbat_get_charge_status(void);
#endif
#if defined(BTL_AUTO_UPDATE_FARMWARE)
int btl_auto_update_fw(void);
#endif
#if defined(BTL_UPDATE_FARMWARE_WITH_BIN)
int btl_fw_upgrade_with_bin_file(unsigned char *firmware_path);
#endif
#ifdef  BTL_UPDATE_FIRMWARE_WITH_REQUEST_FIRMWARE
int btl_update_firmware_via_request_firmware(void);
#endif

#if (UPDATE_MODE == I2C_UPDATE_MODE)
#define   SET_WAKEUP_HIGH    btl_exit_update_with_i2c()
#define   SET_WAKEUP_LOW	 btl_enter_update_with_i2c()
#endif

#if (UPDATE_MODE == INT_UPDATE_MODE)
#define   SET_WAKEUP_HIGH    btl_exit_update_with_int()
#define   SET_WAKEUP_LOW	 btl_enter_update_with_int()
#endif
#define   btl_ts_set_irq()    btl_ts_set_intmode(1)
#define   MDELAY(n)  usleep_range(n * 1000, n * 1000 + 100);

#endif
