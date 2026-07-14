/*
 * omnivision TCM touchscreen driver
 *
 * Copyright (C) 2017-2018 omnivision Incorporated. All rights reserved.
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
 * INFORMATION CONTAINED IN THIS DOCUMENT IS PROVIDED "AS-IS," AND omnivision
 * EXPRESSLY DISCLAIMS ALL EXPRESS AND IMPLIED WARRANTIES, INCLUDING ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE,
 * AND ANY WARRANTIES OF NON-INFRINGEMENT OF ANY INTELLECTUAL PROPERTY RIGHTS.
 * IN NO EVENT SHALL omnivision BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, PUNITIVE, OR CONSEQUENTIAL DAMAGES ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OF THE INFORMATION CONTAINED IN THIS DOCUMENT, HOWEVER CAUSED
 * AND BASED ON ANY THEORY OF LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, AND EVEN IF omnivision WAS ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE. IF A TRIBUNAL OF COMPETENT JURISDICTION DOES
 * NOT PERMIT THE DISCLAIMER OF DIRECT DAMAGES OR ANY OTHER DAMAGES, omnivision'
 * TOTAL CUMULATIVE LIABILITY TO ANY PARTY SHALL NOT EXCEED ONE HUNDRED U.S.
 * DOLLARS.
 */

#include "omnivision_tcm_core.h"
#include "omnivision_tcm_zeroflash.h"

#define ENABLE_SYS_ZEROFLASH true

/*zte_add*/
#define OVT_TCM_FW_NAME	"ovt_tcm_fw_"
extern char ovt_tcm_vendor_name[];
extern int get_ovt_tcm_module_info_from_lcd(void);

#define BOOT_CONFIG_ID "BOOT_CONFIG"

#define F35_APP_CODE_ID "F35_APP_CODE"

#define ROMBOOT_APP_CODE_ID "ROMBOOT_APP_CODE"

#define RESERVED_BYTES 14

#define APP_CONFIG_ID "APP_CONFIG"

#define DISP_CONFIG_ID "DISPLAY"

#define OPEN_SHORT_ID "OPENSHORT"

#define SYSFS_DIR_NAME "zeroflash"

#define IMAGE_FILE_MAGIC_VALUE 0x4818472b

#define FLASH_AREA_MAGIC_VALUE 0x7c05e516

#define PDT_START_ADDR 0x00e9

#define PDT_END_ADDR 0x00ee

#define UBL_FN_NUMBER 0x35

#define F35_CTRL3_OFFSET 18

#define F35_CTRL7_OFFSET 22

#define F35_WRITE_FW_TO_PMEM_COMMAND 4

#define TP_RESET_TO_HDL_DELAY_MS 11

#define DOWNLOAD_RETRY_COUNT 10

static void zeroflash_do_romboot_firmware_download(void);

DECLARE_COMPLETION(zeroflash_remove_complete);

STORE_PROTOTYPE(zeroflash, hdl)

static struct device_attribute *attrs[] = {
	ATTRIFY(hdl),
};

struct zeroflash_hcd *zeroflash_hcd;

static int zeroflash_wait_hdl(struct ovt_tcm_hcd *tcm_hcd)
{
	int retval;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);

	msleep(HOST_DOWNLOAD_WAIT_MS);

	if (!atomic_read(&tcm_hcd->host_downloading))
		return 0;

	retval = wait_event_interruptible_timeout(tcm_hcd->hdl_wq,
			!atomic_read(&tcm_hcd->host_downloading),
			msecs_to_jiffies(HOST_DOWNLOAD_TIMEOUT_MS));
	if (retval == 0) {
		ovt_info(ERR_LOG, "Timed out waiting for completion of host download\n");
		atomic_set(&tcm_hcd->host_downloading, 0);
		retval = -EIO;
	} else {
		retval = 0;
	}

	ovt_info(DEBUG_LOG, "%s exit!\n", __func__);
	return retval;
}

static ssize_t zeroflash_sysfs_hdl_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval = 0;
	unsigned int input;
	struct ovt_tcm_hcd *tcm_hcd = zeroflash_hcd->tcm_hcd;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);

	if (sscanf(buf, "%u", &input) != 1)
		return -EINVAL;

	if (input && (tcm_hcd->in_hdl_mode)) {

		retval = tcm_hcd->reset(tcm_hcd);
		if (retval < 0)
			ovt_info(ERR_LOG, "Failed to trigger the host download by reset\n");

		retval = zeroflash_wait_hdl(tcm_hcd);
		if (retval < 0)
			ovt_info(ERR_LOG, "Failed to wait for completion of host download\n");

		if (zeroflash_hcd->fw_entry) {
			release_firmware(zeroflash_hcd->fw_entry);
			zeroflash_hcd->fw_entry = NULL;
		}

		zeroflash_hcd->image = NULL;

	} else {
		ovt_info(ERR_LOG, "Invalid HDL devices\n");
	}

	ovt_info(DEBUG_LOG, "%s exit!\n", __func__);
	return count;
}

static int zeroflash_check_uboot(void)
{
	int retval;
	unsigned char fn_number;
	unsigned int retry = 3;
	struct rmi_f35_query query;
	struct rmi_pdt_entry p_entry;
	struct ovt_tcm_hcd *tcm_hcd = zeroflash_hcd->tcm_hcd;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);
