/************************************************************************
* Copyright (c) 2012-2020, Betterlife Systems (R)£¬All Rights Reserved.
*
* File Name: bl_test_ini.c
*
* Author: Betterlife Driver Team
*
* Created: 2021-05-25
*
* Abstract: parsing function of INI file
*
************************************************************************/
#include "bl_test.h"
#ifdef BTL_FACTORY_TEST_EN
/*****************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/
#define BTL_INI_REQUEST_SUPPORT				 1

struct ini_ic_type btl_ic_types[] = {
	{BTL_CHIP_NAME,	 BTL_FLASH_ID},
};

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/

/*****************************************************************************
* Static function prototypes
*****************************************************************************/
/* Works only for digits and letters, but small and fast */
#define TOLOWER(x) ((x) | 0x20)
static int btl_strncmp(const char *cs, const char *ct, int count)
{
	u8 c1 = 0, c2 = 0;

	while (count) {
		if	((*cs == '\0') || (*ct == '\0'))
			return -1;
		c1 = TOLOWER(*cs++);
		c2 = TOLOWER(*ct++);
		if (c1 != c2)
			return c1 < c2 ? -1 : 1;
		if (!c1)
			break;
		count--;
	}

	return 0;
}

static int btl_isspace(int x)
{
	if (x == ' ' || x == '\t' || x == '\n' || x == '\f' || x == '\b' || x == '\r')
		return 1;
	else
		return 0;
}

static int btl_isdigit(int x)
{
	if (x <= '9' && x >= '0')
		return 1;
	else
		return 0;
}

static long btl_atol(char *nptr)
{
	int c; /* current char */
	long total; /* current total */
	int sign; /* if ''-'', then negative, otherwise positive */
	/* skip whitespace */
	while ( btl_isspace((int)(unsigned char)*nptr) )
		++nptr;
	c = (int)(unsigned char) * nptr++;
	sign = c; /* save sign indication */
	if (c == '-' || c == '+')
		c = (int)(unsigned char) * nptr++; /* skip sign */
	total = 0;
	while (btl_isdigit(c)) {
		total = 10 * total + (c - '0'); /* accumulate digit */
		c = (int)(unsigned char) * nptr++; /* get next char */
	}
	if (sign == '-')
		return -total;
	else
		return total; /* return result, negated if necessary */
}

static int btl_atoi(char *nptr)
{
	return (int)btl_atol(nptr);
}

static int btl_test_get_ini_via_request_firmware(struct ini_data *ini, char *fwname)
{
	int ret = 0;
	const struct firmware *fw = NULL;
	struct i2c_client* client = g_btl_ts->client;
	struct device *dev = &client->dev;

#if !BTL_INI_REQUEST_SUPPORT
	return -EINVAL;
#endif

	ret = request_firmware(&fw, fwname, dev);
	if (0 == ret) {
		BTL_TEST_INFO("firmware request(%s) success", fwname);
		ini->data = vmalloc(fw->size + 1);
		if (NULL == ini->data) {
			BTL_TEST_ERROR("ini->data buffer vmalloc fail");
			ret = -ENOMEM;
		} else {
			memcpy(ini->data, fw->data, fw->size);
			ini->data[fw->size] = '\n';
			ini->length = fw->size + 1;
		}
	} else {
		BTL_TEST_INFO("firmware request(%s) fail,ret=%d", fwname, ret);
	}

	if (fw != NULL) {
		release_firmware(fw);
		fw = NULL;
	}

	return ret;
}


static void str_space_remove(char *str)
{
	char *t = str;
	char *s = str;

	while (*t != '\0') {
		if (*t != ' ') {
			*s = *t;
			s++;
		}
		t++;
	}

	*s = '\0';
}

static void print_ini_data(struct ini_data *ini)
{
	int i = 0;
	int j = 0;
	struct ini_section *section = NULL;
	struct ini_keyword *keyword = NULL;
	struct btl_test *tdata = btl_ftest;

	if (tdata && (btl_log_level < 10)) {
		return;
	}

	if (!ini || !ini->tmp) {
		BTL_TEST_DBG("ini is null");
		return;
	}

	BTL_TEST_DBG("section num:%d, keyword num total:%d",
				 ini->section_num, ini->keyword_num_total);
	for (i = 0; i < ini->section_num; i++) {
		section = &ini->section[i];
		BTL_TEST_DBG("section name:[%s] keyword num:%d",
					 section->name, section->keyword_num);
		for (j = 0; j < section->keyword_num; j++) {
			keyword = &section->keyword[j];
			BTL_TEST_DBG("%s=%s", keyword->name, keyword->value);
		}
	}
}

static int ini_get_line(char *filedata, char *line_data, int *line_len)
{
	int i = 0;
	int line_length = 0;
	int type;

	/* get a line data */
	for (i = 0; i < MAX_INI_LINE_LEN; i++) {
		if (('\n' == filedata[i]) || ('\r' == filedata[i])) {
			line_data[line_length++] = '\0';
			if (('\n' == filedata[i + 1]) || ('\r' == filedata[i + 1])) {
				line_length++;
			}
			break;
		} else {
			line_data[line_length++] = filedata[i];
		}
	}

	if (i >= MAX_INI_LINE_LEN) {
		BTL_TEST_ERROR("line length(%d)>max(%d)", line_length, MAX_INI_LINE_LEN);
		return -ENODATA;
	}

	/* remove space */
	str_space_remove(line_data);

	/* confirm line type */
	if (('\0' == line_data[0]) || ('#' == line_data[0])) {
		type = LINE_OTHER;
	} else if ('[' == line_data[0]) {
		type = LINE_SECTION;
	} else {
		type = LINE_KEYWORD; /* key word */
	}

	*line_len = line_length;
	return type;
}

static int ini_parse_keyword(struct ini_data *ini, char *line_buffer)
{
	int i = 0;
	int offset = 0;
	int length = strlen(line_buffer);
	struct ini_section *section = NULL;

	for (i = 0; i < length; i++) {
		if (line_buffer[i] == '=')
			break;
	}

	if ((i == 0) || (i >= length)) {
		BTL_TEST_ERROR("mark(=)in keyword line fail");
		return -ENODATA;
	}

	if ((ini->section_num > 0) && (ini->section_num < MAX_INI_SECTION_NUM)) {
		section = &ini->section[ini->section_num - 1];
	}

	if (NULL == section) {
		BTL_TEST_ERROR("section is null");
		return -ENODATA;
	}

	offset = ini->keyword_num_total;
	if (offset > MAX_KEYWORD_NUM) {
		BTL_TEST_ERROR("keyword num(%d)>max(%d),please check MAX_KEYWORD_NUM",
					   ini->keyword_num_total, MAX_KEYWORD_NUM);
		return -ENODATA;
	}
	memcpy(ini->tmp[offset].name, &line_buffer[0], i);
	ini->tmp[offset].name[i] = '\0';
	memcpy(ini->tmp[offset].value, &line_buffer[i + 1], length - i - 1);
	ini->tmp[offset].value[length - i - 1] = '\0';
	section->keyword_num++;
	ini->keyword_num_total++;

	return 0;
}

