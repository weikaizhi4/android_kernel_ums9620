/*
 * leds-aw210xx.c
 *
 * Copyright (c) 2021 AWINIC Technology CO., LTD
 *
 *  Author: hushanping <hushanping@awinic.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
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
#include <linux/uaccess.h>
#include <linux/leds.h>
#include "leds_aw210xx.h"
#include "leds_aw210xx_reg.h"
#include <vendor/common/zte_misc.h> /* add zte boardtest interface */

#ifdef CONFIG_VENDOR_SOC_MTK_COMPILE
#include <mt-plat/mtk_boot_common.h>
extern int battery_get_boot_mode(void);
#endif

#ifdef CONFIG_VENDOR_SOC_SPRD_COMPILE
#include <linux/of.h>

static bool is_charger_mode = false;

static int get_boot_mode(void)
{
	struct device_node *cmdline_node;
	const char *cmd_line;
	int ret;

	cmdline_node = of_find_node_by_path("/chosen");
	ret = of_property_read_string(cmdline_node, "bootargs", &cmd_line);
	if (ret)
		return ret;

	if (strstr(cmd_line, "androidboot.mode=charger"))
		is_charger_mode = true;

	return 0;
}
#endif

#define AW210XX_DRIVER_VERSION "V0.3.0"
#define AW_I2C_RETRIES 5
#define AW_I2C_RETRY_DELAY 1
#define AW_READ_CHIPID_RETRIES 2
#define AW_READ_CHIPID_RETRY_DELAY 1

aw210xx_cfg_t aw210xx_cfg_array[] = {
	{aw210xx_group_cfg_led_off, sizeof(aw210xx_group_cfg_led_off)},
	{aw21009_group_led_on, sizeof(aw21009_group_led_on)},
	{aw21009_long_breath_red_on, sizeof(aw21009_long_breath_red_on)},
	{aw21009_breath_led_on, sizeof(aw21009_breath_led_on)},
	{aw21009_dance_colors_on, sizeof(aw21009_dance_colors_on)},
	{aw21009_power_on_effect, sizeof(aw21009_power_on_effect)}
};

/******************************************************
 *
 * aw210xx i2c write/read
 *
 ******************************************************/
static int aw210xx_i2c_write(struct aw210xx *aw210xx,
		unsigned char reg_addr, unsigned char reg_data)
{
	int ret = -1;
	unsigned char cnt = 0;

	while (cnt < AW_I2C_RETRIES) {
		ret = i2c_smbus_write_byte_data(aw210xx->i2c,
				reg_addr, reg_data);
		if (ret < 0)
			AW_ERR("i2c_write cnt=%d ret=%d\n", cnt, ret);
		else
			break;
		cnt++;
		usleep_range(AW_I2C_RETRY_DELAY * 1000,
				AW_I2C_RETRY_DELAY * 1000 + 500);
	}

	return ret;
}

static int aw210xx_i2c_read(struct aw210xx *aw210xx,
		unsigned char reg_addr, unsigned char *reg_data)
{
	int ret = -1;
	unsigned char cnt = 0;

	while (cnt < AW_I2C_RETRIES) {
		ret = i2c_smbus_read_byte_data(aw210xx->i2c, reg_addr);
		if (ret < 0) {
			AW_ERR("i2c_read cnt=%d ret=%d\n", cnt, ret);
		} else {
			*reg_data = ret;
			break;
		}
		cnt++;
		usleep_range(AW_I2C_RETRY_DELAY * 1000,
				AW_I2C_RETRY_DELAY * 1000 + 500);
	}

	return ret;
}

static int aw210xx_i2c_write_bits(struct aw210xx *aw210xx,
		unsigned char reg_addr, unsigned int mask,
		unsigned char reg_data)
{
	unsigned char reg_val;

	aw210xx_i2c_read(aw210xx, reg_addr, &reg_val);
	reg_val &= mask;
	reg_val |= reg_data;
	aw210xx_i2c_write(aw210xx, reg_addr, reg_val);

	return 0;
}

static void aw210xx_update_cfg_array(struct aw210xx *aw210xx,
		uint8_t *p_cfg_data, uint32_t cfg_size)
{
	unsigned int i = 0;

	for (i = 0; i < cfg_size; i += 2)
		aw210xx_i2c_write(aw210xx, p_cfg_data[i], p_cfg_data[i + 1]);
}

void aw210xx_cfg_update(struct aw210xx *aw210xx)
{
	AW_LOG("aw210xx->effect = %d", aw210xx->effect);

	aw210xx_update_cfg_array(aw210xx,
			aw210xx_cfg_array[aw210xx->effect].p,
			aw210xx_cfg_array[aw210xx->effect].count);
}

void aw210xx_uvlo_set(struct aw210xx *aw210xx, bool flag)
{
	if (flag) {
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_UVCR,
				AW210XX_BIT_UVPD_MASK,
				AW210XX_BIT_UVPD_DISENA);
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_UVCR,
				AW210XX_BIT_UVDIS_MASK,
				AW210XX_BIT_UVDIS_DISENA);
	} else {
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_UVCR,
				AW210XX_BIT_UVPD_MASK,
				AW210XX_BIT_UVPD_ENABLE);
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_UVCR,
				AW210XX_BIT_UVDIS_MASK,
				AW210XX_BIT_UVDIS_ENABLE);
	}
}

void aw210xx_gcr_set(struct aw210xx *aw210xx)
{
	unsigned char tx_buf[2] = {0};

	tx_buf[0] = AW210XX_REG_GCR;
	tx_buf[1] = AW210XX_BIT_CHIPEN_ENABLE  // chip enable
			| AW210XX_BIT_APSE_ENABLE;  // apse enable

	switch (aw210xx->osc_clk) {  // clk_pwm select
	case CLK_FRQ_16M:
		tx_buf[1] |= AW210XX_BIT_CLKFRQ_16MHz;
		break;
	case CLK_FRQ_8M:
		tx_buf[1] |= AW210XX_BIT_CLKFRQ_8MHz;
		break;
	case CLK_FRQ_1M:
		tx_buf[1] |= AW210XX_BIT_CLKFRQ_1MHz;
		break;
	case CLK_FRQ_512k:
		tx_buf[1] |= AW210XX_BIT_CLKFRQ_512kHz;
		break;
	case CLK_FRQ_256k:
		tx_buf[1] |= AW210XX_BIT_CLKFRQ_256kHz;
		break;
	case CLK_FRQ_125K:
		tx_buf[1] |= AW210XX_BIT_CLKFRQ_125kHz;
		break;
	case CLK_FRQ_62_5K:
		tx_buf[1] |= AW210XX_BIT_CLKFRQ_62_5kHz;
		break;
	case CLK_FRQ_31_25K:
		tx_buf[1] |= AW210XX_BIT_CLKFRQ_31_25kHz;
		break;
	default:
		AW_LOG("this clk_pwm is unsupported! Set osc to default 16MHz!\n");
		tx_buf[1] |= AW210XX_BIT_CLKFRQ_16MHz;
		break;
	}

	switch (aw210xx->br_res) {  // br_res select
	case BR_RESOLUTION_8BIT:
		tx_buf[1] |= AW210XX_BIT_PWMRES_8BIT;
		break;
	case BR_RESOLUTION_9BIT:
		tx_buf[1] |= AW210XX_BIT_PWMRES_9BIT;
		break;
	case BR_RESOLUTION_12BIT:
		tx_buf[1] |= AW210XX_BIT_PWMRES_12BIT;
		break;
	case BR_RESOLUTION_9_AND_3_BIT:
		tx_buf[1] |= AW210XX_BIT_PWMRES_9_AND_3_BIT;
		break;
	default:
		AW_LOG("this br_res is unsupported! Set br resolution to default 8bit!\n");
		tx_buf[1] |= AW210XX_BIT_PWMRES_8BIT;
		break;
	}

	// AW_LOG("set GCR[0x%x] to 0x%x.\n", tx_buf[0], tx_buf[1]);
	aw210xx_i2c_write(aw210xx, tx_buf[0], tx_buf[1]);
}

