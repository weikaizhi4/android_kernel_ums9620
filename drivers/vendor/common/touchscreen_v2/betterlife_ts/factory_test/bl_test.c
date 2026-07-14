/*
 *
 * BetterLife TouchScreen driver.
 *
 * Copyright (c) 2012-2020, BetterLife Systems, Ltd., all rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 */

/************************************************************************
*
* File Name: bl_test.c
*
* Author: Betterlife Driver Team
*
* Created: 2016-08-01
*
* Modify:
*
* Abstract: create char device and proc node for  the comm between APK and TP
*
************************************************************************/

/*****************************************************************************
* Included header files
*****************************************************************************/
#include "bl_test.h"

/*****************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/
#include <linux/time.h>
#ifdef BTL_FACTORY_TEST_EN
static int rawdata_test_flag = 0;//use fail for default
struct btl_test *btl_ftest;
struct test_funcs *btl_test_func_list[] = {
#if (CTP_TYPE == SELF_INTERACTIVE_CTP)
	&test_func_mcsc,
	#endif
	#if (CTP_TYPE == SELF_CTP)
	&test_func_sc,
	#endif

};
int g_test_item = 0;
extern int blt_tptest_result;
/*****************************************************************************
* Static function prototypes
*****************************************************************************/

/*****************************************************************************
* functions body
*****************************************************************************/
void btl_sys_delay(int ms)
{
	msleep(ms);
}

int btl_abs(int value)
{
	if (value < 0)
		value = 0 - value;

	return value;
}

void *btl_malloc(size_t size)
{
	return kzalloc(size, GFP_KERNEL);
}

void btl_free_proc(void *p)
{
	return kfree(p);
}

void btl_print_buffer(int *buffer, int length, int line_num)
{
	int i = 0;
	int j = 0;
	int tmpline = 0;
	char *tmpbuf = NULL;
	int tmplen = 0;
	int cnt = 0;
	struct btl_test *tdata = btl_ftest;

	if (tdata && (btl_log_level < 3)) {
		return;
	}

	if ((buffer == NULL) || (length <= 0)) {
		BTL_TEST_DBG("buffer/length(%d) fail", length);
		return;
	}

	tmpline = line_num ? line_num : length;
	tmplen = tmpline * 6 + 128;
	tmpbuf = kzalloc(tmplen, GFP_KERNEL);

	for (i = 0; i < length; i = i + tmpline) {
		cnt = 0;
		for (j = 0; j < tmpline; j++) {
			cnt += snprintf(tmpbuf + cnt, tmplen - cnt, "%5d ", buffer[i + j]);
			if ((cnt >= tmplen) || ((i + j + 1) >= length))
				break;
		}
		BTL_TEST_DBG("%s", tmpbuf);
	}

	if (tmpbuf) {
		kfree(tmpbuf);
		tmpbuf = NULL;
	}
}

/********************************************************************
 * test read/write interface
 *******************************************************************/
int btl_test_bus_read(
	u8 *cmd, int cmdlen, u8 *data, int datalen)
{
	int ret = 0;

	ret = btl_i2c_write_read(g_btl_ts->client, CTP_SLAVE_ADDR, cmd, cmdlen, data, datalen);
	if (ret < 0)
		return ret;
	else
		return 0;
}

int btl_test_bus_write(u8 *writebuf, int writelen)
{
	int ret = 0;

	ret = btl_i2c_write(g_btl_ts->client, CTP_SLAVE_ADDR, writebuf, writelen);
	if (ret < 0)
		return ret;
	else
		return 0;
}

int btl_test_read_reg(u8 addr, u8 *val)
{
	return btl_test_bus_read(&addr, 1, val, 1);
}

int btl_test_write_reg(u8 addr, u8 val)
{
	int ret;
	u8 cmd[2] = {0};

	cmd[0] = addr;
	cmd[1] = val;
	ret = btl_test_bus_write(cmd, 2);

	return ret;
}

int btl_test_read(u8 addr, u8 *readbuf, int readlen)
{
	int ret = 0;
	int i = 0;
	int packet_length = 0;
	int packet_num = 0;
	int packet_remainder = 0;
	int offset = 0;
	int byte_num = readlen;

	packet_num = byte_num / BYTES_PER_TIME;
	packet_remainder = byte_num % BYTES_PER_TIME;
	if (packet_remainder)
		packet_num++;

	if (byte_num < BYTES_PER_TIME) {
		packet_length = byte_num;
	} else {
		packet_length = BYTES_PER_TIME;
	}
	/* BTL_TEST_DBG("packet num:%d, remainder:%d", packet_num, packet_remainder); */

	ret = btl_test_bus_read(&addr, 1, &readbuf[offset], packet_length);
	if (ret < 0) {
		BTL_TEST_ERROR("read buffer fail");
		return ret;
	}
	for (i = 1; i < packet_num; i++) {
		offset += packet_length;
		if ((i == (packet_num - 1)) && packet_remainder) {
			packet_length = packet_remainder;
		}


		ret = btl_test_bus_read(NULL, 0, &readbuf[offset],
								packet_length);
		if (ret < 0) {
			BTL_TEST_ERROR("read buffer fail");
			return ret;
		}
	}

	return 0;
}

int btl_test_read_bytes(u8 *cmd, u8 cmd_len, u8 *readbuf, int readlen)
{
	int ret = 0;
	int i = 0;
	int packet_length = 0;
	int packet_num = 0;
	int packet_remainder = 0;
	int offset = 0;
	int byte_num = readlen;

	packet_num = byte_num / BYTES_PER_TIME;
	packet_remainder = byte_num % BYTES_PER_TIME;
	if (packet_remainder)
		packet_num++;

	if (byte_num < BYTES_PER_TIME) {
		packet_length = byte_num;
	} else {
		packet_length = BYTES_PER_TIME;
	}
	/* BTL_TEST_DBG("packet num:%d, remainder:%d", packet_num, packet_remainder); */

	ret = btl_test_bus_read(cmd, cmd_len, &readbuf[offset], packet_length);
	if (ret < 0) {
		BTL_TEST_ERROR("read buffer fail");
		return ret;
	}
	for (i = 1; i < packet_num; i++) {
		offset += packet_length;
		if ((i == (packet_num - 1)) && packet_remainder) {
			packet_length = packet_remainder;
		}

		cmd[1] = (offset & 0xff00) >> 8;
		cmd[2] = offset & 0x00ff;

		ret = btl_test_bus_read(cmd, cmd_len, &readbuf[offset],
								packet_length);
		if (ret < 0) {
			BTL_TEST_ERROR("read buffer fail");
			return ret;
		}
	}

	return 0;
}

int btl_test_write(u8 addr, u8 *writebuf, int writelen)
{
	int ret = 0;
	int i = 0;
	u8 *data = NULL;
	int packet_length = 0;
	int packet_num = 0;
	int packet_remainder = 0;
	int offset = 0;
	int byte_num = writelen;

	data = btl_malloc(BYTES_PER_TIME + 1);
	if (!data) {
		BTL_TEST_ERROR("malloc memory for bus write data fail");
		return -ENOMEM;
	}

	packet_num = byte_num / BYTES_PER_TIME;
	packet_remainder = byte_num % BYTES_PER_TIME;
	if (packet_remainder)
		packet_num++;

	if (byte_num < BYTES_PER_TIME) {
		packet_length = byte_num;
	} else {
		packet_length = BYTES_PER_TIME;
	}
	/* BTL_TEST_DBG("packet num:%d, remainder:%d", packet_num, packet_remainder); */

	data[0] = addr;
	for (i = 0; i < packet_num; i++) {
		if (i != 0) {
			data[0] = addr + 1;
		}
		if ((i == (packet_num - 1)) && packet_remainder) {
			packet_length = packet_remainder;
		}
		memcpy(&data[1], &writebuf[offset], packet_length);

		ret = btl_test_bus_write(data, packet_length + 1);
		if (ret < 0) {
			BTL_TEST_ERROR("write buffer fail");
			btl_free(data);
			return ret;
		}

		offset += packet_length;
	}

	btl_free(data);
	return 0;
}

/********************************************************************
 * test global function enter work/factory mode
 *******************************************************************/
int btl_enter_work_mode(void)
{
	int ret = 0;
	u8 mode = 0;
	int i = 0;
	int j = 0;

	BTL_TEST_FUNC_ENTER();

	ret = btl_test_read_reg(DEVIDE_MODE_ADDR, &mode);
	if ((ret >= 0) && (0x00 == mode))
		return 0;

	for (i = 0; i < ENTER_WORK_FACTORY_RETRIES; i++) {
		ret = btl_test_write_reg(DEVIDE_MODE_ADDR, 0x00);
		if (ret >= 0) {
			btl_sys_delay(FACTORY_TEST_DELAY);
			for (j = 0; j < 20; j++) {
				ret = btl_test_read_reg(DEVIDE_MODE_ADDR, &mode);
				if ((ret >= 0) && (0x00 == mode)) {
					BTL_TEST_INFO("enter work mode success");
					return 0;
				} else
					btl_sys_delay(FACTORY_TEST_DELAY);
			}
		}

		btl_sys_delay(50);
	}

	if (i >= ENTER_WORK_FACTORY_RETRIES) {
		BTL_TEST_ERROR("Enter work mode fail");
		return -EIO;
	}

	BTL_TEST_FUNC_EXIT();
	return 0;
}

int btl_enter_factory_mode(void)
{
	int ret = 0;
	u8 mode = 0;
	int i = 0;
	int j = 0;

	ret = btl_test_read_reg(DEVIDE_MODE_ADDR, &mode);
	if ((ret >= 0) && ((mode & 0x40)) == 0X40)
		return 0;

	for (i = 0; i < ENTER_WORK_FACTORY_RETRIES; i++) {
		ret = btl_test_write_reg(DEVIDE_MODE_ADDR, 0x40);
		if (ret >= 0) {
			btl_sys_delay(FACTORY_TEST_DELAY);
			for (j = 0; j < 20; j++) {
				ret = btl_test_read_reg(DEVIDE_MODE_ADDR, &mode);
				if ((ret >= 0) && (0x40 == (mode & 0x40))) {
					BTL_TEST_INFO("enter factory mode success");
					btl_sys_delay(200);
					return 0;
				} else
					btl_sys_delay(FACTORY_TEST_DELAY);
			}
		}

		btl_sys_delay(50);
	}

	if (i >= ENTER_WORK_FACTORY_RETRIES) {
		BTL_TEST_ERROR("Enter factory mode fail");
		return -EIO;
	}

	return 0;
}

int btl_sc_cb_calibrate(u8 mode)
{
	int ret = 0;
	int i = 0;
	int j = 0;

	for (i = 0; i < ENTER_WORK_FACTORY_RETRIES; i++) {
		ret = btl_test_write_reg(REG_CALI_CB, mode);
		if (ret >= 0) {
			btl_sys_delay(FACTORY_TEST_DELAY);
			for (j = 0; j < 20; j++) {
				ret = btl_test_read_reg(REG_CALI_CB, &mode);
				if ((ret >= 0) && (0x00 == mode)) {
					BTL_TEST_INFO("calibrate success");
					btl_sys_delay(200);
					return 0;
				} else
					btl_sys_delay(FACTORY_TEST_DELAY);
			}
		}

		btl_sys_delay(50);
	}

	if (i >= ENTER_WORK_FACTORY_RETRIES) {
		BTL_TEST_ERROR("Enter factory mode fail");
		return -EIO;
	}

	return 0;
}

/*
 * btl_read_mass_data - read rawdata/short test data
 * addr - register addr which read data from
 * byte_num - read data length, unit:byte
 * buf - save data
 *
 * return 0 if read data succuss, otherwise return error code
 */
int btl_read_mass_data(u8 *cmd, u8 cmd_len, int byte_num, int *buf)
{
	int ret = 0;
	int i = 0;
	u8 *data = NULL;

	data = btl_malloc(byte_num * sizeof(u8));
	if (data == NULL) {
		BTL_TEST_SAVE_ERR("mass data buffer malloc fail\n");
		return -ENOMEM;
	}

	/* read rawdata buffer */
	BTL_TEST_INFO("mass data len:%d", byte_num);
	ret = btl_test_read_bytes(cmd, cmd_len, data, byte_num);
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("read mass data fail\n");
		goto read_massdata_err;
	}

	for (i = 0; i < byte_num; i = i + 2) {
		buf[i >> 1] = (int)(short)((data[i+1] << 8) + data[i]);
	}

	ret = 0;
