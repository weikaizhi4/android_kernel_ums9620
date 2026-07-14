/* Copyright (c) 2020-2025, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#define pr_fmt(fmt)	"SQC_PD_HAL: %s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <vendor/common/sqc_common.h>

#define SQC_PD_DEVNAME "sqc_pd_node"

struct pd_dev_data {
	int ref_count;
	struct cdev main_dev;
	struct class *device_class;
	struct mutex file_mutex;
	struct sqc_pd_proto_ops *pd_chg_ops;
};

static struct pd_dev_data pd_hal_data;

static int sqc_hal_pd_status_init(void)
{
	struct sqc_pd_proto_ops *pd_chg_ops = pd_hal_data.pd_chg_ops;
	int ret = 0;

	if (pd_chg_ops != NULL && pd_chg_ops->status_init != NULL)
		ret = pd_chg_ops->status_init();

	return ret;
}

static int sqc_hal_pd_status_remove(void)
{
	struct sqc_pd_proto_ops *pd_chg_ops = pd_hal_data.pd_chg_ops;
	int ret = 0;

	if (pd_chg_ops != NULL && pd_chg_ops->status_remove != NULL)
		ret = pd_chg_ops->status_remove();

	return ret;
}

static int sqc_hal_pd_get_charger_type(int *chg_type)
{
	struct sqc_pd_proto_ops *pd_chg_ops = pd_hal_data.pd_chg_ops;
	int ret = 0;

	if (pd_chg_ops != NULL && pd_chg_ops->get_charger_type != NULL)
		ret = pd_chg_ops->get_charger_type(chg_type);

	return ret;
}

static int sqc_hal_pd_get_cable_type(int *cable_type)
{
	struct sqc_pd_proto_ops *pd_chg_ops = pd_hal_data.pd_chg_ops;
	int ret = 0;

	if (pd_chg_ops != NULL && pd_chg_ops->get_cable_type != NULL)
		ret = pd_chg_ops->get_cable_type(cable_type);

	return ret;
}

static int sqc_hal_pd_set_apdo_cap(int mV, int mA)
{
	struct sqc_pd_proto_ops *pd_chg_ops = pd_hal_data.pd_chg_ops;
	int ret = 0;

	if (pd_chg_ops != NULL && pd_chg_ops->set_apdo_cap != NULL)
		ret = pd_chg_ops->set_apdo_cap(mV, mA);

	return ret;
}

static int sqc_hal_pd_get_apdo_cap(int *max_vol_mV, int *max_curr_mA)
{
	struct sqc_pd_proto_ops *pd_chg_ops = pd_hal_data.pd_chg_ops;
	int ret = 0;

	if (pd_chg_ops != NULL && pd_chg_ops->get_apdo_cap != NULL)
		ret = pd_chg_ops->get_apdo_cap(max_vol_mV, max_curr_mA);

	return ret;
}

static loff_t sqc_pd_llseek(struct file *filp, loff_t off, int whence)
{
	loff_t newpos;

	mutex_lock(&(pd_hal_data.file_mutex));

	switch (whence) {
	case SEEK_SET:
		newpos = off;
		break;
	case SEEK_CUR:
		newpos = filp->f_pos + off;
		break;
	case SEEK_END:
		newpos = SQC_IFS_MAX + off;
		break;
	default:
		newpos = SQC_IFS_NONE;
	}

	if (newpos < 0 || newpos > SQC_IFS_MAX) {
		pr_err("%s: New position 0x%04x is invalid\n",
		       __func__, (unsigned int)newpos);
		newpos = SQC_IFS_NONE;
	}

	filp->f_pos = newpos;

	mutex_unlock(&(pd_hal_data.file_mutex));

	return newpos;
}

/*
 * sqc_pd_read: read register data from RMI device
 *
 * @filp: pointer to file structure
 * @buf: pointer to user space buffer
 * @count: number of bytes to read
 * @f_pos: starting RMI register address
 */
static ssize_t sqc_pd_read(struct file *filp, char __user *buf,
			   size_t count, loff_t *f_pos)
{
	ssize_t retval = 0, read_cnt = 0;
	unsigned int temp_val[2] = {0,};

	if ((*f_pos >= SQC_IFS_MAX) || (*f_pos == SQC_IFS_NONE) || (count == 0)) {
		pr_err("%s f_pos Error!\n", __func__, *f_pos);
		return -ENXIO;
	}

	mutex_lock(&(pd_hal_data.file_mutex));

	switch (*f_pos) {
	case SQC_IFS_PTL_COM_CHG_TYPE:
		retval = sqc_hal_pd_get_charger_type(&temp_val[0]);
		read_cnt = sizeof(temp_val[0]);
		break;
	case SQC_IFS_PTL_CABLE_TYPE:
		retval = sqc_hal_pd_get_cable_type(&temp_val[0]);
		read_cnt = sizeof(temp_val[0]);
		break;
	case SQC_IFS_PTL_PD_APDO:
		retval = sqc_hal_pd_get_apdo_cap(&temp_val[0], &temp_val[1]);
		read_cnt = sizeof(temp_val);
		break;
	default:
		retval = -ENOMEM;
		pr_err("%s f_pos default!\n", __func__, *f_pos);
		break;
	}

	if (retval >= 0) {
		if (copy_to_user(buf, temp_val, (count < read_cnt) ? count : read_cnt)) {
			pr_err("%s copy_to_user failed!\n", __func__);
			retval = -EFAULT;
		}
	}

	mutex_unlock(&(pd_hal_data.file_mutex));

	return (retval < 0) ? retval : read_cnt;
}