static int ini_parse_section(struct ini_data *ini, char *line_buffer)
{
	int length = strlen(line_buffer);
	struct ini_section *section = NULL;

	if ((length <= 2) || (length > MAX_KEYWORD_NAME_LEN)) {
		BTL_TEST_ERROR("section line length fail");
		return -EINVAL;
	}

	if ((ini->section_num < 0) || (ini->section_num >= MAX_INI_SECTION_NUM)) {
		BTL_TEST_ERROR("section_num(%d) fail", ini->section_num);
		return -EINVAL;
	}
	section = &ini->section[ini->section_num];
	memcpy(section->name, line_buffer + 1, length - 2);
	section->name[length - 2] = '\0';
	BTL_TEST_INFO("section:%s, keyword offset:%d",
				  section->name, ini->keyword_num_total);
	section->keyword = (struct ini_keyword *)&ini->tmp[ini->keyword_num_total];
	section->keyword_num = 0;
	ini->section_num++;
	if (ini->section_num > MAX_INI_SECTION_NUM) {
		BTL_TEST_ERROR("section num(%d)>max(%d), please check MAX_INI_SECTION_NUM",
					   ini->section_num, MAX_INI_SECTION_NUM);
		return -ENOMEM;
	}

	return 0;
}

static int ini_init_inidata(struct ini_data *ini)
{
	int pos = 0;
	int ret = 0;
	char line_buffer[MAX_INI_LINE_LEN] = { 0 };
	int line_len = 0;

	if (!ini || !ini->data || !ini->tmp) {
		BTL_TEST_DBG("ini/data/tmp is null");
		return -EINVAL;
	}

	while (pos < ini->length) {
		ret = ini_get_line(ini->data + pos, line_buffer, &line_len);
		if (ret < 0) {
			BTL_TEST_ERROR("ini_get_line fail");
			return ret;
		} else if (ret == LINE_KEYWORD) {
			ret = ini_parse_keyword(ini, line_buffer);
			if (ret < 0) {
				BTL_TEST_ERROR("ini_parse_keyword fail");
				return ret;
			}
		} else if (ret == LINE_SECTION) {
			ret = ini_parse_section(ini, line_buffer);
			if (ret < 0) {
				BTL_TEST_ERROR("ini_parse_section fail");
				return ret;
			}
		}

		pos += line_len;
	}

	print_ini_data(ini);
	return 0;
}

static int ini_get_key(char *section_name, char *key_name, char *value)
{
	int i = 0;
	int j = 0;
	struct ini_data *ini = &btl_ftest->ini;
	struct ini_section *section;
	struct ini_keyword *keyword;
	int key_len = 0;
	int log_level = btl_log_level;

	if (log_level >= 10) {
		BTL_TEST_DBG("section name:%s, key name:%s", section_name, key_name);
		BTL_TEST_DBG("section num:%d", ini->section_num);
	}

	for (i = 0; i < ini->section_num; i++) {
		section = &ini->section[i];
		key_len = strlen(section_name);
		if (key_len != strlen(section->name))
			continue;
		if (btl_strncmp(section->name, section_name, key_len) != 0)
			continue;

		if (log_level >= 10) {
			BTL_TEST_DBG("section name:%s keyword num:%d",
						 section->name, section->keyword_num);
		}
		for (j = 0; j < section->keyword_num; j++) {
			keyword = &section->keyword[j];
			key_len = strlen(key_name);
			if (key_len == strlen(keyword->name)) {
				if (0 == btl_strncmp(keyword->name, key_name, key_len)) {
					key_len = strlen(keyword->value);
					memcpy(value, keyword->value, key_len);
					if (log_level >= 3) {
						BTL_TEST_DBG("section:%s,%s=%s",
									 section_name, key_name, value);
					}

					return key_len;
				}
			}
		}
	}

	return -ENODATA;
}

/* return keyword's value length if success */
static int ini_get_string_value(char *section_name, char *key_name, char *rval)
{
	if (!section_name || !key_name || !rval) {
		BTL_TEST_ERROR("section_name/key_name/rval is null");
		return -EINVAL;
	}

	return ini_get_key(section_name, key_name, rval);
}

int btl_get_keyword_value(char *section, char *name, int *value)
{
	int ret = 0;
	char str[MAX_KEYWORD_VALUE_LEN] = { 0 };

	ret = ini_get_string_value(section, name, str);
	if (ret > 0) {
		/* search successfully, so change value, otherwise keep default */
		*value = btl_atoi(str);
	}

	return ret;
}

static void btl_init_buffer(int *buffer, int value, int len, bool key_check, int key_value, int key_len)
{
	int i = 0;
	int va_len = 0;

	if (NULL == buffer) {
		BTL_TEST_ERROR("buffer is null\n");
		return;
	}

	va_len = len - key_len;
	if (va_len < 0) {
		BTL_TEST_ERROR("total len(0x%x) less key len(0x%x)\n", len, key_len);
		return;
	}

	for (i = 0; i < len; i++) {
		buffer[i] = value;
	}

	if (key_check) {
		for (i = 0; i < key_len; i++) {
			buffer[va_len + i] = key_value;
		}
	}

}

static int get_test_item(char name[][MAX_KEYWORD_NAME_LEN], int length, int *val)
{
	int i = 0;
	int ret = 0;
	int tmpval = 0;

	if (length > TEST_ITEM_COUNT_MAX) {
		BTL_TEST_SAVE_ERR("test item count(%d) > max(%d)\n",
						  length, TEST_ITEM_COUNT_MAX);
		return -EINVAL;
	}

	BTL_TEST_INFO("test items in total of driver:%d", length);
	*val = 0;
	for (i = 0; i < length; i++) {
		tmpval = 0;
		ret = get_value_testitem(name[i], &tmpval);
		if (ret < 0) {
			BTL_TEST_DBG("test item:%s not found", name[i]);
		} else {
			BTL_TEST_DBG("test item:%s=%d", name[i], tmpval);
			*val |= (tmpval << i);
		}
	}

	return 0;
}

static int get_basic_threshold(char name[][MAX_KEYWORD_NAME_LEN], int length, int *val)
{
	int i = 0;
	int ret = 0;
	struct btl_test *tdata = btl_ftest;
	int log_level = btl_log_level;

	BTL_TEST_INFO("basic_thr string length(%d), count(%d)\n", length, tdata->basic_thr_count);
	if (length > btl_ftest->basic_thr_count) {
		BTL_TEST_SAVE_ERR("basic_thr string length > count\n");
		return -EINVAL;
	}

	for (i = 0; i < length; i++) {
		ret = get_value_basic(name[i], &val[i]);
		if (log_level >= 3) {
			if (ret < 0) {
				BTL_TEST_DBG("basic thr:%s not found", name[i]);
			} else {
				BTL_TEST_DBG("basic thr:%s=%d", name[i], val[i]);
			}
		}
	}

	return 0;
}

static void get_detail_threshold(char *key_name, bool is_prex, int *thr, int node_num)
{
	char str[MAX_KEYWORD_VALUE_LEN] = { 0 };
	char str_temp[MAX_KEYWORD_NAME_LEN] = { 0 };
	char str_tmp[MAX_KEYWORD_VALUE_ONE_LEN] = { 0 };
	struct btl_test *tdata = btl_ftest;
	int divider_pos = 0;
	int index = 0;
	int i = 0;
	int j = 0;
	int k = 0;
	int tx_num = 0;
	int rx_num = 0;
	int thr_pos = 0;

	if (!key_name || !thr) {
		BTL_TEST_ERROR("key_name/thr is null");
		return;
	}

	if (is_prex) {
		tx_num = tdata->node.tx_num;
		rx_num = tdata->node.rx_num;
	}
	for (i = 0; i < tx_num + 1; i++) {
		if (is_prex) {
			snprintf(str_temp, MAX_KEYWORD_NAME_LEN, "%s%d", key_name, (i + 1));
		} else {
			snprintf(str_temp, MAX_KEYWORD_NAME_LEN, "%s", key_name);
		}
		divider_pos = ini_get_string_value("SpecialSet", str_temp, str);
		if (divider_pos <= 0)
			continue;
		index = 0;
		k = 0;
		memset(str_tmp, 0, sizeof(str_tmp));
		for (j = 0; j < divider_pos; j++) {
			if (',' == str[j]) {
				thr_pos = i * rx_num + k;
				if (thr_pos >= node_num) {
					BTL_TEST_ERROR("key:%s %d,dthr_num(%d>=%d) fail",
								   key_name, i, thr_pos, node_num);
					break;
				}
				thr[thr_pos] = (int)(btl_atoi(str_tmp));
				index = 0;
				memset(str_tmp, 0x00, sizeof(str_tmp));
				k++;
			} else {
				if (' ' == str[j])
					continue;
				str_tmp[index] = str[j];
				index++;
			}
		}
	}
}

