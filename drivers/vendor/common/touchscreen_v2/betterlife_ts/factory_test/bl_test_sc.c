/************************************************************************
* Copyright (c) 2012-2020, Betterlife Systems (R), All Rights Reserved.
*
* File Name: bl_test_sc.c
*
* Author: Betterlife Driver Team
*
* Created: 2015-07-14
*
* Abstract:
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
* Private enumerations, structures and unions using typedef
*****************************************************************************/

/*****************************************************************************
* Static variables
*****************************************************************************/

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/
extern int blt_tptest_result;
#define TEST_BEYOND_MAX_LIMIT		0x0001
#define TEST_BEYOND_MIN_LIMIT		0x0002
#define TEST_GT_SHORT				0x0400
/*****************************************************************************
* Static function prototypes
*****************************************************************************/
#ifdef BTL_FACTORY_TEST_EN
static int sc_rawdata_test(struct btl_test *tdata, int *min, int *max)
{
	int ret = 0;
	bool result = false;
	int *rawdata = NULL;
	int *rawdata_tmp = NULL;
	int rawdata_cnt = 0;
	int byte_num = 0;
	u8 cmd = 0x7b;
	int i = 0;
	u8 *data = NULL;
	u8 *data1 = NULL;

	BTL_TEST_FUNC_ENTER();
	memset(tdata->buffer, 0, tdata->buffer_length);
	rawdata = tdata->buffer;
	byte_num = tdata->node.node_num * 2;

	data = (u8 *)btl_malloc(byte_num * sizeof(u8));
	if (data == NULL) {
		BTL_TEST_SAVE_ERR("malloc memory for rawdata buffer fail\n");
		return -ENOMEM;
	}

	data1 = (u8 *)btl_malloc(byte_num * sizeof(u8));
	if (data1 == NULL) {
		BTL_TEST_SAVE_ERR("malloc memory for rawdata1 buffer fail\n");
		goto err_malloc;
	}

	for(i = 0; i < 6; i++)
	{
		ret = btl_start_scan();
		if(ret < 0)
		{
			BTL_TEST_ERROR("start scan fail\n");
			goto test_err;
		}
	}

	rawdata_tmp = rawdata + rawdata_cnt;
	ret = btl_test_bus_read(&cmd, 1, data, byte_num);
	if(ret < 0)
	{
		BTL_TEST_ERROR("read rawdata error\n");
		goto test_err;	  
	}

	ret = btl_start_scan();
	if(ret < 0)
	{
		BTL_TEST_ERROR("start scan second fail\n");
		goto test_err;
	}

	ret = btl_test_bus_read(&cmd, 1, data1, byte_num);
	if(ret < 0)
	{
		BTL_TEST_ERROR("read rawdata1 error\n");
		goto test_err;	  
	}

	for (i = 0; i < byte_num; i = i + 2) {
		rawdata_tmp[i >> 1] = (int)(((data[i+1] << 8) + data[i] + (data1[i+1] << 8) + data1[i]) / 2);
	}

	btl_show_data_sc(rawdata_tmp);
	
	result = true;
	for (i = 0; i < tdata->sc_node.node_num; i++) {
		if ((rawdata_tmp[i] < min[i]) || (rawdata_tmp[i] > max[i])) {
			BTL_TEST_SAVE_ERR("test fail,Rx%d=%5d,range=(%5d,%5d)\n",
							  i + 1, rawdata_tmp[i], min[i], max[i]);
			result = false;
		}
	}

	
	rawdata_cnt += tdata->node.node_num;


	if (result) {
		ret = 0;
	} else {
		ret = -1;
	}

	//btl_test_save_data("Rawdata Test", CODE_M_RAWDATA_TEST,rawdata, rawdata_cnt, false, false, result);

	BTL_TEST_FUNC_EXIT();

test_err:
	btl_free(data1);
err_malloc:
	btl_free(data);

	return ret;
}