re_check:
	retval = ovt_tcm_rmi_read(tcm_hcd,
			PDT_END_ADDR,
			&fn_number,
			sizeof(fn_number));
	if (retval < 0) {
		ovt_info(ERR_LOG, "Failed to read RMI function number\n");
		return retval;
	}

	ovt_info(INFO_LOG, "Found F$%02x\n", fn_number);

	if (fn_number != UBL_FN_NUMBER) {
		if (retry--)
			goto re_check;

		ovt_info(ERR_LOG, "Failed to find F$35\n");
		return -ENODEV;
	}

	if (zeroflash_hcd->f35_ready)
		return 0;

	retval = ovt_tcm_rmi_read(tcm_hcd,
			PDT_START_ADDR,
			(unsigned char *)&p_entry,
			sizeof(p_entry));
	if (retval < 0) {
		ovt_info(ERR_LOG, "Failed to read PDT entry\n");
		return retval;
	}

	zeroflash_hcd->f35_addr.query_base = p_entry.query_base_addr;
	zeroflash_hcd->f35_addr.command_base = p_entry.command_base_addr;
	zeroflash_hcd->f35_addr.control_base = p_entry.control_base_addr;
	zeroflash_hcd->f35_addr.data_base = p_entry.data_base_addr;

	retval = ovt_tcm_rmi_read(tcm_hcd,
			zeroflash_hcd->f35_addr.query_base,
			(unsigned char *)&query,
			sizeof(query));
	if (retval < 0) {
		ovt_info(ERR_LOG, "Failed to read F$35 query\n");
		return retval;
	}

	zeroflash_hcd->f35_ready = true;

	if (query.has_query2 && query.has_ctrl7 && query.has_host_download) {
		zeroflash_hcd->has_hdl = true;
	} else {
		ovt_info(ERR_LOG, "Host download not supported\n");
		zeroflash_hcd->has_hdl = false;
		return -ENODEV;
	}

	ovt_info(DEBUG_LOG, "%s exit!\n", __func__);
	return 0;
}

static int zeroflash_parse_fw_image(void)
{
	unsigned int idx;
	unsigned int addr;
	unsigned int offset;
	unsigned int length;
	unsigned int checksum;
	unsigned int flash_addr;
	unsigned int magic_value;
	unsigned int num_of_areas;
	struct image_header *header;
	struct image_info *image_info;
	struct area_descriptor *descriptor;
	struct ovt_tcm_hcd *tcm_hcd = zeroflash_hcd->tcm_hcd;
	const unsigned char *image;
	const unsigned char *content;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);
	image = zeroflash_hcd->image;
	image_info = &zeroflash_hcd->image_info;
	header = (struct image_header *)image;

	magic_value = le4_to_uint(header->magic_value);
	if (magic_value != IMAGE_FILE_MAGIC_VALUE) {
		ovt_info(ERR_LOG, "%s:Invalid image file magic value\n", __func__);
		return -EINVAL;
	}

	memset(image_info, 0x00, sizeof(*image_info));

	offset = sizeof(*header);
	num_of_areas = le4_to_uint(header->num_of_areas);

	for (idx = 0; idx < num_of_areas; idx++) {
		addr = le4_to_uint(image + offset);
		descriptor = (struct area_descriptor *)(image + addr);
		offset += 4;

		magic_value = le4_to_uint(descriptor->magic_value);
		if (magic_value != FLASH_AREA_MAGIC_VALUE)
			continue;

		length = le4_to_uint(descriptor->length);
		content = (unsigned char *)descriptor + sizeof(*descriptor);
		flash_addr = le4_to_uint(descriptor->flash_addr_words) * 2;
		checksum = le4_to_uint(descriptor->checksum);

		if (strncmp((char *)descriptor->id_string, BOOT_CONFIG_ID, strlen(BOOT_CONFIG_ID)) == 0) {
			if (checksum != (crc32(~0, content, length) ^ ~0)) {
				ovt_info(ERR_LOG, "%s:Boot config checksum error\n", __func__);
				return -EINVAL;
			}
			image_info->boot_config.size = length;
			image_info->boot_config.data = content;
			image_info->boot_config.flash_addr = flash_addr;
			ovt_info(INFO_LOG, "%s:Boot config size = %d\n", __func__, length);
			ovt_info(INFO_LOG, "%s:Boot config flash address = 0x%08x\n", __func__, flash_addr);
		} else if ((strncmp((char *)descriptor->id_string, F35_APP_CODE_ID, strlen(F35_APP_CODE_ID)) == 0)) {
			if (tcm_hcd->sensor_type != TYPE_F35) {
				ovt_info(ERR_LOG, "%s:Improper descriptor, F35_APP_CODE_ID\n", __func__);
				return -EINVAL;
			}

			if (checksum != (crc32(~0, content, length) ^ ~0)) {
				ovt_info(ERR_LOG, "%s:HDL_F35 firmware checksum error\n", __func__);
				return -EINVAL;
			}
			image_info->app_firmware.size = length;
			image_info->app_firmware.data = content;
			image_info->app_firmware.flash_addr = flash_addr;
			ovt_info(INFO_LOG, "%s:HDL_F35 firmware size = %d\n", __func__, length);
			ovt_info(INFO_LOG, "%s:HDL_F35 firmware flash address = 0x%08x\n", __func__, flash_addr);

		} else if ((strncmp((char *)descriptor->id_string, ROMBOOT_APP_CODE_ID,
			strlen(ROMBOOT_APP_CODE_ID)) == 0)) {
			if (tcm_hcd->sensor_type != TYPE_ROMBOOT) {
				ovt_info(ERR_LOG, "%s:Improper descriptor, ROMBOOT_APP_CODE_ID\n", __func__);
				return -EINVAL;
			}

			if (checksum != (crc32(~0, content, length) ^ ~0)) {
				ovt_info(ERR_LOG, "%s:HDL_ROMBoot firmware checksum error\n", __func__);
				return -EINVAL;
			}
			image_info->app_firmware.size = length;
			image_info->app_firmware.data = content;
			image_info->app_firmware.flash_addr = flash_addr;
			ovt_info(INFO_LOG, "%s:HDL_ROMBoot firmware size = %d\n", __func__, length);
			ovt_info(INFO_LOG, "%s:HDL_ROMBoot firmware flash address = 0x%08x\n", __func__, flash_addr);

		} else if (strncmp((char *)descriptor->id_string, APP_CONFIG_ID, strlen(APP_CONFIG_ID)) == 0) {

			if (checksum != (crc32(~0, content, length) ^ ~0)) {
				ovt_info(ERR_LOG, "Application config checksum error\n");
				return -EINVAL;
			}
			image_info->app_config.size = length;
			image_info->app_config.data = content;
			image_info->app_config.flash_addr = flash_addr;
			image_info->packrat_number = le4_to_uint(&content[14]);

			/*zte_add*/
			zeroflash_hcd->tcm_hcd->zte_ctrl.fw_ver = image_info->packrat_number;
			ovt_info(INFO_LOG, "%s:image_info->packrat_number = %d\n", __func__, image_info->packrat_number);

			ovt_info(INFO_LOG, "%s:Application config size = %d\n", __func__, length);
			ovt_info(INFO_LOG, "%s:Application config flash address = 0x%08x\n", __func__, flash_addr);
		} else if (strncmp((char *)descriptor->id_string, DISP_CONFIG_ID, strlen(DISP_CONFIG_ID)) == 0) {
			if (checksum != (crc32(~0, content, length) ^ ~0)) {
				ovt_info(ERR_LOG, "Display config checksum error\n");
				return -EINVAL;
			}
			image_info->disp_config.size = length;
			image_info->disp_config.data = content;
			image_info->disp_config.flash_addr = flash_addr;
			ovt_info(INFO_LOG, "%s:Display config size = %d\n", __func__, length);
			ovt_info(INFO_LOG, "%s:Display config flash address = 0x%08x\n", __func__, flash_addr);
		} else if (strncmp((char *)descriptor->id_string, OPEN_SHORT_ID, strlen(OPEN_SHORT_ID)) == 0) {
			if (checksum != (crc32(~0, content, length) ^ ~0)) {
				ovt_info(ERR_LOG, "open_short config checksum error\n");
				return -EINVAL;
			}
			zeroflash_hcd->has_open_short_config = true;
			image_info->open_short_config.size = length;
			image_info->open_short_config.data = content;
			image_info->open_short_config.flash_addr = flash_addr;
			ovt_info(INFO_LOG, "%s:open_short config size = %d\n", __func__, length);
			ovt_info(INFO_LOG, "%s:open_short config flash address = 0x%08x\n", __func__, flash_addr);
		}
	}

	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return 0;
}

