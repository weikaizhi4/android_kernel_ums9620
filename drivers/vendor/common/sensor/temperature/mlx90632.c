// SPDX-License-Identifier: GPL-2.0
/*
 * mlx90632.c - Melexis MLX90632 contactless IR temperature sensor
 *
 * Copyright (c) 2017 Melexis <cmo@melexis.com>
 *
 * Driver for the Melexis MLX90632 I2C 16-bit IR thermopile sensor
 */
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/math64.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#endif

#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/uaccess.h>

#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/version.h>

#define MLX90632_DEV_CTL
#define MLX90632_DEV_NAME "mlx90632"

#define MLX90632_INPUT_DEV_SUPPORT

/* Memory sections addresses */
#define MLX90632_ADDR_RAM	0x4000 /* Start address of ram */
#define MLX90632_ADDR_EEPROM	0x2480 /* Start address of user eeprom */

/* EEPROM addresses - used at startup */
#define MLX90632_EE_CTRL	0x24d4 /* Control register initial value */
#define MLX90632_EE_I2C_ADDR	0x24d5 /* I2C address register initial value */
#define MLX90632_EE_VERSION	0x240b /* EEPROM version reg address */
#define MLX90632_EE_P_R		0x240c /* P_R calibration register 32bit */
#define MLX90632_EE_P_G		0x240e /* P_G calibration register 32bit */
#define MLX90632_EE_P_T		0x2410 /* P_T calibration register 32bit */
#define MLX90632_EE_P_O		0x2412 /* P_O calibration register 32bit */
#define MLX90632_EE_Aa		0x2414 /* Aa calibration register 32bit */
#define MLX90632_EE_Ab		0x2416 /* Ab calibration register 32bit */
#define MLX90632_EE_Ba		0x2418 /* Ba calibration register 32bit */
#define MLX90632_EE_Bb		0x241a /* Bb calibration register 32bit */
#define MLX90632_EE_Ca		0x241c /* Ca calibration register 32bit */
#define MLX90632_EE_Cb		0x241e /* Cb calibration register 32bit */
#define MLX90632_EE_Da		0x2420 /* Da calibration register 32bit */
#define MLX90632_EE_Db		0x2422 /* Db calibration register 32bit */
#define MLX90632_EE_Ea		0x2424 /* Ea calibration register 32bit */
#define MLX90632_EE_Eb		0x2426 /* Eb calibration register 32bit */
#define MLX90632_EE_Fa		0x2428 /* Fa calibration register 32bit */
#define MLX90632_EE_Fb		0x242a /* Fb calibration register 32bit */
#define MLX90632_EE_Ga		0x242c /* Ga calibration register 32bit */

#define MLX90632_EE_Gb		0x242e /* Gb calibration register 16bit */
#define MLX90632_EE_Ka		0x242f /* Ka calibration register 16bit */

#define MLX90632_EE_Ha		0x2481 /* Ha customer calib value reg 16bit */
#define MLX90632_EE_Hb		0x2482 /* Hb customer calib value reg 16bit */

#define MLX90632_EE_Me1		0x24E1 /* Measurement setting 1 register 16bit */
#define MLX90632_EE_Me2		0x24E2 /* Measurement setting 2 register 16bit */
#define MLX90632_REFRESH_MASK		GENMASK(10, 8) /* Refresh rate Mask */

#define MLX90632_EE_Unlock_Addr		0x3005 /* EEPROM unlock for customer access */
#define MLX90632_EE_Unlock_Cmd		0x554C /* EEPROM unlock for customer access */

#define MLX90632_Default_Ha		0x4000 /* Ha default value */
#define MLX90632_Default_Hb		0x00   /* Hb default value */

#define MLX90632_Ha_Hb_Max		0xFFFF /* Ha Hb max value */
#define MLX90632_Ha_Hb_Min		0x0   /* Ha Hb min value */

/* Register addresses - volatile */
#define MLX90632_REG_I2C_ADDR	0x3000 /* Chip I2C address register */

/* Control register address - volatile */
#define MLX90632_REG_CONTROL	0x3001 /* Control Register address */
#define   MLX90632_CFG_PWR_MASK		GENMASK(2, 1) /* PowerMode Mask */
/* PowerModes statuses */
#define MLX90632_PWR_STATUS(ctrl_val) (ctrl_val << 1)
#define MLX90632_PWR_STATUS_HALT MLX90632_PWR_STATUS(0) /* hold */
#define MLX90632_PWR_STATUS_SLEEP_STEP MLX90632_PWR_STATUS(1) /* sleep step*/
#define MLX90632_PWR_STATUS_STEP MLX90632_PWR_STATUS(2) /* step */
#define MLX90632_PWR_STATUS_CONTINUOUS MLX90632_PWR_STATUS(3) /* continuous*/

/* Device status register - volatile */
#define MLX90632_REG_STATUS	0x3fff /* Device status register */
#define   MLX90632_STAT_BUSY		BIT(10) /* Device busy indicator */
#define   MLX90632_STAT_EE_BUSY		BIT(9) /* EEPROM busy indicator */
#define   MLX90632_STAT_BRST		BIT(8) /* Brown out reset indicator */
#define   MLX90632_STAT_CYCLE_POS	GENMASK(6, 2) /* Data position */
#define   MLX90632_STAT_DATA_RDY	BIT(0) /* Data ready indicator */

/* RAM_MEAS address-es for each channel */
#define MLX90632_RAM_1(meas_num)	(MLX90632_ADDR_RAM + 3 * meas_num)
#define MLX90632_RAM_2(meas_num)	(MLX90632_ADDR_RAM + 3 * meas_num + 1)
#define MLX90632_RAM_3(meas_num)	(MLX90632_ADDR_RAM + 3 * meas_num + 2)

/* Magic constants */
#define MLX90632_ID_MEDICAL	0x0105 /* EEPROM DSPv5 Medical device id */
#define MLX90632_ID_CONSUMER	0x0205 /* EEPROM DSPv5 Consumer device id */
#define MLX90632_DSP_VERSION	5 /* DSP version */
#define MLX90632_DSP_MASK	GENMASK(7, 0) /* DSP version in EE_VERSION */
#define MLX90632_RESET_CMD	0x0006 /* Reset sensor (address or global) */
#define MLX90632_REF_12		12LL /**< ResCtrlRef value of Ch 1 or Ch 2 */
#define MLX90632_REF_3		12LL /**< ResCtrlRef value of Channel 3 */
#define MLX90632_MAX_MEAS_NUM	31 /**< Maximum measurements in list */
#define MLX90632_SLEEP_DELAY_MS 3000 /**< Autosleep delay */


#ifdef MLX90632_INPUT_DEV_SUPPORT
#define TEMP_INTERNAL_MOD 1
#define TEMP_OBJECT_MOD  2

#define MLX90632_DRV_NAME         "temperature"
#define INPUT_NAME_MLX90632       "temperature"

#define MLX90632_CAL_HA_HB_FILE_PATH        "/persist/sensors/ir_temp_calibrate_ha_hb"

static struct class *mlx90632_class_input;
#endif

struct mlx90632_data {
	struct i2c_client *client;
	struct mutex lock; /* Multiple reads for single measurement */
	struct regmap *regmap;
	u16 emissivity;
	int vdd_gpio;
	int vddio_gpio;
	int power_type;
	int power_enabled;
	struct regulator *temp_reg;
	struct device *mlx90632_data_dev;
#ifdef MLX90632_DEV_CTL
	struct cdev cdev;
	struct list_head device_entry;
	dev_t devt;
	unsigned int	users;
#endif
#ifdef MLX90632_INPUT_DEV_SUPPORT
	struct work_struct	mlx_work; /* for mlx temperature polling */
	struct workqueue_struct *mlx_wq;
	struct hrtimer mlx_timer;
	ktime_t mlx_poll_delay;	/* needed for temperature sensor polling */

	dev_t devt_in;
	struct input_dev *mlx90632_input_dev;

	bool mlx_enable; /* mlx enable,it will set hrtimer to report temp data */
	uint8_t enable_body; /* enable_body, for sensor-hal enable body temperature */
	uint8_t enable_object; /* enable_object, for sensor-hal enable object temperature */
	bool fac_calibrated;
	uint32_t mlx_Ha; /* Ha customer calib value reg 16bit */
	uint32_t mlx_Hb; /* Hb customer calib value reg 16bit */
	struct mutex data_lock; /* data lock for set data safe */
#endif
};

static struct mlx90632_data *mlx90632;

static const struct regmap_range mlx90632_volatile_reg_range[] = {
	regmap_reg_range(MLX90632_REG_I2C_ADDR, MLX90632_REG_CONTROL),
	regmap_reg_range(MLX90632_REG_STATUS, MLX90632_REG_STATUS),
	regmap_reg_range(MLX90632_RAM_1(0),
	MLX90632_RAM_3(MLX90632_MAX_MEAS_NUM)),
};

static const struct regmap_access_table mlx90632_volatile_regs_tbl = {
	.yes_ranges = mlx90632_volatile_reg_range,
	.n_yes_ranges = ARRAY_SIZE(mlx90632_volatile_reg_range),
};

