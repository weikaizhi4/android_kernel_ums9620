/*
* aw_bin_parse.c
*
* Copyright (c) 2020 AWINIC Technology CO., LTD
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
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/debugfs.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/regmap.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/list.h>
#include <linux/string.h>
#include "aw_bin_parse.h"

#define AWINIC_CODE_VERSION "V0.0.7-V1.0.4"	/* "code version"-"excel version" */

#define printing_data_code

typedef unsigned short int aw_uint16;
typedef unsigned long int aw_uint32;

#define BigLittleSwap16(A)	((((aw_uint16)(A) & 0xff00) >> 8) | \
				 (((aw_uint16)(A) & 0x00ff) << 8))

#define BigLittleSwap32(A)	((((aw_uint32)(A) & 0xff000000) >> 24) | \
				(((aw_uint32)(A) & 0x00ff0000) >> 8) | \
				(((aw_uint32)(A) & 0x0000ff00) << 8) | \
				(((aw_uint32)(A) & 0x000000ff) << 24))

#define CHECK_BIN_HEAD -1
#define CHECK_BIN_DATA -2
#define CHECK_BIN_SUM -3
#define CHECK_DATA_VERSION -4
#define CHECK_REG_NUM -5
#define CHECK_BBSP_REG -6
#define CHECK_SOC_APP -7
#define BIN_NULL_POINT -8

static int aw_parse_bin_header_1_0_0(struct aw_bin *bin);

/**
*
* Interface function
*
* return value:
*       value = 0 :success;
*       value = -1 :check bin header version
*       value = -2 :check bin data type
*       value = -3 :check sum or check bin data len error
*       value = -4 :check data version
*       value = -5 :check register num
*       value = -6 :check dsp reg num
*       value = -7 :check soc app num
*       value = -8 :bin is NULL point
*
**/

/********************************************************
*
* check sum data
*
********************************************************/
static int aw_check_sum(struct aw_bin *bin, int bin_num)
{
	unsigned int i = 0;
	unsigned int sum_data = 0;
	unsigned int check_sum = 0;
	char *p_check_sum = NULL;

	pr_info("[sar aw9610]:%s enter\n", __func__);

	p_check_sum =
	    &(bin->info.data[(bin->header_info[bin_num].valid_data_addr -
			      bin->header_info[bin_num].header_len)]);
	pr_info("[sar aw9610]:%s aw_bin_parse p_check_sum = %p\n", __func__, p_check_sum);
	check_sum = GET_32_DATA(*(p_check_sum + 3),
				*(p_check_sum + 2),
				*(p_check_sum + 1), *(p_check_sum));

	for (i = 4;
	     i <
	     bin->header_info[bin_num].bin_data_len +
	     bin->header_info[bin_num].header_len; i++) {
		sum_data += *(p_check_sum + i);
	}
	pr_info("[sar aw9610]:%s aw_bin_parse bin_num=%d, check_sum = 0x%x, sum_data = 0x%x\n", __func__,
		bin_num, check_sum, sum_data);
	if (sum_data != check_sum) {
		p_check_sum = NULL;
		pr_err("[sar aw9610]:%s aw_bin_parse check sum or check bin data len error\n", __func__);
		pr_err("[sar aw9610]:%s aw_bin_parse bin_num=%d, check_sum = 0x%x, sum_data = 0x%x\n",
				__func__, bin_num, check_sum, sum_data);
		return CHECK_BIN_SUM;
	}
	p_check_sum = NULL;

	return 0;
}

static int aw_check_data_version(struct aw_bin *bin, int bin_num)
{
	int i = 0;

	pr_info("[sar aw9610]:%s enter\n", __func__);

	for (i = DATA_VERSION_V1; i < DATA_VERSION_MAX; i++) {
		if (bin->header_info[bin_num].bin_data_ver == i) {
			return 0;
		}
	}
	pr_err("[sar aw9610]:%s aw_bin_parse Unrecognized this bin data version\n", __func__);
	return CHECK_DATA_VERSION;
}