read_massdata_err:
	btl_free(data);
	return ret;
}

int btl_read_mass_data_u16(u8 *cmd, u8 cmd_len, int byte_num, int *buf)
{
	int ret = 0;
	int i = 0;
	u8 *data = NULL;

	data = btl_malloc(byte_num * sizeof(u8));
	if (data ==NULL) {
		BTL_TEST_SAVE_ERR("mass data buffer malloc fail\n");
		return -ENOMEM;
	}

	/* read rawdata buffer */
	BTL_TEST_INFO("mass data len:%d", byte_num);
	ret = btl_test_read_bytes(cmd, cmd_len, data, byte_num);
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("read mass data fail\n");
		goto read_massdata_err;
	}

	for (i = 0; i < byte_num; i = i + 2) {
		buf[i >> 1] = (int)(u16)((data[i + 1] << 8) + data[i]);
	}

	ret = 0;
read_massdata_err:
	btl_free(data);
	return ret;
}

int btl_short_get_adcdata_incell(u8 retval, u8 ch_num, int byte_num, int *adc_buf)
{
	int ret = 0;
	int times = 0;
	u8 short_state = 0;
	u8 cmd_get_adcdata[3] = {0x00};
	

	BTL_TEST_FUNC_ENTER();

	/* Start ADC sample */
	ret = btl_test_write_reg(FACTORY_REG_SHORT_TEST_EN, 0x01);
	if (ret) {
		BTL_TEST_SAVE_ERR("start short test fail\n");
		goto adc_err;
	}

	btl_sys_delay(ch_num * FACTORY_TEST_DELAY);
	for (times = 0; times < FACTORY_TEST_RETRY; times++) {
		ret = btl_test_read_reg(FACTORY_REG_SHORT_TEST_STATE, &short_state);
		if ((ret >= 0) && (retval == short_state))
			break;
		else
			BTL_TEST_DBG("reg%x=%x,retry:%d",
						 FACTORY_REG_SHORT_TEST_STATE, short_state, times);

		btl_sys_delay(FACTORY_TEST_RETRY_DELAY);
	}
	if (times >= FACTORY_TEST_RETRY) {
		BTL_TEST_SAVE_ERR("short test timeout, ADC data not OK\n");
		ret = -EIO;
		goto adc_err;
	}

	cmd_get_adcdata[0] = FACTORY_REG_SHORT_ADDR;
	cmd_get_adcdata[1] = 0x00;
	cmd_get_adcdata[2] = 0x00;
	
	ret = btl_read_mass_data(cmd_get_adcdata, sizeof(cmd_get_adcdata), byte_num, adc_buf);
	if (ret) {
		BTL_TEST_SAVE_ERR("get short(adc) data fail\n");
	}

adc_err:
	BTL_TEST_FUNC_EXIT();
	return ret;
}

/*
 * btl_wait_state_update - wait fw status update
 */
int btl_wait_state_update(u8 retval)
{
	int ret = 0;
	int times = 0;
	u8 state = 0xFF;

	while (times++ < FACTORY_TEST_RETRY) {
		btl_sys_delay(FACTORY_TEST_DELAY);
		/* Wait register status update */
		state = 0xFF;
		ret = btl_test_read_reg(FACTORY_REG_PARAM_UPDATE_STATE, &state);
		if ((ret >= 0) && (retval == state))
			break;
		else
			BTL_TEST_DBG("reg%x=%x,retry:%d", \
						 FACTORY_REG_PARAM_UPDATE_STATE, state, times);
	}

	if (times >= FACTORY_TEST_RETRY) {
		BTL_TEST_SAVE_ERR("Wait State Update fail\n");
		return -EIO;
	}

	return 0;
}

/*
 * btl_start_scan - start to scan a frame
 */
int btl_start_scan(void)
{
	int ret = 0;
	u8 addr = 0;
	u8 val = 0;
	int times = 0;
	struct btl_test *tdata = btl_ftest;

	if ((tdata ==NULL) || (tdata->func == NULL)) {
		BTL_TEST_SAVE_ERR("test/func is null\n");
		return -EINVAL;
	}

	if (tdata->func->startscan_mode == SCAN_SC) {
		/* sc ic */
		addr = FACTORY_REG_SCAN_ADDR2;
		val = 0x01;
	} else {
		addr = DEVIDE_MODE_ADDR;
		val = 0xC0;
	}

	if (g_test_item)
		val = 0xC4;
	
	/* write register to start scan */
	ret = btl_test_write_reg(addr, val);
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("write start scan mode fail\n");
		return ret;
	}

	/* Wait for the scan to complete */
	while (times++ < FACTORY_TEST_RETRY) {
		btl_sys_delay(FACTORY_TEST_DELAY);

		ret = btl_test_read_reg(addr, &val);
		if ((ret >= 0) && ((val & 0x84)== 0x00)) {
			break;
		} else
			BTL_TEST_DBG("reg%x=%x,retry:%d", addr, val, times);
	}

	if (times >= FACTORY_TEST_RETRY) {
		BTL_TEST_SAVE_ERR("scan timeout\n");
		return -EIO;
	}

	return 0;
}

static int btl_start_short_test(void)
{
	int ret = 0;
	u8 addr = 0;
	u8 val = 0;
	u8 finish_val = 0;
	int times = 0;
	struct btl_test *tdata = btl_ftest;

	if ((tdata == NULL) || (tdata->func == NULL)) {
		BTL_TEST_SAVE_ERR("test/func is null\n");
		return -EINVAL;
	}

	if (tdata->func->startscan_mode == SCAN_SC) {
		/* sc ic */
		addr = FACTORY_REG_SCAN_ADDR2;
		val = 0x01;
		finish_val = 0x00;
	} else {
		addr = DEVIDE_MODE_ADDR;
		val = 0x44;
		finish_val = 0x40;
	}
	
	/* write register to start scan */
	ret = btl_test_write_reg(addr, val);
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("start short test fail\n");
		return ret;
	}

	/* Wait for the scan to complete */
	while (times++ < FACTORY_TEST_RETRY) {
		btl_sys_delay(FACTORY_TEST_DELAY);

		ret = btl_test_read_reg(addr, &val);
		if ((ret >= 0) && (val == finish_val)) {
			break;
		} else
			BTL_TEST_DBG("reg%x=%x,retry:%d", addr, val, times);
	}

	if (times >= FACTORY_TEST_RETRY) {
		BTL_TEST_SAVE_ERR("start short test timeout\n");
		return -EIO;
	}

	return 0;
}

static int btl_read_rawdata(
	struct btl_test *tdata,
	u8 *cmd,
	u8 cmd_len,
	int byte_num,
	int *data)
{
	int ret = 0;

	if (tdata->func->raw_u16)
		ret = btl_read_mass_data_u16(cmd, cmd_len, byte_num, data);
	else
		ret = btl_read_mass_data(cmd, cmd_len, byte_num, data);
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("read rawdata fail\n");
		return ret;
	}

	return 0;
}

int btl_get_rawdata(int *data)
{
	int ret = 0;
	u8 val = 0;
	u8 addr = 0;
	u8 rawdata_addr = 0;
	u8 cmd_read_rawdata[3] = {0x00};
	int byte_num = 0;
	struct btl_test *tdata = btl_ftest;

	if ((tdata == NULL) || (tdata->func == NULL)) {
		BTL_TEST_SAVE_ERR("test/func is null\n");
		return -EINVAL;
	}

	/* enter factory mode */
	ret = btl_enter_factory_mode();
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("failed to enter factory mode,ret=%d\n", ret);
		return ret;
	}

	/* start scanning */
	ret = btl_start_scan();
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("scan fail\n");
		return ret;
	}

	/* read rawdata */
	if (tdata->func->hwtype == IC_HW_INCELL) {
		val = 0xAD;
		addr = FACTORY_REG_LINE_ADDR;
		rawdata_addr = FACTORY_REG_RAWDATA_ADDR;
	} else if (tdata->func->hwtype == IC_HW_MC_SC) {
		val = 0xAA;
		addr = FACTORY_REG_LINE_ADDR;
		rawdata_addr = 0x1f;
	} else {
		val = 0x0;
		addr = FACTORY_REG_RAWDATA_SADDR_SC;
		rawdata_addr = FACTORY_REG_RAWDATA_ADDR_SC;
	}

	cmd_read_rawdata[0] = rawdata_addr;
	cmd_read_rawdata[1] = 0x00;
	cmd_read_rawdata[2] = 0x00;
	
	byte_num = tdata->node.node_num * 2;
	ret = btl_read_rawdata(tdata, cmd_read_rawdata, sizeof(cmd_read_rawdata),byte_num, data);
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("read rawdata fail\n");
		return ret;
	}

	return 0;
}

/*
 * read raw data with lose frame and obtain averrage value
 */
static int read_sc_rawdata(
	struct btl_test *tdata,
	int byte_num,
	int *data)
{
	int ret = 0;
	u8 rawdata_addr = 0;
	u8 cmd_sc_readrawdata[3] = {0x00};

	rawdata_addr = FACTORY_REG_RAWDATA_ADDR_MC_SC;

	cmd_sc_readrawdata[0] = rawdata_addr;
	cmd_sc_readrawdata[1] = 0x00;
	cmd_sc_readrawdata[2] = 0x00;

	/* enter factory mode */
	ret = btl_enter_factory_mode();
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("failed to enter factory mode,ret=%d\n", ret);
		return ret;
	}

	/* start scanning */
	ret = btl_start_scan();
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("scan fail\n");
		return ret;
	}

	if (tdata->func->raw_u16)
		ret = btl_read_mass_data_u16(cmd_sc_readrawdata, sizeof(cmd_sc_readrawdata), byte_num, data);
	else
		ret = btl_read_mass_data(cmd_sc_readrawdata, sizeof(cmd_sc_readrawdata), byte_num, data);
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("read rawdata fail\n");
		return ret;
	}

	return 0;
}

/*
 * btl_chip_clb - auto clb
 */
int btl_chip_clb(void)
{
	int ret = 0;
	u8 val = 0;
	int times = 0;

	/* start clb */
	ret = btl_test_write_reg(FACTORY_REG_CLB, 0x04);
	if (ret) {
		BTL_TEST_SAVE_ERR("write start clb fail\n");
		return ret;
	}

	while (times++ < FACTORY_TEST_RETRY) {
		btl_sys_delay(FACTORY_TEST_RETRY_DELAY);
		ret = btl_test_read_reg(FACTORY_REG_CLB, &val);
		if ((0 == ret) && (0x02 == val)) {
			/* clb ok */
			break;
		} else
			BTL_TEST_DBG("reg%x=%x,retry:%d", FACTORY_REG_CLB, val, times);
	}

	if (times >= FACTORY_TEST_RETRY) {
		BTL_TEST_SAVE_ERR("chip clb timeout\n");
		return -EIO;
	}

	return 0;
}

/*
 * btl_get_cb_incell - get cb data for incell IC
 */
int btl_get_cb_incell(u16 saddr, int byte_num, int *cb_buf)
{
	int ret = 0;
	int i = 0;
	u8 cb_addr = 0;
	u8 addr_h = 0;
	u8 addr_l = 0;
	int read_num = 0;
	int packet_num = 0;
	int packet_remainder = 0;
	int offset = 0;
	int addr = 0;
	u8 *data = NULL;

	data = btl_malloc(byte_num * sizeof(u8));
	if (data == NULL) {
		BTL_TEST_SAVE_ERR("cb buffer malloc fail\n");
		return -ENOMEM;
	}

	packet_num = byte_num / BYTES_PER_TIME;
	packet_remainder = byte_num % BYTES_PER_TIME;
	if (packet_remainder)
		packet_num++;
	read_num = BYTES_PER_TIME;

	BTL_TEST_INFO("cb packet:%d,remainder:%d", packet_num, packet_remainder);
	cb_addr = FACTORY_REG_CB_ADDR;
	for (i = 0; i < packet_num; i++) {
		offset = read_num * i;
		addr = saddr + offset;
		addr_h = (addr >> 8) & 0xFF;
		addr_l = addr & 0xFF;
		if ((i == (packet_num - 1)) && packet_remainder) {
			read_num = packet_remainder;
		}

		ret = btl_test_write_reg(FACTORY_REG_CB_ADDR_H, addr_h);
		if (ret) {
			BTL_TEST_SAVE_ERR("write cb addr high fail\n");
			goto TEST_CB_ERR;
		}
		ret = btl_test_write_reg(FACTORY_REG_CB_ADDR_L, addr_l);
		if (ret) {
			BTL_TEST_SAVE_ERR("write cb addr low fail\n");
			goto TEST_CB_ERR;
		}

		ret = btl_test_read(cb_addr, data + offset, read_num);
		if (ret) {
			BTL_TEST_SAVE_ERR("read cb fail\n");
			goto TEST_CB_ERR;
		}
	}

	for (i = 0; i < byte_num; i++) {
		cb_buf[i] = data[i];
	}

TEST_CB_ERR:
	btl_free(data);
	return ret;
}