static const struct regmap_range mlx90632_read_reg_range[] = {
	regmap_reg_range(MLX90632_EE_VERSION, MLX90632_EE_Ka),
	regmap_reg_range(MLX90632_EE_CTRL, MLX90632_EE_I2C_ADDR),
	regmap_reg_range(MLX90632_EE_Ha, MLX90632_EE_Hb),
	regmap_reg_range(MLX90632_EE_Me1, MLX90632_EE_Me2),
	regmap_reg_range(MLX90632_REG_I2C_ADDR, MLX90632_REG_CONTROL),
	regmap_reg_range(MLX90632_REG_STATUS, MLX90632_REG_STATUS),
	regmap_reg_range(MLX90632_RAM_1(0),
	MLX90632_RAM_3(MLX90632_MAX_MEAS_NUM)),
};

static const struct regmap_access_table mlx90632_readable_regs_tbl = {
	.yes_ranges = mlx90632_read_reg_range,
	.n_yes_ranges = ARRAY_SIZE(mlx90632_read_reg_range),
};

static const struct regmap_range mlx90632_no_write_reg_range[] = {
	regmap_reg_range(MLX90632_EE_VERSION, MLX90632_EE_Ka),
	regmap_reg_range(MLX90632_RAM_1(0),
	MLX90632_RAM_3(MLX90632_MAX_MEAS_NUM)),
};

static const struct regmap_access_table mlx90632_writeable_regs_tbl = {
	.no_ranges = mlx90632_no_write_reg_range,
	.n_no_ranges = ARRAY_SIZE(mlx90632_no_write_reg_range),
};

static const struct regmap_config mlx90632_regmap = {
	.reg_bits = 16,
	.val_bits = 16,

	.volatile_table = &mlx90632_volatile_regs_tbl,
	.rd_table = &mlx90632_readable_regs_tbl,
	.wr_table = &mlx90632_writeable_regs_tbl,

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
	.use_single_rw = true,
#else
	.use_single_read = true,
	.use_single_write = true,
#endif
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.cache_type = REGCACHE_RBTREE,
};

static int mlx90632_sleep(struct mlx90632_data *data);
static int mlx90632_wakeup(struct mlx90632_data *data);
static int mlx90632_stepmode(struct mlx90632_data *data);


static s32 mlx90632_pwr_set_sleep_step(struct regmap *regmap)
{
	return regmap_update_bits(regmap, MLX90632_REG_CONTROL,
				  MLX90632_CFG_PWR_MASK,
				  MLX90632_PWR_STATUS_SLEEP_STEP);
}

static s32 mlx90632_pwr_continuous(struct regmap *regmap)
{
	return regmap_update_bits(regmap, MLX90632_REG_CONTROL,
				  MLX90632_CFG_PWR_MASK,
				  MLX90632_PWR_STATUS_CONTINUOUS);
}

static s32 mlx90632_pwr_stepmode(struct regmap *regmap)
{
	return regmap_update_bits(regmap, MLX90632_REG_CONTROL,
				  MLX90632_CFG_PWR_MASK,
				  MLX90632_PWR_STATUS_STEP);
}

/**
 * mlx90632_perform_measurement - Trigger and retrieve current measurement cycle
 * @*data: pointer to mlx90632_data object containing regmap information
 *
 * Perform a measurement and return latest measurement cycle position reported
 * by sensor. This is a blocking function for 500ms, as that is default sensor
 * refresh rate.
 */
static int mlx90632_perform_measurement(struct mlx90632_data *data)
{
	int ret, tries = 100;
	unsigned int reg_status;

	ret = regmap_update_bits(data->regmap, MLX90632_REG_STATUS,
				 MLX90632_STAT_DATA_RDY, 0);
	if (ret < 0)
		return ret;

	while (tries-- > 0) {
		ret = regmap_read(data->regmap, MLX90632_REG_STATUS,
				  &reg_status);
		if (ret < 0)
			return ret;
		if (reg_status & MLX90632_STAT_DATA_RDY)
			break;
		usleep_range(10000, 11000);
	}

	if (tries < 0) {
		dev_err(&data->client->dev, "data not ready");
		return -ETIMEDOUT;
	}

	return (reg_status & MLX90632_STAT_CYCLE_POS) >> 2;
}

