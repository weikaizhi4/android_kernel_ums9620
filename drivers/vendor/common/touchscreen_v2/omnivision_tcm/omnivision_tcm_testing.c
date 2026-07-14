/*
 * Omnivision TCM touchscreen driver
 *
 * Copyright (C) 2017-2018 Omnivision Incorporated. All rights reserved.

 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * INFORMATION CONTAINED IN THIS DOCUMENT IS PROVIDED "AS-IS," AND Omnivision
 * EXPRESSLY DISCLAIMS ALL EXPRESS AND IMPLIED WARRANTIES, INCLUDING ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE,
 * AND ANY WARRANTIES OF NON-INFRINGEMENT OF ANY INTELLECTUAL PROPERTY RIGHTS.
 * IN NO EVENT SHALL Omnivision BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, PUNITIVE, OR CONSEQUENTIAL DAMAGES ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OF THE INFORMATION CONTAINED IN THIS DOCUMENT, HOWEVER CAUSED
 * AND BASED ON ANY THEORY OF LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, AND EVEN IF Omnivision WAS ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE. IF A TRIBUNAL OF COMPETENT JURISDICTION DOES
 * NOT PERMIT THE DISCLAIMER OF DIRECT DAMAGES OR ANY OTHER DAMAGES, Omnivision'
 * TOTAL CUMULATIVE LIABILITY TO ANY PARTY SHALL NOT EXCEED ONE HUNDRED U.S.
 * DOLLARS.
 */

#include <linux/gpio.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/err.h>
#include "omnivision_tcm_core.h"
#include "omnivision_tcm_testing.h"

#define MAX_PATH_LEN_100	100
extern char ovt_tcm_save_file_path[];

#define DYNAMIC_RANGE_TEST_DATA_FILE	"dynamic_range_test.csv"
#define NOISE_TEST_DATA_FILE			"notise_test.csv"
#define OPEN_DETECTION_TEST_DATA_FILE	"open_detection_test.csv"

#define SYSFS_DIR_NAME "testing"

#define REPORT_TIMEOUT_MS 5000

#define testing_sysfs_show(t_name) \
static ssize_t testing_sysfs_##t_name##_show(struct device *dev, \
		struct device_attribute *attr, char *buf) \
{ \
	int retval; \
	struct ovt_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd; \
\
	mutex_lock(&tcm_hcd->extif_mutex); \
\
	retval = testing_##t_name(); \
	if (retval < 0) { \
		ovt_info(ERR_LOG, "Failed to do "#t_name" test\n"); \
		goto exit; \
	} \
\
	retval = snprintf(buf, PAGE_SIZE, \
			"%s\n", \
			testing_hcd->result ? "Passed" : "Failed"); \
\
exit: \
	mutex_unlock(&tcm_hcd->extif_mutex); \
\
	return retval; \
}

#define CHECK_BIT(var, pos) ((var) & (1<<(pos)))

DECLARE_COMPLETION(report_complete);

DECLARE_COMPLETION(testing_remove_complete);

struct testing_hcd *testing_hcd;


/* testing implementation */
int testing_device_id(void);

int testing_config_id(void);

int testing_reset_open(void);

int testing_pt01_trx_trx_short(void);

int testing_pt05_full_raw(void);

int testing_pt07_dynamic_range(void);

int testing_pt10_noise(void);

int testing_pt11_open_detection(void);

SHOW_PROTOTYPE(testing, size)

/* nodes for testing */
SHOW_PROTOTYPE(testing, device_id)
SHOW_PROTOTYPE(testing, config_id)
SHOW_PROTOTYPE(testing, reset_open)
SHOW_PROTOTYPE(testing, pt01_trx_trx_short)
SHOW_PROTOTYPE(testing, pt05_full_raw)
SHOW_PROTOTYPE(testing, pt07_dynamic_range)
SHOW_PROTOTYPE(testing, pt10_noise)
SHOW_PROTOTYPE(testing, pt11_open_detection)


static struct device_attribute *attrs[] = {
	ATTRIFY(size),
	ATTRIFY(device_id),
	ATTRIFY(config_id),
	ATTRIFY(pt01_trx_trx_short),
	ATTRIFY(pt05_full_raw),
	ATTRIFY(pt07_dynamic_range),
	ATTRIFY(pt10_noise),
	ATTRIFY(pt11_open_detection),
	ATTRIFY(reset_open),
};

static ssize_t testing_sysfs_data_show(struct file *data_file,
		struct kobject *kobj, struct bin_attribute *attributes,
		char *buf, loff_t pos, size_t count);

static struct bin_attribute bin_attr = {
	.attr = {
		.name = "data",
		.mode = S_IRUGO,
	},
	.size = 0,
	.read = testing_sysfs_data_show,
};

testing_sysfs_show(device_id)

testing_sysfs_show(config_id)

testing_sysfs_show(pt01_trx_trx_short)

testing_sysfs_show(pt05_full_raw)

testing_sysfs_show(pt07_dynamic_range)

testing_sysfs_show(pt10_noise)

testing_sysfs_show(pt11_open_detection)

testing_sysfs_show(reset_open)


static ssize_t testing_sysfs_size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int retval;
	struct ovt_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);
	mutex_lock(&tcm_hcd->extif_mutex);

	LOCK_BUFFER(testing_hcd->output);

	retval = snprintf(buf, PAGE_SIZE,
			"%u\n",
			testing_hcd->output.data_length);

	UNLOCK_BUFFER(testing_hcd->output);

	mutex_unlock(&tcm_hcd->extif_mutex);
	ovt_info(DEBUG_LOG, "%s exit!\n", __func__);
	return retval;
}

static ssize_t testing_sysfs_data_show(struct file *data_file,
		struct kobject *kobj, struct bin_attribute *attributes,
		char *buf, loff_t pos, size_t count)
{
	int retval;
	unsigned int readlen;
	struct ovt_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);
	mutex_lock(&tcm_hcd->extif_mutex);

	LOCK_BUFFER(testing_hcd->output);

	readlen = MIN(count, testing_hcd->output.data_length - pos);

	retval = secure_memcpy(buf,
			count,
			&testing_hcd->output.buf[pos],
			testing_hcd->output.buf_size - pos,
			readlen);
	if (retval < 0) {
		ovt_info(ERR_LOG, "Failed to copy report data\n");
	} else {
		retval = readlen;
	}

	UNLOCK_BUFFER(testing_hcd->output);

	mutex_unlock(&tcm_hcd->extif_mutex);
	ovt_info(DEBUG_LOG, "%s exit!\n", __func__);
	return retval;
}

