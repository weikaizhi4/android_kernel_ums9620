/************************************************************************
* Copyright (c) 2012-2020, BetterLife Systems, Ltd., all rights reserved.
*
* File Name: bl_test.h
*
* Author: Betterlife Driver Team
*
* Created: 2016-08-01
*
* Abstract: test entry for all IC
*
************************************************************************/
#ifndef _BL_TEST_H
#define _BL_TEST_H

/*****************************************************************************
* Included header files
*****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <asm/uaccess.h>
#include <linux/i2c.h>//iic
#include <linux/delay.h>//msleep
#include <linux/string.h>
#include <asm/unistd.h>
#include <linux/vmalloc.h>
#include <linux/time.h>
#include <linux/syscalls.h>
#include <linux/firmware.h>
#include "../bl_ts.h"
#include "bl_test_ini.h"

/*****************************************************************************
* Macro definitions using #define
*****************************************************************************/
#define BTL_RESULT_FILE_PATH					   "/mnt/sdcard/"
#define BTL_INI_FILE_PATH						"/mnt/sdcard/"
#define BTL_CSV_FILE_NAME						"_testdata.csv"
#define BTL_TXT_FILE_NAME						"_testresult.txt"

#define false 0
#define true  1
#define BTL_MAX_COMPATIBLE_TYPE					4
#define TEST_ICSERIES_LEN						(8)
#define TEST_ICSERIES(x)						((x) >> TEST_ICSERIES_LEN)

#define TEST_OPEN_MAX_VALUE						(255)
#define BYTES_PER_TIME							(32)  /* max:128 */
/* CSV & TXT */
#define CSV_LINE2_BUFFER_LEN					(1024)
#define CSV_BUFFER_LEN							(1024*80*5)
#define TXT_BUFFER_LEN							(1024*80*5)

#define TEST_SAVE_FAIL_RESULT					0

/*-----------------------------------------------------------
Test Status
-----------------------------------------------------------*/
#define RESULT_NULL								0
#define RESULT_PASS								1
#define RESULT_NG								2

#define TX_NUM_MAX								60
#define RX_NUM_MAX								100
#define SC_NUM_MAX					((TX_NUM_MAX) + (RX_NUM_MAX))
#define NUM_MAX_SC								(144)
#define KEY_NUM_MAX								6
#define TEST_ITEM_COUNT_MAX						32
#define TEST_ITEM_NAME_MAX						32
#define TEST_SHORT_RES_MAX						0xFFFF
#define MULTIFLYING_POWER_VALUE					10000

/*
 * factory test registers
 */
#define ENTER_WORK_FACTORY_RETRIES				5

#define START_SCAN_RETRIES_INCELL				20
#define START_SCAN_RETRIES_DELAY_INCELL			16
#define FACTORY_TEST_RETRY						50
#define FACTORY_TEST_DELAY						18
#define FACTORY_TEST_RETRY_DELAY				100

#define DEVIDE_MODE_ADDR						0x00
#define REG_FW_VERSION							0xB6
#define REG_VA_TOUCH_THR						0x80
#define REG_VKEY_TOUCH_THR						0x82
#define REG_CALI_CB								0x9c
#define REG_VOL_SCAN_COUNT_SET					0xc2
#define FACTORY_REG_LINE_ADDR					0x01
#define FACTORY_REG_CHX_NUM						0x23
#define FACTORY_REG_CHY_NUM						0x24
#define FACTORY_REG_CLB							0x04
#define FACTORY_REG_DATA_SELECT					0x06
#define FACTORY_REG_RAWBUF_SELECT				0x09
#define FACTORY_REG_KEY_CBWIDTH					0x0B
#define FACTORY_REG_PARAM_UPDATE_STATE			0x0E
#define FACTORY_REG_SHORT_TEST_EN				0x0F
#define FACTORY_REG_SHORT_TEST_STATE			0x10
#define FACTORY_REG_LCD_NOISE_START				0x11
#define FACTORY_REG_LCD_NOISE_FRAME				0x12
#define FACTORY_REG_LCD_NOISE_TEST_STATE		0x13
#define FACTORY_REG_LCD_NOISE_TTHR				0x14
#define FACTORY_REG_OPEN_START					0x15
#define FACTORY_REG_OPEN_STATE					0x16
#define FACTORY_REG_OPEN_ADDR					0xCF
#define FACTORY_REG_OPEN_IDLE					0x03
#define FACTORY_REG_OPEN_BUSY					0x01
#define FACTORY_REG_CB_ADDR_H					0x18
#define FACTORY_REG_CB_ADDR_L					0x19
#define FACTORY_REG_ORDER_ADDR_H				0x1A
#define FACTORY_REG_ORDER_ADDR_L				0x1B
#define FACTORY_REG_LCD_NOISE_STATE				0x1E
#define FACTORY_REG_KEYSHORT_EN					0x2E
#define FACTORY_REG_KEYSHORT_STATE				0x2F