static int mlx90632_channel_new_select(int perform_ret, uint8_t *channel_new,
				       uint8_t *channel_old)
{
	switch (perform_ret) {
	case 1:
		*channel_new = 1;
		*channel_old = 2;
		break;
	case 2:
		*channel_new = 2;
		*channel_old = 1;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int mlx90632_read_ambient_raw(struct regmap *regmap,
				     s16 *ambient_new_raw, s16 *ambient_old_raw)
{
	int ret;
	unsigned int read_tmp;

	ret = regmap_read(regmap, MLX90632_RAM_3(1), &read_tmp);
	if (ret < 0)
		return ret;
	*ambient_new_raw = (s16)read_tmp;

	ret = regmap_read(regmap, MLX90632_RAM_3(2), &read_tmp);
	if (ret < 0)
		return ret;
	*ambient_old_raw = (s16)read_tmp;

	return ret;
}

static int mlx90632_read_object_raw(struct regmap *regmap,
				    int perform_measurement_ret,
				    s16 *object_new_raw, s16 *object_old_raw)
{
	int ret;
	unsigned int read_tmp;
	s16 read;
	u8 channel = 0;
	u8 channel_old = 0;

	ret = mlx90632_channel_new_select(perform_measurement_ret, &channel,
					  &channel_old);
	if (ret != 0)
		return ret;

	ret = regmap_read(regmap, MLX90632_RAM_2(channel), &read_tmp);
	if (ret < 0)
		return ret;

	read = (s16)read_tmp;

	ret = regmap_read(regmap, MLX90632_RAM_1(channel), &read_tmp);
	if (ret < 0)
		return ret;
	*object_new_raw = (read + (s16)read_tmp) / 2;

	ret = regmap_read(regmap, MLX90632_RAM_2(channel_old), &read_tmp);
	if (ret < 0)
		return ret;
	read = (s16)read_tmp;

	ret = regmap_read(regmap, MLX90632_RAM_1(channel_old), &read_tmp);
	if (ret < 0)
		return ret;
	*object_old_raw = (read + (s16)read_tmp) / 2;

	return ret;
}

static int mlx90632_read_all_channel(struct mlx90632_data *data,
				     s16 *ambient_new_raw, s16 *ambient_old_raw,
				     s16 *object_new_raw, s16 *object_old_raw)
{
	s32 ret, measurement;

	mutex_lock(&data->lock);
	measurement = mlx90632_perform_measurement(data);
	if (measurement < 0) {
		ret = measurement;
		goto read_unlock;
	}
	ret = mlx90632_read_ambient_raw(data->regmap, ambient_new_raw,
					ambient_old_raw);
	if (ret < 0)
		goto read_unlock;

	ret = mlx90632_read_object_raw(data->regmap, measurement,
				       object_new_raw, object_old_raw);
read_unlock:
	mutex_unlock(&data->lock);
	return ret;
}

static int mlx90632_read_ee_register(struct regmap *regmap, u16 reg_lsb,
				     s32 *reg_value)
{
	s32 ret;
	unsigned int read;
	u32 value;

	ret = regmap_read(regmap, reg_lsb, &read);
	if (ret < 0)
		return ret;

	value = read;

	ret = regmap_read(regmap, reg_lsb + 1, &read);
	if (ret < 0)
		return ret;

	*reg_value = (read << 16) | (value & 0xffff);

	return 0;
}

static s64 mlx90632_preprocess_temp_amb(s16 ambient_new_raw,
					s16 ambient_old_raw, s16 Gb)
{
	s64 VR_Ta, kGb, tmp;

	kGb = ((s64)Gb * 1000LL) >> 10ULL;
	VR_Ta = (s64)ambient_old_raw * 1000000LL +
		kGb * div64_s64(((s64)ambient_new_raw * 1000LL),
				(MLX90632_REF_3));
	tmp = div64_s64(
		      div64_s64(((s64)ambient_new_raw * 1000000000000LL),
				(MLX90632_REF_3)), VR_Ta);
	return div64_s64(tmp << 19ULL, 1000LL);
}

static s64 mlx90632_preprocess_temp_obj(s16 object_new_raw, s16 object_old_raw,
					s16 ambient_new_raw,
					s16 ambient_old_raw, s16 Ka)
{
	s64 VR_IR, kKa, tmp;

	kKa = ((s64)Ka * 1000LL) >> 10ULL;
	VR_IR = (s64)ambient_old_raw * 1000000LL +
		kKa * div64_s64(((s64)ambient_new_raw * 1000LL),
				(MLX90632_REF_3));
	tmp = div64_s64(
		      div64_s64(((s64)((object_new_raw))
				 * 1000000000000LL), (MLX90632_REF_12)),
		      VR_IR);
	return div64_s64((tmp << 19ULL), 1000LL);
}

static s32 mlx90632_calc_temp_ambient(s16 ambient_new_raw, s16 ambient_old_raw,
				      s32 P_T, s32 P_R, s32 P_G, s32 P_O,
				      s16 Gb)
{
	s64 Asub, Bsub, Ablock, Bblock, Cblock, AMB, sum;

	AMB = mlx90632_preprocess_temp_amb(ambient_new_raw, ambient_old_raw,
					   Gb);
	Asub = ((s64)P_T * 10000000000LL) >> 44ULL;
	Bsub = AMB - (((s64)P_R * 1000LL) >> 8ULL);
	Ablock = Asub * (Bsub * Bsub);
	Bblock = (div64_s64(Bsub * 10000000LL, P_G)) << 20ULL;
	Cblock = ((s64)P_O * 10000000000LL) >> 8ULL;

	sum = div64_s64(Ablock, 1000000LL) + Bblock + Cblock;

	return div64_s64(sum, 10000000LL);
}

static s32 mlx90632_calc_temp_object_iteration(s32 prev_object_temp, s64 object,
		s64 TAdut, s32 Fa, s32 Fb,
		s32 Ga, s16 Ha, s16 Hb,
		u16 emissivity)
{
	s64 calcedKsTO, calcedKsTA, ir_Alpha, TAdut4, Alpha_corr;
	s64 Ha_customer, Hb_customer;

	Ha_customer = ((s64)Ha * 1000000LL) >> 14ULL;
	Hb_customer = ((s64)Hb * 100) >> 10ULL;

	calcedKsTO = ((s64)((s64)Ga * (prev_object_temp - 25 * 1000LL)
			    * 1000LL)) >> 36LL;
	calcedKsTA = ((s64)(Fb * (TAdut - 25 * 1000000LL))) >> 36LL;
	Alpha_corr = div64_s64((((s64)(Fa * 10000000000LL) >> 46LL)
				* Ha_customer), 1000LL);
	Alpha_corr *= ((s64)(1 * 1000000LL + calcedKsTO + calcedKsTA));
	Alpha_corr = emissivity * div64_s64(Alpha_corr, 100000LL);
	Alpha_corr = div64_s64(Alpha_corr, 1000LL);
	ir_Alpha = div64_s64((s64)object * 10000000LL, Alpha_corr);
	TAdut4 = (div64_s64(TAdut, 10000LL) + 27315) *
		 (div64_s64(TAdut, 10000LL) + 27315) *
		 (div64_s64(TAdut, 10000LL)  + 27315) *
		 (div64_s64(TAdut, 10000LL) + 27315);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
	return (int_sqrt64(int_sqrt64(ir_Alpha * 1000000000000LL + TAdut4))
		- 27315 - Hb_customer) * 10;
#else
	return (int_sqrt(int_sqrt(ir_Alpha * 1000000000000LL + TAdut4))
		- 27315 - Hb_customer) * 10;
#endif
}

static s32 mlx90632_calc_temp_object(s64 object, s64 ambient, s32 Ea, s32 Eb,
				     s32 Fa, s32 Fb, s32 Ga, s16 Ha, s16 Hb,
				     u16 tmp_emi)
{
	s64 kTA, kTA0, TAdut;
	s64 temp = 25000;
	s8 i;

	kTA = (Ea * 1000LL) >> 16LL;
	kTA0 = (Eb * 1000LL) >> 8LL;
	TAdut = div64_s64(((ambient - kTA0) * 1000000LL), kTA) + 25 * 1000000LL;

	/* Iterations of calculation as described in datasheet */
	for (i = 0; i < 5; ++i) {
		temp = mlx90632_calc_temp_object_iteration(temp, object, TAdut,
				Fa, Fb, Ga, Ha, Hb,
				tmp_emi);
	}
	return temp;
}

static int mlx90632_calc_object_dsp105(struct mlx90632_data *data, int *val)
{
	s32 ret;
	s32 Ea, Eb, Fa, Fb, Ga;
	unsigned int read_tmp;
	s16 Ha, Hb, Gb, Ka;
	s16 ambient_new_raw, ambient_old_raw, object_new_raw, object_old_raw;
	s64 object, ambient;

	ret = mlx90632_read_ee_register(data->regmap, MLX90632_EE_Ea, &Ea);
	if (ret < 0)
		return ret;
	ret = mlx90632_read_ee_register(data->regmap, MLX90632_EE_Eb, &Eb);
	if (ret < 0)
		return ret;
	ret = mlx90632_read_ee_register(data->regmap, MLX90632_EE_Fa, &Fa);
	if (ret < 0)
		return ret;
	ret = mlx90632_read_ee_register(data->regmap, MLX90632_EE_Fb, &Fb);
	if (ret < 0)
		return ret;
	ret = mlx90632_read_ee_register(data->regmap, MLX90632_EE_Ga, &Ga);
	if (ret < 0)
		return ret;
	if (data->fac_calibrated == true) {
		Ha = (s16)data->mlx_Ha;
		Hb = (s16)data->mlx_Hb;
	} else {

		ret = regmap_read(data->regmap, MLX90632_EE_Ha, &read_tmp);
		if (ret < 0)
			return ret;
		Ha = (s16)read_tmp;
		ret = regmap_read(data->regmap, MLX90632_EE_Hb, &read_tmp);
		if (ret < 0)
			return ret;
		Hb = (s16)read_tmp;
	}
	ret = regmap_read(data->regmap, MLX90632_EE_Gb, &read_tmp);
	if (ret < 0)
		return ret;
	Gb = (s16)read_tmp;
	ret = regmap_read(data->regmap, MLX90632_EE_Ka, &read_tmp);
	if (ret < 0)
		return ret;
	Ka = (s16)read_tmp;

	ret = mlx90632_read_all_channel(data,
					&ambient_new_raw, &ambient_old_raw,
					&object_new_raw, &object_old_raw);
	if (ret < 0)
		return ret;

	ambient = mlx90632_preprocess_temp_amb(ambient_new_raw,
					       ambient_old_raw, Gb);
	object = mlx90632_preprocess_temp_obj(object_new_raw,
					      object_old_raw,
					      ambient_new_raw,
					      ambient_old_raw, Ka);

	*val = mlx90632_calc_temp_object(object, ambient, Ea, Eb, Fa, Fb, Ga,
					 Ha, Hb, data->emissivity);
	return 0;
}

static int mlx90632_calc_ambient_dsp105(struct mlx90632_data *data, int *val)
{
	s32 ret;
	unsigned int read_tmp;
	s32 PT, PR, PG, PO;
	s16 Gb;
	s16 ambient_new_raw, ambient_old_raw;

	ret = mlx90632_read_ee_register(data->regmap, MLX90632_EE_P_R, &PR);
	if (ret < 0)
		return ret;
	ret = mlx90632_read_ee_register(data->regmap, MLX90632_EE_P_G, &PG);
	if (ret < 0)
		return ret;
	ret = mlx90632_read_ee_register(data->regmap, MLX90632_EE_P_T, &PT);
	if (ret < 0)
		return ret;
	ret = mlx90632_read_ee_register(data->regmap, MLX90632_EE_P_O, &PO);
	if (ret < 0)
		return ret;
	ret = regmap_read(data->regmap, MLX90632_EE_Gb, &read_tmp);
	if (ret < 0)
		return ret;
	Gb = (s16)read_tmp;

	ret = mlx90632_read_ambient_raw(data->regmap, &ambient_new_raw,
					&ambient_old_raw);
	if (ret < 0)
		return ret;
	*val = mlx90632_calc_temp_ambient(ambient_new_raw, ambient_old_raw,
					  PT, PR, PG, PO, Gb);
	return ret;
}

static int mlx90632_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *channel, int *val,
			     int *val2, long mask)
{
	struct mlx90632_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		switch (channel->channel2) {
		case IIO_MOD_TEMP_AMBIENT:
			ret = mlx90632_calc_ambient_dsp105(data, val);
			pr_info("%s,TA ret:%d,channel2:%d, val:%d\n", __func__, ret, channel->channel2, *val);
			if (ret < 0)
				return ret;
			return IIO_VAL_INT;
		case IIO_MOD_TEMP_OBJECT:
			ret = mlx90632_calc_object_dsp105(data, val);
			pr_info("%s,TO ret:%d,channel2:%d, val:%d\n", __func__, ret, channel->channel2, *val);
			if (ret < 0)
				return ret;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_CALIBEMISSIVITY:
		if (data->emissivity == 1000) {
			*val = 1;
			*val2 = 0;
		} else {
			*val = 0;
			*val2 = data->emissivity * 1000;
		}
		return IIO_VAL_INT_PLUS_MICRO;

	default:
		return -EINVAL;
	}
}

static int mlx90632_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *channel, int val,
			      int val2, long mask)
{
	struct mlx90632_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_CALIBEMISSIVITY:
		/* Confirm we are within 0 and 1.0 */
		if (val < 0 || val2 < 0 || val > 1 ||
		    (val == 1 && val2 != 0))
			return -EINVAL;
		data->emissivity = val * 1000 + val2 / 1000;
		return 0;
	default:
		return -EINVAL;
	}
}

static ssize_t mlx90632_show_mode(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct mlx90632_data *data = iio_priv(indio_dev);
	uint32_t  read_tmp;
	int ret;

	mutex_lock(&data->lock);
	ret = regmap_read(data->regmap, MLX90632_REG_CONTROL, &read_tmp);
	mutex_unlock(&data->lock);
	if (ret < 0)
		return ret;

	pr_info("%s MLX90632_REG_CONTROL is 0x%x\n", __func__, read_tmp);
	return scnprintf(buf, sizeof(uint32_t), "0x%x\n", read_tmp);
}

