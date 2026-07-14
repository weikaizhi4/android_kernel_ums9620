/*
* aw9610x.c
*
* Version: v0.1.3
*
* Copyright (c) 2020 AWINIC Technology CO., LTD
*
* Author: Alex <zhangpengbiao@awinic.com>
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2 of the License, or (at your
* option) any later version.
*/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/string.h>
#include <linux/jiffies.h>

#if (defined CONFIG_VENDOR_SOC_SPRD_COMPILE) || (defined CONFIG_VENDOR_SOC_QCOM_COMPILE)
#include <linux/input.h>
#endif
#ifdef CONFIG_VENDOR_SOC_MTK_COMPILE
#ifdef SENSOR_ARCH_1_0
#include <situation.h>
#endif
#endif
#include "aw_bin_parse.h"
#include "aw9610x.h"
#include "aw9610x_reg.h"

#define AW9610X_I2C_NAME "aw9610x_sar"
#define AW9610X_DRIVER_VERSION "v0.1.3"

#define AW_READ_CHIPID_RETRIES 5
#define AW_I2C_RETRIES 5
#define AW9610X_SCAN_DEFAULT_TIME 10000
#define CALI_FILE_MAX_SIZE 128
#define AWINIC_CALI_FILE "/mnt/aw_cali.bin"
static char *aw9610x_cfg_name = "aw9610x.bin";

static u8 diff_ch_num = 1;

static struct device *class_dev;
static struct class *cls;
static dev_t const aw9610x_cls_device_dev_t = MKDEV(10, 0);
/******************************************************
*
* aw9610x i2c write/read
*
******************************************************/
static int32_t
i2c_write(struct aw9610x *aw9610x, uint16_t reg_addr16, uint32_t reg_data32)
{	int32_t ret =  -ENOMEM;
	struct i2c_client *i2c = aw9610x->i2c;
	struct i2c_msg msg;
	uint8_t w_buf[6];

	/*reg_addr*/
	w_buf[0] = (u8)(reg_addr16>>8);
	w_buf[1] = (u8)(reg_addr16);
	/*data*/
	w_buf[2] = (u8)(reg_data32 >> 24);
	w_buf[3] = (u8)(reg_data32 >> 16);
	w_buf[4] = (u8)(reg_data32 >> 8);
	w_buf[5] = (u8)(reg_data32);

	msg.addr = i2c->addr;
	msg.flags = AW9610X_I2C_WR;
	msg.len = 6;
	/*2 bytes regaddr + 4 bytes data*/
	msg.buf = (unsigned char *)w_buf;

	ret = i2c_transfer(i2c->adapter, &msg, 1);
	if (ret < 0)
		pr_info("%s: i2c write reg 0x%x error %d\n", __func__,
							reg_addr16, ret);

	return ret;
}

static int32_t
i2c_read(struct aw9610x *aw9610x, uint16_t reg_addr16, uint32_t *reg_data32)
{
	int32_t ret =  -ENOMEM;
	struct i2c_client *i2c = aw9610x->i2c;
	struct i2c_msg msg[2];
	uint8_t w_buf[2];
	uint8_t buf[4];

	w_buf[0] = (unsigned char)(reg_addr16 >> 8);
	w_buf[1] = (unsigned char)(reg_addr16);
	msg[0].addr = i2c->addr;
	msg[0].flags = AW9610X_I2C_WR;
	msg[0].len = 2;
	msg[0].buf = (unsigned char *)w_buf;

	msg[1].addr = i2c->addr;
	msg[1].flags = AW9610X_I2C_RD;
	msg[1].len = 4;
	msg[1].buf = (unsigned char *)buf;

	ret = i2c_transfer(i2c->adapter, msg, 2);
	if (ret < 0)
		pr_info("%s: i2c read reg 0x%x error %d\n", __func__,
							reg_addr16, ret);

	reg_data32[0] = ((u32)buf[3]) | ((u32)buf[2]<<8) |
			((u32)buf[1]<<16) | ((u32)buf[0]<<24);

	return ret;
}

static int32_t aw9610x_i2c_write(struct aw9610x *aw9610x,
				uint16_t reg_addr16, uint32_t reg_data32)
{
	int32_t ret = -1;
	uint8_t cnt = 0;

	while (cnt < AW_I2C_RETRIES) {
		ret = i2c_write(aw9610x, reg_addr16, reg_data32);
		if (ret < 0) {
			pr_err("%s: i2c_write cnt=%d error=%d\n",
							__func__, cnt, ret);
		} else {
			break;
		}
		cnt++;
	}

	return ret;
}

static int32_t aw9610x_i2c_read(struct aw9610x *aw9610x,
				uint16_t reg_addr16, uint32_t *reg_data32)
{
	int32_t ret = -1;
	uint8_t cnt = 0;

	while (cnt < AW_I2C_RETRIES) {
		ret = i2c_read(aw9610x, reg_addr16, reg_data32);
		if (ret < 0) {
			pr_err("%s: i2c_read cnt=%d error=%d\n", __func__, cnt,
			       ret);
		} else {
			break;
		}
		cnt++;
	}
	return ret;
}

static int32_t aw9610x_i2c_write_bits(struct aw9610x *aw9610x,
				 uint16_t reg_addr16, uint32_t mask,
				 uint32_t reg_data32)
{
	uint32_t reg_val;

	aw9610x_i2c_read(aw9610x, reg_addr16, &reg_val);
	reg_val &= mask;
	reg_val |= reg_data32;
	aw9610x_i2c_write(aw9610x, reg_addr16, reg_val);

	return 0;
}

/******************************************************************************
*
* aw9610x i2c sequential write/read --- one first addr with multiple data.
*
******************************************************************************/
static int32_t i2c_write_seq(struct aw9610x *aw9610x)
{
	int32_t ret =  -ENOMEM;
	struct i2c_client *i2c = aw9610x->i2c;
	struct i2c_msg msg;
	uint8_t w_buf[228];
	uint8_t addr_bytes = aw9610x->aw_i2c_package.addr_bytes;
	uint8_t msg_cnt = 0;
	uint8_t data_bytes = aw9610x->aw_i2c_package.data_bytes;
	uint8_t reg_num = aw9610x->aw_i2c_package.reg_num;
	uint8_t *p_reg_data = aw9610x->aw_i2c_package.p_reg_data;
	uint8_t msg_idx = 0;

	for (msg_idx = 0; msg_idx < addr_bytes; msg_idx++) {
		w_buf[msg_idx] = aw9610x->aw_i2c_package.init_addr[msg_idx];
		pr_info("%s: w_buf_addr[%d] = 0x%02x\n",
					__func__, msg_idx, w_buf[msg_idx]);
	}
	msg_cnt = addr_bytes;
	for (msg_idx = 0; msg_idx < data_bytes * reg_num; msg_idx++) {
		w_buf[msg_cnt] = *p_reg_data++;
		pr_info("%s: w_buf_addr[%d] = 0x%02x\n",
					__func__, msg_cnt, w_buf[msg_cnt]);
		msg_cnt++;
	}
	pr_info("%s: %d\n", __func__, msg_cnt);
	p_reg_data = aw9610x->aw_i2c_package.p_reg_data;
	msg.addr = i2c->addr;
	msg.flags = AW9610X_I2C_WR;
	msg.len = msg_cnt;
	msg.buf = (uint8_t *)w_buf;
	ret = i2c_transfer(i2c->adapter, &msg, 1);

	if (ret < 0)
		pr_info("%s: i2c write seq error %d\n", __func__, ret);

	return ret;
}