static int aw_check_register_num_v1(struct aw_bin *bin, int bin_num)
{
	unsigned int check_register_num = 0;
	unsigned int parse_register_num = 0;
	char *p_check_sum = NULL;

	pr_info("[sar aw9610]:%s enter\n", __func__);

	p_check_sum =
	    &(bin->info.data[(bin->header_info[bin_num].valid_data_addr)]);
	pr_info("[sar aw9610]:%s aw_bin_parse p_check_sum = %p\n", __func__, p_check_sum);
	parse_register_num = GET_32_DATA(*(p_check_sum + 3),
					 *(p_check_sum + 2),
					 *(p_check_sum + 1), *(p_check_sum));
	check_register_num = (bin->header_info[bin_num].bin_data_len - 4) /
	    (bin->header_info[bin_num].reg_byte_len +
	     bin->header_info[bin_num].data_byte_len);
	pr_info
	    ("aw_bin_parse bin_num=%d, parse_register_num = 0x%x, check_register_num = 0x%x\n", __func__,
	     bin_num, parse_register_num, check_register_num);
	if (parse_register_num != check_register_num) {
		p_check_sum = NULL;
		pr_err("[sar aw9610]:%s aw_bin_parse register num is error\n", __func__);
		pr_err("[sar aw9610]:%s aw_bin_parse bin_num=%d, parse_register_num = 0x%x, check_register_num = 0x%x\n",
				__func__, bin_num, parse_register_num, check_register_num);
		return CHECK_REG_NUM;
	}
	bin->header_info[bin_num].reg_num = parse_register_num;
	bin->header_info[bin_num].valid_data_len =
	    bin->header_info[bin_num].bin_data_len - 4;
	p_check_sum = NULL;
	bin->header_info[bin_num].valid_data_addr =
	    bin->header_info[bin_num].valid_data_addr + 4;
	return 0;
}

static int aw_check_dsp_reg_num_v1(struct aw_bin *bin, int bin_num)
{
	unsigned int check_dsp_reg_num = 0;
	unsigned int parse_dsp_reg_num = 0;
	char *p_check_sum = NULL;

	pr_info("[sar aw9610]:%s enter\n", __func__);

	p_check_sum =
	    &(bin->info.data[(bin->header_info[bin_num].valid_data_addr)]);
	pr_info("[sar aw9610]:%s aw_bin_parse p_check_sum = %p\n", __func__, p_check_sum);
	parse_dsp_reg_num = GET_32_DATA(*(p_check_sum + 7),
					*(p_check_sum + 6),
					*(p_check_sum + 5), *(p_check_sum + 4));
	bin->header_info[bin_num].reg_data_byte_len =
	    GET_32_DATA(*(p_check_sum + 11), *(p_check_sum + 10),
			*(p_check_sum + 9), *(p_check_sum + 8));
	check_dsp_reg_num =
	    (bin->header_info[bin_num].bin_data_len -
	     12) / bin->header_info[bin_num].reg_data_byte_len;
	pr_info
	    ("aw_bin_parse bin_num=%d, parse_dsp_reg_num = 0x%x, check_dsp_reg_num = 0x%x\n", __func__,
	     bin_num, parse_dsp_reg_num, check_dsp_reg_num);
	if (parse_dsp_reg_num != check_dsp_reg_num) {
		p_check_sum = NULL;
		pr_err("[sar aw9610]:%s aw_bin_parse dsp reg num is error\n", __func__);
		pr_err("[sar aw9610]:%s aw_bin_parse bin_num=%d, parse_dsp_reg_num = 0x%x, check_dsp_reg_num = 0x%x\n",
				__func__, bin_num, parse_dsp_reg_num, check_dsp_reg_num);
		return CHECK_BBSP_REG;
	}
	bin->header_info[bin_num].download_addr =
	    GET_32_DATA(*(p_check_sum + 3), *(p_check_sum + 2),
			*(p_check_sum + 1), *(p_check_sum));
	bin->header_info[bin_num].reg_num = parse_dsp_reg_num;
	bin->header_info[bin_num].valid_data_len =
	    bin->header_info[bin_num].bin_data_len - 12;
	p_check_sum = NULL;
	bin->header_info[bin_num].valid_data_addr =
	    bin->header_info[bin_num].valid_data_addr + 12;
	return 0;
}

