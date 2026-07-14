/*
 * @file   spilead_fp_platform.c
 * @brief  Contains silead_fp device implements for common platform.
 *
 *
 * Copyright 2016-2021 Gigadevice/Silead Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 *
 * ------------------- Revision History ------------------------------
 * <author>    <date>   <version>     <desc>
 * Bill Yu    2018/5/2    0.1.0      Init version
 * Bill Yu    2018/5/20   0.1.1      Default wait 3ms after reset
 * Bill Yu    2018/6/5    0.1.2      Support chip enter power down
 * Bill Yu    2018/6/27   0.1.3      Expand pwdn I/F
 * Taobb      2019/6/6    0.1.4      Expand feature interface, irq pin set to reset pin
 * Bill Yu    2019/8/10   0.1.5      Fix crash while parse dts fail
 * Bill Yu    2020/12/16  0.1.6      Allow GPIO ID number to be zero
 *
 */

#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/of_gpio.h>
#include <linux/timer.h>
#include <linux/err.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/gpio.h>

#include "silead_fp.h"

#ifdef SUPPORT_REE_SPI
#define ERR_NO_SENSOR    111
struct sil_tx_buf_t {
	uint8_t cmd;
	uint8_t addr_h;
	uint8_t addr_l;
	uint8_t len_h;
	uint8_t len_l;
	uint8_t buf[512];
};

struct sil_rx_buf_t {
	uint8_t cmd;
	uint8_t buf[512];
};

u32 sil_spi_speed = 1*1000000;

void endian_exchange(u8 *buf, u32 len)
{
	u32 i = 0;
	u8 buf_tmp;
	u32 size = len / 2;

	LOG_MSG_DEBUG(DEBUG_LOG, "[%s] enter!\n", __func__);

	for (i = 0; i < size; i++) {
		buf_tmp = buf[2 * i + 1];
		buf[2 * i + 1] = buf[2 * i];
		buf[2 * i] = buf_tmp;
	}
}
/*silead chip_id reg only need read 0xFC value*/
int sil_spi_read_bytes_ree_new(struct silfp_data *fp_dev, u16 addr, u32 data_len, u8 *buf)
{
	struct spi_message msg;
	struct spi_transfer xfer;

	struct sil_tx_buf_t *s_tx_buf;
	struct sil_rx_buf_t *s_rx_buf;

	LOG_MSG_DEBUG(DEBUG_LOG, "[%s] enter!\n", __func__);

	s_tx_buf = kzalloc(512 + 5, GFP_KERNEL);
	s_rx_buf = kzalloc(512 + 5, GFP_KERNEL);

	 LOG_MSG_DEBUG(INFO_LOG, "s_tx_buf : %p, s_rx_buf : %p\n", s_tx_buf, s_rx_buf); 

	spi_message_init(&msg);
	memset(&xfer, 0, sizeof(struct spi_transfer));
	s_tx_buf->cmd = 0xFC;

	xfer.tx_buf = s_tx_buf;
	xfer.rx_buf = s_rx_buf;

	xfer.len = data_len + 1;

	xfer.speed_hz = sil_spi_speed;

	spi_message_add_tail(&xfer, &msg);
	spi_sync(fp_dev->spi, &msg);

	memcpy(buf, s_rx_buf->buf, data_len);

	/*change the read data to little endian. */
       /*	endian_exchange(buf, data_len);    */

	kfree(s_tx_buf);
	kfree(s_rx_buf);

	return 0;
}