static int32_t i2c_read_seq(struct aw9610x *aw9610x, uint8_t *reg_data)
{
	int32_t ret =  -ENOMEM;
	struct i2c_client *i2c = aw9610x->i2c;
	struct i2c_msg msg[2];
	uint8_t w_buf[4];
	uint8_t buf[228];
	uint8_t data_bytes = aw9610x->aw_i2c_package.data_bytes;
	uint8_t reg_num = aw9610x->aw_i2c_package.reg_num;
	uint8_t addr_bytes = aw9610x->aw_i2c_package.addr_bytes;
	uint8_t msg_idx = 0;
	uint8_t msg_cnt = 0;

	/*
	* step 1 : according to addr_bytes assemble first_addr.
	* step 2 : initialize msg[0] including first_addr transfer to client.
	* step 3 : wait for client return reg_data.
	*/
	for (msg_idx = 0; msg_idx < addr_bytes; msg_idx++) {
		w_buf[msg_idx] = aw9610x->aw_i2c_package.init_addr[msg_idx];
		pr_info("%s: w_buf_addr[%d] = 0x%02x\n",
					__func__, msg_idx, w_buf[msg_idx]);
	}
	msg[0].addr = i2c->addr;
	msg[0].flags = AW9610X_I2C_WR;
	msg[0].len = msg_idx;
	msg[0].buf = (uint8_t *)w_buf;

	/*
	* receive client to msg[1].buf.
	*/
	msg_cnt = data_bytes * reg_num;
	msg[1].addr = i2c->addr;
	msg[1].flags = AW9610X_I2C_RD;
	msg[1].len = msg_cnt;
	msg[1].buf = (uint8_t *)buf;

	ret = i2c_transfer(i2c->adapter, msg, 2);
	for (msg_idx = 0; msg_idx < msg_cnt; msg_idx++) {
		reg_data[msg_idx] = buf[msg_idx];
		pr_info("%s: buf = 0x%02x\n", __func__, buf[msg_idx]);
	}

	if (ret < 0)
		pr_info("%s: i2c write error %d\n", __func__, ret);

	return ret;
}

static void
aw9610x_addrblock_load(struct device *dev, const char *buf)
{
	uint32_t addrbuf[4] = { 0 };
	uint8_t temp_buf[2] = { 0 };
	uint32_t i = 0;
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	uint8_t addr_bytes = aw9610x->aw_i2c_package.addr_bytes;
	uint8_t reg_num = aw9610x->aw_i2c_package.reg_num;

	for (i = 0; i < addr_bytes; i++) {
		if (reg_num < attr_buf[1]) {
			temp_buf[0] = buf[attr_buf[0] + i * 5];
			temp_buf[1] = buf[attr_buf[0] + i * 5 + 1];
		} else if (reg_num >= attr_buf[1] && reg_num < attr_buf[3]) {
			temp_buf[0] = buf[attr_buf[2] + i * 5];
			temp_buf[1] = buf[attr_buf[2] + i * 5 + 1];
		} else if (reg_num >= attr_buf[3] && reg_num < attr_buf[5]) {
			temp_buf[0] = buf[attr_buf[4] + i * 5];
			temp_buf[1] = buf[attr_buf[4] + i * 5 + 1];
		}
		if (sscanf(temp_buf, "%02x", &addrbuf[i]) == 1)
			aw9610x->aw_i2c_package.init_addr[i] =
							(uint8_t)addrbuf[i];
	}
}

/******************************************************
 *
 *the document of storage_spedata
 *
 ******************************************************/
static int32_t aw9610x_filedata_deal(struct aw9610x *aw9610x)
{
	struct file *fp = NULL;
	mm_segment_t fs;
	int8_t *buf;
	int8_t temp_buf[8] = { 0 };
	uint8_t i = 0;
	uint8_t j = 0;
	int32_t ret;
	uint32_t nv_flag = 0;

	pr_info("%s: enter, cali_node = %d\n", __func__, aw9610x->node);

	fp = filp_open(AWINIC_CALI_FILE, O_RDWR | O_CREAT, 0644);

	if (IS_ERR(fp)) {
		pr_err("%s: open failed!\n", __func__);
		return -EINVAL;
	}

	fs = get_fs();
	set_fs(KERNEL_DS);
	buf = kzalloc(CALI_FILE_MAX_SIZE, GFP_KERNEL);
	if (!buf) {
		pr_err("%s: malloc failed!\n", __func__);
		filp_close(fp, NULL);
		set_fs(fs);
		return -EINVAL;
	}
	ret = 0;//vfs_read(fp, buf, CALI_FILE_MAX_SIZE, &(fp->f_pos));
	if (ret < 0) {
		pr_info("%s: read failed\n", __func__);
		return ret;
	} else if (ret == 0) {
		aw9610x->nvspe_data[i] = 0;
	} else {
		if (aw9610x->node == AW_CALI_NORM_MODE)
			return 0;

		for (i = 0; i < 8; i++) {
			for (j = 0; j < 8; j++)
				temp_buf[j] = buf[8 * i + j];

			if (sscanf(temp_buf, "%08x", &aw9610x->nvspe_data[i]) == 1)
				pr_info("%s: nv_spe_data[%d] = 0x%08x\n", __func__, i, aw9610x->nvspe_data[i]);
		}
	}
	set_fs(fs);

	filp_close(fp, NULL);
	kfree(buf);
	/* nvspe_datas come from nv*/

	for (i = 0; i < 8; i++) {
		nv_flag |= aw9610x->nvspe_data[i];
		if (nv_flag != 0)
			break;
	}

	if (aw9610x->node == AW_CALI_NORM_MODE) {
		if (nv_flag == 0) {
			aw9610x->cali_flag = AW_CALI;
			pr_info("%s: the chip need to cali! nv_flag = 0x%08x\n",
							__func__, nv_flag);
		} else {
			aw9610x->cali_flag = AW_NO_CALI;
			pr_info("%s: chip not need to cali! nv_flag = 0x%08x\n",
							__func__, nv_flag);
		}
	}

	return 0;
}

static int32_t aw9610x_store_spedata_to_file(struct aw9610x *aw9610x, char *buf)
{
	struct file *fp = NULL;
	//loff_t pos = 0;
	mm_segment_t fs;

	pr_info("%s: buf = %s\n", __func__, buf);

	fp = filp_open(AWINIC_CALI_FILE, O_RDWR | O_CREAT, 0644);
	if (IS_ERR(fp)) {
		pr_err("%s: open failed!\n", __func__);
		return -EINVAL;
	}

	fs = get_fs();
	set_fs(KERNEL_DS);

	//vfs_write(fp, buf, strlen(buf), &pos);

	set_fs(fs);

	pr_info("%s: write successfully!\n", __func__);

	filp_close(fp, NULL);
	return 0;
}

/******************************************************
 *
 *configuration of special reg
 *
 ******************************************************/
static void aw9610x_get_calidata(struct aw9610x *aw9610x)
{
	uint8_t i = 0;
	uint32_t buf_size = 0;
	int32_t ret;
	uint8_t temp_buf[9] = { 0 };
	uint8_t buf[CALI_FILE_MAX_SIZE] = { 0 };

	pr_info("%s enter\n", __func__);

	/*class 1 special reg*/
	for (i = 0; i < 6; i++) {
		aw9610x_i2c_read(aw9610x,
				REG_AFECFG1_CH0 + i * AW_CL1SPE_CALI_OS,
				&aw9610x->spedata[i]);
		pr_info("%s: specialdata[%d]=0x%08x\n",
				__func__, i, aw9610x->spedata[i]);
	}
	/*class 2 special reg*/
	for (; i < 8; i++) {
		aw9610x_i2c_read(aw9610x,
				REG_REFACFG + (i - 6) * AW_CL2SPE_CALI_OS,
				&aw9610x->spedata[i]);
		pr_info("%s: channel number = 0x%08x\n", __func__,
						aw9610x->spedata[i]);
	}
	/* spedatas come from register*/

	/* write spedatas to nv */
	for (i = 0; i < 8; i++) {
		snprintf(temp_buf, sizeof(temp_buf), "%08x",
							aw9610x->spedata[i]);
		memcpy(buf + buf_size, temp_buf, strlen(temp_buf));
		buf_size = strlen(buf);
	}
	ret = aw9610x_store_spedata_to_file(aw9610x, buf);
	if (ret < 0) {
		pr_info("%s: store spedata failed\n", __func__);
		return;
	}

	pr_info("%s: successfully write_spereg_to_file\n", __func__);
}

static void aw9610x_class1_reg(struct aw9610x *aw9610x)
{
	int32_t i = 0;
	uint32_t reg_val;

	pr_info("%s enter\n", __func__);

	for (i = 0; i < 6; i++) {
		reg_val = (aw9610x->nvspe_data[i] >> 16) & 0x0000ffff;
		aw9610x_i2c_write_bits(aw9610x,
				REG_INITPROX0_CH0 + i * AW_CL1SPE_DEAL_OS,
				~(0xffff), reg_val);
	}
}