static int aw_check_soc_app_num_v1(struct aw_bin *bin, int bin_num)
{
	unsigned int check_soc_app_num = 0;
	unsigned int parse_soc_app_num = 0;
	char *p_check_sum = NULL;

	pr_info("[sar aw9610]:%s enter\n", __func__);

	p_check_sum =
	    &(bin->info.data[(bin->header_info[bin_num].valid_data_addr)]);
	pr_info("[sar aw9610]:%s aw_bin_parse p_check_sum = %p\n", __func__, p_check_sum);
	bin->header_info[bin_num].app_version = GET_32_DATA(*(p_check_sum + 3),
							    *(p_check_sum + 2),
							    *(p_check_sum + 1),
							    *(p_check_sum));
	parse_soc_app_num = GET_32_DATA(*(p_check_sum + 11),
					*(p_check_sum + 10),
					*(p_check_sum + 9), *(p_check_sum + 8));
	check_soc_app_num = bin->header_info[bin_num].bin_data_len - 12;
	pr_info
	    ("aw_bin_parse bin_num=%d, parse_soc_app_num = 0x%x, check_soc_app_num = 0x%x\n", __func__,
	     bin_num, parse_soc_app_num, check_soc_app_num);
	if (parse_soc_app_num != check_soc_app_num) {
		p_check_sum = NULL;
		pr_err("[sar aw9610]:%s aw_bin_parse soc app num is error\n");
		pr_err("[sar aw9610]:%s aw_bin_parse bin_num=%d, parse_soc_app_num = 0x%x, check_soc_app_num = 0x%x\n",
				__func__, bin_num, parse_soc_app_num, check_soc_app_num);
		return CHECK_SOC_APP;
	}
	bin->header_info[bin_num].reg_num = parse_soc_app_num;
	bin->header_info[bin_num].download_addr =
	    GET_32_DATA(*(p_check_sum + 7), *(p_check_sum + 6),
			*(p_check_sum + 5), *(p_check_sum + 4));
	bin->header_info[bin_num].valid_data_len =
	    bin->header_info[bin_num].bin_data_len - 12;
	p_check_sum = NULL;
	bin->header_info[bin_num].valid_data_addr =
	    bin->header_info[bin_num].valid_data_addr + 12;
	return 0;
}

/************************
***
***bin header 1_0_0
***
************************/
static void aw_get_single_bin_header_1_0_0(struct aw_bin *bin)
{
	int i;

	pr_info("[sar aw9610]:%s enter\n", __func__);
	bin->header_info[bin->all_bin_parse_num].header_len = 60;
	bin->header_info[bin->all_bin_parse_num].check_sum =
	    GET_32_DATA(*(bin->p_addr + 3), *(bin->p_addr + 2),
			*(bin->p_addr + 1), *(bin->p_addr));
	bin->header_info[bin->all_bin_parse_num].header_ver =
	    GET_32_DATA(*(bin->p_addr + 7), *(bin->p_addr + 6),
			*(bin->p_addr + 5), *(bin->p_addr + 4));
	bin->header_info[bin->all_bin_parse_num].bin_data_type =
	    GET_32_DATA(*(bin->p_addr + 11), *(bin->p_addr + 10),
			*(bin->p_addr + 9), *(bin->p_addr + 8));
	bin->header_info[bin->all_bin_parse_num].bin_data_ver =
	    GET_32_DATA(*(bin->p_addr + 15), *(bin->p_addr + 14),
			*(bin->p_addr + 13), *(bin->p_addr + 12));
	bin->header_info[bin->all_bin_parse_num].bin_data_len =
	    GET_32_DATA(*(bin->p_addr + 19), *(bin->p_addr + 18),
			*(bin->p_addr + 17), *(bin->p_addr + 16));
	bin->header_info[bin->all_bin_parse_num].ui_ver =
	    GET_32_DATA(*(bin->p_addr + 23), *(bin->p_addr + 22),
			*(bin->p_addr + 21), *(bin->p_addr + 20));
	bin->header_info[bin->all_bin_parse_num].reg_byte_len =
	    GET_32_DATA(*(bin->p_addr + 35), *(bin->p_addr + 34),
			*(bin->p_addr + 33), *(bin->p_addr + 32));
	bin->header_info[bin->all_bin_parse_num].data_byte_len =
	    GET_32_DATA(*(bin->p_addr + 39), *(bin->p_addr + 38),
			*(bin->p_addr + 37), *(bin->p_addr + 36));
	bin->header_info[bin->all_bin_parse_num].device_addr =
	    GET_32_DATA(*(bin->p_addr + 43), *(bin->p_addr + 42),
			*(bin->p_addr + 41), *(bin->p_addr + 40));
	for (i = 0; i < 8; i++) {
		bin->header_info[bin->all_bin_parse_num].chip_type[i] =
		    *(bin->p_addr + 24 + i);
	}
	bin->header_info[bin->all_bin_parse_num].reg_num = 0x00000000;
	bin->header_info[bin->all_bin_parse_num].reg_data_byte_len = 0x00000000;
	bin->header_info[bin->all_bin_parse_num].download_addr = 0x00000000;
	bin->header_info[bin->all_bin_parse_num].app_version = 0x00000000;
	bin->header_info[bin->all_bin_parse_num].valid_data_len = 0x00000000;
	bin->all_bin_parse_num += 1;
}

