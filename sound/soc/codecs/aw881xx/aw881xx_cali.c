/*
 * aw881xx_cali.c cali_module
 *
 * Version: v0.2.0
 *
 * Copyright (c) 2019 AWINIC Technology CO., LTD
 *
 *  Author: Nick Li <liweilei@awinic.com.cn>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/math64.h>
#include "aw881xx.h"
#include "aw881xx_reg.h"
#include "aw881xx_cali.h"
#include "aw881xx_monitor.h"

static DEFINE_MUTEX(g_cali_lock);
static unsigned int g_cali_re_time = AW_CALI_RE_DEFAULT_TIMER;
static bool is_single_cali;
static struct miscdevice *g_misc_dev;
static unsigned int g_dev_select = 0;
static unsigned int g_msic_wr_flag = CALI_STR_NONE;
static const char *cali_str[CALI_STR_MAX] = {"none", "start_cali", "cali_re",
	"cali_f0", "store_re", "show_re", "show_r0", "show_cali_f0", "show_f0",
	"show_te", "dev_sel", "get_ver", "get_re_range"
};

/******************************************************
 *
 * aw881xx cali store
 *
 ******************************************************/

/*write cali to persist file example */
#ifdef AW_CALI_STORE_EXAMPLE

#define AWINIC_CALI_FILE  "/mnt/vendor/persist/factory/audio/aw_cali.bin"
#define AW_INT_DEC_DIGIT 10

static void aw_fs_read(struct file *file, char *buf, size_t count, loff_t *pos)
{
#ifdef AW_KERNEL_VER_OVER_5_4_0
	kernel_read(file, buf, count, pos);
#else
	vfs_read(file, buf, count, pos);
#endif
}

static void aw_fs_write(struct file *file, char *buf, size_t count, loff_t *pos)
{
#ifdef AW_KERNEL_VER_OVER_5_4_0
	kernel_write(file, buf, count, pos);
#else
	vfs_write(file, buf, count, pos);
#endif
}

static int aw881xx_write_cali_re_to_file(uint32_t cali_re, uint8_t channel)
{
	struct file *fp = NULL;
	char buf[50] = { 0 };
	loff_t pos = 0;
	mm_segment_t fs;

	pos = channel * AW_INT_DEC_DIGIT;

	fp = filp_open(AWINIC_CALI_FILE, O_RDWR | O_CREAT, 0644);
	if (IS_ERR(fp)) {
		pr_err("%s:channel:%d open %s failed!\n",
			__func__, channel, AWINIC_CALI_FILE);
		return -EINVAL;
	}

	snprintf(buf, sizeof(buf), "%10u", cali_re);

	fs = get_fs();
	set_fs(KERNEL_DS);

	aw_fs_write(fp, buf, strlen(buf), &pos);

	set_fs(fs);

	pr_info("%s: channel:%d  buf:%s cali_re:%d\n",
		__func__, channel, buf, cali_re);

	filp_close(fp, NULL);
	return 0;
}

static int aw881xx_get_cali_re_from_file(uint32_t *cali_re, uint8_t channel)
{
	struct file *fp = NULL;
	int f_size;
	char *buf = NULL;
	int32_t int_cali_re = 0;
	loff_t pos = 0;
	mm_segment_t fs;

	pos = channel * AW_INT_DEC_DIGIT;

	fp = filp_open(AWINIC_CALI_FILE, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		pr_err("%s:channel:%d open %s failed!\n",
			__func__, channel, AWINIC_CALI_FILE);
		return -EINVAL;
	}

	f_size = AW_INT_DEC_DIGIT;

	buf = kzalloc(f_size + 1, GFP_ATOMIC);
	if (!buf) {
		pr_err("%s: channel:%d malloc mem %d failed!\n",
			__func__, channel, f_size);
		filp_close(fp, NULL);
		return -EINVAL;
	}

	fs = get_fs();
	set_fs(KERNEL_DS);

	aw_fs_read(fp, buf, f_size, &pos);

	set_fs(fs);

	if (sscanf(buf, "%d", &int_cali_re) != 1)
		*cali_re = AW_ERRO_CALI_VALUE;
	*cali_re = int_cali_re;

	pr_info("%s: channel:%d buf:%s cali_re: %d\n",
		__func__, channel, buf, *cali_re);

	kfree(buf);
	buf = NULL;
	filp_close(fp, NULL);

	return 0;

}
#endif

bool aw881xx_cali_check_result(struct aw_cali_desc *cali_desc)
{
	if (cali_desc->cali_check_st &&
		(cali_desc->cali_result == CALI_RESULT_ERROR))
		return false;
	else
		return true;
}

static void aw881xx_run_mute_for_cali(struct aw881xx *aw881xx, int8_t cali_result)
{
	aw_dev_dbg(aw881xx->dev, "%s: enter\n", __func__);

	if (aw881xx->cali_desc.cali_check_st) {
		if (cali_result == CALI_RESULT_ERROR)
			aw881xx_run_mute(aw881xx, true);
		else if (cali_result == CALI_RESULT_NORMAL)
			aw881xx_run_mute(aw881xx, false);
	} else {
		aw_dev_dbg(aw881xx->dev, "%s: cali check disable\n", __func__);
	}
	aw_dev_dbg(aw881xx->dev, "%s: done\n", __func__);
}

static int aw881xx_get_cali_re_from_phone(struct aw881xx *aw881xx, uint32_t *cali_re)
{
	/* customer add, get re from nv or persist or cali file */
#ifdef AW_CALI_STORE_EXAMPLE

	return aw881xx_get_cali_re_from_file(cali_re,
					aw881xx->channel);
#else
	return 0;
#endif
}

int aw881xx_get_cali_re(struct aw_cali_desc *cali_desc)
{
	int ret = -EINVAL;
	uint32_t cali_value = 0;
	struct aw881xx *aw881xx =
		container_of(cali_desc, struct aw881xx, cali_desc);

	ret = aw881xx_get_cali_re_from_phone(aw881xx, &cali_value);
	if (ret < 0) {
		cali_desc->cali_re = AW_ERRO_CALI_VALUE;
		cali_desc->cali_result = CALI_RESULT_NONE;
		aw_dev_err(aw881xx->dev, "%s: get re failed, use default\n", __func__);
		return ret;
	}

	if ((cali_value < aw881xx->re_range.re_min) || (cali_value > aw881xx->re_range.re_max)) {
		aw_dev_err(aw881xx->dev,
				"%s:out range re value: %d\n", __func__, cali_value);
		cali_desc->cali_re = AW_ERRO_CALI_VALUE;

		/*cali_result is error when aw-cali-check enable*/
		if (aw881xx->cali_desc.cali_check_st) {
			cali_desc->cali_result = CALI_RESULT_ERROR;
		}
		return -EINVAL;
	}
	cali_desc->cali_re = cali_value;

	/*cali_result is normal when aw-cali-check enable*/
	if (aw881xx->cali_desc.cali_check_st) {
		cali_desc->cali_result = CALI_RESULT_NORMAL;
	}

	aw_dev_info(aw881xx->dev, "%s:get cali re %d\n", __func__, cali_desc->cali_re);

	return 0;

}

static int aw881xx_set_cali_re_to_phone(struct aw881xx *aw881xx)
{
	/* customer add, set re to nv or persist or cali file */
#ifdef AW_CALI_STORE_EXAMPLE
	struct aw_cali_desc *cali_desc = &aw881xx->cali_desc;

	return aw881xx_write_cali_re_to_file(cali_desc->cali_re,
					aw881xx->channel);
#else
	return 0;
#endif
}

void aw881xx_set_cali_re_to_dsp(struct aw_cali_desc *cali_desc)
{
	uint16_t dsp_re = 0;
	uint16_t set_value = 0;
	uint16_t dsp_ra = 0;
	struct aw881xx *aw881xx =
		container_of(cali_desc, struct aw881xx, cali_desc);

	aw881xx_dsp_read(aw881xx, AW881XX_DSP_REG_CFG_ADPZ_RA, &dsp_ra);

	dsp_re = AW_SHOW_RE_TO_DSP_RE(cali_desc->cali_re);

	set_value = dsp_re + dsp_ra;

	/* set cali re to aw881xx */
	aw881xx_dsp_write(aw881xx, AW881XX_DSP_REG_CFG_ADPZ_RE,
			set_value);

}

static int aw881xx_read_cali_re_from_dsp(struct aw_cali_desc *cali_desc, uint32_t *read_re)
{
	int ret = -EINVAL;
	uint16_t dsp_re = 0;
	uint16_t dsp_ra = 0;
	uint32_t show_re = 0;
	struct aw881xx *aw881xx =
			container_of(cali_desc, struct aw881xx, cali_desc);

	ret = aw881xx_dsp_read(aw881xx, AW881XX_DSP_REG_CFG_ADPZ_RA, &dsp_ra);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s:read ra fail\n", __func__);
		return ret;
	}

	ret = aw881xx_dsp_read(aw881xx, AW881XX_DSP_REG_CFG_ADPZ_RE, &dsp_re);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s:read re fail\n", __func__);
		return ret;
	}

	show_re = AW_DSP_RE_TO_SHOW_RE(dsp_re - dsp_ra);

	*read_re = show_re;

	return 0;
}

static int aw881xx_set_cali_re(struct aw_cali_desc *cali_desc)
{
	int ret = -EINVAL;
	struct aw881xx *aw881xx =
		container_of(cali_desc, struct aw881xx, cali_desc);

	ret = aw881xx_set_cali_re_to_phone(aw881xx);
	if (ret < 0)
		return ret;

	/* set cali re to aw881xx */
	aw881xx_set_cali_re_to_dsp(cali_desc);

	return 0;
}

static int aw881xx_get_ste_re_addr(struct aw881xx *aw881xx, uint16_t *dsp_addr)
{
	int ret = 0;

	switch (aw881xx->pid) {
	case AW881XX_PID_01:
		*dsp_addr = AW88194_DSP_REG_ST_STE_RE;
		break;
	case AW881XX_PID_03:
		*dsp_addr = AW88195_DSP_REG_ST_STE_RE;
		break;
	default:
		ret = -1;
		*dsp_addr = AW88195_DSP_REG_ST_STE_RE;
		break;
	}

	return ret;
}

static void aw881xx_bubble_sort(uint32_t *data, int data_size)
{
	int loop_num = data_size - 1;
	uint32_t temp_store = 0;
	int i;
	int j;

	if (data == NULL) {
		aw_pr_err("data is NULL");
		return;
	}

	for (i = 0; i < loop_num; i++) {
		for (j = 0; j < loop_num - i; j++) {
			if (data[j] > data[j + 1]) {
				temp_store = data[j];
				data[j] = data[j + 1];
				data[j + 1] = temp_store;
			}
		}
	}
}

int aw881xx_cali_get_iv(struct aw881xx *aw881xx, uint8_t *iv_flag)
{
	int ret = -EINVAL;
	uint16_t dsp_addr = 0;
	uint16_t dsp_val = 0;
	uint32_t iabs_val = 0;
	uint32_t iabs_temp[CALI_READ_CNT_MAX] = { 0 };
	uint32_t iabs_sum = 0;
	uint16_t iabs_average;
	uint8_t cnt = 0;

	if (aw881xx == NULL) {
		aw_pr_err("aw881xx is NULL");
		return -1;
	}

	if (aw881xx->pid != AW881XX_PID_03) {
		*iv_flag = 1;
		return 0;
	}

	for (cnt = 0; cnt < CALI_READ_CNT_MAX; cnt++) {
		dsp_addr = AW88195_DSP_REG_ST_STE_IABS;
		ret = aw881xx_dsp_read(aw881xx, dsp_addr, &dsp_val);
		if (ret < 0) {
			aw_dev_err(aw881xx->dev, "dsp read reg0x%04x error", dsp_addr);
			return ret;
		}
		iabs_val = dsp_val;
		aw_dev_dbg(aw881xx->dev, "%s: iabs[%d]=0x%x", __func__, cnt, iabs_val);
		iabs_temp[cnt] = iabs_val;

		msleep(30);  // delay 30 ms
	}

	// sort read re value
	aw881xx_bubble_sort(iabs_temp, CALI_READ_CNT_MAX);

	// delete two min value and two max value,compute mid value
	for (cnt = 1; cnt < CALI_READ_CNT_MAX - 1; cnt++) {
		aw_dev_dbg(aw881xx->dev, "%s: vialed iabs[%d]=0x%x", __func__, cnt, iabs_temp[cnt]);
		iabs_sum += iabs_temp[cnt];
		aw_dev_dbg(aw881xx->dev, "%s: iabs sum=0x%x", __func__, iabs_sum);
	}
	iabs_average = (uint16_t)(iabs_sum / (CALI_READ_CNT_MAX - CALI_DATA_SUM_RM));

	if (iabs_average < 32)
		*iv_flag = 0;
	else
		*iv_flag = 1;

	aw_dev_dbg(aw881xx->dev, "%s: iv_flag=%d", __func__, *iv_flag);

	return 0;
}

static int aw881xx_get_adpz_ra_addr(struct aw881xx *aw881xx, uint16_t *dsp_addr)
{
	int ret;

	switch (aw881xx->pid) {
	case AW881XX_PID_01:
		*dsp_addr = AW88194_DSP_REG_CFG_ADPZ_RA;
		ret = 0;
		break;
	case AW881XX_PID_03:
		*dsp_addr = AW88195_DSP_REG_CFG_ADPZ_RA;
		ret = 0;
		break;
	default:
		ret = -1;
		*dsp_addr = AW88195_DSP_REG_CFG_ADPZ_RA;
		break;
	}

	return ret;
}