/*
 * sqc_pd_write: write register data to RMI device
 *
 * @filp: pointer to file structure
 * @buf: pointer to user space buffer
 * @count: number of bytes to write
 * @f_pos: starting RMI register address
 */
static ssize_t sqc_pd_write(struct file *filp, const char __user *buf,
			    size_t count, loff_t *f_pos)
{
	ssize_t retval = 0;
	unsigned int mV = 0, mA = 0;
	unsigned int *handle_buf = NULL;

	if ((*f_pos >= SQC_IFS_MAX) || (*f_pos == SQC_IFS_NONE) || (count == 0)) {
		pr_err("%s f_pos Error!\n", __func__, *f_pos);
		return -ENXIO;
	}

	handle_buf = kzalloc(count, GFP_KERNEL);
	if (!handle_buf) {
		pr_err("%s kzalloc Error!\n", __func__, *f_pos);
		return -ENOMEM;
	}

	if (copy_from_user(handle_buf, buf, count)) {
		pr_err("%s copy_from_user Error!\n", __func__, *f_pos);
		return -EFAULT;
	}

	mutex_lock(&(pd_hal_data.file_mutex));

	switch (*f_pos) {
	case SQC_IFS_PTL_COM_INIT:
		retval = sqc_hal_pd_status_init();
		break;
	case SQC_IFS_PTL_COM_EXIT:
		retval = sqc_hal_pd_status_remove();
		break;
	case SQC_IFS_PTL_PD_APDO:
		if (count != (sizeof(mV) + sizeof(mA))) {
			retval = -ENXIO;
			break;
		}
		mV = handle_buf[0];
		mA = handle_buf[1];
		retval = sqc_hal_pd_set_apdo_cap(mV, mA);
		break;
	default:
		retval = -ENOMEM;
		pr_err("%s f_pos default!\n", __func__, *f_pos);
		break;
	}

	mutex_unlock(&(pd_hal_data.file_mutex));

	kfree(handle_buf);

	return (retval < 0) ? retval : count;
}

static int sqc_pd_open(struct inode *inp, struct file *filp)
{
	int retval = 0;

	mutex_lock(&(pd_hal_data.file_mutex));

	if (pd_hal_data.ref_count < 1)
		pd_hal_data.ref_count++;
	else
		retval = -EBUSY;

	mutex_unlock(&(pd_hal_data.file_mutex));

	return retval;
}

static int sqc_pd_release(struct inode *inp, struct file *filp)
{
	mutex_lock(&(pd_hal_data.file_mutex));

	pd_hal_data.ref_count--;
	if (pd_hal_data.ref_count < 0)
		pd_hal_data.ref_count = 0;

	mutex_unlock(&(pd_hal_data.file_mutex));

	return 0;
}

static const struct file_operations sqc_pd_fops = {
	.owner = THIS_MODULE,
	.llseek = sqc_pd_llseek,
	.read = sqc_pd_read,
	.write = sqc_pd_write,
	.open = sqc_pd_open,
	.release = sqc_pd_release,

};

int sqc_hal_pd_register(struct sqc_pd_proto_ops *ops)
{
	int ret = 0, sqc_pd_major = 0;
	dev_t sqc_pd_devno;
	struct device *device_ptr = NULL;

	if ((ops == NULL) || (pd_hal_data.pd_chg_ops != NULL)) {
		pr_err("sqc pd_phy register fail!\n");
		return -EPERM;
	}

	mutex_init(&pd_hal_data.file_mutex);

	ret = alloc_chrdev_region(&sqc_pd_devno, 0, 1, SQC_PD_DEVNAME);
	if (ret) {
		pr_err("Error: Can't Get Major number for adc_cali\n");
		return ret;
	}

	cdev_init(&(pd_hal_data.main_dev), &sqc_pd_fops);

	ret = cdev_add(&pd_hal_data.main_dev, sqc_pd_devno, 1);
	if (ret) {
		pr_err("adc_cali Error: cdev_add\n");
		goto err_cdev_add;
	}

	sqc_pd_major = MAJOR(sqc_pd_devno);
	pd_hal_data.device_class = class_create(THIS_MODULE, SQC_PD_DEVNAME);
	if (!pd_hal_data.device_class) {
		pr_err("%s: Failed to create sqc_pd char device\n",	__func__);
		goto err_device_class;
	}

	device_ptr = device_create(pd_hal_data.device_class, NULL,
				   sqc_pd_devno, NULL, SQC_PD_DEVNAME);
	if (IS_ERR(device_ptr)) {
		pr_err("%s: Failed to create sqc_pd char device\n",	__func__);
		goto err_device_region;
	}

	pd_hal_data.pd_chg_ops = ops;

	pr_info("sqc_pd_fops init done!!!\n ");

	return 0;
err_device_region:
	if (pd_hal_data.device_class != NULL) {
		class_destroy(pd_hal_data.device_class);
		pd_hal_data.device_class = NULL;
	}
err_device_class:
	cdev_del(&pd_hal_data.main_dev);
err_cdev_add:
	unregister_chrdev_region(sqc_pd_devno, 1);

	return -ENODEV;
}
EXPORT_SYMBOL_GPL(sqc_hal_pd_register);

