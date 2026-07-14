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
#define pr_fmt(fmt)	"SQC_BC1D2_HAL: %s: " fmt, __func__

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
#include <sqc_common.h>

#define SQC_BC1D2_DEVNAME "sqc_bc1d2_node"

struct bc1d2_dev_data {
	int ref_count;
	struct cdev main_dev;
	struct class *device_class;
	struct mutex file_mutex;
	struct sqc_bc1d2_proto_ops *bc1d2_chg_ops;
};

static struct bc1d2_dev_data bc1d2_hal_data;

static int sqc_hal_bc1d2_status_init(void)
{
	struct sqc_bc1d2_proto_ops *bc1d2_chg_ops = bc1d2_hal_data.bc1d2_chg_ops;
	int ret = 0;

	if (bc1d2_chg_ops != NULL && bc1d2_chg_ops->status_init != NULL)
		ret = bc1d2_chg_ops->status_init();

	return ret;
}

static int sqc_hal_bc1d2_status_remove(void)
{
	struct sqc_bc1d2_proto_ops *bc1d2_chg_ops = bc1d2_hal_data.bc1d2_chg_ops;
	int ret = 0;

	if (bc1d2_chg_ops != NULL && bc1d2_chg_ops->status_remove != NULL)
		ret = bc1d2_chg_ops->status_remove();

	return ret;
}

static int sqc_hal_bc1d2_set_charger_type(int chg_type)
{
	struct sqc_bc1d2_proto_ops *bc1d2_chg_ops = bc1d2_hal_data.bc1d2_chg_ops;
	int ret = 0;

	if (bc1d2_chg_ops != NULL && bc1d2_chg_ops->set_charger_type != NULL)
		ret = bc1d2_chg_ops->set_charger_type(chg_type);

	return ret;
}

static int sqc_hal_bc1d2_get_charger_type(int *chg_type)
{
	struct sqc_bc1d2_proto_ops *bc1d2_chg_ops = bc1d2_hal_data.bc1d2_chg_ops;
	int ret = 0;

	if (bc1d2_chg_ops != NULL && bc1d2_chg_ops->get_charger_type != NULL)
		ret = bc1d2_chg_ops->get_charger_type(chg_type);

	return ret;
}

static int sqc_hal_bc1d2_get_protocol_status(unsigned int *status)
{
	struct sqc_bc1d2_proto_ops *bc1d2_chg_ops = bc1d2_hal_data.bc1d2_chg_ops;
	int ret = 0;

	if (bc1d2_chg_ops != NULL && bc1d2_chg_ops->get_protocol_status != NULL)
		ret = bc1d2_chg_ops->get_protocol_status(status);

	return ret;
}

static int sqc_hal_bc1d2_get_chip_vendor_id(unsigned int *vendor_id)
{
	struct sqc_bc1d2_proto_ops *bc1d2_chg_ops = bc1d2_hal_data.bc1d2_chg_ops;
	int ret = 0;

	if (bc1d2_chg_ops != NULL && bc1d2_chg_ops->get_chip_vendor_id != NULL)
		ret = bc1d2_chg_ops->get_chip_vendor_id(vendor_id);

	return ret;
}

static int sqc_hal_bc1d2_set_qc3d0_dp(unsigned int dp_cnt)
{
	struct sqc_bc1d2_proto_ops *bc1d2_chg_ops = bc1d2_hal_data.bc1d2_chg_ops;
	int ret = 0;

	if (bc1d2_chg_ops != NULL && bc1d2_chg_ops->set_qc3d0_dp != NULL)
		ret = bc1d2_chg_ops->set_qc3d0_dp(dp_cnt);

	return ret;
}

static int sqc_hal_bc1d2_set_qc3d0_dm(unsigned int dm_cnt)
{
	struct sqc_bc1d2_proto_ops *bc1d2_chg_ops = bc1d2_hal_data.bc1d2_chg_ops;
	int ret = 0;

	if (bc1d2_chg_ops != NULL && bc1d2_chg_ops->set_qc3d0_dm != NULL)
		ret = bc1d2_chg_ops->set_qc3d0_dm(dm_cnt);

	return ret;
}

static int sqc_hal_bc1d2_set_qc3d0_plus_dp(unsigned int dp_cnt)
{
	struct sqc_bc1d2_proto_ops *bc1d2_chg_ops = bc1d2_hal_data.bc1d2_chg_ops;
	int ret = 0;

	if (bc1d2_chg_ops != NULL && bc1d2_chg_ops->set_qc3d0_plus_dp != NULL)
		ret = bc1d2_chg_ops->set_qc3d0_plus_dp(dp_cnt);

	return ret;
}