static int aw881xx_get_fs(struct aw881xx *aw881xx)
{
	int ret = -EINVAL;
	uint8_t reg_addr = 0;
	uint16_t reg_val = 0;
	uint32_t aw881xx_fs[AW881XX_FS_CFG_MAX] = {
		8000,
		11000,
		12000,
		16000,
		22000,
		24000,
		32000,
		44100,
		48000,
		96000,
		192000,
	};

	reg_addr = AW881XX_REG_I2SCTRL;
	ret = aw881xx_reg_read(aw881xx, reg_addr, &reg_val);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: i2c read reg0x%02x error",
			__func__, reg_addr);
		return ret;
	}

	aw881xx->fs = aw881xx_fs[reg_val&(~AW881XX_BIT_I2SCTRL_SR_MASK)];

	aw_dev_dbg(aw881xx->dev, "%s: fs=%d", __func__, aw881xx->fs);

	return ret;
}

static int64_t aw_func_1(uint64_t x, uint16_t precision)
{
	uint16_t i = 0;
	int64_t iteration_factor = 1ULL << (precision - 1);
	int64_t res = 0;
	uint64_t normalized_range_low = 1ULL << precision;
	uint64_t normalized_range_high = 2ULL << precision;

	if (x == 0) {
		return LLONG_MIN;
	}

	while (x < normalized_range_low) {
		x <<= 2;
		res -= normalized_range_high;
	}
	while (x >= normalized_range_high) {
		x >>= 1;
		res += normalized_range_low;
	}

	for (i = 0; i < LOG2_ITERATION_TIMES; i++) {
		x = x * x >> precision;
		if (x >= normalized_range_high) {
			x >>= 1;
			res += iteration_factor;
		}
		iteration_factor >>= 1;
	}

	return res;
}

static int64_t aw_func_2(uint64_t x, uint16_t precision)
{
	int64_t res;

	res = (aw_func_1(x, precision) * INV_LOG2_E_Q1DOT31) >> LOG_PRECISION;
	if ( x < 1U << precision) {
		res = (int32_t)res;
	}

	return res;
}

static uint64_t aw_func_3(uint64_t x)
{
	uint16_t iterations = SQRT_ITERATION_BASE + (SQRT_PRECISION >> 1);
	uint64_t res = 0;
	uint64_t remainder_high = 0;
	uint64_t divisor = 0;
	uint64_t remainder_low = x;

	do {
		remainder_high = (remainder_high << 2) | (remainder_low >> SQRT_REMLOW_DECAY);
		remainder_low <<= 2 ;
		res <<= 1;
		divisor = (res << 1) + 1;
		if (remainder_high >= divisor) {
			remainder_high -= divisor;
			res += 1;
		}
	} while (iterations-- != 0);

	return res;
}

static int64_t aw_func_4(int64_t x, int64_t *complex_real, int64_t *complex_imag)
{
	unsigned short x_corros0 = 0;
	int64_t res = 0;
	int64_t sqrt_res = 0;
	int64_t log_res = 0;
	int64_t x_fraction = ABS_VALUE(x);

	res = ((x * x) >> ACOS_PRECISION) - ACOS_ONE_24BIT;
	res = x - aw_func_3(res);
	if (x > ACOS_ONE_24BIT) {
		res = -aw_func_2(res, ACOS_PRECISION);
	} else if (x < -ACOS_ONE_24BIT) {
		log_res = aw_func_2(ABS_VALUE(res), ACOS_PRECISION);
		res = ((log_res * log_res) >> ACOS_PRECISION) + ACOS_PI2_24BIT;
		res = aw_func_3(res);
		*complex_real = ACOS_PI_24BIT;
		*complex_imag = log_res;
	} else {
		if (x < 0) {
		    x_corros0 = 1;
		}
		res = -((x_fraction * ACOS_COE1_24BIT) >> ACOS_PRECISION) + ACOS_COE2_24BIT;
		res = ((res * x_fraction) >> ACOS_PRECISION) - ACOS_COE3_24BIT;
		res = ((x_fraction * res) >> ACOS_PRECISION) + ACOS_COE4_24BIT;
		sqrt_res = aw_func_3((ACOS_ONE_24BIT - x_fraction));
		res = (1 - (x_corros0 << 1)) * ((res * sqrt_res) >> ACOS_PRECISION) + x_corros0 * ACOS_PI_24BIT;
	}

	return res;
}


static void aw_func_5(int64_t *res_arccpx, int64_t acos_in, int64_t res_log)
{
	int64_t complex_real = 0;
	int64_t complex_imag = 0;
	int64_t res_arc = 0;
	int64_t cpx_square_real = 0;
	int64_t cpx_square_imag = 0;
	int64_t res = 0;

	res_arc = aw_func_4(acos_in, &complex_real, &complex_imag);
	if (acos_in > ADPZ_FQ_1_24BIT) {
		res = (res_log * res_log - res_arc * res_arc) >> ADPZ_FQ_PRECISION;
	} else if (acos_in < -ADPZ_FQ_1_24BIT) {
		cpx_square_real = (int64_t)(complex_real + complex_imag) * (complex_real - complex_imag);
		cpx_square_imag = ((int64_t)complex_real * complex_imag) >> (ADPZ_FQ_PRECISION - 1);
		res = (cpx_square_real + res_log * res_log) >> ADPZ_FQ_PRECISION;
		res = (res * res + cpx_square_imag * cpx_square_imag) >> ADPZ_FQ_PRECISION;
		res = aw_func_3(res);
	} else {
		res = (res_log * res_log + res_arc * res_arc) >> ADPZ_FQ_PRECISION;
	}
	*res_arccpx = res;
}

static int aw_get_f0_q(adapZdriver_t *cfg, int32_t fs, int32_t *f0, int32_t *q)
{
	int func_status = 0;
	int64_t a1 = 0;
	int64_t a2 = 0;
	int64_t res = 0;
	int64_t res_arccpx = 0;
	int64_t res_log = 0;

	a1 = (int64_t)cfg->real_cal_A1 << ADPZ_FQ_1INV2PRECISION;
	a2 = (int64_t)cfg->real_cal_A2 << ADPZ_FQ_1INV2PRECISION;
	if (a2 <= 0) {
		aw_pr_err("a1 or a2 is illicit");
		return -1;
	}

	a2 = aw_func_3(a2);
	if (!a2) {
		aw_pr_err("a2 is illicit or too small to calculate! ");
		return -1;
	}
	res = div_s64((-(a1 << (ADPZ_FQ_PRECISION - 1))), a2);
	res_log = aw_func_2(a2, ADPZ_FQ_PRECISION);

	aw_func_5(&res_arccpx, res, res_log);
	res = aw_func_3(ABS_VALUE(res_arccpx));

	*f0 = (((res * ADPZ_INV_2PI_24BIT) >> ADPZ_FQ_PRECISION) * fs) >> ADPZ_F0_PRECISION;

	if (res_log == 0) {
		*q = 0;
	} else {
		*q = div_s64((-(res * ADPZ_Q_EXPAND_RATIO)), res_log);
	}

	return func_status;
}

static int aw881xx_get_r0(struct aw881xx *aw881xx, uint32_t *re)
{
	int ret = -EINVAL;
	uint32_t cali_re = 0;
	uint16_t spkr_te = 0;
	uint16_t factor = 0;
	uint16_t dsp_t0 = 0;
	int32_t te = 0;
	int32_t te_cacl = 0;
	uint32_t show_re = 0;
	uint16_t dsp_re = 0;
	uint16_t dsp_ra = 0;


	ret = aw881xx_dsp_read(aw881xx, AW881XX_DSP_REG_CFG_ADPZ_RA, &dsp_ra);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s:read ra fail\n", __func__);
		return ret;
	}

	ret = aw881xx_dsp_read(aw881xx, AW881XX_DSP_REG_CFG_ADPZ_RE, &dsp_re);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s:read re fail\n", __func__);
		return ret;
	}

	cali_re = dsp_re - dsp_ra;

	ret = aw881xx_reg_read(aw881xx, AW881XX_REG_ASR2, &spkr_te);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: read speaker te failed\n", __func__);
		return ret;
	}

	ret = aw881xx_dsp_read(aw881xx, AW881XX_DSP_REG_TEMP_FACTOR, &factor);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: read factor failed\n", __func__);
		return ret;
	}

	ret = aw881xx_dsp_read(aw881xx, AW881XX_DSP_REG_ADPZ_T0, &dsp_t0);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: read t0 failed\n", __func__);
		return ret;
	}

	te = (int32_t)(spkr_te - dsp_t0);
	te_cacl = AW_TE_CACL_VALUE(te, factor);
	show_re = AW_RE_REALTIME_VALUE((int32_t)cali_re, te_cacl);

	*re = AW_DSP_RE_TO_SHOW_RE(show_re);

	return 0;
}

static int aw_cali_svc_set_cali_cfg(struct aw881xx *aw881xx, struct cali_cfg cali_cfg)
{
	int ret = -EINVAL;
	uint16_t reg_addr = 0;

	reg_addr = AW881XX_DSP_REG_CFG_MBMEC_ACTAMPTH;
	ret = aw881xx_dsp_write(aw881xx, reg_addr, cali_cfg.data[0]);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s set cali cfg failed\n", __func__);
		return ret;
	}

	reg_addr = AW881XX_DSP_REG_CFG_MBMEC_NOISEAMPTH;
	ret = aw881xx_dsp_write(aw881xx, reg_addr, cali_cfg.data[1]);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s set cali cfg failed\n", __func__);
		return ret;
	}

	reg_addr = AW881XX_DSP_REG_CFG_ADPZ_USTEPN;
	ret = aw881xx_dsp_write(aw881xx, reg_addr, cali_cfg.data[2]);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s set cali cfg failed\n", __func__);
		return ret;
	}

	return 0;
}

static int aw_cali_svc_get_cali_cfg(struct aw881xx *aw881xx)
{
	int ret = -EINVAL;
	uint16_t reg_addr = 0;
	uint16_t reg_val = 0;
	struct cali_cfg *cali_cfg = &aw881xx->cali_desc.cali_cfg;

	reg_addr = AW881XX_DSP_REG_CFG_MBMEC_ACTAMPTH;
	ret = aw881xx_dsp_read(aw881xx, reg_addr, &cali_cfg->data[0]);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s get cali cfg failed\n", __func__);
		return ret;
	}

	reg_addr = AW881XX_DSP_REG_CFG_MBMEC_NOISEAMPTH;
	ret = aw881xx_dsp_read(aw881xx, reg_addr, &cali_cfg->data[1]);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s get cali cfg failed\n", __func__);
		return ret;
	}

	reg_addr = AW881XX_DSP_REG_CFG_ADPZ_USTEPN;
	ret = aw881xx_dsp_read(aw881xx, reg_addr, &cali_cfg->data[2]);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s get cali cfg failed\n", __func__);
		return ret;
	}

	ret = aw881xx_dsp_read(aw881xx, AW881XX_CALI_DELAY, &reg_val);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "dsp read reg0x%02x error", AW881XX_CALI_DELAY);
		return ret;
	}
	aw881xx->cali_delay = AW_F0_CALI_DELAY_CALC(reg_val);



	return 0;
}

static int aw_cali_svc_cali_f0_init_cfg(struct aw881xx *aw881xx, bool cali_en)
{
	int ret = -EINVAL;
	struct cali_cfg set_cfg;

	aw_dev_info(aw881xx->dev, "cali_en:%d", cali_en);

	if (cali_en) {
		ret = aw_cali_svc_get_cali_cfg(aw881xx);
		if (ret < 0) {
			aw_dev_err(aw881xx->dev, "get cali cfg failed");
			return ret;
		}
		set_cfg.data[0] = 0;
		set_cfg.data[1] = 0;
		set_cfg.data[2] = (uint16_t)(-1);
		set_cfg.data[3] = 0;

		ret = aw_cali_svc_set_cali_cfg(aw881xx, set_cfg);
		if (ret < 0) {
			aw_dev_err(aw881xx->dev, "set cali cfg failed");
			aw_cali_svc_set_cali_cfg(aw881xx, aw881xx->cali_desc.cali_cfg);
			return ret;
		}
	} else {
		aw_cali_svc_set_cali_cfg(aw881xx, aw881xx->cali_desc.cali_cfg);
	}

	return 0;
}

static void aw_cali_svc_set_cali_status(struct aw881xx *aw881xx, bool status)
{
	aw881xx->cali_desc.status = status;

	if (status)
		aw881xx_monitor_stop(&aw881xx->monitor);
	else
		aw881xx_monitor_start(&aw881xx->monitor);

	aw_dev_info(aw881xx->dev, "cali %s",
		(status == 0) ? ("disable") : ("enable"));
}

static int aw_dev_get_hmute(struct aw881xx *aw881xx)
{
	int ret = -EINVAL;
	uint16_t reg_val = 0;

	ret = aw881xx_reg_read(aw881xx, AW881XX_REG_PWMCTRL, &reg_val);
	if (ret < 0) {
		aw_dev_dbg(aw881xx->dev, "%s: i2c read reg0x%02x error",
			__func__, AW881XX_REG_PWMCTRL);
		return ret;
	}
	if (reg_val & AW881XX_BIT_PWMCTRL_HMUTE_ENABLE) {
		aw_dev_dbg(aw881xx->dev, "%s: hmute check failed, reg_val=%04x",
			__func__, reg_val);
		ret = -EINVAL;
	} else {
		aw_dev_dbg(aw881xx->dev, "%s: hmute check pass",
			__func__);
	}

	return ret;
}