int testing_run_prod_test_item(enum test_code test_code)
{
	int retval;
	struct ovt_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);
	if (tcm_hcd->features.dual_firmware &&
			tcm_hcd->id_info.mode != MODE_PRODUCTIONTEST_FIRMWARE) {
		retval = tcm_hcd->switch_mode(tcm_hcd, FW_MODE_PRODUCTION_TEST);
		if (retval < 0) {
			ovt_info(ERR_LOG, "Failed to run production test firmware\n");
			return retval;
		}
	} else if (IS_NOT_FW_MODE(tcm_hcd->id_info.mode) ||
			tcm_hcd->app_status != APP_STATUS_OK) {
		ovt_info(ERR_LOG, "Identifying mode = 0x%02x\n",
			tcm_hcd->id_info.mode);
		return -ENODEV;
	}

	LOCK_BUFFER(testing_hcd->out);

	retval = ovt_tcm_alloc_mem(tcm_hcd,
			&testing_hcd->out,
			1);
	if (retval < 0) {
		ovt_info(ERR_LOG, "Failed to allocate memory for testing_hcd->out.buf\n");
		UNLOCK_BUFFER(testing_hcd->out);
		return retval;
	}

	testing_hcd->out.buf[0] = test_code;

	LOCK_BUFFER(testing_hcd->resp);

	retval = tcm_hcd->write_message(tcm_hcd,
			CMD_PRODUCTION_TEST,
			testing_hcd->out.buf,
			1,
			&testing_hcd->resp.buf,
			&testing_hcd->resp.buf_size,
			&testing_hcd->resp.data_length,
			NULL,
			0);
	if (retval < 0) {
		ovt_info(ERR_LOG, "Failed to write command %s\n",
				STR(CMD_PRODUCTION_TEST));
		UNLOCK_BUFFER(testing_hcd->resp);
		UNLOCK_BUFFER(testing_hcd->out);
		return retval;
	}

	UNLOCK_BUFFER(testing_hcd->resp);
	UNLOCK_BUFFER(testing_hcd->out);
	ovt_info(DEBUG_LOG, "%s exit!\n", __func__);
	return 0;
}

static int testing_collect_reports(enum report_type report_type,
		unsigned int num_of_reports)
{
	int retval;
	bool completed;
	unsigned int timeout;
	struct ovt_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);
	testing_hcd->report_index = 0;
	testing_hcd->report_type = report_type;
	testing_hcd->num_of_reports = num_of_reports;

#if (KERNEL_VERSION(3, 13, 0) <= LINUX_VERSION_CODE)
	reinit_completion(&report_complete);
#else
	INIT_COMPLETION(report_complete);
#endif

	LOCK_BUFFER(testing_hcd->out);

	retval = ovt_tcm_alloc_mem(tcm_hcd,
			&testing_hcd->out,
			1);
	if (retval < 0) {
		ovt_info(ERR_LOG, "Failed to allocate memory for testing_hcd->out.buf\n");
		UNLOCK_BUFFER(testing_hcd->out);
		goto exit;
	}

	testing_hcd->out.buf[0] = testing_hcd->report_type;

	LOCK_BUFFER(testing_hcd->resp);

	retval = tcm_hcd->write_message(tcm_hcd,
			CMD_ENABLE_REPORT,
			testing_hcd->out.buf,
			1,
			&testing_hcd->resp.buf,
			&testing_hcd->resp.buf_size,
			&testing_hcd->resp.data_length,
			NULL,
			0);
	if (retval < 0) {
		ovt_info(ERR_LOG, "Failed to write command %s\n",
				STR(CMD_ENABLE_REPORT));
		UNLOCK_BUFFER(testing_hcd->resp);
		UNLOCK_BUFFER(testing_hcd->out);
		goto exit;
	}

	UNLOCK_BUFFER(testing_hcd->resp);
	UNLOCK_BUFFER(testing_hcd->out);

	completed = false;
	timeout = REPORT_TIMEOUT_MS * num_of_reports;

	retval = wait_for_completion_timeout(&report_complete,
			msecs_to_jiffies(timeout));
	if (retval == 0) {
		ovt_info(ERR_LOG, "Timed out waiting for report collection\n");
	} else {
		completed = true;
	}

	LOCK_BUFFER(testing_hcd->out);

	testing_hcd->out.buf[0] = testing_hcd->report_type;

	LOCK_BUFFER(testing_hcd->resp);

	retval = tcm_hcd->write_message(tcm_hcd,
			CMD_DISABLE_REPORT,
			testing_hcd->out.buf,
			1,
			&testing_hcd->resp.buf,
			&testing_hcd->resp.buf_size,
			&testing_hcd->resp.data_length,
			NULL,
			0);
	if (retval < 0) {
		ovt_info(ERR_LOG, "Failed to write command %s\n",
				STR(CMD_DISABLE_REPORT));
		UNLOCK_BUFFER(testing_hcd->resp);
		UNLOCK_BUFFER(testing_hcd->out);
		goto exit;
	}

	UNLOCK_BUFFER(testing_hcd->resp);
	UNLOCK_BUFFER(testing_hcd->out);

	if (completed)
		retval = 0;
	else
		retval = -EIO;

exit:
	testing_hcd->report_type = 0;
	ovt_info(DEBUG_LOG, "%s exit!\n", __func__);
	return retval;
}

void testing_get_frame_size_words(unsigned int *size, bool image_only)
{
	unsigned int rows;
	unsigned int cols;
	unsigned int hybrid;
	unsigned int buttons;
	struct ovt_tcm_app_info *app_info;
	struct ovt_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);
	app_info = &tcm_hcd->app_info;

	rows = le2_to_uint(app_info->num_of_image_rows);
	cols = le2_to_uint(app_info->num_of_image_cols);
	hybrid = le2_to_uint(app_info->has_hybrid_data);
	buttons = le2_to_uint(app_info->num_of_buttons);

	*size = rows * cols;

	if (!image_only) {
		if (hybrid)
			*size += rows + cols;
		*size += buttons;
	}
	ovt_info(DEBUG_LOG, "%s exit!\n", __func__);
}