#define FACTORY_REG_LEFT_KEY					0x1E
#define FACTORY_REG_RIGHT_KEY					0x1F
#define FACTORY_REG_OPEN_REG20					0x20
#define FACTORY_REG_OPEN_REG21					0x21
#define FACTORY_REG_OPEN_REG22					0x22
#define FACTORY_REG_OPEN_REG23					0x23
#define FACTORY_REG_OPEN_REG2E					0x2E
#define FACTORY_REG_OPEN_REG86					0x86
#define FACTORY_REG_K1							0x31
#define FACTORY_REG_K2							0x32
#define FACTORY_REG_RAWDATA_ADDR				0x6A
#define FACTORY_REG_ORDER_ADDR					0x6C
#define FACTORY_REG_CB_ADDR						0x6E
#define FACTORY_REG_SHORT_ADDR					0x89
#define FACTORY_REG_RAWDATA_TEST_EN				0x9E
#define FACTORY_REG_CB_TEST_EN					0x9F
#define FACTORY_REG_OPEN_TEST_EN				0xA0
#define FACTORY_REG_RAWDATA_TARGET				0xCA
#define FACTORY_REG_DRIVE_VOLTAGE				0x0C
#define FACTORY_REG_NORMAL_FREQ					0x11
#define FACTORY_REG_DRAM						0x3c
#define FACTORY_REG_PATTERN_TYPE				0xfb

/* mc_sc */
#define FACTORY_REG_FRE_LIST					0x0A
#define FACTORY_REG_DATA_TYPE					0x5B
#define FACTORY_REG_NORMALIZE					0x16
#define FACTORY_REG_RAWDATA_ADDR_MC_SC			0x20
#define FACTORY_REG_PATTERN						0x53
#define FACTORY_REG_NOMAPPING					0x54
#define FACTORY_REG_CHX_NUM_NOMAP				0x55
#define FACTORY_REG_CHY_NUM_NOMAP				0x56
#define FACTORY_REG_WC_SEL						0x09
#define FACTORY_REG_HC_SEL						0x0F
#define FACTORY_REG_MC_SC_MODE					0x44
#define FACTORY_REG_MC_SC_CB_ADDR_OFF			0x45
#define FACTORY_REG_MC_SC_CB_H_ADDR_OFF			0x49
#define FACTORY_REG_MC_SC_CB_ADDR				0x0d
#define FACTROY_REG_SHORT_TEST_EN				0x07
#define FACTROY_REG_SHORT_CA					0x01
#define FACTROY_REG_SHORT_CC					0x02
#define FACTROY_REG_SHORT_CG					0x03
#define FACTROY_REG_SHORT_OFFSET				0x04
#define FACTROY_REG_SHORT_AB_CH					0x58
#define FACTROY_REG_SHORT_RES_LEVEL				0x5A
#define FACTORY_REG_SHORT_ADDR_MC				0x21
#define FACTORY_REG_FIR							0xFB

#define FACTROY_REG_SHORT2_TEST_EN				0xC0
#define FACTROY_REG_SHORT2_CA					0x01
#define FACTROY_REG_SHORT2_CC					0x02
#define FACTROY_REG_SHORT2_CG					0x03
#define FACTROY_REG_SHORT2_OFFSET				0x04
#define FACTROY_REG_SHORT2_RES_LEVEL			0xC1
#define FACTROY_REG_SHORT2_DEALY				0xC2
#define FACTROY_REG_SHORT2_TEST_STATE			0xC3
#define FACTORY_REG_SHORT2_ADDR_MC				0xC4
#define FACTROY_REG_SHORT2_AB_CH				0xC6