void aw210xx_gcr2_set(struct aw210xx *aw210xx)
{
	unsigned char tx_buf[2] = {0};

	tx_buf[0] = AW210XX_REG_GCR2;
	tx_buf[1] = AW210XX_BIT_SBMD_ENABLE  // sbmd enable
			| AW210XX_BIT_RGBMD_ENABLE;  // rgbmd enable

	// AW_LOG("set GCR2[0x%x] to 0x%x.\n", tx_buf[0], tx_buf[1]);
	aw210xx_i2c_write(aw210xx, tx_buf[0], tx_buf[1]);
}

static int32_t aw210xx_group_gcfg_set(struct aw210xx *aw210xx, bool flag)
{
	if (flag) {
		aw210xx_i2c_write(aw210xx, AW210XX_REG_GCFG,
				AW21009_GROUP_ENABLE);
	} else {
		aw210xx_i2c_write(aw210xx, AW210XX_REG_GCFG,
				AW21009_GROUP_DISABLE);
	}
	return 0;
}

static void aw210xx_update(struct aw210xx *aw210xx)
{
	aw210xx_i2c_write(aw210xx, AW210XX_REG_UPDATE, AW210XX_UPDATE_BR_SL);
}

void aw210xx_global_set(struct aw210xx *aw210xx)
{
	aw210xx_i2c_write(aw210xx,
			AW210XX_REG_GCCR, aw210xx->glo_current);
}

static int aw210xx_led_init(struct aw210xx *aw210xx)
{
	aw210xx_gcr_set(aw210xx);
	aw210xx_gcr2_set(aw210xx);
	aw210xx_global_set(aw210xx);
	aw210xx_uvlo_set(aw210xx, true);
	return 0;
}

static uint32_t aw210xx_get_pat_time_cfg(int16_t ms)
{
	int32_t i = 0;
	int32_t len = sizeof(pat_time_cfg_group) / sizeof(struct pat_time);

	for (i = 0; i < len; i++) {
		if (pat_time_cfg_group[i].ms >= ms)
			return pat_time_cfg_group[i].reg_val;
	}

	return -1;
}

static int32_t aw210xx_patcfg_set(struct aw210xx *aw210xx,
								bool flag, uint8_t times)
{
	uint8_t t0_cfg, t1_cfg, t2_cfg, t3_cfg;

	t0_cfg = aw210xx_get_pat_time_cfg(aw210xx->pat_time_cfg.t0);
	t1_cfg = aw210xx_get_pat_time_cfg(aw210xx->pat_time_cfg.t1);
	t2_cfg = aw210xx_get_pat_time_cfg(aw210xx->pat_time_cfg.t2);
	t3_cfg = aw210xx_get_pat_time_cfg(aw210xx->pat_time_cfg.t3);

	if (flag == 0) {
		AW_LOG("pat off.\n");
		aw210xx_i2c_write(aw210xx, AW210XX_REG_PATT0, 0x00);
		aw210xx_i2c_write(aw210xx, AW210XX_REG_PATT1, 0x00);
		aw210xx_i2c_write(aw210xx, AW210XX_REG_PATT2, 0x00);
		aw210xx_i2c_write(aw210xx, AW210XX_REG_PATT3, 0x00);
		aw210xx_i2c_write(aw210xx, AW210XX_REG_PATGO, 0x00);
		aw210xx_i2c_write(aw210xx, AW210XX_REG_PATCFG, AW210XX_BIT_PATE_DISENA);
		return 0;
	}

	/* flag = 1 *
	 * breath & flash */
	if (aw210xx->dance_state == WORK_STOP) {
		AW_LOG("pat on. times = %d pat_time_cfg = [%d %d %d %d]\n",
				times, aw210xx->pat_time_cfg.t0, aw210xx->pat_time_cfg.t1,
				aw210xx->pat_time_cfg.t2, aw210xx->pat_time_cfg.t3);
	}
	aw210xx_i2c_write(aw210xx, AW210XX_REG_PATT0,
		((t0_cfg << 4) | t1_cfg));
	aw210xx_i2c_write(aw210xx, AW210XX_REG_PATT1,
		((t2_cfg << 4) | t3_cfg));

	aw210xx_i2c_write(aw210xx, AW210XX_REG_PATT2, AW210XX_PATT2_SET);
	aw210xx_i2c_write(aw210xx, AW210XX_REG_PATT3, times);
	aw210xx_i2c_write(aw210xx, AW210XX_REG_PATCFG, AW210XX_PATCFG_SET);
	return 0;
}

void aw210xx_light_on(struct aw210xx *aw210xx, uint32_t rgb_data, uint8_t brightness)
{
	uint32_t max_rgb = 0, sum_rgb = 0;

	/* chip intialize */
	aw210xx_led_init(aw210xx);

	if ((rgb_data == 0) || (brightness == 0)) {
		/* set sl 0 */
		aw210xx_i2c_write(aw210xx, AW210XX_REG_GSLR, 0);
		aw210xx_i2c_write(aw210xx, AW210XX_REG_GSLG, 0);
		aw210xx_i2c_write(aw210xx, AW210XX_REG_GSLB, 0);
		/* set br 0 */
		aw210xx_i2c_write(aw210xx, AW210XX_REG_GBRL, 0);
		aw210xx_i2c_write(aw210xx, AW210XX_REG_GBRH, 0);
	} else {
		aw210xx->red = (rgb_data & 0xff0000) >> 16;
		aw210xx->green = (rgb_data & 0x00ff00) >> 8;
		aw210xx->blue = (rgb_data & 0x0000ff);

		/* AW21009_MAX_CURRENT check
		 * max_current determined by global_current */
		max_rgb = 255 * 255 / brightness;
		sum_rgb = aw210xx->red + aw210xx->green + aw210xx->blue;
		if (sum_rgb > max_rgb) {
			aw210xx->red = aw210xx->red * max_rgb / sum_rgb;
			aw210xx->green = aw210xx->green * max_rgb / sum_rgb;
			aw210xx->blue = aw210xx->blue * max_rgb / sum_rgb;
		}

		AW_LOG("color = [%d %d %d] brightness = %d\n",
				aw210xx->red, aw210xx->green, aw210xx->blue, brightness);

		/* set sl */
		aw210xx_i2c_write(aw210xx, AW210XX_REG_GSLR, aw210xx->red);
		aw210xx_i2c_write(aw210xx, AW210XX_REG_GSLG, aw210xx->green);
		aw210xx_i2c_write(aw210xx, AW210XX_REG_GSLB, aw210xx->blue);
		/* set br */
		aw210xx_i2c_write(aw210xx, AW210XX_REG_GBRL, brightness);
		aw210xx_i2c_write(aw210xx, AW210XX_REG_GBRH, AW210XX_GBRH_DEFAULT_SET);
	}

	aw210xx_group_gcfg_set(aw210xx, true);
	aw210xx_patcfg_set(aw210xx, false, 0);

	/* update */
	aw210xx_update(aw210xx);
}