static int sc_cb_test(struct btl_test *tdata, int *base, int *min, int *max, u8 mode)
{
	int ret = 0;
	bool tmp_result = true;
	int byte_num = 0;
	u8 cmd = 0x11;
	int *scap_cb = NULL;
	int *scb_tmp = NULL;
	int scb_cnt = 0;
	u8 *cb = NULL;
	int i = 0;
	u8 rx_num = 0;
	u8 key_num = 0;
	u8 pattern_type = 0;
	
	BTL_TEST_FUNC_ENTER();
	memset(tdata->buffer, 0, tdata->buffer_length);
	scap_cb = tdata->buffer;
	byte_num = tdata->sc_node.node_num;
	rx_num = tdata->node.rx_num;
	key_num = tdata->node.key_num;
	pattern_type = tdata->pattern_type;
	
	cb = (u8 *)btl_malloc(byte_num * sizeof(u8));
	if (NULL == cb) {
		BTL_TEST_SAVE_ERR("malloc memory for cb buffer fail\n");
		return -ENOMEM;
	}
	
	scb_tmp = scap_cb + scb_cnt;

	ret = btl_test_bus_read(&cmd,1, cb, byte_num);
	if(ret < 0)
	{
		BTL_TEST_ERROR("get cb value fail\n");
		goto test_err;
	}

	for (i = 0; i < byte_num; i++) 
	{		
		scb_tmp[i] = cb[i];	
	}
	BTL_TEST_SAVE_INFO("CbMode%dOrigin:", mode);
	btl_show_data_sc(scb_tmp);

	btl_sc_cb_to_slope(cb, rx_num, key_num, pattern_type, scb_tmp);
	
	BTL_TEST_SAVE_INFO("CbMode%dRatio:",mode);
	btl_show_data_sc(scb_tmp);
	for (i = 0; i < rx_num; i++) {
		scb_tmp[i] = scb_tmp[i] - base[i];
	}
	BTL_TEST_SAVE_INFO("CbMode%d:",mode);
	btl_show_data_sc(scb_tmp);

	tmp_result = true;
	for(i = 0; i < tdata->sc_node.node_num; i++)
	{
		 if ((scb_tmp[i] < min[i]) || (scb_tmp[i] > max[i])) {
			BTL_TEST_SAVE_ERR("test fail,%d=%5d,range=(%5d,%5d)\n",
							  i + 1, scb_tmp[i], min[i], max[i]);
			tmp_result = false;
		}	   
	}
	scb_cnt += tdata->sc_node.node_num;

	if (tmp_result) {
		ret = 0;
	} else {
		ret = -1;
	}
	
	/* save data */
	//btl_test_save_data("SCAP CB Test", CODE_M_SCAP_CB_TEST,scap_cb, scb_cnt, false, false, *test_result);
	
	BTL_TEST_FUNC_EXIT();

test_err:
	btl_free(cb);
	return ret;
}

static int sc_rawdata_cb_mode1_test(struct btl_test *tdata, bool *test_result)
{
	int ret = 0;
	bool rawdata_result = false;
	bool cb_result = false;
	u8 proValue = 0;
	u8 scanMode = 0;
	u8 freq = 0;
	u8 vdd = 0;
	u8 cf = 0;
	u8 tValueHigh = 0;
	u8 tValueLow = 0;
	
	struct sc_threshold *thr = &tdata->ic.sc.thr;

	BTL_TEST_FUNC_ENTER();
	BTL_TEST_SAVE_INFO("\n============ Test Item: RAWDATA_CB_TEST_MODE1 test\n");

	if (!thr->rawdata_mode1_min|| !thr->rawdata_mode1_max || !thr->cb_mode1_min || !thr->cb_mode1_max) {
		BTL_TEST_SAVE_ERR("rawdata_mode1_min/max and cb_mode1_min/max is null\n");
		ret = -EINVAL;
		goto test_err;
	}
	
	ret = btl_enter_factory_mode();
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("failed to enter factory mode,ret=%d\n", ret);
		goto test_err;
	}

	proValue = 0x01;
	scanMode = 0x11;
	freq = (thr->basic.test_mode1_freq & 0xff);
	vdd = (thr->basic.test_mode1_vdd & 0xff);
	cf = (thr->basic.test_mode1_cf & 0xff);
	tValueHigh = (thr->basic.test_mode1_tvalue >> 8);
	tValueLow = (thr->basic.test_mode1_tvalue & 0xff);
	BTL_TEST_DBG("proValue=%d,scanMode=%d,freq=%d,vdd=%d,cf=%d,tValueHigh=%d,tValueLow=%d",proValue, scanMode, freq, vdd, cf, tValueHigh, tValueLow);
	ret = btl_select_test_mode(proValue, scanMode, freq, vdd, cf, tValueHigh, tValueLow);
	if(ret < 0)
	{
		goto test_err;
	}
	
	btl_sys_delay(70);
	
	/* BTL_TEST_SAVE_INFO("Cb_Mode1_Min:");
	btl_show_sc_threshold_value(thr->cb_mode1_min, "min");
	BTL_TEST_SAVE_INFO("Cb_Mode1_Max:");
	btl_show_sc_threshold_value(thr->cb_mode1_max, "max"); */

	ret = btl_sc_cb_calibrate(0x01);
	if(ret < 0)
	{
		goto test_err;
	}	

	ret = sc_cb_test(tdata, thr->cb_mode1_base, thr->cb_mode1_min, thr->cb_mode1_max, 1);
	if(ret < 0)
	{
		cb_result = false;
	}
	else
	{
		cb_result = true;	
	}
	BTL_TEST_SAVE_INFO("\n");

	/* BTL_TEST_SAVE_INFO("RawDataTest_Mode1_Min:");
	btl_show_sc_threshold_value(thr->rawdata_mode1_min, "min");
	BTL_TEST_SAVE_INFO("RawDataTest_Mode1_Max:");
	btl_show_sc_threshold_value(thr->rawdata_mode1_max, "max"); */

	BTL_TEST_SAVE_INFO("RawDataMode1:");
	ret = sc_rawdata_test(tdata, thr->rawdata_mode1_min, thr->rawdata_mode1_max);
	if(ret < 0)
	{
		rawdata_result = false;
	}
	else
	{
		rawdata_result = true;	 
	}

	BTL_TEST_SAVE_INFO("\n");