static int zeroflash_get_fw_image(void)
{
	int retval;
	struct ovt_tcm_hcd *tcm_hcd = zeroflash_hcd->tcm_hcd;
	char fwname[50] = { 0 };/*zte_add*/
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);

	if (zeroflash_hcd->fw_entry != NULL) {
		ovt_info(INFO_LOG, "%s:zeroflash_hcd->fw_entry is not NULL\n", __func__);
		release_firmware(zeroflash_hcd->fw_entry);
		zeroflash_hcd->fw_entry = NULL;
		zeroflash_hcd->image = NULL;
	} else {
		ovt_info(INFO_LOG, "%s:zeroflash_hcd->fw_entry is NULL, so delay 2s\n", __func__);
		msleep(2000); //power on case, sleep 2s to get the file system img
	}
	/*zte_add*/
	get_ovt_tcm_module_info_from_lcd();
	snprintf(fwname, sizeof(fwname), "%s%s.img", OVT_TCM_FW_NAME, ovt_tcm_vendor_name);

	if (zeroflash_hcd->image == NULL) {
		retval = request_firmware(&zeroflash_hcd->fw_entry, fwname, tcm_hcd->pdev->dev.parent);
		if (retval < 0) {
			ovt_info(ERR_LOG, "%s:Failed to request %s, so request default fw\n", __func__, fwname);
			retval = request_firmware(&zeroflash_hcd->fw_entry,
				OVT_DEFAULT_FW_IMAGE_NAME,
				tcm_hcd->pdev->dev.parent);
			if (retval < 0) {
				ovt_info(ERR_LOG, "%s:Failed to request %s\n", __func__, OVT_DEFAULT_FW_IMAGE_NAME);
				return retval;
			} else {
				ovt_info(INFO_LOG, "%s:Success to request %s\n", __func__, OVT_DEFAULT_FW_IMAGE_NAME);
			}
		} else {
			ovt_info(INFO_LOG, "%s:Success to request %s\n", __func__, fwname);
		}
	}

	ovt_info(INFO_LOG, "%s:Firmware image size = %d\n", __func__,
			(unsigned int)zeroflash_hcd->fw_entry->size);

	zeroflash_hcd->image = zeroflash_hcd->fw_entry->data;

	retval = zeroflash_parse_fw_image();
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to parse firmware image\n", __func__);
		release_firmware(zeroflash_hcd->fw_entry);
		zeroflash_hcd->fw_entry = NULL;
		zeroflash_hcd->image = NULL;
		return retval;
	} else {
		ovt_info(INFO_LOG, "%s:Success to parse firmware image\n", __func__);
	}

	ovt_info(DEBUG_LOG, "%s exit!\n", __func__);
	return 0;
}

static void zeroflash_download_config(void)
{
	int retval;
	struct firmware_status *fw_status;
	struct ovt_tcm_hcd *tcm_hcd = zeroflash_hcd->tcm_hcd;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);
	fw_status = &zeroflash_hcd->fw_status;

	if (!fw_status->need_app_config && !fw_status->need_disp_config
			&& !(fw_status->need_open_short_config
			&& zeroflash_hcd->has_open_short_config)
			&& (atomic_read(&tcm_hcd->host_downloading))) {
		atomic_set(&tcm_hcd->host_downloading, 0);
		retval = wait_for_completion_timeout(tcm_hcd->helper.helper_completion,
			msecs_to_jiffies(500));
		if (retval == 0) {
			ovt_info(ERR_LOG, "timeout to wait for helper completion\n");
			return;
		}
		if (atomic_read(&tcm_hcd->helper.task) == HELP_NONE) {
			atomic_set(&tcm_hcd->helper.task, HELP_SEND_REINIT_NOTIFICATION);
			queue_work(tcm_hcd->helper.workqueue, &tcm_hcd->helper.work);
		}
		return;
	}

	if (atomic_read(&tcm_hcd->host_downloading) && !tcm_hcd->ovt_tcm_driver_removing)
		queue_work(zeroflash_hcd->workqueue, &zeroflash_hcd->config_work);

	ovt_info(DEBUG_LOG, "%s exit!\n", __func__);
}