static int aw_dev_sysst_check(struct aw881xx *aw881xx)
{
	int ret = -EINVAL;
	uint16_t reg_val = 0;
	uint16_t temp = 0;

	ret = aw881xx_reg_read(aw881xx, AW881XX_REG_SYSST, &reg_val);
	if (ret < 0) {
		aw_dev_dbg(aw881xx->dev, "%s: i2c read reg0x%02x error",
			__func__, AW881XX_REG_SYSST);
		return -EINVAL;
	}

	temp = AW881XX_BIT_SYSST_PLLS |
			AW881XX_BIT_SYSST_CLKS |
			AW881XX_BIT_SYSST_SWS |
			AW881XX_BIT_SYSST_BSTS;
	if ((reg_val & temp) == temp ) {
		aw_dev_dbg(aw881xx->dev, "%s: i2s pll check pass\n", __func__);
	} else {
		aw_dev_err(aw881xx->dev, "%s: i2s pll check failed, reg_val=%04x",
			__func__, reg_val);
		return -EINVAL;
	}

	return ret;
}

static int aw_cali_svc_cali_init_check(struct aw881xx *aw881xx)
{
	int ret = -EINVAL;

	aw_dev_dbg(aw881xx->dev, "%s: enter\n", __func__);

	ret = aw_dev_sysst_check(aw881xx);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: syst_check failed\n", __func__);
		return ret;
	}

	ret = aw881xx_get_dsp_status(aw881xx);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: dsp status error\n", __func__);
		return ret;
	}

	ret = aw_dev_get_hmute(aw881xx);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: mute staus\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int aw_cali_svc_get_cali_f0_q(struct aw881xx *aw881xx)
{
	int ret = -EINVAL;
	uint16_t reg_addr;
	uint16_t reg_val = 0;
	uint16_t temp_val;
	int16_t f0_a1_val;
	int16_t f0_a2_val;
	int32_t f0_temp;
	int32_t f0_sum = 0;
	int32_t q_temp;
	int32_t q_sum = 0;
	uint8_t cnt;
	uint8_t iv_flag = 0;
	adapZdriver_t cfg;

	ret = aw881xx_get_fs(aw881xx);
	if (ret != 0)
		return ret;

	for (cnt = 0; cnt < F0_READ_CNT_MAX; cnt++) {
		/* get f0 a1 */
		temp_val = 0;
		reg_addr = AW881XX_REG_ASR4;
		ret = aw881xx_reg_read(aw881xx, reg_addr, &reg_val);
		if (ret < 0)
			return ret;
		temp_val = reg_val;
		f0_a1_val = (int16_t)temp_val;

		/* get f0 a2 */
		temp_val = 0;
		reg_addr = AW881XX_REG_ASR3;
		ret = aw881xx_reg_read(aw881xx, reg_addr, &reg_val);
		if (ret < 0)
			return ret;
		temp_val = reg_val;
		f0_a2_val = (int16_t)temp_val;

		/* f0 q compute */
		cfg.real_cal_A1 = (int32_t)f0_a1_val;
		cfg.real_cal_A2 = (int32_t)f0_a2_val;
		ret = aw_get_f0_q(&cfg, aw881xx->fs, &f0_temp, &q_temp);
		if (ret < 0) {
			aw_dev_err(aw881xx->dev, "%s: cnt[%d], get f0 q failed\n",
				__func__ ,cnt);
			return -EINVAL;
		}
		f0_sum += f0_temp;
		q_sum += q_temp;
		msleep(20);
	}

	aw881xx->cali_desc.f0 = f0_sum / cnt;
	aw881xx->cali_desc.q = q_sum / cnt;

	aw881xx_cali_get_iv(aw881xx, &iv_flag);

	if (!iv_flag) {
		aw881xx->cali_desc.f0 = 0;
		aw881xx->cali_desc.q = 0;
		aw_dev_err(aw881xx->dev, "%s: iv data abnormal", __func__);
		return -EINVAL;
	}

	aw_dev_info(aw881xx->dev, "f0[%d] q[%d]", aw881xx->cali_desc.f0, aw881xx->cali_desc.q);

	return 0;
}

int aw881xx_cali_vol_cfg(struct aw881xx *aw881xx, bool flag)
{
	int ret = -EINVAL;
	uint16_t reg_addr = 0;
	uint16_t reg_val = 0;

	reg_addr = AW881XX_REG_DSPCFG;
	ret = aw881xx_reg_read(aw881xx, reg_addr, &reg_val);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: read reg0x%02x error\n",
			__func__, reg_addr);
		return ret;
	}
	if (flag) {
		aw881xx->cali_desc.dft_dsp_cfg = reg_val;
		reg_val &= 0x00ff;
		reg_val |= 0x1800;
	} else {
		reg_val = aw881xx->cali_desc.dft_dsp_cfg;
	}

	ret = aw881xx_reg_write(aw881xx, reg_addr, reg_val);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: write reg0x%02x error\n",
			__func__, reg_addr);
		return ret;
	}
	return 0;
}

int aw881xx_cali_white_noise(struct aw881xx *aw881xx, bool flag)
{
	int ret = -EINVAL;
	uint16_t reg_addr = 0;
	uint16_t reg_val = 0;

	/* cfg mbmec glbcfg.bit4*/
	reg_addr = AW881XX_DSP_REG_CFG_MBMEC_GLBCFG;
	ret = aw881xx_dsp_read(aw881xx, reg_addr, &reg_val);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: dsp read reg0x%02x error",
			__func__, reg_addr);
		return ret;
	}
	if (true == flag) {
		reg_val |= (1<<4);
	} else {
		reg_val &= (~(1<<4));
	}
	ret = aw881xx_dsp_write(aw881xx, reg_addr, reg_val);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: write reg0x%02x error\n",
			__func__, reg_addr);
	}

	return 0;
}

static int aw_cali_svc_cali_f0_en(struct aw881xx *aw881xx, bool flag)
{
	int ret = -EINVAL;

	if (flag) {
		ret = aw881xx_cali_vol_cfg(aw881xx, true);
		if (ret < 0) {
			aw_dev_err(aw881xx->dev, "dsp cali vol cfg error, ret=%d", ret);
			return ret;
		}

		msleep(aw881xx->cali_delay);

		ret = aw881xx_cali_white_noise(aw881xx, true);
		if (ret < 0) {
			aw_dev_err(aw881xx->dev, "dsp set noise error, ret=%d", ret);
			return ret;
		}
	} else {
		ret = aw881xx_cali_white_noise(aw881xx, false);
		if (ret < 0) {
			aw_dev_err(aw881xx->dev, "dsp restore noise error, ret=%d", ret);
			return ret;
		}

		ret = aw881xx_cali_vol_cfg(aw881xx, false);
		if (ret < 0) {
			aw_dev_err(aw881xx->dev, "%s: dsp restore vol cfg error, ret=%d",
				__func__, ret);
			return ret;
		}
	}

	return 0;
}

static int aw_cali_svc_cali_run_dsp_vol(struct aw881xx *aw881xx, bool flag)
{
	int ret = -EINVAL;
	uint16_t reg_val, temp = 0;

	if (flag) {
		ret = aw881xx_reg_read(aw881xx, AW881XX_REG_DSPCFG, &reg_val);
		if (ret < 0) {
			aw_dev_err(aw881xx->dev, "%s: dsp read reg0x%02x error",
				__func__, AW881XX_REG_DSPCFG);
			return ret;
		}
		temp = reg_val & 0xff00;
		aw881xx->cali_desc.dsp_vol = temp;
		reg_val = ((reg_val & 0x00ff) | 0xff00);
		ret = aw881xx_reg_write(aw881xx, AW881XX_REG_DSPCFG, reg_val);
		if (ret < 0) {
			aw_dev_err(aw881xx->dev, "%s: dsp read reg0x%02x error",
				__func__, AW881XX_REG_DSPCFG);
			return ret;
		}
	} else {
		ret = aw881xx_reg_read(aw881xx, AW881XX_REG_DSPCFG, &reg_val);
		if (ret < 0) {
			aw_dev_err(aw881xx->dev, "%s: dsp read reg0x%02x error",
				__func__, AW881XX_REG_DSPCFG);
			return ret;
		}
		temp = reg_val & 0x00ff;
		reg_val = aw881xx->cali_desc.dsp_vol | temp;
		ret = aw881xx_reg_write(aw881xx, AW881XX_REG_DSPCFG, reg_val);
		if (ret < 0) {
			aw_dev_err(aw881xx->dev, "%s: dsp read reg0x%02x error",
				__func__, AW881XX_REG_DSPCFG);
			return ret;
		}
	}

	return 0;
}

static int aw_cali_svc_get_re_init_cfg(struct aw881xx *aw881xx)
{
	int ret = -EINVAL;
	uint16_t reg_addr = 0;
	struct cali_cfg *cali_cfg = &aw881xx->cali_desc.cali_cfg;

	reg_addr = AW881XX_DSP_REG_CFG_MBMEC_ACTAMPTH;
	ret = aw881xx_dsp_read(aw881xx, reg_addr, &cali_cfg->data[0]);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: dsp read reg0x%02x error",
			__func__, reg_addr);
		return ret;
	}

	reg_addr = AW881XX_DSP_REG_CFG_MBMEC_NOISEAMPTH;
	ret = aw881xx_dsp_read(aw881xx, reg_addr, &cali_cfg->data[1]);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: dsp read reg0x%02x error",
			__func__, reg_addr);
		return ret;
	}

	reg_addr = AW881XX_DSP_REG_CFG_ADPZ_USTEPN;
	ret = aw881xx_dsp_read(aw881xx, reg_addr, &cali_cfg->data[2]);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: dsp read reg0x%02x error",
			__func__, reg_addr);
		return ret;
	}

	reg_addr = AW881XX_DSP_REG_CFG_RE_ALPHAN;
	ret = aw881xx_dsp_read(aw881xx, reg_addr, &cali_cfg->data[3]);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: dsp read reg0x%02x error",
			__func__, reg_addr);
		return ret;
	}

	return ret;
}

static int aw_cali_svc_set_re_init_cfg(struct aw881xx *aw881xx, struct cali_cfg cali_cfg)
{
	int ret = -EINVAL;
	uint16_t reg_addr = 0;

	reg_addr = AW881XX_DSP_REG_CFG_MBMEC_ACTAMPTH;
	ret = aw881xx_dsp_write(aw881xx, reg_addr, cali_cfg.data[0]);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: dsp read reg0x%02x error",
			__func__, reg_addr);
		return ret;
	}

	reg_addr = AW881XX_DSP_REG_CFG_MBMEC_NOISEAMPTH;
	ret = aw881xx_dsp_write(aw881xx, reg_addr, cali_cfg.data[1]);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: dsp read reg0x%02x error",
			__func__, reg_addr);
		return ret;
	}

	reg_addr = AW881XX_DSP_REG_CFG_ADPZ_USTEPN;
	ret = aw881xx_dsp_write(aw881xx, reg_addr, cali_cfg.data[2]);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: dsp read reg0x%02x error",
			__func__, reg_addr);
		return ret;
	}

	reg_addr = AW881XX_DSP_REG_CFG_RE_ALPHAN;
	ret = aw881xx_dsp_write(aw881xx, reg_addr, cali_cfg.data[3]);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: dsp read reg0x%02x error",
			__func__, reg_addr);
		return ret;
	}

	return ret;
}

static int aw_cali_svc_cali_re_init_cfg(struct aw881xx *aw881xx, bool cali_en)
{
	int ret = -EINVAL;
	struct cali_cfg set_cfg;

	aw_dev_info(aw881xx->dev, "cali_en:%d", cali_en);

	if (cali_en) {
		ret = aw_cali_svc_get_re_init_cfg(aw881xx);
		if (ret < 0) {
			aw_dev_err(aw881xx->dev, "get cali cfg failed");
			return ret;
		}
		set_cfg.data[0] = 0;
		set_cfg.data[1] = 0;
		set_cfg.data[2] = (uint16_t)(-1);
		set_cfg.data[3] = (uint16_t)(1);

		ret = aw_cali_svc_set_re_init_cfg(aw881xx, set_cfg);
		if (ret < 0) {
			aw_dev_err(aw881xx->dev, "set cali cfg failed");
			aw_cali_svc_set_re_init_cfg(aw881xx, aw881xx->cali_desc.cali_cfg);
			return ret;
		}
	} else {
		aw_cali_svc_set_re_init_cfg(aw881xx, aw881xx->cali_desc.cali_cfg);
	}

	return 0;
}

static int aw_cali_svc_cali_mode_enable(struct aw881xx *aw881xx,
					int type, unsigned int flag, bool is_enable)
{
	int ret = -EINVAL;

	aw_dev_info(aw881xx->dev, "%s: type:%d, flag:0x%x, is_enable:%d",
			__func__, type, flag, is_enable);

	if (is_enable) {
		ret = aw_cali_svc_cali_init_check(aw881xx);
		if (ret < 0) {
			aw_dev_err(aw881xx->dev, "%s: init check failed\n", __func__);
			return ret;
		}

		ret = aw881xx_read_dsp_pid(aw881xx);
		if (ret < 0) {
			aw_dev_err(aw881xx->dev, "%s: read pid error\n", __func__);
			return ret;
		}

		aw_cali_svc_set_cali_status(aw881xx, true);

		if ((type == CALI_TYPE_RE) && (flag & CALI_OPS_HMUTE)) {
			ret = aw_cali_svc_cali_run_dsp_vol(aw881xx, true);
			if (ret < 0) {
				aw_cali_svc_set_cali_status(aw881xx, false);
				return ret;
			}

			ret = aw_cali_svc_cali_re_init_cfg(aw881xx, true);
			if (ret < 0) {
				aw_cali_svc_cali_run_dsp_vol(aw881xx, false);
				aw_cali_svc_set_cali_status(aw881xx, false);
				return ret;
			}
		} else if ((type == CALI_TYPE_F0) && (flag & CALI_OPS_NOISE)) {
			ret = aw_cali_svc_cali_f0_init_cfg(aw881xx, true);
			if (ret < 0) {
				aw_cali_svc_set_cali_status(aw881xx, false);
				return ret;
			}

			ret = aw_cali_svc_cali_f0_en(aw881xx, true);
			if (ret < 0) {
				aw_cali_svc_cali_f0_init_cfg(aw881xx, false);
				aw_cali_svc_set_cali_status(aw881xx, false);
				return ret;
			}
		}
	} else {

		if ((type == CALI_TYPE_RE) && (flag & CALI_OPS_HMUTE)) {
			aw_cali_svc_cali_re_init_cfg(aw881xx, false);
			aw_cali_svc_cali_run_dsp_vol(aw881xx, false);
		} else if ((type == CALI_TYPE_F0) && (flag & CALI_OPS_NOISE)) {
			aw_cali_svc_cali_f0_en(aw881xx, false);
			aw_cali_svc_cali_f0_init_cfg(aw881xx, false);
		}
		aw881xx_clear_int_status(aw881xx);
		aw_cali_svc_set_cali_status(aw881xx, false);
	}

	return 0;
}