static int init_node_valid(void)
{
	char str[MAX_KEYWORD_NAME_LEN] = {0};
	int i = 0;
	int j = 0;
	int chy = 0;
	int node_num = 0;
	int cnt = 0;
	struct btl_test *tdata = btl_ftest;

	if (!tdata || !tdata->node_valid || !tdata->node_valid_sc) {
		BTL_TEST_ERROR("tdata/node_valid/node_valid_sc is null");
		return -EINVAL;
	}

	chy = tdata->node.rx_num;
	node_num = tdata->node.node_num;
	btl_init_buffer(tdata->node_valid, 1 , node_num, false, 0, 0);
	if ((tdata->func->hwtype == IC_HW_INCELL) || (tdata->func->hwtype == IC_HW_MC_SC)) {
		for (cnt = 0; cnt < node_num; cnt++) {
			i = cnt / chy + 1;
			j = cnt % chy + 1;
			snprintf(str, MAX_KEYWORD_NAME_LEN, "InvalidNode[%d][%d]", i, j);
			btl_get_keyword_value("INVALID_NODE", str, &tdata->node_valid[cnt]);
		}
	}

	if (tdata->func->hwtype == IC_HW_MC_SC) {
		chy = tdata->sc_node.rx_num;
		node_num = tdata->sc_node.node_num;
		btl_init_buffer(tdata->node_valid_sc, 1, node_num, false, 0, 0);

		for (cnt = 0; cnt < node_num; cnt++) {
			i = (cnt >= chy) ? 2 : 1;
			j = (cnt >= chy) ? (cnt - chy + 1) : (cnt + 1);
			snprintf(str, MAX_KEYWORD_NAME_LEN, "InvalidNodeS[%d][%d]", i, j);
			btl_get_keyword_value("INVALID_NODES", str, &tdata->node_valid_sc[cnt]);
		}
	}

	btl_print_buffer(tdata->node_valid, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(tdata->node_valid_sc, tdata->sc_node.node_num, tdata->sc_node.rx_num);
	return 0;
}

/* incell */
static int get_test_item_incell(void)
{
	int ret = 0;
	char item_name[][MAX_KEYWORD_NAME_LEN] = TEST_ITEM_INCELL;
	int length = sizeof(item_name) / MAX_KEYWORD_NAME_LEN;
	int item_val = 0;

	ret = get_test_item(item_name, length, &item_val);
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("get test item fail\n");
		return ret;
	}

	btl_ftest->ic.incell.u.tmp = item_val;
	return 0;
}

static char bthr_name_incell[][MAX_KEYWORD_NAME_LEN] = BASIC_THRESHOLD_INCELL;
static int get_test_threshold_incell(void)
{
	int ret = 0;
	int length = sizeof(bthr_name_incell) / MAX_KEYWORD_NAME_LEN;
	struct btl_test *tdata = btl_ftest;
	struct incell_threshold *thr = &tdata->ic.incell.thr;
	int node_num = tdata->node.node_num;
	int key_num = tdata->node.key_num;
	bool raw_key_check = thr->basic.rawdata_vkey_check;
	bool cb_key_check = thr->basic.cb_vkey_check;

	tdata->basic_thr_count = sizeof(struct incell_threshold_b) / sizeof(int);
	/* get standard basic threshold */
	ret = get_basic_threshold(bthr_name_incell, length, (int *)&thr->basic);
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("get basic thr fail\n");
		return ret;
	}

	/* basic special set by ic */
	if (tdata->func->param_init) {
		ret = tdata->func->param_init();
		if (ret < 0) {
			BTL_TEST_SAVE_ERR("special basic thr init fail\n");
			return ret;
		}
	}

	/* init buffer */
	btl_init_buffer(thr->rawdata_max, thr->basic.rawdata_max, node_num, raw_key_check, thr->basic.rawdata_max_vk, key_num);
	btl_init_buffer(thr->rawdata_min, thr->basic.rawdata_min, node_num, raw_key_check, thr->basic.rawdata_min_vk, key_num);
	if (tdata->func->rawdata2_support) {
		btl_init_buffer(thr->rawdata2_max, thr->basic.rawdata2_max, node_num, false, 0, 0);
		btl_init_buffer(thr->rawdata2_min, thr->basic.rawdata2_min, node_num, false, 0, 0);
	}
	btl_init_buffer(thr->cb_max, thr->basic.cb_max, node_num, cb_key_check, thr->basic.cb_max_vk, key_num);
	btl_init_buffer(thr->cb_min, thr->basic.cb_min, node_num, cb_key_check, thr->basic.cb_min_vk, key_num);

	/* detail threshold */
	get_detail_threshold("RawData_Max_Tx", true, thr->rawdata_max, node_num);
	get_detail_threshold("RawData_Min_Tx", true, thr->rawdata_min, node_num);
	get_detail_threshold("CB_Max_Tx", true, thr->cb_max, node_num);
	get_detail_threshold("CB_Min_Tx", true, thr->cb_min, node_num);

	return 0;
}

static void print_thr_incell(void)
{
	struct btl_test *tdata = btl_ftest;
	struct incell_threshold *thr = &tdata->ic.incell.thr;

	if (btl_log_level < 3) {
		return;
	}

	BTL_TEST_DBG("short_res_min:%d", thr->basic.short_res_min);
	BTL_TEST_DBG("short_res_vk_min:%d", thr->basic.short_res_vk_min);
	BTL_TEST_DBG("open_cb_min:%d", thr->basic.open_cb_min);
	BTL_TEST_DBG("open_k1_check:%d", thr->basic.open_k1_check);
	BTL_TEST_DBG("open_k1_value:%d", thr->basic.open_k1_value);
	BTL_TEST_DBG("open_k2_check:%d", thr->basic.open_k2_check);
	BTL_TEST_DBG("open_k2_value:%d", thr->basic.open_k2_value);
	BTL_TEST_DBG("cb_min:%d", thr->basic.cb_min);
	BTL_TEST_DBG("cb_max:%d", thr->basic.cb_max);
	BTL_TEST_DBG("cb_vkey_check:%d", thr->basic.cb_vkey_check);
	BTL_TEST_DBG("cb_min_vk:%d", thr->basic.cb_min_vk);
	BTL_TEST_DBG("cb_max_vk:%d", thr->basic.cb_max_vk);
	BTL_TEST_DBG("rawdata_min:%d", thr->basic.rawdata_min);
	BTL_TEST_DBG("rawdata_max:%d", thr->basic.rawdata_max);
	BTL_TEST_DBG("rawdata_vkey_check:%d", thr->basic.rawdata_vkey_check);
	BTL_TEST_DBG("rawdata_min_vk:%d", thr->basic.rawdata_min_vk);
	BTL_TEST_DBG("rawdata_max_vk:%d", thr->basic.rawdata_max_vk);
	BTL_TEST_DBG("lcdnoise_frame:%d", thr->basic.lcdnoise_frame);
	BTL_TEST_DBG("lcdnoise_coefficient:%d", thr->basic.lcdnoise_coefficient);
	BTL_TEST_DBG("lcdnoise_coefficient_vkey:%d", thr->basic.lcdnoise_coefficient_vkey);
	BTL_TEST_DBG("open_diff_min:%d", thr->basic.open_diff_min);

	BTL_TEST_DBG("open_nmos:%d", thr->basic.open_nmos);
	BTL_TEST_DBG("keyshort_k1:%d", thr->basic.keyshort_k1);
	BTL_TEST_DBG("keyshort_cb_max:%d", thr->basic.keyshort_cb_max);
	BTL_TEST_DBG("rawdata2_min:%d", thr->basic.rawdata2_min);
	BTL_TEST_DBG("rawdata2_max:%d", thr->basic.rawdata2_max);
	BTL_TEST_DBG("mux_open_cb_min:%d", thr->basic.mux_open_cb_min);
	BTL_TEST_DBG("open_delta_V:%d", thr->basic.open_delta_V);

	btl_print_buffer(thr->rawdata_min, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->rawdata_max, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->cb_min, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->cb_max, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->rawdata2_min, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->rawdata2_max, tdata->node.node_num, tdata->node.rx_num);
}