/*****************************************************
 *
 * aw210xx led cfg
 *
 *****************************************************/
static void aw210xx_brightness_work(struct work_struct *work)
{
	struct aw210xx *aw210xx = container_of(work, struct aw210xx,
			brightness_work[LED_CDEV]);
	AW_LOG("enter\n");

	if (aw210xx->cdev[LED_CDEV].brightness > aw210xx->cdev[LED_CDEV].max_brightness)
		aw210xx->cdev[LED_CDEV].brightness = aw210xx->cdev[LED_CDEV].max_brightness;

	aw210xx->brightness = aw210xx->cdev[LED_CDEV].brightness;
}

static void aw210xx_set_brightness(struct led_classdev *cdev,
		enum led_brightness brightness)
{
	struct aw210xx *aw210xx = container_of(cdev, struct aw210xx, cdev[LED_CDEV]);
	aw210xx->cdev[LED_CDEV].brightness = brightness;
	schedule_work(&aw210xx->brightness_work[LED_CDEV]);
}

static void aw210xx_red_brightness_work(struct work_struct *work)
{
	struct aw210xx *aw210xx = container_of(work, struct aw210xx,
			brightness_work[RED_CDEV]);
	AW_LOG("enter\n");

	if (aw210xx->cdev[RED_CDEV].brightness > aw210xx->cdev[RED_CDEV].max_brightness)
		aw210xx->cdev[RED_CDEV].brightness = aw210xx->cdev[RED_CDEV].max_brightness;

	aw210xx_light_on(aw210xx, COLOR_PURE_RED, aw210xx->cdev[RED_CDEV].brightness);
}

static void aw210xx_set_red_brightness(struct led_classdev *cdev,
		enum led_brightness brightness)
{
	struct aw210xx *aw210xx = container_of(cdev, struct aw210xx, cdev[RED_CDEV]);
	aw210xx->cdev[RED_CDEV].brightness = brightness;
	schedule_work(&aw210xx->brightness_work[RED_CDEV]);
}

static void aw210xx_green_brightness_work(struct work_struct *work)
{
	struct aw210xx *aw210xx = container_of(work, struct aw210xx,
			brightness_work[GREEN_CDEV]);
	AW_LOG("enter\n");

	if (aw210xx->cdev[GREEN_CDEV].brightness > aw210xx->cdev[GREEN_CDEV].max_brightness)
		aw210xx->cdev[GREEN_CDEV].brightness = aw210xx->cdev[GREEN_CDEV].max_brightness;

	aw210xx_light_on(aw210xx, COLOR_PURE_GREEN, aw210xx->cdev[GREEN_CDEV].brightness);
}

static void aw210xx_set_green_brightness(struct led_classdev *cdev,
		enum led_brightness brightness)
{
	struct aw210xx *aw210xx = container_of(cdev, struct aw210xx, cdev[GREEN_CDEV]);
	aw210xx->cdev[GREEN_CDEV].brightness = brightness;
	schedule_work(&aw210xx->brightness_work[GREEN_CDEV]);
}

static void aw210xx_blue_brightness_work(struct work_struct *work)
{
	struct aw210xx *aw210xx = container_of(work, struct aw210xx,
			brightness_work[BLUE_CDEV]);
	AW_LOG("enter\n");

	if (aw210xx->cdev[BLUE_CDEV].brightness > aw210xx->cdev[BLUE_CDEV].max_brightness)
		aw210xx->cdev[BLUE_CDEV].brightness = aw210xx->cdev[BLUE_CDEV].max_brightness;

	aw210xx_light_on(aw210xx, COLOR_PURE_BLUE, aw210xx->cdev[BLUE_CDEV].brightness);
}

static void aw210xx_set_blue_brightness(struct led_classdev *cdev,
		enum led_brightness brightness)
{
	struct aw210xx *aw210xx = container_of(cdev, struct aw210xx, cdev[BLUE_CDEV]);
	aw210xx->cdev[BLUE_CDEV].brightness = brightness;
	schedule_work(&aw210xx->brightness_work[BLUE_CDEV]);
}

static int aw210xx_hw_enable(struct aw210xx *aw210xx, bool flag)
{
	AW_LOG("enter\n");

	if (aw210xx && gpio_is_valid(aw210xx->enable_gpio)) {
		if (flag) {
			gpio_set_value_cansleep(aw210xx->enable_gpio, 1);
			usleep_range(2000, 2500);
		} else {
			gpio_set_value_cansleep(aw210xx->enable_gpio, 0);
		}
	} else {
		AW_ERR("failed\n");
	}

	return 0;
}

static void effect_work_func(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct aw210xx *aw210xx = container_of(dwork, struct aw210xx, effect_work);
	uint8_t buf = 0, i = 0, j = 0;
	int time_max = 0;

	aw210xx_led_init(aw210xx);

	/* set sl */
	aw210xx_i2c_write(aw210xx, AW210XX_REG_GSLR, aw210xx->red);
	aw210xx_i2c_write(aw210xx, AW210XX_REG_GSLG, aw210xx->green);
	aw210xx_i2c_write(aw210xx, AW210XX_REG_GSLB, aw210xx->blue);
	/* br set */
	aw210xx_i2c_write(aw210xx, AW210XX_REG_GBRH, aw210xx->brightness);
	aw210xx_i2c_write(aw210xx, AW210XX_REG_GBRL, 0);
	aw210xx_i2c_write(aw210xx, AW210XX_REG_PATGO, 0);
	aw210xx_group_gcfg_set(aw210xx, true);  /* group config */

	while ((aw210xx->effect_work_state == WORK_RUNNING) && (j < aw210xx->flash_times)) {
		for (i = 0; i < 2; i++) {
			aw210xx->pat_time_cfg.t0 = 700;
			aw210xx->pat_time_cfg.t1 = 200;
			aw210xx->pat_time_cfg.t2 = 2000;
			aw210xx->pat_time_cfg.t3 = 1000;
			aw210xx_patcfg_set(aw210xx, true, 1);  /* pat config */
			time_max = (aw210xx->pat_time_cfg.t0 + aw210xx->pat_time_cfg.t1 +
					aw210xx->pat_time_cfg.t2 + aw210xx->pat_time_cfg.t3) * 1 / 100 + 5;
			aw210xx_i2c_write(aw210xx, AW210XX_REG_PATGO, AW210XX_PATGO_SET);  /* PAT run */
			/* wait current parrent over */
			do {
				if (aw210xx->effect_work_state != WORK_RUNNING)
					break;
				mdelay(100);
				aw210xx_i2c_read(aw210xx, AW210XX_REG_PATGO, &buf);
				time_max--;
				//AW_LOG("Reg[0x%x] read 0x%x.\n", AW210XX_REG_PATGO, buf);
			} while(((buf & 0x4) != 0x4) && (time_max));
			AW_LOG("patcfg done! time_max = %d\n", time_max);
			aw210xx_i2c_write(aw210xx, AW210XX_REG_PATGO, 0);  /* set PAT run enable 0 for next pattern*/

			if (aw210xx->effect_work_state != WORK_RUNNING)
				break;

			if (i > 0)
				continue;

			aw210xx->pat_time_cfg.t0 = 0;
			aw210xx->pat_time_cfg.t1 = 100;
			aw210xx->pat_time_cfg.t2 = 0;
			aw210xx->pat_time_cfg.t3 = 100;
			aw210xx_patcfg_set(aw210xx, true, 2);  /* pat config */
			time_max = (aw210xx->pat_time_cfg.t0 + aw210xx->pat_time_cfg.t1 +
					aw210xx->pat_time_cfg.t2 + aw210xx->pat_time_cfg.t3) * 2 / 100 + 5;
			aw210xx_i2c_write(aw210xx, AW210XX_REG_PATGO, AW210XX_PATGO_SET);  /* PAT run */
			/* wait current parrent over */
			do {
				if (aw210xx->effect_work_state != WORK_RUNNING)
					break;
				mdelay(100);
				aw210xx_i2c_read(aw210xx, AW210XX_REG_PATGO, &buf);
				time_max--;
				//AW_LOG("Reg[0x%x] read 0x%x.\n", AW210XX_REG_PATGO, buf);
			} while(((buf & 0x4) != 0x4) && (time_max));
			AW_LOG("patcfg2 done! time_max = %d\n", time_max);
			aw210xx_i2c_write(aw210xx, AW210XX_REG_PATGO, 0);  /* set PAT run enable 0 for next pattern*/
			if (aw210xx->effect_work_state != WORK_RUNNING)
				break;
		}
		j++;
	}
	aw210xx->effect_work_state = WORK_STOP;
	AW_LOG("effect_work done\n");
}