int silead_check_6152s_chip_id(struct silfp_data *fp_dev)
{
	u32 time_out = 0;
	u8 tmp_buf[8] = {0};

	LOG_MSG_DEBUG(DEBUG_LOG, "[%s] enter!\n", __func__);

	do {
		/* read data start from offset 8 */
		sil_spi_read_bytes_ree_new(fp_dev, 0x0000, 8, tmp_buf);
		LOG_MSG_DEBUG(INFO_LOG, "%s, chip id0 is 0x%02x 0x%02x 0x%02x 0x%02x\n", __func__,
			 tmp_buf[4], tmp_buf[3], tmp_buf[2], tmp_buf[1]);
		LOG_MSG_DEBUG(INFO_LOG, "%s, chip id1 is 0x%02x 0x%02x 0x%02x 0x%02x\n", __func__,
			 tmp_buf[0], tmp_buf[5], tmp_buf[6], tmp_buf[7]);

		time_out++;

		if ((tmp_buf[4] == 0x61) &&(tmp_buf[3] == 0x52) && (tmp_buf[2] == 0xa0) && (tmp_buf[1] == 0x01)) {
			LOG_MSG_DEBUG(INFO_LOG, "%s, SL6152S chip id check pass, time_out=%d\n", __func__, time_out);
			return 0;
		}
	} while (time_out < 3);

	LOG_MSG_DEBUG(ERR_LOG, "[%s] chip id read failed!time_out=%d\n", __func__, time_out);
	return -ERR_NO_SENSOR;
}
#endif

int silfp_parse_dts(struct silfp_data *fp_dev)
{
	int ret = 0;
	int status = 0;

	LOG_MSG_DEBUG(DEBUG_LOG, "[%s] enter!\n", __func__);

	/*---avdd port---*/
	fp_dev->avdd_port = of_get_named_gpio(fp_dev->spi->dev.of_node, "avdd-gpios", 0);
	LOG_MSG_DEBUG(INFO_LOG, "[%s] fp_dev->avdd_port : %d\n", __func__ , fp_dev->avdd_port);
	if (fp_dev->avdd_port) {
		ret = gpio_request(fp_dev->avdd_port, "SILFP_AVDD_PIN");
		if (ret < 0) {
			LOG_MSG_DEBUG(ERR_LOG, "[%s] Failed to request avdd_port = %d, ret = %d",
					  __func__, (s32)fp_dev->avdd_port, ret);
			status = -ENODEV;
			goto err_avdd;
		} else {
			LOG_MSG_DEBUG(INFO_LOG, "[%s] Success to request avdd_port!\n", __func__ );
			gpio_direction_output(fp_dev->avdd_port, 0);
		}
	}

	/*---int port---*/
	fp_dev->int_port = of_get_named_gpio(fp_dev->spi->dev.of_node, "irq-gpios", 0);
	LOG_MSG_DEBUG(INFO_LOG, "[%s] fp_dev->int_port : %d\n", __func__, fp_dev->int_port);
	if (fp_dev->int_port) {
		ret = gpio_request(fp_dev->int_port, "SILFP_INT_IRQ");
		if (ret < 0) {
			LOG_MSG_DEBUG(ERR_LOG, "[%s] Failed to request int_port =%d, ret = %d",
					 __func__, (s32)fp_dev->int_port, ret);
			status = -ENODEV;
			goto err_irq_port;
		} else {
			LOG_MSG_DEBUG(INFO_LOG, "[%s] Success to request int_port!\n", __func__ );
			gpio_direction_input(fp_dev->int_port);
			fp_dev->irq = gpio_to_irq(fp_dev->int_port);
			fp_dev->irq_is_disable = 0;

			ret  = request_irq(fp_dev->irq,
					   silfp_irq_handler,
					   IRQ_TYPE_EDGE_RISING, /*IRQ_TYPE_LEVEL_HIGH, irq_table[ts->int_trigger_type]*/
					   "silfp",
					   fp_dev);
			if (ret < 0) {
				LOG_MSG_DEBUG(ERR_LOG, "[%s] Failed to request_irq (%d), ret=%d", __func__, fp_dev->irq, ret);
				status = -ENODEV;
				goto err_irq_request;
			} else {
				LOG_MSG_DEBUG(INFO_LOG, "[%s] Enable_irq_wake.\n", __func__);
				enable_irq_wake(fp_dev->irq);
				silfp_irq_disable(fp_dev);
			}
		}
	}

	/*---rst port---*/
	fp_dev->rst_port = of_get_named_gpio(fp_dev->spi->dev.of_node, "rst-gpios", 0);
	LOG_MSG_DEBUG(INFO_LOG, "[%s] fp_dev->rst_port : %d\n", __func__, fp_dev->rst_port);
	if (fp_dev->rst_port) {
		ret = gpio_request(fp_dev->rst_port, "SILFP_RST_PIN");
		if (ret < 0) {
			LOG_MSG_DEBUG(ERR_LOG, "[%s] Failed to request rst_port = %d, ret = %d",
				__func__, (s32)fp_dev->rst_port, ret);
			status = -ENODEV;
			goto err_rst_port;
		} else {
			LOG_MSG_DEBUG(INFO_LOG, "[%s] Success to request rst_port!\n", __func__ );
			gpio_direction_output(fp_dev->rst_port, 0);
		}
	}
	LOG_MSG_DEBUG(DEBUG_LOG, "[%s] Done.\n", __func__);

	return status;

	err_rst_port:
		fp_dev->rst_port = 0;

	err_irq_request:
		gpio_free(fp_dev->int_port);

	err_irq_port:
		fp_dev->int_port = 0;

	err_avdd:
		fp_dev->avdd_port = 0;
		return status;
}