void testing_standard_frame_output(bool image_only)
{
	int retval;
	unsigned int data_size;
	unsigned int header_size;
	unsigned int output_size;
	struct ovt_tcm_app_info *app_info;
	struct ovt_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);
	app_info = &tcm_hcd->app_info;

	testing_get_frame_size_words(&data_size, image_only);

	header_size = sizeof(app_info->num_of_buttons) +
			sizeof(app_info->num_of_image_rows) +
			sizeof(app_info->num_of_image_cols) +
			sizeof(app_info->has_hybrid_data);

	output_size = header_size + data_size * 2;

	LOCK_BUFFER(testing_hcd->output);

	retval = ovt_tcm_alloc_mem(tcm_hcd,
			&testing_hcd->output,
			output_size);
	if (retval < 0) {
		ovt_info(ERR_LOG, "Failed to allocate memory for testing_hcd->output.buf\n");
		UNLOCK_BUFFER(testing_hcd->output);
		return;
	}

	retval = secure_memcpy(testing_hcd->output.buf,
			testing_hcd->output.buf_size,
			&app_info->num_of_buttons[0],
			header_size,
			header_size);
	if (retval < 0) {
		ovt_info(ERR_LOG, "Failed to copy header data\n");
		UNLOCK_BUFFER(testing_hcd->output);
		return;
	} else {
		ovt_info(INFO_LOG, "Success to copy header data\n");
	}

	output_size = header_size;

	LOCK_BUFFER(testing_hcd->resp);

	retval = secure_memcpy(testing_hcd->output.buf + header_size,
			testing_hcd->output.buf_size - header_size,
			testing_hcd->resp.buf,
			testing_hcd->resp.buf_size,
			testing_hcd->resp.data_length);
	if (retval < 0) {
		ovt_info(ERR_LOG, "Failed to copy test data\n");
		UNLOCK_BUFFER(testing_hcd->resp);
		UNLOCK_BUFFER(testing_hcd->output);
		return;
	} else {
		ovt_info(INFO_LOG, "Success to copy test data\n");
	}

	output_size += testing_hcd->resp.data_length;

	UNLOCK_BUFFER(testing_hcd->resp);

	testing_hcd->output.data_length = output_size;

	UNLOCK_BUFFER(testing_hcd->output);
	ovt_info(DEBUG_LOG, "%s exit!\n", __func__);
}

int testing_device_id(void)
{
	struct ovt_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	struct ovt_tcm_identification *id_info;
	char *strptr = NULL;

	ovt_info(INFO_LOG, "Start testing_device_id\n");
	testing_hcd->result = false;

	id_info = &tcm_hcd->id_info;

	strptr = strnstr(id_info->part_number, device_id_limit, sizeof(id_info->part_number));
	if (strptr != NULL)
		testing_hcd->result = true;
	else
		ovt_info(ERR_LOG, "Device ID is mismatching, FW: %s (%s)\n",
				id_info->part_number, device_id_limit);

	ovt_info(INFO_LOG, "testing_device_id result = %s\n", (testing_hcd->result)?"pass":"fail");
	return 0;
}

int testing_config_id(void)
{
	struct ovt_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	struct ovt_tcm_app_info *app_info;
	int i;

	ovt_info(INFO_LOG, "Start testing_config_id\n");
	testing_hcd->result = false;

	app_info = &tcm_hcd->app_info;

	testing_hcd->result = true;
	for (i = 0; i < sizeof(config_id_limit); i++) {
		if (config_id_limit[i] !=
				tcm_hcd->app_info.customer_config_id[i]) {
			ovt_info(ERR_LOG, "Config ID is mismatching at byte %d\n", i);
			testing_hcd->result = false;
		}
	}

	ovt_info(INFO_LOG, "testing_config_id result = %s\n", (testing_hcd->result)?"pass":"fail");
	return 0;
}

#ifdef PT1_GET_PIN_ASSIGNMENT

static int testing_get_static_config(unsigned char *buf, unsigned int buf_len)
{
	int retval;
	struct ovt_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);
	if (!buf) {
		ovt_info(ERR_LOG, "invalid parameter\n");
		return -EINVAL;
	}

	LOCK_BUFFER(testing_hcd->out);
	LOCK_BUFFER(testing_hcd->resp);

	retval = tcm_hcd->write_message(tcm_hcd,
					CMD_GET_STATIC_CONFIG,
					NULL,
					0,
					&testing_hcd->resp.buf,
					&testing_hcd->resp.buf_size,
					&testing_hcd->resp.data_length,
					NULL,
					0);
	if (retval < 0) {
		ovt_info(ERR_LOG, "Failed to write command %s\n",
				STR(CMD_GET_STATIC_CONFIG));
		goto exit;
	}

	if (testing_hcd->resp.data_length != buf_len) {
		ovt_info(ERR_LOG, "Cfg size mismatch\n");
		retval = -EINVAL;
		goto exit;
	} else {
		ovt_info(INFO_LOG, "Cfg size match\n");
	}

	retval = secure_memcpy(buf,
				buf_len,
				testing_hcd->resp.buf,
				testing_hcd->resp.buf_size,
				buf_len);
	if (retval < 0) {
		ovt_info(ERR_LOG, "Failed to copy cfg data\n");
		goto exit;
	} else {
		ovt_info(INFO_LOG, "Success to copy cfg data\n");
	}

exit:
	UNLOCK_BUFFER(testing_hcd->resp);
	UNLOCK_BUFFER(testing_hcd->out);

	return retval;
}

static bool testing_is_pins_assigned(int pin)
{
	int i;
	short *tx_pins = testing_hcd->tx_pins;
	short tx_assigned = testing_hcd->tx_assigned;
	short *rx_pins = testing_hcd->rx_pins;
	short rx_assigned = testing_hcd->rx_assigned;
	short *guard_pins = testing_hcd->guard_pins;
	short guard_assigned = testing_hcd->guard_assigned;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);
	for (i = 0; i < tx_assigned; i++) {
		if (pin == tx_pins[i])
			return true;
	}
	for (i = 0; i < rx_assigned; i++) {
		if (pin == rx_pins[i])
			return true;
	}
	for (i = 0; i < guard_assigned; i++) {
		if (pin == guard_pins[i])
			return true;
	}

	return false;
}

static int testing_get_pins_mapping(unsigned char *cfg_data, int length)
{
	struct ovt_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	int i, j = 0;
	int idx;

	int offset_tx_pin = cfg_imagetxes.offset/8;
	int length_tx_pin = cfg_imagetxes.length/8;
	int offset_rx_pin = cfg_imagerxes.offset/8;
	int length_rx_pin = cfg_imagerxes.length/8;

	int num_tx_guard = 0;
	int offset_num_tx_guard = cfg_numtxguards.offset/8;
	int length_num_tx_guard = cfg_numtxguards.length/8;
	int offset_tx_guard = cfg_txguardpins.offset/8;
	int length_tx_guard = cfg_txguardpins.length/8;

	int num_rx_guard = 0;
	int offset_num_rx_guard = cfg_numrxguards.offset/8;
	int length_num_rx_guard = cfg_numrxguards.length/8;
	int offset_rx_guard = cfg_rxguardpins.offset/8;
	int length_rx_guard = cfg_rxguardpins.length/8;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);
	if (!cfg_data) {
		ovt_info(ERR_LOG, "invalid parameter\n");
		return -EINVAL;
	}

	testing_hcd->tx_assigned = 0;
	testing_hcd->rx_assigned = 0;
	testing_hcd->guard_assigned = 0;

	/* get tx pins mapping */
	if (0 == (offset_tx_pin + length_tx_pin))
		goto get_rx_pins;

	if (length > offset_tx_pin + length_tx_pin) {

		testing_hcd->tx_assigned = (length_tx_pin/2);

		idx = 0;
		for (i = 0; i < (length_tx_pin/2); i++) {
			testing_hcd->tx_pins[i] =
				(short)cfg_data[offset_tx_pin + idx] |
				(short)(cfg_data[offset_tx_pin + idx + 1] << 8);
			idx += 2;

			ovt_info(INFO_LOG, "tx[%d] = %2d\n", i, testing_hcd->tx_pins[i]);
		}
	}