static void dance_work_func(struct work_struct *work)
{
	struct aw210xx *aw210xx = container_of(work, struct aw210xx, dance_work);
	uint8_t buf = 0, i = 0;
	int time_max = 0;

	AW_LOG("enter. dance color = [%d %d %d] pat_time_cfg = [%d %d %d %d] brightness = %d flash_times = %d\n",
			aw210xx->red, aw210xx->green, aw210xx->blue,
			aw210xx->pat_time_cfg.t0, aw210xx->pat_time_cfg.t1,
			aw210xx->pat_time_cfg.t2, aw210xx->pat_time_cfg.t3,
			aw210xx->brightness, aw210xx->flash_times);

	if (aw210xx->brightness == 0) {
		AW_LOG("param invalid! brightness 0! EXIT!");
		return;
	}

	aw210xx_led_init(aw210xx);
	aw210xx_i2c_write(aw210xx, AW210XX_REG_PATGO, 0);  /* set PAT run enable 0 for next pattern*/

	if ((aw210xx->red == 0xff) && (aw210xx->green == 0xff) && (aw210xx->blue == 0xff)) {  // 7 colors breath(T0=T2=0.7s T1=01.S T3=4S) 2 times by turn
		uint32_t dance_colors[7] =
			{COLOR_PINK, COLOR_ORANGE, COLOR_YELLOW, COLOR_GREEN, COLOR_CYAN, COLOR_BLUE, COLOR_PURPLE};
		int i = 0;

		while (aw210xx->dance_state == WORK_RUNNING) {
			/* set sl */
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GSLR, (dance_colors[i] & 0xff0000) >> 16);
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GSLG, (dance_colors[i] & 0x00ff00) >> 8);
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GSLB, (dance_colors[i] & 0x0000ff));
			/* br set */
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GBRH, aw210xx->brightness);
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GBRL, 0);
			aw210xx_group_gcfg_set(aw210xx, true);  /* group config */
			aw210xx_patcfg_set(aw210xx, true, aw210xx->flash_times);  /* pat config */
			aw210xx_i2c_write(aw210xx, AW210XX_REG_PATGO, AW210XX_PATGO_SET);  /* PAT run */
			time_max = (aw210xx->pat_time_cfg.t0 + aw210xx->pat_time_cfg.t1 +
					aw210xx->pat_time_cfg.t2 + aw210xx->pat_time_cfg.t3) * aw210xx->flash_times / 100 + 5;

			/* wait current parrent over */
			do {
				if (aw210xx->dance_state != WORK_RUNNING)
					break;
				mdelay(100);
				aw210xx_i2c_read(aw210xx, AW210XX_REG_PATGO, &buf);
				time_max--;
				//AW_LOG("Reg[0x%x] read 0x%x.\n", AW210XX_REG_PATGO, buf);
			} while(((buf & 0x4) != 0x4) && (time_max));

			aw210xx_i2c_write(aw210xx, AW210XX_REG_PATGO, 0);  /* set PAT run enable 0 for next pattern*/

			if (aw210xx->dance_state != WORK_RUNNING)
				break;

			mdelay(aw210xx->pat_time_cfg.t3);
			//AW_LOG("%s i=%d done\n", __func__, i);
			i = (i + 1) % (sizeof(dance_colors) / sizeof(uint32_t));
		}
	} else {
		while (aw210xx->dance_state == WORK_RUNNING) {
			/* set sl */
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GSLR, aw210xx->red);
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GSLG, aw210xx->green);
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GSLB, aw210xx->blue);
			/* br set */
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GBRH, aw210xx->brightness);
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GBRL, 0);
			aw210xx_group_gcfg_set(aw210xx, true);  /* group config */
			aw210xx_patcfg_set(aw210xx, true, DANCE_FLASH_TIMES);  /* pat config */
			aw210xx_i2c_write(aw210xx, AW210XX_REG_PATGO, AW210XX_PATGO_SET);  /* PAT run */
			time_max = (aw210xx->pat_time_cfg.t0 + aw210xx->pat_time_cfg.t1 +
					aw210xx->pat_time_cfg.t2 + aw210xx->pat_time_cfg.t3) * DANCE_FLASH_TIMES / 100 + 5;

			/* wait current parrent over */
			do {
				if (aw210xx->dance_state != WORK_RUNNING)
					break;
				mdelay(100);
				aw210xx_i2c_read(aw210xx, AW210XX_REG_PATGO, &buf);
				time_max--;
				// AW_LOG("Reg[0x%x] read 0x%x.\n", AW210XX_REG_PATGO, buf);
			} while(((buf & 0x4) != 0x4) && (time_max));

			aw210xx_i2c_write(aw210xx, AW210XX_REG_PATGO, 0);  /* set PAT run enable 0 for next pattern*/

			if (aw210xx->dance_state != WORK_RUNNING)
				break;

			mdelay(aw210xx->dance_sleep_ms);

			if ((aw210xx->flash_times) && (++i >= aw210xx->flash_times))
				break;
			else
				continue;
		}
	}
	aw210xx->dance_state = WORK_STOP;
}

static void cancel_all_works(struct aw210xx *aw210xx)
{
	if (aw210xx->dance_state != WORK_STOP) {
		aw210xx->dance_state = WORK_STOP;
		cancel_work_sync(&aw210xx->dance_work);
	}
	if (aw210xx->effect_work_state != WORK_STOP) {
		aw210xx->effect_work_state = WORK_STOP;
		cancel_delayed_work_sync(&aw210xx->effect_work);
	}
	AW_LOG("%s works canced.\n", __func__);
}

/******************************************************
 *
 * sys group attribute: reg
 *
 ******************************************************/