static void aw9610x_class2_reg(struct aw9610x *aw9610x)
{
	int32_t i = 0;
	uint32_t reg_val = 0;
	uint32_t ret = 0;

	pr_info("%s enter\n", __func__);

	for (i = 6; i < 8; i++) {
		ret = aw9610x->nvspe_data[i] & 0x07;
		switch (ret) {
		case 0x00:
			aw9610x_i2c_read(aw9610x, REG_VALID_CH0,
							&reg_val);
			break;
		case 0x01:
			aw9610x_i2c_read(aw9610x, REG_VALID_CH1,
							&reg_val);
			break;
		case 0x02:
			aw9610x_i2c_read(aw9610x, REG_VALID_CH2,
							&reg_val);
			break;
		case 0x03:
			aw9610x_i2c_read(aw9610x, REG_VALID_CH3,
							&reg_val);
			break;
		case 0x04:
			aw9610x_i2c_read(aw9610x, REG_VALID_CH4,
							&reg_val);
			break;
		case 0x05:
			aw9610x_i2c_read(aw9610x, REG_VALID_CH5,
							&reg_val);
			break;
		default:
			return;
		}

		reg_val = ((reg_val >> 6) & 0x03fffff0) |
					(aw9610x->nvspe_data[i] & 0xfc00000f);
		aw9610x_i2c_write(aw9610x,
			REG_REFACFG + (i - 6) * AW_CL2SPE_DEAL_OS,
			reg_val);
	}
}

static void aw9610x_spereg_deal(struct aw9610x *aw9610x)
{
	pr_info("%s enter!\n", __func__);

	aw9610x_class1_reg(aw9610x);
	aw9610x_class2_reg(aw9610x);
}

static void aw9610x_datablock_load(struct device *dev, const char *buf)
{
	uint32_t i = 0;
	uint8_t reg_data[220] = { 0 };
	uint32_t databuf[220] = { 0 };
	uint8_t temp_buf[2] = { 0 };
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	uint8_t addr_bytes = aw9610x->aw_i2c_package.addr_bytes;
	uint8_t data_bytes = aw9610x->aw_i2c_package.data_bytes;
	uint8_t reg_num = aw9610x->aw_i2c_package.reg_num;

	for (i = 0; i < data_bytes * reg_num; i++) {
		if (reg_num < attr_buf[1]) {
			temp_buf[0] = buf[attr_buf[0] + (addr_bytes + i) * 5];
			temp_buf[1] =
				    buf[attr_buf[0] + (addr_bytes + i) * 5 + 1];
		} else if (reg_num >= attr_buf[1] && reg_num < attr_buf[3]) {
			temp_buf[0] = buf[attr_buf[2] + (addr_bytes + i) * 5];
			temp_buf[1] =
				    buf[attr_buf[2] + (addr_bytes + i) * 5 + 1];
		} else if (reg_num >= attr_buf[3] && reg_num < attr_buf[5]) {
			temp_buf[0] = buf[attr_buf[4] + (addr_bytes + i) * 5];
			temp_buf[1] =
				    buf[attr_buf[4] + (addr_bytes + i) * 5 + 1];
		}
		if (sscanf(temp_buf, "%02x", &databuf[i]) == 0) {
			pr_info("%s: temp_buf should lager than 0!\n", __func__);
			return;
		}
		reg_data[i] = (uint8_t)databuf[i];
	}
	aw9610x->aw_i2c_package.p_reg_data = reg_data;
	i2c_write_seq(aw9610x);
}

static void aw9610x_reset_channel(struct aw9610x *aw9610x) {
	uint32_t reg_data = 0;
	aw9610x_i2c_read(aw9610x, REG_SCANCTRL0, &reg_data);
	reg_data |= 0x0F00;
	aw9610x_i2c_write(aw9610x, REG_SCANCTRL0, reg_data);
	pr_info("%s reset channel done\n", __func__);
}

static void aw9610x_channel_scan_start(struct aw9610x *aw9610x)
{
	uint32_t reg_data;
	int32_t ret;
	uint32_t temp_time = AW9610X_SCAN_DEFAULT_TIME;

	pr_info("%s: enter\n", __func__);

	aw9610x_reset_channel(aw9610x);

	if (aw9610x->pwprox_dete == true) {
		ret = aw9610x_filedata_deal(aw9610x);
		if ((aw9610x->cali_flag == AW_NO_CALI) && ret >= 0)
			aw9610x_spereg_deal(aw9610x);
	} else {
		aw9610x->cali_flag = AW_NO_CALI;
	}

	aw9610x_i2c_write(aw9610x, REG_HOSTIRQEN, 0);
	aw9610x_i2c_write(aw9610x, REG_CMD, 0x0001);
	while ((temp_time)--) {
		aw9610x_i2c_read(aw9610x, REG_HOSTIRQSRC, &reg_data);
		reg_data = (reg_data >> 4) & 0x01;
		if (reg_data == 1) {
			pr_info("%s: time = %d\n", __func__, temp_time);
			if ((aw9610x->cali_flag == AW_CALI) && ret >= 0)
				aw9610x_get_calidata(aw9610x);
			break;
		}
		udelay(1000);
	}
	aw9610x_i2c_write(aw9610x, REG_HOSTIRQEN, aw9610x->hostirqen);
}

static void aw9610x_bin_valid_loaded(struct aw9610x *aw9610x,
					struct aw_bin *aw_bin_data_s)
{
	uint32_t i;
	uint16_t reg_addr;
	uint32_t reg_data;
	uint32_t start_addr = aw_bin_data_s->header_info[0].valid_data_addr;

	for (i = 0; i < aw_bin_data_s->header_info[0].valid_data_len;
						i += 6, start_addr += 6) {
		reg_addr = (aw_bin_data_s->info.data[start_addr]) |
				aw_bin_data_s->info.data[start_addr + 1] << 8;
		reg_data = aw_bin_data_s->info.data[start_addr + 2] |
			(aw_bin_data_s->info.data[start_addr + 3] << 8) |
			(aw_bin_data_s->info.data[start_addr + 4] << 16) |
			(aw_bin_data_s->info.data[start_addr + 5] << 24);
		aw9610x_i2c_write(aw9610x, reg_addr, reg_data);
		if (reg_addr == REG_HOSTIRQEN)
			aw9610x->hostirqen = reg_data;
		pr_info("%s :reg_addr = 0x%04x, reg_data = 0x%08x\n", __func__,
							reg_addr, reg_data);
	}
	pr_info("%s bin writen completely:\n", __func__);

	aw9610x_channel_scan_start(aw9610x);
}

/***************************************************************************
* para loaded
****************************************************************************/
static int32_t aw9610x_para_loaded(struct aw9610x *aw9610x)
{
	int32_t i = 0;
	int32_t len = ARRAY_SIZE(aw9610x_reg_default);

	pr_info("%s: start to download para!\n", __func__);
	for (i = 0; i < len; i = i + 2) {
		aw9610x_i2c_write(aw9610x,
				(uint16_t)aw9610x_reg_default[i],
				aw9610x_reg_default[i+1]);
		if (aw9610x_reg_default[i] == REG_HOSTIRQEN)
			aw9610x->hostirqen = aw9610x_reg_default[i+1];
		/*pr_info("%s: reg_addr = 0x%04x, reg_data = 0x%08x\n",
						__func__,
						aw9610x_reg_default[i],
						aw9610x_reg_default[i+1]);*/
	}
	pr_info("%s para writen completely:\n", __func__);

	aw9610x_channel_scan_start(aw9610x);

	return 0;
}