static int aw_parse_each_of_multi_bins_1_0_0(unsigned int bin_num, int bin_serial_num,
				      struct aw_bin *bin)
{
	int ret = 0;
	unsigned int bin_start_addr = 0;
	unsigned int valid_data_len = 0;

	pr_info("[sar aw9610]:%s aw_bin_parse enter multi bin branch\n", __func__);
	if (!bin_serial_num) {
		bin_start_addr = GET_32_DATA(*(bin->p_addr + 67),
					     *(bin->p_addr + 66),
					     *(bin->p_addr + 65),
					     *(bin->p_addr + 64));
		bin->p_addr += (60 + bin_start_addr);
		bin->header_info[bin->all_bin_parse_num].valid_data_addr =
		    bin->header_info[bin->all_bin_parse_num -
				     1].valid_data_addr + 4 + 8 * bin_num + 60;
	} else {
		valid_data_len =
		    bin->header_info[bin->all_bin_parse_num - 1].bin_data_len;
		bin->p_addr += (60 + valid_data_len);
		bin->header_info[bin->all_bin_parse_num].valid_data_addr =
		    bin->header_info[bin->all_bin_parse_num -
				     1].valid_data_addr +
		    bin->header_info[bin->all_bin_parse_num - 1].bin_data_len +
		    60;
	}

	ret = aw_parse_bin_header_1_0_0(bin);
	return ret;
}

/* Get the number of bins in multi bins, and set a for loop, loop processing each bin data */
static int aw_get_multi_bin_header_1_0_0(struct aw_bin *bin)
{
	int i = 0;
	int ret = 0;
	unsigned int bin_num = 0;

	pr_info("[sar aw9610]:%s aw_bin_parse enter multi bin branch\n", __func__);
	bin_num = GET_32_DATA(*(bin->p_addr + 63),
			      *(bin->p_addr + 62),
			      *(bin->p_addr + 61), *(bin->p_addr + 60));
	if (bin->multi_bin_parse_num == 1) {
		bin->header_info[bin->all_bin_parse_num].valid_data_addr = 60;
	}
	aw_get_single_bin_header_1_0_0(bin);

	for (i = 0; i < bin_num; i++) {
		pr_info("[sar aw9610]:%s aw_bin_parse enter multi bin for is %d\n", __func__, i);
		ret = aw_parse_each_of_multi_bins_1_0_0(bin_num, i, bin);
		if (ret < 0) {
			return ret;
		}
	}
	return 0;
}

/********************************************************
*
* If the bin framework header version is 1.0.0,
  determine the data type of bin, and then perform different processing
  according to the data type
  If it is a single bin data type, write the data directly into the structure array
  If it is a multi-bin data type, first obtain the number of bins,
  and then recursively call the bin frame header processing function
  according to the bin number to process the frame header information of each bin separately
*
********************************************************/
static int aw_parse_bin_header_1_0_0(struct aw_bin *bin)
{
	int ret = 0;
	unsigned int bin_data_type;

	pr_info("[sar aw9610]:%s enter %s\n", __func__);
	bin_data_type = GET_32_DATA(*(bin->p_addr + 11),
				    *(bin->p_addr + 10),
				    *(bin->p_addr + 9), *(bin->p_addr + 8));
	pr_info("[sar aw9610]:%s aw_bin_parse bin_data_type 0x%x\n", __func__, bin_data_type);
	switch (bin_data_type) {
	case DATA_TYPE_REGISTER:
	case DATA_TYPE_DSP_REG:
	case DATA_TYPE_SOC_APP:
		/* Divided into two processing methods,
		   one is single bin processing,
		   and the other is single bin processing in multi bin */
		pr_info("[sar aw9610]:%s aw_bin_parse enter single bin branch\n");
		bin->single_bin_parse_num += 1;
		pr_info("[sar aw9610]:%s %s bin->single_bin_parse_num is %d\n", __func__,
			bin->single_bin_parse_num);
		if (!bin->multi_bin_parse_num) {
			bin->header_info[bin->
					 all_bin_parse_num].valid_data_addr =
			    60;
		}
		aw_get_single_bin_header_1_0_0(bin);
		break;
	case DATA_TYPE_MULTI_BINS:
		/* Get the number of times to enter multi bins */
		pr_info("[sar aw9610]:%s aw_bin_parse enter multi bin branch\n", __func__);
		bin->multi_bin_parse_num += 1;
		pr_info("[sar aw9610]:%s %s bin->multi_bin_parse_num is %d\n", __func__,
			bin->multi_bin_parse_num);
		ret = aw_get_multi_bin_header_1_0_0(bin);
		if (ret < 0) {
			return ret;
		}
		break;
	default:
		pr_err("[sar aw9610]:%s aw_bin_parse Unrecognized this bin data type\n", __func__);
		return CHECK_BIN_DATA;
	}
	return 0;
}