static int zeroflash_download_open_short_config(void)
{
	int retval;
	unsigned char response_code;
	struct image_info *image_info;
	struct ovt_tcm_hcd *tcm_hcd = zeroflash_hcd->tcm_hcd;
	static unsigned int retry_count;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);
	ovt_info(INFO_LOG, "%s:open_short config download start\n", __func__);

	image_info = &zeroflash_hcd->image_info;

	if (image_info->open_short_config.size == 0) {
		ovt_info(ERR_LOG, "%s:No open_short config in image file\n", __func__);
		return -EINVAL;
	}

	LOCK_BUFFER(zeroflash_hcd->out);

	retval = ovt_tcm_alloc_mem(tcm_hcd,
			&zeroflash_hcd->out,
			image_info->open_short_config.size + 2);
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to allocate memory for open_short config\n", __func__);
		goto unlock_out;
	}

	switch (zeroflash_hcd->fw_status.hdl_version) {
	case 0:
	case 1:
		zeroflash_hcd->out.buf[0] = 1;
		break;
	case 2:
		zeroflash_hcd->out.buf[0] = 2;
		break;
	default:
		retval = -EINVAL;
		ovt_info(ERR_LOG, "%s:Invalid HDL version (%d)\n", __func__,
				zeroflash_hcd->fw_status.hdl_version);
		goto unlock_out;
	}

	zeroflash_hcd->out.buf[1] = HDL_OPEN_SHORT_CONFIG;

	retval = secure_memcpy(&zeroflash_hcd->out.buf[2],
			zeroflash_hcd->out.buf_size - 2,
			image_info->open_short_config.data,
			image_info->open_short_config.size,
			image_info->open_short_config.size);
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to copy open_short config data\n", __func__);
		goto unlock_out;
	}

	zeroflash_hcd->out.data_length = image_info->open_short_config.size + 2;

	LOCK_BUFFER(zeroflash_hcd->resp);

	retval = tcm_hcd->write_message(tcm_hcd,
			CMD_DOWNLOAD_CONFIG,
			zeroflash_hcd->out.buf,
			zeroflash_hcd->out.data_length,
			&zeroflash_hcd->resp.buf,
			&zeroflash_hcd->resp.buf_size,
			&zeroflash_hcd->resp.data_length,
			&response_code,
			0);
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to write command %s\n", __func__,
				STR(CMD_DOWNLOAD_CONFIG));
		if (response_code != STATUS_ERROR)
			goto unlock_resp;
		retry_count++;
		if (DOWNLOAD_RETRY_COUNT && retry_count > DOWNLOAD_RETRY_COUNT)
			goto unlock_resp;
	} else {
		retry_count = 0;
	}

	retval = secure_memcpy((unsigned char *)&zeroflash_hcd->fw_status,
			sizeof(zeroflash_hcd->fw_status),
			zeroflash_hcd->resp.buf,
			zeroflash_hcd->resp.buf_size,
			sizeof(zeroflash_hcd->fw_status));
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to copy firmware status\n", __func__);
		goto unlock_resp;
	}

	ovt_info(INFO_LOG, "%s:open_short config download end\n", __func__);

	retval = 0;

unlock_resp:
	UNLOCK_BUFFER(zeroflash_hcd->resp);

unlock_out:
	UNLOCK_BUFFER(zeroflash_hcd->out);

	ovt_info(DEBUG_LOG, "%s exit!\n", __func__);
	return retval;
}

static int zeroflash_download_disp_config(void)
{
	int retval;
	unsigned char response_code;
	struct image_info *image_info;
	struct ovt_tcm_hcd *tcm_hcd = zeroflash_hcd->tcm_hcd;
	static unsigned int retry_count;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);
	ovt_info(INFO_LOG, "%s:Display config download start\n", __func__);

	image_info = &zeroflash_hcd->image_info;

	if (image_info->disp_config.size == 0) {
		ovt_info(ERR_LOG, "%s:No display config in image file\n", __func__);
		return -EINVAL;
	}

	LOCK_BUFFER(zeroflash_hcd->out);

	retval = ovt_tcm_alloc_mem(tcm_hcd,
			&zeroflash_hcd->out,
			image_info->disp_config.size + 2);
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to allocate memory for display config\n", __func__);
		goto unlock_out;
	}

	switch (zeroflash_hcd->fw_status.hdl_version) {
	case 0:
	case 1:
		zeroflash_hcd->out.buf[0] = 1;
		break;
	case 2:
		zeroflash_hcd->out.buf[0] = 2;
		break;
	default:
		retval = -EINVAL;
		ovt_info(ERR_LOG, "%s:Invalid HDL version (%d)\n", __func__,
			zeroflash_hcd->fw_status.hdl_version);
		goto unlock_out;
	}

	zeroflash_hcd->out.buf[1] = HDL_DISPLAY_CONFIG;

	retval = secure_memcpy(&zeroflash_hcd->out.buf[2],
			zeroflash_hcd->out.buf_size - 2,
			image_info->disp_config.data,
			image_info->disp_config.size,
			image_info->disp_config.size);
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to copy display config data\n", __func__);
		goto unlock_out;
	}

	zeroflash_hcd->out.data_length = image_info->disp_config.size + 2;

	LOCK_BUFFER(zeroflash_hcd->resp);

	retval = tcm_hcd->write_message(tcm_hcd,
			CMD_DOWNLOAD_CONFIG,
			zeroflash_hcd->out.buf,
			zeroflash_hcd->out.data_length,
			&zeroflash_hcd->resp.buf,
			&zeroflash_hcd->resp.buf_size,
			&zeroflash_hcd->resp.data_length,
			&response_code,
			0);
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to write command %s\n", __func__,
				STR(CMD_DOWNLOAD_CONFIG));
		if (response_code != STATUS_ERROR)
			goto unlock_resp;
		retry_count++;
		if (DOWNLOAD_RETRY_COUNT && retry_count > DOWNLOAD_RETRY_COUNT)
			goto unlock_resp;
	} else {
		retry_count = 0;
	}

	retval = secure_memcpy((unsigned char *)&zeroflash_hcd->fw_status,
			sizeof(zeroflash_hcd->fw_status),
			zeroflash_hcd->resp.buf,
			zeroflash_hcd->resp.buf_size,
			sizeof(zeroflash_hcd->fw_status));
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to copy firmware status\n", __func__);
		goto unlock_resp;
	}

	ovt_info(INFO_LOG, "%s:Display config download end\n", __func__);

	retval = 0;