static int sqc_hal_bc1d2_set_qc3d0_plus_dm(unsigned int dm_cnt)
{
	struct sqc_bc1d2_proto_ops *bc1d2_chg_ops = bc1d2_hal_data.bc1d2_chg_ops;
	int ret = 0;

	if (bc1d2_chg_ops != NULL && bc1d2_chg_ops->set_qc3d0_plus_dm != NULL)
		ret = bc1d2_chg_ops->set_qc3d0_plus_dm(dm_cnt);

	return ret;
}

static loff_t sqc_bc1d2_llseek(struct file *filp, loff_t off, int whence)
{
	loff_t newpos;

	mutex_lock(&(bc1d2_hal_data.file_mutex));

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

	mutex_unlock(&(bc1d2_hal_data.file_mutex));

	return newpos;
}

/*
 * sqc_bc1d2_read: read register data from RMI device
 *
 * @filp: pointer to file structure
 * @buf: pointer to user space buffer
 * @count: number of bytes to read
 * @f_pos: starting RMI register address
 */
static ssize_t sqc_bc1d2_read(struct file *filp, char __user *buf,
			      size_t count, loff_t *f_pos)
{
	ssize_t retval = 0, read_cnt = 0;
	unsigned int temp_val = 0;

	if ((*f_pos >= SQC_IFS_MAX) || (*f_pos == SQC_IFS_NONE) || (count == 0)) {
		pr_err("%s f_pos Error!\n", __func__, *f_pos);
		return -ENXIO;
	}

	mutex_lock(&(bc1d2_hal_data.file_mutex));

	switch (*f_pos) {
	case SQC_IFS_PTL_COM_CHG_TYPE:
		retval = sqc_hal_bc1d2_get_charger_type(&temp_val);
		read_cnt = sizeof(temp_val);
		break;
	case SQC_IFS_PTL_COM_STATUS:
		retval = sqc_hal_bc1d2_get_protocol_status(&temp_val);
		read_cnt = sizeof(temp_val);
		break;
	case SQC_IFS_PTL_COM_VENDOR_ID:
		retval = sqc_hal_bc1d2_get_chip_vendor_id(&temp_val);
		read_cnt = sizeof(temp_val);
		break;
	default:
		retval = -ENOMEM;
		pr_err("%s f_pos default!\n", __func__, *f_pos);
		break;
	}

	if (retval >= 0) {
		if (copy_to_user(buf, &temp_val, read_cnt)) {
			pr_err("%s copy_to_user failed!\n", __func__);
			retval = -EFAULT;
		}
	}

	mutex_unlock(&(bc1d2_hal_data.file_mutex));

	return (retval < 0) ? retval : read_cnt;
}

/*
 * sqc_bc1d2_write: write register data to RMI device
 *
 * @filp: pointer to file structure
 * @buf: pointer to user space buffer
 * @count: number of bytes to write
 * @f_pos: starting RMI register address
 */
static ssize_t sqc_bc1d2_write(struct file *filp, const char __user *buf,
			       size_t count, loff_t *f_pos)
{
	ssize_t retval = 0;
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
		kfree(handle_buf);
		return -EFAULT;
	}

	mutex_lock(&(bc1d2_hal_data.file_mutex));

	switch (*f_pos) {
	case SQC_IFS_PTL_COM_INIT:
		retval = sqc_hal_bc1d2_status_init();
		break;
	case SQC_IFS_PTL_COM_EXIT:
		retval = sqc_hal_bc1d2_status_remove();
		break;
	case SQC_IFS_PTL_COM_CHG_TYPE:
		retval = sqc_hal_bc1d2_set_charger_type(handle_buf[0]);
		break;
	case QC_IFS_PTL_QC3D0_DP:
		retval = sqc_hal_bc1d2_set_qc3d0_dp(handle_buf[0]);
		break;
	case QC_IFS_PTL_QC3D0_DM:
		retval = sqc_hal_bc1d2_set_qc3d0_dm(handle_buf[0]);
		break;
	case QC_IFS_PTL_QC3D0_PLUS_DP:
		retval = sqc_hal_bc1d2_set_qc3d0_plus_dp(handle_buf[0]);
		break;
	case QC_IFS_PTL_QC3D0_PLUS_DM:
		retval = sqc_hal_bc1d2_set_qc3d0_plus_dm(handle_buf[0]);
		break;
	default:
		retval = -ENOMEM;
		pr_err("%s f_pos default!\n", __func__, *f_pos);
		break;
	}

	mutex_unlock(&(bc1d2_hal_data.file_mutex));

	kfree(handle_buf);

	return (retval < 0) ? retval : count;
}