static ssize_t mlx90632_write_mode(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct mlx90632_data *data = iio_priv(indio_dev);
	int ret = 0, val = 0;

	ret = sscanf(buf, "%d", &val);
	if (ret != 1) {
		pr_err("%s Parameter invalid, ret=%d\n", __func__, ret);
		goto exit;
	}

	switch (val) {
	case 1:
		ret = mlx90632_sleep(data);
		break;
	case 2:
		ret = mlx90632_stepmode(data);
		break;
	case 3:
		ret = mlx90632_wakeup(data);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pr_info("%s val=%d, ret=%d\n", __func__, val, ret);

exit:
	return ret;
}


static IIO_DEVICE_ATTR(mode_control, 0664,
		       mlx90632_show_mode, mlx90632_write_mode, 0);

static struct attribute *mlx90632_attributes[] = {
	&iio_dev_attr_mode_control.dev_attr.attr,
	NULL,
};


static const struct attribute_group mlx90632_attribute_group = {
	.attrs = mlx90632_attributes,
};

static int mlx90632_debug_reg(struct iio_dev *indio_dev,
			      uint32_t reg, uint32_t writeval, uint32_t *readval)
{
	struct mlx90632_data *data = iio_priv(indio_dev);
	int ret = 0;

	if (!readval) {
		ret = regmap_write(data->regmap, reg, writeval);
	} else {
		ret = regmap_read(data->regmap, reg, readval);
	}

	if ((ret >= 0) && (readval != NULL)) {
		pr_info("%s ret=%d reg:[0x%x]=0x%x\n", __func__, ret, reg, *readval);
		ret = 0;
	}

	pr_info("%s reg:0x%x, writeval:0x%x, ret=%d\n", __func__, reg, writeval, ret);
	return ret;
}


static const struct iio_chan_spec mlx90632_channels[] = {
	{
		.type = IIO_TEMP,
		.modified = 1,
		.channel2 = IIO_MOD_TEMP_AMBIENT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
	},
	{
		.type = IIO_TEMP,
		.modified = 1,
		.channel2 = IIO_MOD_TEMP_OBJECT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |
		BIT(IIO_CHAN_INFO_CALIBEMISSIVITY),
	},
};

static const struct iio_info mlx90632_info = {
	.read_raw = mlx90632_read_raw,
	.write_raw = mlx90632_write_raw,
	.attrs = &mlx90632_attribute_group,
	.debugfs_reg_access = mlx90632_debug_reg,
};

static int mlx90632_sleep(struct mlx90632_data *data)
{
	int ret = 0;

	dev_dbg(&data->client->dev, "Requesting sleep");
	ret = mlx90632_pwr_set_sleep_step(data->regmap);
	if (ret < 0) {
		dev_err(&data->client->dev,
			"Failed to set sleep mode: %d\n", ret);
		goto exit;
	}
	regcache_mark_dirty(data->regmap);
exit:
	pr_info("%s Requesting Sleep, ret:%d\n", __func__, ret);
	return ret;
}

static int mlx90632_wakeup(struct mlx90632_data *data)
{
	int ret = 0;

	ret = regcache_sync(data->regmap);
	if (ret < 0) {
		dev_err(&data->client->dev,
			"Failed to sync regmap registers: %d\n", ret);
		return ret;
	}

	ret = mlx90632_pwr_continuous(data->regmap);

	pr_info("%s Requesting wake-up, et:%d\n", __func__, ret);

	return ret;
}

static int mlx90632_stepmode(struct mlx90632_data *data)
{
	int ret = 0;

	ret = regcache_sync(data->regmap);
	if (ret < 0) {
		dev_err(&data->client->dev,
			"Failed to sync regmap registers: %d\n", ret);
		return ret;
	}


	ret = mlx90632_pwr_stepmode(data->regmap);
	pr_info("%s Requesting stepmode, ret:%d\n", __func__, ret);

	return ret;
}



static int mlx90632_power_init(struct mlx90632_data *pdata,  struct device *dev)
{
	int ret = 0;

	pr_info("%s, start\n", __func__);

	pdata->temp_reg = regulator_get(dev, "temp_vdd");

	if (IS_ERR(pdata->temp_reg)) {
		pr_err("%s get regulator failed\n", __func__);
		return IS_ERR(pdata->temp_reg);
	}

	ret = regulator_set_voltage(pdata->temp_reg, 3300000, 3300000);
	if (ret) {
		pr_err("%s regulator_set_voltage(%d)\n", __func__, ret);
		goto err;
	}
	ret = regulator_enable(pdata->temp_reg);
	if (ret) {
		pr_err("%s regulator enable failed(%d)\n", __func__, ret);
		goto err;
	}
	pr_info("%s, success\n", __func__);

	return 0;

err:
	regulator_put(pdata->temp_reg);
	pdata->temp_reg = NULL;
	return ret;
}


static void mlx90632_cleanup(struct mlx90632_data *pdata)
{
	pr_info("%s pdata->power_type:%d\n", __func__, pdata->power_type);

	if (pdata->power_type == 1) {
		/*  powered by pmic regulator */
		/*
		if (pdata->temp_reg != NULL){
			regulator_put(pdata->temp_reg);
			pdata->temp_reg = NULL;
		}
		*/
	} else {
		if (gpio_is_valid(pdata->vdd_gpio)) {
			gpio_set_value(pdata->vdd_gpio, 0);
			gpio_free(pdata->vdd_gpio);
			pr_info("set power low and remove vdd_gpio success\n");
			pdata->vdd_gpio = 0;
		}
		if (gpio_is_valid(pdata->vddio_gpio)) {
			gpio_set_value(pdata->vddio_gpio, 0);
			gpio_free(pdata->vddio_gpio);
			pr_info("set power low and remove vddio_gpio success\n");
			pdata->vddio_gpio = 0;
		}
	}

	pr_info("%s end\n", __func__);
}

static int mlx90632_parse_dt(struct mlx90632_data *pdata,  struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device_node *dNode = dev->of_node;
	int ret = 0;

	if ((dNode == NULL) || (dev == NULL))
		return -ENODEV;


	/* get power type from dts config */
	of_property_read_u32(dNode, "power-type", &pdata->power_type);
	pr_info("%s, get power type[%d] from dts\n", __func__, pdata->power_type);

	if (pdata->power_type == 1) {
		/*  powered by pmic regulator */
		ret = mlx90632_power_init(pdata, dev);
		if (ret) {
			pr_err("%s, mlx90632_power_init failed.\n", __func__);
		}
	} else {
		/*---powered by gpio control---*/
		pdata->vdd_gpio = of_get_named_gpio(dNode, "temp,vdd-gpio", 0);
		pr_info("%s pdata->vdd_gpio:%d\n", __func__, pdata->vdd_gpio);

		if (gpio_is_valid(pdata->vdd_gpio)) {
			ret = gpio_request(pdata->vdd_gpio, "mlx90632_vdd");
			if (ret) {
				pr_err("%s failed to request vdd gpio!rc = %d\n", __func__, ret);
			} else {
				pr_info("%s success to request vdd gpio!\n", __func__);
				gpio_direction_output(pdata->vdd_gpio, 1);
			}
		}
		pdata->vddio_gpio = of_get_named_gpio(dNode, "temp,vddio-gpio", 0);
		pr_info("%s pdata->vddio_gpio:%d\n", __func__, pdata->vddio_gpio);

		if (gpio_is_valid(pdata->vddio_gpio)) {
			ret = gpio_request(pdata->vddio_gpio, "mlx90632_vddio");
			if (ret) {
				pr_err("%s failed to request vddio gpio!rc = %d\n", __func__, ret);
			} else {
				pr_info("%s success to request vddio gpio!\n", __func__);
				gpio_direction_output(pdata->vddio_gpio, 1);
			}
		}
	}

	msleep(20);
	/*
	ret = mlx90632_config_i2c_mode(pdata, client);
	if (ret != 0) {
		pr_err("%s, mlx90632_config_i2c_mode failed, ret:%d\n", __func__, ret);
	}
	*/

	pr_info("%s pdata->pwr_gpio:%d, ret:%d\n", __func__, pdata->vdd_gpio, ret);

	return ret;
}


static int mlx90632_power_on(struct mlx90632_data *pdata)
{
	int ret = 0;
	int gpio_value = 0;

	if (pdata == NULL)
		return -ENODEV;

	/* power type 0: gpio enable, 1: regulator enable  */
	pr_info("%s, power type[%d] power_enabled[%d]\n", __func__, pdata->power_type, pdata->power_enabled);
	if (pdata->power_enabled == false) {
		if (pdata->power_type == 1) {
			/*  powered by pmic regulator */
			ret = regulator_enable(pdata->temp_reg);
			if (ret) {
				pr_err("%s regulator enable failed(%d)\n", __func__, ret);
				goto err;
			}
		} else {
			/*---powered by gpio control---*/
			if (gpio_is_valid(pdata->vdd_gpio)) {
				gpio_set_value(pdata->vdd_gpio, 1);
				gpio_value = gpio_get_value(pdata->vdd_gpio);
				pr_info("%s vdd gpio value is %d\n", __func__, gpio_value);
			}
			usleep_range(5000, 6000);
			if (gpio_is_valid(pdata->vddio_gpio)) {
				gpio_set_value(pdata->vddio_gpio, 1);
				gpio_value = gpio_get_value(pdata->vddio_gpio);
				pr_info("%s vddio gpio value is %d\n", __func__, gpio_value);
			}

		}

		usleep_range(75000, 76000);
		pdata->power_enabled = true;
	}

err:
	return ret;
}


static int mlx90632_power_off(struct mlx90632_data *pdata)
{
	int ret = 0;
	int gpio_value = 0;

	if (pdata == NULL)
		return -ENODEV;

	/* FIXME Later: For the IR chip power-on bug, currently not power off during sleep. */
	/* power type 0: gpio enable, 1: regulator enable  */
	pr_info("%s, power type[%d]\n", __func__, pdata->power_type);

	if (pdata->power_type == 1) {
		ret = regulator_disable(pdata->temp_reg);
		if (ret) {
			pr_err("%s regulator enable failed(%d)\n", __func__, ret);
			goto err;
		}
	} else {
		if (gpio_is_valid(pdata->vddio_gpio)) {
			gpio_set_value(pdata->vddio_gpio, 0);
			gpio_value = gpio_get_value(pdata->vddio_gpio);
			pr_info("%s vddio power gpio value is %d\n", __func__, gpio_value);
		}
		usleep_range(5000, 6000);
		if (gpio_is_valid(pdata->vdd_gpio)) {
			gpio_set_value(pdata->vdd_gpio, 0);
			gpio_value = gpio_get_value(pdata->vdd_gpio);
			pr_info("%s vdd power gpio value is %d\n", __func__, gpio_value);
		}
	}
	pdata->power_enabled = false;
err:
	return ret;
}



#ifdef MLX90632_DEV_CTL

typedef struct temp_reg_operation {
	uint32_t reg;
	uint32_t val;
} temp_reg_operation_t;



#define MLX90632_IOC_MAGIC    'm'     /* define magic number */
#define MLX90632_IOC_INIT              _IOR(MLX90632_IOC_MAGIC, 0)
#define MLX90632_IOC_EXIT              _IO(MLX90632_IOC_MAGIC, 1)
#define MLX90632_IOC_SLEEP_MODE        _IO(MLX90632_IOC_MAGIC, 2)
#define MLX90632_IOC_STEP_MODE         _IO(MLX90632_IOC_MAGIC, 3)
#define MLX90632_IOC_CONTINUOUS_MODE    _IO(MLX90632_IOC_MAGIC, 4)
#define MLX90632_IOC_READ_TA           _IOR(MLX90632_IOC_MAGIC, 5, int32_t)
#define MLX90632_IOC_READ_TO           _IOR(MLX90632_IOC_MAGIC, 6, int32_t)
#define MLX90632_IOC_MODE              _IOW(MLX90632_IOC_MAGIC, 7, int32_t)
#define MLX90632_IOC_POWER_ON          _IO(MLX90632_IOC_MAGIC, 8)
#define MLX90632_IOC_POWER_OFF         _IO(MLX90632_IOC_MAGIC, 9)
#define MLX90632_IOC_READ_REG          _IOWR(MLX90632_IOC_MAGIC, 10, temp_reg_operation_t)
#define MLX90632_IOC_WRITE_REG         _IOW(MLX90632_IOC_MAGIC, 11, temp_reg_operation_t)
#define MLX90632_IOC_WRITE_CAL         _IOW(MLX90632_IOC_MAGIC, 12, temp_reg_operation_t)
#define MLX90632_IOC_CLEAR_CAL         _IO(MLX90632_IOC_MAGIC, 13)

static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);