unlock_resp:
	UNLOCK_BUFFER(zeroflash_hcd->resp);

unlock_out:
	UNLOCK_BUFFER(zeroflash_hcd->out);

	ovt_info(DEBUG_LOG, "%s exit!\n", __func__);
	return retval;
}

static int zeroflash_download_app_config(void)
{
	int retval;
	unsigned char padding;
	unsigned char response_code;
	struct image_info *image_info;
	struct ovt_tcm_hcd *tcm_hcd = zeroflash_hcd->tcm_hcd;
	static unsigned int retry_count;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);
	ovt_info(INFO_LOG, "%s:Application config download start\n", __func__);

	image_info = &zeroflash_hcd->image_info;

	if (image_info->app_config.size == 0) {
		ovt_info(ERR_LOG, "%s:No application config in image file\n", __func__);
		return -EINVAL;
	}

	padding = image_info->app_config.size % 8;
	if (padding)
		padding = 8 - padding;

	LOCK_BUFFER(zeroflash_hcd->out);

	retval = ovt_tcm_alloc_mem(tcm_hcd,
			&zeroflash_hcd->out,
			image_info->app_config.size + 2 + padding);
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to allocate memory for application config\n", __func__);
		goto unlock_out;
	}

	switch (zeroflash_hcd->fw_status.hdl_version) {
	case 0:
	case 1:
		zeroflash_hcd->out.buf[0] = 1;
		break;
	case 2:
		zeroflash_hcd->out.buf[0] = 2;
		break;
	default:
		retval = -EINVAL;
		ovt_info(ERR_LOG, "%s:Invalid HDL version (%d)\n", __func__,
			zeroflash_hcd->fw_status.hdl_version);
		goto unlock_out;
	}

	zeroflash_hcd->out.buf[1] = HDL_TOUCH_CONFIG;

	retval = secure_memcpy(&zeroflash_hcd->out.buf[2],
			zeroflash_hcd->out.buf_size - 2,
			image_info->app_config.data,
			image_info->app_config.size,
			image_info->app_config.size);
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to copy application config data\n", __func__);
		goto unlock_out;
	}

	zeroflash_hcd->out.data_length = image_info->app_config.size + 2;
	zeroflash_hcd->out.data_length += padding;

	LOCK_BUFFER(zeroflash_hcd->resp);

	retval = tcm_hcd->write_message(tcm_hcd,
			CMD_DOWNLOAD_CONFIG,
			zeroflash_hcd->out.buf,
			zeroflash_hcd->out.data_length,
			&zeroflash_hcd->resp.buf,
			&zeroflash_hcd->resp.buf_size,
			&zeroflash_hcd->resp.data_length,
			&response_code,
			0);
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to write command %s\n", __func__,
				STR(CMD_DOWNLOAD_CONFIG));
		if (response_code != STATUS_ERROR)
			goto unlock_resp;
		retry_count++;
		if (DOWNLOAD_RETRY_COUNT && retry_count > DOWNLOAD_RETRY_COUNT)
			goto unlock_resp;
	} else {
		retry_count = 0;
	}

	retval = secure_memcpy((unsigned char *)&zeroflash_hcd->fw_status,
			sizeof(zeroflash_hcd->fw_status),
			zeroflash_hcd->resp.buf,
			zeroflash_hcd->resp.buf_size,
			sizeof(zeroflash_hcd->fw_status));
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to copy firmware status\n", __func__);
		goto unlock_resp;
	}

	ovt_info(INFO_LOG, "%s:Application config download end\n", __func__);

	retval = 0;

unlock_resp:
	UNLOCK_BUFFER(zeroflash_hcd->resp);

unlock_out:
	UNLOCK_BUFFER(zeroflash_hcd->out);

	ovt_info(DEBUG_LOG, "%s exit!\n", __func__);
	return retval;
}

static void zeroflash_download_config_work(struct work_struct *work)
{
	int retval;
	struct ovt_tcm_hcd *tcm_hcd = zeroflash_hcd->tcm_hcd;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);

	retval = zeroflash_get_fw_image();
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to get firmware image\n", __func__);
		return;
	} else {
		ovt_info(INFO_LOG, "%s:Success to get firmware image\n", __func__);
	}

	ovt_info(INFO_LOG, "%s:Start of config download\n", __func__);

	if (zeroflash_hcd->fw_status.need_app_config) {
		retval = zeroflash_download_app_config();
		if (retval < 0) {
			atomic_set(&tcm_hcd->host_downloading, 0);
			ovt_info(ERR_LOG, "%s:Failed to download application config, abort\n", __func__);
			return;
		} else {
			ovt_info(INFO_LOG, "%s:Success to download application config\n", __func__);
		}
		goto exit;
	}

	if (zeroflash_hcd->fw_status.need_disp_config) {
		retval = zeroflash_download_disp_config();
		if (retval < 0) {
			atomic_set(&tcm_hcd->host_downloading, 0);
			ovt_info(ERR_LOG, "%s:Failed to download display config, abort\n", __func__);
			return;
		} else {
			ovt_info(INFO_LOG, "%s:Success to download display config\n", __func__);
		}
		goto exit;
	}

	if (zeroflash_hcd->fw_status.need_open_short_config &&
			zeroflash_hcd->has_open_short_config) {

		retval = zeroflash_download_open_short_config();
		if (retval < 0) {
			atomic_set(&tcm_hcd->host_downloading, 0);
			ovt_info(ERR_LOG, "%s:Failed to download open_short config, abort\n", __func__);
			return;
		} else {
			ovt_info(INFO_LOG, "%s:Success to download open_short config\n", __func__);
		}
		goto exit;
	}