static ssize_t aw210xx_reg_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev[LED_CDEV]);
	uint32_t databuf[2] = { 0, 0 };

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		if (aw210xx_reg_access[(uint8_t)databuf[0]] & REG_WR_ACCESS)
			aw210xx_i2c_write(aw210xx, (uint8_t)databuf[0],
					(uint8_t)databuf[1]);
	}

	return len;
}

static ssize_t aw210xx_reg_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev[LED_CDEV]);
	ssize_t len = 0;
	unsigned int i = 0;
	unsigned char reg_val = 0;

	aw210xx_i2c_read(aw210xx, AW210XX_REG_GCR, &reg_val);
	len += snprintf(buf + len, PAGE_SIZE - len,
			"reg:0x%02x=0x%02x\n", AW210XX_REG_GCR, reg_val);

	for (i = AW210XX_REG_BR00L; i <= AW210XX_REG_BR08H; i++) {
		if (!(aw210xx_reg_access[i] & REG_RD_ACCESS))
			continue;
		aw210xx_i2c_read(aw210xx, i, &reg_val);
		len += snprintf(buf + len, PAGE_SIZE - len,
				"reg:0x%02x=0x%02x\n", i, reg_val);
	}
	for (i = AW210XX_REG_SL00; i <= AW210XX_REG_SL08; i++) {
		if (!(aw210xx_reg_access[i] & REG_RD_ACCESS))
			continue;
		aw210xx_i2c_read(aw210xx, i, &reg_val);
		len += snprintf(buf + len, PAGE_SIZE - len,
				"reg:0x%02x=0x%02x\n", i, reg_val);
	}
	for (i = AW210XX_REG_GCCR; i <= AW210XX_REG_GCFG; i++) {
		if (!(aw210xx_reg_access[i] & REG_RD_ACCESS))
			continue;
		aw210xx_i2c_read(aw210xx, i, &reg_val);
		len += snprintf(buf + len, PAGE_SIZE - len,
				"reg:0x%02x=0x%02x\n", i, reg_val);
	}

	return len;
}

static ssize_t aw210xx_hwen_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev[LED_CDEV]);
	int rc;
	unsigned int val = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	if (val > 0)
		aw210xx_hw_enable(aw210xx, true);
	else
		aw210xx_hw_enable(aw210xx, false);

	return len;
}

static ssize_t aw210xx_hwen_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev[LED_CDEV]);
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "hwen=%d\n",
			gpio_get_value(aw210xx->enable_gpio));
	return len;
}

static ssize_t aw210xx_cur_brightness_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev[LED_CDEV]);
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "cur_brightness=%d(0x%x)\n",
			aw210xx->brightness, aw210xx->brightness);
	return len;
}

static ssize_t aw210xx_cur_brightness_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev[LED_CDEV]);
	int rc;
	unsigned int val = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	aw210xx->brightness = val;
	return len;
}

static ssize_t aw210xx_global_current_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev[LED_CDEV]);
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "glo_current=%d(0x%x)\n",
			aw210xx->glo_current, aw210xx->glo_current);
	return len;
}

static ssize_t aw210xx_global_current_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev[LED_CDEV]);
	int rc;
	unsigned int val = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	aw210xx->glo_current = val;
	aw210xx_global_set(aw210xx);

	return len;
}

static ssize_t aw210xx_effect_color_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev[LED_CDEV]);
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "effect_color=0x%x(%d)\n",
			aw210xx->effect_color, aw210xx->effect_color);
	return len;
}

static ssize_t aw210xx_effect_color_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev[LED_CDEV]);
	int rc;
	unsigned int val = 0;

	rc = sscanf(buf, "%x", &val);
	if (rc < 0)
		return rc;

	aw210xx->effect_color = val;

	return len;
}

static ssize_t aw210xx_effect_on_ms_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev[LED_CDEV]);
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "effect_on_ms=%d(0x%x)ms\n",
			aw210xx->effect_on_ms, aw210xx->effect_on_ms);
	return len;
}

static ssize_t aw210xx_effect_on_ms_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev[LED_CDEV]);
	int rc;
	unsigned int val = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	aw210xx->effect_on_ms = val;

	return len;
}

static ssize_t aw210xx_effect_off_ms_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev[LED_CDEV]);
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "effect_off_ms=%d(0x%x)ms\n",
			aw210xx->effect_off_ms, aw210xx->effect_off_ms);
	return len;
}

static ssize_t aw210xx_effect_off_ms_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev[LED_CDEV]);
	int rc;
	unsigned int val = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	aw210xx->effect_off_ms = val;

	return len;
}

static ssize_t aw210xx_dance_sleep_ms_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev[LED_CDEV]);
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "dance_sleep_ms=%d(0x%x)ms\n",
			aw210xx->dance_sleep_ms, aw210xx->dance_sleep_ms);
	return len;
}

static ssize_t aw210xx_dance_sleep_ms_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev[LED_CDEV]);
	int rc;
	unsigned int val = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	aw210xx->dance_sleep_ms = val;

	return len;
}

static ssize_t aw210xx_effect_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev[LED_CDEV]);
	ssize_t len = 0;
	unsigned int i;

	for (i = 0; i < (sizeof(aw210xx_cfg_array) /
			sizeof(aw210xx_cfg_t)); i++) {
		len += snprintf(buf + len, PAGE_SIZE - len, "effect[%d]: %pf\n",
				i, aw210xx_cfg_array[i].p);
	}

	len += snprintf(buf + len, PAGE_SIZE - len, "current effect[%d]: %pf\n",
			aw210xx->effect, aw210xx_cfg_array[aw210xx->effect].p);
	return len;
}