/*****************************************************************
 * Static variables
 *****************************************************************/
static struct class *mlx90632_class;

/*****************************************************************
 * Global variable or extern global variabls/functions
 *****************************************************************/
/* struct mlx90632_i2c_dev *mlx90632_i2c_dev_tt; */

/*****************************************************************
 * Static function prototypes
 *****************************************************************/

/************************************************************************
 * Name: mlx90632_iic_drv_open
 * Brief: char device open function interface
 * Input: node, file point
 * Output: no
 * Return: 0
 ***********************************************************************/
static int mlx90632_iic_drv_open(struct inode *inode, struct file *filp)
{
	struct mlx90632_data *pdata;
	int status = -ENXIO;

	pr_info("%s in\n", __func__);

	mutex_lock(&device_list_lock);

	list_for_each_entry(pdata, &device_list, device_entry) {
		if (pdata->devt == inode->i_rdev) {
			pr_info("%s mlx90632_data Found\n", __func__);
			status = 0;
			break;
		}
	}

	if (status == 0) {
		pdata->users++;
		filp->private_data = pdata;

		pr_info("%s Succeed to open /dev/mlx90632\n", __func__);
	} else {
		pr_err("%s nothing for minor %d, status:%d\n", __func__, iminor(inode), status);
		goto err_exit;
	}

	mutex_unlock(&device_list_lock);

	/* Currently the HAL didn't call the MLX90632_IOC_POWER_ON before read data */
	mlx90632_power_on(pdata);

err_exit:
	return status;
}

/************************************************************************
 * Name: mlx90632_iic_drv_exit
 * Brief: delete char device
 * Input: no
 * Output: no
 * Return: no
 ***********************************************************************/
void mlx90632_iic_drv_exit(struct mlx90632_data *pdata)
{
	/* Need define what to do in release, may add powerdown here.
			but need sync with open function. */

	pr_info("[MLX90632]----%s exit ---\n", __func__);
	/*device_destroy(mlx90632_class, pdata->devt);
	class_destroy(mlx90632_class);
	cdev_del(&pdata->cdev);
	unregister_chrdev_region(pdata->devt, 1);*/
}


/************************************************************************
 * Name: mlx90632_iic_drv_release
 * Brief: char device close function interface
 * Input: node, file point
 * Output: no
 * Return: 0
 ***********************************************************************/
static int mlx90632_iic_drv_release(struct inode *inode, struct file *filp)
{
	struct mlx90632_data *pdata;
	int status = 0;

	pr_info("%s in\n", __func__);

	mutex_lock(&device_list_lock);
	pdata = filp->private_data;
	filp->private_data = NULL;

	/* last close? */
	pdata->users--;
	if (!pdata->users) {
		mlx90632_iic_drv_exit(pdata);
	}
	mutex_unlock(&device_list_lock);

	return status;
}


static int mlx90632_write_eeprom(struct mlx90632_data *pdata, temp_reg_operation_t reg_val)
{
	int ret = 0;
	unsigned int regStatus = 0, regControl = 0;
	int tries = 100;

	/* readgoto exit current statue */
	ret = regmap_read(pdata->regmap, MLX90632_REG_CONTROL, &regControl);
	if (ret != 0) {
		pr_err("%s failed to regmap_read control reg\n", __func__);
		ret = -EFAULT;
		goto exit;
	}
	/* disable continuous mode */
	ret = regmap_write(pdata->regmap, MLX90632_REG_CONTROL, (regControl & (~GENMASK(3, 1))));
	if (ret != 0) {
		pr_err("%s failed to disable continuous mode\n", __func__);
		ret = -EFAULT;
		goto exit;
	}
	/* unlock */
	ret = regmap_write(pdata->regmap, MLX90632_EE_Unlock_Addr, MLX90632_EE_Unlock_Cmd);
	if (ret != 0) {
		pr_err("%s failed to disable continuous mode\n", __func__);
		ret = -EFAULT;
		goto exit;
	}
	/* erase */
	ret = regmap_write(pdata->regmap, reg_val.reg, 0x0000);
	if (ret != 0) {
		pr_err("%s failed to regmap_write reg:0x%x val:0x%x\n", __func__, reg_val.reg, reg_val.val);
		ret = -EFAULT;
	}
	/* check erase end */
	do {
		ret = regmap_read(pdata->regmap, MLX90632_REG_STATUS, &regStatus);
		if (ret != 0) {
			pr_err("%s failed to check erase end reg\n", __func__);
			ret = -EFAULT;
			goto exit;
		}
		usleep_range(10000, 11000);
		tries--;
	} while ((regStatus & MLX90632_STAT_EE_BUSY) && (tries > 0));
	/* write new data */
	ret = regmap_write(pdata->regmap, reg_val.reg, reg_val.val);
	if (ret != 0) {
		pr_err("%s failed to regmap_write reg:0x%x val:0x%x\n", __func__, reg_val.reg, reg_val.val);
		ret = -EFAULT;
	}
	/* check write end */
	tries = 100;
	do {
		ret = regmap_read(pdata->regmap, MLX90632_REG_STATUS, &regStatus);
		if (ret != 0) {
			pr_err("%s failed to check write end reg\n", __func__);
			ret = -EFAULT;
			goto exit;
		}
		usleep_range(10000, 11000);
		tries--;
	} while ((regStatus & MLX90632_STAT_EE_BUSY) && (tries > 0));

	/* restore control register value */
	ret = regmap_write(pdata->regmap, MLX90632_REG_CONTROL, regControl);
	if (ret != 0) {
		pr_err("%s failed to disable continuous mode\n", __func__);
		ret = -EFAULT;
		goto exit;
	}
exit:
	return ret;
}

/************************************************************************
 * Name: mlx90632_iic_drv_ioctl
 * Brief: char device I/O control function interface
 * Input: file point, command, package
 * Output: no
 * Return: fail <0
 ***********************************************************************/