int btl_get_cb_sc(int byte_num, int *cb_buf, enum byte_mode mode)
{
	int ret = 0;
	int i = 0;
	u8 cb_addr = 0;
	u8 cmd_getcb[3] = {0x00};
	u8 off_addr = 0;
	u8 off_h_addr = 0;
	struct btl_test *tdata = btl_ftest;
	u8 *cb = NULL;

	if ((tdata == NULL) || (tdata->func == NULL)) {
		BTL_TEST_SAVE_ERR("test/func is null\n");
		return -EINVAL;
	}

	cb = btl_malloc(byte_num * sizeof(u8));
	if (cb == NULL) {
		BTL_TEST_SAVE_ERR("malloc memory for cb buffer fail\n");
		return -ENOMEM;
	}

	if (tdata->func->hwtype == IC_HW_MC_SC ) {
		cb_addr = FACTORY_REG_MC_SC_CB_ADDR;
		off_addr = FACTORY_REG_MC_SC_CB_ADDR_OFF;
		off_h_addr = FACTORY_REG_MC_SC_CB_H_ADDR_OFF;
	} else if (tdata->func->hwtype == IC_HW_SC ) {
		cb_addr = FACTORY_REG_SC_CB_ADDR;
		off_addr = FACTORY_REG_SC_CB_ADDR_OFF;
	}

	cmd_getcb[0] = cb_addr;
	cmd_getcb[1] = 0x00;	
	cmd_getcb[2] = 0x00;

	ret = btl_test_read_bytes(cmd_getcb, sizeof(cmd_getcb), cb, byte_num);
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("read cb fail\n");
		goto cb_err;
	}

	if (mode == DATA_ONE_BYTE) {
		for (i = 0; i < byte_num; i++) {
			cb_buf[i] = cb[i];
		}
	} else if (mode == DATA_TWO_BYTE) {
		for (i = 0; i < byte_num; i = i + 2) {
			cb_buf[i >> 1] = (int)(((int)(cb[i]) << 8) + cb[i + 1]);
		}
	}

	ret = 0;
cb_err:
	btl_free(cb);
	return ret;
}

bool btl_compare_data(int *data, int min, int max, int min_vk, int max_vk, bool key)
{
	int i = 0;
	bool result = true;
	struct btl_test *tdata = btl_ftest;
	int rx = tdata->node.rx_num;
	int node_va = tdata->node.node_num - tdata->node.key_num;

	if (!data || !tdata->node_valid) {
		BTL_TEST_SAVE_ERR("data/node_valid is null\n");
		return false;
	}

	for (i = 0; i < node_va; i++) {
		if (tdata->node_valid[i] == 0)
			continue;

		if ((data[i] < min) || (data[i] > max)) {
			BTL_TEST_SAVE_ERR("test fail,node(%4d,%4d)=%5d,range=(%5d,%5d)\n",
							  i / rx + 1, i % rx + 1, data[i], min, max);
			result = false;
		}
	}

	if (key) {
		for (i = node_va; i < tdata->node.node_num; i++) {
			if (tdata->node_valid[i] == 0)
				continue;

			if ((data[i] < min_vk) || (data[i] > max_vk)) {
				BTL_TEST_SAVE_ERR("test fail,node(%4d,%4d)=%5d,range=(%5d,%5d)\n",
								  i / rx + 1, i % rx + 1,
								  data[i], min_vk, max_vk);
				result = false;
			}
		}
	}

	return result;
}

bool btl_compare_array(int *data, int *min, int *max, bool key)
{
	int i = 0;
	bool result = true;
	struct btl_test *tdata = btl_ftest;
	int rx = tdata->node.rx_num;
	int node_num = tdata->node.node_num;

	if (!data || !min || !max || !tdata->node_valid) {
		BTL_TEST_SAVE_ERR("data/min/max/node_valid is null\n");
		return false;
	}

	if (!key) {
		node_num -= tdata->node.key_num;
	}
	for (i = 0; i < node_num; i++) {
		if (tdata->node_valid[i] == 0)
			continue;
		if ((data[i] < min[i]) || (data[i] > max[i])) {
			BTL_TEST_SAVE_ERR("test fail,node(%4d,%4d)=%5d,range=(%5d,%5d)\n",
							  i / rx + 1, i % rx + 1, data[i], min[i], max[i]);
			result = false;
		}
	}

	return result;
}

bool btl_compare_diff(int *data, int *min, int *max, bool key)
{
	int i = 0;
	bool result = true;
	struct btl_test *tdata = btl_ftest;
	int rx = tdata->node.rx_num;
	int node_num = tdata->node.node_num;

	if (!data || !min || !max || !tdata->node_valid) {
		BTL_TEST_SAVE_ERR("data/min/max/node_valid is null\n");
		return false;
	}

	if (!key) {
		node_num -= tdata->node.key_num;
	}
	for (i = 0; i < node_num; i++) {
		if (tdata->node_valid[i] == 0)
			continue;
		if ((data[i] < min[i]) || (data[i] > max[i])) {
			BTL_TEST_SAVE_ERR("test fail,node(%4d,%4d)=%5d,range=(%5d,%5d)\n",
							  i / rx + 1, i % rx + 1, data[i], min[i], max[i]);
			result = false;
		}
	}

	return result;
}

void btl_show_threshold_value(int *data, bool key, char *str)
{
#if TXT_SUPPORT
	int i = 0;
	int j = 0;
	struct btl_test *tdata = btl_ftest;
	int node_num = tdata->node.node_num;
	int tx_num = tdata->node.tx_num;
	int rx_num = tdata->node.rx_num;
	BTL_TEST_FUNC_ENTER();

	for (i = 0; i < tx_num; i++) {
		BTL_TEST_SAVE_INFO("%s/Tx_%02d:	 ", str, i + 1);
		for (j = 0; j < rx_num; j++) {
			BTL_TEST_SAVE_INFO("%5d, ", data[i * rx_num + j]);
		}
		BTL_TEST_SAVE_INFO("\n");
	}
	if (key) {
		BTL_TEST_SAVE_INFO("%s/Tx_%02d:	 ", str, tx_num + 1);
		for (i = tx_num * rx_num; i < node_num; i++) {
			BTL_TEST_SAVE_INFO("%5d, ",	 data[i]);
		}
		BTL_TEST_SAVE_INFO("\n\n");
	}
	BTL_TEST_FUNC_EXIT();
#endif
} 

/*
 * btl_show_data - show and save test data to testresult.txt
 */
void btl_show_data(int *data, bool key)
{
#if TXT_SUPPORT
	int i = 0;
	int j = 0;
	struct btl_test *tdata = btl_ftest;
	int node_num = tdata->node.node_num;
	int tx_num = tdata->node.tx_num;
	int rx_num = tdata->node.rx_num;

	BTL_TEST_FUNC_ENTER();
	for (i = 0; i < tx_num; i++) {
		BTL_TEST_SAVE_INFO("Ch/Tx_%02d:	 ", i + 1);
		for (j = 0; j < rx_num; j++) {
			BTL_TEST_SAVE_INFO("%5d, ", data[i * rx_num + j]);
		}
		BTL_TEST_SAVE_INFO("\n");
	}

	if (key) {
		BTL_TEST_SAVE_INFO("Ch/Tx_%02d:	 ", tx_num + 1);
		for (i = tx_num * rx_num; i < node_num; i++) {
			BTL_TEST_SAVE_INFO("%5d, ",	 data[i]);
		}
		BTL_TEST_SAVE_INFO("\n");
	}
	BTL_TEST_FUNC_EXIT();
#endif
}

/* mc_sc only */
/* Only V3 Pattern has mapping & no-mapping */
int btl_mapping_switch(u8 mapping)
{
	int ret = 0;
	u8 val = 0xFF;
	struct btl_test *tdata = btl_ftest;

	if (tdata->v3_pattern) {
		ret = btl_test_read_reg(FACTORY_REG_NOMAPPING, &val);
		if (ret < 0) {
			BTL_TEST_ERROR("read 0x54 register fail");
			return ret;
		}

		if (val != mapping) {
			ret = btl_test_write_reg(FACTORY_REG_NOMAPPING, mapping);
			if (ret < 0) {
				BTL_TEST_ERROR("write 0x54 register fail");
				return ret;
			}
			btl_sys_delay(FACTORY_TEST_DELAY);
		}
	}

	return 0;
}

bool btl_get_fw_wp(u8 wp_ch_sel, enum wp_type water_proof_type)
{
	bool fw_wp_state = false;

	switch (water_proof_type) {
	case WATER_PROOF_ON:
		/* bit5: 0-check in wp on, 1-not check */
		fw_wp_state = !(wp_ch_sel & 0x20);
		break;
	case WATER_PROOF_ON_TX:
		/* Bit6:  0-check Rx+Tx in wp mode	1-check one channel
		   Bit2:  0-check Tx in wp mode;  1-check Rx in wp mode
		*/
		fw_wp_state = (!(wp_ch_sel & 0x40) || !(wp_ch_sel & 0x04));
		break;
	case WATER_PROOF_ON_RX:
		fw_wp_state = (!(wp_ch_sel & 0x40) || (wp_ch_sel & 0x04));
		break;
	case WATER_PROOF_OFF:
		/* bit7: 0-check in wp off, 1-not check */
		fw_wp_state = !(wp_ch_sel & 0x80);
		break;
	case WATER_PROOF_OFF_TX:
		/* Bit1-0:	00-check Tx in non-wp mode
					01-check Rx in non-wp mode
					10:check Rx+Tx in non-wp mode
		*/
		fw_wp_state = (((wp_ch_sel & 0x03) == 0X0) || ((wp_ch_sel & 0x03) == 0x02));
		break;
	case WATER_PROOF_OFF_RX:
		fw_wp_state = (((wp_ch_sel & 0x03) == 0X01) || ((wp_ch_sel & 0x03) == 0X02));
		break;
	default:
		break;
	}

	return fw_wp_state;
}

int btl_get_cb_mc_sc(u8 wp, int byte_num, int *cb_buf, enum byte_mode mode)
{
	int ret = 0;
	/* read cb */
	ret = btl_get_cb_sc(byte_num, cb_buf, mode);
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("get sc cb fail\n");
		return ret;
	}

	return 0;
}

int btl_get_rawdata_mc_sc(enum wp_type wp, int *data)
{
	int ret = 0;
	int byte_num = 0;
	int i = 0;
	struct btl_test *tdata = btl_ftest;
	int *frame_rawdata0 = NULL;
	int *frame_rawdata1 = NULL;
	
	if ((tdata == NULL) || (tdata->func == NULL)) {
		BTL_TEST_SAVE_ERR("test/func is null\n");
		return -EINVAL;
	}

	byte_num = tdata->sc_node.node_num * 2;

	frame_rawdata0 = btl_malloc(byte_num * 2 * sizeof(u8));
	if (frame_rawdata0 == NULL) {
		BTL_TEST_SAVE_ERR("frame_rawdata0 malloc fail\n");
		ret = -ENOMEM;
		goto frame_rawdata0_err;
	}

	frame_rawdata1 = btl_malloc(byte_num * 2 * sizeof(u8));
	if (frame_rawdata1 == NULL) {
		BTL_TEST_SAVE_ERR("frame_rawdata1 malloc fail\n");
		ret = -ENOMEM;
		goto frame_rawdata1_err;
	}

	/* Lose 5 frame rawdata */
	for (i = 0; i < 6; i++) {
		ret = read_sc_rawdata(tdata, byte_num, frame_rawdata0);
		if (ret < 0) {	
			BTL_TEST_SAVE_ERR("read sc rawdata fail\n");
			goto read_sc_rawdata_err;
		}
	}
	
	/* Abtain one more frame rawdata */
	ret = read_sc_rawdata(tdata, byte_num, frame_rawdata1);
	if (ret < 0) {	
		BTL_TEST_SAVE_ERR("read sc rawdata fail\n");
		goto read_sc_rawdata_err;
	}

	/* Obtain rawdata through averrage value of frame_rawdata0 and frame_rawdata1 */
	for (i = 0; i < (byte_num / 2); i++)
	{
		data[i] = (frame_rawdata0[i] + frame_rawdata1[i]) / 2;
	}


read_sc_rawdata_err:
	btl_free(frame_rawdata1);
frame_rawdata1_err:
	btl_free(frame_rawdata0);
frame_rawdata0_err:
	return ret;
}