static void aw9610x_cfg_all_loaded(const struct firmware *cont, void *context)
{
	int32_t ret;
	struct aw_bin *aw_bin;
	struct aw9610x *aw9610x = context;

	pr_info("%s enter\n", __func__);

	if (!cont) {
		pr_info("%s: %s download failed\n", __func__, aw9610x_cfg_name);
		release_firmware(cont);
		return;
	}

	pr_info("%s download successfully\n", aw9610x_cfg_name);

	aw_bin = kzalloc(cont->size + sizeof(struct aw_bin), GFP_KERNEL);
	if (!aw_bin) {
		kfree(aw_bin);
		release_firmware(cont);
		pr_err("%s: failed to allcating memory!\n", __func__);
		return;
	}
	aw_bin->info.len = cont->size;
	memcpy(aw_bin->info.data, cont->data, cont->size);
	ret = aw9610x_parsing_bin_file(aw_bin);
	if (ret < 0) {
		pr_info("%s:aw9610x parse bin fail! ret = %d\n", __func__, ret);
		kfree(aw_bin);
		release_firmware(cont);
		return;
	}

	ret = strcmp(aw9610x->chip_name, aw_bin->header_info[0].chip_type);
	if (ret != 0) {
		pr_info("%s:chip name(%s) incompatible with bin chip(%s)\n",
					__func__, aw9610x->chip_name,
					aw_bin->header_info[0].chip_type);
		kfree(aw_bin);
		release_firmware(cont);
		return;
	}
	aw9610x_bin_valid_loaded(aw9610x, aw_bin);
	kfree(aw_bin);
	release_firmware(cont);
}

static int32_t aw9610x_cfg_update(struct aw9610x *aw9610x)
{
	pr_info("%s: enter\n", __func__);

	if (aw9610x->firmware_flag == true)
		return request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
							aw9610x_cfg_name,
							aw9610x->dev,
							GFP_KERNEL,
							aw9610x,
							aw9610x_cfg_all_loaded);
	else
		aw9610x_para_loaded(aw9610x);

	return AW_SAR_SUCCESS;
}

static void aw9610x_cfg_work_routine(struct work_struct *work)
{
	struct aw9610x
		*aw9610x = container_of(work, struct aw9610x, cfg_work.work);

	pr_info("%s: enter\n", __func__);

	aw9610x_cfg_update(aw9610x);
}

static int32_t aw9610x_sar_cfg_init(struct aw9610x *aw9610x, int32_t flag)
{
	uint32_t cfg_timer_val = 0, i = 0;

	pr_info("%s: enter\n", __func__);

	if (flag == AW_CFG_LOADED) {
		cfg_timer_val = 20;
		aw9610x->node = AW_CALI_NODE_MODE;
	} else if (flag == AW_CFG_UNLOAD) {
		cfg_timer_val = 5000;
		aw9610x->node = AW_CALI_NORM_MODE;
	} else {
		return -AW_CFG_LOAD_TIME_FAILED;
	}

	pr_info("%s: cali_node = %d\n", __func__, aw9610x->node);

	INIT_DELAYED_WORK(&aw9610x->cfg_work, aw9610x_cfg_work_routine);
	schedule_delayed_work(&aw9610x->cfg_work,
					      msecs_to_jiffies(cfg_timer_val));

	for (i = 0; i < AW_SAR_CAHNNEL_MAX; i++) {
		aw9610x->curr_state[i] = 0;
		aw9610x->last_state[i] = 0;
	}

	return AW_SAR_SUCCESS;
}

/*****************************************************
 *
 * first irq clear
 *
 *****************************************************/
static int32_t aw9610x_init_irq_handle(struct aw9610x *aw9610x)
{
	uint8_t cnt = 20;
	uint32_t reg_data;

	pr_info("%s enter\n", __func__);
	while (cnt--) {
		aw9610x_i2c_read(aw9610x, REG_HOSTIRQSRC, &reg_data);
		aw9610x->first_irq_flag = reg_data & 0x01;
		if (aw9610x->first_irq_flag == 1) {
			pr_info("%s: cnt = %d\n", __func__, cnt);
			return AW_SAR_SUCCESS;
		}
		udelay(1000);
	}
	pr_err("%s: hardware has trouble!\n", __func__);

	return -AW_IRQIO_FAILED;
}

/*****************************************************
 *
 * software reset
 *
 *****************************************************/
static void aw9610x_sw_reset(struct aw9610x *aw9610x)
{
	pr_info("%s: enter\n", __func__);

	aw9610x_i2c_write(aw9610x, REG_HOSTCTRL2, 0);
}

/******************************************************
 *
 * sys group attribute
 *
 ******************************************************/
static ssize_t aw9610x_set_reg_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	uint32_t databuf[2] = { 0, 0 };

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2)
		aw9610x_i2c_write(aw9610x,
				(uint16_t)databuf[0],
				(uint32_t)databuf[1]);

	return count;
}

static ssize_t aw9610x_get_reg_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	ssize_t len = 0;
	uint32_t i = 0;
	uint32_t reg_val = 0;
	uint32_t reg_num = 0;

	reg_num = ARRAY_SIZE(aw9610x_reg_access);
	for (i = 0; i < reg_num; i++)
		if (aw9610x_reg_access[i].rw & REG_RD_ACCESS) {
			aw9610x_i2c_read(aw9610x,
					aw9610x_reg_access[i].reg,
					&reg_val);
			len += snprintf(buf + len, PAGE_SIZE - len,
						"reg:0x%04x=0x%08x\n",
						aw9610x_reg_access[i].reg,
						reg_val);
		}

	return len;
}

static ssize_t aw9610x_valid_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	ssize_t len = 0;
	uint8_t i = 0;
	uint32_t reg_val = 0;

	for (i = 0; i < AW_SAR_CAHNNEL_MAX; i++) {
		aw9610x_i2c_read(aw9610x, REG_VALID_CH0 + i * 4, &reg_val);
		reg_val /= 1024;
		len += snprintf(buf+len, PAGE_SIZE-len,
					"VALID_CH%d = %d\n", i, reg_val);
	}

	return len;
}

static ssize_t aw9610x_baseline_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	ssize_t len = 0;
	uint8_t i = 0;
	uint32_t reg_val = 0;

	for (i = 0; i < AW_SAR_CAHNNEL_MAX; i++) {
		aw9610x_i2c_read(aw9610x, REG_BASELINE_CH0 + i * 4, &reg_val);
		reg_val /= 1024;
		len += snprintf(buf+len, PAGE_SIZE-len,
					"BASELINE_CH%d = %d\n", i, reg_val);
	}

	return len;
}

static ssize_t aw9610x_diff_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	int32_t diff = 0, valid = 0, baseline = 0;

	aw9610x_i2c_read(aw9610x, REG_DIFF_CH0 + diff_ch_num * 4, &diff);
	aw9610x_i2c_read(aw9610x, REG_VALID_CH0 + diff_ch_num * 4, &valid);
	aw9610x_i2c_read(aw9610x, REG_BASELINE_CH0 + diff_ch_num * 4, &baseline);
	diff /= 1024;
	valid /= 1024;
	baseline /= 1024;

	pr_err("%s - %d,%d,%d\n", __func__, valid, baseline, diff);
	return snprintf(buf, 64, "%d,%d,%d\n", valid, baseline, diff);

}

static ssize_t aw9610x_diff_chx_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int chx = 1;

	if (sscanf(buf, "%d", &chx) != 1) {
		pr_err("%s - The number of data are wrong\n", __func__);
		return -EINVAL;
	}
	diff_ch_num = chx;

	return count;
}

static ssize_t aw9610x_raw_data_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	ssize_t len = 0;
	uint8_t i = 0;
	uint32_t reg_val = 0;

	for (i = 0; i < AW_SAR_CAHNNEL_MAX; i++) {
		aw9610x_i2c_read(aw9610x, REG_RAW_CH0 + i * 4, &reg_val);
		reg_val /= 1024;
		len += snprintf(buf+len, PAGE_SIZE-len,
					"RAW_DATA_CH%d = %d\n", i, reg_val);
	}

	return len;
}

static ssize_t aw9610x_awrw_get(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	uint8_t reg_data[228] = { 0 };
	uint8_t i = 0;
	ssize_t len = 0;
	uint8_t reg_num = aw9610x->aw_i2c_package.reg_num;
	uint8_t data_bytes = aw9610x->aw_i2c_package.data_bytes;

	for (i = 0; i < reg_num * data_bytes - 1; i++) {
		i2c_read_seq(aw9610x, reg_data);
		len += snprintf(buf + len, PAGE_SIZE - len,
					"0x%02x,", reg_data[i]);
	}
	if (!i)
		i2c_read_seq(aw9610x, reg_data);
	len += snprintf(buf + len, PAGE_SIZE - len,
					"0x%02x\n", reg_data[i]);

	return len;
}