get_rx_pins:
	/* get rx pins mapping */
	if (0 == (offset_rx_pin + length_rx_pin))
		goto get_num_tx_guards;

	if (length > offset_rx_pin + length_rx_pin) {

		testing_hcd->rx_assigned = (length_rx_pin/2);

		idx = 0;
		for (i = 0; i < (length_rx_pin/2); i++) {
			testing_hcd->rx_pins[i] =
				(short)cfg_data[offset_rx_pin + idx] |
				(short)(cfg_data[offset_rx_pin + idx + 1] << 8);
			idx += 2;

			ovt_info(INFO_LOG, "rx[%d] = %2d\n", i, testing_hcd->rx_pins[i]);
		}
	}

get_num_tx_guards:
	/* get number of tx guards */
	if (0 == (offset_num_tx_guard + length_num_tx_guard))
		goto get_num_rx_guards;

	if (length > offset_num_tx_guard + length_num_tx_guard) {

		num_tx_guard = (short)cfg_data[offset_num_tx_guard] |
				(short)(cfg_data[offset_num_tx_guard + 1] << 8);

		testing_hcd->guard_assigned += num_tx_guard;
	}

get_num_rx_guards:
	/* get number of rx guards */
	if (0 == (offset_num_rx_guard + length_num_rx_guard))
		goto get_guards;

	if (length > offset_num_rx_guard + length_num_rx_guard) {

		num_rx_guard = (short)cfg_data[offset_num_rx_guard] |
				(short)(cfg_data[offset_num_rx_guard + 1] << 8);

		testing_hcd->guard_assigned += num_rx_guard;
	}

get_guards:
	if (testing_hcd->guard_assigned > 0)
		ovt_info(INFO_LOG, "num of guards = %2d\n", testing_hcd->guard_assigned);

	/* get tx guards mapping */
	if ((num_tx_guard > 0) &&
		(length > offset_tx_guard + length_tx_guard)) {
		idx = 0;
		for (i = 0; i < num_tx_guard; i++) {
			testing_hcd->guard_pins[j] =
			  (short)cfg_data[offset_tx_guard + idx] |
			  (short)(cfg_data[offset_tx_guard + idx + 1] << 8);

			ovt_info(INFO_LOG, "guard_pins[%d] = %2d\n", i, testing_hcd->guard_pins[j]);
			idx += 2;
			j += 1;
		}
	}

	/* get rx guards mapping */
	if ((num_rx_guard > 0) &&
		(length > offset_rx_guard + length_rx_guard)) {
		idx = 0;
		for (i = 0; i < num_rx_guard; i++) {
			testing_hcd->guard_pins[j] =
			  (short)cfg_data[offset_rx_guard + idx] |
			  (short)(cfg_data[offset_rx_guard + idx + 1] << 8);

			ovt_info(INFO_LOG, "guard_pins[%d] = %2d\n", i, testing_hcd->guard_pins[j]);
			idx += 2;
			j += 1;
		}
	}

	return 0;
}

#endif /* end of PT1_GET_PIN_ASSIGNMENT */

int testing_pt01_trx_trx_short(void)
{
	int retval;
	int i, j;
	int phy_pin;
	bool do_pin_test = false;
	struct ovt_tcm_app_info *app_info;
	struct ovt_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	unsigned int size;
	unsigned char limit;
	unsigned char *buf;
	unsigned char data;
#ifdef PT1_GET_PIN_ASSIGNMENT
	unsigned char *satic_cfg_buf = NULL;
	unsigned int satic_cfg_length;
#endif

	ovt_info(INFO_LOG, "Start testing_pt01_trx_trx_short\n");
	testing_hcd->result = false;

	app_info = &tcm_hcd->app_info;

#ifdef PT1_GET_PIN_ASSIGNMENT
	satic_cfg_length = le2_to_uint(app_info->static_config_size);

	if (!testing_hcd->satic_cfg_buf) {
		satic_cfg_buf = kzalloc(satic_cfg_length, GFP_KERNEL);
		if (!satic_cfg_buf) {
			ovt_info(ERR_LOG, "Failed to allocate memory for satic_cfg_buf\n");
			goto exit;
		}

		retval = testing_get_static_config(satic_cfg_buf,
						satic_cfg_length);
		if (retval < 0) {
			ovt_info(ERR_LOG, "Failed to get static config\n");
			goto exit;
		} else {
			ovt_info(INFO_LOG, "Success to get static config\n");
		}

		testing_hcd->satic_cfg_buf = satic_cfg_buf;
	}

	if ((testing_hcd->tx_assigned <= 0) ||
		(testing_hcd->rx_assigned <= 0) ||
		(testing_hcd->guard_assigned <= 0)) {

		if (!testing_hcd->satic_cfg_buf) {
			ovt_info(ERR_LOG, "Failed to get proper satic_cfg\n");
			goto exit;
		} else {
			ovt_info(INFO_LOG, "Success to get proper satic_cfg\n");
		}

		retval = testing_get_pins_mapping(testing_hcd->satic_cfg_buf,
							satic_cfg_length);
		if (retval < 0) {
			ovt_info(ERR_LOG, "Failed to get pins mapping\n");
			goto exit;
		} else {
			ovt_info(INFO_LOG, "Success to get pins mapping\n");
		}
	}
#endif

	retval = testing_run_prod_test_item(TEST_PT1_TRX_TRX_SHORTS);
	if (retval < 0) {
		ovt_info(ERR_LOG, "Failed to run test\n");
		return -EIO;
	}

	LOCK_BUFFER(testing_hcd->resp);

	size = ARRAY_SIZE(pt1_limits);

	if (size < testing_hcd->resp.data_length) {
		ovt_info(ERR_LOG, "Mismatching limits data\n");
		UNLOCK_BUFFER(testing_hcd->resp);
		retval = -EINVAL;
		goto exit;
	} else {
		ovt_info(INFO_LOG, "Matching limits data\n");
	}

	buf = testing_hcd->resp.buf;
	testing_hcd->result = true;

	for (i = 0; i < testing_hcd->resp.data_length; i++) {

		data = buf[i];
		ovt_info(INFO_LOG, "[%d]: 0x%02x\n", i, data);
		for (j = 0; j < 8; j++) {

			phy_pin = (i*8 + j);

#ifdef PT1_GET_PIN_ASSIGNMENT
			do_pin_test = testing_is_pins_assigned(phy_pin);
#else
			do_pin_test = true;
#endif
			limit = CHECK_BIT(pt1_limits[i], j);
			if (do_pin_test) {
				if (CHECK_BIT(data, j) != limit) {
					ovt_info(ERR_LOG, "pin-%2d : fail\n", phy_pin);
					testing_hcd->result = false;
				} else
					ovt_info(INFO_LOG, "pin-%2d : pass\n", phy_pin);
			}
		}
	}

	UNLOCK_BUFFER(testing_hcd->resp);

exit:
#ifdef PT1_GET_PIN_ASSIGNMENT
	kfree(satic_cfg_buf);
#endif

	if (tcm_hcd->features.dual_firmware) {
		if (tcm_hcd->reset(tcm_hcd) < 0) {
			ovt_info(ERR_LOG, "Failed to do reset\n");
		}
	}

	ovt_info(INFO_LOG, "testing_pt01_trx_trx_short result = %s\n", (testing_hcd->result)?"pass":"fail");
	return retval;
}