void silfp_cleanup(struct silfp_data *fp_dev)
{
	LOG_MSG_DEBUG(DEBUG_LOG, "[%s] enter.\n", __func__);

	if (gpio_is_valid(fp_dev->avdd_port)) {
		gpio_set_value(fp_dev->avdd_port, 0);
		gpio_free(fp_dev->avdd_port);
		LOG_MSG_DEBUG(INFO_LOG, "silead set avdd_port low and remove avdd_port success\n");
		fp_dev->avdd_port = 0;
	}
	if (gpio_is_valid(fp_dev->int_port)) {
		gpio_free(fp_dev->int_port);
		LOG_MSG_DEBUG(INFO_LOG, "silead remove int_port success\n");
		fp_dev->int_port = 0;
	}
	if (gpio_is_valid(fp_dev->rst_port)) {
		gpio_set_value(fp_dev->rst_port, 0);
		gpio_free(fp_dev->rst_port);
		LOG_MSG_DEBUG(INFO_LOG, "silead set rst_port low and remove rst_port success\n");
		fp_dev->rst_port = 0;
	}
}

void silfp_hw_poweron(struct silfp_data *fp_dev)
{
	LOG_MSG_DEBUG(DEBUG_LOG, "[%s] enter.\n", __func__);

	if (gpio_is_valid(fp_dev->avdd_port)) {
		gpio_set_value(fp_dev->avdd_port, 1);
		msleep(20);
		LOG_MSG_DEBUG(INFO_LOG, "----silead power on ok ----\n");
	}
	fp_dev->power_is_off = 0;
}

void silfp_hw_poweroff(struct silfp_data *fp_dev)
{
	LOG_MSG_DEBUG(DEBUG_LOG, "[%s] enter.\n", __func__);

	if (gpio_is_valid(fp_dev->avdd_port)) {
		gpio_set_value(fp_dev->avdd_port, 0);
		LOG_MSG_DEBUG(INFO_LOG, "----silead power off ok ----\n");
	}
	fp_dev->power_is_off = 1;
}

void silfp_hw_reset(struct silfp_data *fp_dev, u8 delay)
{
	LOG_MSG_DEBUG(DEBUG_LOG, "[%s] enter.\n", __func__);

	if (gpio_is_valid(fp_dev->rst_port)) {
		gpio_set_value(fp_dev->rst_port, 0);
		msleep(20);
		gpio_set_value(fp_dev->rst_port, 1);
		msleep(delay);
		LOG_MSG_DEBUG(INFO_LOG, "----silead hw reset ok----\n");
	}
}