static int aw_cali_svc_devs_cali_mode_enable(struct list_head *dev_list,
						int type, unsigned int flag,
						bool is_enable)
{
	int ret = -EINVAL;
	struct list_head *pos = NULL;
	struct aw881xx *local_dev = NULL;

	list_for_each(pos, dev_list) {
		local_dev = container_of(pos, struct aw881xx, list_node);
		if (is_enable)
			aw881xx_run_mute_for_cali(local_dev, CALI_RESULT_NORMAL);
		ret = aw_cali_svc_cali_mode_enable(local_dev, type, flag, is_enable);
		if (ret < 0)
			return ret;
		if (!is_enable && (type == CALI_TYPE_F0))
			aw881xx_run_mute_for_cali(local_dev, local_dev->cali_desc.cali_result);
	}

	return ret;
}

static int aw_cali_store_cali_re(struct aw881xx *aw881xx, int32_t re)
{
	int ret = -EINVAL;

	if ((re > aw881xx->re_range.re_min) && (re < aw881xx->re_range.re_max)) {
		aw881xx->cali_desc.cali_re = re;
		ret = aw881xx_set_cali_re_to_phone(aw881xx);
		if (ret < 0) {
			aw_dev_err(aw881xx->dev, "write re to nvram failed!");
			return ret;
		}
	} else {
		aw_dev_err(aw881xx->dev, "invalid cali re %d!", re);
		return -EINVAL;
	}

	return 0;
}

static int aw_cali_svc_get_devs_r0(struct aw881xx *aw881xx, int32_t *r0_buf, int num)
{
	int ret = -EINVAL;
	int cnt = 0;
	struct list_head *dev_list = NULL;
	struct list_head *pos = NULL;
	struct aw881xx *local_dev = NULL;

	//get dev list
	ret = aw881xx_get_list_head(&dev_list);
	if (ret) {
		aw_dev_err(aw881xx->dev, "get dev list failed");
		return ret;
	}

	list_for_each (pos, dev_list) {
		local_dev = container_of(pos, struct aw881xx, list_node);
		if (local_dev->channel < num) {
			ret = aw881xx_get_r0(local_dev, &r0_buf[local_dev->channel]);
			if (ret) {
				aw_dev_err(local_dev->dev, "get r0 failed!");
				return ret;
			}
			cnt++;
		} else {
			aw_dev_err(aw881xx->dev, "channel num[%d] overflow buf num[%d] ",
						 local_dev->channel, num);
		}
	}
	return cnt;
}


static int aw_cali_svc_get_dev_cali_val(struct aw881xx *aw881xx, int type, uint32_t *data_buf)
{
	switch (type) {
	case GET_RE_TYPE:
		*data_buf = aw881xx->cali_desc.cali_re;
		break;
	case GET_F0_TYPE:
		*data_buf = aw881xx->cali_desc.f0;
		break;
	case GET_Q_TYPE:
		*data_buf = aw881xx->cali_desc.q;
		break;
	default:
		aw_dev_err(aw881xx->dev, "type:%d not support", type);
		return -EINVAL;
	}

	return 0;
}

static int aw_cali_svc_get_devs_cali_val(struct aw881xx *aw881xx,
						int type, uint32_t *data_buf, int num)
{
	int ret = -EINVAL;
	int cnt = 0;
	struct list_head *dev_list = NULL;
	struct list_head *pos = NULL;
	struct aw881xx *local_dev = NULL;

	/*get dev list*/
	ret = aw881xx_get_list_head(&dev_list);
	if (ret) {
		aw_dev_err(aw881xx->dev, "get dev list failed");
		return ret;
	}

	list_for_each(pos, dev_list) {
		local_dev = container_of(pos, struct aw881xx, list_node);
		if (local_dev->channel < num) {
			switch (type) {
			case GET_RE_TYPE:
				data_buf[local_dev->channel] = local_dev->cali_desc.cali_re;
				break;
			case GET_F0_TYPE:
				data_buf[local_dev->channel] = local_dev->cali_desc.f0;
				break;
			case GET_Q_TYPE:
				data_buf[local_dev->channel] = local_dev->cali_desc.q;
				break;
			default:
				aw_dev_err(local_dev->dev, "type:%d not support", type);
				return -EINVAL;
			}
			cnt++;
		} else {
			aw_dev_err(local_dev->dev, "channel num[%d] overflow buf num[%d]",
						local_dev->channel, num);
			return -EINVAL;
		}
	}

	return cnt;
}
static int aw_cali_svc_set_devs_re_str(struct aw881xx *aw881xx, const char *re_str)
{
	int ret = -EINVAL;
	int cnt = 0;
	struct list_head *dev_list = NULL, *pos = NULL;
	struct aw881xx *local_dev = NULL;
	int re_data[AW_DEV_CH_MAX] = { 0 };
	char str_data[32] = { 0 };
	int i, len = 0;
	int dev_num = 0;

	/*get dev list*/
	ret = aw881xx_get_list_head(&dev_list);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "get dev list failed");
		return ret;
	}

	dev_num = aw881xx_get_dev_num();

	for (i = 0 ; i < dev_num; i++) {
		memset(str_data, 0, sizeof(str_data));
		snprintf(str_data, sizeof(str_data), "dev[%d]:%s ", i, "%d");
		ret = sscanf(re_str + len, str_data, &re_data[i]);
		if (ret <= 0) {
			aw_dev_err(aw881xx->dev, "unsupported str: %s", re_str);
			return -EINVAL;
		}
		len += snprintf(str_data, sizeof(str_data), "dev[%d]:%d ", i, re_data[i]);
		if (len > strlen(re_str)) {
			aw_dev_err(aw881xx->dev, "%s: unsupported", re_str);
			return -EINVAL;
		}
	}

	list_for_each (pos, dev_list) {
		local_dev = container_of(pos, struct aw881xx, list_node);
		if (local_dev->channel < AW_DEV_CH_MAX) {
			ret = aw_cali_store_cali_re(local_dev, re_data[local_dev->channel]);
			if (ret < 0) {
				aw_dev_err(local_dev->dev, "store cali re failed");
				return ret;
			}
			cnt++;
		}
	}

	return cnt;
}

static int aw_cali_svc_get_smooth_cali_re(struct aw881xx *aw881xx)
{
	int ret = -EINVAL;
	uint16_t dsp_addr = 0;
	uint16_t dsp_val = 0;
	uint32_t re_val = 0;
	uint32_t re_temp[CALI_READ_CNT_MAX] = { 0 };
	uint32_t re_sum = 0;
	uint16_t re_average = 0;
	uint16_t ra_val = 0;
	uint8_t iv_flag = 0;
	uint8_t cnt = 0;
	struct aw_cali_desc *cali_desc = &aw881xx->cali_desc;

	if (aw881xx == NULL) {
		aw_pr_err("aw881xx is NULL");
		return -1;
	}

	for (cnt = 0; cnt < CALI_READ_CNT_MAX; cnt++) {
		ret = aw881xx_get_ste_re_addr(aw881xx, &dsp_addr);
		if (ret < 0) {
			aw_dev_err(aw881xx->dev, "%s: get ste re addr error\n", __func__);
			goto cali_re_fail;
		}
		ret = aw881xx_dsp_read(aw881xx, dsp_addr, &dsp_val);
		if (ret < 0) {
			aw_dev_err(aw881xx->dev, "%s: dsp read reg0x%04x error", __func__, dsp_addr);
			goto cali_re_fail;
		}

		re_val = dsp_val;
		re_temp[cnt] = re_val;

		msleep(30);
	}

	// sort read re value
	aw881xx_bubble_sort(re_temp, CALI_READ_CNT_MAX);

	// delete two min value and two max value,compute mid value
	for (cnt = 1; cnt < CALI_READ_CNT_MAX - 1; cnt++) {
		re_sum += re_temp[cnt];
	}
	re_average = (uint16_t)(re_sum / (CALI_READ_CNT_MAX - CALI_DATA_SUM_RM));

	ret = aw881xx_get_adpz_ra_addr(aw881xx, &dsp_addr);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: get adpz ra addr error\n", __func__);
		goto cali_re_fail;
	}
	ret = aw881xx_dsp_read(aw881xx, dsp_addr, &dsp_val);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: dsp read dsp0x%04x error\n", __func__, dsp_addr);
		goto cali_re_fail;
	}
	ra_val = dsp_val;
	aw_dev_dbg(aw881xx->dev, "%s: ra=0x%x\n", __func__, ra_val);

	cali_desc->cali_re = AW_DSP_RE_TO_SHOW_RE((uint32_t)(re_average - ra_val));

	aw881xx_cali_get_iv(aw881xx, &iv_flag);
	if (!iv_flag) {
		aw_dev_err(aw881xx->dev, "%s: iv data abnormal\n", __func__);
		goto cali_re_fail;
	}

	if (cali_desc->cali_re < aw881xx->re_range.re_min ||
		cali_desc->cali_re > aw881xx->re_range.re_max) {
		aw_dev_err(aw881xx->dev, "%s: re value out range: %dmohm\n", __func__, cali_desc->cali_re);
		if (aw881xx->cali_desc.cali_check_st) {
			aw881xx->cali_desc.cali_result = CALI_RESULT_ERROR;
			ret = aw881xx_set_cali_re_to_phone(aw881xx);
			if (ret < 0) {
				aw_dev_err(aw881xx->dev, "write re failed");
			}
		}
		aw881xx_run_mute_for_cali(aw881xx, aw881xx->cali_desc.cali_result);
		return 0;
	}

	ret = aw881xx_set_cali_re_to_phone(aw881xx);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "write re failed");
		goto cali_re_fail;
	}

	aw881xx_set_cali_re_to_dsp(cali_desc);
	if (aw881xx->cali_desc.cali_check_st)
		aw881xx->cali_desc.cali_result = CALI_RESULT_NORMAL;

	aw_dev_info(aw881xx->dev, "re[%d]mohm", cali_desc->cali_re);

	return 0;
cali_re_fail:
	if (aw881xx->cali_desc.cali_check_st)
		aw881xx->cali_desc.cali_result = CALI_RESULT_ERROR;
	aw881xx_run_mute_for_cali(aw881xx, aw881xx->cali_desc.cali_result);
	return -EINVAL;

}

static int aw_cali_svc_dev_cali_re(struct aw881xx *aw881xx, unsigned int flag)
{
	int ret = -EINVAL;

	aw_dev_info(aw881xx->dev, "%s: enter\n", __func__);

	aw881xx_run_mute_for_cali(aw881xx, CALI_RESULT_NORMAL);

	ret = aw_cali_svc_cali_mode_enable(aw881xx,
				CALI_TYPE_RE, flag, true);
	if (ret < 0)
		return ret;

	msleep(g_cali_re_time);

	ret = aw_cali_svc_get_smooth_cali_re(aw881xx);
	if (ret < 0)
		aw_dev_err(aw881xx->dev, "%s: cali re failed\n", __func__);

	aw_cali_svc_cali_mode_enable(aw881xx,
				CALI_TYPE_RE, flag, false);

	return ret;
}

static int aw_cali_svc_devs_get_cali_re(struct list_head *dev_list)
{
	int ret = -EINVAL;
	struct list_head *pos = NULL;
	struct aw881xx *local_dev = NULL;

	list_for_each(pos, dev_list) {
		local_dev = container_of(pos, struct aw881xx, list_node);
		ret = aw_cali_svc_get_smooth_cali_re(local_dev);
		if (ret < 0) {
			aw_dev_err(local_dev->dev, "%s:get re failed", __func__);
			return ret;
		}
	}

	return ret;
}

static int aw_cali_svc_devs_cali_re(struct aw881xx *aw881xx, unsigned int flag)
{
	int ret = -EINVAL;
	struct list_head *dev_list = NULL;

	aw_dev_info(aw881xx->dev, "%s: enter\n", __func__);

	ret = aw881xx_get_list_head(&dev_list);
	if (ret) {
		aw_dev_err(aw881xx->dev, "%s: get dev list failed\n", __func__);
		return ret;
	}

	ret = aw_cali_svc_devs_cali_mode_enable(dev_list, CALI_TYPE_RE, flag, true);
	if (ret < 0)
		goto error;

	msleep(g_cali_re_time);

	ret = aw_cali_svc_devs_get_cali_re(dev_list);
	if (ret < 0) {
		aw_pr_err("get re failed\n");
		goto error;
	}

	aw_cali_svc_devs_cali_mode_enable(dev_list, CALI_TYPE_RE, flag, false);

	return 0;
error:
	aw_cali_svc_devs_cali_mode_enable(dev_list, CALI_TYPE_RE, flag, false);
	return ret;
}

static int aw_cali_svc_cali_re(struct aw881xx *aw881xx, bool is_single, unsigned int flag)
{
if (is_single)
		return aw_cali_svc_dev_cali_re(aw881xx, flag);
	else
		return aw_cali_svc_devs_cali_re(aw881xx, flag);
}

