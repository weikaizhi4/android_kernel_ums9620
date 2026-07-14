/*
 * FPC Capacitive Fingerprint sensor device driver
 *
 * This driver will control the platform resources that the FPC fingerprint
 * sensor needs to operate. The major things are probing the sensor to check
 * that it is actually connected and let the Kernel know this and with that also
 * enabling and disabling of regulators, enabling and disabling of platform
 * clocks.
 * *
 * The driver will expose most of its available functionality in sysfs which
 * enables dynamic control of these features from eg. a user space process.
 *
 * The sensor's IRQ events will be pushed to Kernel's event handling system and
 * are exposed in the drivers event node. This makes it possible for a user
 * space process to poll the input node and receive IRQ events easily. Usually
 * this node is available under /dev/input/eventX where 'X' is a number given by
 * the event system. A user space process will need to traverse all the event
 * nodes and ask for its parent's name (through EVIOCGNAME) which should match
 * the value in device tree named input-device-name.
 *
 *
 * Copyright (c) 2020-2021 Fingerprint Cards AB <tech@fingerprints.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License Version 2
 * as published by the Free Software Foundation.
 */
#include "fpc1020.h"

struct fpc_ree_dev
{
	u8     *tx_buf;
	u8     *rx_buf;
	dev_t  devt;
	struct class *ree_class;
	struct spi_device *spidev;
};

static struct fpc_ree_dev g_ree_dev;
#define FPC_REE_SPIDEV_MAJOR 234
#define FPC_REE_BUF_SIZE (1024 * 32)

static int fpc_ree_open(struct inode *inode, struct file *filp)
{
	fpc_debug(DEBUG_LOG, "%s enter!\n", __func__);
	(void)inode;
	(void)filp;
	return GENERIC_OK;
}

static int fpc_ree_release(struct inode *inode, struct file *filp)
{
	fpc_debug(DEBUG_LOG, "%s enter!\n", __func__);
	(void)inode;
	(void)filp;
	return GENERIC_OK;
}

static long fpc_ree_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	u32    hz;
	int    status = 0;
	struct spi_message msg;
	struct spi_transfer spi_t;
	struct spi_ioc_transfer spi_buf;
	struct spi_device *spi = g_ree_dev.spidev;
	fpc_debug(DEBUG_LOG, "%s enter!\n", __func__);
	switch (cmd) {
	case SPI_IOC_WR_MAX_SPEED_HZ:
		status = __get_user(hz, (__u32 __user *)arg);
		if (status == 0 && hz > 0) {
			spi->max_speed_hz = hz;
			status = spi_setup(spi);
			if (status < 0) {
				fpc_debug(ERR_LOG, "%s:err spi_setup status = %d hz = %d\n", __func__, status, hz);
				return -EFAULT;
			}
		}
		break;
	default:
		memset(g_ree_dev.tx_buf, 0, FPC_REE_BUF_SIZE);
		memset(g_ree_dev.rx_buf, 0, FPC_REE_BUF_SIZE);
		status = copy_from_user(&spi_buf, (struct spi_ioc_transfer __user *)arg, sizeof(struct spi_ioc_transfer));
		if (status != 0) {
			fpc_debug(ERR_LOG, "%s:err copy_from_user status = %d\n", __func__, status);
			return -EFAULT;
		}
		if (spi_buf.len > FPC_REE_BUF_SIZE) {
			fpc_debug(ERR_LOG, "%s:err spi_buf.len = %d\n", __func__, spi_buf.len);
			return -EFAULT;
		}
		status = copy_from_user(g_ree_dev.tx_buf, (const u8 __user *)(uintptr_t)spi_buf.tx_buf, spi_buf.len);
		if (status != 0) {
			fpc_debug(ERR_LOG, "%s:err copy_from_user status = %d\n", __func__, status);
			return -EFAULT;
		}
		spi_t.tx_buf = g_ree_dev.tx_buf;
		spi_t.rx_buf = g_ree_dev.rx_buf;
		spi_t.speed_hz = spi->max_speed_hz;
		spi_t.len = spi_buf.len;
		spi_message_init(&msg);
		spi_message_add_tail(&spi_t, &msg);
		status = spi_sync(spi, &msg);
		if (status != 0) {
			fpc_debug(ERR_LOG, "%s:err spi_sync status = %d\n", __func__, status);
			return -EFAULT;
		}
		if (spi_t.rx_buf) {
			status = copy_to_user((u8 __user *) (uintptr_t) spi_buf.rx_buf, spi_t.rx_buf, spi_buf.len);
			if (status != 0) {
				fpc_debug(ERR_LOG, "%s:err copy_to_user status = %d\n", __func__, status);
				return -EFAULT;
			}
		}
		break;
	}
	fpc_debug(DEBUG_LOG, "%s exit!\n", __func__);
	return status;
}