static int sqc_bc1d2_open(struct inode *inp, struct file *filp)
{
	int retval = 0;

	mutex_lock(&(bc1d2_hal_data.file_mutex));

	if (bc1d2_hal_data.ref_count < 1)
		bc1d2_hal_data.ref_count++;
	else
		retval = -EBUSY;

	mutex_unlock(&(bc1d2_hal_data.file_mutex));

	return retval;
}

static int sqc_bc1d2_release(struct inode *inp, struct file *filp)
{
	mutex_lock(&(bc1d2_hal_data.file_mutex));

	bc1d2_hal_data.ref_count--;
	if (bc1d2_hal_data.ref_count < 0)
		bc1d2_hal_data.ref_count = 0;

	mutex_unlock(&(bc1d2_hal_data.file_mutex));

	return 0;
}

static const struct file_operations sqc_bc1d2_fops = {
	.owner = THIS_MODULE,
	.llseek = sqc_bc1d2_llseek,
	.read = sqc_bc1d2_read,
	.write = sqc_bc1d2_write,
	.open = sqc_bc1d2_open,
	.release = sqc_bc1d2_release,
};

int sqc_hal_bc1d2_register(struct sqc_bc1d2_proto_ops *ops)
{
	int ret = 0, sqc_bc1d2_major = 0;
	dev_t sqc_bc1d2_devno;
	struct device *device_ptr = NULL;

	if ((ops == NULL) || (bc1d2_hal_data.bc1d2_chg_ops != NULL)) {
		pr_err("sqc bc1d2_phy register fail!\n");
		return -EPERM;
	}

	mutex_init(&bc1d2_hal_data.file_mutex);

	ret = alloc_chrdev_region(&sqc_bc1d2_devno, 0, 1, SQC_BC1D2_DEVNAME);
	if (ret) {
		pr_err("Error: Can't Get Major number for adc_cali\n");
		return ret;
	}

	cdev_init(&(bc1d2_hal_data.main_dev), &sqc_bc1d2_fops);

	ret = cdev_add(&bc1d2_hal_data.main_dev, sqc_bc1d2_devno, 1);
	if (ret) {
		pr_err("adc_cali Error: cdev_add\n");
		goto err_cdev_add;
	}

	sqc_bc1d2_major = MAJOR(sqc_bc1d2_devno);
	bc1d2_hal_data.device_class = class_create(THIS_MODULE, SQC_BC1D2_DEVNAME);
	if (!bc1d2_hal_data.device_class) {
		pr_err("%s: Failed to create sqc_bc1d2 char device\n",	__func__);
		goto err_device_class;
	}

	device_ptr = device_create(bc1d2_hal_data.device_class, NULL,
				   sqc_bc1d2_devno, NULL, SQC_BC1D2_DEVNAME);
	if (IS_ERR(device_ptr)) {
		pr_err("%s: Failed to create sqc_bc1d2 char device\n",	__func__);
		goto err_device_region;
	}

	bc1d2_hal_data.bc1d2_chg_ops = ops;

	pr_info("sqc_bc1d2_fops init done!!!\n ");

	return 0;
err_device_region:
	if (bc1d2_hal_data.device_class != NULL) {
		class_destroy(bc1d2_hal_data.device_class);
		bc1d2_hal_data.device_class = NULL;
	}
err_device_class:
	cdev_del(&bc1d2_hal_data.main_dev);
err_cdev_add:
	unregister_chrdev_region(sqc_bc1d2_devno, 1);

	return -ENODEV;
}
EXPORT_SYMBOL_GPL(sqc_hal_bc1d2_register);

int sqc_hal_bc1d2_unregister(void)
{
	if (bc1d2_hal_data.bc1d2_chg_ops == NULL) {
		pr_err("sqc bc1d2_phy register fail!\n");
		return -EPERM;
	}

	mutex_lock(&(bc1d2_hal_data.file_mutex));

	bc1d2_hal_data.bc1d2_chg_ops = NULL;

	mutex_unlock(&(bc1d2_hal_data.file_mutex));

	pr_info("sqc_bc1d2_fops exit done!!!\n ");

	return 0;
}
EXPORT_SYMBOL_GPL(sqc_hal_bc1d2_unregister);