test_err:
	if (cb_result && rawdata_result) {
		*test_result = true;
		BTL_TEST_SAVE_INFO("------ RAWDATA_CB_TEST_MODE1 PASS\n");
	} else {
		*test_result = false;
		BTL_TEST_SAVE_INFO("------ RAWDATA_CB_TEST_MODE1 NG\n");
	}

	BTL_TEST_FUNC_EXIT();
	return ret;
}

static int sc_rawdata_cb_mode2_test(struct btl_test *tdata, bool *test_result)
{
	int ret = 0;
	bool rawdata_result = false;
	bool cb_result = false;
	u8 proValue = 0;
	u8 scanMode = 0;
	u8 freq = 0;
	u8 vdd = 0;
	u8 cf = 0;
	u8 tValueHigh = 0;
	u8 tValueLow = 0;

	struct sc_threshold *thr = &tdata->ic.sc.thr;

	BTL_TEST_FUNC_ENTER();
	BTL_TEST_SAVE_INFO("\n============ Test Item: RAWDATA_CB_TEST_MODE2 test\n");

	if (!thr->rawdata_mode2_min|| !thr->rawdata_mode2_max || !thr->cb_mode2_min || !thr->cb_mode2_max) {
		BTL_TEST_SAVE_ERR("rawdata_mode2_min/max and cb_mode2_min/max is null\n");
		ret = -EINVAL;
		goto test_err;
	}
		
	ret = btl_enter_factory_mode();
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("failed to enter factory mode,ret=%d\n", ret);
		goto test_err;
	}

	proValue = 0x01;
	scanMode = 0x11;
	freq = (thr->basic.test_mode2_freq & 0xff);
	vdd = (thr->basic.test_mode2_vdd & 0xff);
	cf = (thr->basic.test_mode2_cf & 0xff);
	tValueHigh = (thr->basic.test_mode2_tvalue >> 8);
	tValueLow = (thr->basic.test_mode2_tvalue & 0xff);
	BTL_TEST_DBG("proValue=%d,scanMode=%d,freq=%d,vdd=%d,cf=%d,tValueHigh=%d,tValueLow=%d",proValue, scanMode, freq, vdd, cf, tValueHigh, tValueLow);
	ret = btl_select_test_mode(proValue, scanMode, freq, vdd, cf, tValueHigh, tValueLow);
	if(ret < 0)
	{
		goto test_err;
	}
	
	btl_sys_delay(70);
	
	/* BTL_TEST_SAVE_INFO("Cb_Mode2_Min:");
	btl_show_sc_threshold_value(thr->cb_mode2_min, "min");
	BTL_TEST_SAVE_INFO("Cb_Mode2_Max:");
	btl_show_sc_threshold_value(thr->cb_mode2_max, "max"); */

	ret = btl_sc_cb_calibrate(0x02);
	if(ret < 0)
	{
		goto test_err;
	}	

	ret = sc_cb_test(tdata, thr->cb_mode2_base, thr->cb_mode2_min, thr->cb_mode2_max, 2);
	if(ret < 0)
	{
		cb_result = false;
	}
	else
	{
		cb_result = true;
	}


	BTL_TEST_SAVE_INFO("\n");

	/* BTL_TEST_SAVE_INFO("RawDataTest_Mode2_Min:");
	btl_show_sc_threshold_value(thr->rawdata_mode2_min, "min");
	BTL_TEST_SAVE_INFO("RawDataTest_Mode2_Max:");
	btl_show_sc_threshold_value(thr->rawdata_mode2_max, "max"); */

	BTL_TEST_SAVE_INFO("RawDataMode2:");
	ret = sc_rawdata_test(tdata, thr->rawdata_mode2_min, thr->rawdata_mode2_max);
	if(ret < 0)
	{
		rawdata_result = false;
	}
	else
	{
		rawdata_result = true;
	}

	BTL_TEST_SAVE_INFO("\n");