static long mlx90632_iic_drv_ioctl(struct file *filp, unsigned int cmd,
				   unsigned long arg)
{
	int ret = 0;
	int val = 0;
	struct mlx90632_data *pdata;
	temp_reg_operation_t reg_val = {0};


	/* int gpio_value = 0; */

	/* Check type and command number */
	if (_IOC_TYPE(cmd) != MLX90632_IOC_MAGIC) {
		pr_err("[MLX90632]%s MLX90632_IOC_MAGIC failed \n", __func__);
		return -ENOTTY;
	}

	/* Check access direction once here; don't repeat below.
	 * IOC_DIR is from the user perspective, while access_ok is
	 * from the kernel perspective; so they look reversed.
	 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
	if (_IOC_DIR(cmd) & _IOC_READ)
		ret = !access_ok(VERIFY_WRITE, (void __user *)arg,
				 _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		ret = !access_ok(VERIFY_READ, (void __user *)arg,
				 _IOC_SIZE(cmd));

	if (ret) {
		pr_err("[MLX90632]access error: %08X, (%2d, %2d)\n", cmd,
			  _IOC_DIR(cmd), _IOC_SIZE(cmd));
		return -EFAULT;
	}
#else
	ret = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
	if (ret) {
		pr_err("[MLX90632]%s access failed \n", __func__);
		return -EFAULT;
	}
#endif
	pdata = filp->private_data;

	switch (cmd) {
	case MLX90632_IOC_POWER_ON:
		ret = mlx90632_power_on(pdata);
		break;
	case MLX90632_IOC_POWER_OFF:
		ret = mlx90632_power_off(pdata);
		break;
	case MLX90632_IOC_READ_TA:
		ret = mlx90632_calc_ambient_dsp105(pdata, &val);
		pr_info("[MLX90632]%s TA ret:%d cmd:%d val:%d, length:%d\n", __func__, ret, cmd, val, sizeof(int));
		if ((ret == 0) && copy_to_user((void __user *)arg, (void *)&val, sizeof(int))) {
			pr_err("[MLX90632]%s TA failed cmd:%d val:%d\n", __func__, cmd, val);
			ret = -EFAULT;
		}
		break;
	case MLX90632_IOC_READ_TO:
		ret = mlx90632_calc_object_dsp105(pdata, &val);
		pr_info("[MLX90632]%s TO ret:%d cmd:%d val:%d, length:%d\n", __func__, ret, cmd, val, sizeof(int));
		if ((ret == 0) && copy_to_user((void __user *)arg, (void *)&val, sizeof(int))) {
			pr_err("[MLX90632]%s TO failed cmd:%d val:%d\n", __func__, cmd, val);
			ret = -EFAULT;
		}
		break;
	case MLX90632_IOC_SLEEP_MODE:
		ret = mlx90632_sleep(pdata);
		break;
	case MLX90632_IOC_STEP_MODE:
		ret = mlx90632_stepmode(pdata);
		break;
	case MLX90632_IOC_CONTINUOUS_MODE:
		ret = mlx90632_wakeup(pdata);
		break;
	case MLX90632_IOC_READ_REG:
		ret = copy_from_user(&reg_val, (void __user *)arg, sizeof(temp_reg_operation_t));
		if (ret != 0) {
			pr_err("%s read failed to copy mode user to kernel\n", __func__);
			ret = -EFAULT;
			break;
		}

		ret = regmap_read(pdata->regmap, reg_val.reg, &reg_val.val);
		if (ret != 0) {
			pr_err("%s failed to regmap_read reg:0x%x\n", __func__, reg_val.reg);
			ret = -EFAULT;
			break;
		}

		ret = copy_to_user((void __user *)arg, (void *)&reg_val, sizeof(temp_reg_operation_t));
		if (ret != 0) {
			pr_err("%s failed to user cmd:%d reg:0x%x val:0x%x\n", __func__, cmd, reg_val.reg, reg_val.val);
			ret = -EFAULT;
		}
		pr_info("%s read cmd:%d reg:0x%x val:0x%x\n", __func__, cmd, reg_val.reg, reg_val.val);
		break;
	case MLX90632_IOC_WRITE_REG:
		ret = copy_from_user(&reg_val, (void __user *)arg, sizeof(temp_reg_operation_t));
		if (ret != 0) {
			pr_err("%s write failed to copy mode user to kernel\n", __func__);
			ret = -EFAULT;
			break;
		}
		ret = mlx90632_write_eeprom(pdata, reg_val);
		pr_info("%s write cmd:%d reg:0x%x val:0x%x, ret:%d\n",
			__func__, cmd, reg_val.reg, reg_val.val, ret);
		break;
	case MLX90632_IOC_MODE:
		if (copy_from_user(&val, (void __user *)arg, sizeof(int))) {
			pr_err("%s failed to copy mode user to kernel\n", __func__);
			ret = -EFAULT;
			break;
		}

		pr_info("%s mode is %d\n", __func__, val);

		switch (val) {
		case 1:
			ret = mlx90632_sleep(pdata);
			break;
		case 2:
			ret = mlx90632_stepmode(pdata);
			break;
		case 3:
			ret = mlx90632_wakeup(pdata);
			break;
		default:
			pr_err("%s Unsupport mode:%d\n", __func__, val);
			ret = -EINVAL;
			break;
		}
		break;
	case MLX90632_IOC_WRITE_CAL:
		ret = copy_from_user(&reg_val, (void __user *)arg, sizeof(temp_reg_operation_t));
		if (ret != 0) {
			pr_err("%s hahb failed to copy user to kernel\n", __func__);
			ret = -EFAULT;
			break;
		}

		if ((reg_val.reg > MLX90632_Ha_Hb_Max) || (reg_val.reg < MLX90632_Ha_Hb_Min) ||
		    (reg_val.val > MLX90632_Ha_Hb_Max) || (reg_val.val < MLX90632_Ha_Hb_Min)) {
			pr_info("%s invalid %d 0x%x 0x%x\n", __func__, pdata->fac_calibrated, reg_val.reg, reg_val.val);
			break;
		}

		pdata->mlx_Ha = reg_val.reg;
		pdata->mlx_Hb = reg_val.val;
		pdata->fac_calibrated = true;
		pr_info("%s w %d 0x%x 0x%x\n", __func__, pdata->fac_calibrated, pdata->mlx_Ha, pdata->mlx_Hb);
		break;
	case MLX90632_IOC_CLEAR_CAL:
		pdata->mlx_Ha = MLX90632_Default_Ha;
		pdata->mlx_Hb = MLX90632_Default_Hb;
		pdata->fac_calibrated = false;
		pr_info("%s c %d 0x%x 0x%x\n", __func__, pdata->fac_calibrated, pdata->mlx_Ha, pdata->mlx_Hb);
		break;
	default:
		pr_err("[MLX90632]%s Unsupport cmd:%d\n", __func__, cmd);
		ret = -ENOTTY;
		break;
	}

	pr_info("[MLX90632]%s end cmd:%d val:%d\n", __func__, cmd, val);

	return ret;
}

/*
 * char device file operation which will be put to register the char device
 */
static const struct file_operations mlx90632_iic_drv_fops = {
	.owner = THIS_MODULE,
	.open = mlx90632_iic_drv_open,
	.release = mlx90632_iic_drv_release,
	.unlocked_ioctl = mlx90632_iic_drv_ioctl,
};

/************************************************************************
 * Name: mlx90632_iic_drv_myinitdev
 * Brief: initial char device
 * Input: i2c info
 * Output: no
 * Return: fail <0
 ***********************************************************************/
static int mlx90632_iic_drv_myinitdev(struct mlx90632_data *pdata)
{
	int err = 0;
	dev_t devno;
	struct device *dev = NULL;

	if (mlx90632_class == NULL) {
		err = alloc_chrdev_region(&devno, 0, 1, MLX90632_DEV_NAME);
		if (err < 0) {
			pr_err("%s:mlx90632_iic_drv cdev_add failed, err:%d\n", __func__, err);
			goto alloc_chr_err;
		}
		pdata->devt = devno;

		cdev_init(&pdata->cdev, &mlx90632_iic_drv_fops);

		err = cdev_add(&pdata->cdev, devno, 1);
		if (err) {
			pr_err("%s:mlx90632_iic_drv cdev_add failed, err:%d\n", __func__, err);
			goto cdev_add_err;
		}

		mlx90632_class = class_create(THIS_MODULE, "mlx90632_class");
		if (IS_ERR(mlx90632_class)) {
			pr_err("%s:mlx90632_iic_drv cdev_add failed\n", __func__);
			err = -1;
			goto dev_class_err;
		}
		/*create device node*/
		dev = device_create(mlx90632_class, &pdata->client->dev, pdata->devt, pdata, MLX90632_DEV_NAME);
		if (IS_ERR(dev)) {
			pr_err("%s:device_create() fail.\n", __func__);
			err = -ENODEV;
			goto dev_class_err;
		} else {
			mutex_lock(&device_list_lock);
			list_add(&pdata->device_entry, &device_list);
			mutex_unlock(&device_list_lock);
			pr_info("[%s] device create success!\n", __func__);
		}

	}

	pr_info("%s done\n", __func__);
	return 0;

dev_class_err:
	cdev_del(&pdata->cdev);

cdev_add_err:
	unregister_chrdev_region(devno, 1);

alloc_chr_err:
	return err;
}


/************************************************************************
 * Name: mlx90632_iic_drv_init
 * Brief: call initial char device
 * Input: i2c info
 * Output: no
 * Return: fail <0
 ***********************************************************************/
static int mlx90632_iic_drv_init(struct mlx90632_data *pdata)
{
	pr_info("[MLX90632]----%s init ---\n", __func__);

	return mlx90632_iic_drv_myinitdev(pdata);
}

#endif

#ifdef MLX90632_INPUT_DEV_SUPPORT

static int mlx90632_read_raw_in(struct mlx90632_data *data, int mod, int *val)
{
	int ret = 0;

	switch (mod) {
	case TEMP_INTERNAL_MOD:
		ret = mlx90632_calc_ambient_dsp105(data, val);
		if (ret < 0) {
			pr_info("%s, TEMP_INTERNAL_MOD failed ret:%d\n", __func__, ret);
		}
		break;
	case TEMP_OBJECT_MOD:
		ret = mlx90632_calc_object_dsp105(data, val);
		if (ret < 0) {
			pr_info("%s, TEMP_OBJECT_MOD failed ret:%d\n", __func__, ret);
		}
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static void mlx90632_input_report(struct mlx90632_data *data, int object_temp, int internal_temp)
{

	input_report_rel(data->mlx90632_input_dev, REL_X, object_temp);
	input_report_rel(data->mlx90632_input_dev, REL_Y, internal_temp);
	input_sync(data->mlx90632_input_dev);
	pr_info("input event object_temp:%d, internal_temp:%d\n", object_temp, internal_temp);
}

/* temperature polling routine */
static void mlx90632_temp_polling_work_handler(struct work_struct *work)
{
	struct mlx90632_data *data = container_of(work, struct mlx90632_data, mlx_work);
	int object_temp, internal_temp;

	mlx90632_read_raw_in(data, TEMP_INTERNAL_MOD, &internal_temp);
	mlx90632_read_raw_in(data, TEMP_OBJECT_MOD, &object_temp);

	mlx90632_input_report(data, object_temp, internal_temp);
}

/* hrtimer_start call back function,use to report temperature data */
static enum hrtimer_restart mlx90632_temp_timer_func(struct hrtimer *timer)
{
	struct mlx90632_data *data = container_of(timer, struct mlx90632_data, mlx_timer);

	queue_work(data->mlx_wq, &data->mlx_work);
	hrtimer_forward_now(&data->mlx_timer, data->mlx_poll_delay);
	return HRTIMER_RESTART;
}

static int32_t mlx90632_enable_workqueue(struct mlx90632_data *data, uint8_t enable)
{
	int32_t ret = 0;
	uint8_t curr_mlx_enable = (data->mlx_enable) ? 1 : 0;

	if (curr_mlx_enable == enable)
		return 0;

	if (enable) {
		data->mlx_enable = true;
		hrtimer_start(&data->mlx_timer, data->mlx_poll_delay, HRTIMER_MODE_REL);
	} else {
		data->mlx_enable = false;
		hrtimer_cancel(&data->mlx_timer);
		cancel_work_sync(&data->mlx_work);
	}

	return ret;
}

static ssize_t mlx90632_init_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct mlx90632_data *data =  dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%s %d %d %d\n", "Is_Cal:", data->fac_calibrated, data->mlx_Ha, data->mlx_Hb);
}

static ssize_t mlx90632_init_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct mlx90632_data *data =  dev_get_drvdata(dev);
	int32_t  cal_ha, cal_hb;
	int  ret = 0;
	int object_temp, internal_temp;

	ret = sscanf(buf, "%d %d", &cal_ha, &cal_hb);
	if (ret != 2) {
		pr_err("%s Parameter invalid, ret=%d\n", __func__, ret);
		goto exit;
	}

	if ((cal_ha > MLX90632_Ha_Hb_Max) || (cal_ha < MLX90632_Ha_Hb_Min) ||
	    (cal_hb > MLX90632_Ha_Hb_Max) || (cal_hb < MLX90632_Ha_Hb_Min)) {
		pr_info("%s invalid %d 0x%x 0x%x\n", __func__, data->fac_calibrated, cal_ha, cal_hb);
		goto exit;
	}
	data->mlx_Ha = cal_ha;
	data->mlx_Hb = cal_hb;
	data->fac_calibrated = true;
	pr_info("%s %d 0x%x 0x%x\n", __func__, data->fac_calibrated, data->mlx_Ha, data->mlx_Hb);

	ret = mlx90632_power_on(data);
	if (ret != 0) {
		pr_err("%s failed ret=%d\n", __func__, ret);
		goto exit;
	}

	/* read once after power on, need about ~500ms */
	mlx90632_read_raw_in(data, TEMP_INTERNAL_MOD, &internal_temp);
	mlx90632_read_raw_in(data, TEMP_OBJECT_MOD, &object_temp);

exit:
	return size;
}

static ssize_t mlx90632_enable_body_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mlx90632_data *data =  dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", (data->enable_body) ? 1 : 0);
}