static int aw_cali_svc_dev_cali_f0_q(struct aw881xx *aw881xx, unsigned int flag)
{
	int ret = -EINVAL;

	aw881xx_run_mute_for_cali(aw881xx, CALI_RESULT_NORMAL);

	ret = aw_cali_svc_cali_mode_enable(aw881xx, CALI_TYPE_F0, flag, true);
	if (ret < 0)
		return ret;

	msleep(AW_CALI_F0_TIME);

	ret = aw_cali_svc_get_cali_f0_q(aw881xx);
	if (ret < 0)
		aw_dev_err(aw881xx->dev, "%s: get f0 q failed\n", __func__);

	aw_cali_svc_cali_mode_enable(aw881xx, CALI_TYPE_F0, flag, false);

	aw881xx_run_mute_for_cali(aw881xx, aw881xx->cali_desc.cali_result);

	return ret;
}

static int aw_cali_svc_devs_get_cali_f0_q(struct list_head *dev_list)
{
	int ret = -EINVAL;
	struct list_head *pos = NULL;
	struct aw881xx *local_dev = NULL;

	list_for_each(pos, dev_list) {
		local_dev = container_of(pos, struct aw881xx, list_node);
		ret = aw_cali_svc_get_cali_f0_q(local_dev);
		if (ret < 0) {
			aw_dev_err(local_dev->dev, "%s: get f0 q failed", __func__);
			return ret;
		}
	}

	return ret;
}

static int aw_cali_svc_devs_cali_f0_q(struct aw881xx *aw881xx, unsigned int flag)
{
	int ret = -EINVAL;
	struct list_head *dev_list = NULL;

	ret = aw881xx_get_list_head(&dev_list);
	if (ret) {
		aw_dev_err(aw881xx->dev, "%s: get dev list failed\n", __func__);
		return ret;
	}

	ret = aw_cali_svc_devs_cali_mode_enable(dev_list, CALI_TYPE_F0, flag, true);
	if (ret < 0)
		goto error;

	msleep(AW_CALI_F0_TIME);

	ret = aw_cali_svc_devs_get_cali_f0_q(dev_list);
	if (ret < 0) {
		aw_pr_err("get f0 q failed");
		goto error;
	}

	aw_cali_svc_devs_cali_mode_enable(dev_list, CALI_TYPE_F0, flag, false);

	return 0;

error:
	aw_cali_svc_devs_cali_mode_enable(dev_list, CALI_TYPE_F0, flag, false);
	return ret;
}

static int aw_cali_svc_cali_f0_q(struct aw881xx *aw881xx, bool is_single, unsigned int flag)
{
	if (is_single)
		return aw_cali_svc_dev_cali_f0_q(aw881xx, flag);
	else
		return aw_cali_svc_devs_cali_f0_q(aw881xx, flag);
}

static int aw_cali_svc_cali_re_f0_q(struct aw881xx *aw881xx, bool is_single, unsigned int flag)
{
	int ret = -EINVAL;

	ret = aw_cali_svc_cali_re(aw881xx, is_single, flag);
	if (ret < 0)
		return ret;

	ret = aw_cali_svc_cali_f0_q(aw881xx, is_single, flag);
	if (ret < 0)
		return ret;

	return 0;
}

static int aw_cali_svc_get_dev_f0(struct aw881xx *aw881xx, uint32_t *f0)
{
	int ret = -EINVAL;

	int16_t f0_a1_val;
	int16_t f0_a2_val;
	uint16_t temp_val = 0;
	uint16_t reg_val = 0;
	int32_t f0_val;
	int32_t q_val;
	adapZdriver_t cfg;

	ret = aw881xx_get_fs(aw881xx);
	if (ret != 0)
		return ret;

	ret = aw881xx_reg_read(aw881xx, AW881XX_REG_ASR4, &reg_val);
	if (ret < 0)
		return ret;
	temp_val = reg_val;
	f0_a1_val = (int16_t)temp_val;

	temp_val = 0;
	ret = aw881xx_reg_read(aw881xx, AW881XX_REG_ASR3, &reg_val);
	if (ret < 0)
		return ret;
	temp_val = reg_val;
	f0_a2_val = (int16_t)temp_val;

	cfg.real_cal_A1 = (int32_t)f0_a1_val;
	cfg.real_cal_A2 = (int32_t)f0_a2_val;
	ret = aw_get_f0_q(&cfg, aw881xx->fs, &f0_val, &q_val);
	*f0 = f0_val;

	aw_dev_info(aw881xx->dev, "%s: spkr f0 = %d", __func__, *f0);

	return 0;
}

static int aw_cali_svc_get_devs_f0(struct aw881xx *aw881xx, int32_t *f0_buf, int num)
{
	int ret = -EINVAL;
	int cnt = 0;
	struct list_head *dev_list = NULL;
	struct list_head *pos = NULL;
	struct aw881xx *local_dev = NULL;

	//get dev list
	ret = aw881xx_get_list_head(&dev_list);
	if (ret) {
		aw_dev_err(aw881xx->dev, "%s: get dev list failed\n", __func__);
		return ret;
	}

	list_for_each (pos, dev_list) {
		local_dev = container_of(pos, struct aw881xx, list_node);
		if (local_dev->channel < num) {
			ret = aw_cali_svc_get_dev_f0(local_dev, &f0_buf[local_dev->channel]);
			if (ret) {
				aw_dev_err(local_dev->dev, "%s: get f0 failed!\n", __func__);
				return ret;
			}
			cnt++;
		} else {
			aw_dev_err(aw881xx->dev, "%s: channel num[%d] overflow buf num[%d] \n",
				__func__, local_dev->channel, num);
		}
	}
	return cnt;
}

static int aw_cali_svc_get_dev_te(struct aw_cali_desc *cali_desc, int32_t *te)
{
	int ret = -EINVAL;
	int16_t te_val = 0;
	struct aw881xx *aw881xx =
		container_of(cali_desc, struct aw881xx, cali_desc);

	ret = aw881xx_reg_read(aw881xx, AW881XX_REG_ASR2, (uint16_t *)&te_val);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: read addr:0x%x failed\n",
			__func__, AW881XX_REG_ASR2);
		return ret;
	}

	*te = te_val;

	aw_dev_info(aw881xx->dev, "%s: real_te:[%d]\n", __func__, *te);

	return 0;
}

static int aw_cali_svc_get_devs_te(struct aw881xx *aw881xx, int32_t *te_buf, int num)
{
	int ret = -EINVAL;
	int cnt = 0;
	struct list_head *dev_list = NULL;
	struct list_head *pos = NULL;
	struct aw881xx *local_dev = NULL;

	//get dev list
	ret = aw881xx_get_list_head(&dev_list);
	if (ret) {
		aw_dev_err(aw881xx->dev, "%s: get dev list failed\n", __func__);
		return ret;
	}

	list_for_each (pos, dev_list) {
		local_dev = container_of(pos, struct aw881xx, list_node);
		if (local_dev->channel < num) {
			ret = aw_cali_svc_get_dev_te(&local_dev->cali_desc, &te_buf[local_dev->channel]);
			if (ret) {
				aw_dev_err(local_dev->dev, "%s: get temperature failed!\n", __func__);
				return ret;
			}
			cnt++;
		} else {
			aw_dev_err(aw881xx->dev, "%s: channel num[%d] overflow buf num[%d]\n",
						__func__, local_dev->channel, num);
		}
	}
	return cnt;
}

static int aw_cali_svc_get_dev_re_range(struct aw881xx *aw881xx,
						uint32_t *data_buf)
{
	data_buf[RE_MIN_FLAG] = aw881xx->re_range.re_min;
	data_buf[RE_MAX_FLAG] = aw881xx->re_range.re_max;

	return 0;
}

static int aw_cali_svc_get_devs_re_range(struct aw881xx *aw881xx,
						uint32_t *data_buf, int num)
{
	int ret = -EINVAL;
	int cnt = 0;
	struct list_head *dev_list = NULL;
	struct list_head *pos = NULL;
	struct aw881xx *local_dev = NULL;

	/*get dev list*/
	ret = aw881xx_get_list_head(&dev_list);
	if (ret) {
		aw_dev_err(aw881xx->dev, "%s: get dev list failed\n", __func__);
		return ret;
	}

	list_for_each(pos, dev_list) {
		local_dev = container_of(pos, struct aw881xx, list_node);
		if (local_dev->channel < num) {
			data_buf[RE_MIN_FLAG + local_dev->channel * 2] =
				local_dev->re_range.re_min;
			data_buf[RE_MAX_FLAG + local_dev->channel * 2] =
				local_dev->re_range.re_max;
			cnt++;
		} else {
			aw_dev_err(local_dev->dev, "%s: channel num[%d] overflow buf num[%d]",
						__func__, local_dev->channel, num);
			return -EINVAL;
		}
	}

	return cnt;
}

/*****************************class node******************************************************/
static ssize_t aw_cali_class_time_show(struct class *class, struct class_attribute *attr, char *buf)
{
	ssize_t len = 0;

	len += snprintf(buf+len, PAGE_SIZE-len,
		"time: %d\n", g_cali_re_time);

	return len;
}

static ssize_t aw_cali_class_time_store(struct class *class,
					struct class_attribute *attr, const char *buf, size_t len)
{
	int ret = -EINVAL;
	uint32_t time;

	ret = kstrtoint(buf, 0, &time);
	if (ret < 0) {
		aw_pr_err("read buf %s failed", buf);
		return ret;
	}

	if (time < 1000) {
		aw_pr_err("time:%d is too short, no set", time);
		return -EINVAL;
	}

	g_cali_re_time = time;
	aw_pr_dbg("time:%d", time);

	return len;
}

static ssize_t aw_cali_class_cali_re_show(struct  class *class, struct class_attribute *attr, char *buf)
{
	int ret = -EINVAL;
	int i = 0;
	struct list_head *dev_list;
	struct aw881xx *local_dev = NULL;
	ssize_t len = 0;
	uint32_t cali_re[AW_DEV_CH_MAX] = { 0 };

	aw_pr_info("enter");

	ret = aw881xx_get_list_head(&dev_list);
	if (ret) {
		aw_pr_err("get dev list failed");
		return ret;
	}

	local_dev = list_first_entry(dev_list, struct aw881xx, list_node);

	ret = aw_cali_svc_cali_re(local_dev, false, CALI_OPS_HMUTE);
	if (ret < 0)
		return ret;

	ret = aw_cali_svc_get_devs_cali_val(local_dev, GET_RE_TYPE, cali_re, AW_DEV_CH_MAX);
	if (ret <= 0) {
		aw_dev_err(local_dev->dev, "get re failed");
	} else {
		for (i = 0; i < ret; i++)
			len += snprintf(buf+len, PAGE_SIZE-len, "dev[%d]:%u mOhms ", i, cali_re[i]);

		len += snprintf(buf+len, PAGE_SIZE-len, "\n");
	}

	return len;
}

static ssize_t aw_cali_class_cali_re_store(struct class *class,
					struct class_attribute *attr, const char *buf, size_t len)
{
	int ret = -EINVAL;
	struct list_head *dev_list = NULL;
	struct aw881xx *local_dev = NULL;

	ret = aw881xx_get_list_head(&dev_list);
	if (ret) {
		aw_pr_err("get dev list failed");
		return ret;
	}

	local_dev = list_first_entry(dev_list, struct aw881xx, list_node);

	ret = aw_cali_svc_set_devs_re_str(local_dev, buf);
	if (ret <= 0) {
		aw_pr_err("set re str %s failed", buf);
		return -EPERM;
	}

	return len;
}

static ssize_t aw_cali_class_cali_f0_show(struct  class *class,
					struct class_attribute *attr, char *buf)
{
	int ret = -EINVAL;
	struct list_head *dev_list = NULL;
	struct aw881xx *local_dev = NULL;
	int i = 0;
	ssize_t len = 0;
	uint32_t f0[AW_DEV_CH_MAX] = { 0 };

	aw_pr_info("enter");

	ret = aw881xx_get_list_head(&dev_list);
	if (ret < 0) {
		aw_pr_err("get dev list failed");
		return ret;
	}

	local_dev = list_first_entry(dev_list, struct aw881xx, list_node);

	ret = aw_cali_svc_cali_f0_q(local_dev, is_single_cali, CALI_OPS_NOISE);
	if (ret < 0) {
		aw_pr_err("cali f0 failed");
		return ret;
	}

	ret = aw_cali_svc_get_devs_cali_val(local_dev, GET_F0_TYPE, f0, AW_DEV_CH_MAX);
	if (ret <= 0) {
		aw_pr_err("get f0 failed");
	} else {
		for (i = 0; i < ret; i++)
			len += snprintf(buf+len, PAGE_SIZE-len, "dev[%d]:%u Hz ",
					i, f0[i]);

		len += snprintf(buf+len, PAGE_SIZE-len, " \n");
	}

	return len;
}