test_err:
	if (cb_result && rawdata_result) {
		*test_result = true;
		BTL_TEST_SAVE_INFO("------ RAWDATA_CB_TEST_MODE2 PASS\n");
	} else {
		*test_result = false;
		BTL_TEST_SAVE_INFO("------ RAWDATA_CB_TEST_MODE2 NG\n");
	}

	BTL_TEST_FUNC_EXIT();
	return ret;
}

static int sc_rawdata_cb_mode3_test(struct btl_test *tdata, bool *test_result)
{
	int ret = 0;
	bool rawdata_result = false;
	bool cb_result = false;
	u8 proValue = 0;
	u8 scanMode = 0;
	u8 freq = 0;
	u8 vdd = 0;
	u8 cf = 0;
	u8 tValueHigh = 0;
	u8 tValueLow = 0;

	struct sc_threshold *thr = &tdata->ic.sc.thr;

	BTL_TEST_FUNC_ENTER();
	BTL_TEST_SAVE_INFO("\n============ Test Item: RAWDATA_CB_TEST_MODE3 test\n");

	if (!thr->rawdata_mode3_min|| !thr->rawdata_mode3_max || !thr->cb_mode3_min || !thr->cb_mode3_max) {
		BTL_TEST_SAVE_ERR("rawdata_mode3_min/max and cb_mode3_min/max is null\n");
		ret = -EINVAL;
		goto test_err;
	}
	
	ret = btl_enter_factory_mode();
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("failed to enter factory mode,ret=%d\n", ret);
		goto test_err;
	}

	proValue = 0x01;
	scanMode = 0x10;
	freq = (thr->basic.test_mode3_freq & 0xff);
	vdd = (thr->basic.test_mode3_vdd & 0xff);
	cf = (thr->basic.test_mode3_cf & 0xff);
	tValueHigh = (thr->basic.test_mode3_tvalue >> 8);
	tValueLow = (thr->basic.test_mode3_tvalue & 0xff);
	BTL_TEST_DBG("proValue=%d,scanMode=%d,freq=%d,vdd=%d,cf=%d,tValueHigh=%d,tValueLow=%d",proValue, scanMode, freq, vdd, cf, tValueHigh, tValueLow);
	ret = btl_select_test_mode(proValue, scanMode, freq, vdd, cf, tValueHigh, tValueLow);
	if(ret < 0)
	{
		goto test_err;
	}

	btl_sys_delay(70);

	/* BTL_TEST_SAVE_INFO("Cb_Mode3_Min:");
	btl_show_sc_threshold_value(thr->cb_mode3_min, "min");
	BTL_TEST_SAVE_INFO("Cb_Mode3_Max:");
	btl_show_sc_threshold_value(thr->cb_mode3_max, "max"); */

	ret = btl_sc_cb_calibrate(0x02);
	if(ret < 0)
	{
		goto test_err;
	}	

	ret = btl_sc_cb_calibrate(0x02);
	if(ret < 0)
	{
		goto test_err;
	}	

	ret = sc_cb_test(tdata, thr->cb_mode3_base, thr->cb_mode3_min, thr->cb_mode3_max, 3);
	if(ret < 0)
	{
		cb_result = false;
	}
	else
	{
		cb_result = true;
	}

	BTL_TEST_SAVE_INFO("\n");

	/* BTL_TEST_SAVE_INFO("RawDataTest_Mode3_Min:");
	btl_show_sc_threshold_value(thr->rawdata_mode3_min, "min");
	BTL_TEST_SAVE_INFO("RawDataTest_Mode3_Max:");
	btl_show_sc_threshold_value(thr->rawdata_mode3_max, "max"); */

	BTL_TEST_SAVE_INFO("RawDataMode3:");
	ret = sc_rawdata_test(tdata, thr->rawdata_mode3_min, thr->rawdata_mode3_max);
	if(ret < 0)
	{
		rawdata_result = false;
	}
	else
	{
		rawdata_result = true;
	}

	BTL_TEST_SAVE_INFO("\n");