int testing_pt05_full_raw(void)
{
	int retval;
	unsigned char *buf;
	unsigned int idx;
	unsigned int row;
	unsigned int col;
	unsigned int rows;
	unsigned int cols;
	unsigned int limits_rows;
	unsigned int limits_cols;
	unsigned int frame_size;
	struct ovt_tcm_app_info *app_info;
	struct ovt_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	unsigned short data;

	ovt_info(INFO_LOG, "Start testing_pt05_full_raw\n");
	testing_hcd->result = false;

	app_info = &tcm_hcd->app_info;

	rows = le2_to_uint(app_info->num_of_image_rows);
	cols = le2_to_uint(app_info->num_of_image_cols);

	frame_size = rows * cols * 2;

	retval = testing_run_prod_test_item(TEST_PT5_FULL_RAW_CAP);
	if (retval < 0) {
		ovt_info(ERR_LOG, "Failed to run test\n");
		goto exit;
	}

	LOCK_BUFFER(testing_hcd->resp);

	if (frame_size != testing_hcd->resp.data_length) {
		ovt_info(ERR_LOG, "Frame size mismatch\n");
		UNLOCK_BUFFER(testing_hcd->resp);
		retval = -EINVAL;
		goto exit;
	} else {
		ovt_info(INFO_LOG, "Frame size match\n");
	}

	limits_rows = ARRAY_SIZE(pt5_hi_limits);
	limits_cols = ARRAY_SIZE(pt5_hi_limits[0]);

	ovt_info(DEBUG_LOG, "hi rows:%d, limits_rows:%d\n", rows, limits_rows);
	ovt_info(DEBUG_LOG, "hi cols:%d, limits_cols:%d\n", cols, limits_cols);

	if (rows > limits_rows || cols > limits_cols) {
		ovt_info(ERR_LOG, "Mismatching hi limits data\n");
		UNLOCK_BUFFER(testing_hcd->resp);
		retval = -EINVAL;
		goto exit;
	} else {
		ovt_info(INFO_LOG, "Matching hi limits data\n");
	}

	limits_rows = ARRAY_SIZE(pt5_lo_limits);
	limits_cols = ARRAY_SIZE(pt5_lo_limits[0]);

	ovt_info(DEBUG_LOG, "lo rows:%d, limits_rows:%d\n", rows, limits_rows);
	ovt_info(DEBUG_LOG, "lo cols:%d, limits_cols:%d\n", cols, limits_cols);

	if (rows > limits_rows || cols > limits_cols) {
		ovt_info(ERR_LOG, "Mismatching lo limits data\n");
		UNLOCK_BUFFER(testing_hcd->resp);
		retval = -EINVAL;
		goto exit;
	} else {
		ovt_info(INFO_LOG, "Matching lo limits data\n");
	}

	buf = testing_hcd->resp.buf;
	testing_hcd->result = true;

	idx = 0;
	for (row = 0; row < rows; row++) {
		for (col = 0; col < cols; col++) {

			data = (unsigned short)(buf[idx] & 0xff) |
					(unsigned short)(buf[idx+1] << 8);

			if (data  > pt5_hi_limits[row][col] ||
					data  < pt5_lo_limits[row][col]) {

				ovt_info(ERR_LOG, "fail at (%2d, %2d) data = %5d, limit = (%4d, %4d)\n",
					row, col, data, pt5_lo_limits[row][col],
					pt5_hi_limits[row][col]);

				testing_hcd->result = false;
			}

			idx += 2;
		}
	}

	UNLOCK_BUFFER(testing_hcd->resp);

	retval = 0;

exit:
	if (tcm_hcd->features.dual_firmware) {
		if (tcm_hcd->reset(tcm_hcd) < 0) {
			ovt_info(ERR_LOG, "Failed to do reset\n");
		}
	}

	ovt_info(INFO_LOG, "testing_pt05_full_raw result = %s\n", (testing_hcd->result)?"pass":"fail");
	return retval;
}