/* sc */
#define FACTORY_REG_SCAN_ADDR2					0x08
#define FACTORY_REG_PRO_SC						0x0e
#define FACTORY_REG_SCAN_SC						0x20
#define FACTORY_REG_FREQ_SC						0x0f
#define FACTORY_REG_VDD_SC						0x0c
#define FACTORY_REG_CF_SC						0x0d
#define FACTORY_REG_TVALUE_SC					0x5e
#define FACTORY_REG_CH_NUM_SC					0x14
#define FACTORY_REG_KEY_NUM_SC					0x15
#define FACTORY_REG_SC_CB_ADDR_OFF				0x33
#define FACTORY_REG_SC_CB_ADDR					0x39
#define FACTORY_REG_RAWDATA_SADDR_SC			0x34
#define FACTORY_REG_RAWDATA_ADDR_SC				0x35
#define FACTORY_REG_CB_SEL						0x41
#define FACTORY_REG_FMODE						0xAE

#define TEST_RETVAL_00							0x00
#define TEST_RETVAL_AA							0xAA

#define BTL_MAX_SORT_SC							32768
#define BTL_MIN_SORT_SC							0

/*****************************************************************************
* enumerations, structures and unions
*****************************************************************************/
struct item_info {
	char name[TEST_ITEM_NAME_MAX];
	int code;
	int *data;
	int datalen;
	int result;
	int mc_sc;
	int key_support;
};

struct btl_test_data {
	int item_count;
	struct item_info info[TEST_ITEM_COUNT_MAX];
};

/* incell */
struct incell_testitem {
	u32 short_test					: 1;
	u32 open_test					: 1;
	u32 cb_test						: 1;
	u32 rawdata_test				: 1;
	u32 lcdnoise_test				: 1;
	u32 keyshort_test				: 1;
	u32 mux_open_test				: 1;
};

struct incell_threshold_b {
	int short_res_min;
	int short_res_vk_min;
	int open_cb_min;
	int open_k1_check;
	int open_k1_value;
	int open_k2_check;
	int open_k2_value;
	int cb_min;
	int cb_max;
	int cb_vkey_check;
	int cb_min_vk;
	int cb_max_vk;
	int rawdata_min;
	int rawdata_max;
	int rawdata_vkey_check;
	int rawdata_min_vk;
	int rawdata_max_vk;
	int lcdnoise_frame;
	int lcdnoise_coefficient;
	int lcdnoise_coefficient_vkey;
	int open_diff_min;
	int open_nmos;
	int keyshort_k1;
	int keyshort_cb_max;
	int rawdata2_min;
	int rawdata2_max;
	int mux_open_cb_min;
	int open_delta_V;
};

struct incell_threshold {
	struct incell_threshold_b basic;
	int *rawdata_min;
	int *rawdata_max;
	int *rawdata2_min;
	int *rawdata2_max;
	int *cb_min;
	int *cb_max;
};

struct incell_test {
	struct incell_threshold thr;
	union {
		int tmp;
		struct incell_testitem item;
	} u;
};

/* mc_sc */
enum mapping_type {
	MAPPING = 0,
	NO_MAPPING = 1,
};

struct mc_sc_testitem {
	u32 rawdata_test				: 1;
	u32 rawdata_uniformity_test		: 1;
	u32 scap_cb_test				: 1;
	u32 scap_rawdata_test			: 1;
	u32 short_test					: 1;
	u32 panel_differ_test			: 1;
	u32 relative_rawdata_test		: 1;
	u32 dram_test					: 1;
};