exit:
	ovt_info(INFO_LOG, "%s:End of config download\n", __func__);

	if (tcm_hcd->ovt_tcm_driver_removing == 1)
		return;

	zeroflash_download_config();
}

static int zeroflash_download_app_fw(void)
{
	int retval;
	unsigned char command;
	struct image_info *image_info;
	struct ovt_tcm_hcd *tcm_hcd = zeroflash_hcd->tcm_hcd;
#if TP_RESET_TO_HDL_DELAY_MS
	const struct ovt_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;
#endif
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);
	ovt_info(INFO_LOG, "%s:Downloading application firmware\n", __func__);

	image_info = &zeroflash_hcd->image_info;

	if (image_info->app_firmware.size == 0) {
		ovt_info(ERR_LOG, "%s:No application firmware in image file\n", __func__);
		return -EINVAL;
	}

	LOCK_BUFFER(zeroflash_hcd->out);

	retval = ovt_tcm_alloc_mem(tcm_hcd,
			&zeroflash_hcd->out,
			image_info->app_firmware.size);
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to allocate memory for application firmware\n", __func__);
		UNLOCK_BUFFER(zeroflash_hcd->out);
		return retval;
	}

	retval = secure_memcpy(zeroflash_hcd->out.buf,
			zeroflash_hcd->out.buf_size,
			image_info->app_firmware.data,
			image_info->app_firmware.size,
			image_info->app_firmware.size);
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to copy application firmware data\n", __func__);
		UNLOCK_BUFFER(zeroflash_hcd->out);
		return retval;
	}

	zeroflash_hcd->out.data_length = image_info->app_firmware.size;

	command = F35_WRITE_FW_TO_PMEM_COMMAND;

#if TP_RESET_TO_HDL_DELAY_MS
	if (bdata->tpio_reset_gpio >= 0) {
		gpio_set_value(bdata->tpio_reset_gpio, bdata->reset_on_state);
		msleep(bdata->reset_active_ms);
		gpio_set_value(bdata->tpio_reset_gpio, !bdata->reset_on_state);
		mdelay(TP_RESET_TO_HDL_DELAY_MS);
	}
#endif

	retval = ovt_tcm_rmi_write(tcm_hcd,
			zeroflash_hcd->f35_addr.control_base + F35_CTRL3_OFFSET,
			&command,
			sizeof(command));
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to write F$35 command\n", __func__);
		UNLOCK_BUFFER(zeroflash_hcd->out);
		return retval;
	}

	retval = ovt_tcm_rmi_write(tcm_hcd,
			zeroflash_hcd->f35_addr.control_base + F35_CTRL7_OFFSET,
			zeroflash_hcd->out.buf,
			zeroflash_hcd->out.data_length);
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to write application firmware data\n", __func__);
		UNLOCK_BUFFER(zeroflash_hcd->out);
		return retval;
	}

	UNLOCK_BUFFER(zeroflash_hcd->out);

	ovt_info(INFO_LOG, "%s:Application firmware downloaded\n", __func__);

	ovt_info(DEBUG_LOG, "%s exit!\n", __func__);
	return 0;
}


static void zeroflash_do_f35_firmware_download(void)
{
	int retval;
	struct rmi_f35_data data;
	struct ovt_tcm_hcd *tcm_hcd = zeroflash_hcd->tcm_hcd;
	static unsigned int retry_count;
	const struct ovt_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);

	if (tcm_hcd->irq_enabled) {
		retval = tcm_hcd->enable_irq(tcm_hcd, false, true);
		if (retval < 0) {
			ovt_info(ERR_LOG, "%s:Failed to disable interrupt\n", __func__);
		}
	}

	ovt_info(INFO_LOG, "%s:Prepare F35 firmware download\n", __func__);

	if (tcm_hcd->id_info.mode == MODE_ROMBOOTLOADER) {
		ovt_info(ERR_LOG, "%s:Incorrect uboot type, exit\n", __func__);
		goto exit;
	}
	retval = zeroflash_check_uboot();
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to find valid uboot\n", __func__);
		goto exit;
	} else {
		ovt_info(INFO_LOG, "%s:Success to find valid uboot\n", __func__);
	}

	atomic_set(&tcm_hcd->host_downloading, 1);

	retval = ovt_tcm_rmi_read(tcm_hcd,
			zeroflash_hcd->f35_addr.data_base,
			(unsigned char *)&data,
			sizeof(data));
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to read F$35 data\n", __func__);
		goto exit;
	} else {
		ovt_info(INFO_LOG, "%s:Success to read F$35 data\n", __func__);
	}

	if (data.error_code != REQUESTING_FIRMWARE) {
		ovt_info(ERR_LOG, "%s:Microbootloader error code = 0x%02x\n", __func__,
				data.error_code);
		if (data.error_code != CHECKSUM_FAILURE) {
			retval = -EIO;
			goto exit;
		} else {
			retry_count++;
		}
	} else {
		retry_count = 0;
	}

	retval = zeroflash_get_fw_image();
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to get firmware image\n", __func__);
		goto exit;
	} else {
		ovt_info(INFO_LOG, "%s:Success to get firmware image\n", __func__);
	}

	ovt_info(INFO_LOG, "%s:Start of firmware download\n", __func__);

	/* perform firmware downloading */
	retval = zeroflash_download_app_fw();
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to download application firmware, so reset tp \n", __func__);
		goto exit;
	} else {
		ovt_info(INFO_LOG, "%s:Success to download application firmware\n", __func__);
	}

	ovt_info(INFO_LOG, "%s:End of firmware download\n", __func__);