static int ini_init_test_incell(void)
{
	int ret = 0;

	ret = get_test_item_incell();
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("get incell test item fail\n");
		return ret;
	}


	ret = get_test_threshold_incell();
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("get incell threshold fail\n");
		return ret;
	}

	print_thr_incell();
	return 0;
}

/* mc_sc */
static int get_test_item_mc_sc(void)
{
	int ret = 0;
	char item_name[][MAX_KEYWORD_NAME_LEN] = TEST_ITEM_MC_SC;
	int length = sizeof(item_name) / MAX_KEYWORD_NAME_LEN;
	int item_val = 0;

	ret = get_test_item(item_name, length, &item_val);
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("get test item fail\n");
		return ret;
	}

	btl_ftest->ic.mc_sc.u.tmp = item_val;
	BTL_TEST_INFO("test item:0x%x in ini", btl_ftest->ic.mc_sc.u.tmp);
	return 0;
}

static char bthr_name_mc_sc[][MAX_KEYWORD_NAME_LEN] = BASIC_THRESHOLD_MC_SC;
static int get_test_threshold_mc_sc(void)
{
	int ret = 0;
	int length = sizeof(bthr_name_mc_sc) / MAX_KEYWORD_NAME_LEN;
	struct btl_test *tdata = btl_ftest;
	struct mc_sc_threshold *thr = &tdata->ic.mc_sc.thr;
	int node_num = tdata->node.node_num;
	int sc_num = tdata->sc_node.node_num;

	tdata->basic_thr_count = sizeof(struct mc_sc_threshold_b) / sizeof(int);
	/* get standard basic threshold */
	ret = get_basic_threshold(bthr_name_mc_sc, length, (int *)&thr->basic);
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("get basic thr fail\n");
		return ret;
	}

	/* basic special set by ic */
	if (tdata->func->param_init) {
		ret = tdata->func->param_init();
		if (ret < 0) {
			BTL_TEST_SAVE_ERR("special basic thr init fail\n");
			return ret;
		}
	}

	/* init buffer */
	btl_init_buffer(thr->rawdata_h_min, thr->basic.rawdata_h_min, node_num, false, 0, 0);
	btl_init_buffer(thr->rawdata_h_max, thr->basic.rawdata_h_max, node_num, false, 0, 0);
	if (tdata->func->rawdata2_support) {
		btl_init_buffer(thr->rawdata_l_min, thr->basic.rawdata_l_min, node_num, false, 0, 0);
		btl_init_buffer(thr->rawdata_l_max, thr->basic.rawdata_l_max, node_num, false, 0, 0);
	}
	btl_init_buffer(thr->tx_linearity_max, thr->basic.uniformity_tx_hole, node_num, false, 0, 0);
	btl_init_buffer(thr->tx_linearity_min, 0, node_num, false, 0, 0);
	btl_init_buffer(thr->rx_linearity_max, thr->basic.uniformity_rx_hole, node_num, false, 0, 0);
	btl_init_buffer(thr->rx_linearity_min, 0, node_num, false, 0, 0);
	btl_init_buffer(thr->scap_cb_off_min, thr->basic.scap_cb_off_min, sc_num, false, 0, 0);
	btl_init_buffer(thr->scap_cb_off_max, thr->basic.scap_cb_off_max, sc_num, false, 0, 0);
	btl_init_buffer(thr->scap_cb_on_min, thr->basic.scap_cb_on_min, sc_num, false, 0, 0);
	btl_init_buffer(thr->scap_cb_on_max, thr->basic.scap_cb_on_max, sc_num, false, 0, 0);
	btl_init_buffer(thr->scap_cb_hi_min, thr->basic.scap_cb_hi_min, sc_num, false, 0, 0);
	btl_init_buffer(thr->scap_cb_hi_max, thr->basic.scap_cb_hi_max, sc_num, false, 0, 0);
	btl_init_buffer(thr->scap_cb_hov_min, thr->basic.scap_cb_hov_min, sc_num, false, 0, 0);
	btl_init_buffer(thr->scap_cb_hov_max, thr->basic.scap_cb_hov_max, sc_num, false, 0, 0);
	btl_init_buffer(thr->scap_rawdata_off_min, thr->basic.scap_rawdata_off_min, sc_num, false, 0, 0);
	btl_init_buffer(thr->scap_rawdata_off_max, thr->basic.scap_rawdata_off_max, sc_num, false, 0, 0);
	btl_init_buffer(thr->scap_rawdata_on_min, thr->basic.scap_rawdata_on_min, sc_num, false, 0, 0);
	btl_init_buffer(thr->scap_rawdata_on_max, thr->basic.scap_rawdata_on_max, sc_num, false, 0, 0);
	btl_init_buffer(thr->scap_rawdata_hi_min, thr->basic.scap_rawdata_hi_min, sc_num, false, 0, 0);
	btl_init_buffer(thr->scap_rawdata_hi_max, thr->basic.scap_rawdata_hi_max, sc_num, false, 0, 0);
	btl_init_buffer(thr->scap_rawdata_hov_min, thr->basic.scap_rawdata_hov_min, sc_num, false, 0, 0);
	btl_init_buffer(thr->scap_rawdata_hov_max, thr->basic.scap_rawdata_hov_max, sc_num, false, 0, 0);
	btl_init_buffer(thr->panel_differ_min, thr->basic.panel_differ_min, node_num, false, 0, 0);
	btl_init_buffer(thr->panel_differ_max, thr->basic.panel_differ_max, node_num, false, 0, 0);
	btl_init_buffer(thr->panel_differ_vol, thr->basic.panel_differ_vol, node_num, false, 0, 0);
	btl_init_buffer(thr->panel_differ_high_freq, thr->basic.panel_differ_high_freq, node_num, false, 0, 0);
	btl_init_buffer(thr->panel_differ_low_freq, thr->basic.panel_differ_low_freq, node_num, false, 0, 0);
	btl_init_buffer(thr->short_cg, thr->basic.short_cg, sc_num, false, 0, 0);
	btl_init_buffer(thr->short_cc, thr->basic.short_cc, sc_num, false, 0, 0);
	btl_init_buffer(thr->rel_rawdata_min, thr->basic.rel_rawdata_min, node_num, false, 0, 0);
	btl_init_buffer(thr->rel_rawdata_max, thr->basic.rel_rawdata_max, node_num, false, 0, 0);
	/* detail threshold */
	get_detail_threshold("RawData_Min_High_Tx", true, thr->rawdata_h_min, node_num);
	get_detail_threshold("RawData_Max_High_Tx", true, thr->rawdata_h_max, node_num);
	if (tdata->func->rawdata2_support) {
		get_detail_threshold("RawData_Min_Low_Tx", true, thr->rawdata_l_min, node_num);
		get_detail_threshold("RawData_Max_Low_Tx", true, thr->rawdata_l_max, node_num);
	}
	get_detail_threshold("Tx_Linearity_Max_Tx", true, thr->tx_linearity_max, node_num);
	get_detail_threshold("Rx_Linearity_Max_Tx", true, thr->rx_linearity_max, node_num);
	get_detail_threshold("ScapCB_OFF_Min_", true, thr->scap_cb_off_min, sc_num);
	get_detail_threshold("ScapCB_OFF_Max_", true, thr->scap_cb_off_max, sc_num);
	get_detail_threshold("ScapCB_ON_Min_", true, thr->scap_cb_on_min, sc_num);
	get_detail_threshold("ScapCB_ON_Max_", true, thr->scap_cb_on_max, sc_num);
	get_detail_threshold("ScapCB_High_Min_", true, thr->scap_cb_hi_min, sc_num);
	get_detail_threshold("ScapCB_High_Max_", true, thr->scap_cb_hi_max, sc_num);
	get_detail_threshold("ScapCB_Hov_Min_", true, thr->scap_cb_hov_min, sc_num);
	get_detail_threshold("ScapCB_Hov_Max_", true, thr->scap_cb_hov_max, sc_num);
	get_detail_threshold("ScapRawData_OFF_Min_", true, thr->scap_rawdata_off_min, sc_num);
	get_detail_threshold("ScapRawData_OFF_Max_", true, thr->scap_rawdata_off_max, sc_num);
	get_detail_threshold("ScapRawData_ON_Min_", false, thr->scap_rawdata_on_min, sc_num);
	get_detail_threshold("ScapRawData_ON_Max_", false, thr->scap_rawdata_on_max, sc_num);
	get_detail_threshold("ScapRawData_High_Min_", true, thr->scap_rawdata_hi_min, sc_num);
	get_detail_threshold("ScapRawData_High_Max_", true, thr->scap_rawdata_hi_max, sc_num);
	get_detail_threshold("ScapRawData_Hov_Min_", true, thr->scap_rawdata_hov_min, sc_num);
	get_detail_threshold("ScapRawData_Hov_Max_", true, thr->scap_rawdata_hov_max, sc_num);
	get_detail_threshold("Panel_Differ_Min_Tx", true, thr->panel_differ_min, node_num);
	get_detail_threshold("Panel_Differ_Max_Tx", true, thr->panel_differ_max, node_num);
	get_detail_threshold("WeakShortTest_Min_", false, thr->short_cg, sc_num);
	get_detail_threshold("WeakShortTest_Max_", false, thr->short_cc, sc_num);
	get_detail_threshold("Rel_RawData_Min_Tx", true, thr->rel_rawdata_min, node_num);
	get_detail_threshold("Rel_RawData_Max_Tx", true, thr->rel_rawdata_max, node_num);

	return 0;
}