struct mc_sc_threshold_b {
	int rawdata_h_min;
	int rawdata_h_max;
	int rawdata_set_hfreq;
	int rawdata_l_min;
	int rawdata_l_max;
	int rawdata_set_lfreq;
	int uniformity_check_tx;
	int uniformity_check_rx;
	int uniformity_check_min_max;
	int uniformity_tx_hole;
	int uniformity_rx_hole;
	int uniformity_min_max_hole;
	int scap_cb_off_min;
	int scap_cb_off_max;
	int scap_cb_wp_off_check;
	int scap_cb_on_min;
	int scap_cb_on_max;
	int scap_cb_wp_on_check;
	int scap_rawdata_off_min;
	int scap_rawdata_off_max;
	int scap_rawdata_wp_off_check;
	int scap_rawdata_on_min;
	int scap_rawdata_on_max;
	int scap_rawdata_wp_on_check;
	int short_cg;
	int short_cc;
	int panel_differ_min;
	int panel_differ_max;
	int scap_cb_hi_min;
	int scap_cb_hi_max;
	int scap_cb_hi_check;
	int scap_rawdata_hi_min;
	int scap_rawdata_hi_max;
	int scap_rawdata_hi_check;
	int scap_cb_hov_min;
	int scap_cb_hov_max;
	int scap_cb_hov_check;
	int scap_rawdata_hov_min;
	int scap_rawdata_hov_max;
	int scap_rawdata_hov_check;
	int panel_differ_vol;
	int panel_differ_high_freq;
	int panel_differ_low_freq;
	int rel_rawdata_min;
	int rel_rawdata_max;
};

struct mc_sc_threshold {
	struct mc_sc_threshold_b basic;
	int *rawdata_h_min;
	int *rawdata_h_max;
	int *rawdata_l_min;
	int *rawdata_l_max;
	int *tx_linearity_max;
	int *tx_linearity_min;
	int *rx_linearity_max;
	int *rx_linearity_min;
	int *scap_cb_off_min;
	int *scap_cb_off_max;
	int *scap_cb_on_min;
	int *scap_cb_on_max;
	int *scap_cb_hi_min;
	int *scap_cb_hi_max;
	int *scap_cb_hov_min;
	int *scap_cb_hov_max;
	int *scap_rawdata_off_min;
	int *scap_rawdata_off_max;
	int *scap_rawdata_on_min;
	int *scap_rawdata_on_max;
	int *scap_rawdata_hi_min;
	int *scap_rawdata_hi_max;
	int *scap_rawdata_hov_min;
	int *scap_rawdata_hov_max;
	int *panel_differ_min;
	int *panel_differ_max;
	int *panel_differ_vol;
	int *panel_differ_high_freq;
	int *panel_differ_low_freq;
	int *short_cg;
	int *short_cc;
	int *rel_rawdata_min;
	int *rel_rawdata_max;
};

struct mc_sc_test {
	struct mc_sc_threshold thr;
	union {
		u32 tmp;
		struct mc_sc_testitem item;
	} u;
};

/* sc */
struct sc_testitem {
	u32 rawdata_test				: 1;
	u32 cb_test						: 1;
	u32 delta_cb_test				: 1;
	u32 rawdata_cb_mode1_test		: 1;
	u32 rawdata_cb_mode2_test		: 1;
	u32 rawdata_cb_mode3_test		: 1;
	u32 rawdata_cb_mode4_test		: 1;
	u32 rawdata_cb_mode5_test		: 1;
	u32 rawdata_cb_mode6_test		: 1;
	u32 short_test					: 1;
};

struct sc_threshold_b {
	int rawdata_min;
	int rawdata_max;
	int cb_min;
	int cb_max;
	int dcb_base;
	int dcb_differ_max;
	int dcb_key_check;
	int dcb_key_differ_max;
	int dcb_ds1;
	int dcb_ds2;
	int dcb_ds3;
	int dcb_ds4;
	int dcb_ds5;
	int dcb_ds6;
	int dcb_critical_check;
	int dcb_cs1;
	int dcb_cs2;
	int dcb_cs3;
	int dcb_cs4;
	int dcb_cs5;
	int dcb_cs6;
	int test_mode1_freq;
	int test_mode2_freq;
	int test_mode3_freq;
	int test_mode4_freq;
	int test_mode5_freq;
	int test_mode6_freq;
	int test_mode1_vdd;
	int test_mode2_vdd;
	int test_mode3_vdd;
	int test_mode4_vdd;
	int test_mode5_vdd;
	int test_mode6_vdd;
	int test_mode1_cf;
	int test_mode2_cf;
	int test_mode3_cf;
	int test_mode4_cf;
	int test_mode5_cf;
	int test_mode6_cf;
	int test_mode1_tvalue;
	int test_mode2_tvalue;
	int test_mode3_tvalue;
	int test_mode4_tvalue;
	int test_mode5_tvalue;
	int test_mode6_tvalue;
	int rawdata_mode1_min;
	int rawdata_mode1_max;
	int rawdata_mode2_min;
	int rawdata_mode2_max;
	int rawdata_mode3_min;
	int rawdata_mode3_max;
	int rawdata_mode4_min;
	int rawdata_mode4_max;
	int rawdata_mode5_min;
	int rawdata_mode5_max;
	int rawdata_mode6_min;
	int rawdata_mode6_max;
	int cb_mode1_min;
	int cb_mode1_max;
	int cb_mode2_min;
	int cb_mode2_max;
	int cb_mode3_min;
	int cb_mode3_max;
	int cb_mode4_min;
	int cb_mode4_max;
	int cb_mode5_min;
	int cb_mode5_max;
	int cb_mode6_min;
	int cb_mode6_max;
	int short_min;
	int short_max;
};