static ssize_t aw210xx_effect_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev[LED_CDEV]);
	int rc;
	unsigned int val = 0;
	uint32_t on_ms = 0, off_ms = 0, set_flag = 0;

	rc = kstrtouint(buf, 10, &val);
	if (rc < 0)
		return rc;
	if ((val >= (sizeof(aw210xx_cfg_array) /
			sizeof(aw210xx_cfg_t))) || (val < 0)) {
		pr_err("%s, store effect num error.\n", __func__);
		return -EINVAL;
	}

	aw210xx->effect = val;
	pr_info("%s, line%d,val = %d\n", __func__, __LINE__, val);

	cancel_all_works(aw210xx);

	switch(aw210xx->effect) {
	case GROUP_CFG_LED_OFF:
		aw210xx_light_on(aw210xx, 0, 0);
	break;
	case AW21009_GROUP_LED_ON:
		aw210xx_light_on(aw210xx, aw210xx->effect_color, aw210xx->brightness);
	break;
	case AW21009_LONG_BREATH_RED_ON:  // T0=T2=1.6s T1=0.2s T3=4s by default
		if (!set_flag) {
			aw210xx->effect_color = COLOR_ORANGE;
			on_ms = 0x064000c8;
			off_ms = 0x06400fa0;
			set_flag = 1;
		}
	case AW21009_BREATH_LED_ON:  // T0=T2=0.7s T1=0.1s T3=4s by default
		if (!set_flag) {
			on_ms = 0x02bc0064;
			off_ms = 0x02bc0fa0;
			set_flag = 1;
		}
		if (set_flag) {
			aw210xx_led_init(aw210xx);
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GSLR, ((aw210xx->effect_color >> 16) & 0xff));
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GSLG, ((aw210xx->effect_color >> 8) & 0xff));
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GSLB, (aw210xx->effect_color & 0xff));
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GBRH, aw210xx->brightness);
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GBRL, 0);
			aw210xx_group_gcfg_set(aw210xx, true);
			aw210xx->pat_time_cfg.t0 = (on_ms >> 16) & 0xffff;
			aw210xx->pat_time_cfg.t1 = on_ms & 0xffff;
			aw210xx->pat_time_cfg.t2 = (off_ms >> 16) & 0xffff;
			aw210xx->pat_time_cfg.t3 = off_ms & 0xffff;
			aw210xx_patcfg_set(aw210xx, true, 0);
			aw210xx_i2c_write(aw210xx, AW210XX_REG_PATGO, AW210XX_PATGO_SET);
		}
	break;
	/* T0=T2=0s T1=T3=0.1s flash 3 times, sleep 1s, and so on ...*/
	case AW21009_DANCE_COLORS_ON:
		aw210xx->flash_times = 0;
		aw210xx->red = (aw210xx->effect_color & 0xff0000) >> 16;
		aw210xx->green = (aw210xx->effect_color & 0x00ff00) >> 8;
		aw210xx->blue = (aw210xx->effect_color & 0x0000ff);
		aw210xx->pat_time_cfg.t0 = (aw210xx->effect_on_ms >> 16) & 0xffff;
		aw210xx->pat_time_cfg.t1 = aw210xx->effect_on_ms & 0xffff;
		aw210xx->pat_time_cfg.t2 = (aw210xx->effect_off_ms >> 16) & 0xffff;
		aw210xx->pat_time_cfg.t3 = aw210xx->effect_off_ms & 0xffff;
		aw210xx->dance_state = WORK_RUNNING;
		schedule_work(&aw210xx->dance_work);
		AW_LOG("dance work scheduled.\n");
	break;
	case AW21009_POWER_ON_EFFECT:
		aw210xx->effect_work_state = WORK_RUNNING;
		schedule_delayed_work(&aw210xx->effect_work, 0);
		AW_LOG("effect %d work scheduled.", aw210xx->effect);
	break;
	default:
		AW_LOG("effect %d WHY?\n", aw210xx->effect);
	break;
	}

	return len;
}

static ssize_t aw210xx_groupcolor_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev[LED_CDEV]);
	uint32_t rgb_data = 0;

	if (sscanf(buf, "%x", &rgb_data) == 1) {
		AW_LOG("enter, rgb_data=0x%06x\n", rgb_data);
		cancel_all_works(aw210xx);
		aw210xx_light_on(aw210xx, rgb_data, aw210xx->brightness);
	}

	return len;
}

static ssize_t aw210xx_breathcolor_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev[LED_CDEV]);
	uint32_t rgb_data = 0,  on_ms = 0, off_ms = 0;
	uint32_t max_rgb = 0, sum_rgb = 0;

	if (sscanf(buf, "%x %x %x", &rgb_data, &on_ms, &off_ms) == 3) {
		AW_LOG("enter, rgb_data=0x%06x on_ms=0x%x off_ms=0x%x\n",
				rgb_data, on_ms, off_ms);
		cancel_all_works(aw210xx);

		if (rgb_data == 0) {
			aw210xx_light_on(aw210xx, 0, 0);
			return len;
		}

		aw210xx->flash_times = (rgb_data & 0xff000000) >> 24;
		aw210xx_led_init(aw210xx);

		/* set sl */
		aw210xx->red = (rgb_data & 0xff0000) >> 16;
		aw210xx->green = (rgb_data & 0x00ff00) >> 8;
		aw210xx->blue = (rgb_data & 0x0000ff);

		/* AW21009_MAX_CURRENT check
		 * max_current determined by global_current */
		max_rgb = 255 * 255 / aw210xx->brightness;
		sum_rgb = aw210xx->red + aw210xx->green + aw210xx->blue;
		if (sum_rgb > max_rgb) {
			aw210xx->red = aw210xx->red * max_rgb / sum_rgb;
			aw210xx->green = aw210xx->green * max_rgb / sum_rgb;
			aw210xx->blue = aw210xx->blue * max_rgb / sum_rgb;
		}

		AW_LOG("color = [%d %d %d] flash_times = %d brightness = %d\n",
				aw210xx->red, aw210xx->green, aw210xx->blue, aw210xx->flash_times, aw210xx->brightness);

		aw210xx_i2c_write(aw210xx, AW210XX_REG_PATGO, 0);  /* set PAT run enable 0 for next pattern*/

		aw210xx_i2c_write(aw210xx, AW210XX_REG_GSLR, aw210xx->red);
		aw210xx_i2c_write(aw210xx, AW210XX_REG_GSLG, aw210xx->green);
		aw210xx_i2c_write(aw210xx, AW210XX_REG_GSLB, aw210xx->blue);

		/* br set */
		aw210xx_i2c_write(aw210xx, AW210XX_REG_GBRH, aw210xx->brightness);
		aw210xx_i2c_write(aw210xx, AW210XX_REG_GBRL, 0);

		/* group config */
		aw210xx_group_gcfg_set(aw210xx, true);

		/* pat config */
		aw210xx->pat_time_cfg.t0 = (on_ms >> 16) & 0xffff;
		aw210xx->pat_time_cfg.t1 = on_ms & 0xffff;
		aw210xx->pat_time_cfg.t2 = (off_ms >> 16) & 0xffff;
		aw210xx->pat_time_cfg.t3 = off_ms & 0xffff;
		aw210xx_patcfg_set(aw210xx, true, aw210xx->flash_times);

		/* PAT run */
		aw210xx_i2c_write(aw210xx, AW210XX_REG_PATGO, AW210XX_PATGO_SET);
	}

	return len;
}

static ssize_t aw210xx_dancecolor_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev[LED_CDEV]);
	uint32_t rgb_times = 0, on_ms = 0, off_ms = 0;

	if (sscanf(buf, "%x %x %x", &rgb_times, &on_ms, &off_ms) == 3) {
		AW_LOG("rgb_times=0x%x on_ms=0x%x off_ms=0x%x\n", rgb_times, on_ms, off_ms);
		cancel_all_works(aw210xx);
		if ((on_ms == 0) && (off_ms == 0)) { // power on effect
			aw210xx->flash_times = (rgb_times & 0xff000000) >> 24;
			aw210xx->red = (rgb_times & 0xff0000) >> 16;
			aw210xx->green = (rgb_times & 0x00ff00) >> 8;
			aw210xx->blue = (rgb_times & 0x0000ff);
			aw210xx->effect_work_state = WORK_RUNNING;
			schedule_delayed_work(&aw210xx->effect_work, 0);
		}
		if (rgb_times && on_ms && off_ms) {
			aw210xx->flash_times = (rgb_times & 0xff000000) >> 24;
			aw210xx->red = (rgb_times & 0xff0000) >> 16;
			aw210xx->green = (rgb_times & 0x00ff00) >> 8;
			aw210xx->blue = (rgb_times & 0x0000ff);
			aw210xx->pat_time_cfg.t0 = (on_ms >> 16) & 0xffff;
			aw210xx->pat_time_cfg.t1 = on_ms & 0xffff;
			aw210xx->pat_time_cfg.t2 = (off_ms >> 16) & 0xffff;
			aw210xx->pat_time_cfg.t3 = off_ms & 0xffff;
			aw210xx->dance_state = WORK_RUNNING;
			schedule_work(&aw210xx->dance_work);
			AW_LOG("dance work scheduled.\n");
		}
	}

	return len;
}