static void print_thr_mc_sc(void)
{
	struct btl_test *tdata = btl_ftest;
	struct mc_sc_threshold *thr = &tdata->ic.mc_sc.thr;

	if (btl_log_level < 3) {
		return;
	}

	BTL_TEST_DBG("rawdata_h_min:%d", thr->basic.rawdata_h_min);
	BTL_TEST_DBG("rawdata_h_max:%d", thr->basic.rawdata_h_max);
	BTL_TEST_DBG("rawdata_set_hfreq:%d", thr->basic.rawdata_set_hfreq);
	BTL_TEST_DBG("rawdata_l_min:%d", thr->basic.rawdata_l_min);
	BTL_TEST_DBG("rawdata_l_max:%d", thr->basic.rawdata_l_max);
	BTL_TEST_DBG("rawdata_set_lfreq:%d", thr->basic.rawdata_set_lfreq);
	BTL_TEST_DBG("uniformity_check_tx:%d", thr->basic.uniformity_check_tx);
	BTL_TEST_DBG("uniformity_check_rx:%d", thr->basic.uniformity_check_rx);
	BTL_TEST_DBG("uniformity_check_min_max:%d", thr->basic.uniformity_check_min_max);
	BTL_TEST_DBG("uniformity_tx_hole:%d", thr->basic.uniformity_tx_hole);
	BTL_TEST_DBG("uniformity_rx_hole:%d", thr->basic.uniformity_rx_hole);
	BTL_TEST_DBG("uniformity_min_max_hole:%d", thr->basic.uniformity_min_max_hole);
	BTL_TEST_DBG("scap_cb_off_min:%d", thr->basic.scap_cb_off_min);
	BTL_TEST_DBG("scap_cb_off_max:%d", thr->basic.scap_cb_off_max);
	BTL_TEST_DBG("scap_cb_wp_off_check:%d", thr->basic.scap_cb_wp_off_check);
	BTL_TEST_DBG("scap_cb_on_min:%d", thr->basic.scap_cb_on_min);
	BTL_TEST_DBG("scap_cb_on_max:%d", thr->basic.scap_cb_on_max);
	BTL_TEST_DBG("scap_cb_wp_on_check:%d", thr->basic.scap_cb_wp_on_check);
	BTL_TEST_DBG("scap_rawdata_off_min:%d", thr->basic.scap_rawdata_off_min);
	BTL_TEST_DBG("scap_rawdata_off_max:%d", thr->basic.scap_rawdata_off_max);
	BTL_TEST_DBG("scap_rawdata_wp_off_check:%d", thr->basic.scap_rawdata_wp_off_check);
	BTL_TEST_DBG("scap_rawdata_on_min:%d", thr->basic.scap_rawdata_on_min);
	BTL_TEST_DBG("scap_rawdata_on_max:%d", thr->basic.scap_rawdata_on_max);
	BTL_TEST_DBG("scap_rawdata_wp_on_check:%d", thr->basic.scap_rawdata_wp_on_check);
	BTL_TEST_DBG("short_cg:%d", thr->basic.short_cg);
	BTL_TEST_DBG("short_cc:%d", thr->basic.short_cc);
	BTL_TEST_DBG("panel_differ_min:%d", thr->basic.panel_differ_min);
	BTL_TEST_DBG("panel_differ_max:%d", thr->basic.panel_differ_max);
	BTL_TEST_DBG("panel_differ_vol:%d", thr->basic.panel_differ_vol);

	BTL_TEST_DBG("panel_differ_high_freq:%d", thr->basic.panel_differ_high_freq);
	BTL_TEST_DBG("panel_differ_low_freq:%d", thr->basic.panel_differ_low_freq);
	BTL_TEST_DBG("rel_rawdata_min:%d", thr->basic.rel_rawdata_min);
	BTL_TEST_DBG("rel_rawdata_max:%d", thr->basic.rel_rawdata_max);
	btl_print_buffer(thr->rawdata_h_min, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->rawdata_h_max, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->rawdata_l_min, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->rawdata_l_max, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->tx_linearity_max, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->rx_linearity_max, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->scap_cb_off_min, tdata->sc_node.node_num, tdata->sc_node.rx_num);
	btl_print_buffer(thr->scap_cb_off_max, tdata->sc_node.node_num, tdata->sc_node.rx_num);
	btl_print_buffer(thr->scap_cb_on_min, tdata->sc_node.node_num, tdata->sc_node.rx_num);
	btl_print_buffer(thr->scap_cb_on_max, tdata->sc_node.node_num, tdata->sc_node.rx_num);
	btl_print_buffer(thr->scap_cb_hi_min, tdata->sc_node.node_num, tdata->sc_node.rx_num);
	btl_print_buffer(thr->scap_cb_hi_max, tdata->sc_node.node_num, tdata->sc_node.rx_num);
	btl_print_buffer(thr->scap_cb_hov_min, tdata->sc_node.node_num, tdata->sc_node.rx_num);
	btl_print_buffer(thr->scap_cb_hov_max, tdata->sc_node.node_num, tdata->sc_node.rx_num);
	btl_print_buffer(thr->scap_rawdata_off_min, tdata->sc_node.node_num, tdata->sc_node.rx_num);
	btl_print_buffer(thr->scap_rawdata_off_max, tdata->sc_node.node_num, tdata->sc_node.rx_num);
	btl_print_buffer(thr->scap_rawdata_on_min, tdata->sc_node.node_num, tdata->sc_node.rx_num);
	btl_print_buffer(thr->scap_rawdata_on_max, tdata->sc_node.node_num, tdata->sc_node.rx_num);
	btl_print_buffer(thr->scap_rawdata_hi_min, tdata->sc_node.node_num, tdata->sc_node.rx_num);
	btl_print_buffer(thr->scap_rawdata_hi_max, tdata->sc_node.node_num, tdata->sc_node.rx_num);
	btl_print_buffer(thr->scap_rawdata_hov_min, tdata->sc_node.node_num, tdata->sc_node.rx_num);
	btl_print_buffer(thr->scap_rawdata_hov_max, tdata->sc_node.node_num, tdata->sc_node.rx_num);
	btl_print_buffer(thr->panel_differ_min, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->panel_differ_max, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->panel_differ_vol, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->panel_differ_high_freq, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->panel_differ_low_freq, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->short_cg, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->short_cc, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->rel_rawdata_min, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->rel_rawdata_max, tdata->node.node_num, tdata->node.rx_num);
}