int testing_pt07_dynamic_range(void)
{
	int retval;
	unsigned char *buf;
	unsigned int idx;
	unsigned int row;
	unsigned int col;
	unsigned int data;
	char ch_data[6];
	unsigned int rows;
	unsigned int cols;
	unsigned int limits_rows;
	unsigned int limits_cols;
	unsigned int frame_size_words;
	struct ovt_tcm_app_info *app_info;
	struct ovt_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;

	ovt_info(INFO_LOG, "Start testing_pt07_dynamic_range\n");
	testing_hcd->result = false;

	app_info = &tcm_hcd->app_info;

	rows = le2_to_uint(app_info->num_of_image_rows);
	cols = le2_to_uint(app_info->num_of_image_cols);

	/*zte_add*/
	tcm_hcd->zte_ctrl.rawdata_rows = rows;
	tcm_hcd->zte_ctrl.rawdata_cols = cols;
	ovt_info(INFO_LOG, "%s:RAWDATA_ROW:%d\n", __func__, tcm_hcd->zte_ctrl.rawdata_rows);
	ovt_info(INFO_LOG, "%s:RAWDATA_COL:%d\n", __func__, tcm_hcd->zte_ctrl.rawdata_cols);

	testing_get_frame_size_words(&frame_size_words, false);

	retval = testing_run_prod_test_item(TEST_PT7_DYNAMIC_RANGE);
	if (retval < 0) {
		ovt_info(ERR_LOG, "Failed to run test\n");
		goto exit;
	} else {
		ovt_info(INFO_LOG, "Success to run test\n");
	}

	LOCK_BUFFER(testing_hcd->resp);

	if (frame_size_words != testing_hcd->resp.data_length / 2) {
		ovt_info(ERR_LOG, "Frame size mismatch\n");
		UNLOCK_BUFFER(testing_hcd->resp);
		retval = -EINVAL;
		goto exit;
	} else {
		ovt_info(INFO_LOG, "Frame size match\n");
	}

	limits_rows = ARRAY_SIZE(pt7_hi_limits);
	limits_cols = ARRAY_SIZE(pt7_hi_limits[0]);

	ovt_info(DEBUG_LOG, "hi rows:%d, limits_rows:%d\n", rows, limits_rows);
	ovt_info(DEBUG_LOG, "hi cols:%d, limits_cols:%d\n", cols, limits_cols);

	if (rows > limits_rows || cols > limits_cols) {
		ovt_info(ERR_LOG, "Mismatching hi limits data\n");
		UNLOCK_BUFFER(testing_hcd->resp);
		retval = -EINVAL;
		goto exit;
	} else {
		ovt_info(INFO_LOG, "Matching hi limits data\n");
	}

	limits_rows = ARRAY_SIZE(pt7_lo_limits);
	limits_cols = ARRAY_SIZE(pt7_lo_limits[0]);

	ovt_info(DEBUG_LOG, "lo rows:%d, limits_rows:%d\n", rows, limits_rows);
	ovt_info(DEBUG_LOG, "lo cols:%d, limits_cols:%d\n", cols, limits_cols);

	if (rows > limits_rows || cols > limits_cols) {
		ovt_info(ERR_LOG, "Mismatching lo limits data\n");
		UNLOCK_BUFFER(testing_hcd->resp);
		retval = -EINVAL;
		goto exit;
	} else {
		ovt_info(INFO_LOG, "Matching lo limits data\n");
	}

	idx = 0;
	buf = testing_hcd->resp.buf;
	testing_hcd->result = true;
	tpd_copy_to_tp_firmware_data("\n rawdata:\n");
	for (row = 0; row < rows; row++) {
		for (col = 0; col < cols; col++) {
			data = le2_to_uint(&buf[idx * 2]);
			ovt_info(DEBUG_LOG, "(%2d, %2d) data = %5d\n", row, col, data);

			snprintf(ch_data, sizeof(ch_data), "%5d", data);
			tpd_copy_to_tp_firmware_data(ch_data);
			memset(ch_data, 0, sizeof(ch_data));

			if (data > pt7_hi_limits[row][col] || data < pt7_lo_limits[row][col]) {
				ovt_info(ERR_LOG, "fail at (%2d, %2d) data = %5d, limit = (%4d, %4d)\n",
					row, col, data, pt7_lo_limits[row][col],
					pt7_hi_limits[row][col]);

				testing_hcd->result = false;
			}
			idx++;
		}
		tpd_copy_to_tp_firmware_data("\n");
	}

	UNLOCK_BUFFER(testing_hcd->resp);
	testing_standard_frame_output(false);

	retval = 0;

exit:
	if (tcm_hcd->features.dual_firmware) {
		if (tcm_hcd->reset(tcm_hcd) < 0) {
			ovt_info(ERR_LOG, "Failed to do reset\n");
		}
	}

	ovt_info(INFO_LOG, "-------->testing_pt07_dynamic_range result = %s\n", (testing_hcd->result)?"pass":"fail");
	if (testing_hcd->result) {
		retval = 0;
	} else {
		retval = -1;
	}
	return retval;
}

int testing_pt10_noise(void)
{
	int retval;
	short data;
	char ch_data[6];
	unsigned char *buf;
	unsigned int idx;
	unsigned int row;
	unsigned int col;
	unsigned int rows;
	unsigned int cols;
	unsigned int limits_rows;
	unsigned int limits_cols;
	unsigned int frame_size_words;
	struct ovt_tcm_app_info *app_info;
	struct ovt_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;

	ovt_info(INFO_LOG, "Start testing_pt10_noise\n");
	testing_hcd->result = false;

	app_info = &tcm_hcd->app_info;

	rows = le2_to_uint(app_info->num_of_image_rows);
	cols = le2_to_uint(app_info->num_of_image_cols);

	testing_get_frame_size_words(&frame_size_words, true);

	retval = testing_run_prod_test_item(TEST_PT10_DELTA_NOISE);
	if (retval < 0) {
		ovt_info(ERR_LOG, "Failed to run test\n");
		goto exit;
	} else {
		ovt_info(INFO_LOG, "Success to run test\n");
	}

	LOCK_BUFFER(testing_hcd->resp);

	if (frame_size_words != testing_hcd->resp.data_length / 2) {
		ovt_info(ERR_LOG, "Frame size mismatch\n");
		UNLOCK_BUFFER(testing_hcd->resp);
		retval = -EINVAL;
		goto exit;
	} else {
		ovt_info(INFO_LOG, "Frame size match\n");
	}

	limits_rows = ARRAY_SIZE(pt10_limits);
	limits_cols = ARRAY_SIZE(pt10_limits[0]);

	ovt_info(DEBUG_LOG, "rows:%d, limits_rows:%d\n", rows, limits_rows);
	ovt_info(DEBUG_LOG, "cols:%d, limits_cols:%d\n", cols, limits_cols);

	if (rows > limits_rows || cols > limits_cols) {
		ovt_info(ERR_LOG, "Mismatching limits data\n");
		UNLOCK_BUFFER(testing_hcd->resp);
		retval = -EINVAL;
		goto exit;
	} else {
		ovt_info(INFO_LOG, "Matching limits data\n");
	}

	idx = 0;
	buf = testing_hcd->resp.buf;
	testing_hcd->result = true;
	tpd_copy_to_tp_firmware_data("\n noise:\n");
	for (row = 0; row < rows; row++) {
		for (col = 0; col < cols; col++) {
			data = (short)le2_to_uint(&buf[idx * 2]);

			ovt_info(DEBUG_LOG, "(%2d, %2d) data = %5d\n", row, col, data);

			snprintf(ch_data, sizeof(ch_data), "%5d", data);
			tpd_copy_to_tp_firmware_data(ch_data);
			memset(ch_data, 0, sizeof(ch_data));

			if (data > pt10_limits[row][col]) {
				ovt_info(ERR_LOG, "fail at (%2d, %2d) data = %5d, limit = %4d\n",
					row, col, data, pt10_limits[row][col]);

				testing_hcd->result = false;
			}
			idx++;
		}
		tpd_copy_to_tp_firmware_data("\n");
	}

	UNLOCK_BUFFER(testing_hcd->resp);
	testing_standard_frame_output(false);

	retval = 0;

exit:
	if (tcm_hcd->features.dual_firmware) {
		if (tcm_hcd->reset(tcm_hcd) < 0) {
			ovt_info(ERR_LOG, "Failed to do reset\n");
		}
	}

	ovt_info(INFO_LOG, "-------->testing_pt10_noise result = %s\n", (testing_hcd->result)?"pass":"fail");
	if (testing_hcd->result) {
		retval = 0;
	} else {
		retval = -1;
	}
	return retval;
}


