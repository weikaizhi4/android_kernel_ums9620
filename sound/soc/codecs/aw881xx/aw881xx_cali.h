#ifndef __AW881XX_CALI_H__
#define __AW881XX_CALI_H__

//#define AW_CALI_STORE_EXAMPLE

#define AW_ERRO_CALI_VALUE (0)

#define AW_CALI_RE_DEFAULT_TIMER	(3000)
#define AW_DEV_CH_MAX			(16)
#define AW_DEV_RE_RANGE			(RE_RANGE_NUM * AW_DEV_CH_MAX)
#define AW_CALI_F0_TIME			(5 * 1000)
#define F0_READ_CNT_MAX			(5)
#define AW881XX_FS_CFG_MAX		(11)
#define AW_CALI_CFG_NUM			(4)
#define CALI_READ_CNT_MAX		(8)
#define CALI_DATA_SUM_RM		(2)
#define RE_MAX_DEFAULT			(15000)
#define RE_MIN_DEFAULT			(1000)
#define AW_DSP_RE_TO_SHOW_RE(re)	(((re) * 1000) >> AW881XX_DSP_REG_ST_STE_RE_DECIMAL_SHIFT)
#define AW_SHOW_RE_TO_DSP_RE(re)	(((re) << AW881XX_DSP_REG_ST_STE_RE_DECIMAL_SHIFT) / 1000)
#define AW_TE_CACL_VALUE(te, coil_alpha) (int32_t)(((int32_t)te << 18) / (coil_alpha))
#define AW_RE_REALTIME_VALUE(re_cacl, te_cacl) ((re_cacl) + (int32_t)((int64_t)((te_cacl) * (re_cacl)) >> 14))

#define ADPZ_F0_DOWN_LIMIT  (500)
#define ADPZ_F0_UP_LIMIT    (1000)
#define ADPZ_FS_HZ (48000)
#define DOUBLE_TO_24BIT_INT (16777216)

#define ABS_VALUE(a)  (((a) >=0) ? (a) : (-(a)))
#define AW_F0_CALI_DELAY_CALC(value) (value * 32 / 48)

#define AW88194_DSP_REG_CFG_ADPZ_RA			(0x8678)
#define AW88195_DSP_REG_CFG_ADPZ_RA			(0x8678)

enum {
	CALI_CHECK_DISABLE = 0,
	CALI_CHECK_ENABLE = 1,
};

enum {
	CALI_RESULT_NONE = 0,
	CALI_RESULT_NORMAL = 1,
	CALI_RESULT_ERROR = -1,
};

enum {
	ACOS_PRECISION = 24,
	ACOS_ONE_24BIT = 16777216,
	ACOS_PI2_24BIT = 165584484,
	ACOS_PI_24BIT = 52707178,
	ACOS_COE1_24BIT = 314225,
	ACOS_COE2_24BIT = 1245892,
	ACOS_COE3_24BIT = 3558689,
	ACOS_COE4_24BIT = 26352456,
};

enum {
	ADPZ_FQ_PRECISION = 24,
	ADPZ_FQ_2PRECISION = 48,
	ADPZ_FQ_1INV2PRECISION = 12,
	ADPZ_Q_EXPAND_RATIO = 500,
	ADPZ_INV_2PI_24BIT = 2670176,
	ADPZ_FQ_DOWN_NUM = 3,
	ADPZ_F0_PRECISION = 27,
	ADPZ_FQ_1_24BIT = 16777216,
	ADPZ_COE_MAX
};

enum {
	INV_LOG2_E_Q1DOT31 = 1488522236,
	LOG_PRECISION = 31,
	LOG2_ITERATION_TIMES = 24,
	LOG_PRECISION_MAX = 31,
};

enum {
	SQRT_PRECISION = 24,
	SQRT_ITERATION_BASE = 31,
	SQRT_REMLOW_DECAY = (SQRT_ITERATION_BASE << 1),
};

