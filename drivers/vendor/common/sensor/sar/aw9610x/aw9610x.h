#ifndef _AW9610X_H_
#define _AW9610X_H_

#include <linux/regmap.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/leds.h>
#include "aw9610x_reg_config.h"
#ifdef CONFIG_VENDOR_SOC_MTK_COMPILE
#ifdef SENSOR_ARCH_1_0
#include "sensor_list.h"
#endif
#ifdef SENSOR_ARCH_2_0
#include <hf_manager.h>
#endif
#endif

#define AW9610X_CHIP_ID		0xa961
#define AWINIC_CFG_UPDATE_DELAY	1
#define AW_SAR_SUCCESS		0
#define AW_SAR_CAHNNEL_MAX	6

/**********************************************
* cfg load situation
**********************************************/
enum aw9610x_cfg_situ {
	AW_CFG_UNLOAD = 0,
	AW_CFG_LOADED = 1,
};

/**********************************************
* cali mode
**********************************************/
enum aw9610x_cali_mode {
	AW_CALI_NORM_MODE = 0,
	AW_CALI_NODE_MODE = 1,
};

/**********************************************
*spereg cali flag
**********************************************/
enum aw9610x_cali_flag {
	AW_NO_CALI = 0,
	AW_CALI = 1,
};

/**********************************************
*spereg addr offset
**********************************************/
enum aw9610x_spereg_addr_offset {
	AW_CL1SPE_CALI_OS = 20,
	AW_CL1SPE_DEAL_OS = 60,
	AW_CL2SPE_CALI_OS = 4,
	AW_CL2SPE_DEAL_OS = 4,
};

/**********************************************
*the flag of i2c read/write
**********************************************/
enum aw9610x_i2c_flags {
	AW9610X_I2C_WR = 0,
	AW9610X_I2C_RD = 1,
};

/*********************************************************
* aw9610x error flag:
* @AW_MALLOC_FAILED: malloc space failed.
* @AW_CHIPID_FAILED: the chipid is error.
* @AW_IRQIO_FAILED: irq gpio invalid.
* @AW_IRQ_REQUEST_FAILED: irq request failed.
* @AW_CFG_LOAD_TIME_FAILED : cfg situation not confirmed.
**********************************************************/
enum aw9610x_err_flags {
	AW_MALLOC_FAILED = 200,
	AW_CHIPID_FAILED = 201,
	AW_IRQIO_FAILED = 202,
	AW_IRQ_REQUEST_FAILED = 203,
	AW_INPUT_ALLOCATE_FILED = 204,
	AW_INPUT_REGISTER_FAILED = 205,
	AW_CFG_LOAD_TIME_FAILED = 206,
};

/************************************************************
* Interrupts near or far from the threshold will be triggered
*************************************************************/
enum aw9610x_irq_trigger_position {
	FAR,
	TRIGGER_TH0,
	TRIGGER_TH1 = 0x03,
	TRIGGER_TH2 = 0x07,
	TRIGGER_TH3 = 0x0f,
};

struct aw_i2c_package {
	uint8_t addr_bytes;
	uint8_t data_bytes;
	uint8_t reg_num;
	uint8_t init_addr[4];
	uint8_t *p_reg_data;
};

struct aw9610x {
	uint8_t cali_flag;
	uint8_t node;
	const char *chip_name;

	int32_t irq_gpio;
	uint32_t irq_status;
	uint32_t status;
	uint32_t ch_num;
	uint32_t hostirqen;
	uint32_t first_irq_flag;
	uint32_t spedata[8];
	uint32_t nvspe_data[8];
	bool pwprox_dete;
	bool firmware_flag;

	struct delayed_work cfg_work;
	struct i2c_client *i2c;
	struct device *dev;
#if (defined CONFIG_VENDOR_SOC_SPRD_COMPILE) || (defined CONFIG_VENDOR_SOC_QCOM_COMPILE)
	struct input_dev *input;
#endif
#ifdef CONFIG_VENDOR_SOC_MTK_COMPILE
#ifdef SENSOR_ARCH_2_0
	struct hf_device hf_dev;
#endif
#endif
	struct delayed_work dworker; /* work struct for worker function */
	struct aw_bin *aw_bin;
	struct aw_i2c_package aw_i2c_package;
	uint8_t curr_state[6];
	uint8_t last_state[6];
};

static struct aw9610x *aw_sar_ptr;
static char chip_info[20];

struct aw9610x_cfg {
	int len;
	unsigned int data[];
};

static uint32_t attr_buf[] = {
	8, 10,
	9, 100,
	10, 1000,
};
#endif