static ssize_t aw_cali_class_cali_f0_q_show(struct  class *class, struct class_attribute *attr, char *buf)
{
	int ret = -EINVAL;
	int i = 0;
	struct list_head *dev_list = NULL;
	struct aw881xx *local_dev = NULL;
	ssize_t len = 0;
	uint32_t f0[AW_DEV_CH_MAX] = { 0 };
	uint32_t q[AW_DEV_CH_MAX] = { 0 };

	aw_pr_info("enter");

	ret = aw881xx_get_list_head(&dev_list);
	if (ret < 0) {
		aw_pr_err("get dev list failed");
		return ret;
	}

	local_dev = list_first_entry(dev_list, struct aw881xx, list_node);

	ret = aw_cali_svc_cali_f0_q(local_dev, is_single_cali, CALI_OPS_NOISE);
	if (ret < 0) {
		aw_dev_err(local_dev->dev, "%s: cali f0 q failed\n", __func__);
		return ret;
	}

	ret = aw_cali_svc_get_devs_cali_val(local_dev, GET_F0_TYPE, f0, AW_DEV_CH_MAX);
	if (ret <= 0) {
		aw_dev_err(local_dev->dev, "%s: get f0 failed\n", __func__);
		return -EINVAL;
	}

	ret = aw_cali_svc_get_devs_cali_val(local_dev, GET_Q_TYPE, q, AW_DEV_CH_MAX);
	if (ret <= 0) {
		aw_dev_err(local_dev->dev, "%s: get q failed\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < ret; i++)
		len += snprintf(buf+len, PAGE_SIZE-len, "dev[%d]:f0:%u Hz q:%u ",
			i, f0[i], q[i]);

	len += snprintf(buf+len, PAGE_SIZE-len, " \n");

	return len;
}

static ssize_t aw_class_re_range_show(struct  class *class, struct class_attribute *attr, char *buf)
{
	int ret = -EINVAL;
	int i = 0;
	ssize_t len = 0;
	struct list_head *dev_list = NULL;
	struct aw881xx *local_dev = NULL;
	uint32_t re_value[AW_DEV_RE_RANGE] = { 0 };

	aw_pr_info("enter");

	ret = aw881xx_get_list_head(&dev_list);
	if (ret < 0) {
		aw_pr_err("get dev list failed");
		return ret;
	}

	local_dev = list_first_entry(dev_list, struct aw881xx, list_node);
	ret = aw_cali_svc_get_devs_re_range(local_dev, re_value, AW_DEV_CH_MAX);
	if (ret <= 0) {
		aw_dev_err(local_dev->dev, "%s: get re range failed\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < ret; i++) {
		len += snprintf(buf+len, PAGE_SIZE-len, "dev[%d]:re_min:%d re_max:%d ",
			i, re_value[RE_MIN_FLAG + i * RE_RANGE_NUM],
			re_value[RE_MAX_FLAG + i * RE_RANGE_NUM]);
	}
	len += snprintf(buf+len, PAGE_SIZE-len, " \n");

	return len;
}

static struct class aw_cali_class = {
	.name = "smartpa",
	.owner = THIS_MODULE,
};

static struct class_attribute class_attr_cali_time = \
		__ATTR(cali_time, S_IWUSR | S_IRUGO, \
		aw_cali_class_time_show, aw_cali_class_time_store);

static struct class_attribute class_attr_re25_calib = \
		__ATTR(re25_calib, S_IWUSR | S_IRUGO, \
		aw_cali_class_cali_re_show, aw_cali_class_cali_re_store);

static struct class_attribute class_attr_f0_calib = \
		__ATTR(f0_calib, S_IRUGO, \
		aw_cali_class_cali_f0_show, NULL);

static struct class_attribute class_attr_f0_q_calib = \
		__ATTR(f0_q_calib, S_IRUGO, \
		aw_cali_class_cali_f0_q_show, NULL);

static struct class_attribute class_att_re_range = \
		__ATTR(re_range, S_IRUGO, \
		aw_class_re_range_show, NULL);

static void aw_cali_class_attr_init(struct aw881xx *aw881xx)
{
	int ret = -EINVAL;

	if (aw881xx->channel != 0) {
		aw_dev_err(aw881xx->dev, "%s: class node already register\n", __func__);
		return;
	}

	ret = class_register(&aw_cali_class);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: error creating class node\n", __func__);
		return;
	}

	ret = class_create_file(&aw_cali_class, &class_attr_cali_time);
	if (ret)
		aw_dev_err(aw881xx->dev, "%s: creat class_attr_cali_time fail\n", __func__);

	ret = class_create_file(&aw_cali_class, &class_attr_re25_calib);
	if (ret)
		aw_dev_err(aw881xx->dev, "%s: creat class_attr_re25_calib fail\n", __func__);

	ret = class_create_file(&aw_cali_class, &class_attr_f0_calib);
	if (ret)
		aw_dev_err(aw881xx->dev, "%s: creat class_attr_f0_calib fail\n", __func__);


	ret = class_create_file(&aw_cali_class, &class_attr_f0_q_calib);
	if (ret)
		aw_dev_err(aw881xx->dev, "%s: creat class_attr_f0_q_calib fail\n", __func__);

	ret = class_create_file(&aw_cali_class, &class_att_re_range);
	if (ret)
		aw_dev_err(aw881xx->dev, "%s: creat class_att_re_range fail\n", __func__);
}

static void aw_cali_class_attr_deinit(struct aw881xx *aw881xx)
{
	class_remove_file(&aw_cali_class, &class_att_re_range);
	class_remove_file(&aw_cali_class, &class_attr_f0_q_calib);
	class_remove_file(&aw_cali_class, &class_attr_f0_calib);
	class_remove_file(&aw_cali_class, &class_attr_re25_calib);
	class_remove_file(&aw_cali_class, &class_attr_cali_time);

	class_unregister(&aw_cali_class);
	aw_dev_info(aw881xx->dev, "%s: unregister class node\n", __func__);
}

static ssize_t aw_cali_attr_re_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	int ret = -EINVAL;
	struct aw881xx *aw881xx = dev_get_drvdata(dev);
	int re;

	if (is_single_cali) {
		ret = kstrtoint(buf, 0, &re);
		if (ret < 0) {
			aw_dev_err(aw881xx->dev, "%s: read buf %s failed\n", __func__, buf);
			return ret;
		}

		ret = aw_cali_store_cali_re(aw881xx, re);
		if (ret < 0) {
			aw_dev_err(aw881xx->dev, "%s: store cali re failed!\n", __func__);
			return ret;
		}
	} else {
		ret = aw_cali_svc_set_devs_re_str(aw881xx, buf);
		if (ret <= 0) {
			aw_pr_err("set re str %s failed", buf);
			return -EPERM;
		}
	}

	return count;
}

static ssize_t aw_cali_attr_re_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int ret = -EINVAL;
	int i = 0;
	struct aw881xx *aw881xx = dev_get_drvdata(dev);
	ssize_t len = 0;
	int32_t re[AW_DEV_CH_MAX] = { 0 };

	ret = aw_cali_svc_cali_re(aw881xx, is_single_cali, CALI_OPS_HMUTE);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "cali re failed");
		return ret;
	}

	if (is_single_cali) {
		len += snprintf(buf+len, PAGE_SIZE-len, "dev[%d]: %umOhms \n",
				aw881xx->channel, aw881xx->cali_desc.cali_re);
	} else {
		ret = aw_cali_svc_get_devs_cali_val(aw881xx, GET_RE_TYPE, re, AW_DEV_CH_MAX);
		if (ret <= 0) {
			aw_dev_err(aw881xx->dev, "get re failed");
		} else {
			for (i = 0; i < ret; i++)
				len += snprintf(buf+len, PAGE_SIZE-len, "dev[%d]: %umOhms ", i, re[i]);

			len += snprintf(buf+len, PAGE_SIZE-len, " \n");
		}
	}

	return len;
}

static ssize_t aw_cali_attr_f0_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int ret = -EINVAL;
	int i = 0;
	struct aw881xx *aw881xx = dev_get_drvdata(dev);
	ssize_t len = 0;
	uint32_t f0[AW_DEV_CH_MAX] = { 0 };

	ret = aw_cali_svc_cali_f0_q(aw881xx, is_single_cali, CALI_OPS_NOISE);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "cali f0 failed");
		return ret;
	}

	if (is_single_cali) {
		len += snprintf(buf+len, PAGE_SIZE-len, "dev[%d]:%u Hz\n",
				aw881xx->channel, aw881xx->cali_desc.f0);
	} else {
		ret = aw_cali_svc_get_devs_cali_val(aw881xx, GET_F0_TYPE, f0, AW_DEV_CH_MAX);
		if (ret <= 0) {
			aw_dev_err(aw881xx->dev, "get re failed");
		} else {
			for (i = 0; i < ret; i++)
				len += snprintf(buf+len, PAGE_SIZE-len, "dev[%d]:%u Hz ", i, f0[i]);

			len += snprintf(buf+len, PAGE_SIZE-len, " \n");
		}
	}

	return len;
}

static ssize_t aw_cali_attr_f0_q_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int ret = -EINVAL;
	int i = 0;
	struct aw881xx *aw881xx = dev_get_drvdata(dev);
	ssize_t len = 0;
	uint32_t f0[AW_DEV_CH_MAX] = { 0 };
	uint32_t q[AW_DEV_CH_MAX] = { 0 };

	ret = aw_cali_svc_cali_f0_q(aw881xx, is_single_cali, CALI_OPS_NOISE);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "cali f0 q failed");
		return ret;
	}

	if (is_single_cali) {
		len += snprintf(buf+len, PAGE_SIZE-len, "dev[%d]f0:%u Hz q:%u\n",
				aw881xx->channel, aw881xx->cali_desc.f0, aw881xx->cali_desc.q);
	} else {

		ret = aw_cali_svc_get_devs_cali_val(aw881xx, GET_F0_TYPE, f0, AW_DEV_CH_MAX);
		if (ret <= 0) {
			aw_dev_err(aw881xx->dev, "get f0 failed");
			return -EINVAL;
		}

		ret = aw_cali_svc_get_devs_cali_val(aw881xx, GET_Q_TYPE, q, AW_DEV_CH_MAX);
		if (ret <= 0) {
			aw_dev_err(aw881xx->dev, "get q failed");
			return -EINVAL;
		}

		for (i = 0; i < ret; i++)
			len += snprintf(buf+len, PAGE_SIZE-len, "dev[%d]:f0:%u Hz q:%u ",
				i, f0[i], q[i]);

		len += snprintf(buf+len, PAGE_SIZE-len, " \n");
	}

	return len;
}

static ssize_t aw_cali_attr_time_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = -EINVAL;
	uint32_t time;
	struct aw881xx *aw881xx = dev_get_drvdata(dev);

	ret = kstrtoint(buf, 0, &time);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "read buf %s failed", buf);
		return ret;
	}

	if (time < 1000) {
		aw_dev_err(aw881xx->dev, "time:%d is too short, no set", time);
		return -EINVAL;
	}

	g_cali_re_time = time;
	aw_dev_dbg(aw881xx->dev, "time:%u", time);

	return count;
}

static ssize_t aw_cali_attr_time_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;

	len += snprintf(buf+len, PAGE_SIZE-len,
		"time: %u\n", g_cali_re_time);

	return len;
}

static ssize_t aw_cali_attr_re_range_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	struct aw881xx *aw881xx = dev_get_drvdata(dev);
	uint32_t range_buf[RE_RANGE_NUM] = { 0 };

	aw_cali_svc_get_dev_re_range(aw881xx, range_buf);

	len += snprintf(buf + len, PAGE_SIZE - len,
		"re_min value: [%d]\n", range_buf[RE_MIN_FLAG]);
	len += snprintf(buf + len, PAGE_SIZE - len,
		"re_max value: [%d]\n", range_buf[RE_MAX_FLAG]);

	return len;
}

static DEVICE_ATTR(cali_re, S_IWUSR | S_IRUGO,
			aw_cali_attr_re_show, aw_cali_attr_re_store);
static DEVICE_ATTR(cali_f0, S_IRUGO,
			aw_cali_attr_f0_show, NULL);
static DEVICE_ATTR(cali_f0_q, S_IRUGO,
			aw_cali_attr_f0_q_show, NULL);
static DEVICE_ATTR(cali_time, S_IWUSR | S_IRUGO,
			aw_cali_attr_time_show, aw_cali_attr_time_store);
static DEVICE_ATTR(re_range, S_IRUGO,
			aw_cali_attr_re_range_show, NULL);

static struct attribute *aw_cali_attr[] = {
	&dev_attr_cali_re.attr,
	&dev_attr_cali_f0.attr,
	&dev_attr_cali_f0_q.attr,
	&dev_attr_cali_time.attr,
	&dev_attr_re_range.attr,
	NULL
};

static struct attribute_group aw_cali_attr_group = {
	.attrs = aw_cali_attr
};

static void aw_cali_attr_init(struct aw881xx *aw881xx)
{
	int ret = -EINVAL;

	ret = sysfs_create_group(&aw881xx->dev->kobj, &aw_cali_attr_group);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev,
			"%s error creating sysfs attr files\n", __func__);
	}
}

static void aw_cali_attr_deinit(struct aw881xx *aw881xx)
{
	sysfs_remove_group(&aw881xx->dev->kobj, &aw_cali_attr_group);
	aw_dev_info(aw881xx->dev, "attr files deinit");
}

static ssize_t aw881xx_cali_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int ret = -EINVAL;
	struct aw881xx *aw881xx = dev_get_drvdata(dev);
	struct aw_cali_desc *cali_desc = &aw881xx->cali_desc;
	unsigned int databuf[2] = { 0 };

	ret = kstrtouint(buf, 0, &databuf[0]);
	if (ret < 0)
		return ret;

	if (databuf[0] < aw881xx->re_range.re_min ||
		databuf[0] > aw881xx->re_range.re_max) {
		aw_dev_err(aw881xx->dev, "%s: re value out range: %dmohm\n",
			__func__, databuf[0]);
		return -EINVAL;
	}
	cali_desc->cali_re = databuf[0];
	ret = aw881xx_set_cali_re(cali_desc);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t aw881xx_cali_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw881xx *aw881xx = dev_get_drvdata(dev);
	struct aw_cali_desc *cali_desc = &aw881xx->cali_desc;
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len,
			"cali_re=%umohm\n", cali_desc->cali_re);

	return len;
}