int btl_get_relative_rawdata_value(int tx_num, int rx_num, int *rawdata)
{
	int i = 0, j = 0, k = 0;
	unsigned long result[6] = {0};
	unsigned long temp = 0;
	int *data = NULL;
	data = btl_malloc(tx_num * rx_num * sizeof(int));

	if (data == NULL) {
		BTL_TEST_SAVE_ERR("data malloc fail\n");
		return -ENOMEM;
	}
	for (i = 0; i < tx_num; i++)
	{
		for (j = 0; j < rx_num; j++)
		{
			if (rawdata[i*rx_num + j] == 0) {
				continue;
			}
			if (j == 0) {
				result[LEFT_MINUS_CENTER] = 0;
				result[LEFT_MINUS_RIGHT] = 0;
			} else {
				result[LEFT_MINUS_CENTER] = ABS((rawdata[i * rx_num + j - 1] * MULTIFLYING_POWER_VALUE - rawdata[i * rx_num + j] * MULTIFLYING_POWER_VALUE) / rawdata[i * rx_num + j]);
				if (j != (rx_num - 1)) {
					result[LEFT_MINUS_RIGHT] = ABS((rawdata[i * rx_num + j - 1] * MULTIFLYING_POWER_VALUE - rawdata[i * rx_num + j + 1] *  MULTIFLYING_POWER_VALUE) / rawdata[i * rx_num + j]);
				}
			}
			if (j == (rx_num - 1)) {
				result[RIGHT_MINUS_CENTER] = 0;
				result[LEFT_MINUS_RIGHT] = 0;
			} else {
				result[RIGHT_MINUS_CENTER] = ABS((rawdata[i * rx_num + j + 1] * MULTIFLYING_POWER_VALUE	 - rawdata[i * rx_num + j] * MULTIFLYING_POWER_VALUE) / rawdata[i * rx_num + j]);
			}
			if (i == 0) {
				result[TOP_MINUS_CENTER] = 0;
				result[TOP_MINUS_BOTTOM] = 0;
			} else {
				result[TOP_MINUS_CENTER] = ABS((rawdata[(i - 1) * rx_num + j] * MULTIFLYING_POWER_VALUE - rawdata[i * rx_num + j] * MULTIFLYING_POWER_VALUE)  / rawdata[i * rx_num + j]);
				if (i != (tx_num - 1)) {
					result[TOP_MINUS_BOTTOM] = ABS((rawdata[(i - 1) * rx_num + j] * MULTIFLYING_POWER_VALUE - rawdata[(i + 1) * rx_num + j] * MULTIFLYING_POWER_VALUE)	/ rawdata[i * rx_num + j]);
				}
			}
			if (i == (tx_num - 1)) {
				result[BOTTOM_MINUS_CENTER] = 0;
				result[TOP_MINUS_BOTTOM] = 0;
			} else {
				result[BOTTOM_MINUS_CENTER] = ABS((rawdata[(i + 1) * rx_num + j] * MULTIFLYING_POWER_VALUE - rawdata[i * rx_num + j]* MULTIFLYING_POWER_VALUE)  / rawdata[i * rx_num + j]);
			}
			temp = 0;
			for (k = 0; k < 6; k++) {
				temp = MAX(temp,result[k]);
			}
			data[i * rx_num + j] = temp;
		}
	}
	memcpy(rawdata,data,tx_num*rx_num*sizeof(int));
	btl_free(data);
	return 0;
}
int btl_get_rawdata_mc(u8 fre, u8 fir, int *rawdata)
{
	int ret = 0;
	int i = 0;

	if (rawdata ==NULL) {
		BTL_TEST_SAVE_ERR("rawdata buffer is null\n");
		return -EINVAL;
	}
	/* get rawdata */
	for (i = 0; i < 5; i++) {
		/* lost 3 frames, in order to obtain stable data */
		ret = btl_get_rawdata(rawdata);
	}
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("get rawdata fail,ret=%d\n", ret);
		return ret;
	}

	return 0;
}

int btl_short_get_adc_data_mc(u8 retval, int byte_num, int *adc_buf, u8 mode)
{
	int ret = 0;
	u8 short_state_reg = 0;
	u8 cmd_read_short[3] = {0x00};
	u8 cmd_set_volsc[3] = {0x00};
	u8 short_en_reg = 0;
	u8 short_data_reg = 0;
	struct btl_test *tdata = btl_ftest;

	BTL_TEST_FUNC_ENTER();

	/* enter factory mode */
	ret = btl_enter_factory_mode();
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("failed to enter factory mode,ret=%d\n", ret);
		return ret;
	}

	/* set voltage and scan count */
	cmd_set_volsc[0] = REG_VOL_SCAN_COUNT_SET;
	cmd_set_volsc[1] = 0x01;
	cmd_set_volsc[2] = 0x03;
	ret = btl_test_bus_write(cmd_set_volsc, sizeof(cmd_set_volsc));
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("set_vol_scan_count fail\n");
		return ret;
	}

	/* start scanning */
	ret = btl_start_short_test();
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("scan fail\n");
		return ret;
	}

	if (tdata->func->mc_sc_short_v2) {
		short_en_reg = FACTROY_REG_SHORT2_TEST_EN;
		short_state_reg = FACTROY_REG_SHORT2_TEST_STATE;
		short_data_reg = FACTORY_REG_SHORT2_ADDR_MC;
	} else {
		short_en_reg = FACTROY_REG_SHORT_TEST_EN;
		short_state_reg = FACTROY_REG_SHORT_TEST_EN;
		short_data_reg = FACTORY_REG_SHORT_ADDR_MC;
	}
	cmd_read_short[0] = short_data_reg;
	cmd_read_short[1] = 0x00;
	cmd_read_short[2] = 0x00;
	
	ret = btl_read_mass_data(cmd_read_short, sizeof(cmd_read_short), byte_num, adc_buf);
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("get short(adc) data fail\n");
	}

	BTL_TEST_DBG("adc data:\n");
	btl_print_buffer(adc_buf, byte_num / 2, 0);

	BTL_TEST_FUNC_EXIT();
	return ret;
}

bool btl_compare_mc_sc(bool tx_check, bool rx_check, int *data, int *min, int *max)
{
	int i = 0;
	bool result = true;
	struct btl_test *tdata = btl_ftest;

	if (tx_check) {
		for (i = 0; i < tdata->sc_node.tx_num; i++) {
			if (0 == tdata->node_valid_sc[i])
				continue;

			if ((data[i] < min[i]) || (data[i] > max[i])) {
				BTL_TEST_SAVE_ERR("test fail,tx%d=%5d,range=(%5d,%5d)\n",
								  i + 1, data[i], min[i], max[i]);
				result = false;
			}
		}
	}

	if (rx_check) {
		for (i = tdata->sc_node.tx_num; i < tdata->sc_node.node_num; i++) {
			if (0 == tdata->node_valid_sc[i])
				continue;
			if ((data[i] < min[i]) || (data[i] > max[i])) {
				BTL_TEST_SAVE_ERR("test fail,rx%d=%5d,range=(%5d,%5d)\n",
								   i - tdata->sc_node.tx_num + 1,
								   data[i], min[i], max[i]);
				result = false;
			}
		}
	}

	return result;
}

bool btl_compare_sc_cb(bool tx_check, bool rx_check, int *data, int *min, int *max)
{
	int i = 0;
	bool result = true;
	struct btl_test *tdata = btl_ftest;

	if (tx_check) {
		for (i = 0; i < tdata->sc_node.tx_num; i++) {
			if (0 == tdata->node_valid_sc[i])
				continue;

			 if ((data[i] == min[i]) || (data[i] == max[i]) || (data[i] == 0)) {
				BTL_TEST_SAVE_ERR("test fail,tx%d=%5d,range=(%5d,%5d, %5d)\n",
								  i + 1, data[i], min[i], max[i], 0);
				result = false;
			}
		}
	}

	if (rx_check) {
		for (i = tdata->sc_node.tx_num; i < tdata->sc_node.node_num; i++) {
			if (0 == tdata->node_valid_sc[i])
				continue;
			 if ((data[i] == min[i]) || (data[i] == max[i]) || (data[i] == 0)) {
				BTL_TEST_SAVE_ERR("test fail,rx%d=%5d,range=(%5d,%5d,%5d)\n",
								   i - tdata->sc_node.rx_num + 1,
								   data[i], min[i], max[i], 0);
				result = false;
			}
		}
	}

	return result;
}
 void btl_show_mc_sc_threshold_value(int *data, bool tx_check, bool rx_check, char *str)
{
	int i = 0;
	struct btl_test *tdata = btl_ftest;

	if (tx_check) {
		BTL_TEST_SAVE_INFO("%sSCap Tx: ", str);
		for (i = 0; i < tdata->sc_node.tx_num; i++) {
			BTL_TEST_SAVE_INFO( "%5d, ", data[i]);
		}
		BTL_TEST_SAVE_INFO("\n");
	}
	if (rx_check) 	{
		BTL_TEST_SAVE_INFO("%s/SCap Rx: ", str);
		for (i = tdata->sc_node.tx_num; i < tdata->sc_node.node_num; i++) {
			BTL_TEST_SAVE_INFO( "%5d, ", data[i]);
		}
		BTL_TEST_SAVE_INFO("\n");
	}
}

void btl_show_sc_threshold_value(int *data, char *str)
{
#if TXT_SUPPORT
	int i = 0;
	struct btl_test *tdata = btl_ftest;

	for (i = 0; i < tdata->sc_node.node_num; i++) {
		BTL_TEST_SAVE_INFO( "%5d, ", data[i]);
	}
	BTL_TEST_SAVE_INFO("\n");
#endif
} 

void btl_show_data_mc_sc(int *data)
{
#if TXT_SUPPORT
	int i = 0;
	struct btl_test *tdata = btl_ftest;

	BTL_TEST_SAVE_INFO("SCap Tx: ");
	for (i = 0; i < tdata->sc_node.tx_num; i++) {
		BTL_TEST_SAVE_INFO( "%5d, ", data[i]);
	}
	BTL_TEST_SAVE_INFO("\n");

	BTL_TEST_SAVE_INFO("SCap Rx: ");
	for (i = tdata->sc_node.tx_num; i < tdata->sc_node.node_num; i++) {
		BTL_TEST_SAVE_INFO( "%5d, ", data[i]);
	}
	BTL_TEST_SAVE_INFO("\n");
#endif
}

void btl_show_data_sc(int *data)
{
#if TXT_SUPPORT
	int i = 0;
	struct btl_test *tdata = btl_ftest;

	for (i = 0; i < tdata->sc_node.node_num; i++) {
		BTL_TEST_SAVE_INFO( "%5d, ", data[i]);
	}
	BTL_TEST_SAVE_INFO("\n");
#endif
}

/* mc_sc end*/

int set_drive_voltage(u8 reg, u8 val)
{
	int ret = 0;
	u8 vol = 0;
	int i = 0;
	int j = 0;

	BTL_TEST_FUNC_ENTER();

	ret = btl_test_read_reg(reg, &vol);
	if ((ret >= 0) && (val == vol))
		return 0;

	for (i = 0; i < ENTER_WORK_FACTORY_RETRIES; i++) {
		ret = btl_test_write_reg(reg, val);
		if (ret >= 0) {
			btl_sys_delay(FACTORY_TEST_DELAY);
			for (j = 0; j < 20; j++) {
				ret = btl_test_read_reg(reg, &vol);
				if ((ret >= 0) && (val == vol)) {
					BTL_TEST_INFO("set voltage success");
					return 0;
				} else
					btl_sys_delay(FACTORY_TEST_DELAY);
			}
		}

		btl_sys_delay(50);
	}

	if (i >= ENTER_WORK_FACTORY_RETRIES) {
		BTL_TEST_ERROR("set voltage fail");
		return -EIO;
	}

	BTL_TEST_FUNC_EXIT();
	return 0;

}