exit:
	if (retval < 0) {
		gpio_set_value(bdata->reset_gpio, 0);
		usleep_range(5000, 5001);
		gpio_set_value(bdata->reset_gpio, 1);
		usleep_range(5000, 5001);
		retry_count++;
	}

	if (DOWNLOAD_RETRY_COUNT && retry_count > DOWNLOAD_RETRY_COUNT) {
		//retval = tcm_hcd->enable_irq(tcm_hcd, false, true);
		retval = tcm_hcd->enable_irq(tcm_hcd, true, true);
		if (retval < 0) {
			ovt_info(ERR_LOG, "%s:Failed to disable interrupt\n", __func__);
		}

		ovt_info(INFO_LOG, "%s:Interrupt is disabled\n", __func__);
	} else {
		retval = tcm_hcd->enable_irq(tcm_hcd, true, NULL);
		if (retval < 0) {
			ovt_info(ERR_LOG, "%s:Failed to enable interrupt\n", __func__);
		}
	}

	ovt_info(DEBUG_LOG, "%s exit!\n", __func__);
}

static void zeroflash_do_romboot_firmware_download(void)
{
	int retval;
	unsigned char *resp_buf = NULL;
	unsigned int resp_buf_size;
	unsigned int resp_length;
	unsigned int data_size_blocks;
	unsigned int image_size;
	struct ovt_tcm_hcd *tcm_hcd = zeroflash_hcd->tcm_hcd;
	/*zte_add*/
	const struct ovt_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);

	ovt_info(INFO_LOG, "%s:Prepare ROMBOOT firmware download\n", __func__);

	atomic_set(&tcm_hcd->host_downloading, 1);
	resp_buf = NULL;
	resp_buf_size = 0;

	if (!tcm_hcd->irq_enabled) {
		retval = tcm_hcd->enable_irq(tcm_hcd, true, NULL);
		if (retval < 0) {
			ovt_info(ERR_LOG, "%s:Failed to enable interrupt\n", __func__);
		}
	}

	pm_stay_awake(&tcm_hcd->pdev->dev);

	if (tcm_hcd->id_info.mode != MODE_ROMBOOTLOADER) {
		ovt_info(ERR_LOG, "%s:Not in romboot mode\n", __func__);
		atomic_set(&tcm_hcd->host_downloading, 0);
		goto exit;
	}

	retval = zeroflash_get_fw_image();
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to request romboot.img\n", __func__);
		goto exit;
	} else {
		ovt_info(INFO_LOG, "%s:Success to request romboot.img\n", __func__);
	}

	image_size = (unsigned int)zeroflash_hcd->image_info.app_firmware.size;

	ovt_info(INFO_LOG, "%s:image_size = %d\n", __func__, image_size);

	data_size_blocks = image_size / 16;

	LOCK_BUFFER(zeroflash_hcd->out);

	retval = ovt_tcm_alloc_mem(tcm_hcd,
			&zeroflash_hcd->out,
			image_size + RESERVED_BYTES);
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to allocate memory for application firmware\n", __func__);
		UNLOCK_BUFFER(zeroflash_hcd->out);
		goto exit;
	}

	zeroflash_hcd->out.buf[0] = zeroflash_hcd->image_info.app_firmware.size >> 16;

	retval = secure_memcpy(&zeroflash_hcd->out.buf[RESERVED_BYTES],
			zeroflash_hcd->image_info.app_firmware.size,
			zeroflash_hcd->image_info.app_firmware.data,
			zeroflash_hcd->image_info.app_firmware.size,
			zeroflash_hcd->image_info.app_firmware.size);
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to copy application firmware data\n", __func__);
		UNLOCK_BUFFER(zeroflash_hcd->out);
		goto exit;
	}

	ovt_info(INFO_LOG, "%s:data_size_blocks: %d\n", __func__, data_size_blocks);

	retval = tcm_hcd->write_message(tcm_hcd,
			CMD_ROMBOOT_DOWNLOAD,
			zeroflash_hcd->out.buf,
			image_size + RESERVED_BYTES,
			&resp_buf,
			&resp_buf_size,
			&resp_length,
			NULL,
			20);
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to write command ROMBOOT DOWNLOAD\n", __func__);
		UNLOCK_BUFFER(zeroflash_hcd->out);
		/*zte_add*/
		/*disable irq*/
		tcm_hcd->enable_irq(tcm_hcd, false, true);
		ovt_info(INFO_LOG, "%s:abnormal, force set reset %d low to high\n", __func__, bdata->reset_gpio);
		gpio_set_value(bdata->reset_gpio, 0);
		usleep_range(5000, 5001);
		gpio_set_value(bdata->reset_gpio, 1);
		usleep_range(5000, 5001);
		/*enable irq*/
		tcm_hcd->enable_irq(tcm_hcd, true, NULL);
		goto exit;
	} else {
		ovt_info(INFO_LOG, "%s:Success to write command ROMBOOT DOWNLOAD\n", __func__);
	}
	UNLOCK_BUFFER(zeroflash_hcd->out);

	retval = tcm_hcd->switch_mode(tcm_hcd, FW_MODE_BOOTLOADER);
	if (retval < 0) {
		ovt_info(ERR_LOG, "%s:Failed to switch to bootloader\n", __func__);
		/*zte_add*/
		/*disable irq*/
		tcm_hcd->enable_irq(tcm_hcd, false, true);
		ovt_info(INFO_LOG, "%s:abnormal, force set reset %d low to high\n", __func__, bdata->reset_gpio);
		gpio_set_value(bdata->reset_gpio, 0);
		usleep_range(5000, 5001);
		gpio_set_value(bdata->reset_gpio, 1);
		usleep_range(5000, 5001);
		/*enable irq*/
		tcm_hcd->enable_irq(tcm_hcd, true, NULL);
		goto exit;
	} else {
		ovt_info(INFO_LOG, "%s:Success to switch to bootloader\n", __func__);
	}

exit:
	pm_relax(&tcm_hcd->pdev->dev);

	kfree(resp_buf);
	ovt_info(DEBUG_LOG, "%s exit!\n", __func__);
}