test_err:
	if (cb_result && rawdata_result) {
		*test_result = true;
		BTL_TEST_SAVE_INFO("------ RAWDATA_CB_TEST_MODE3 PASS\n");
	} else {
		*test_result = false;
		BTL_TEST_SAVE_INFO("------ RAWDATA_CB_TEST_MODE3 NG\n");
	}

	BTL_TEST_FUNC_EXIT();
	return ret;
}

static int sc_rawdata_cb_mode4_test(struct btl_test *tdata, bool *test_result)
{
	int ret = 0;
	bool rawdata_result = false;
	bool cb_result = false;
	u8 proValue = 0;
	u8 scanMode = 0;
	u8 freq = 0;
	u8 vdd = 0;
	u8 cf = 0;
	u8 tValueHigh = 0;
	u8 tValueLow = 0;
	struct sc_threshold *thr = &tdata->ic.sc.thr;

	BTL_TEST_FUNC_ENTER();
	BTL_TEST_SAVE_INFO("\n============ Test Item: RAWDATA_CB_TEST_MODE4 test\n");

	if (!thr->rawdata_mode4_min|| !thr->rawdata_mode4_max || !thr->cb_mode4_min || !thr->cb_mode4_max) {
		BTL_TEST_SAVE_ERR("rawdata_mode4_min/max and cb_mode4_min/max is null\n");
		ret = -EINVAL;
		goto test_err;
	}
	
	ret = btl_enter_factory_mode();
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("failed to enter factory mode,ret=%d\n", ret);
		goto test_err;
	}

	proValue = 0x01;
	scanMode = 0x10;
	freq = (thr->basic.test_mode4_freq & 0xff);
	vdd = (thr->basic.test_mode4_vdd & 0xff);
	cf = (thr->basic.test_mode4_cf & 0xff);
	tValueHigh = (thr->basic.test_mode4_tvalue >> 8);
	tValueLow = (thr->basic.test_mode4_tvalue & 0xff);
	BTL_TEST_DBG("proValue=%d,scanMode=%d,freq=%d,vdd=%d,cf=%d,tValueHigh=%d,tValueLow=%d",proValue, scanMode, freq, vdd, cf, tValueHigh, tValueLow);
	ret = btl_select_test_mode(proValue, scanMode, freq, vdd, cf, tValueHigh, tValueLow);
	if(ret < 0)
	{
		goto test_err;
	}
	
	btl_sys_delay(70);

	/* BTL_TEST_SAVE_INFO("Cb_Mode4_Min:");
	btl_show_sc_threshold_value(thr->cb_mode4_min, "min");
	BTL_TEST_SAVE_INFO("Cb_Mode4_Max:");
	btl_show_sc_threshold_value(thr->cb_mode4_max, "max"); */

	ret = btl_sc_cb_calibrate(0x02);
	if(ret < 0)
	{
		goto test_err;
	}	

	ret = sc_cb_test(tdata, thr->cb_mode4_base, thr->cb_mode4_min, thr->cb_mode4_max, 4);
	if(ret < 0)
	{
		cb_result = false;
	}
	else
	{
		cb_result = true;
	}

	BTL_TEST_SAVE_INFO("\n");
	/*BTL_TEST_SAVE_INFO("RawDataTest_Mode4_Min:");
	btl_show_sc_threshold_value(thr->rawdata_mode4_min, "min");
	BTL_TEST_SAVE_INFO("RawDataTest_Mode4_Max:");
	btl_show_sc_threshold_value(thr->rawdata_mode4_max, "max"); */

	BTL_TEST_SAVE_INFO("RawDataMode4:");
	ret = sc_rawdata_test(tdata, thr->rawdata_mode4_min, thr->rawdata_mode4_max);
	if(ret < 0)
	{
		rawdata_result = false;
	}
	else
	{
		rawdata_result = true;
	}

	BTL_TEST_SAVE_INFO("\n");

test_err:
	if (cb_result && rawdata_result) {
		*test_result = true;
		BTL_TEST_SAVE_INFO("------ RAWDATA_CB_TEST_MODE4 PASS\n");
	} else {
		*test_result = false;
		BTL_TEST_SAVE_INFO("------ RAWDATA_CB_TEST_MODE4 NG\n");
	}

	BTL_TEST_FUNC_EXIT();
	return ret;
}