int get_drive_voltage(u8 reg, u8 *val)
{
	int ret = 0;
	ret = btl_test_read_reg(reg, val);
	if (ret < 0)
		return ret;
	else
		return 0;
}
/* rd add start */
int set_drive_freq(u8 reg, u8 *val)
{
	int ret = 0;
	u8 freq[2] = {0};
	int i = 0;
	int j = 0;
	u8 cmd[3] = {0x00};

	BTL_TEST_FUNC_ENTER();
	 if (*val == 0) {
		BTL_TEST_INFO("set freq == 0 error! /n");
		return 0;
	}
	cmd[0] = reg;
	cmd[1] = val[0];
	cmd[2] = val[1];

	ret = btl_test_bus_read(&reg, 1, freq, 2);
	BTL_TEST_INFO("freq[0] : 0x%x, freq[1] : 0x%x",freq[0],freq[1]);
	if ((ret >= 0) && (!memcmp(val, freq, 2)))
		return 0;
	BTL_TEST_INFO("cmd[0] : 0x%x, cmd[1] : 0x%x, cmd[2] : 0x%x",cmd[0],cmd[1],cmd[2]);
	for (i = 0; i < ENTER_WORK_FACTORY_RETRIES; i++) {
		ret = btl_test_bus_write(cmd, sizeof(cmd));
		if (ret >= 0) {
			btl_sys_delay(FACTORY_TEST_DELAY);
			for (j = 0; j < 20; j++) {
				ret = btl_test_bus_read(&reg, 1, freq, 2);
				BTL_TEST_INFO("ret : 0x%x, freq[0] : 0x%x, freq[1] : 0x%x",ret,freq[0],freq[1]);
				if ((ret >= 0) && (!memcmp(val, freq, 2))) {
					BTL_TEST_INFO("set freq success");
					return 0;
				} else
					btl_sys_delay(FACTORY_TEST_DELAY);
			}
		}

		btl_sys_delay(50);
	}

	if (i >= ENTER_WORK_FACTORY_RETRIES) {
		BTL_TEST_ERROR("set freq fail");
		return -EIO;
	}

	BTL_TEST_FUNC_EXIT();
	return 0;

}

int get_drive_value_freq(u8 reg, u8 *val)
{
	int ret = 0;

	ret = btl_test_bus_read(&reg, 1, val, 2);
	if (ret < 0)
		return ret;
	else
		return 0;
}

int btl_get_panel_diff(int *data, int *hig_freq, int *low_freq)
{
	int ret = 0;
	int i = 0;
	u8 nor_freq[2] = { 0 };
	int byte_num = 0;
	int *frame_rawdata0 = NULL;
	int *frame_rawdata1 = NULL;
	u8 test_hig_freq[2] = {*hig_freq & 0xff, ((*hig_freq>>8) & 0xff)};
	u8 test_low_freq[2] = {*low_freq & 0xff, ((*low_freq>>8) & 0xff)};
	struct btl_test *tdata = btl_ftest;

	if (data == NULL) {
		BTL_TEST_SAVE_ERR("rawdata buffer is null\n");
		return -EINVAL;
	}

	byte_num = tdata->node.node_num * 2;
 
	frame_rawdata0 = btl_malloc(byte_num * 2 * sizeof(u8));
	if (frame_rawdata0 == NULL) {
		BTL_TEST_SAVE_ERR("frame_rawdata0 malloc fail\n");
		ret = -ENOMEM;
		goto frame_rawdata0_err;
	}

	frame_rawdata1 = btl_malloc(byte_num * 2 * sizeof(u8));
	if (frame_rawdata1 == NULL) {
		BTL_TEST_SAVE_ERR("frame_rawdata1 malloc fail\n");
		ret = -ENOMEM;
		goto frame_rawdata1_err;
	}

	/* obtain origin drive voltage */
	ret = get_drive_value_freq(FACTORY_REG_NORMAL_FREQ,nor_freq);
	if (ret < 0) {
		goto get_drive_freq_err;
	}

	ret = set_drive_freq(FACTORY_REG_NORMAL_FREQ, test_hig_freq);
	if (ret < 0) {
		goto get_rawdata_err;
	}
	/* get rawdata for current*/
	for (i = 0; i < 6; i++) {
		ret = btl_get_rawdata(frame_rawdata0);
		if (ret < 0) {
			BTL_TEST_SAVE_ERR("get rawdata fail,ret=%d\n", ret);
			goto restore_reg_err;
		}

	}

	ret = set_drive_freq(FACTORY_REG_NORMAL_FREQ, test_low_freq);
	if (ret < 0) {
		goto restore_reg_err;
	}
	/* get rawdata for current*/
	 for (i = 0; i < 6; i++) {
		ret = btl_get_rawdata(frame_rawdata1);
		if (ret < 0) {
			BTL_TEST_SAVE_ERR("get rawdata fail,ret=%d\n", ret);
			goto restore_reg_err;
		}
	}

	/* computer diff */
	for (i = 0; i < (byte_num/2); i++) {
		data[i] = frame_rawdata1[i] - frame_rawdata0[i];
	}	

restore_reg_err:
	ret = set_drive_freq(FACTORY_REG_NORMAL_FREQ, nor_freq);
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("restore drive freq reg fail,ret=%d\n", ret);
	}
get_rawdata_err:
get_drive_freq_err:
	btl_free(frame_rawdata1);
frame_rawdata1_err:
	btl_free(frame_rawdata0);
frame_rawdata0_err:
	return ret;
}

int btl_select_test_mode(u8 proValue, u8 scanMode, u8 freq, u8 vdd, u8 cf, u8 tValueHigh, u8 tValueLow)
{
	int ret = 0;
	u8 tValue[2] = {0x00};

	BTL_TEST_FUNC_ENTER();

	tValue[0] = tValueLow;
	tValue[1] = tValueHigh;

	ret = btl_test_write_reg(FACTORY_REG_PRO_SC, proValue);
	if (ret<0)
	{
		ret = -1;
		BTL_TEST_ERROR("set proValue error");
		goto err;
	}
	
	ret = btl_test_write_reg(FACTORY_REG_SCAN_SC, scanMode);
	if (ret<0)
	{
		ret = -1;
		BTL_TEST_ERROR("set scanMode error");
		goto err;
	}
	
	ret = btl_test_write_reg(FACTORY_REG_FREQ_SC, freq);
	if (ret<0)
	{
		ret = -1;
		BTL_TEST_ERROR("set freq error");
		goto err;
	}

	ret = btl_test_write_reg(FACTORY_REG_VDD_SC, vdd);
	if (ret<0)
	{
		ret = -1;
		BTL_TEST_ERROR("set vdd error");
		goto err;
	}
	
	ret = btl_test_write_reg(FACTORY_REG_CF_SC, cf);
	if (ret<0)
	{
		ret = -1;
		BTL_TEST_ERROR("set cf error");
		goto err;
	}
	
	ret = btl_test_write(FACTORY_REG_TVALUE_SC, tValue, sizeof(tValue));
	if (ret<0)
	{
		ret = -1;
		BTL_TEST_ERROR("set tValue error");
		goto err;
	}

err:
	BTL_TEST_FUNC_EXIT();
	return ret;

	
}

int btl_dram_test(void)
{
	int ret = 0;
	int retry = 100;
	unsigned char buf = 0x55;

	/* enter factory mode */
	ret = btl_enter_factory_mode();
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("failed to enter factory mode,ret=%d\n", ret);
		return ret; 
	}

	/* dram test start */
	ret = btl_test_write_reg(FACTORY_REG_DRAM, buf);
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("failed to start dram test,ret=%d\n", ret);
		return ret; 
	}

	/* obtain dram test result */
	do {
		btl_sys_delay(35);
		ret = btl_test_read_reg(FACTORY_REG_DRAM, &buf);
		if (ret < 0) {
			BTL_TEST_SAVE_ERR("failed to get dram test result,ret=%d\n", ret);
			return ret; 
		}
	
		if (buf == 0x66) {
			ret = 0;
			break;
		}
		else {
			ret = -1;
		}
	} while (retry--);
	
	return ret;
}

unsigned long btl_sqrt(unsigned long x)
{
	unsigned long op, res, one;
	op = x;
	res = 0;
	one = 1UL << (BITS_PER_LONG - 2);

	while (one > op)
		one >>= 2;
 
	while (one != 0) {
		if (op >= res + one) {
			op = op - (res + one);
			res = res +	 2 * one;
		}
		res /= 2;
		one /= 4;
	}
	return res;
}

void btl_sc_cb_to_slope(u8 *cb, u8 rx_num, u8 key_num, u8 pattern_type, int *result)
{
	int i = 0;
	int j = 0;
	u8 row = 0;
	u8 col = 0;
	int sum = 0;
	int avg = 0;
	int absValue = 0;
	int variance = 0;
	int pow = 0;
	int avgVariance = 0;
	int stdVariance = 0;
	int tmpCb[RX_NUM_MAX] = {0x00};
	bool cbInvalid = false;
	int validCount = 0;
	int tmpData[RX_NUM_MAX] = {0x00};
	int tmpResult[RX_NUM_MAX] = {0x00};

	BTL_TEST_FUNC_ENTER();
	BTL_TEST_INFO("pattern_type = %d, rx_num = %d, key_num = %d", pattern_type, rx_num, key_num);
	
	if (pattern_type == 0) {
		row = 2;
	}

	if (pattern_type == 3) {
		row = 4;
	}	

	col = rx_num / row;

	BTL_TEST_INFO("row = %d, col = %d", row, col);

	BTL_TEST_INFO("cb origin value:");
	for (i = 0; i < row; i++) {
		for (j = 0; j < col; j++) 	{
			printk("%d(%x),", cb[i * col + j]);
		}
		BTL_TEST_INFO("\n");
	}

	for (j = 0; j < col; j++) 	{
		tmpData[j] = cb[j * 2];
		tmpData[j + col] =	cb[j * 2 +1];
		if (row == 4) {
			tmpData[j + 2 * col] = cb[(j + col) *2];
			tmpData[j + 3 * col] = cb[(j + col) *2 + 1];
		}
	}
	BTL_TEST_INFO("cb transter value:\n");
	for (i = 0; i < row; i++) {
		for (j = 0; j < col; j++) {
			printk("%d(%x),",tmpData[i * col + j], tmpData[i * col + j]);
		}
		BTL_TEST_INFO("\n");
	}	
	for (i = 0; i < row; i++) {
		BTL_TEST_INFO("Start %d process", i);
		BTL_TEST_INFO("Computer average value");
		for (j = 0; j < col; j++) {
			if ((j == 0) || (j == (col -1))) {
				continue;
			}
			sum += tmpData[i * col + j];
			BTL_TEST_INFO("cb = %d,sum = %d", tmpData[i * col + j], sum);
		}
		avg = sum * 10 / (col - 2);
		if ((avg % 10) > 4) {
			avg = avg / 10 + 1;
		} else {
			avg = avg / 10;
		}
		BTL_TEST_INFO("avg = %d", avg);
		
		sum = 0;
			
		BTL_TEST_INFO("Computer pow of cb - avg");
		for (j = 0; j < col; j++) {
			if ((j == 0) || (j == (col -1))) {
				continue;
			}
			absValue = ABS(tmpData[i * col + j] - avg);
			pow = absValue * absValue;
			variance += pow;
			BTL_TEST_INFO("absValue = %d, pow = %d, variance = %d", absValue, pow, variance);
		}
		
		avgVariance = variance / (col - 2);
		
		stdVariance = btl_sqrt(avgVariance);
		BTL_TEST_INFO("avgVariance = %d,stdVariance = %d", avgVariance, stdVariance);

		variance = 0;
		
		BTL_TEST_INFO("Delete incalid cb");
		for (j = 0; j < col; j++) {
			if ((j == 0) || (j == (col -1))) {
				continue;
			}
			
			if ((tmpData[i * col +j] < (avg - 3 * stdVariance)) || (tmpData[i * col +j] > (avg + 3 * stdVariance))) {
				cbInvalid = true;
				BTL_TEST_INFO("cb[%d] = %d is invalid", j, tmpData[i * col + j]);
				continue;
			}
			tmpCb[validCount++] = tmpData[i * col + j];
		}
		BTL_TEST_INFO("validCount = %d,cbInvalid = %d", validCount, cbInvalid);

		if (cbInvalid) {
			BTL_TEST_INFO("Need recomputer avgValue");
			for (j = 0; j < validCount; j++) {
				sum += tmpCb[j];
				BTL_TEST_INFO("tmpCb = %d, sum = %d", tmpCb[j], sum);
			}
			avg = sum * 10 / validCount;
			if ((avg % 10) > 4) {
				avg = avg / 10 + 1;
			} else {
				avg = avg / 10;
			}
			BTL_TEST_INFO("sum = %d, avg = %d", sum, avg);
			sum = 0;
		}
		
		validCount = 0;
		
		BTL_TEST_INFO("slope:");
		for (j = 0; j < col; j++) {
			tmpResult[i * col + j] = tmpData[i * col + j] * MULTIFLYING_POWER_VALUE / avg;
			printk("%d,", tmpResult[i * col + j]);
		}
		BTL_TEST_INFO("\n");
		BTL_TEST_INFO("End %d process\n", i);
	}

	BTL_TEST_INFO("ratio origin value:\n");
	for (i = 0; i < row; i++) {
		for (j = 0; j < col; j++) {
			printk("%d,",tmpResult[i * col + j]);
		}
		BTL_TEST_INFO("\n");
	}
	
	for (j = 0; j < col; j++) {
		result[2 * j] = tmpResult[j];
		result[2 * j + 1] = tmpResult[j + col];
		if (row == 4) {
			result[2 * j + 2 * col] = tmpResult[j + 2 * col];
			result[2 * j + 2 * col + 1] = tmpResult[j + 3 * col];
		}
	}
	BTL_TEST_INFO("ratio transter value:\n");
	for (i = 0; i < row; i++) {
		for (j = 0; j < col; j++) {
			printk("%d,",result[i * col + j]);
		}
		BTL_TEST_INFO("\n");
	}

	for (i = 0; i < key_num; i++) {
		result[rx_num + i] = cb[rx_num + i];
	}
	BTL_TEST_FUNC_EXIT();
}