struct sc_threshold {
	struct sc_threshold_b basic;
	int *rawdata_min;
	int *rawdata_max;
	int *cb_min;
	int *cb_max;
	int *dcb_sort;
	int *dcb_base;
	int *rawdata_mode1_min;
	int *rawdata_mode1_max;
	int *rawdata_mode2_min;
	int *rawdata_mode2_max;
	int *rawdata_mode3_min;
	int *rawdata_mode3_max;
	int *rawdata_mode4_min;
	int *rawdata_mode4_max;
	int *rawdata_mode5_min;
	int *rawdata_mode5_max;
	int *rawdata_mode6_min;
	int *rawdata_mode6_max;
	int *cb_mode1_base;
	int *cb_mode1_min;
	int *cb_mode1_max;
	int *cb_mode2_base;
	int *cb_mode2_min;
	int *cb_mode2_max;
	int *cb_mode3_base;
	int *cb_mode3_min;
	int *cb_mode3_max;
	int *cb_mode4_base;
	int *cb_mode4_min;
	int *cb_mode4_max;
	int *cb_mode5_base;
	int *cb_mode5_min;
	int *cb_mode5_max;
	int *cb_mode6_base;
	int *cb_mode6_min;
	int *cb_mode6_max;
	int *short_min;
	int *short_max;
};

struct sc_test {
	struct sc_threshold thr;
	union {
		u32 tmp;
		struct sc_testitem item;
	} u;
};

enum test_hw_type {
	IC_HW_INCELL = 1,
	IC_HW_MC_SC,
	IC_HW_SC,
};

enum test_scan_mode {
	SCAN_NORMAL = 0,
	SCAN_SC,
};

struct btl_test_node {
	int channel_num;
	int tx_num;
	int rx_num;
	int node_num;
	int key_num;
};

struct btl_test {
	struct btl_test_node node;
	struct btl_test_node sc_node;
	u8 fw_ver;
	u8 pattern_type;
	u8 va_touch_thr;
	u8 vk_touch_thr;
	bool key_support;
	bool v3_pattern;
	u8 mapping;
	u8 normalize;
	int test_num;
	int *buffer;
	int buffer_length;
	int *node_valid;
	int *node_valid_sc;
	int basic_thr_count;
	int code1;
	int code2;
	int offset;
	union {
		struct incell_test incell;
		struct mc_sc_test mc_sc;
		struct sc_test sc;
	} ic;

	struct test_funcs *func;
	struct btl_test_data testdata;
	char *testresult;
	int testresult_len;
	int result;
#if defined(TEST_SAVE_FAIL_RESULT) && TEST_SAVE_FAIL_RESULT
	struct timeval tv;
#endif
	struct ini_data ini;
};

struct test_funcs {
	u16 ctype[BTL_MAX_COMPATIBLE_TYPE];
	enum test_hw_type hwtype;
	int startscan_mode;
	int key_num_total;
	bool rawdata2_support;
	bool force_touch;
	bool mc_sc_short_v2;
	bool raw_u16;
	bool cb_high_support;
	int (*param_init)(void);
	int (*init)(void);
	int (*start_test)(void);
};