static int sc_rawdata_cb_mode5_test(struct btl_test *tdata, bool *test_result)
{
	int ret = 0;
	bool rawdata_result = false;
	bool cb_result = false;
	u8 proValue = 0;
	u8 scanMode = 0;
	u8 freq = 0;
	u8 vdd = 0;
	u8 cf = 0;
	u8 tValueHigh = 0;
	u8 tValueLow = 0;
	struct sc_threshold *thr = &tdata->ic.sc.thr;

	BTL_TEST_FUNC_ENTER();
	BTL_TEST_SAVE_INFO("\n============ Test Item: RAWDATA_CB_TEST_MODE5 test\n");

	if (!thr->rawdata_mode5_min|| !thr->rawdata_mode5_max || !thr->cb_mode5_min || !thr->cb_mode5_max) {
		BTL_TEST_SAVE_ERR("rawdata_mode5_min/max and cb_mode5_min/max is null\n");
		ret = -EINVAL;
		goto test_err;
	}
	
	ret = btl_enter_factory_mode();
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("failed to enter factory mode,ret=%d\n", ret);
		goto test_err;
	}

	proValue = 0x01;
	scanMode = 0x02;
	freq = (thr->basic.test_mode5_freq & 0xff);
	vdd = (thr->basic.test_mode5_vdd & 0xff);
	cf = (thr->basic.test_mode5_cf & 0xff);
	tValueHigh = (thr->basic.test_mode5_tvalue >> 8);
	tValueLow = (thr->basic.test_mode5_tvalue & 0xff);
	BTL_TEST_DBG("proValue=%d,scanMode=%d,freq=%d,vdd=%d,cf=%d,tValueHigh=%d,tValueLow=%d",proValue, scanMode, freq, vdd, cf, tValueHigh, tValueLow);
	ret = btl_select_test_mode(proValue, scanMode, freq, vdd, cf, tValueHigh, tValueLow);
	if(ret < 0)
	{
		goto test_err;
	}

	btl_sys_delay(70);

	/* BTL_TEST_SAVE_INFO("Cb_Mode5_Min:");
	btl_show_sc_threshold_value(thr->cb_mode5_min, "min");
	BTL_TEST_SAVE_INFO("Cb_Mode5_Max:");
	btl_show_sc_threshold_value(thr->cb_mode5_max, "max"); */

	ret = btl_sc_cb_calibrate(0x02);
	if(ret < 0)
	{
		goto test_err;
	}	

	ret = sc_cb_test(tdata, thr->cb_mode5_base, thr->cb_mode5_min, thr->cb_mode5_max, 5);
	if(ret < 0)
	{
		cb_result = false;
	}
	else
	{
		cb_result = true;
	}

	BTL_TEST_SAVE_INFO("\n");

	/* BTL_TEST_SAVE_INFO("RawDataTest_Mode5_Min:");
	btl_show_sc_threshold_value(thr->rawdata_mode5_min, "min");
	BTL_TEST_SAVE_INFO("RawDataTest_Mode5_Max:");
	btl_show_sc_threshold_value(thr->rawdata_mode5_max, "max"); */

	BTL_TEST_SAVE_INFO("RawDataMode5:");
	ret = sc_rawdata_test(tdata, thr->rawdata_mode5_min, thr->rawdata_mode5_max);
	if(ret < 0)
	{
		rawdata_result = false;
	}
	else
	{
		rawdata_result = true;
	}

	BTL_TEST_SAVE_INFO("\n");

test_err:
	if (cb_result && rawdata_result) {
		*test_result = true;
		BTL_TEST_SAVE_INFO("------ RAWDATA_CB_TEST_MODE5 PASS\n");
	} else {
		*test_result = false;
		BTL_TEST_SAVE_INFO("------ RAWDATA_CB_TEST_MODE5 NG\n");
	}

	BTL_TEST_FUNC_EXIT();
	return ret;
}