#if CSV_SUPPORT || TXT_SUPPORT
static int btl_test_save_test_data(char *file_name, char *data_buf, int len)
{
	BTL_TEST_FUNC_ENTER();
	tpd_copy_to_tp_firmware_data(data_buf);	
	BTL_TEST_FUNC_EXIT();
	return 0;
}

#if defined(TEST_SAVE_FAIL_RESULT) && TEST_SAVE_FAIL_RESULT
void btl_test_save_fail_result(
	struct btl_test *tdata, char *prefix, char *suffix, char *buf, int len)
{
	char file_name[128];

	if (tdata->result == false) {
		snprintf(file_name, 128, "%s_%ld_%ld%s", prefix,
				 (long)tdata->tv.tv_sec, (long)tdata->tv.tv_usec, suffix);
		btl_test_save_test_data(file_name, buf, len);
	}
}
#endif
#endif

static int btl_test_malloc_free_data_txt(struct btl_test *tdata, bool allocate)
{
#if TXT_SUPPORT
	if (allocate == true) {
		tdata->testresult = vmalloc(TXT_BUFFER_LEN);
		if (NULL == tdata->testresult) {
			BTL_TEST_ERROR("tdata->testresult malloc fail\n");
			return -ENOMEM;
		}

		tdata->testresult_len = 0;
		BTL_TEST_SAVE_INFO("FW version:0x%02x\n", tdata->fw_ver);
		BTL_TEST_SAVE_INFO("tx_num:%d, rx_num:%d, key_num:%d\n",
						   tdata->node.tx_num, tdata->node.rx_num,
						   tdata->node.key_num);
	} else {
		if (tdata->testresult) {
			vfree(tdata->testresult);
			tdata->testresult = NULL;
		}
	}
#endif
	return 0;
}

#if CSV_SUPPORT
static int btl_test_get_item_count_scap_csv(int index)
{
	int ret = 0;
	int i = 0;
	int select = 0;
	u8 wc_sel = 0;
	u8 hc_sel = 0;
	u8 scap_select[4] = { 0 };

	/* get waterproof channel select */
	ret = btl_test_read_reg(FACTORY_REG_WC_SEL, &wc_sel);
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("read water_channel_sel fail,ret=%d\n", ret);
		return index;
	}

	ret = btl_test_read_reg(FACTORY_REG_HC_SEL, &hc_sel);
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("read high_channel_sel fail,ret=%d\n", ret);
		return index;
	}

	scap_select[0] = btl_get_fw_wp(wc_sel, WATER_PROOF_ON);
	scap_select[1] = btl_get_fw_wp(wc_sel, WATER_PROOF_OFF);
	scap_select[2] = (hc_sel & 0x03) ? 1 : 0;
	scap_select[3] = (hc_sel & 0x04) ? 1 : 0;

	for (i = 0; i < 4; i++) {
		if (scap_select[i])
			select++;
		if (select == index)
			break;
	}

	return (i + 1);
}
#endif

static void btl_test_save_data_csv(struct btl_test *tdata)
{
#if CSV_SUPPORT
	int i = 0;
	int j = 0;
	int index = 0;
	int k = 0;
	int tx = 0;
	int rx = 0;
	int node_num = 0;
	int offset = 0;
	int start_line = 11;
	int data_count = 0;
	char *csv_buffer = NULL;
	char *line2_buffer = NULL;
	int csv_length = 0;
	int line2_length = 0;
	int csv_item_count = 0;
	struct btl_test_data *td = &tdata->testdata;
	struct item_info *info = NULL;

	BTL_TEST_INFO("save data in csv format");
	csv_buffer = vmalloc(CSV_BUFFER_LEN);
	if (!csv_buffer) {
		BTL_TEST_ERROR("csv_buffer malloc fail\n");
		return ;
	}

	line2_buffer = vmalloc(CSV_LINE2_BUFFER_LEN);
	if (!line2_buffer) {
		BTL_TEST_ERROR("line2_buffer malloc fail\n");
		goto csv_save_err;
	}

	BTL_TEST_INFO("test item count:%d", td->item_count);
	/* line 1 */
	csv_length += snprintf(csv_buffer + csv_length, \
						   CSV_BUFFER_LEN - csv_length, \
						   "ECC, 85, 170, IC Name, %s, IC Code, %x\n", \
						   tdata->ini.ic_name, \
						   (tdata->ini.ic_code >> IC_CODE_OFFSET));

	/* line 2 */
	for (i = 0; i < td->item_count; i++) {
		info = &td->info[i];
		if (info->mc_sc) {
			node_num = tdata->sc_node.node_num;
			/* set max len of tx/rx to column */
			rx = (tdata->sc_node.tx_num > tdata->sc_node.rx_num)
				 ? tdata->sc_node.tx_num : tdata->sc_node.rx_num;
		} else {
			if (info->key_support && (tdata->node.key_num > 0))
				node_num = (tdata->node.tx_num + 1) * tdata->node.rx_num;
			else
				node_num = tdata->node.tx_num * tdata->node.rx_num;
			rx = tdata->node.rx_num;
		}

		if (info->datalen > node_num) {
			data_count = (info->datalen - 1 ) / node_num + 1;
			tx = (node_num - 1 ) / rx + 1;
		} else {
			data_count = 1;
			tx = ((info->datalen - 1) / rx) + 1;
		}

		for (j = 1; j <= data_count; j++) {
			index = j;

			if (tdata->func->hwtype == IC_HW_MC_SC) {
				/*MC_SC, rawdata index will be 2*/
				if ((info->code == CODE_M_RAWDATA_TEST) && (data_count == 1)) {
					index = 2;
				}

				/*MC_SC, SCAP index will be 1~4*/
				if ((info->code == CODE_M_SCAP_CB_TEST)
					|| (info->code == CODE_M_SCAP_RAWDATA_TEST)) {
					index = btl_test_get_item_count_scap_csv(j);
				}
			}

			line2_length += snprintf(line2_buffer + line2_length, \
									 CSV_LINE2_BUFFER_LEN - line2_length, \
									 "%s, %d, %d, %d, %d, %d, ", \
									 info->name, info->code, tx, rx,
									 start_line, index);
			start_line += tx;
			csv_item_count++;
		}
	}

	csv_length += snprintf(csv_buffer + csv_length, \
						   CSV_BUFFER_LEN - csv_length, \
						   "TestItem Num, %d, ", \
						   csv_item_count);

	if (line2_length > 0) {
		csv_length += snprintf(csv_buffer + csv_length, \
							   CSV_BUFFER_LEN - csv_length, \
							   "%s", line2_buffer);
	}

	/* line 3 ~ 10	"\n" */
	csv_length += snprintf(csv_buffer + csv_length, \
						   CSV_BUFFER_LEN - csv_length, \
						   "\n\n\n\n\n\n\n\n\n");

	/* line 11 ~ data area */
	for (i = 0; i < td->item_count; i++) {
		info = &td->info[i];
		if (!info->data) {
			BTL_TEST_ERROR("test item data is null");
			goto csv_save_err;
		}

		if (info->mc_sc) {
			offset = 0;
			for (j = 0; j < info->datalen;) {
				for (k = 0; k < tdata->sc_node.node_num; k++) {
					csv_length += snprintf(csv_buffer + csv_length, \
										   CSV_BUFFER_LEN - csv_length, \
										   "%d, ", info->data[offset + k]);
					if ((k + 1) == tdata->sc_node.rx_num) {
						csv_length += snprintf(csv_buffer + csv_length, \
											   CSV_BUFFER_LEN - csv_length, \
											   "\n");
					}
				}
				csv_length += snprintf(csv_buffer + csv_length, \
									   CSV_BUFFER_LEN - csv_length, \
									   "\n");
				offset += k;
				j += k;
			}
		} else {
			for (j = 0; j < info->datalen; j++) {
				csv_length += snprintf(csv_buffer + csv_length, \
									   CSV_BUFFER_LEN - csv_length, \
									   "%d, ", info->data[j]);
				if (((j + 1) % tdata->node.rx_num) == 0) {
					csv_length += snprintf(csv_buffer + csv_length, \
										   CSV_BUFFER_LEN - csv_length,
										   "\n");
				}
			}
		}
	}
	BTL_TEST_INFO("csv length:%d", csv_length);
	btl_test_save_test_data(BTL_CSV_FILE_NAME, csv_buffer, csv_length);

#if defined(TEST_SAVE_FAIL_RESULT) && TEST_SAVE_FAIL_RESULT
	btl_test_save_fail_result(tdata, "testdata_fail", ".csv",
							  csv_buffer, csv_length);
#endif


csv_save_err:
	if (line2_buffer) {
		vfree(line2_buffer);
		line2_buffer = NULL;
	}

	if (csv_buffer) {
		vfree(csv_buffer);
		csv_buffer = NULL;
	}
#endif
}

static void btl_test_save_result_txt(struct btl_test *tdata)
{
#if TXT_SUPPORT
	if (!tdata || !tdata->testresult) {
		BTL_TEST_ERROR("test result is null");
		return;
	}

	BTL_TEST_INFO("test result length in txt:%d", tdata->testresult_len);
	btl_test_save_test_data(BTL_TXT_FILE_NAME, tdata->testresult,
							tdata->testresult_len);

#if defined(TEST_SAVE_FAIL_RESULT) && TEST_SAVE_FAIL_RESULT
	btl_test_save_fail_result(tdata, "testresult_fail", ".txt",
							  tdata->testresult, tdata->testresult_len);
#endif

#endif
}

/*****************************************************************************
* Name: btl_test_save_data
* Brief: Save test data.
*		 If multi-data of MC, length of data package must be tx*rx,(tx+1)*rx
*		 If multi-data of MC-SC, length of data package should be (tx+rx)*2
*		 Need fill 0 when no actual data
* Input:
* Output:
* Return:
*****************************************************************************/
void btl_test_save_data(char *name, int code, int *data, int datacnt,
						bool mc_sc, bool key, bool result)
{
	int datalen = datacnt;
	struct btl_test *tdata = btl_ftest;
	struct btl_test_data *td = &tdata->testdata;
	struct item_info *info = &td->info[td->item_count];

	if (!name || !data) {
		BTL_TEST_ERROR("name/data is null");
		return ;
	}

	strlcpy(info->name, name, TEST_ITEM_NAME_MAX - 1);
	info->code = code;
	info->mc_sc = mc_sc;
	info->key_support = key;
	info->result = result;
	if (datalen <= 0) {
		if (mc_sc) {
			datalen = tdata->sc_node.node_num * 2;
		} else {
			if (key && (tdata->node.key_num > 0))
				datalen = (tdata->node.tx_num + 1) * tdata->node.rx_num;
			else
				datalen = tdata->node.tx_num * tdata->node.rx_num;

		}
	}

	BTL_TEST_DBG("name:%s,len:%d", name, datalen);
	info->data = btl_malloc(datalen * sizeof(int));
	if (!info->data) {
		BTL_TEST_ERROR("malloc memory for item(%d) data fail", td->item_count);
		info->datalen = 0;
		return ;
	}
	memcpy(info->data, data, datalen * sizeof(int));
	info->datalen = datalen;

	td->item_count++;
}