static int fpc_ree_malloc(void)
{
	fpc_debug(DEBUG_LOG, "%s enter!\n", __func__);
	if (!g_ree_dev.tx_buf) {
		g_ree_dev.tx_buf = kmalloc(FPC_REE_BUF_SIZE, GFP_KERNEL);
		if (!g_ree_dev.tx_buf) {
			fpc_debug(ERR_LOG, "%s err 1\n", __func__);
			return -ENOMEM;
		}
	}

	if (!g_ree_dev.rx_buf) {
		g_ree_dev.rx_buf = kmalloc(FPC_REE_BUF_SIZE, GFP_KERNEL);
		if (!g_ree_dev.rx_buf) {
			fpc_debug(ERR_LOG, "%s:err 2\n", __func__);
			return -ENOMEM;
		}
	}
	fpc_debug(DEBUG_LOG, "%s exit!\n", __func__);
	return GENERIC_OK;
}

static void fpc_ree_free(void)
{
	fpc_debug(DEBUG_LOG, "%s enter!\n", __func__);
	if (g_ree_dev.tx_buf) {
		kfree(g_ree_dev.tx_buf);
		g_ree_dev.tx_buf = NULL;
	}
	if (g_ree_dev.rx_buf) {
		kfree(g_ree_dev.rx_buf);
		g_ree_dev.rx_buf = NULL;
	}
	fpc_debug(DEBUG_LOG, "%s exit!\n", __func__);
}

static const struct file_operations fpc_ree_ops = {
	.owner =    THIS_MODULE,
	.unlocked_ioctl = fpc_ree_ioctl,
	.open =     fpc_ree_open,
	.release =  fpc_ree_release,
};

int fpc_ree_init(struct spi_device *spi)
{
	struct device *dev = NULL;
	int status = 0;
	fpc_debug(DEBUG_LOG, "%s enter!\n", __func__);
	if (spi == NULL || FPC_REE == 0) {
		fpc_debug(ERR_LOG, "%s:none\n", __func__);
		return GENERIC_OK;
	}
	if (spi == NULL) {
		fpc_debug(ERR_LOG, "%s:err NULL spi_device\n", __func__);
		return GENERIC_ERR;
	}
	status = register_chrdev(FPC_REE_SPIDEV_MAJOR, FPC_MODULE_NAME, &fpc_ree_ops);
	if (status < 0) {
		fpc_debug(ERR_LOG, "%s:err register_chrdev status = %d", __func__, status);
		return GENERIC_ERR;
	}
	g_ree_dev.ree_class = class_create(THIS_MODULE, FPC_MODULE_NAME);
	if (IS_ERR(g_ree_dev.ree_class)) {
		 fpc_debug(ERR_LOG, "%s:err class_create", __func__);
		return GENERIC_ERR;
	}
	g_ree_dev.devt = MKDEV(FPC_REE_SPIDEV_MAJOR, 15);
	dev = device_create(g_ree_dev.ree_class, &spi->dev, g_ree_dev.devt, &g_ree_dev, FPC_MODULE_NAME);
	if (IS_ERR(dev)) {
		fpc_debug(ERR_LOG, "%s:err device_create\n", __func__);
		return GENERIC_ERR;
	}
	g_ree_dev.spidev = spi;
	fpc_debug(DEBUG_LOG, "%s exit!\n", __func__);
	return fpc_ree_malloc();
}

void fpc_ree_exit(void)
{
	fpc_debug(DEBUG_LOG, "%s enter!\n", __func__);
	if(g_ree_dev.spidev){
		device_destroy(g_ree_dev.ree_class, g_ree_dev.devt);
		class_destroy(g_ree_dev.ree_class);
		unregister_chrdev(FPC_REE_SPIDEV_MAJOR, FPC_MODULE_NAME);
		fpc_ree_free();
	}
	fpc_debug(DEBUG_LOG, "%s exit!\n", __func__);
}