static ssize_t aw881xx_re_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	int ret = -EINVAL;
	struct aw881xx *aw881xx = dev_get_drvdata(dev);
	struct aw_cali_desc *cali_desc = &aw881xx->cali_desc;
	unsigned int databuf[2] = { 0 };

	ret = kstrtouint(buf, 0, &databuf[0]);
	if (ret < 0)
		return ret;

	if (databuf[0] < aw881xx->re_range.re_min ||
		databuf[0] > aw881xx->re_range.re_max) {
		aw_dev_err(aw881xx->dev, "%s: re value out range: %dmohm\n",
			__func__, databuf[0]);
		return -EINVAL;
	}

	cali_desc->cali_re = databuf[0];

	ret = aw881xx_set_cali_re(cali_desc);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t aw881xx_re_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw881xx *aw881xx = dev_get_drvdata(dev);
	struct aw_cali_desc *cali_desc = &aw881xx->cali_desc;
	ssize_t len = 0;

	aw881xx_get_cali_re(cali_desc);

	len += snprintf(buf + len, PAGE_SIZE - len,
			"re=%umohm\n", cali_desc->cali_re);

	return len;
}

static ssize_t aw881xx_f0_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	int ret = -EINVAL;
	struct aw881xx *aw881xx = dev_get_drvdata(dev);
	struct aw_cali_desc *cali_desc = &aw881xx->cali_desc;
	unsigned int databuf[2] = { 0 };

	ret = kstrtouint(buf, 0, &databuf[0]);
	if (ret < 0)
		return ret;

	cali_desc->f0 = databuf[0];

	return count;
}

static ssize_t aw881xx_f0_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw881xx *aw881xx = dev_get_drvdata(dev);
	struct aw_cali_desc *cali_desc = &aw881xx->cali_desc;
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "f0=%uHz\n", cali_desc->f0);

	return len;
}

static ssize_t aw881xx_q_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	int ret = -EINVAL;
	struct aw881xx *aw881xx = dev_get_drvdata(dev);
	struct aw_cali_desc *cali_desc = &aw881xx->cali_desc;
	unsigned int databuf[2] = { 0 };

	ret = kstrtouint(buf, 0, &databuf[0]);
	if (ret < 0)
		return ret;

	cali_desc->q = databuf[0];

	return count;
}

static ssize_t aw881xx_q_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw881xx *aw881xx = dev_get_drvdata(dev);
	struct aw_cali_desc *cali_desc = &aw881xx->cali_desc;
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "q=%u\n", cali_desc->q);

	return len;
}

static ssize_t aw881xx_dsp_re_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int ret = -EINVAL;
	struct aw881xx *aw881xx = dev_get_drvdata(dev);
	ssize_t len = 0;
	uint32_t read_re = 0;

	ret = aw881xx_read_cali_re_from_dsp(&aw881xx->cali_desc, &read_re);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s:read dsp re fail\n", __func__);
		return ret;
	}

	len += snprintf((char *)(buf + len),
		PAGE_SIZE - len,
		"dsp_re: %d\n", read_re);

	return len;
}

static ssize_t aw881xx_r0_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int ret = -EINVAL;
	struct aw881xx *aw881xx = dev_get_drvdata(dev);
	ssize_t len = 0;
	uint32_t r0;

	aw881xx_get_dsp_config(aw881xx);
	if (aw881xx->dsp_cfg == AW881XX_DSP_BYPASS) {
		len += snprintf((char *)(buf + len), PAGE_SIZE - len,
				"%s: aw881xx dsp bypass\n", __func__);
		return len;
	}

	ret = aw881xx_get_iis_status(aw881xx);
	if (ret < 0) {
		len += snprintf((char *)(buf + len),
				PAGE_SIZE - len,
				"%s: aw881xx no iis signal\n",
				__func__);
		return len;
	}

	ret = aw881xx_get_r0(aw881xx, &r0);
	if (ret < 0) {
		len += snprintf((char *)(buf + len),
				PAGE_SIZE - len,
				"%s: read r0 failed\n",
				__func__);
		return len;
	}
	len += snprintf((char *)(buf + len),
		PAGE_SIZE - len,
		"r0: %d\n", r0);

	return len;
}

static DEVICE_ATTR(cali, S_IWUSR | S_IRUGO,
			aw881xx_cali_show, aw881xx_cali_store);
static DEVICE_ATTR(re, S_IWUSR | S_IRUGO,
			aw881xx_re_show, aw881xx_re_store);
static DEVICE_ATTR(f0, S_IWUSR | S_IRUGO,
			aw881xx_f0_show, aw881xx_f0_store);
static DEVICE_ATTR(q, S_IWUSR | S_IRUGO,
			aw881xx_q_show, aw881xx_q_store);
static DEVICE_ATTR(dsp_re, S_IRUGO,
			aw881xx_dsp_re_show, NULL);
static DEVICE_ATTR(r0, S_IRUGO,
			aw881xx_r0_show, NULL);

static struct attribute *aw881xx_cali_attr[] = {
	&dev_attr_cali.attr,
	&dev_attr_re.attr,
	&dev_attr_f0.attr,
	&dev_attr_q.attr,
	&dev_attr_dsp_re.attr,
	&dev_attr_r0.attr,
	NULL
};

static struct attribute_group aw881xx_cali_attr_group = {
	.attrs = aw881xx_cali_attr
};

static void aw881xx_attr_init(struct aw881xx *aw881xx)
{
	int ret = -EINVAL;

	ret = sysfs_create_group(&aw881xx->dev->kobj, &aw881xx_cali_attr_group);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev,
			"%s error creating sysfs attr files\n", __func__);
	}
}

static void aw881xx_attr_deinit(struct aw881xx *aw881xx)
{
	sysfs_remove_group(&aw881xx->dev->kobj, &aw881xx_cali_attr_group);
	aw_dev_info(aw881xx->dev, "attr files deinit");
}

static int aw_cali_misc_open(struct inode *inode, struct file *file)
{
	int ret = -EINVAL;
	struct list_head *dev_list = NULL;
	struct list_head *pos = NULL;
	struct aw881xx *local_dev = NULL;

	aw_pr_dbg("misc open success");

	ret = aw881xx_get_list_head(&dev_list);
	if (ret) {
		aw_pr_err("get dev list failed");
		file->private_data = NULL;
		return -EINVAL;
	}

	//find select dev
	list_for_each (pos, dev_list) {
		local_dev = container_of(pos, struct aw881xx, list_node);
		if (local_dev->channel == g_dev_select)
			break;
	}

	if (local_dev == NULL) {
		aw_pr_err("get dev failed");
		return -EINVAL;
	}

	//cannot find sel dev, use list first dev
	if (local_dev->channel != g_dev_select) {
		local_dev = list_first_entry(dev_list, struct aw881xx, list_node);
		aw_dev_dbg(local_dev->dev, "can not find dev[%d], use default", g_dev_select);
	}

	file->private_data = (void *)local_dev;

	aw_dev_dbg(local_dev->dev, "misc open success");

	return 0;
}

static int aw_cali_misc_release(struct inode *inode, struct file *file)
{
	file->private_data = (void *)NULL;

	aw_pr_dbg("misc release success");

	return 0;
}

static int aw_cali_misc_ops_write(struct aw881xx *aw881xx,
			unsigned int cmd, unsigned long arg)
{

	int ret = 0;
	unsigned int data_len = _IOC_SIZE(cmd);
	char *data_ptr = NULL;

	data_ptr = devm_kzalloc(aw881xx->dev, data_len, GFP_KERNEL);
	if (!data_ptr) {
		aw_dev_err(aw881xx->dev, "malloc failed !");
		return -ENOMEM;
	}

	if (copy_from_user(data_ptr, (void __user *)arg, data_len)) {
		ret = -EFAULT;
		goto exit;
	}

	switch (cmd) {
		case AW_IOCTL_SET_CALI_RE: {
			aw_cali_store_cali_re(aw881xx, *((int32_t *)data_ptr));
		} break;
		default:{
			aw_dev_err(aw881xx->dev, "unsupported  cmd %d", cmd);
			ret = -EINVAL;
		} break;
	}

exit:
	devm_kfree(aw881xx->dev, data_ptr);
	data_ptr = NULL;
	return ret;
}

static int aw_cali_misc_ops_read(struct aw881xx *aw881xx,
			unsigned int cmd, unsigned long arg)
{
	int ret = -EINVAL;

	int16_t data_len = _IOC_SIZE(cmd);
	char *data_ptr = NULL;
	int32_t *data_32_ptr = NULL;

	data_ptr = devm_kzalloc(aw881xx->dev, data_len, GFP_KERNEL);
	if (!data_ptr) {
		aw_dev_err(aw881xx->dev, "malloc failed !");
		return -ENOMEM;
	}

	data_32_ptr = (int32_t *)data_ptr;
	switch (cmd) {
		case AW_IOCTL_GET_RE: {
			ret = aw_cali_svc_dev_cali_re(aw881xx, CALI_OPS_HMUTE);
			if (ret < 0)
				goto exit;

			ret = aw_cali_svc_get_dev_cali_val(aw881xx, GET_RE_TYPE, data_32_ptr);
		} break;
		case AW_IOCTL_GET_CALI_F0: {
			ret = aw_cali_svc_dev_cali_f0_q(aw881xx, CALI_OPS_NOISE);
			if (ret < 0)
				goto exit;

			ret = aw_cali_svc_get_dev_cali_val(aw881xx, GET_F0_TYPE, data_32_ptr);
		} break;
		case AW_IOCTL_GET_F0: {
			ret = aw_cali_svc_get_dev_f0(aw881xx, data_32_ptr);
		} break;
		case AW_IOCTL_GET_TE: {
			ret = aw_cali_svc_get_dev_te(&aw881xx->cali_desc, data_32_ptr);
		} break;
		case AW_IOCTL_GET_REAL_R0: {
			ret = aw881xx_get_r0(aw881xx, data_32_ptr);
		} break;
		case AW_IOCTL_GET_RE_RANGE: {
			ret = aw_cali_svc_get_dev_re_range(aw881xx, data_32_ptr);
		} break;
		default:{
			aw_dev_err(aw881xx->dev, "unsupported  cmd %d", cmd);
			ret = -EINVAL;
		} break;
	}

exit:
	if (copy_to_user((void __user *)arg,
		data_ptr, data_len)) {
		ret = -EFAULT;
	}

	devm_kfree(aw881xx->dev,data_ptr);
	data_ptr = NULL;
	return ret;
}