/* -------------------------------------------------------------------- */
/*                            power  down                               */
/* -------------------------------------------------------------------- */
void silfp_pwdn(struct silfp_data *fp_dev, u8 flag_avdd)
{
	LOG_MSG_DEBUG(INFO_LOG, "[%s] enter, port=%d\n", __func__, fp_dev->rst_port);

	if (SIFP_PWDN_FLASH == flag_avdd) {
		silfp_hw_poweroff(fp_dev);
		msleep(200 * RESET_TIME_MULTIPLE);
		silfp_hw_poweron(fp_dev);
	}

	if ( fp_dev->rst_port >= 0 ) {
		gpio_direction_output(fp_dev->rst_port, 0);
	}

	if (SIFP_PWDN_POWEROFF == flag_avdd) {
		silfp_hw_poweroff(fp_dev);
	}
}

void silfp_set_spi(struct silfp_data *fp_dev, bool enable)
{

	LOG_MSG_DEBUG(DEBUG_LOG, "%s: sprd ap no needed!\n", __func__);

}

int silfp_irq_to_reset_init(struct silfp_data *fp_dev)
{
	int ret = 0;

	LOG_MSG_DEBUG(DEBUG_LOG, "[%s] enter.\n", __func__);
	silfp_irq_disable(fp_dev);
	free_irq(fp_dev->irq, fp_dev);

	if (fp_dev->int_port) {
		gpio_free(fp_dev->int_port);
		ret = gpio_request(fp_dev->int_port, "SILFP_IRQ_TO_RST_PIN");
		if (ret < 0) {
			LOG_MSG_DEBUG(ERR_LOG, "[%s] Failed to request GPIO=%d, ret=%d",__func__,(s32)fp_dev->int_port, ret);
			return -ENODEV;
		} else {
			gpio_direction_output(fp_dev->int_port, 0);
		}
	}

	return ret;
}

int silfp_set_feature(struct silfp_data *fp_dev, u8 feature)
{
	int ret = 0;

	LOG_MSG_DEBUG(DEBUG_LOG, "[%s] enter\n", __func__);

	switch (feature) {
	case FEATURE_FLASH_CS:
		LOG_MSG_DEBUG(INFO_LOG, "%s set feature flash cs\n", __func__);
		ret = silfp_irq_to_reset_init(fp_dev);
		break;

	default:
		break;
	}

	return ret;
}

int silfp_resource_init(struct silfp_data *fp_dev, struct fp_dev_init_t *dev_info)
{
	int ret = 0;

	LOG_MSG_DEBUG(DEBUG_LOG, "[%s] enter\n", __func__);

	if (atomic_read(&fp_dev->init)) {
		atomic_inc(&fp_dev->init);
		LOG_MSG_DEBUG(INFO_LOG, "[%s] dev already init(%d).\n", __func__, atomic_read(&fp_dev->init));
		return ret;
	}

	ret = silfp_parse_dts(fp_dev);
	/* If here not poweron, the chip_id will not corret*/
	silfp_hw_poweron(fp_dev);

	if (!ret) {
		if (silfp_input_init(fp_dev)) {
			goto err_input;
		}
		atomic_set(&fp_dev->init, 1);
	}

	dev_info->reserve = PKG_SIZE;
	dev_info->reserve <<= 12;

#ifdef SUPPORT_REE_SPI
	/* Ree Read Chip Id */
	/* If here not enble_irq, the chip_id will not corret*/
	silfp_irq_enable(fp_dev);
	silfp_hw_poweron(fp_dev);
	silfp_hw_reset(fp_dev,3);
	mt_spi_enable_master_clk(fp_dev->spi);
	silead_check_6152s_chip_id(fp_dev);
	mt_spi_disable_master_clk(fp_dev->spi);
	silfp_hw_poweroff(fp_dev);
	silfp_irq_disable(fp_dev);
#endif
	LOG_MSG_DEBUG(INFO_LOG, "[%s] done.\n", __func__);
	return ret;

	err_input:
		if (fp_dev->rst_port > 0) {
			gpio_free(fp_dev->rst_port);
		}
	return ret;
}

/* End of file spilead_fp_platform.c */