static ssize_t aw9610x_cali_set(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	uint32_t databuf[1] = { 0 };

	if (sscanf(buf, "%d", &databuf[0]) == 1) {
		if ((databuf[0] == 1) && (aw9610x->pwprox_dete == true)) {
			aw9610x_sw_reset(aw9610x);
			aw9610x->cali_flag = AW_CALI;
		} else {
			pr_info("%s:aw_unsupport the pw_prox_dete=%d\n",
						__func__, aw9610x->pwprox_dete);
			return count;
		}
		aw9610x_sar_cfg_init(aw9610x, AW_CFG_LOADED);
	}

	return count;
}

static ssize_t aw9610x_awrw_set(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	uint32_t datatype[3] = { 0 };

	if (sscanf(buf, "%d %d %d", &datatype[0], &datatype[1],
							&datatype[2]) == 3) {
		aw9610x->aw_i2c_package.addr_bytes = (uint8_t)datatype[0];
		aw9610x->aw_i2c_package.data_bytes = (uint8_t)datatype[1];
		aw9610x->aw_i2c_package.reg_num = (uint8_t)datatype[2];

		aw9610x_addrblock_load(dev, buf);
		if (count > 7 + 5 * aw9610x->aw_i2c_package.addr_bytes)
			aw9610x_datablock_load(dev, buf);
	}

	return count;
}

static ssize_t aw9610x_calibrate_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	uint32_t reg_data = 0;

	aw9610x_i2c_read(aw9610x, REG_SCANCTRL0, &reg_data);
	pr_info("%s calibrate reg value is 0x%x\n", __func__, reg_data);
	return snprintf(buf, 64, "%x\n", reg_data);
}

static ssize_t aw9610x_calibrate_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	uint32_t reg_data = 0;

	aw9610x_i2c_read(aw9610x, REG_SCANCTRL0, &reg_data);
	reg_data |= 0x0F00;
	aw9610x_i2c_write(aw9610x, REG_SCANCTRL0, reg_data);
	pr_info("%s calibrate done!\n", __func__);
	return count;
}

static ssize_t aw9610x_set_update(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	ssize_t ret;
	uint32_t state;
	int32_t cfg_timer_val = 10;
	struct aw9610x *aw9610x = dev_get_drvdata(dev);

	ret = kstrtouint(buf, 10, &state);
	if (ret) {
		pr_err("%s: fail to change str to int\n", __func__);
		return ret;
	}
	if (state)
		schedule_delayed_work(&aw9610x->cfg_work,
				      msecs_to_jiffies(cfg_timer_val));

	return count;
}

static ssize_t aw9610x_batch_store(struct device *dev,
			struct device_attribute *attr,  const char *buf,  size_t count)
{
	return count;
}
static ssize_t aw9610x_flush_store(struct device *dev,
			struct device_attribute *attr,  const char *buf,  size_t count)
{
	return count;
}
static ssize_t aw9610x_flush_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 64, "0\n");
}

static ssize_t aw9610x_enable_store(struct device *dev,
			struct device_attribute *attr,  const char *buf,  size_t count)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	int en;

	if (sscanf(buf, "%d", &en) != 1) {
		pr_err("%s - The number of data are wrong\n", __func__);
		return -EINVAL;
	}
	pr_info("%s en = %d", __func__, en);
	if (en == 0)
		en = 2;
	aw9610x_i2c_write(aw9610x, REG_CMD, en);
	return count;
}

static ssize_t aw9610x_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	uint32_t steady_st = 0;
	uint32_t reg_data = 0;
	struct aw9610x *aw9610x = dev_get_drvdata(dev);

	aw9610x_i2c_read(aw9610x, REG_STAT1, &reg_data);
	steady_st = (reg_data >> 24) & 0x2;
	pr_err("%s steady_st value is %d", __func__, steady_st);
	if ((aw9610x->status == 0) && (steady_st != 0x02)) {
		return snprintf(buf, 64, "1\n");
	} else {
		return snprintf(buf, 64, "0\n");
	}
}

static DEVICE_ATTR(reg, 0664, aw9610x_get_reg_show, aw9610x_set_reg_store);
static DEVICE_ATTR(valid, 0664, aw9610x_valid_show, NULL);
static DEVICE_ATTR(baseline, 0664, aw9610x_baseline_show, NULL);
static DEVICE_ATTR(diff, 0664, aw9610x_diff_show, aw9610x_diff_chx_store);
static DEVICE_ATTR(raw_data, 0664, aw9610x_raw_data_show, NULL);
static DEVICE_ATTR(cali, 0664, NULL, aw9610x_cali_set);
static DEVICE_ATTR(awrw, 0664, aw9610x_awrw_get, aw9610x_awrw_set);
static DEVICE_ATTR(calibrate, 0664, aw9610x_calibrate_show, aw9610x_calibrate_store);
static DEVICE_ATTR(batch, 0664, NULL, aw9610x_batch_store);
static DEVICE_ATTR(enable, 0664, NULL, aw9610x_enable_store);
static DEVICE_ATTR(flush, 0664, aw9610x_flush_show, aw9610x_flush_store);
static DEVICE_ATTR(status, 0444, aw9610x_status_show, NULL);
static DEVICE_ATTR(update, 0444, NULL, aw9610x_set_update);

static struct attribute *aw9610x_sar_attributes[] = {
	&dev_attr_reg.attr,
	&dev_attr_valid.attr,
	&dev_attr_baseline.attr,
	&dev_attr_diff.attr,
	&dev_attr_raw_data.attr,
	&dev_attr_awrw.attr,
	&dev_attr_cali.attr,
	&dev_attr_calibrate.attr,
	&dev_attr_enable.attr,
	&dev_attr_batch.attr,
	&dev_attr_flush.attr,
	&dev_attr_status.attr,
	&dev_attr_update.attr,
	NULL
};

static struct attribute_group aw9610x_sar_attribute_group = {
	.attrs = aw9610x_sar_attributes
};

#if (defined CONFIG_VENDOR_SOC_SPRD_COMPILE) || (defined CONFIG_VENDOR_SOC_QCOM_COMPILE)
static int32_t aw9610x_input_sys_init(struct aw9610x *aw9610x)
{
	int32_t ret = 0;
	struct input_dev *input_dev;

	pr_info("%s: enter\n", __func__);
	input_dev = input_allocate_device();
	if (!input_dev)
		return -AW_INPUT_ALLOCATE_FILED;

	input_dev->name = "sar_sensor";
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(EV_SYN, input_dev->evbit);
	__set_bit(KEY_F1, input_dev->keybit);
	input_set_abs_params(input_dev, ABS_DISTANCE, 0, 100, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_RX);
	input_set_capability(input_dev, EV_ABS, ABS_RY);
	input_set_capability(input_dev, EV_ABS, ABS_RZ);
	ret = input_register_device(input_dev);
	if (ret) {
		pr_err("%s: failed to register input device: %s\n", __func__,
						dev_name(&aw9610x->i2c->dev));
		input_free_device(input_dev);
		return -AW_INPUT_REGISTER_FAILED;
	}
	aw9610x->input = input_dev;

	return AW_SAR_SUCCESS;
}
#endif