int testing_pt11_open_detection(void)
{
	int retval;
	short data;
	char ch_data[6];
	unsigned char *buf;
	unsigned int idx;
	unsigned int row;
	unsigned int col;
	unsigned int rows;
	unsigned int cols;
	unsigned int limits_rows;
	unsigned int limits_cols;
	unsigned int image_size_words;
	struct ovt_tcm_app_info *app_info;
	struct ovt_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;

	ovt_info(INFO_LOG, "Start testing_pt11_open_detection\n");
	testing_hcd->result = false;

	app_info = &tcm_hcd->app_info;

	rows = le2_to_uint(app_info->num_of_image_rows);
	cols = le2_to_uint(app_info->num_of_image_cols);

	testing_get_frame_size_words(&image_size_words, true);

	retval = testing_run_prod_test_item(TEST_PT11_OPEN_DETECTION);
	if (retval < 0) {
		ovt_info(ERR_LOG, "Failed to run test\n");
		goto exit;
	} else {
		ovt_info(INFO_LOG, "Success to run test\n");
	}

	LOCK_BUFFER(testing_hcd->resp);

	if (image_size_words != testing_hcd->resp.data_length / 2) {
		ovt_info(ERR_LOG, "Image size mismatch\n");
		UNLOCK_BUFFER(testing_hcd->resp);
		retval = -EINVAL;
		goto exit;
	} else {
		ovt_info(INFO_LOG, "Image size match\n");
	}

	limits_rows = ARRAY_SIZE(pt11_hi_limits);
	limits_cols = ARRAY_SIZE(pt11_hi_limits[0]);

	ovt_info(DEBUG_LOG, "hi rows:%d, limits_rows:%d\n", rows, limits_rows);
	ovt_info(DEBUG_LOG, "hi cols:%d, limits_cols:%d\n", cols, limits_cols);

	if (rows > limits_rows || cols > limits_cols) {
		ovt_info(ERR_LOG, "Mismatching hi limits data\n");
		UNLOCK_BUFFER(testing_hcd->resp);
		retval = -EINVAL;
		goto exit;
	} else {
		ovt_info(INFO_LOG, "Matching hi limits data\n");
	}

	limits_rows = ARRAY_SIZE(pt11_lo_limits);
	limits_cols = ARRAY_SIZE(pt11_lo_limits[0]);

	ovt_info(DEBUG_LOG, "lo rows:%d, limits_rows:%d\n", rows, limits_rows);
	ovt_info(DEBUG_LOG, "lo cols:%d, limits_cols:%d\n", cols, limits_cols);

	if (rows > limits_rows || cols > limits_cols) {
		ovt_info(ERR_LOG, "Mismatching lo limits data\n");
		UNLOCK_BUFFER(testing_hcd->resp);
		retval = -EINVAL;
		goto exit;
	} else {
		ovt_info(INFO_LOG, "Matching lo limits data\n");
	}

	idx = 0;
	buf = testing_hcd->resp.buf;
	testing_hcd->result = true;
	tpd_copy_to_tp_firmware_data("\n open test data:\n");
	for (row = 0; row < rows; row++) {
		for (col = 0; col < cols; col++) {
			data = (short)le2_to_uint(&buf[idx * 2]);
			ovt_info(DEBUG_LOG, "(%2d, %2d) data = %5d\n", row, col, data);

			snprintf(ch_data, sizeof(ch_data), "%5d", data);
			tpd_copy_to_tp_firmware_data(ch_data);			
			memset(ch_data, 0, sizeof(ch_data));

			if (data > pt11_hi_limits[row][col] ||
					data < pt11_lo_limits[row][col]) {

				ovt_info(ERR_LOG, "fail at (%2d, %2d) data = %5d, limit = (%4d, %4d)\n",
					row, col, data,
					pt11_lo_limits[row][col],
					pt11_hi_limits[row][col]);

				testing_hcd->result = false;
			}
			idx++;
		}
		tpd_copy_to_tp_firmware_data("\n");
	}

	UNLOCK_BUFFER(testing_hcd->resp);
	testing_standard_frame_output(true);

	retval = 0;

exit:
	if (tcm_hcd->features.dual_firmware) {
		if (tcm_hcd->reset(tcm_hcd) < 0) {
			ovt_info(ERR_LOG, "Failed to do reset\n");
		}
	}

	ovt_info(INFO_LOG, "-------->testing_pt11_open_detection result = %s\n", (testing_hcd->result)?"pass":"fail");
	if (testing_hcd->result) {
		retval = 0;
	} else {
		retval = -1;
	}
	return retval;
}


int testing_reset_open(void)
{
	int retval;
	struct ovt_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	const struct ovt_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;

	ovt_info(INFO_LOG, "Start testing_reset_open\n");
	testing_hcd->result = false;

	if (bdata->reset_gpio < 0) {
		ovt_info(ERR_LOG, "Hardware reset unavailable\n");
		return -EINVAL;
	}

	mutex_lock(&tcm_hcd->reset_mutex);

#ifdef WATCHDOG_SW
	tcm_hcd->update_watchdog(tcm_hcd, false);
#endif

	gpio_set_value(bdata->reset_gpio, bdata->reset_on_state);
	msleep(bdata->reset_active_ms);
	gpio_set_value(bdata->reset_gpio, !bdata->reset_on_state);
	msleep(bdata->reset_delay_ms);

#ifdef WATCHDOG_SW
	tcm_hcd->update_watchdog(tcm_hcd, true);
#endif

	mutex_unlock(&tcm_hcd->reset_mutex);

	if (tcm_hcd->id_info.mode == MODE_APPLICATION_FIRMWARE) {
		retval = tcm_hcd->switch_mode(tcm_hcd, FW_MODE_BOOTLOADER);
		if (retval < 0) {
			ovt_info(ERR_LOG, "Failed to enter bootloader mode\n");
			return retval;
		} else {
			ovt_info(INFO_LOG, "Success to enter bootloader mode\n");
		}
	} else {
		retval = tcm_hcd->identify(tcm_hcd, false);
		if (retval < 0) {
			ovt_info(ERR_LOG, "Failed to do identification\n");
			goto run_app_firmware;
		} else {
			ovt_info(INFO_LOG, "Success to do identification\n");
		}
	}

	if (tcm_hcd->boot_info.last_reset_reason == reset_open_limit)
		testing_hcd->result = true;
	else
		testing_hcd->result = false;

	retval = 0;

run_app_firmware:
	if (tcm_hcd->switch_mode(tcm_hcd, FW_MODE_APPLICATION) < 0) {
		ovt_info(ERR_LOG, "Failed to run application firmware\n");
	} else {
		ovt_info(INFO_LOG, "Success to run application firmware\n");
	}

	ovt_info(INFO_LOG, "testing_reset_open result = %s\n", (testing_hcd->result)?"pass":"fail");
	return retval;
}