static int ini_init_test_mc_sc(void)
{
	int ret = 0;

	ret = get_test_item_mc_sc();
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("get mc_sc test item fail\n");
		return ret;
	}

	ret = get_test_threshold_mc_sc();
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("get mc_sc threshold fail\n");
		return ret;
	}

	print_thr_mc_sc();
	return 0;
}

/* sc */
static int get_test_item_sc(void)
{
	int ret = 0;
	char item_name[][MAX_KEYWORD_NAME_LEN] = TEST_ITEM_SC;
	int length = sizeof(item_name) / MAX_KEYWORD_NAME_LEN;
	int item_val = 0;

	ret = get_test_item(item_name, length, &item_val);
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("get test item fail\n");
		return ret;
	}

	btl_ftest->ic.sc.u.tmp = item_val;
	return 0;
}

static char bthr_name_sc[][MAX_KEYWORD_NAME_LEN] = BASIC_THRESHOLD_SC;
static int get_test_threshold_sc(void)
{
	int ret = 0;
	int length = sizeof(bthr_name_sc) / MAX_KEYWORD_NAME_LEN;
	struct btl_test *tdata = btl_ftest;
	struct sc_threshold *thr = &tdata->ic.sc.thr;
	int node_num = tdata->node.node_num;

	tdata->basic_thr_count = sizeof(struct sc_threshold_b) / sizeof(int);
	/* get standard basic threshold */
	ret = get_basic_threshold(bthr_name_sc, length, (int *)&thr->basic);
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("get basic thr fail\n");
		return ret;
	}

	/* basic special set by ic */
	if (tdata->func->param_init) {
		ret = tdata->func->param_init();
		if (ret < 0) {
			BTL_TEST_SAVE_ERR("special basic thr init fail\n");
			return ret;
		}
	}

	/* init buffer */
	btl_init_buffer(thr->rawdata_min, thr->basic.rawdata_min, node_num, false, 0, 0);
	btl_init_buffer(thr->rawdata_max, thr->basic.rawdata_max, node_num, false, 0, 0);
	btl_init_buffer(thr->cb_min, thr->basic.cb_min, node_num, false, 0, 0);
	btl_init_buffer(thr->cb_max, thr->basic.cb_max, node_num, false, 0, 0);
	btl_init_buffer(thr->dcb_sort, 0, node_num, false, 0, 0);
	btl_init_buffer(thr->dcb_base, thr->basic.dcb_base, node_num, false, 0, 0);
	btl_init_buffer(thr->rawdata_mode1_min, thr->basic.rawdata_mode1_min, node_num, false, 0, 0);
	btl_init_buffer(thr->rawdata_mode1_max, thr->basic.rawdata_mode1_max, node_num, false, 0, 0); 
	btl_init_buffer(thr->rawdata_mode2_min, thr->basic.rawdata_mode2_min, node_num, false, 0, 0);
	btl_init_buffer(thr->rawdata_mode2_max, thr->basic.rawdata_mode2_max, node_num, false, 0, 0); 
	btl_init_buffer(thr->rawdata_mode3_min, thr->basic.rawdata_mode3_min, node_num, false, 0, 0);
	btl_init_buffer(thr->rawdata_mode3_max, thr->basic.rawdata_mode3_max, node_num, false, 0, 0); 
	btl_init_buffer(thr->rawdata_mode4_min, thr->basic.rawdata_mode4_min, node_num, false, 0, 0);
	btl_init_buffer(thr->rawdata_mode4_max, thr->basic.rawdata_mode4_max, node_num, false, 0, 0); 
	btl_init_buffer(thr->rawdata_mode5_min, thr->basic.rawdata_mode5_min, node_num, false, 0, 0);
	btl_init_buffer(thr->rawdata_mode5_max, thr->basic.rawdata_mode5_max, node_num, false, 0, 0); 
	btl_init_buffer(thr->rawdata_mode6_min, thr->basic.rawdata_mode6_min, node_num, false, 0, 0);
	btl_init_buffer(thr->rawdata_mode6_max, thr->basic.rawdata_mode6_max, node_num, false, 0, 0); 
	btl_init_buffer(thr->cb_mode1_min, thr->basic.cb_mode1_min, node_num, false, 0, 0);
	btl_init_buffer(thr->cb_mode1_max, thr->basic.cb_mode1_max, node_num, false, 0, 0); 
	btl_init_buffer(thr->cb_mode2_min, thr->basic.cb_mode2_min, node_num, false, 0, 0);
	btl_init_buffer(thr->cb_mode2_max, thr->basic.cb_mode2_max, node_num, false, 0, 0); 
	btl_init_buffer(thr->cb_mode3_min, thr->basic.cb_mode3_min, node_num, false, 0, 0);
	btl_init_buffer(thr->cb_mode3_max, thr->basic.cb_mode3_max, node_num, false, 0, 0); 
	btl_init_buffer(thr->cb_mode4_min, thr->basic.cb_mode4_min, node_num, false, 0, 0);
	btl_init_buffer(thr->cb_mode4_max, thr->basic.cb_mode4_max, node_num, false, 0, 0); 
	btl_init_buffer(thr->cb_mode5_min, thr->basic.cb_mode5_min, node_num, false, 0, 0);
	btl_init_buffer(thr->cb_mode5_max, thr->basic.cb_mode5_max, node_num, false, 0, 0); 
	btl_init_buffer(thr->cb_mode6_min, thr->basic.cb_mode6_min, node_num, false, 0, 0);
	btl_init_buffer(thr->cb_mode6_max, thr->basic.cb_mode6_max, node_num, false, 0, 0); 
	btl_init_buffer(thr->short_min, thr->basic.short_min, node_num, false, 0, 0);
	btl_init_buffer(thr->short_max, thr->basic.short_max, node_num, false, 0, 0);	 
	/* detail threshold */
	get_detail_threshold("RawDataTest_Node_Min", false, thr->rawdata_min, node_num);
	get_detail_threshold("RawDataTest_Node_Max", false, thr->rawdata_max, node_num);
	get_detail_threshold("CbTest_Node_Min", false, thr->cb_min, node_num);
	get_detail_threshold("CbTest_Node_Max", false, thr->cb_max, node_num);
	get_detail_threshold("DeltaCxTest_Sort", false, thr->dcb_sort, node_num);
	get_detail_threshold("DeltaCbTest_Base", false, thr->dcb_base, node_num);
	get_detail_threshold("RawDataTest_Mode1_Node_Min", false, thr->rawdata_mode1_min, node_num);
	get_detail_threshold("RawDataTest_Mode1_Node_Max", false, thr->rawdata_mode1_max, node_num);
	get_detail_threshold("RawDataTest_Mode2_Node_Min", false, thr->rawdata_mode2_min, node_num);
	get_detail_threshold("RawDataTest_Mode2_Node_Max", false, thr->rawdata_mode2_max, node_num);
	get_detail_threshold("RawDataTest_Mode3_Node_Min", false, thr->rawdata_mode3_min, node_num);
	get_detail_threshold("RawDataTest_Mode3_Node_Max", false, thr->rawdata_mode3_max, node_num);
	get_detail_threshold("RawDataTest_Mode4_Node_Min", false, thr->rawdata_mode4_min, node_num);
	get_detail_threshold("RawDataTest_Mode4_Node_Max", false, thr->rawdata_mode4_max, node_num);
	get_detail_threshold("RawDataTest_Mode5_Node_Min", false, thr->rawdata_mode5_min, node_num);
	get_detail_threshold("RawDataTest_Mode5_Node_Max", false, thr->rawdata_mode5_max, node_num);
	get_detail_threshold("RawDataTest_Mode6_Node_Min", false, thr->rawdata_mode6_min, node_num);
	get_detail_threshold("RawDataTest_Mode6_Node_Max", false, thr->rawdata_mode6_max, node_num);
	get_detail_threshold("CbTest_Mode1_Base", false, thr->cb_mode1_base, node_num);	
	get_detail_threshold("CbTest_Mode1_Node_Min", false, thr->cb_mode1_min, node_num);
	get_detail_threshold("CbTest_Mode1_Node_Max", false, thr->cb_mode1_max, node_num);
	get_detail_threshold("CbTest_Mode2_Base", false, thr->cb_mode2_base, node_num);	
	get_detail_threshold("CbTest_Mode2_Node_Min", false, thr->cb_mode2_min, node_num);
	get_detail_threshold("CbTest_Mode2_Node_Max", false, thr->cb_mode2_max, node_num);
	get_detail_threshold("CbTest_Mode3_Base", false, thr->cb_mode3_base, node_num);	
	get_detail_threshold("CbTest_Mode3_Node_Min", false, thr->cb_mode3_min, node_num);
	get_detail_threshold("CbTest_Mode3_Node_Max", false, thr->cb_mode3_max, node_num);
	get_detail_threshold("CbTest_Mode4_Base", false, thr->cb_mode4_base, node_num);	
	get_detail_threshold("CbTest_Mode4_Node_Min", false, thr->cb_mode4_min, node_num);
	get_detail_threshold("CbTest_Mode4_Node_Max", false, thr->cb_mode4_max, node_num);
	get_detail_threshold("CbTest_Mode5_Base", false, thr->cb_mode5_base, node_num);	
	get_detail_threshold("CbTest_Mode5_Node_Min", false, thr->cb_mode5_min, node_num);
	get_detail_threshold("CbTest_Mode5_Node_Max", false, thr->cb_mode5_max, node_num);
	get_detail_threshold("CbTest_Mode6_Base", false, thr->cb_mode6_base, node_num);	
	get_detail_threshold("CbTest_Mode6_Node_Min", false, thr->cb_mode6_min, node_num);
	get_detail_threshold("CbTest_Mode6_Node_Max", false, thr->cb_mode6_max, node_num);
	get_detail_threshold("ShortTest_Node_Min", false, thr->short_min, node_num);
	get_detail_threshold("ShortTest_Node_Max", false, thr->short_max, node_num);
	return 0;
}