static int zeroflash_init(struct ovt_tcm_hcd *tcm_hcd)
{
	int retval = 0;
	int idx;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);

	zeroflash_hcd = NULL;
	if (!(tcm_hcd->in_hdl_mode))
		return 0;

	zeroflash_hcd = kzalloc(sizeof(*zeroflash_hcd), GFP_KERNEL);
	if (!zeroflash_hcd) {
		ovt_info(ERR_LOG, "%s:Failed to allocate memory for zeroflash_hcd\n", __func__);
		return -ENOMEM;
	}

	zeroflash_hcd->tcm_hcd = tcm_hcd;
	zeroflash_hcd->image = NULL;
	zeroflash_hcd->has_hdl = false;
	zeroflash_hcd->f35_ready = false;
	zeroflash_hcd->has_open_short_config = false;

	INIT_BUFFER(zeroflash_hcd->out, false);
	INIT_BUFFER(zeroflash_hcd->resp, false);

	zeroflash_hcd->workqueue =
			create_singlethread_workqueue("ovt_tcm_zeroflash");
	INIT_WORK(&zeroflash_hcd->config_work,
			zeroflash_download_config_work);

	if (false == ENABLE_SYS_ZEROFLASH)
		goto init_finished;

	zeroflash_hcd->sysfs_dir = kobject_create_and_add(SYSFS_DIR_NAME,
			tcm_hcd->sysfs_dir);
	if (!zeroflash_hcd->sysfs_dir) {
		ovt_info(ERR_LOG, "%s:Failed to create sysfs directory\n", __func__);
		return -EINVAL;
	} else {
		ovt_info(INFO_LOG, "%s:Success to create sysfs directory\n", __func__);
	}

	for (idx = 0; idx < ARRAY_SIZE(attrs); idx++) {
		retval = sysfs_create_file(zeroflash_hcd->sysfs_dir,
				&(*attrs[idx]).attr);
		if (retval < 0) {
			ovt_info(ERR_LOG, "%s:Failed to create sysfs file\n", __func__);
		}
	}
	ovt_info(INFO_LOG, "%s:Success to create sysfs file\n", __func__);
init_finished:
	/* prepare the firmware download process */
	if (tcm_hcd->in_hdl_mode) {
		switch (tcm_hcd->sensor_type) {
		case TYPE_F35:
			zeroflash_do_f35_firmware_download();
			break;
		case TYPE_ROMBOOT:
			zeroflash_do_romboot_firmware_download();
			break;
		default:
			ovt_info(ERR_LOG, "%s:Failed to find valid HDL state (%d)\n", __func__,
					 tcm_hcd->sensor_type);
			break;

		}
	}

	ovt_info(DEBUG_LOG, "%s exit!\n", __func__);
	return retval;
}

static int zeroflash_remove(struct ovt_tcm_hcd *tcm_hcd)
{
	int idx;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);

	if (!zeroflash_hcd)
		goto exit;

	if (zeroflash_hcd->fw_entry)
		release_firmware(zeroflash_hcd->fw_entry);


	if (true == ENABLE_SYS_ZEROFLASH) {

		for (idx = 0; idx < ARRAY_SIZE(attrs); idx++) {
			sysfs_remove_file(zeroflash_hcd->sysfs_dir,
					&(*attrs[idx]).attr);
		}

		kobject_put(zeroflash_hcd->sysfs_dir);
	}


	cancel_work_sync(&zeroflash_hcd->config_work);
	flush_workqueue(zeroflash_hcd->workqueue);
	destroy_workqueue(zeroflash_hcd->workqueue);

	RELEASE_BUFFER(zeroflash_hcd->resp);
	RELEASE_BUFFER(zeroflash_hcd->out);

	kfree(zeroflash_hcd);
	zeroflash_hcd = NULL;

exit:
	complete(&zeroflash_remove_complete);

	ovt_info(DEBUG_LOG, "%s exit!\n", __func__);
	return 0;
}

static int zeroflash_syncbox(struct ovt_tcm_hcd *tcm_hcd)
{
	int retval;
	unsigned char *fw_status;
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);

	if (!zeroflash_hcd)
		return 0;

	ovt_info(INFO_LOG, "%s:tcm_hcd->report.id is 0x%x\n", __func__, tcm_hcd->report.id);
	switch (tcm_hcd->report.id) {
	case REPORT_STATUS:
		fw_status = (unsigned char *)&zeroflash_hcd->fw_status;

		retval = secure_memcpy(fw_status,
				sizeof(zeroflash_hcd->fw_status),
				tcm_hcd->report.buffer.buf,
				tcm_hcd->report.buffer.buf_size,
				sizeof(zeroflash_hcd->fw_status));

		if (retval < 0) {
			ovt_info(ERR_LOG, "%s:Failed to copy firmware status\n", __func__);
			return retval;
		}
		zeroflash_download_config();
		break;
	case REPORT_HDL_F35:
		zeroflash_do_f35_firmware_download();
		break;
	case REPORT_HDL_ROMBOOT:
		zeroflash_do_romboot_firmware_download();
		break;

	default:
		break;
	}

	ovt_info(DEBUG_LOG, "%s exit!\n", __func__);
	return 0;
}

static int zeroflash_reinit(struct ovt_tcm_hcd *tcm_hcd)
{
	int retval;

	if (!zeroflash_hcd && tcm_hcd->in_hdl_mode) {
		retval = zeroflash_init(tcm_hcd);
		return retval;
	}

	return 0;
}

static struct ovt_tcm_module_cb zeroflash_module = {
	.type = TCM_ZEROFLASH,
	.init = zeroflash_init,
	.remove = zeroflash_remove,
	.syncbox = zeroflash_syncbox,
#ifdef REPORT_NOTIFIER
	.asyncbox = NULL,
#endif
	.reinit = zeroflash_reinit,
	.suspend = NULL,
	.resume = NULL,
	.early_suspend = NULL,
};

int zeroflash_module_init(void)
{
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);

	return ovt_tcm_add_module(&zeroflash_module, true);
}

void zeroflash_module_exit(void)
{
	ovt_info(DEBUG_LOG, "%s enter!\n", __func__);

	ovt_tcm_add_module(&zeroflash_module, false);
	wait_for_completion(&zeroflash_remove_complete);
}

/*module_init(zeroflash_module_init);
module_exit(zeroflash_module_exit);*/

MODULE_AUTHOR("omnivision, Inc.");
MODULE_DESCRIPTION("omnivision TCM Zeroflash Module");
MODULE_LICENSE("GPL v2");