static void testing_report(void)
{
	int retval;
	unsigned int offset;
	unsigned int report_size;
	struct ovt_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);
	report_size = tcm_hcd->report.buffer.data_length;

	LOCK_BUFFER(testing_hcd->report);

	if (testing_hcd->report_index == 0) {
		retval = ovt_tcm_alloc_mem(tcm_hcd,
				&testing_hcd->report,
				report_size * testing_hcd->num_of_reports);
		if (retval < 0) {
			ovt_info(ERR_LOG, "Failed to allocate memory for testing_hcd->report.buf\n");
			UNLOCK_BUFFER(testing_hcd->report);
			return;
		}
	}

	if (testing_hcd->report_index < testing_hcd->num_of_reports) {
		offset = report_size * testing_hcd->report_index;

		retval = secure_memcpy(testing_hcd->report.buf + offset,
				testing_hcd->report.buf_size - offset,
				tcm_hcd->report.buffer.buf,
				tcm_hcd->report.buffer.buf_size,
				tcm_hcd->report.buffer.data_length);
		if (retval < 0) {
			ovt_info(ERR_LOG, "Failed to copy report data\n");
			UNLOCK_BUFFER(testing_hcd->report);
			return;
		} else {
			ovt_info(INFO_LOG, "Success to copy report data\n");
		}

		testing_hcd->report_index++;
		testing_hcd->report.data_length += report_size;
	}

	UNLOCK_BUFFER(testing_hcd->report);

	if (testing_hcd->report_index == testing_hcd->num_of_reports)
		complete(&report_complete);
	ovt_info(DEBUG_LOG, "%s exit!\n", __func__);
}


static int testing_init(struct ovt_tcm_hcd *tcm_hcd)
{
	int retval;
	int idx;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);
	testing_hcd = kzalloc(sizeof(*testing_hcd), GFP_KERNEL);
	if (!testing_hcd) {
		ovt_info(ERR_LOG, "Failed to allocate memory for testing_hcd\n");
		return -ENOMEM;
	}

	testing_hcd->tcm_hcd = tcm_hcd;

	testing_hcd->collect_reports = testing_collect_reports;

	INIT_BUFFER(testing_hcd->out, false);
	INIT_BUFFER(testing_hcd->resp, false);
	INIT_BUFFER(testing_hcd->report, false);
	INIT_BUFFER(testing_hcd->process, false);
	INIT_BUFFER(testing_hcd->output, false);

	testing_hcd->sysfs_dir = kobject_create_and_add(SYSFS_DIR_NAME,
			tcm_hcd->sysfs_dir);
	if (!testing_hcd->sysfs_dir) {
		ovt_info(ERR_LOG, "%s:Failed to create sysfs directory\n", __func__);
		retval = -EINVAL;
		goto err_sysfs_create_dir;
	} else {
		ovt_info(INFO_LOG, "%s:Success to create sysfs directory\n", __func__);
	}

	for (idx = 0; idx < ARRAY_SIZE(attrs); idx++) {
		retval = sysfs_create_file(testing_hcd->sysfs_dir,
				&(*attrs[idx]).attr);
		if (retval < 0) {
			ovt_info(ERR_LOG, "%s:Failed to create sysfs file\n", __func__);
			goto err_sysfs_create_file;
		}
	}
	ovt_info(INFO_LOG, "%s:Success to create sysfs file\n", __func__);

	retval = sysfs_create_bin_file(testing_hcd->sysfs_dir, &bin_attr);
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to create sysfs bin file\n", __func__);
		goto err_sysfs_create_bin_file;
	} else {
		ovt_info(INFO_LOG, "%s:Success to create sysfs bin file\n", __func__);
	}

	ovt_info(DEBUG_LOG, "%s exit!\n", __func__);
	return 0;

err_sysfs_create_bin_file:
err_sysfs_create_file:
	for (idx--; idx >= 0; idx--)
		sysfs_remove_file(testing_hcd->sysfs_dir, &(*attrs[idx]).attr);

	kobject_put(testing_hcd->sysfs_dir);

err_sysfs_create_dir:
	RELEASE_BUFFER(testing_hcd->output);
	RELEASE_BUFFER(testing_hcd->process);
	RELEASE_BUFFER(testing_hcd->report);
	RELEASE_BUFFER(testing_hcd->resp);
	RELEASE_BUFFER(testing_hcd->out);

	kfree(testing_hcd);
	testing_hcd = NULL;

	return retval;
}

static int testing_remove(struct ovt_tcm_hcd *tcm_hcd)
{
	int idx;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);
	if (!testing_hcd)
		goto exit;

	sysfs_remove_bin_file(testing_hcd->sysfs_dir, &bin_attr);

	for (idx = 0; idx < ARRAY_SIZE(attrs); idx++)
		sysfs_remove_file(testing_hcd->sysfs_dir, &(*attrs[idx]).attr);

	kobject_put(testing_hcd->sysfs_dir);

	RELEASE_BUFFER(testing_hcd->output);
	RELEASE_BUFFER(testing_hcd->process);
	RELEASE_BUFFER(testing_hcd->report);
	RELEASE_BUFFER(testing_hcd->resp);
	RELEASE_BUFFER(testing_hcd->out);

	kfree(testing_hcd);
	testing_hcd = NULL;

exit:
	complete(&testing_remove_complete);

	return 0;
}

static int testing_reinit(struct ovt_tcm_hcd *tcm_hcd)
{
	int retval;

	if (!testing_hcd) {
		retval = testing_init(tcm_hcd);
		return retval;
	}

	return 0;
}

static int testing_syncbox(struct ovt_tcm_hcd *tcm_hcd)
{
	if (!testing_hcd)
		return 0;

	if (tcm_hcd->report.id == testing_hcd->report_type)
		testing_report();

	return 0;
}

static struct ovt_tcm_module_cb testing_module = {
	.type = TCM_TESTING,
	.init = testing_init,
	.remove = testing_remove,
	.syncbox = testing_syncbox,
#ifdef REPORT_NOTIFIER
	.asyncbox = NULL,
#endif
	.reinit = testing_reinit,
	.suspend = NULL,
	.resume = NULL,
	.early_suspend = NULL,
};

int testing_module_init(void)
{
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);
	return ovt_tcm_add_module(&testing_module, true);
}

void testing_module_exit(void)
{
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);
	ovt_tcm_add_module(&testing_module, false);

	wait_for_completion(&testing_remove_complete);
}

/*module_init(testing_module_init);
module_exit(testing_module_exit);*/

MODULE_AUTHOR("Omnivision, Inc.");
MODULE_DESCRIPTION("Omnivision TCM Testing Module");
MODULE_LICENSE("GPL v2");