static void print_thr_sc(void)
{
	struct btl_test *tdata = btl_ftest;
	struct sc_threshold *thr = &tdata->ic.sc.thr;

	if (btl_log_level < 3) {
		return;
	}

	BTL_TEST_DBG("rawdata_min:%d", thr->basic.rawdata_min);
	BTL_TEST_DBG("rawdata_max:%d", thr->basic.rawdata_max);
	BTL_TEST_DBG("cb_min:%d", thr->basic.cb_min);
	BTL_TEST_DBG("cb_max:%d", thr->basic.cb_max);
	BTL_TEST_DBG("dcb_differ_max:%d", thr->basic.dcb_differ_max);
	BTL_TEST_DBG("dcb_key_check:%d", thr->basic.dcb_key_check);
	BTL_TEST_DBG("dcb_key_differ_max:%d", thr->basic.dcb_key_differ_max);
	BTL_TEST_DBG("dcb_ds1:%d", thr->basic.dcb_ds1);
	BTL_TEST_DBG("dcb_ds2:%d", thr->basic.dcb_ds2);
	BTL_TEST_DBG("dcb_ds3:%d", thr->basic.dcb_ds3);
	BTL_TEST_DBG("dcb_ds4:%d", thr->basic.dcb_ds4);
	BTL_TEST_DBG("dcb_ds5:%d", thr->basic.dcb_ds5);
	BTL_TEST_DBG("dcb_ds6:%d", thr->basic.dcb_ds6);
	BTL_TEST_DBG("dcb_critical_check:%d", thr->basic.dcb_critical_check);
	BTL_TEST_DBG("dcb_cs1:%d", thr->basic.dcb_cs1);
	BTL_TEST_DBG("dcb_cs2:%d", thr->basic.dcb_cs2);
	BTL_TEST_DBG("dcb_cs3:%d", thr->basic.dcb_cs3);
	BTL_TEST_DBG("dcb_cs4:%d", thr->basic.dcb_cs4);
	BTL_TEST_DBG("dcb_cs5:%d", thr->basic.dcb_cs5);
	BTL_TEST_DBG("dcb_cs6:%d", thr->basic.dcb_cs6);
	BTL_TEST_DBG("rawdata_mode1_min:%d", thr->basic.rawdata_mode1_min);
	BTL_TEST_DBG("rawdata_mode1_max:%d", thr->basic.rawdata_mode1_max);
	BTL_TEST_DBG("rawdata_mode2_min:%d", thr->basic.rawdata_mode2_min);
	BTL_TEST_DBG("rawdata_mode2_max:%d", thr->basic.rawdata_mode2_max);
	BTL_TEST_DBG("rawdata_mode3_min:%d", thr->basic.rawdata_mode3_min);
	BTL_TEST_DBG("rawdata_mode3_max:%d", thr->basic.rawdata_mode3_max);
	BTL_TEST_DBG("rawdata_mode4_min:%d", thr->basic.rawdata_mode4_min);
	BTL_TEST_DBG("rawdata_mode4_max:%d", thr->basic.rawdata_mode4_max);
	BTL_TEST_DBG("rawdata_mode5_min:%d", thr->basic.rawdata_mode5_min);
	BTL_TEST_DBG("rawdata_mode5_max:%d", thr->basic.rawdata_mode5_max);
	BTL_TEST_DBG("rawdata_mode6_min:%d", thr->basic.rawdata_mode6_min);
	BTL_TEST_DBG("rawdata_mode6_max:%d", thr->basic.rawdata_mode6_max);
	BTL_TEST_DBG("cb_mode1_min:%d", thr->basic.cb_mode1_min);
	BTL_TEST_DBG("cb_mode1_max:%d", thr->basic.cb_mode1_max);
	BTL_TEST_DBG("cb_mode2_min:%d", thr->basic.cb_mode2_min);
	BTL_TEST_DBG("cb_mode2_max:%d", thr->basic.cb_mode2_max);
	BTL_TEST_DBG("cb_mode3_min:%d", thr->basic.cb_mode3_min);
	BTL_TEST_DBG("cb_mode3_max:%d", thr->basic.cb_mode3_max);
	BTL_TEST_DBG("cb_mode4_min:%d", thr->basic.cb_mode4_min);
	BTL_TEST_DBG("cb_mode4_max:%d", thr->basic.cb_mode4_max);
	BTL_TEST_DBG("cb_mode5_min:%d", thr->basic.cb_mode5_min);
	BTL_TEST_DBG("cb_mode5_max:%d", thr->basic.cb_mode5_max);
	BTL_TEST_DBG("cb_mode6_min:%d", thr->basic.cb_mode6_min);
	BTL_TEST_DBG("cb_mode6_max:%d", thr->basic.cb_mode6_max);
	BTL_TEST_DBG("short_min:%d", thr->basic.short_min);
	BTL_TEST_DBG("short_max:%d", thr->basic.short_max);

	btl_print_buffer(thr->rawdata_min, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->rawdata_max, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->cb_min, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->cb_max, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->dcb_sort, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->dcb_base, tdata->node.node_num, tdata->node.rx_num);	
	btl_print_buffer(thr->rawdata_mode1_min, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->rawdata_mode1_max, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->rawdata_mode2_min, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->rawdata_mode2_max, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->rawdata_mode3_min, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->rawdata_mode3_max, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->rawdata_mode4_min, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->rawdata_mode4_max, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->rawdata_mode5_min, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->rawdata_mode5_max, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->rawdata_mode6_min, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->rawdata_mode6_max, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->cb_mode1_base, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->cb_mode1_min, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->cb_mode1_max, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->cb_mode2_base, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->cb_mode2_min, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->cb_mode2_max, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->cb_mode3_base, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->cb_mode3_min, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->cb_mode3_max, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->cb_mode4_base, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->cb_mode4_min, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->cb_mode4_max, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->cb_mode5_base, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->cb_mode5_min, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->cb_mode5_max, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->cb_mode6_base, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->cb_mode6_min, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->cb_mode6_max, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->short_min, tdata->node.node_num, tdata->node.rx_num);
	btl_print_buffer(thr->short_max, tdata->node.node_num, tdata->node.rx_num);
}