enum {
	RE_MIN_FLAG = 0,
	RE_MAX_FLAG = 1,
	RE_RANGE_NUM = 2,
};

enum {
	AW_CALI_MODE_NONE = 0,
	AW_CALI_MODE_ATTR,
	AW_CALI_MODE_CLASS,
	AW_CALI_MODE_MISC,
	AW_CALI_MODE_MAX
};

enum {
	AW_CHIP_PID_01 = 0x01,
	AW_CHIP_PID_00 = 0x00,
	AW_CHIP_PID_03 = 0x03,
};

enum {
	GET_RE_TYPE = 0,
	GET_F0_TYPE,
	GET_Q_TYPE,
};

enum {
	CALI_OPS_HMUTE = 0X0001,
	CALI_OPS_NOISE = 0X0002,
};

enum {
	CALI_TYPE_RE = 0,
	CALI_TYPE_F0,
};

enum {
	AW_CALI_CMD_RE = 0,
	AW_CALI_CMD_F0,
	AW_CALI_CMD_RE_F0,
	AW_CALI_CMD_F0_Q,
	AW_CALI_CMD_RE_F0_Q,
};

enum {
	CALI_STR_NONE = 0,
	CALI_STR_CALI_RE_F0,
	CALI_STR_CALI_RE,
	CALI_STR_CALI_F0,
	CALI_STR_SET_RE,
	CALI_STR_SHOW_RE,		/*show cali_re*/
	CALI_STR_SHOW_R0,		/*show real r0*/
	CALI_STR_SHOW_CALI_F0,		/*GET DEV CALI_F0*/
	CALI_STR_SHOW_F0,		/*SHOW REAL F0*/
	CALI_STR_SHOW_TE,
	CALI_STR_DEV_SEL,		/*switch device*/
	CALI_STR_VER,
	CALI_STR_SHOW_RE_RANGE,
	CALI_STR_MAX,
};

struct re_data {
	uint32_t re_range[2];
};

#define AW_IOCTL_MAGIC				'a'

#define AW_IOCTL_GET_F0				_IOWR(AW_IOCTL_MAGIC, 5, int32_t)
#define AW_IOCTL_SET_CALI_RE			_IOWR(AW_IOCTL_MAGIC, 6, int32_t)

#define AW_IOCTL_GET_RE				_IOWR(AW_IOCTL_MAGIC, 17, int32_t)
#define AW_IOCTL_GET_CALI_F0			_IOWR(AW_IOCTL_MAGIC, 18, int32_t)
#define AW_IOCTL_GET_REAL_R0			_IOWR(AW_IOCTL_MAGIC, 19, int32_t)
#define AW_IOCTL_GET_TE				_IOWR(AW_IOCTL_MAGIC, 20, int32_t)
#define AW_IOCTL_GET_RE_RANGE			_IOWR(AW_IOCTL_MAGIC, 21, struct re_data)

typedef struct adapZdriver{
	int32_t real_cal_A1;	// 12bit
	int32_t real_cal_A2;	//12bit
}adapZdriver_t;

struct cali_cfg {
	uint16_t data[AW_CALI_CFG_NUM];
};

struct aw_cali_desc {
	bool status;
	struct cali_cfg cali_cfg;
	uint32_t cali_re;
	uint32_t f0;
	uint32_t q;
	uint32_t ra;
	uint16_t dft_dsp_cfg;
	uint16_t dsp_vol;
	int8_t cali_result;
	uint8_t cali_check_st;
};

void aw881xx_set_cali_re_to_dsp(struct aw_cali_desc *cali_desc);
int aw881xx_get_cali_re(struct aw_cali_desc *cali_desc);
void aw881xx_cali_init(struct aw_cali_desc *cali_desc);
void aw881xx_cali_deinit(struct aw_cali_desc *cali_desc);
bool aw881xx_cali_check_result(struct aw_cali_desc *cali_desc);

#endif