static void btl_test_free_data(struct btl_test *tdata)
{
	int i = 0;
	struct btl_test_data *td = &tdata->testdata;

	for (i = 0; i < td->item_count; i++) {
		if (td->info[i].data) {
			btl_free(td->info[i].data);
		}
	}
}

static int btl_test_malloc_free_incell(struct btl_test *tdata, bool allocate)
{
	struct incell_threshold *thr = &tdata->ic.incell.thr;
	int buflen = tdata->node.node_num * sizeof(int);

	if (allocate == true) {
		BTL_TEST_INFO("buflen:%d", buflen);
		btl_malloc_r(thr->rawdata_min, buflen);
		btl_malloc_r(thr->rawdata_max, buflen);
		if (tdata->func->rawdata2_support) {
			btl_malloc_r(thr->rawdata2_min, buflen);
			btl_malloc_r(thr->rawdata2_max, buflen);
		}
		btl_malloc_r(thr->cb_min, buflen);
		btl_malloc_r(thr->cb_max, buflen);
	} else {
		btl_free(thr->rawdata_min);
		btl_free(thr->rawdata_max);
		if (tdata->func->rawdata2_support) {
			btl_free(thr->rawdata2_min);
			btl_free(thr->rawdata2_max);
		}
		btl_free(thr->cb_min);
		btl_free(thr->cb_max);
	}

	return 0;
}

static int btl_test_malloc_free_mc_sc(struct btl_test *tdata, bool allocate)
{
	struct mc_sc_threshold *thr = &tdata->ic.mc_sc.thr;
	int buflen = tdata->node.node_num * sizeof(int);
	int buflen_sc = tdata->sc_node.node_num * sizeof(int);

	if (allocate == true) {
		btl_malloc_r(thr->rawdata_h_min, buflen);
		btl_malloc_r(thr->rawdata_h_max, buflen);
		if (tdata->func->rawdata2_support) {
			btl_malloc_r(thr->rawdata_l_min, buflen);
			btl_malloc_r(thr->rawdata_l_max, buflen);
		}
		btl_malloc_r(thr->tx_linearity_max, buflen);
		btl_malloc_r(thr->tx_linearity_min, buflen);
		btl_malloc_r(thr->rx_linearity_max, buflen);
		btl_malloc_r(thr->rx_linearity_min, buflen);

		btl_malloc_r(thr->scap_cb_off_min, buflen_sc);
		btl_malloc_r(thr->scap_cb_off_max, buflen_sc);
		btl_malloc_r(thr->scap_cb_on_min, buflen_sc);
		btl_malloc_r(thr->scap_cb_on_max, buflen_sc);
		btl_malloc_r(thr->scap_cb_hi_min, buflen_sc);
		btl_malloc_r(thr->scap_cb_hi_max, buflen_sc);
		btl_malloc_r(thr->scap_cb_hov_min, buflen_sc);
		btl_malloc_r(thr->scap_cb_hov_max, buflen_sc);

		btl_malloc_r(thr->scap_rawdata_off_min, buflen_sc);
		btl_malloc_r(thr->scap_rawdata_off_max, buflen_sc);
		btl_malloc_r(thr->scap_rawdata_on_min, buflen_sc);
		btl_malloc_r(thr->scap_rawdata_on_max, buflen_sc);
		btl_malloc_r(thr->scap_rawdata_hi_min, buflen_sc);
		btl_malloc_r(thr->scap_rawdata_hi_max, buflen_sc);
		btl_malloc_r(thr->scap_rawdata_hov_min, buflen_sc);
		btl_malloc_r(thr->scap_rawdata_hov_max, buflen_sc);

		btl_malloc_r(thr->panel_differ_min, buflen);
		btl_malloc_r(thr->panel_differ_max, buflen);
		btl_malloc_r(thr->panel_differ_vol, buflen);
		btl_malloc_r(thr->panel_differ_high_freq, buflen);		
		btl_malloc_r(thr->panel_differ_low_freq, buflen);
		btl_malloc_r(thr->short_cg, buflen);		
		btl_malloc_r(thr->short_cc, buflen);
		btl_malloc_r(thr->rel_rawdata_min, buflen);		
		btl_malloc_r(thr->rel_rawdata_max, buflen);
	} else {
		btl_free(thr->rawdata_h_min);
		btl_free(thr->rawdata_h_max);
		if (tdata->func->rawdata2_support) {
			btl_free(thr->rawdata_l_min);
			btl_free(thr->rawdata_l_max);
		}
		btl_free(thr->tx_linearity_max);
		btl_free(thr->tx_linearity_min);
		btl_free(thr->rx_linearity_max);
		btl_free(thr->rx_linearity_min);

		btl_free(thr->scap_cb_off_min);
		btl_free(thr->scap_cb_off_max);
		btl_free(thr->scap_cb_on_min);
		btl_free(thr->scap_cb_on_max);
		btl_free(thr->scap_cb_hi_min);
		btl_free(thr->scap_cb_hi_max);
		btl_free(thr->scap_cb_hov_min);
		btl_free(thr->scap_cb_hov_max);

		btl_free(thr->scap_rawdata_off_min);
		btl_free(thr->scap_rawdata_off_max);
		btl_free(thr->scap_rawdata_on_min);
		btl_free(thr->scap_rawdata_on_max);
		btl_free(thr->scap_rawdata_hi_min);
		btl_free(thr->scap_rawdata_hi_max);
		btl_free(thr->scap_rawdata_hov_min);
		btl_free(thr->scap_rawdata_hov_max);

		btl_free(thr->panel_differ_min);
		btl_free(thr->panel_differ_max);
		btl_free(thr->panel_differ_vol);
		btl_free(thr->panel_differ_high_freq);		
		btl_free(thr->panel_differ_low_freq);
		btl_free(thr->short_cg);		
		btl_free(thr->short_cc);
		btl_free(thr->rel_rawdata_min);		
		btl_free(thr->rel_rawdata_max);
	}

	return 0;
}

static int btl_test_malloc_free_sc(struct btl_test *tdata, bool allocate)
{
	struct sc_threshold *thr = &tdata->ic.sc.thr;
	int buflen = tdata->node.node_num * sizeof(int);

	if (allocate == true) {
		btl_malloc_r(thr->rawdata_min, buflen);
		btl_malloc_r(thr->rawdata_max, buflen);
		btl_malloc_r(thr->cb_min, buflen);
		btl_malloc_r(thr->cb_max, buflen);
		btl_malloc_r(thr->dcb_sort, buflen);
		btl_malloc_r(thr->dcb_base, buflen);
		btl_malloc_r(thr->rawdata_mode1_min, buflen);
		btl_malloc_r(thr->rawdata_mode1_max, buflen);
		btl_malloc_r(thr->rawdata_mode2_min, buflen);
		btl_malloc_r(thr->rawdata_mode2_max, buflen);
		btl_malloc_r(thr->rawdata_mode3_min, buflen);
		btl_malloc_r(thr->rawdata_mode3_max, buflen);
		btl_malloc_r(thr->rawdata_mode4_min, buflen);
		btl_malloc_r(thr->rawdata_mode4_max, buflen);
		btl_malloc_r(thr->rawdata_mode5_min, buflen);
		btl_malloc_r(thr->rawdata_mode5_max, buflen);
		btl_malloc_r(thr->rawdata_mode6_min, buflen);
		btl_malloc_r(thr->rawdata_mode6_max, buflen);
		btl_malloc_r(thr->cb_mode1_base, buflen);
		btl_malloc_r(thr->cb_mode1_min, buflen);
		btl_malloc_r(thr->cb_mode1_max, buflen);
		btl_malloc_r(thr->cb_mode2_base, buflen);
		btl_malloc_r(thr->cb_mode2_min, buflen);
		btl_malloc_r(thr->cb_mode2_max, buflen);
		btl_malloc_r(thr->cb_mode3_base, buflen);
		btl_malloc_r(thr->cb_mode3_min, buflen);
		btl_malloc_r(thr->cb_mode3_max, buflen);
		btl_malloc_r(thr->cb_mode4_base, buflen);
		btl_malloc_r(thr->cb_mode4_min, buflen);
		btl_malloc_r(thr->cb_mode4_max, buflen);
		btl_malloc_r(thr->cb_mode5_base, buflen);
		btl_malloc_r(thr->cb_mode5_min, buflen);
		btl_malloc_r(thr->cb_mode5_max, buflen);
		btl_malloc_r(thr->cb_mode6_base, buflen);
		btl_malloc_r(thr->cb_mode6_min, buflen);
		btl_malloc_r(thr->cb_mode6_max, buflen);
		btl_malloc_r(thr->short_min, buflen);
		btl_malloc_r(thr->short_max, buflen);		
	} else {
		btl_free(thr->rawdata_min);
		btl_free(thr->rawdata_max);
		btl_free(thr->cb_min);
		btl_free(thr->cb_max);
		btl_free(thr->dcb_sort);
		btl_free(thr->dcb_base);
		btl_free(thr->rawdata_mode1_min);
		btl_free(thr->rawdata_mode1_max);
		btl_free(thr->rawdata_mode2_min);
		btl_free(thr->rawdata_mode2_max);
		btl_free(thr->rawdata_mode3_min);
		btl_free(thr->rawdata_mode3_max);
		btl_free(thr->rawdata_mode4_min);
		btl_free(thr->rawdata_mode4_min);
		btl_free(thr->rawdata_mode5_min);
		btl_free(thr->rawdata_mode5_max);
		btl_free(thr->rawdata_mode6_min);
		btl_free(thr->rawdata_mode6_max);
		btl_free(thr->cb_mode1_base);
		btl_free(thr->cb_mode1_min);
		btl_free(thr->cb_mode1_max);
		btl_free(thr->cb_mode2_base);
		btl_free(thr->cb_mode2_min);
		btl_free(thr->cb_mode2_max);
		btl_free(thr->cb_mode3_base);
		btl_free(thr->cb_mode3_min);
		btl_free(thr->cb_mode3_max);
		btl_free(thr->cb_mode4_base);
		btl_free(thr->cb_mode4_min);
		btl_free(thr->cb_mode4_max);
		btl_free(thr->cb_mode5_base);
		btl_free(thr->cb_mode5_min);
		btl_free(thr->cb_mode5_max);
		btl_free(thr->cb_mode6_base);
		btl_free(thr->cb_mode6_min);
		btl_free(thr->cb_mode6_max);
		btl_free(thr->short_min);
		btl_free(thr->short_max);		
	}

	return 0;
}

static int btl_test_malloc_free_thr(struct btl_test *tdata, bool allocate)
{
	int ret = 0;

	if ((tdata == NULL) || (tdata->func == NULL)) {
		BTL_TEST_SAVE_ERR("tdata/func is NULL\n");
		return -EINVAL;
	}

	if (allocate == true) {
		btl_malloc_r(tdata->node_valid, tdata->node.node_num * sizeof(int));
		btl_malloc_r(tdata->node_valid_sc, tdata->sc_node.node_num * sizeof(int));
	} else {
		btl_free(tdata->node_valid);
		btl_free(tdata->node_valid_sc);
	}

	switch (tdata->func->hwtype) {
	case IC_HW_INCELL:
		ret = btl_test_malloc_free_incell(tdata, allocate);
		break;
	case IC_HW_MC_SC:
		ret = btl_test_malloc_free_mc_sc(tdata, allocate);
		break;
	case IC_HW_SC:
		ret = btl_test_malloc_free_sc(tdata, allocate);
		break;
	default:
		BTL_TEST_SAVE_ERR("test ic type(%d) fail\n", tdata->func->hwtype);
		ret = -EINVAL;
		break;
	}

	return ret;
}

/* default enable all test item */
static void btl_test_init_item(struct btl_test *tdata)
{
	switch (tdata->func->hwtype) {
	case IC_HW_INCELL:
		tdata->ic.incell.u.tmp = 0xFFFFFFFF;
		break;
	case IC_HW_MC_SC:
		tdata->ic.mc_sc.u.tmp = 0xFFFFFFFF;
		break;
	case IC_HW_SC:
		tdata->ic.sc.u.tmp = 0xFFFFFFFF;
		break;
	}
}