/* get the bin's header version */
static int aw_check_bin_header_version(struct aw_bin *bin)
{
	int ret = 0;
	unsigned int header_version = 0;

	header_version = GET_32_DATA(*(bin->p_addr + 7),
				     *(bin->p_addr + 6),
				     *(bin->p_addr + 5), *(bin->p_addr + 4));

	pr_info("[sar aw9610]:%s aw_bin_parse header_version 0x%x\n", __func__, header_version);

	/* Write data to the corresponding structure array
	   according to different formats of the bin frame header version */
	switch (header_version) {
	case HEADER_VERSION_1_0_0:
		ret = aw_parse_bin_header_1_0_0(bin);
		return ret;
	default:
		pr_err("[sar aw9610]:%s aw_bin_parse Unrecognized this bin header version\n",
				__func__);
		return CHECK_BIN_HEAD;
	}
}

int aw9610x_parsing_bin_file(struct aw_bin *bin)
{
	int i = 0;
	int ret = 0;

	pr_info("[sar aw9610]:%s aw_bin_parse code version:%s\n", __func__, AWINIC_CODE_VERSION);
	if (!bin) {
		pr_err("[sar aw9610]:%s aw_bin_parse bin is NULL\n", __func__);
		return BIN_NULL_POINT;
	}
	bin->p_addr = bin->info.data;
	bin->all_bin_parse_num = 0;
	bin->multi_bin_parse_num = 0;
	bin->single_bin_parse_num = 0;

	/* filling bins header info */
	ret = aw_check_bin_header_version(bin);
	if (ret < 0) {
		pr_err("[sar aw9610]:%s aw_bin_parse check bin header version error\n", __func__);
		return ret;
	}
	bin->p_addr = NULL;

	/* check bin header info */
	for (i = 0; i < bin->all_bin_parse_num; i++) {
		/* check sum */
		ret = aw_check_sum(bin, i);
		if (ret < 0) {
			pr_err("[sar aw9610]:%s aw_bin_parse check sum data error\n", __func__);
			return ret;
		}
		/* check bin data version */
		ret = aw_check_data_version(bin, i);
		if (ret < 0) {
			pr_err("[sar aw9610]:%s aw_bin_parse check data version error\n", __func__);
			return ret;
		}
		/* check valid data */
		if (bin->header_info[i].bin_data_ver == DATA_VERSION_V1) {
			/* check register num */
			if (bin->header_info[i].bin_data_type ==
			    DATA_TYPE_REGISTER) {
				ret = aw_check_register_num_v1(bin, i);
				if (ret < 0) {
					pr_err
					    ("aw_bin_parse check register num error\n", __func__);
					return ret;
				}
				/* check dsp reg num */
			} else if (bin->header_info[i].bin_data_type ==
				   DATA_TYPE_DSP_REG) {
				ret = aw_check_dsp_reg_num_v1(bin, i);
				if (ret < 0) {
					pr_err
					    ("aw_bin_parse check dsp reg num error\n", __func__);
					return ret;
				}
				/* check soc app num */
			} else if (bin->header_info[i].bin_data_type ==
				   DATA_TYPE_SOC_APP) {
				ret = aw_check_soc_app_num_v1(bin, i);
				if (ret < 0) {
					pr_err
					    ("aw_bin_parse check soc app num error\n", __func__);
					return ret;
				}
			} else {
				bin->header_info[i].valid_data_len =
				    bin->header_info[i].bin_data_len;
			}
		}
	}
	pr_info("[sar aw9610]:%s aw_bin_parse parsing success\n", __func__);

	return 0;
}