enum byte_mode {
	DATA_ONE_BYTE,
	DATA_TWO_BYTE,
};
/* mc_sc */
enum normalize_type {
	NORMALIZE_OVERALL,
	NORMALIZE_AUTO,
};

enum wp_type {
	WATER_PROOF_OFF = 0,
	WATER_PROOF_ON = 1,
	HIGH_SENSITIVITY = 2,
	HOV = 3,
	WATER_PROOF_ON_TX = 100,
	WATER_PROOF_ON_RX,
	WATER_PROOF_OFF_TX,
	WATER_PROOF_OFF_RX,
};
/* mc end */

/* sc */
enum factory_mode {
	FACTORY_NORMAL,
	FACTORY_TESTMODE_1,
	FACTORY_TESTMODE_2,
};

enum dcb_sort_num {
	DCB_SORT_MIN = 1,
	DCB_SORT_MAX = 6,
};

struct dcb_sort_d {
	int ch_num;
	int deviation;
	int critical;
	int min;
	int max;
};
/* sc end */

enum csv_itemcode_incell {
	CODE_ENTER_FACTORY_MODE = 0,
	CODE_RAWDATA_TEST = 7,
	CODE_CB_TEST = 12,
	CODE_SHORT_TEST = 15,
	CODE_OPEN_TEST = 25,
	CODE_LCD_NOISE_TEST = 27,
	CODE_MUX_OPEN_TEST = 41,
};

enum csv_itemcode_mc_sc {
	CODE_M_RAWDATA_TEST = 7,
	CODE_M_SCAP_CB_TEST = 9,
	CODE_M_SCAP_RAWDATA_TEST = 10,
	CODE_M_WEAK_SHORT_CIRCUIT_TEST = 15,
	CODE_M_RAWDATA_UNIFORMITY_TEST = 16,
	CODE_M_PANELDIFFER_TEST = 20,
	CODE_M_REL_RAWDATA_TEST = 22,
};

enum csv_itemcode_sc {
	CODE_S_RAWDATA_TEST = 7,
	CODE_S_CB_TEST = 13,
	CODE_S_DCB_TEST = 14,
};
enum minus_cmd {
	LEFT_MINUS_CENTER		= 0x00,
	LEFT_MINUS_RIGHT		 = 0x01,
	TOP_MINUS_CENTER		 = 0x02,
	TOP_MINUS_BOTTOM		 = 0x03,
	RIGHT_MINUS_CENTER		 = 0x04,
	BOTTOM_MINUS_CENTER		 = 0x05,
};

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/
extern struct test_funcs test_func_mcsc;
extern struct test_funcs test_func_sc;
extern struct btl_test *btl_ftest;
extern int g_test_item;
void btl_sys_delay(int ms);
int btl_abs(int value);
void btl_print_buffer(int *buffer, int length, int line_num);
int btl_test_read_reg(u8 addr, u8 *val);
int btl_test_write_reg(u8 addr, u8 val);
int btl_test_read(u8 addr, u8 *readbuf, int readlen);
int btl_test_write(u8 addr, u8 *writebuf, int writelen);
int btl_test_bus_read(u8 *cmd, int cmdlen, u8 *data, int datalen);
int btl_test_bus_write(u8 *writebuf, int writelen);
void btl_show_data_sc(int *data);
int btl_select_test_mode(u8 proValue, u8 scanMode, u8 freq, u8 vdd, u8 cf, u8 tValueHigh, u8 tValueLow);
void btl_show_sc_threshold_value(int *data, char *str);
int btl_enter_work_mode(void);
int btl_enter_factory_mode(void);
int btl_read_mass_data(u8 *cmd, u8 cmd_len, int byte_num, int *buf);
int btl_chip_clb(void);
int btl_wait_state_update(u8 retval);
int btl_get_cb_incell(u16 saddr, int byte_num, int *cb_buf);
int btl_short_get_adcdata_incell(u8 retval, u8 ch_num, int byte_num, int *adc_buf);
int btl_start_scan(void);
int btl_get_rawdata(int *data);
int btl_get_cb_sc(int byte_num, int *cb_buf, enum byte_mode mode);
bool btl_compare_data(int *data, int min, int max, int min_vk, int max_vk, bool key);
bool btl_compare_array(int *data, int *min, int *max, bool key);
bool btl_compare_sc_cb(bool tx_check, bool rx_check, int *data, int *min, int *max);
bool btl_compare_mc_sc(bool, bool, int *, int *, int *);
bool btl_compare_diff(int *data, int *min, int *max, bool key);
void btl_show_data(int *data, bool key);
/* mc_sc */
int btl_mapping_switch(u8 mapping);
bool btl_get_fw_wp(u8 wp_channel_select, enum wp_type water_proof_type);
int btl_get_cb_mc_sc(u8 wp, int byte_num, int *cb_buf, enum byte_mode mode);
int btl_get_rawdata_mc_sc(enum wp_type wp, int *data);
int btl_get_rawdata_mc(u8 fre, u8 fir, int *rawdata);
int btl_short_get_adc_data_mc(u8 retval, int byte_num, int *adc_buf, u8 mode);
void btl_show_data_mc_sc(int *data);
void *btl_malloc(size_t size);
void btl_free_proc(void *p);
void btl_test_save_data(char *name, int code, int *data, int datacnt,
						bool mc_sc, bool key, bool result);