static int sc_rawdata_cb_mode6_test(struct btl_test *tdata, bool *test_result)
{
	int ret = 0;
	bool rawdata_result = false;
	bool cb_result = false;
	u8 proValue = 0;
	u8 scanMode = 0;
	u8 freq = 0;
	u8 vdd = 0;
	u8 cf = 0;
	u8 tValueHigh = 0;
	u8 tValueLow = 0;
	struct sc_threshold *thr = &tdata->ic.sc.thr;

	BTL_TEST_FUNC_ENTER();
	BTL_TEST_SAVE_INFO("\n============ Test Item: RAWDATA_CB_TEST_MODE6 test\n");

	if (!thr->rawdata_mode6_min|| !thr->rawdata_mode6_max || !thr->cb_mode6_min || !thr->cb_mode6_max) {
		BTL_TEST_SAVE_ERR("rawdata_mode6_min/max and cb_mode6_min/max is null\n");
		ret = -EINVAL;
		goto test_err;
	}
		
	ret = btl_enter_factory_mode();
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("failed to enter factory mode,ret=%d\n", ret);
		goto test_err;
	}

	proValue = 0x00;
	scanMode = 0x02;
	freq = (thr->basic.test_mode6_freq & 0xff);
	vdd = (thr->basic.test_mode6_vdd & 0xff);
	cf = (thr->basic.test_mode6_cf & 0xff);
	tValueHigh = (thr->basic.test_mode6_tvalue >> 8);
	tValueLow = (thr->basic.test_mode6_tvalue & 0xff);
	BTL_TEST_DBG("proValue=%d,scanMode=%d,freq=%d,vdd=%d,cf=%d,tValueHigh=%d,tValueLow=%d",proValue, scanMode, freq, vdd, cf, tValueHigh, tValueLow);
	ret = btl_select_test_mode(proValue, scanMode, freq, vdd, cf, tValueHigh, tValueLow);
	if(ret < 0)
	{
		goto test_err;
	}

	btl_sys_delay(70);

	/* BTL_TEST_SAVE_INFO("Cb_Mode6_Min:");
	btl_show_sc_threshold_value(thr->cb_mode6_min, "min");
	BTL_TEST_SAVE_INFO("Cb_Mode6_Max:");
	btl_show_sc_threshold_value(thr->cb_mode6_max, "max"); */

	ret = btl_sc_cb_calibrate(0x02);
	if(ret < 0)
	{
		goto test_err;
	}	

	ret = sc_cb_test(tdata, thr->cb_mode6_base, thr->cb_mode6_min, thr->cb_mode6_max, 6);
	if(ret < 0)
	{
		cb_result = false;
	}
	else
	{
		cb_result = true;
	}

	BTL_TEST_SAVE_INFO("\n");

	/* BTL_TEST_SAVE_INFO("RawDataTest_Mode6_Min:");
	btl_show_sc_threshold_value(thr->rawdata_mode6_min, "min");
	BTL_TEST_SAVE_INFO("RawDataTest_Mode6_Max:");
	btl_show_sc_threshold_value(thr->rawdata_mode6_max, "max"); */

	BTL_TEST_SAVE_INFO("RawDataMode6:");
	ret = sc_rawdata_test(tdata, thr->rawdata_mode6_min, thr->rawdata_mode6_max);
	if(ret < 0)
	{
		rawdata_result = false;
	}
	else
	{
		rawdata_result = true;
	}

	BTL_TEST_SAVE_INFO("\n");

test_err:
	if (cb_result && rawdata_result) {
		*test_result = true;
		BTL_TEST_SAVE_INFO("------ RAWDATA_CB_TEST_MODE6 PASS\n");
	} else {
		*test_result = false;
		BTL_TEST_SAVE_INFO("------ RAWDATA_CB_TEST_MODE6 NG\n");
	}

	BTL_TEST_FUNC_EXIT();
	return ret;
}

static int sc_short_test(struct btl_test *tdata, bool *test_result)
{
	int ret = 0;
	int i = 0;
	bool tmp_result = false;
	int *adc = NULL;
	int byte_num = 0;
	u8 cmd = 0x7b;
	u8 *data = NULL;
	
	struct sc_threshold *thr = &tdata->ic.sc.thr;
	
	BTL_TEST_FUNC_ENTER();
	
	memset(tdata->buffer, 0, tdata->buffer_length);
	adc = tdata->buffer;
    byte_num = tdata->node.node_num * 2;

	BTL_TEST_SAVE_INFO("\n============ Test Item: WEAK_SHORT_TEST test\n");

	data = (u8 *)btl_malloc(byte_num * sizeof(u8));
	if (NULL == data) {
		BTL_TEST_SAVE_ERR("malloc memory for short data buffer fail\n");
		return -ENOMEM;
	}
	if (!thr->short_min|| !thr->short_max) {
		BTL_TEST_SAVE_ERR("short_min/max is null\n");
		ret = -EINVAL;
		goto test_err;
	}
		
	ret = btl_enter_factory_mode();
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("failed to enter factory mode,ret=%d\n", ret);
		goto test_err;
	}

	for (i = 0; i < 5; i++) {
		ret = btl_start_scan();
		if(ret < 0)
		{
			BTL_TEST_ERROR("start scan fail\n");
			goto test_err;
		}
	}

	BTL_TEST_SAVE_INFO("ShortTest_Min:");
	btl_show_sc_threshold_value(thr->short_min, "min");
	BTL_TEST_SAVE_INFO("ShortTest_Max:");
	btl_show_sc_threshold_value(thr->short_max, "max");

	ret = btl_test_bus_read(&cmd, 1, data, byte_num);
	if(ret < 0)
	{
		BTL_TEST_ERROR("read adc fail\n");
		goto test_err;
	}
	for (i = 0; i < byte_num; i = i + 2) {
		adc[i >> 1] = (int)((data[i+1] << 8) + data[i]);
	}

	BTL_TEST_SAVE_INFO("Short:");
	btl_show_data_sc(adc);
	
	tmp_result = true;
	for (i = 0; i < tdata->sc_node.node_num; i++) {
		if ((adc[i] < thr->short_min[i])||(adc[i] > thr->short_max[i])) {
			tmp_result = false;
			BTL_TEST_SAVE_ERR("RX%d:%d,range=(%5d,%5d)\n", i + 1, adc[i], thr->short_min[i], thr->short_max[i]);
		}
	}