static int ini_init_test_sc(void)
{
	int ret = 0;

	ret = get_test_item_sc();
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("get sc test item fail\n");
		return ret;
	}

	ret = get_test_threshold_sc();
	if (ret < 0) {
		BTL_TEST_SAVE_ERR("get sc threshold fail\n");
		return ret;
	}

	print_thr_sc();
	return 0;
}

static u32 ini_get_ic_code(char *ic_name)
{
	int i = 0;
	int type_size = 0;
	int ini_icname_len = 0;
	int ic_types_len = 0;

	ini_icname_len = strlen(ic_name);
	type_size = sizeof(btl_ic_types) / sizeof(btl_ic_types[0]);
	for (i = 0; i < type_size; i++) {
		ic_types_len = strlen(ic_name);
		if (ini_icname_len == ic_types_len) {
			if (0 == strncmp(ic_name, btl_ic_types[i].ic_name, ic_types_len))
				return btl_ic_types[i].ic_type;
		}
	}

	BTL_TEST_ERROR("no IC type match");
	return 0;
}


static void ini_init_interface(struct ini_data *ini)
{
	char str[MAX_KEYWORD_VALUE_LEN] = { 0 };
	u32 value = 0;
	struct btl_test *tdata = btl_ftest;

	/* IC type */
	ini_get_string_value("Interface", "IC_Type", str);
	snprintf(ini->ic_name, MAX_IC_NAME_LEN, "%s", str);

	value = ini_get_ic_code(str);
	ini->ic_code = value;
	BTL_TEST_INFO("ic name:%s, ic code:%x", ini->ic_name, ini->ic_code);

	if (IC_HW_MC_SC == tdata->func->hwtype) {
		get_value_interface("Normalize_Type", &value);
		tdata->normalize = (u8)value;
		BTL_TEST_DBG("normalize:%d", tdata->normalize);
	}
}

static int ini_init_test(struct ini_data *ini)
{
	int ret = 0;
	struct btl_test *tdata = btl_ftest;

	/* interface init */
	ini_init_interface(ini);

	/* node valid */
	ret = init_node_valid();
	if (ret < 0) {
		BTL_TEST_ERROR("init node valid fail");
		return ret;
	}

	switch (tdata->func->hwtype) {
	case IC_HW_INCELL:
		ret = ini_init_test_incell();
		break;
	case IC_HW_MC_SC:
		ret = ini_init_test_mc_sc();
		break;
	case IC_HW_SC:
		ret = ini_init_test_sc();
		break;
	default:
		BTL_TEST_SAVE_ERR("test ic type(%d) fail\n", tdata->func->hwtype);
		ret = -EINVAL;
		break;
	}

	return ret;
}

/*
 * btl_test_get_testparam_from_ini - get test parameters from ini
 *
 * read, parse the configuration file, initialize the test variable
 *
 * return 0 if succuss, else errro code
 */
int btl_test_get_testparam_from_ini(char *config_name)
{
	int ret = 0;
	struct ini_data *ini = &btl_ftest->ini;

	ret = btl_test_get_ini_via_request_firmware(ini, config_name);
	if (ret != 0) {
		BTL_TEST_ERROR("get ini(default) fail");
		goto get_ini_err;
	}

	ini->keyword_num_total = 0;
	ini->section_num = 0;

	ini->tmp = vmalloc(sizeof(struct ini_keyword) * MAX_KEYWORD_NUM);
	if (NULL == ini->tmp) {
		BTL_TEST_ERROR("malloc memory for ini tmp fail");
		ret = -ENOMEM;
		goto get_ini_err;
	}
	memset(ini->tmp, 0, sizeof(struct ini_keyword) * MAX_KEYWORD_NUM);

	/* parse ini data to get keyword name&value */
	ret = ini_init_inidata(ini);
	if (ret < 0) {
		BTL_TEST_ERROR("ini_init_inidata fail");
		goto get_ini_err;
	}

	/* parse threshold & test item */
	ret = ini_init_test(ini);
	if (ret < 0) {
		BTL_TEST_ERROR("ini init fail");
		goto get_ini_err;
	}

	ret = 0;
get_ini_err:
	if (ini->tmp) {
		vfree(ini->tmp);
		ini->tmp = NULL;
	}

	if (ini->data) {
		vfree(ini->data);
		ini->data = NULL;
	}

	return ret;
}
#endif