static int get_tx_rx_num(u8 tx_rx_reg, u8 *ch_num, u8 ch_num_max)
{
	int ret = 0;
	int i = 0;

	for (i = 0; i < 3; i++) {
		ret = btl_test_read_reg(tx_rx_reg, ch_num);
		if ((ret < 0) || (*ch_num > ch_num_max)) {
			btl_sys_delay(50);
		} else
			break;
	}

	if (i >= 3) {
		BTL_TEST_ERROR("get channel num fail");
		return -EIO;
	}

	return 0;
}
static int get_key_num(int *key_num_en, int max_key_num)
{
	int ret = 0;
	u8 key_en = 0;

	if (!max_key_num) {
		BTL_TEST_DBG("not support key, don't read key num register");
		return 0;
	}

	ret = btl_test_read_reg(FACTORY_REG_LEFT_KEY, &key_en);
	if (ret >= 0) {
		if (key_en & 0x01) {
			(*key_num_en)++;
		}

		if (key_en & 0x02) {
			(*key_num_en)++;
		}

		if (key_en & 0x04) {
			(*key_num_en)++;
		}
	}

	ret = btl_test_read_reg(FACTORY_REG_RIGHT_KEY, &key_en);
	if (ret >= 0) {
		if (key_en & 0x01) {
			(*key_num_en)++;
		}

		if (key_en & 0x02) {
			(*key_num_en)++;
		}

		if (key_en & 0x04) {
			(*key_num_en)++;
		}
	}

	if (*key_num_en > max_key_num) {
		BTL_TEST_ERROR("get key num, fw:%d > max:%d", *key_num_en, max_key_num);
		return -EIO;
	}

	return ret;
}

static int get_channel_num(struct btl_test *tdata)
{
	int ret = 0;
	u8 tx_num = 0;
	u8 rx_num = 0;
	int key_num = 0;

	/* node structure */
	if (tdata->func->hwtype == IC_HW_SC ) {
		ret = get_tx_rx_num(FACTORY_REG_CH_NUM_SC, &tx_num, NUM_MAX_SC);
		if (ret < 0) {
			BTL_TEST_ERROR("get channel number fail");
			return ret;
		}

		ret = get_tx_rx_num(FACTORY_REG_KEY_NUM_SC, &rx_num, KEY_NUM_MAX);
		if (ret < 0) {
			BTL_TEST_ERROR("get key number fail");
			return ret;
		}

		tdata->node.tx_num = 1;
		tdata->node.rx_num = tx_num;
		tdata->node.channel_num = tx_num;
		tdata->node.node_num = tx_num;
		key_num = rx_num;
	} else {
		ret = get_tx_rx_num(FACTORY_REG_CHX_NUM, &tx_num, TX_NUM_MAX);
		if (ret < 0) {
			BTL_TEST_ERROR("get tx_num fail");
			return ret;
		}

		ret = get_tx_rx_num(FACTORY_REG_CHY_NUM, &rx_num, RX_NUM_MAX);
		if (ret < 0) {
			BTL_TEST_ERROR("get rx_num fail");
			return ret;
		}

		if (IC_HW_INCELL == tdata->func->hwtype) {
			ret = get_key_num(&key_num, tdata->func->key_num_total);
			if (ret < 0) {
				BTL_TEST_ERROR("get key_num fail");
				return ret;
			}
		} else if (tdata->func->hwtype == IC_HW_MC_SC ) {
			key_num = tdata->func->key_num_total;
		}
		tdata->node.tx_num = tx_num;
		tdata->node.rx_num = rx_num;
		if (IC_HW_INCELL == tdata->func->hwtype)
			tdata->node.channel_num = tx_num * rx_num;
		else if (tdata->func->hwtype == IC_HW_MC_SC )
			tdata->node.channel_num = tx_num + rx_num;
		tdata->node.node_num = tx_num * rx_num;
	}

	/* key */
	tdata->node.key_num = key_num;
	tdata->node.node_num += tdata->node.key_num;

	/* sc node structure */
	tdata->sc_node = tdata->node;
	if (tdata->func->hwtype == IC_HW_MC_SC ) {
		tdata->sc_node.tx_num = tx_num;
		tdata->sc_node.rx_num = rx_num;
		tdata->sc_node.channel_num = tx_num + rx_num;
		tdata->sc_node.node_num = tx_num + rx_num;
	}
	
	
	if (tdata->node.tx_num > TX_NUM_MAX) {
		BTL_TEST_ERROR("tx num(%d) fail", tdata->node.tx_num);
		return -EIO;
	}

	if (tdata->node.rx_num > RX_NUM_MAX) {
		BTL_TEST_ERROR("rx num(%d) fail", tdata->node.rx_num);
		return -EIO;
	}

	BTL_TEST_INFO("node_num:%d, tx:%d, rx:%d, key:%d",
				  tdata->node.node_num, tdata->node.tx_num,
				  tdata->node.rx_num, tdata->node.key_num);
	return 0;
}

static int btl_test_init_basicinfo(struct btl_test *tdata)
{
	int ret = 0;
	u8 val = 0;

	if ((tdata == NULL) || (tdata->func == NULL)) {
		BTL_TEST_SAVE_ERR("tdata/func is NULL\n");
		return -EINVAL;
	}

	btl_test_read_reg(REG_FW_VERSION, &val);
	tdata->fw_ver = val;

	if (tdata->func->hwtype == IC_HW_SC ) {
		btl_test_read_reg(FACTORY_REG_PATTERN_TYPE, &val);
		tdata->pattern_type = val;
	}

	if (IC_HW_INCELL == tdata->func->hwtype) {
		btl_test_read_reg(REG_VA_TOUCH_THR, &val);
		tdata->va_touch_thr = val;
		btl_test_read_reg(REG_VKEY_TOUCH_THR, &val);
		tdata->vk_touch_thr = val;
	}

	/* enter factory mode */
	ret = btl_enter_factory_mode();
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("enter factory mode fail\n");
		return ret;
	}

	/* enter into factory mode and read tx/rx num */
	ret = get_channel_num(tdata);
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("get channel number fail\n");
		return ret;
	}

	return ret;
}

static int btl_test_main_init(void)
{
	int ret = 0;
	struct btl_test *tdata = btl_ftest;

	BTL_TEST_FUNC_ENTER();
	/* Init btl_test_data to 0 before test,	 */
	memset(&tdata->testdata, 0, sizeof(struct btl_test_data));

	/* get basic information: tx/rx num ... */
	ret = btl_test_init_basicinfo(tdata);
	if (ret < 0) {
		BTL_TEST_ERROR("test init basicinfo fail");
		return ret;
	}

	/* allocate memory for test threshold */
	ret = btl_test_malloc_free_thr(tdata, true);
	if (ret < 0) {
		BTL_TEST_ERROR("test malloc for threshold fail");
		return ret;
	}

	/* default enable all test item */
	btl_test_init_item(tdata);

	ret = btl_test_malloc_free_data_txt(tdata, true);
	if (ret < 0) {
		BTL_TEST_ERROR("allocate memory for test data(txt) fail");
		return ret;
	}

	/* allocate test data buffer */
	tdata->buffer_length = (tdata->node.tx_num + 1) * tdata->node.rx_num;
	tdata->buffer_length *= sizeof(int) * 2;
	BTL_TEST_INFO("test buffer length:%d", tdata->buffer_length);
	tdata->buffer = (int *)btl_malloc(tdata->buffer_length);
	if (NULL == tdata->buffer) {
		BTL_TEST_ERROR("test buffer(%d) malloc fail", tdata->buffer_length);
		return -ENOMEM;
	}
	memset(tdata->buffer, 0, tdata->buffer_length);

	BTL_TEST_FUNC_EXIT();
	return ret;
}

static int btl_test_main_exit(void)
{
	struct btl_test *tdata = btl_ftest;

	BTL_TEST_FUNC_ENTER();
	btl_test_save_data_csv(tdata);
	btl_test_save_result_txt(tdata);

	/* free memory */
	btl_test_malloc_free_data_txt(tdata, false);
	btl_test_malloc_free_thr(tdata, false);

	/* free test data */
	btl_test_free_data(tdata);

	/*free test data buffer*/
	btl_free(tdata->buffer);

	BTL_TEST_FUNC_EXIT();
	return 0;
}


/*
 * btl_test_get_testparams - get test parameter from ini
 */
static int btl_test_get_testparams(char *config_name)
{
	int ret = 0;

	ret = btl_test_get_testparam_from_ini(config_name);

	return ret;
}

static int btl_test_start(void)
{
	int testresult = 0;
	struct btl_test *tdata = btl_ftest;

	if (tdata && tdata->func && tdata->func->start_test) {
		tdata->testdata.item_count = 0;
		testresult = tdata->func->start_test();
	} else {
		BTL_TEST_ERROR("test func/start_test func is null");
	}

	return testresult;
}

/*
 * btl_test_entry - test main entry
 *
 * warning - need disable irq & esdcheck before call this function
 *
 */
int btl_test_entry(char *ini_file_name)
{
	int ret = 0;
	int retry = 0;

	/* test initialize */
	ret = btl_test_main_init();
	if (ret < 0) {
		BTL_TEST_ERROR("btl_test_main_init fail");
		goto test_err;
	}

	/*Read parse configuration file*/
	BTL_TEST_SAVE_INFO("ini_file_name:%s\n", ini_file_name);
	ret = btl_test_get_testparams(ini_file_name);
	if (ret < 0) {
		BTL_TEST_ERROR("get testparam fail");
		goto test_err;
	}

	/* Start testing according to the test configuration */
	do {
		if (btl_test_start() == true) {
			BTL_TEST_SAVE_INFO("\n\n=======Tp test pass.\n");
			BTL_TEST_INFO("\n\n=======Tp test pass.\n");
			btl_ftest->result = true;
			rawdata_test_flag = 1;
			rawdata_tested_flag = 1;
			break;
		} else {
			retry++;
			msleep(20);
			BTL_TEST_SAVE_INFO("\n\n=======Tp test failure.\n");
			BTL_TEST_ERROR("\n\n=======Tp test failure.\n");
			btl_ftest->result = false;
			rawdata_test_flag = 0;
			rawdata_tested_flag = 0;
			blt_tptest_result = 0;
		}
	} while (retry < 3);

	if (retry == 3) {
		BTL_TEST_ERROR("selftest failed!");
		ret = -EIO;
	} else {
		BTL_TEST_INFO("selftest success!");
		ret = 0;
	}
test_err:
	btl_test_main_exit();
	btl_enter_work_mode();
	return ret;
}

static int btl_test_func_init(void)
{
	int i = 0;
	int j = 0;
	
	struct test_funcs *func = btl_test_func_list[0];
	int func_count = sizeof(btl_test_func_list) / sizeof(btl_test_func_list[0]);

	BTL_TEST_INFO("init test function");
	if (func_count == 0) {
		BTL_TEST_SAVE_ERR("test functions list is NULL, fail\n");
		return -ENODATA;
	}

	btl_ftest = kzalloc(sizeof(*btl_ftest), GFP_KERNEL);
	if (btl_ftest == NULL) {
		BTL_TEST_ERROR("malloc memory for test fail");
		return -ENOMEM;
	}

	for (i = 0; i < func_count; i++) {
		func = btl_test_func_list[i];
		for (j = 0; j < BTL_MAX_COMPATIBLE_TYPE; j++) {
			if (0 == func->ctype[j])
				break;
			else if (func->ctype[j] == g_btl_ts->chipInfo.chipID) {
				BTL_TEST_INFO("match test function,type:%x", (int)func->ctype[j]);
				btl_ftest->func = func;
			}
		}
	}
	if (btl_ftest->func == NULL) {
		BTL_TEST_ERROR("no test function match, can't test");
		return -ENODATA;
	}

	return 0;
}

int btl_test_init(void)
{
	int ret = 0;

	BTL_TEST_FUNC_ENTER();
	/* get test function, must be the first step */
	ret = btl_test_func_init();
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("test functions init fail");
		return ret;
	}
	BTL_TEST_FUNC_EXIT();

	return ret;
}

int btl_test_exit(void)
{
	BTL_TEST_FUNC_ENTER();
	btl_free(btl_ftest);
	BTL_TEST_FUNC_EXIT();
	return 0;
}
#endif