static void aw9610x_interrupt_clear(struct aw9610x *aw9610x)
{
	uint32_t report_data[3] = {10, 0, 0};
	uint32_t state0 = 0;
	int i = 0;
#ifdef CONFIG_VENDOR_SOC_MTK_COMPILE
#ifdef SENSOR_ARCH_2_0
	struct hf_manager *manager = aw9610x->hf_dev.manager;
	struct hf_manager_event event;

	if (!manager)
		return;
	memset(&event, 0, sizeof(struct hf_manager_event));
	event.timestamp = ktime_get_boot_ns();
	event.sensor_type = SENSOR_TYPE_SAR;
	event.action = DATA_ACTION;
#endif
#endif

	pr_info("%s enter\n", __func__);

	aw9610x_i2c_read(aw9610x, REG_HOSTIRQSRC, &aw9610x->irq_status);
	aw9610x_i2c_read(aw9610x, REG_STAT0, &state0);
	pr_info("sar state0 = 0x%08x\n", state0);

	for (i = 0; i < AW_SAR_CAHNNEL_MAX; i++) {
		aw9610x->curr_state[i] =
					 (((uint8_t)(state0 >> (24 + i)) & 0x01) << 0) | /* prox0 */
					 (((uint8_t)(state0 >> (16 + i)) & 0x01) << 1) | /* prox1 */
					 (((uint8_t)(state0 >> (8 + i)) & 0x01) << 2) | /* prox2 */
					 (((uint8_t)(state0 >> (0 + i)) & 0x01) << 3);  /* prox3 */
	}

	for (i = 0; i < AW_SAR_CAHNNEL_MAX; i++) {
		if (aw9610x->last_state[i] !=
			aw9610x->curr_state[i]) {
			report_data[1] = i;
			aw9610x->status = 0;
			switch (aw9610x->curr_state[i]) {
			case FAR:
				report_data[0] = (i + 1) * 10; /* no close */
				aw9610x->status = 5;
				break;
			case TRIGGER_TH0:
				report_data[0] = i + 1; /* prox0 */
				break;
			case TRIGGER_TH1:
				report_data[0] = i + 21; /* prox1 */
				break;
			case TRIGGER_TH2:
				report_data[0] = i + 31; /* prox2 */
				break;
			case TRIGGER_TH3:
				report_data[0] = i + 41; /* prox3 */
				break;
			default:
				pr_err("sar ch[%d] err", i);
				aw9610x->status = 1;
				return;
			}
		}
		aw9610x->last_state[i] = aw9610x->curr_state[i];
	}

#if (defined CONFIG_VENDOR_SOC_SPRD_COMPILE) || (defined CONFIG_VENDOR_SOC_QCOM_COMPILE)
	input_report_abs(aw9610x->input, ABS_RX, report_data[0]);
	input_report_abs(aw9610x->input, ABS_RY, report_data[1]);
	input_sync(aw9610x->input);
#endif
#ifdef CONFIG_VENDOR_SOC_MTK_COMPILE
#ifdef SENSOR_ARCH_1_0
	sar_data_report(report_data);
#endif
#ifdef SENSOR_ARCH_2_0
	event.word[0] = report_data[0];
	event.word[1] = report_data[1];
	manager->report(manager, &event);
	manager->complete(manager);
#endif
#endif
	pr_info("sar %s channel[%d] status %d\n", __func__, report_data[1], report_data[0]);
}

static irqreturn_t aw9610x_irq(int32_t irq, void *data)
{
	struct aw9610x *aw9610x = data;

	pr_info("%s enter\n", __func__);

	aw9610x_interrupt_clear(aw9610x);
	pr_info("%s exit\n", __func__);

	return IRQ_HANDLED;
}

static int32_t aw9610x_interrupt_init(struct aw9610x *aw9610x)
{
	int32_t irq_flags = 0;
	int32_t ret = 0;

	pr_info("%s enter\n", __func__);

	if (gpio_is_valid(aw9610x->irq_gpio)) {
		ret = devm_gpio_request_one(&aw9610x->i2c->dev,
						aw9610x->irq_gpio,
						GPIOF_DIR_IN,
						"aw9610x_irq_gpio");
		if (ret) {
			pr_err("%s: sar request irq gpio failed, ret = %d\n",
					__func__, ret);
		}

		/* register irq handler */
		irq_flags = IRQF_TRIGGER_LOW | IRQF_ONESHOT | IRQF_SHARED;
		ret = devm_request_threaded_irq(&aw9610x->i2c->dev,
					gpio_to_irq(aw9610x->irq_gpio),
					NULL, aw9610x_irq, irq_flags,
					"aw9610x_irq", aw9610x);
		if (ret != 0) {
			pr_err("%s: failed to request IRQ %d: %d\n",
							__func__,
					gpio_to_irq(aw9610x->irq_gpio),
							ret);
			ret = -AW_IRQ_REQUEST_FAILED;
		} else {
			pr_info("%s: IRQ request successfully!\n", __func__);
			ret = AW_SAR_SUCCESS;
		}
	} else {
		pr_err("%s: irq gpio invalid!\n", __func__);
		return -AW_IRQIO_FAILED;
	}
	return ret;
}

/*****************************************************
 *
 * parse dts
 *
 *****************************************************/
static void aw9610x_parse_dt(struct device *dev, struct aw9610x *aw9610x,
			   struct device_node *np)
{
	int32_t val = 0;

	aw9610x->irq_gpio = of_get_named_gpio(np, "irq-gpio", 0);
	if (aw9610x->irq_gpio < 0) {
		aw9610x->irq_gpio = -1;
		pr_err("%s: no irq gpio provided.\n", __func__);
		return;
	}

	pr_info("%s: irq gpio provided ok.\n", __func__);

	val = of_property_read_string(np, "chip_name", &aw9610x->chip_name);
	if (val != 0) {
		aw9610x->chip_name = NULL;
		pr_info("%s: failed to find chip name\n", __func__);
	} else {
		pr_info("%s: the chip name is %s detected\n",
						__func__, aw9610x->chip_name);
	}

	aw9610x->firmware_flag =
			of_property_read_bool(np, "aw9610x,using-firmware");
	pr_info("%s firmware_flag = <%d>\n", __func__, aw9610x->firmware_flag);

	aw9610x->pwprox_dete =
		of_property_read_bool(np, "aw9610x,using-pwon-prox-dete");
	pr_info("%s pwprox_dete = <%d>\n", __func__, aw9610x->pwprox_dete);
}

/*****************************************************
 *
 * check chip id
 *
 *****************************************************/
static int32_t aw9610x_read_chipid(struct aw9610x *aw9610x)
{
	int32_t ret = -1;
	uint8_t cnt = 0;
	uint32_t reg_val = 0;

	while (cnt < AW_READ_CHIPID_RETRIES) {
		ret = aw9610x_i2c_read(aw9610x, REG_CHIP_ID, &reg_val);
		if (ret < 0) {
			pr_err("%s: read CHIP ID failed: %d\n", __func__, ret);
		} else {
			reg_val = reg_val >> 16;
			break;
		}

		cnt++;
		usleep_range(2000, 3000);
	}

	if (reg_val == AW9610X_CHIP_ID) {
		pr_info("%s aw9610x detected\n", __func__);
		return AW_SAR_SUCCESS;
	} else {
		pr_info("%s unsupported device,the chipid is (0x%04x)\n",
							__func__, reg_val);
	}

	return -AW_CHIPID_FAILED;
}

static void aw9610x_i2c_set(struct i2c_client *i2c, struct aw9610x *aw9610x)
{
	pr_info("%s: enter\n", __func__);
	aw9610x->dev = &i2c->dev;
	aw9610x->i2c = i2c;
	i2c_set_clientdata(i2c, aw9610x);
}

#ifdef CONFIG_VENDOR_SOC_MTK_COMPILE
#ifdef SENSOR_ARCH_1_0
static int sar_open_report_data(int open)
{
	return 0;
}

static int sar_batch(int flag, int64_t samplingPeriodNs, int64_t maxBatchReportLatencyNs)
{
	return 0;
}

static int sar_flush(void)
{
	situation_flush_report(ID_SAR);
	return 0;
}

static int sar_get_data(int *value, int *status)
{
	pr_err("%s not supported!\n", __func__);
	return 0;
}

extern int sensorlist_register_deviceinfo(int sensor, struct sensorInfo_NonHub_t *devinfo);
#endif

#ifdef SENSOR_ARCH_2_0
static struct sensor_info support_sensors = {
	.sensor_type = SENSOR_TYPE_SAR,
	.gain = 1,
	.name = AW9610X_I2C_NAME,
	.vendor = "awinic",
};

static int aw9610x_batch(struct hf_device *hfdev,
		int sensor_type, int64_t delay, int64_t latency)
{
	return 0;
}

static int aw9610x_flush(struct hf_device *hfdev,
		int sensor_type)
{
	struct hf_manager *manager = aw_sar_ptr->hf_dev.manager;
	struct hf_manager_event event;
	memset(&event, 0, sizeof(struct hf_manager_event));

	event.timestamp = ktime_get_boot_ns();
	event.sensor_type = SENSOR_TYPE_SAR;
	event.action = FLUSH_ACTION;

	manager->report(manager, &event);

	return 0;
}

static int aw9610x_enable(struct hf_device *hfdev,
		int sensor_type, int en)
{
	if (!aw_sar_ptr) {
		pr_err("%s aw_sar_ptr null!\n", __func__);
		return 0;
	}

	pr_info("%s en = %d", __func__, en);
	aw9610x_i2c_write(aw_sar_ptr, REG_CMD, en);
	return 0;
}
#endif
#endif