static ssize_t mlx90632_enable_body_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct mlx90632_data *data = dev_get_drvdata(dev);

	if (sysfs_streq(buf, "1"))
		data->enable_body = 1;
	else if (sysfs_streq(buf, "0"))
		data->enable_body = 0;
	else {
		pr_err("invalid value %d\n", *buf);
		return -EINVAL;
	}
	pr_info("Enable: %d\n", data->enable_body);
	/*
	 * body temperature and object temperature use same input type
	 * so if disable body temperature, need check object enable state
	 */
	if (!data->enable_body && data->enable_object)
		return size;

	mlx90632_enable_workqueue(data, data->enable_body);
	return size;
}

static ssize_t mlx90632_enable_object_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mlx90632_data *data =  dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", (data->enable_object) ? 1 : 0);
}

static ssize_t mlx90632_enable_object_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct mlx90632_data *data = dev_get_drvdata(dev);

	if (sysfs_streq(buf, "1"))
		data->enable_object = 1;
	else if (sysfs_streq(buf, "0"))
		data->enable_object = 0;
	else {
		pr_err("invalid value %d\n", *buf);
		return -EINVAL;
	}
	pr_info("Enable: %d\n", data->enable_object);
	/*
	 * body temperature and object temperature use same input type
	 * so if disable object temperature, need check body enable state
	 */
	if (!data->enable_object && data->enable_body)
		return size;

	mlx90632_enable_workqueue(data, data->enable_object);
	return size;
}

static ssize_t internal_temp_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct mlx90632_data *data = dev_get_drvdata(dev);
	int internal_temp;

	mlx90632_read_raw_in(data, TEMP_INTERNAL_MOD, &internal_temp);

	return scnprintf(buf, sizeof(int), "%d\n", internal_temp);
}
static ssize_t object_temp_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mlx90632_data *data = dev_get_drvdata(dev);
	int object_temp;

	mlx90632_read_raw_in(data, TEMP_OBJECT_MOD, &object_temp);

	return scnprintf(buf, sizeof(int), "%d\n", object_temp);

}

static struct device_attribute attrs_mlx90632_device[] = {
	__ATTR(init, 0664, mlx90632_init_show, mlx90632_init_store),
	__ATTR(enable_body, 0664, mlx90632_enable_body_show, mlx90632_enable_body_store),
	__ATTR(enable_object, 0664, mlx90632_enable_object_show, mlx90632_enable_object_store),
	__ATTR(object_temp, 0444, object_temp_show, NULL),
	__ATTR(internal_temp, 0444, internal_temp_show, NULL),
};

/************************************************************************
 * Name: mlx90632_create_temp_sys_file
 * Brief: initial sysfs device
 * Input: i2c info
 * Output: no
 * Return: fail <0
 ***********************************************************************/
static int mlx90632_create_temp_sys_file(struct mlx90632_data *pdata)
{
	int err = 0;
	dev_t devno;
	unsigned char attr_count;

	if (mlx90632_class_input == NULL) {
		err = alloc_chrdev_region(&devno, 0, 1, MLX90632_DRV_NAME);
		if (err < 0) {
			pr_err("%s: failed, err:%d\n", __func__, err);
			goto alloc_chr_err;
		}
		pdata->devt_in = devno;


		mlx90632_class_input = class_create(THIS_MODULE, MLX90632_DRV_NAME);
		if (IS_ERR(mlx90632_class_input)) {
			pr_err("%s: cdev_add failed\n", __func__);
			err = -ENODEV;
			goto cdev_add_err;
		}
		/*create device node*/
		pdata->mlx90632_data_dev = device_create(mlx90632_class_input, &pdata->client->dev,
					   pdata->devt_in, pdata, MLX90632_DRV_NAME);
		if (IS_ERR(pdata->mlx90632_data_dev)) {
			pr_err("%s:device_create() fail.\n", __func__);
			err = -ENODEV;
			goto dev_class_err;
		}

		for (attr_count = 0; attr_count < ARRAY_SIZE(attrs_mlx90632_device); attr_count++) {
			err = sysfs_create_file(&pdata->mlx90632_data_dev->kobj,
						&attrs_mlx90632_device[attr_count].attr);
			if (err < 0) {
				pr_err("%s: Failed to create sysfs attributes\n",
				       __func__);
				goto device_create_err;
			}
		}

	}

	pr_info("%s done\n", __func__);
	return 0;

device_create_err:
	device_destroy(mlx90632_class_input, pdata->devt);

dev_class_err:
	class_destroy(mlx90632_class_input);

cdev_add_err:
	unregister_chrdev_region(devno, 1);

alloc_chr_err:
	return err;
}