int btl_get_panel_diff(int *data, int *hig_freq, int *low_freq);
int btl_sc_cb_calibrate(u8 mode);
int set_drive_freq(u8 reg, u8 *val);
int get_drive_value_freq(u8 reg, u8 *val);
void btl_show_mc_sc_threshold_value(int *data, bool tx_check, bool rx_check, char *str);
void btl_show_threshold_value(int *data, bool key, char *str);
int btl_get_relative_rawdata_value(int tx_num, int rx_num, int *rawdata);
int btl_dram_test(void);
void btl_sc_cb_to_slope(u8 *cb, u8 rx_num, u8 key_num, u8 pattern_type, int *result);
#define btl_malloc_r(p, size) do {\
	if (NULL == p) {\
		p = btl_malloc(size);\
		if (NULL == p) {\
			return -ENOMEM;\
		}\
	}\
} while(0)

#define btl_free(p) do {\
	if (p) {\
		btl_free_proc(p);\
		p = NULL;\
	}\
} while(0)

#define CSV_SUPPORT				0
#define TXT_SUPPORT				1
#define ABS(x) ((x) > 0 ? (x) : -(x))
#define MAX( _x, _y )								   ( ((_x) > (_y)) ? (_x) : (_y) )

#define BTL_TEST_DBG(fmt, args...) do { \
printk("[BTL_TS][TEST]%s:"fmt"\n",	__func__, ##args); \
} while (0)

#define BTL_TEST_FUNC_ENTER() do { \
	printk("[BTL_TS][TEST]%s: Enter\n", __func__); \
} while (0)

#define BTL_TEST_FUNC_EXIT()  do { \
	printk("[BTL_TS][TEST]%s: Exit(%d)\n", __func__, __LINE__); \
} while (0)

#define BTL_TEST_INFO(fmt, args...) do { \
	printk("[BTL_TS/I][TEST]%s:"fmt"\n", __func__, ##args); \
} while (0)

#define BTL_TEST_ERROR(fmt, args...) do { \
	printk("[BTL_TS/E][TEST]%s:"fmt"\n", __func__, ##args); \
} while (0)

#define BTL_TEST_SAVE_INFO(fmt, args...) do { \
	if (btl_ftest->testresult) { \
		btl_ftest->testresult_len += snprintf( \
		btl_ftest->testresult + btl_ftest->testresult_len, \
		TXT_BUFFER_LEN, \
		fmt, ##args);\
	} \
} while (0)

#define BTL_TEST_SAVE_ERR(fmt, args...)	 do { \
	if (btl_ftest->testresult && (btl_ftest->testresult_len < TXT_BUFFER_LEN)) { \
		btl_ftest->testresult_len += snprintf( \
		btl_ftest->testresult + btl_ftest->testresult_len, \
		TXT_BUFFER_LEN, \
		fmt, ##args);\
	} \
	printk(KERN_ERR "[BTL_TS/E][TEST]%s:"fmt"\n", __func__, ##args);\
} while (0)
#endif