static struct class sar_sensor_class = {
	.name = "sarsensor",
	.owner = THIS_MODULE,
};

static ssize_t delay_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	pr_info("awinic sar %s: enter\n", __func__);
	return snprintf(buf, 8, "%d\n", 200);
}

static ssize_t delay_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	pr_info("awinic sar %s: enter\n", __func__);
	return count;
}

static CLASS_ATTR_RW(delay);

static ssize_t enable_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	int en;

	if (!aw_sar_ptr) {
		pr_err("%s aw_sar_ptr null!\n", __func__);
		return count;
	}

	if (sscanf(buf, "%d", &en) != 1) {
		pr_err("%s - The number of data are wrong\n", __func__);
		return -EINVAL;
	}
	pr_info("%s en = %d", __func__, en);
	if (en == 0)
		en = 2;
	aw9610x_i2c_write(aw_sar_ptr, REG_CMD, en);
	return count;
}

static CLASS_ATTR_WO(enable);

static ssize_t chip_info_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	pr_info("awinic sar %s: enter, chip=%s\n", __func__, chip_info);
	return snprintf(buf, 25, "%s", chip_info);
}

static CLASS_ATTR_RO(chip_info);

static ssize_t status_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	uint32_t status = 0;
	uint32_t reg_data = 0;

	if (aw_sar_ptr) {
		aw9610x_i2c_read(aw_sar_ptr, REG_STAT1, &reg_data);
		status = (reg_data >> 24) & 0x2;
		pr_info("%s status value is %d", __func__, status);
		if ((aw_sar_ptr->status == 0) && (status != 0x02)) {
			return snprintf(buf, 64, "1\n");
		} else {
			return snprintf(buf, 64, "0\n");
		}
	}

	pr_err("%s aw_sar_ptr is NULL!!!", __func__);
	return snprintf(buf, 64, "1\n");
}

static CLASS_ATTR_RO(status);

static ssize_t diff_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	uint32_t valid = 0;
	uint32_t baseline = 0;
	uint32_t diff = 0;

	if (aw_sar_ptr) {
		aw9610x_i2c_read(aw_sar_ptr, REG_DIFF_CH0 + diff_ch_num * 4, &diff);
		aw9610x_i2c_read(aw_sar_ptr, REG_VALID_CH0 + diff_ch_num * 4, &valid);
		aw9610x_i2c_read(aw_sar_ptr, REG_BASELINE_CH0 + diff_ch_num * 4, &baseline);
		diff /= 1024;
		valid /= 1024;
		baseline /= 1024;
	}
	pr_err("%s - %d,%d,%d\n", __func__, valid, baseline, diff);
	return snprintf(buf, 64, "%d,%d,%d\n", valid, baseline, diff);
}

static ssize_t diff_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	int chx = 1;

	if (sscanf(buf, "%d", &chx) != 1) {
		pr_err("%s - The number of data are wrong\n", __func__);
		return -EINVAL;
	}
	diff_ch_num = chx;
	return count;
}

static CLASS_ATTR_RW(diff);

static ssize_t batch_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	pr_info("%s entry", __func__);
	return snprintf(buf, 64, "200\n");
}

static ssize_t batch_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	pr_info("%s entry", __func__);
	return count;
}
static CLASS_ATTR_RW(batch);

static ssize_t flush_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	pr_info("%s entry", __func__);
	return snprintf(buf, 64, "0\n");
}

static ssize_t flush_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	pr_info("%s entry", __func__);
	return count;
}
static CLASS_ATTR_RW(flush);

static ssize_t calibrate_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	uint32_t reg_data = 0;

	if (aw_sar_ptr) {
		aw9610x_i2c_read(aw_sar_ptr, REG_SCANCTRL0, &reg_data);
		pr_info("%s calibrate reg value is 0x%x\n", __func__, reg_data);
		return snprintf(buf, 64, "%x\n", reg_data);
	}

	pr_err("%s aw_sar_ptr is NULL!!!", __func__);
	return snprintf(buf, 64, "%d\n", reg_data);
}

static ssize_t calibrate_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	uint32_t reg_data = 0;

	if (aw_sar_ptr) {
		aw9610x_i2c_read(aw_sar_ptr, REG_SCANCTRL0, &reg_data);
		pr_info("%s calibrate reg before value is 0x%x\n", __func__, reg_data);
		reg_data |= 0x0F00;
		pr_info("%s calibrate reg after value is 0x%x\n", __func__, reg_data);
		aw9610x_i2c_write(aw_sar_ptr, REG_SCANCTRL0, reg_data);
		return count;
	}

	pr_err("%s aw_sar_ptr is NULL!!!", __func__);
	return count;
}
static CLASS_ATTR_RW(calibrate);

static int create_class_sys() {
	int32_t ret = 0;

	ret = class_register(&sar_sensor_class);
	if (ret < 0) {
		pr_info("%s: class_register\n", __func__);
		return ret;
	}

	ret = class_create_file(&sar_sensor_class, &class_attr_delay);
	if (ret < 0) {
		pr_info("%s: class_attr_delay!\n", __func__);
		return ret;
	}

	ret = class_create_file(&sar_sensor_class, &class_attr_enable);
	if (ret < 0) {
		pr_info("%s: class_attr_enable!\n", __func__);
		return ret;
	}

	ret = class_create_file(&sar_sensor_class, &class_attr_chip_info);
	if (ret < 0) {
		pr_info("%s: class_attr_chip_info!\n", __func__);
		return ret;
	}

	ret = class_create_file(&sar_sensor_class, &class_attr_status);
	if (ret < 0) {
		pr_info("%s: class_attr_status!\n", __func__);
		return ret;
	}

	ret = class_create_file(&sar_sensor_class, &class_attr_diff);
	if (ret < 0) {
		pr_info("%s: class_attr_diff!\n", __func__);
		return ret;
	}

	ret = class_create_file(&sar_sensor_class, &class_attr_batch);
	if (ret < 0) {
		pr_info("%s: class_attr_batch!\n", __func__);
		return ret;
	}

	ret = class_create_file(&sar_sensor_class, &class_attr_flush);
	if (ret < 0) {
		pr_info("%s: class_attr_flush!\n", __func__);
		return ret;
	}

	ret = class_create_file(&sar_sensor_class, &class_attr_calibrate);
	if (ret < 0) {
		pr_info("%s: class_attr_calibrate!\n", __func__);
		return ret;
	}

	return ret;
}

static int32_t aw9610x_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	struct aw9610x *aw9610x;
	struct device_node *np = i2c->dev.of_node;
	int32_t ret = 0;
#ifdef CONFIG_VENDOR_SOC_MTK_COMPILE
#ifdef SENSOR_ARCH_1_0
	struct situation_control_path ctl = {0};
	struct situation_data_path data = {0};
	struct sensorInfo_NonHub_t devinfo = {"aw9610x"};
#endif
#endif

	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C)) {
		dev_err(&i2c->dev, "check_functionality failed\n");
		return -EIO;
	}

	snprintf(chip_info, sizeof(chip_info), "%s", "awinic");

	aw9610x = devm_kzalloc(&i2c->dev, sizeof(struct aw9610x), GFP_KERNEL);
	if (aw9610x == NULL) {
		pr_err("%s:failed to malloc memory for aw9610x!\n", __func__);
		ret = -AW_MALLOC_FAILED;
		goto err_malloc;
	}
	aw_sar_ptr = aw9610x;

	aw9610x_i2c_set(i2c, aw9610x);

	/* aw9610x chip id */
	ret = aw9610x_read_chipid(aw9610x);
	if (ret != AW_SAR_SUCCESS) {
		pr_err("%s: read chipid failed, ret=%d\n", __func__, ret);
		goto err_chipid;
	}

	aw9610x_sw_reset(aw9610x);

	// wait 10ms for hardware to soft reset
	mdelay(20);

	ret = aw9610x_init_irq_handle(aw9610x);
	if (ret != AW_SAR_SUCCESS) {
		pr_err("%s: hardware has trouble!, ret=%d\n", __func__, ret);
		goto err_first_irq;
	}

	aw9610x_parse_dt(&i2c->dev, aw9610x, np);