static int aw_cali_misc_ops(struct aw881xx *aw881xx,
			unsigned int cmd, unsigned long arg)
{
	int ret = 0;

	switch (cmd) {
	case AW_IOCTL_SET_CALI_RE:
		return aw_cali_misc_ops_write(aw881xx, cmd, arg);
	case AW_IOCTL_GET_F0:
	case AW_IOCTL_GET_CALI_F0:
	case AW_IOCTL_GET_RE:
	case AW_IOCTL_GET_REAL_R0:
	case AW_IOCTL_GET_TE:
	case AW_IOCTL_GET_RE_RANGE:
		return aw_cali_misc_ops_read(aw881xx, cmd, arg);
	default:
		aw_dev_err(aw881xx->dev, "unsupported  cmd %d", cmd);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static long aw_cali_misc_unlocked_ioctl(struct file *file,
			unsigned int cmd, unsigned long arg)
{
	int ret = -EINVAL;
	struct aw881xx *aw881xx = NULL;

	if (((_IOC_TYPE(cmd)) != (AW_IOCTL_MAGIC))) {
		aw_pr_err(" cmd magic err");
		return -EINVAL;
	}
	aw881xx = (struct aw881xx *)file->private_data;
	ret = aw_cali_misc_ops(aw881xx, cmd, arg);
	if (ret < 0)
		return -EINVAL;
	return 0;
}

#ifdef CONFIG_COMPAT
static long aw_cali_misc_compat_ioctl(struct file *file,
	unsigned int cmd, unsigned long arg)
{
	int ret = -EINVAL;
	struct aw881xx *aw881xx = NULL;

	if (((_IOC_TYPE(cmd)) != (AW_IOCTL_MAGIC))) {
		aw_pr_err("cmd magic err");
		return -EINVAL;
	}
	aw881xx = (struct aw881xx *)file->private_data;
	ret = aw_cali_misc_ops(aw881xx, cmd, arg);
	if (ret < 0)
		return -EINVAL;


	return 0;
}
#endif

static ssize_t aw_cali_misc_read(struct file *filp, char __user *buf, size_t size, loff_t *pos)
{
	int ret = -EINVAL;
	int i = 0;
	int len = 0;
	struct aw881xx *aw881xx = (struct aw881xx *)filp->private_data;
	char local_buf[512] = { 0 };
	uint32_t temp_data[AW_DEV_CH_MAX] = { 0 };
	uint32_t re_value[AW_DEV_RE_RANGE] = { 0 };

	aw_dev_info(aw881xx->dev, "enter");

	if (*pos) {
		*pos = 0;
		return 0;
	}

	switch (g_msic_wr_flag) {
	case CALI_STR_SHOW_RE: {
		ret = aw_cali_svc_get_devs_cali_val(aw881xx, GET_RE_TYPE, temp_data, AW_DEV_CH_MAX);
		if (ret <= 0) {
			aw_dev_err(aw881xx->dev, "get re failed");
			return -EINVAL;
		} else {
			for (i = 0; i < ret; i++)
				len += snprintf(local_buf+len, PAGE_SIZE-len, "dev[%d]:%u ", i, temp_data[i]);

			len += snprintf(local_buf+len, PAGE_SIZE-len, "\n");
		}
	} break;
	case CALI_STR_SHOW_R0: {
		ret = aw_cali_svc_get_devs_r0(aw881xx, temp_data, AW_DEV_CH_MAX);
		if (ret <= 0) {
			aw_dev_err(aw881xx->dev, "get r0 failed");
			return -EINVAL;
		} else {
			for (i = 0; i < ret; i++)
				len += snprintf(local_buf+len, sizeof(local_buf)-len,
							"dev[%d]:%d ", i, temp_data[i]);
			len += snprintf(local_buf+len, sizeof(local_buf)-len, "\n");
		}
	} break;
	case CALI_STR_SHOW_CALI_F0: {
		ret = aw_cali_svc_get_devs_cali_val(aw881xx, GET_F0_TYPE, temp_data, AW_DEV_CH_MAX);
		if (ret <= 0) {
			aw_dev_err(aw881xx->dev, "get cali f0 failed");
			return -EINVAL;
		} else {
			for (i = 0; i < ret; i++)
				len += snprintf(local_buf+len, sizeof(local_buf)-len,
							"dev[%d]:%d ", i, temp_data[i]);

			len += snprintf(local_buf+len, sizeof(local_buf)-len, "\n");
		}
	}break;
	case CALI_STR_SHOW_F0: {
		ret = aw_cali_svc_get_devs_f0(aw881xx, temp_data, AW_DEV_CH_MAX);
		if (ret <= 0) {
			aw_dev_err(aw881xx->dev, "get f0 failed");
			return -EINVAL;
		} else {
			for (i = 0; i < ret; i++)
				len += snprintf(local_buf+len, sizeof(local_buf)-len,
							"dev[%d]:%d ", i, temp_data[i]);

			len += snprintf(local_buf+len, sizeof(local_buf) - len, "\n");
		}
	}break;
	case CALI_STR_SHOW_TE: {
		ret = aw_cali_svc_get_devs_te(aw881xx, temp_data, AW_DEV_CH_MAX);
		if (ret <= 0) {
			aw_dev_err(aw881xx->dev, "get te failed");
			return -EINVAL;
		} else {
			for (i = 0; i < ret; i++)
				len += snprintf(local_buf+len, sizeof(local_buf)-len,
							"dev[%d]:%d ", i, temp_data[i]);
			len += snprintf(local_buf+len, sizeof(local_buf)-len, "\n");
		}
	} break;
	case CALI_STR_VER: {
		len = aw881xx_get_version(local_buf, sizeof(local_buf));
		if (len < 0) {
			aw_dev_err(aw881xx->dev, "get version failed");
			return -EINVAL;
		}
		len += snprintf(local_buf+len, sizeof(local_buf) - len, "\n");
	} break;
	case CALI_STR_SHOW_RE_RANGE: {
		ret = aw_cali_svc_get_devs_re_range(aw881xx, re_value, AW_DEV_CH_MAX);
		if (ret <= 0) {
			aw_dev_err(aw881xx->dev, "get re range failed");
			return -EINVAL;
		} else {
			for (i = 0; i < ret; i++) {
				len += snprintf(local_buf+len, sizeof(local_buf)-len,
					"dev[%d]:re_min:%d re_max:%d\n",
					i, re_value[RE_MIN_FLAG + i * RE_RANGE_NUM],
					re_value[RE_MAX_FLAG + i * RE_RANGE_NUM]);
			}
		}
	} break;
	default: {
		if (g_msic_wr_flag == CALI_STR_NONE) {
			aw_dev_info(aw881xx->dev, "please write cmd first");
			return -EINVAL;
		} else {
			aw_dev_err(aw881xx->dev, "unsupported flag [%d]", g_msic_wr_flag);
			g_msic_wr_flag = CALI_STR_NONE;
			return -EINVAL;
		}
	} break;
	}

	if (copy_to_user((void __user *)buf, local_buf, len)) {
		aw_dev_err(aw881xx->dev, "copy_to_user error");
		g_msic_wr_flag = CALI_STR_NONE;
		return -EFAULT;
	}

	g_msic_wr_flag = CALI_STR_NONE;
	*pos += len;
	return len;

}

static int aw_cali_svc_get_cmd_form_str(struct aw881xx *aw881xx, const char *buf)
{
	int i;

	for (i = 0; i < CALI_STR_MAX; i++) {
		if (!strncmp(cali_str[i], buf, strlen(cali_str[i]))) {
			break;
		}
	}

	if (i == CALI_STR_MAX) {
		aw_dev_err(aw881xx->dev, "supported cmd [%s]!", buf);
		return -EINVAL;
	}

	aw_dev_dbg(aw881xx->dev, "find str [%s]", cali_str[i]);
	return i;
}

static int aw_cali_svc_cali_cmd(struct aw881xx *aw881xx, int cali_cmd, bool is_single, unsigned int flag)
{
	switch (cali_cmd) {
		case AW_CALI_CMD_RE : {
			return aw_cali_svc_cali_re(aw881xx, is_single, flag);
		} break;
		case AW_CALI_CMD_F0 :
		case AW_CALI_CMD_F0_Q: {
			return aw_cali_svc_cali_f0_q(aw881xx, is_single, flag);
		} break;
		case AW_CALI_CMD_RE_F0:
		case AW_CALI_CMD_RE_F0_Q: {
			return aw_cali_svc_cali_re_f0_q(aw881xx, is_single, flag);
		}
		default : {
			aw_dev_err(aw881xx->dev, "unsupported cmd %d", cali_cmd);
			return -EINVAL;
		}
	}
	return 0;
}

static int aw_cali_misc_switch_dev(struct file *filp, struct aw881xx *aw881xx, char *cmd_buf)
{
	int ret = -EINVAL;
	int dev_select_num;
	struct list_head *dev_list = NULL;
	struct list_head *pos = NULL;
	struct aw881xx *local_dev = NULL;

	if (cmd_buf == NULL) {
		aw_dev_err(aw881xx->dev, "cmd_buf is NULL");
		return -EINVAL;
	}

	/*get sel dev str*/
	sscanf(cmd_buf, "dev_sel:dev[%d]", &dev_select_num);

	if (dev_select_num >= AW_DEV_CH_MAX) {
		aw_dev_err(aw881xx->dev, "unsupport str [%s]", cmd_buf);
		return -EINVAL;
	}

	/*get dev list*/
	ret = aw881xx_get_list_head(&dev_list);
	if (ret) {
		aw_dev_err(aw881xx->dev, "get dev list failed");
		return ret;
	}

	/*find sel dev*/
	list_for_each (pos, dev_list) {
		local_dev = container_of(pos, struct aw881xx, list_node);
		if (local_dev->channel == dev_select_num) {
			filp->private_data = (void *)local_dev;
			g_dev_select = dev_select_num;
			aw_dev_info(local_dev->dev, "switch to dev[%d]", dev_select_num);
			return 0;
		}
	}
	aw_dev_err(aw881xx->dev, " unsupport [%s]", cmd_buf);
	return -EINVAL;
}

static ssize_t aw_cali_misc_write(struct file *filp, const char __user *buf, size_t size, loff_t *pos)
{
	int ret = -EINVAL;
	char *kernel_buf = NULL;
	struct aw881xx *aw881xx = (struct aw881xx *)filp->private_data;

	aw_dev_info(aw881xx->dev, "enter, write size:%d", (int)size);
	kernel_buf = kzalloc(size + 1, GFP_KERNEL);
	if (kernel_buf == NULL) {
		aw_dev_err(aw881xx->dev, "kzalloc failed !");
		return -ENOMEM;
	}

	if (copy_from_user(kernel_buf,
			(void __user *)buf,
			size)) {
		ret = -EFAULT;
		goto exit;
	}

	ret = aw_cali_svc_get_cmd_form_str(aw881xx, kernel_buf);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "upported cmd [%s]!", kernel_buf);
		ret = -EINVAL;
		goto exit;
	}

	switch (ret) {
	case CALI_STR_CALI_RE_F0: {
		ret = aw_cali_svc_cali_cmd(aw881xx, AW_CALI_CMD_RE_F0,
					is_single_cali, CALI_OPS_HMUTE|CALI_OPS_NOISE);
	} break;
	case CALI_STR_CALI_RE: {
		ret = aw_cali_svc_cali_cmd(aw881xx, AW_CALI_CMD_RE,
					is_single_cali, CALI_OPS_HMUTE);
	} break;
	case CALI_STR_CALI_F0: {
		ret = aw_cali_svc_cali_cmd(aw881xx, AW_CALI_CMD_F0,
					is_single_cali, CALI_OPS_HMUTE|CALI_OPS_NOISE);
	} break;
	case CALI_STR_SET_RE: {
		ret = aw_cali_svc_set_devs_re_str(aw881xx,
				kernel_buf + strlen(cali_str[CALI_STR_SET_RE]) + 1);
	} break;
	case CALI_STR_DEV_SEL: {
		ret = aw_cali_misc_switch_dev(filp, aw881xx, kernel_buf);
	} break;
	case CALI_STR_SHOW_RE:			/*show cali_re*/
	case CALI_STR_SHOW_R0:			/*show real r0*/
	case CALI_STR_SHOW_CALI_F0:		/*GET DEV CALI_F0*/
	case CALI_STR_SHOW_F0:			/*SHOW REAL F0*/
	case CALI_STR_SHOW_TE:
	case CALI_STR_VER:
	case CALI_STR_SHOW_RE_RANGE: {
		g_msic_wr_flag = ret;
		ret = 0;
	} break;
	default: {
		aw_dev_err(aw881xx->dev, "unsupported [%s]!", kernel_buf);
		ret = -EINVAL;
	} break;
	};

exit:
	aw_dev_dbg(aw881xx->dev, "cmd [%s]!", kernel_buf);
	if (kernel_buf) {
		kfree(kernel_buf);
		kernel_buf = NULL;
	}
	if (ret < 0)
		return -EINVAL;
	else
		return size;
}

static const struct file_operations aw_cali_misc_fops = {
	.owner = THIS_MODULE,
	.open = aw_cali_misc_open,
	.read = aw_cali_misc_read,
	.write = aw_cali_misc_write,
	.release = aw_cali_misc_release,
	.unlocked_ioctl = aw_cali_misc_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = aw_cali_misc_compat_ioctl,
#endif
};

struct miscdevice misc_cali = {
	.name = "aw_smartpa",
	.minor = MISC_DYNAMIC_MINOR,
	.fops  = &aw_cali_misc_fops,
};

static int aw_cali_misc_init(struct aw881xx *aw881xx)
{
	int ret = -EINVAL;

	mutex_lock(&g_cali_lock);
	if (g_misc_dev == NULL) {
		ret = misc_register(&misc_cali);
		if (ret) {
			aw_dev_err(aw881xx->dev, "misc register fail: %d\n", ret);
			mutex_unlock(&g_cali_lock);
			return -EINVAL;
		}
		g_misc_dev = &misc_cali;
		aw_dev_dbg(aw881xx->dev, "misc register success");
	} else {
		aw_dev_dbg(aw881xx->dev, "misc already register");
	}
	mutex_unlock(&g_cali_lock);

	return 0;
}

static void aw_cali_misc_deinit(struct aw881xx *aw881xx)
{
	mutex_lock(&g_cali_lock);
	if (g_misc_dev) {
		misc_deregister(g_misc_dev);
		g_misc_dev = NULL;
	}
	mutex_unlock(&g_cali_lock);
	aw_dev_dbg(aw881xx->dev, " misc unregister done");
}

static void aw881xx_parse_re_range_dt(struct aw881xx *aw881xx)
{
	int ret = -EINVAL;
	uint32_t re_max;
	uint32_t re_min;
	struct device_node *np = aw881xx->dev->of_node;

	ret = of_property_read_u32(np, "re-min", &re_min);
	if (ret < 0) {
		aw881xx->re_range.re_min = RE_MIN_DEFAULT;
		aw_dev_info(aw881xx->dev,
			"%s: read re-min value failed, set deafult value:[%d]mohm\n",
			__func__, aw881xx->re_range.re_min);
	} else {
		aw_dev_info(aw881xx->dev,
			"%s: parse re-min:[%d]\n", __func__, re_min);
		aw881xx->re_range.re_min = re_min;
	}

	ret = of_property_read_u32(np, "re-max", &re_max);
	if (ret < 0) {
		aw881xx->re_range.re_max = RE_MAX_DEFAULT;
		aw_dev_info(aw881xx->dev,
			"%s: read re-max failed, set deafult value:[%d]mohm\n",
			__func__, aw881xx->re_range.re_max);
	} else {
		aw_dev_info(aw881xx->dev,
			"%s: parse re-max:[%d]\n", __func__, re_max);
		aw881xx->re_range.re_max = re_max;
	}
}

static void aw881xx_cali_parse_dt(struct aw881xx *aw881xx)
{
	int ret = -EINVAL;
	struct device_node *np = aw881xx->dev->of_node;
	uint32_t cali_check = CALI_CHECK_DISABLE;
	struct aw_cali_desc *desc = &aw881xx->cali_desc;

	ret = of_property_read_u32(np, "aw-cali-check", &cali_check);
	if (ret < 0) {
		aw_dev_info(aw881xx->dev, " cali-check get failed ,default turn off");
		cali_check = CALI_CHECK_DISABLE;
	}

	desc->cali_check_st = cali_check;
	aw_dev_info(aw881xx->dev, "%s: cali_check_st:%s\n",
		__func__, (desc->cali_check_st) ? "enable" : "disable");
}

void aw881xx_cali_init(struct aw_cali_desc *cali_desc)
{
	struct aw881xx *aw881xx =
		container_of(cali_desc, struct aw881xx, cali_desc);
	memset(cali_desc, 0, sizeof(struct aw_cali_desc));

	aw881xx_cali_parse_dt(aw881xx);
	aw881xx_parse_re_range_dt(aw881xx);

	aw_cali_attr_init(aw881xx);
	aw_cali_class_attr_init(aw881xx);
	aw_cali_misc_init(aw881xx);

	aw881xx_attr_init(aw881xx);

	cali_desc->cali_result = CALI_RESULT_NONE;
}

void aw881xx_cali_deinit(struct aw_cali_desc *cali_desc)
{
	struct aw881xx *aw881xx =
		container_of(cali_desc, struct aw881xx, cali_desc);

	aw_cali_attr_deinit(aw881xx);
	aw_cali_class_attr_deinit(aw881xx);
	aw_cali_misc_deinit(aw881xx);
	aw881xx_attr_deinit(aw881xx);
}

