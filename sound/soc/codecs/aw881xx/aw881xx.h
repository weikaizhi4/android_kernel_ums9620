#ifndef __AW881XX_H__
#define __AW881XX_H__

#include <linux/version.h>
#include <sound/control.h>
#include <sound/soc.h>
#include "aw881xx_spin.h"
#include "aw881xx_monitor.h"
#include "aw881xx_cali.h"
#include "aw_data_type.h"

/*#define AW_SYNC_LOAD*/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 1)
#define AW_KERNEL_VER_OVER_4_19_1
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)
#define AW_KERNEL_VER_OVER_5_4_0
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
#endif

/********************************************
 *
 * enum
 *
 *******************************************/
enum {
	AW_DEV_TYPE_OK = 0,
	AW_DEV_TYPE_NONE = 1,
};

enum {
	AW_UI_DSP_FW_MODE = 0,
	AW_UI_DSP_CFG_MODE,
	AW_UI_MODE_MAX,
};

enum {
	AW881XX_FW_FAILED = 0,
	AW881XX_FW_OK,
};

enum aw881xx_dev_status {
	AW881XX_PW_OFF = 0,
	AW881XX_PW_ON,
};

enum aw881xx_id {
	AW881XX_CHIPID = 0x1806,
	AW881XX_PID_01 = 0x01,
	AW881XX_PID_02 = 0x02,
	AW881XX_PID_03 = 0x03,
};

enum aw881xx_dsp_pid {
	AW881XX_DSP_PID_01 = 0x0000,
	AW881XX_DSP_PID_02 = 0x0001,
	AW881XX_DSP_PID_03 = 0x6E90,
};

enum aw881xx_memclk {
	AW881XX_MEMCLK_OSC = 0,
	AW881XX_MEMCLK_PLL = 1,
};

enum aw881xx_init {
	AW881XX_INIT_ST = 0,
	AW881XX_INIT_OK = 1,
	AW881XX_INIT_NG = 2,
};

enum aw881x_audio_stream_st {
	AW881XX_AUDIO_STOP = 0,
	AW881XX_AUDIO_START = 1,
};

enum aw881xx_dsp_cfg {
	AW881XX_DSP_WORK = 0,
	AW881XX_DSP_BYPASS = 1,
};

enum aw881xx_baseaddr {
	AW881XX_REG_ADDR = 0x00,
	AW881XX_DSP_FW_ADDR = 0x8c00,
	AW881XX_DSP_CFG_ADDR = 0x8600,
};

enum {
	AW_1000_US = 1000,
	AW_2000_US = 2000,
	AW_3000_US = 3000,
	AW_4000_US = 4000,
	AW_5000_US = 5000,
	AW_10000_US = 10000,
	AW_100000_US = 100000,
};

enum {
	AW_DEV_RENAME_DISABLE = 0,
	AW_DEV_RENAME_ENABLE = 1,
};

#define AW881XX_ADD_CHAN_NAME_SHIFT	(2)

#define AW881XX_READ_MSG_NUM		(2)

/*
 * i2c transaction on Linux limited to 64k
 * (See Linux kernel documentation: Documentation/i2c/writing-clients)
*/
#define MAX_I2C_BUFFER_SIZE		(65536)

#define AW881XX_FLAG_START_ON_MUTE	(1 << 0)
#define AW881XX_FLAG_SKIP_INTERRUPTS	(1 << 1)

#define AW881XX_NUM_RATES		(9)
#define AW881XX_SYSST_CHECK_MAX		(10)
#define AW881XX_DSP_CHECK_MAX		(3)

#define AW881XX_DFT_CALI_RE		(0x8000)

#define AW881XX_CHAN_VAL_DEFAULT	(0)
#define AW881XX_PA_SYNC_DEFAULT		(0)
#define AW881XX_FADE_IN_OUT_DEFAULT	(0)
#define AW881XX_RENAME_VAL_DEFAULT	(0)

#define AW881XX_VBAT_COEFF_INT_10BIT	(1023)

#define AW881XX_CFG_NAME_MAX		(64)

#define AW_FADE_OUT_TARGET_VOL		(90 * 2)
#define AW_VOLUME_STEP_DB		(6 * 2)
#define AW_1000_US			(1000)
#define AW_FW_LOAD_RETRIES		(5)


#define AW_CONTROL_NUM			(3)
#define AW_LOAD_BIN_TIME_MS		(1000)
#define AW881XX_START_WORK_DELAY_MS	(30)

#define AW_GET_MIN_VALUE(value1, value2) \
	((value1) > (value2) ? (value2) : (value1))

#define AW_GET_MAX_VALUE(value1, value2) \
	((value1) > (value2) ? (value1) : (value2))

/********************************************
 *
 * DSP I2C WRITES
 *
 *******************************************/
#define AW881XX_DSP_I2C_WRITES
#define AW881XX_MAX_RAM_WRITE_BYTE_SIZE		(128)

/********************************************
 *
 * print information control
 *
 *******************************************/