test_err:
	if (tmp_result) {
		*test_result = true;
		BTL_TEST_SAVE_INFO("------ Short Test PASS\n");
	} else {
		*test_result = false;
		BTL_TEST_SAVE_ERR("------ Short Test NG\n");
	}
	btl_free(data);
	BTL_TEST_FUNC_EXIT();
	return ret;
}

static int start_test_sc(void)
{
	int ret = 0;
	struct btl_test *tdata = btl_ftest;
	struct sc_testitem *test_item = &tdata->ic.sc.u.item;
	bool temp_result = false;
	bool test_result = true;
	BTL_TEST_FUNC_ENTER();

	/* rawdata cb mode1 test */
	if (true == test_item->rawdata_cb_mode1_test) {
		ret = sc_rawdata_cb_mode1_test(tdata, &temp_result);
		if ((ret < 0) || (false == temp_result)) {
			test_result = false;
		}
	}

	/* rawdata cb mode2 test */
	if (true == test_item->rawdata_cb_mode2_test) {
		ret = sc_rawdata_cb_mode2_test(tdata, &temp_result);
		if ((ret < 0) || (false == temp_result)) {
			test_result = false;
		}
	}

	/* rawdata cb mode3 test */
	if (true == test_item->rawdata_cb_mode3_test) {
		ret = sc_rawdata_cb_mode3_test(tdata, &temp_result);
		if ((ret < 0) || (false == temp_result)) {
			test_result = false;
		}
	}

	/* rawdata cb mode4 test */
	if (true == test_item->rawdata_cb_mode4_test) {
		ret = sc_rawdata_cb_mode4_test(tdata, &temp_result);
		if ((ret < 0) || (false == temp_result)) {
			test_result = false;
		}
	}

	/* rawdata cb mode5 test */
	if (true == test_item->rawdata_cb_mode5_test) {
		ret = sc_rawdata_cb_mode5_test(tdata, &temp_result);
		if ((ret < 0) || (false == temp_result)) {
			test_result = false;
		}
	}

	/* rawdata cb mode6 test */
	if (true == test_item->rawdata_cb_mode6_test) {
		ret = sc_rawdata_cb_mode6_test(tdata, &temp_result);
		if ((ret < 0) || (false == temp_result)) {
			test_result = false;
		}
	}
	if (!test_result)
		blt_tptest_result = blt_tptest_result | TEST_BEYOND_MIN_LIMIT | TEST_BEYOND_MAX_LIMIT;
	#if defined(RESET_PIN_WAKEUP)
	btl_ts_reset_wakeup();
	#endif
	
	/* short test */
	if (true == test_item->short_test) {
		g_test_item = 1;
		ret = sc_short_test(tdata, &temp_result);
		g_test_item = 0;
		if ((ret < 0) || (false == temp_result)) {
			test_result = false;
			blt_tptest_result = blt_tptest_result | TEST_GT_SHORT;
		}
	}
	
	return test_result;
}

struct test_funcs test_func_sc = {
	.ctype = {BTL_FLASH_ID},
	.hwtype = IC_HW_SC,
	.key_num_total = 3,
	.rawdata2_support = false,
	.raw_u16 = false,
	.mc_sc_short_v2 = false,
	.startscan_mode = SCAN_NORMAL,
	.start_test = start_test_sc,
};
#endif