static int mlx90632_temp_sys_file_clean(struct mlx90632_data *pdata)
{
	unsigned char attr_count;

	for (attr_count = 0; attr_count < ARRAY_SIZE(attrs_mlx90632_device); attr_count++) {
		sysfs_remove_file(&pdata->mlx90632_data_dev->kobj,
				  &attrs_mlx90632_device[attr_count].attr);
	}

	if (mlx90632_class_input != NULL) {
		device_destroy(mlx90632_class_input, pdata->devt_in);
		class_destroy(mlx90632_class_input);
	}

	unregister_chrdev_region(pdata->devt_in, 1);
	pr_info("%s done\n", __func__);
	return 0;

}

/************************************************************************
 * Name: mlx90632_register_input_device
 * Brief: initial input device
 * Input: i2c info
 * Output: no
 * Return: fail <0
 ***********************************************************************/
static int mlx90632_register_input_device(struct mlx90632_data *pdata)
{
	int ret = 0;


	/* allocate temperature input device */
	pdata->mlx90632_input_dev = input_allocate_device();
	if (IS_ERR_OR_NULL(pdata->mlx90632_input_dev)) {
		ret = -ENOMEM;
		pr_err("could not allocate virtual input device\n");
		goto allocate_input_device_failed;
	}

	input_set_drvdata(pdata->mlx90632_input_dev, mlx90632);
	pdata->mlx90632_input_dev->name = INPUT_NAME_MLX90632;
	pdata->mlx90632_input_dev->id.bustype = BUS_I2C;

	set_bit(EV_REL, pdata->mlx90632_input_dev->evbit);
	set_bit(REL_X, pdata->mlx90632_input_dev->relbit);
	set_bit(REL_Y, pdata->mlx90632_input_dev->relbit);

	ret = input_register_device(pdata->mlx90632_input_dev);
	if (ret < 0) {
		pr_err("could not register virtual_als input device\n");
		ret = -ENOMEM;
		goto register_input_device_failed;
	}

	return ret;
register_input_device_failed:
	input_free_device(pdata->mlx90632_input_dev);

allocate_input_device_failed:
	return ret;
}

#endif

static int mlx90632_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	int ret = 0;
	unsigned int read = 0;

	pr_info("%s enter\n", __func__);

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*mlx90632));
	if (!indio_dev) {
		dev_err(&client->dev, "Failed to allocate device\n");
		return -ENOMEM;
	}

	regmap = devm_regmap_init_i2c(client, &mlx90632_regmap);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&client->dev, "Failed to allocate regmap: %d\n", ret);
		goto free_dev;
	}

	mlx90632 = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	mlx90632->client = client;
	mlx90632->regmap = regmap;

	mutex_init(&mlx90632->lock);
	indio_dev->dev.parent = &client->dev;
	indio_dev->name = id->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &mlx90632_info;
	indio_dev->channels = mlx90632_channels;
	indio_dev->num_channels = ARRAY_SIZE(mlx90632_channels);

	ret = mlx90632_parse_dt(mlx90632, client);
	if (ret) {
		dev_err(&client->dev, "%s could not setup dts, ret:%d\n", __func__, ret);
		goto free_dev;
	}

	/*ret = regcache_sync(mlx90632->regmap);
	if (ret < 0) {
		dev_err(&client->dev,
			"Failed to sync regmap registers: %d\n", ret);
		goto parse_clean;
	}*/
	ret = mlx90632_wakeup(mlx90632);
	if (ret < 0) {
		dev_err(&client->dev, "mlx90632_wakeup failed: %d\n", ret);
		/*goto parse_clean;*/
	}

	ret = regmap_read(mlx90632->regmap, MLX90632_EE_VERSION, &read);
	if (ret < 0) {
		dev_err(&client->dev, "read of version failed: %d\n", ret);
		goto parse_clean;
	}
	if (read == MLX90632_ID_MEDICAL) {
		pr_info("Detected Medical EEPROM calibration 0x%x\n", read);
	} else if (read == MLX90632_ID_CONSUMER) {
		pr_info("Detected Consumer EEPROM calibration 0x%x\n", read);
	} else if ((read & MLX90632_DSP_MASK) == MLX90632_DSP_VERSION) {
		pr_info("Detected Unknown EEPROM calibration 0x%x\n", read);
	} else {
		dev_err(&client->dev,
			"Wrong DSP version %x (expected %x)\n",
			read, MLX90632_DSP_VERSION);
		goto parse_clean;
	}

	mlx90632->emissivity = 1000;

	ret = mlx90632_wakeup(mlx90632);
	if (ret < 0) {
		dev_err(&client->dev, "mlx90632_wakeup failed: %d\n", ret);
		/*goto parse_clean;*/
	}

#ifdef MLX90632_DEV_CTL
	if (mlx90632_iic_drv_init(mlx90632) < 0) {
		dev_notice(&client->dev,
			   "%s: create mlx90632 char device iic driver failed\n",
			   __func__);
		goto parse_clean;
	}
#endif

	pm_runtime_disable(&client->dev);
	ret = pm_runtime_set_active(&client->dev);
	if (ret < 0) {
		dev_err(&client->dev, "pm_runtime_set_active failed: %d\n", ret);
		ret = mlx90632_sleep(mlx90632);
		if (ret < 0) {
			dev_err(&client->dev, "Sleep failed: %d\n", ret);
		}
		goto parse_clean;
	}
	pm_runtime_enable(&client->dev);
	pm_runtime_set_autosuspend_delay(&client->dev, MLX90632_SLEEP_DELAY_MS);
	pm_runtime_use_autosuspend(&client->dev);

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "iio_device_register failed: %d\n", ret);
		goto parse_clean;
	}

#ifdef MLX90632_INPUT_DEV_SUPPORT
	/* register sysfs for control and input device for report data */
	ret = mlx90632_create_temp_sys_file(mlx90632);
	if (ret < 0) {
		dev_err(&client->dev, "%s failed: %d\n", __func__, ret);
		goto parse_clean;
	}

	ret = mlx90632_register_input_device(mlx90632);
	if (ret < 0) {
		dev_err(&client->dev, "%s failed: %d\n", __func__, ret);
		goto sys_file_clean;
	}

	mlx90632->mlx_wq = create_singlethread_workqueue("mlx_work_queue");
	INIT_WORK(&mlx90632->mlx_work, mlx90632_temp_polling_work_handler);
	hrtimer_init(&mlx90632->mlx_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	mlx90632->mlx_timer.function = mlx90632_temp_timer_func;
	mlx90632->mlx_poll_delay = ns_to_ktime(550 * NSEC_PER_MSEC);
#endif

	mlx90632->mlx_enable = false;
	mlx90632->fac_calibrated = false;
	mlx90632->mlx_Ha = MLX90632_Default_Ha;
	mlx90632->mlx_Hb = MLX90632_Default_Hb;
	mlx90632->power_enabled = false;
	pr_info("%s success, ret=%d\n", __func__, ret);

	return ret;

sys_file_clean:
	mlx90632_temp_sys_file_clean(mlx90632);

parse_clean:
	mlx90632_cleanup(mlx90632);

free_dev:
	devm_iio_device_free(&client->dev, indio_dev);

	pr_info("%s failed, ret=%d\n", __func__, ret);

	return ret;
}

static int mlx90632_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct mlx90632_data *data = iio_priv(indio_dev);

#ifdef MLX90632_DEV_CTL
	mlx90632_iic_drv_exit(data);
#endif

#ifdef MLX90632_INPUT_DEV_SUPPORT
	input_unregister_device(data->mlx90632_input_dev);
	input_free_device(data->mlx90632_input_dev);
	mlx90632_temp_sys_file_clean(mlx90632);
#endif

	iio_device_unregister(indio_dev);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	pm_runtime_put_noidle(&client->dev);

	mlx90632_sleep(data);

	return 0;
}

static const struct i2c_device_id mlx90632_id[] = {
	{ "mlx90632", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mlx90632_id);

static const struct of_device_id mlx90632_of_match[] = {
	{ .compatible = "melexis,mlx90632" },
	{ }
};
MODULE_DEVICE_TABLE(of, mlx90632_of_match);

static int __maybe_unused mlx90632_pm_suspend(struct device *dev)
{
	int ret = 0;
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct mlx90632_data *data = iio_priv(indio_dev);

	ret = mlx90632_sleep(data);
	pr_info("%s ret=%d\n", __func__, ret);
	return ret;
}

static int __maybe_unused mlx90632_pm_resume(struct device *dev)
{
	int ret = 0;
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct mlx90632_data *data = iio_priv(indio_dev);

	ret = mlx90632_wakeup(data);
	pr_info("%s ret=%d\n", __func__, ret);
	return ret;
}

static UNIVERSAL_DEV_PM_OPS(mlx90632_pm_ops, mlx90632_pm_suspend,
			    mlx90632_pm_resume, NULL);

static struct i2c_driver mlx90632_driver = {
	.driver = {
		.name	= "mlx90632",
		.of_match_table = mlx90632_of_match,
		.pm	= &mlx90632_pm_ops,
	},
	.probe = mlx90632_probe,
	.remove = mlx90632_remove,
	.id_table = mlx90632_id,
};
module_i2c_driver(mlx90632_driver);

MODULE_AUTHOR("Crt Mori <cmo@melexis.com>");
MODULE_DESCRIPTION("Melexis MLX90632 contactless Infra Red temperature sensor driver");
MODULE_LICENSE("GPL v2");