#define aw_dev_err(dev, format, ...) \
		pr_err("[%s]" format, dev_name(dev), ##__VA_ARGS__)

#define aw_dev_info(dev, format, ...) \
		pr_info("[%s]" format, dev_name(dev), ##__VA_ARGS__)

#define aw_dev_dbg(dev, format, ...) \
		pr_debug("[%s]" format, dev_name(dev), ##__VA_ARGS__)

#define aw_pr_err(format, ...) \
	do { \
		pr_err("[Awinic]%s: " format "\n", __func__, ##__VA_ARGS__); \
	} while (0)
#define aw_pr_info(format, ...) \
	do { \
		pr_info("[Awinic]%s: " format "\n", __func__, ##__VA_ARGS__); \
	} while (0)
#define aw_pr_dbg(format, ...) \
	do { \
		pr_debug("[Awinic]%s: " format "\n", __func__, ##__VA_ARGS__); \
	} while (0)

/********************************************
 *
 * Compatible with codec and component
 *
 *******************************************/
#ifdef AW_KERNEL_VER_OVER_4_19_1
typedef struct snd_soc_component aw_snd_soc_codec_t;
typedef struct snd_soc_component_driver aw_snd_soc_codec_driver_t;
#else
typedef struct snd_soc_codec aw_snd_soc_codec_t;
typedef struct snd_soc_codec_driver aw_snd_soc_codec_driver_t;
#endif

struct aw_componet_codec_ops {
	aw_snd_soc_codec_t *(*aw_snd_soc_kcontrol_codec)(struct snd_kcontrol *kcontrol);
	void *(*aw_snd_soc_codec_get_drvdata)(aw_snd_soc_codec_t *codec);
	int (*aw_snd_soc_add_codec_controls)(aw_snd_soc_codec_t *codec,
		const struct snd_kcontrol_new *controls,
		unsigned int num_controls);
	void (*aw_snd_soc_unregister_codec)(struct device *dev);
	int (*aw_snd_soc_register_codec)(struct device *dev,
			const aw_snd_soc_codec_driver_t *codec_drv,
			struct snd_soc_dai_driver *dai_drv,
			int num_dai);
};

struct aw881xx_container {
	uint32_t len;
	uint8_t data[];
};

struct aw_re_range_desc {
	uint32_t re_min;
	uint32_t re_max;
};

struct aw_volume_desc {
	unsigned int init_volume;
	unsigned int ctl_volume;
	unsigned int monitor_volume;
};

/********************************************
 *
 * aw881xx struct
 *
 *******************************************/
struct aw881xx {
	struct regmap *regmap;
	struct i2c_client *i2c;
	struct device *dev;
	struct mutex lock;
	struct mutex i2c_lock;
	struct aw881xx_monitor monitor;
	struct aw_cali_desc cali_desc;
	aw_snd_soc_codec_t *codec;
	struct aw_prof_info prof_info;
	struct aw_spin_desc spin_desc;
	struct aw_volume_desc volume_desc;
	struct aw_componet_codec_ops *codec_ops;

	int sysclk;
	int pstream;
	uint8_t spin_flag;

	int reset_gpio;
	int irq_gpio;

	uint8_t flags;

	uint8_t fw_status;	/*load cfg status*/
	unsigned char cur_prof;	/*current profile index*/
	unsigned char set_prof;	/*set profile index*/
	unsigned char fw_retry_cnt;

	uint16_t chipid;
	uint8_t pid;
	uint32_t cali_delay;
	uint8_t i2c_log_en;

	uint8_t reg_addr;
	uint16_t dsp_addr;

	uint8_t dsp_cfg;
	char ui_cfg_name[AW_UI_MODE_MAX][AW881XX_CFG_NAME_MAX];

	uint16_t intmask;
	uint16_t sysint_st;
	uint8_t channel;
	uint8_t rename_flag;

	unsigned int allow_pw;	/*allow power*/
	int status;

	uint32_t pa_syn_en;
	uint32_t fade_en;
	uint32_t fade_time;
	uint32_t fs;

	uint32_t dsp_fw_len;
	uint32_t dsp_cfg_len;

	struct aw_re_range_desc re_range;

	struct workqueue_struct *work_queue;
	struct delayed_work start_work;
	struct delayed_work load_fw_work;
	struct list_head list_node;
};

/******************************************************
 *
 * aw881xx i2c write/read
 *
 ******************************************************/
int aw881xx_reg_writes(struct aw881xx *aw881xx,
			uint8_t reg_addr, uint8_t *buf, uint16_t len);

int aw881xx_reg_write(struct aw881xx *aw881xx,
			uint8_t reg_addr, uint16_t reg_data);
int aw881xx_reg_read(struct aw881xx *aw881xx,
			uint8_t reg_addr, uint16_t *reg_data);
int aw881xx_reg_write_bits(struct aw881xx *aw881xx,
			uint8_t reg_addr, uint16_t mask, uint16_t reg_data);
int aw881xx_dsp_write(struct aw881xx *aw881xx,
			uint16_t dsp_addr, uint16_t dsp_data);
int aw881xx_dsp_read(struct aw881xx *aw881xx,
			uint16_t dsp_addr, uint16_t *dsp_data);

int aw881xx_dev_get_volume(struct aw881xx *aw881xx, uint32_t *value);
int aw881xx_dev_set_volume(struct aw881xx *aw881xx, uint32_t value);
uint32_t aw881xx_reg_val_to_db(uint32_t value);

int aw881xx_get_dsp_config(struct aw881xx *aw881xx);
int aw881xx_get_iis_status(struct aw881xx *aw881xx);
int aw881xx_get_dsp_status(struct aw881xx *aw881xx);
int aw881xx_get_sysint(struct aw881xx *aw881xx, uint16_t *int_status);
int aw881xx_get_hmute(struct aw881xx *aw881xx);
int aw881xx_get_list_head(struct list_head **head);
void aw881xx_clear_int_status(struct aw881xx *aw881xx);
int aw881xx_get_dev_num(void);
int aw881xx_get_version(char *buf, int size);
int aw881xx_read_dsp_pid(struct aw881xx *aw881xx);
void aw881xx_run_mute(struct aw881xx *aw881xx, bool mute);

#endif