#if (defined CONFIG_VENDOR_SOC_SPRD_COMPILE) || (defined CONFIG_VENDOR_SOC_QCOM_COMPILE)
	ret = aw9610x_input_sys_init(aw9610x);
	if (ret == -AW_INPUT_ALLOCATE_FILED) {
		pr_err("%s:allocate input failed, ret = %d\n", __func__, ret);
		goto exit_input_dev_alloc_failed;
	} else if (ret == -AW_INPUT_REGISTER_FAILED) {
		pr_err("%s:register input failed, ret = %d\n", __func__, ret);
		goto exit_input_register_device_failed;
	}
#endif

#ifdef CONFIG_VENDOR_SOC_MTK_COMPILE
#ifdef SENSOR_ARCH_1_0
	ctl.open_report_data = sar_open_report_data;
	ctl.batch = sar_batch;
	ctl.flush = sar_flush;
	ctl.is_support_wake_lock = true; // To be considered later
	ctl.is_support_batch = false;
	ret = situation_register_control_path(&ctl, ID_SAR);
	if (ret) {
		pr_err("register sar contrl path err\n");
		return ret;
	}

	data.get_data = sar_get_data;
	ret = situation_register_data_path(&data, ID_SAR);
	if (ret) {
		pr_err("register sar data path err\n");
		return ret;
	}
	sensorlist_register_deviceinfo(ID_SAR, &devinfo);
#endif

#ifdef SENSOR_ARCH_2_0
	memset(&aw9610x->hf_dev, 0, sizeof(struct hf_device));
	aw9610x->hf_dev.dev_name = AW9610X_I2C_NAME;
	aw9610x->hf_dev.device_poll = HF_DEVICE_IO_INTERRUPT;
	aw9610x->hf_dev.device_bus = HF_DEVICE_IO_ASYNC;
	aw9610x->hf_dev.support_list = &support_sensors;
	aw9610x->hf_dev.support_size = 1;
	aw9610x->hf_dev.enable = aw9610x_enable;
	aw9610x->hf_dev.batch = aw9610x_batch;
	aw9610x->hf_dev.flush = aw9610x_flush;
	ret = hf_manager_create(&aw9610x->hf_dev);
	if (ret < 0) {
		pr_err("%s hf_manager_create fail\n", __func__);
		return ret;
	}
#endif
#endif

	/* attribute */
	ret = sysfs_create_group(&i2c->dev.kobj, &aw9610x_sar_attribute_group);
	if (ret < 0) {
		dev_info(&i2c->dev, "%s error creating sysfs attr files\n",
			 __func__);
		goto err_sysfs;
	}

	ret = create_class_sys();
	if (ret < 0) {
		pr_err("%s create_class_sys err\n", __func__);
		goto err_class_sysfs;
	}

	ret = aw9610x_sar_cfg_init(aw9610x, AW_CFG_UNLOAD);
	if (ret < 0) {
		pr_err("%s: cfg situation not confirmed!\n", __func__);
		goto err_cfg;
	}

	aw9610x->status = 1;
	aw9610x->ch_num = 0;

	ret = aw9610x_interrupt_init(aw9610x);
	if (ret == -AW_IRQ_REQUEST_FAILED) {
		pr_err("%s: request irq failed!, ret=%d\n", __func__, ret);
		goto err_request_irq;
	}

	pr_info("%s: probe successfully\n!\n", __func__);
	return AW_SAR_SUCCESS;

err_request_irq:
	devm_gpio_free(&i2c->dev, aw9610x->irq_gpio);
err_cfg:
err_class_sysfs:
	class_unregister(&sar_sensor_class);
err_sysfs:
	sysfs_remove_group(&i2c->dev.kobj, &aw9610x_sar_attribute_group);
#if (defined CONFIG_VENDOR_SOC_SPRD_COMPILE) || (defined CONFIG_VENDOR_SOC_QCOM_COMPILE)
exit_input_register_device_failed:
exit_input_dev_alloc_failed:
#endif
	devm_kfree(&i2c->dev, aw9610x);
err_first_irq:
err_chipid:
err_malloc:
	pr_info("%s: err_class_creat fail!\n", __func__);
	return ret;

}

static int32_t aw9610x_i2c_remove(struct i2c_client *i2c)
{
	struct aw9610x *aw9610x = i2c_get_clientdata(i2c);
	devm_kfree(&i2c->dev, aw9610x);
#ifdef CONFIG_VENDOR_SOC_MTK_COMPILE
#ifdef SENSOR_ARCH_2_0
	hf_manager_destroy(aw9610x->hf_dev.manager);
#endif
#endif
#if (defined CONFIG_VENDOR_SOC_SPRD_COMPILE) || (defined CONFIG_VENDOR_SOC_QCOM_COMPILE)
	input_unregister_device(aw9610x->input);
#endif
	if (class_dev != NULL) {
		sysfs_remove_link(&class_dev->kobj, "sar_sensor");
		class_dev = NULL;
	}
	if (cls != NULL) {
		device_destroy(cls, aw9610x_cls_device_dev_t);
		class_destroy(cls);
		cls = NULL;
	}
	return 0;
}

static int aw9610x_suspend(struct device *dev)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);

	if (aw9610x != NULL) {
		pr_info("%s enter suspend enter", __func__);
		aw9610x_i2c_write(aw9610x, REG_CMD, 0x02); // enter sleep mode
		disable_irq(gpio_to_irq(aw9610x->irq_gpio));
	}
	return 0;
}

static int aw9610x_resume(struct device *dev)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);

	if (aw9610x != NULL) {
		pr_info("%s enter resume enter", __func__);
		aw9610x_i2c_write(aw9610x, REG_CMD, 0x01); // enter active mode
		enable_irq(gpio_to_irq(aw9610x->irq_gpio));
	}
	return 0;
}

static const struct dev_pm_ops aw9610x_pm_ops = {
	.suspend = aw9610x_suspend,
	.resume = aw9610x_resume,
};

static const struct of_device_id aw9610x_dt_match[] = {
	{ .compatible = "awinic,aw9610x_sar", },
	{ },
};

static const struct i2c_device_id aw9610x_i2c_id[] = {
	{ AW9610X_I2C_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, aw9610x_i2c_id);

static struct i2c_driver aw9610x_i2c_driver = {
	.driver = {
		.name = AW9610X_I2C_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(aw9610x_dt_match),
		.pm = &aw9610x_pm_ops,
	},
	.probe = aw9610x_i2c_probe,
	.remove = aw9610x_i2c_remove,
	.id_table = aw9610x_i2c_id,
};

static int sar_aw9610x_local_init(void)
{
	int32_t ret = 0;

	ret = i2c_add_driver(&aw9610x_i2c_driver);
	if (ret) {
		pr_err("fail to add aw9610x device infor i2c\n");
		return ret;
	}

	return 0;
}

#ifdef CONFIG_VENDOR_SOC_MTK_COMPILE
#ifdef SENSOR_ARCH_1_0
static int sar_aw9610x_remove(void)
{
	i2c_del_driver(&aw9610x_i2c_driver);
	return 0;
}

static struct situation_init_info sar_aw9610x_init_info = {
    .name = "sar_aw9610x",
    .init = sar_aw9610x_local_init,
    .uninit = sar_aw9610x_remove,
};
#endif
#endif

static int32_t __init sar_aw9610x_init(void)
{
	pr_info("aw9610x driver version %s\n", AW9610X_DRIVER_VERSION);
#ifdef CONFIG_VENDOR_SOC_MTK_COMPILE
#ifdef SENSOR_ARCH_1_0
	situation_driver_add(&sar_aw9610x_init_info, ID_SAR);
#endif
#ifdef SENSOR_ARCH_2_0
	sar_aw9610x_local_init();
#endif
#endif

#if (defined CONFIG_VENDOR_SOC_SPRD_COMPILE) || (defined CONFIG_VENDOR_SOC_QCOM_COMPILE)
	sar_aw9610x_local_init();
#endif
	return 0;
}

static void __exit sar_aw9610x_exit(void)
{
	return;
}

module_init(sar_aw9610x_init);
module_exit(sar_aw9610x_exit);
MODULE_DESCRIPTION("AW9610X SAR Driver");
MODULE_LICENSE("GPL v2");