static DEVICE_ATTR(reg, 0664, aw210xx_reg_show, aw210xx_reg_store);
static DEVICE_ATTR(hwen, 0664, aw210xx_hwen_show, aw210xx_hwen_store);
static DEVICE_ATTR(cur_brightness, 0664, aw210xx_cur_brightness_show, aw210xx_cur_brightness_store);
static DEVICE_ATTR(global_current, 0664, aw210xx_global_current_show, aw210xx_global_current_store);
static DEVICE_ATTR(effect_color, 0664, aw210xx_effect_color_show, aw210xx_effect_color_store);
static DEVICE_ATTR(effect_on_ms, 0664, aw210xx_effect_on_ms_show, aw210xx_effect_on_ms_store);
static DEVICE_ATTR(effect_off_ms, 0664, aw210xx_effect_off_ms_show, aw210xx_effect_off_ms_store);
static DEVICE_ATTR(dance_sleep_ms, 0664, aw210xx_dance_sleep_ms_show, aw210xx_dance_sleep_ms_store);
static DEVICE_ATTR(effect, 0664, aw210xx_effect_show, aw210xx_effect_store);
static DEVICE_ATTR(groupcolor, 0664, NULL, aw210xx_groupcolor_store);
static DEVICE_ATTR(breathcolor, 0664, NULL, aw210xx_breathcolor_store);
static DEVICE_ATTR(dancecolor, 0664, NULL, aw210xx_dancecolor_store);

static struct attribute *aw210xx_attributes[] = {
	&dev_attr_reg.attr,
	&dev_attr_hwen.attr,
	&dev_attr_cur_brightness.attr,
	&dev_attr_global_current.attr,
	&dev_attr_effect_color.attr,
	&dev_attr_effect_on_ms.attr,
	&dev_attr_effect_off_ms.attr,
	&dev_attr_dance_sleep_ms.attr,
	&dev_attr_effect.attr,
	&dev_attr_groupcolor.attr,
	&dev_attr_breathcolor.attr,
	&dev_attr_dancecolor.attr,
	NULL,
};

static struct attribute_group aw210xx_attribute_group = {
	.attrs = aw210xx_attributes
};

/******************************************************
 *
 * led class dev
 ******************************************************/
static int aw210xx_init_led_cdev(struct aw210xx *aw210xx,
		struct device_node *np)
{
	int ret = -1, i = 0, j = 0;
	AW_LOG("enter\n");

	for (i = 0; i < AW210XX_LED_DEV_MAX_NUM; i++) {
		aw210xx->cdev[i].brightness = 255;
		aw210xx->cdev[i].max_brightness = 255;
		switch(i) {
		case LED_CDEV:
			aw210xx->cdev[i].name = AW210XX_NAME;
			INIT_WORK(&aw210xx->brightness_work[i], aw210xx_brightness_work);
			aw210xx->cdev[i].brightness_set = aw210xx_set_brightness;
		break;
		case RED_CDEV:
			aw210xx->cdev[i].name = "red";
			INIT_WORK(&aw210xx->brightness_work[i], aw210xx_red_brightness_work);
			aw210xx->cdev[i].brightness_set = aw210xx_set_red_brightness;
		break;
		case GREEN_CDEV:
			aw210xx->cdev[i].name = "green";
			INIT_WORK(&aw210xx->brightness_work[i], aw210xx_green_brightness_work);
			aw210xx->cdev[i].brightness_set = aw210xx_set_green_brightness;
		break;
		case BLUE_CDEV:
			aw210xx->cdev[i].name = "blue";
			INIT_WORK(&aw210xx->brightness_work[i], aw210xx_blue_brightness_work);
			aw210xx->cdev[i].brightness_set = aw210xx_set_blue_brightness;
		break;
		default:
		break;
		}
		ret = led_classdev_register(aw210xx->dev, &aw210xx->cdev[i]);
		if (ret) {
			AW_ERR("unable to register led ret=%d\n", ret);
			goto free_class;
		}
	}

	ret = sysfs_create_group(&aw210xx->cdev[LED_CDEV].dev->kobj,
			&aw210xx_attribute_group);
	if (ret) {
		AW_ERR("led sysfs ret: %d\n", ret);
		goto free_class;
	}

	aw210xx_led_init(aw210xx);

	return 0;

free_class:
	for (j = 0; j < i; j++)
		led_classdev_unregister(&aw210xx->cdev[j]);

	return ret;
}

/*****************************************************
 *
 * check chip id and version
 *
 *****************************************************/
static uint8_t is_breath_led_exist = 0;  /* zte boardtest interface */
static int aw210xx_read_chipid(struct aw210xx *aw210xx)
{
	int ret = -1;
	unsigned char cnt = 0;
	unsigned char chipid = 0;
	is_breath_led_exist = 0;  /* zte boardtest interface */

	while (cnt < AW_READ_CHIPID_RETRIES) {
		ret = aw210xx_i2c_read(aw210xx, AW210XX_REG_RESET, &chipid);
		if (ret < 0) {
			AW_ERR("failed to read chipid: %d\n", ret);
		} else {
			aw210xx->chipid = chipid;
			switch (aw210xx->chipid) {
			case AW21018_CHIPID:
				AW_LOG("AW21018, read chipid = 0x%02x!!\n",
						chipid);
				is_breath_led_exist = 1;  /* zte boardtest interface */
				return 0;
			case AW21012_CHIPID:
				AW_LOG("AW21012, read chipid = 0x%02x!!\n",
						chipid);
				is_breath_led_exist = 1;  /* zte boardtest interface */
				return 0;
			case AW21009_CHIPID:
				AW_LOG("AW21009, read chipid = 0x%02x!!\n",
						chipid);
				is_breath_led_exist = 1;  /* zte boardtest interface */
				return 0;
			default:
				AW_LOG("chip is unsupported device id = %x\n",
						chipid);
				break;
			}
		}
		cnt++;
		usleep_range(AW_READ_CHIPID_RETRY_DELAY * 1000,
				AW_READ_CHIPID_RETRY_DELAY * 1000 + 500);
	}

	return -EINVAL;
}

/*****************************************************
 *
 * device tree
 *
 *****************************************************/
static int aw210xx_parse_dt(struct device *dev, struct aw210xx *aw210xx,
		struct device_node *np)
{
	int ret = -EINVAL;

	aw210xx->enable_gpio = of_get_named_gpio(np, "enable-gpio", 0);
	if (aw210xx->enable_gpio < 0) {
		aw210xx->enable_gpio = -1;
		AW_ERR("no enable gpio provided, HW enable unsupported\n");
		return ret;
	}

	ret = of_property_read_u32(np, "osc_clk",
			&aw210xx->osc_clk);
	if (ret < 0) {
		AW_ERR("no osc_clk provided, osc clk unsupported\n");
		return ret;
	}

	ret = of_property_read_u32(np, "br_res",
			&aw210xx->br_res);
	if (ret < 0) {
		AW_ERR("brightness resolution unsupported\n");
		return ret;
	}

	return 0;
}

/* add zte boardtest interface start */
int is_breath_led_exist_get(char *val, const void *arg)
{
	return snprintf(val, PAGE_SIZE, "%d", is_breath_led_exist);
}

static struct zte_misc_ops is_breath_led_exist_node = {
	.node_name = "is_breath_led_exist",
	.set = NULL,
	.get = is_breath_led_exist_get,
	.free = NULL,
	.arg = NULL,
};
/* add zte boardtest interface end */

/******************************************************
 *
 * i2c driver
 *
 ******************************************************/
static int aw210xx_i2c_probe(struct i2c_client *i2c,
		const struct i2c_device_id *id)
{
	struct aw210xx *aw210xx;
	struct device_node *np = i2c->dev.of_node;
	int ret;
#ifdef CONFIG_VENDOR_SOC_MTK_COMPILE
	int boot_mode = UNKNOWN_BOOT;
#endif

	AW_LOG("enter\n");

	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C)) {
		AW_ERR("check_functionality failed\n");
		return -EIO;
	}

	aw210xx = devm_kzalloc(&i2c->dev, sizeof(struct aw210xx), GFP_KERNEL);
	if (aw210xx == NULL)
		return -ENOMEM;

	aw210xx->dev = &i2c->dev;
	aw210xx->i2c = i2c;
	i2c_set_clientdata(i2c, aw210xx);

	/* aw210xx parse device tree */
	if (np) {
		ret = aw210xx_parse_dt(&i2c->dev, aw210xx, np);
		if (ret) {
			AW_ERR("failed to parse device tree node\n");
			goto err_parse_dt;
		}
	}

	if (gpio_is_valid(aw210xx->enable_gpio)) {
		ret = devm_gpio_request_one(&i2c->dev, aw210xx->enable_gpio,
				GPIOF_OUT_INIT_LOW, NULL);
		if (ret) {
			AW_ERR("enable gpio request failed\n");
			goto err_gpio_request;
		}
	}

	/* hardware enable */
	aw210xx_hw_enable(aw210xx, true);

	/* add zte boardtest interface */
	zte_misc_register_callback(&is_breath_led_exist_node, NULL);

	/* aw210xx identify */
	ret = aw210xx_read_chipid(aw210xx);
	if (ret < 0) {
		AW_ERR("aw210xx_read_chipid failed ret=%d\n", ret);
		goto err_id;
	}

	dev_set_drvdata(&i2c->dev, aw210xx);
	aw210xx_init_led_cdev(aw210xx, np);
	if (ret < 0) {
		AW_ERR("error creating led class dev\n");
		goto err_sysfs;
	}

	aw210xx->effect_color = COLOR_BLUE;
	aw210xx->effect_on_ms = USER_DEFINE_EFFECT_ON_MS;
	aw210xx->effect_off_ms = USER_DEFINE_EFFECT_OFF_MS;
	aw210xx->dance_sleep_ms = DANCE_SLEEP_MS;
	aw210xx->brightness = AW21009_BRIGHTNESS;
	aw210xx->glo_current = AW21009_GLO_CURRENT;
	aw210xx->flash_times = 0;
	aw210xx->dance_state = WORK_STOP;
	aw210xx->effect_work_state = WORK_STOP;
	INIT_WORK(&aw210xx->dance_work, dance_work_func);
	INIT_DELAYED_WORK(&aw210xx->effect_work, effect_work_func);

#ifdef CONFIG_VENDOR_SOC_MTK_COMPILE
	boot_mode = battery_get_boot_mode();
	if ((boot_mode != KERNEL_POWER_OFF_CHARGING_BOOT) &&
		(boot_mode != LOW_POWER_OFF_CHARGING_BOOT)) {
#elif CONFIG_VENDOR_SOC_SPRD_COMPILE
	get_boot_mode();
	if(!is_charger_mode) {
#endif
		aw210xx->flash_times = 1;
		aw210xx->red = (COLOR_BLUE & 0xff0000) >> 16;
		aw210xx->green = (COLOR_BLUE & 0x00ff00) >> 8;
		aw210xx->blue = (COLOR_BLUE & 0x0000ff);
		aw210xx->effect_work_state = WORK_RUNNING;
		schedule_delayed_work(&aw210xx->effect_work, 0);
	}

	AW_LOG("effect %d work scheduled.", aw210xx->effect);
	AW_LOG("probe completed!\n");
	return 0;

err_sysfs:
err_id:
	devm_gpio_free(&i2c->dev, aw210xx->enable_gpio);
err_gpio_request:
err_parse_dt:
	devm_kfree(&i2c->dev, aw210xx);
	aw210xx = NULL;
	return ret;
}

static void aw210xx_i2c_shutdown(struct i2c_client *i2c)
{
	struct aw210xx *aw210xx = i2c_get_clientdata(i2c);
	AW_LOG("enter\n");

	if (gpio_is_valid(aw210xx->enable_gpio))
		gpio_set_value_cansleep(aw210xx->enable_gpio, 0);
}

static int aw210xx_i2c_remove(struct i2c_client *i2c)
{
	struct aw210xx *aw210xx = i2c_get_clientdata(i2c);
	int i = 0;

	AW_LOG("enter\n");
	if (gpio_is_valid(aw210xx->enable_gpio))
		gpio_set_value_cansleep(aw210xx->enable_gpio, 0);

	sysfs_remove_group(&aw210xx->cdev[LED_CDEV].dev->kobj, &aw210xx_attribute_group);

	for (i = 0; i < AW210XX_LED_DEV_MAX_NUM; i++)
		cancel_work_sync(&aw210xx->brightness_work[i]);

	cancel_work_sync(&aw210xx->dance_work);
	cancel_delayed_work_sync(&aw210xx->effect_work);

	for (i = 0; i < AW210XX_LED_DEV_MAX_NUM; i++)
		led_classdev_unregister(&aw210xx->cdev[i]);

	if (gpio_is_valid(aw210xx->enable_gpio))
		devm_gpio_free(&i2c->dev, aw210xx->enable_gpio);

	devm_kfree(&i2c->dev, aw210xx);
	aw210xx = NULL;

	return 0;
}

static const struct i2c_device_id aw210xx_i2c_id[] = {
	{AW210XX_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, aw210xx_i2c_id);

static const struct of_device_id aw210xx_dt_match[] = {
	{.compatible = "awinic,aw210xx_led"},
	{}
};

static struct i2c_driver aw210xx_i2c_driver = {
	.driver = {
		.name = AW210XX_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(aw210xx_dt_match),
		},
	.probe = aw210xx_i2c_probe,
	.shutdown = aw210xx_i2c_shutdown,
	.remove = aw210xx_i2c_remove,
	.id_table = aw210xx_i2c_id,
};

static int __init aw210xx_i2c_init(void)
{
	int ret = 0;

	AW_LOG("enter, aw210xx driver version %s\n", AW210XX_DRIVER_VERSION);

	ret = i2c_add_driver(&aw210xx_i2c_driver);
	if (ret) {
		AW_ERR("failed to register aw210xx driver!\n");
		return ret;
	}

	return 0;
}
module_init(aw210xx_i2c_init);

static void __exit aw210xx_i2c_exit(void)
{
	i2c_del_driver(&aw210xx_i2c_driver);
}
module_exit(aw210xx_i2c_exit);

MODULE_DESCRIPTION("AW210XX LED Driver");
MODULE_LICENSE("GPL v2");
