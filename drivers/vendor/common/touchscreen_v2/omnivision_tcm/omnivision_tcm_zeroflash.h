#ifndef _OMNIVISION_TCM_ZEROFLASH_H_
#define _OMNIVISION_TCM_ZEROFLASH_H_

#include <linux/gpio.h>
#include <linux/crc32.h>
#include <linux/firmware.h>

enum f35_error_code {
	SUCCESS = 0,
	UNKNOWN_FLASH_PRESENT,
	MAGIC_NUMBER_NOT_PRESENT,
	INVALID_BLOCK_NUMBER,
	BLOCK_NOT_ERASED,
	NO_FLASH_PRESENT,
	CHECKSUM_FAILURE,
	WRITE_FAILURE,
	INVALID_COMMAND,
	IN_DEBUG_MODE,
	INVALID_HEADER,
	REQUESTING_FIRMWARE,
	INVALID_CONFIGURATION,
	DISABLE_BLOCK_PROTECT_FAILURE,
};

enum config_download {
	HDL_INVALID = 0,
	HDL_TOUCH_CONFIG,
	HDL_DISPLAY_CONFIG,
	HDL_OPEN_SHORT_CONFIG,
};

struct area_descriptor {
	unsigned char magic_value[4];
	unsigned char id_string[16];
	unsigned char flags[4];
	unsigned char flash_addr_words[4];
	unsigned char length[4];
	unsigned char checksum[4];
};

struct block_data {
	const unsigned char *data;
	unsigned int size;
	unsigned int flash_addr;
};

struct image_info {
	unsigned int packrat_number;
	struct block_data boot_config;
	struct block_data app_firmware;
	struct block_data app_config;
	struct block_data disp_config;
	struct block_data open_short_config;
};

struct image_header {
	unsigned char magic_value[4];
	unsigned char num_of_areas[4];
};

struct rmi_f35_query {
	unsigned char version:4;
	unsigned char has_debug_mode:1;
	unsigned char has_data5:1;
	unsigned char has_query1:1;
	unsigned char has_query2:1;
	unsigned char chunk_size;
	unsigned char has_ctrl7:1;
	unsigned char has_host_download:1;
	unsigned char has_spi_master:1;
	unsigned char advanced_recovery_mode:1;
	unsigned char reserved:4;
} __packed;

struct rmi_f35_data {
	unsigned char error_code:5;
	unsigned char recovery_mode_forced:1;
	unsigned char nvm_programmed:1;
	unsigned char in_recovery:1;
} __packed;

struct rmi_pdt_entry {
	unsigned char query_base_addr;
	unsigned char command_base_addr;
	unsigned char control_base_addr;
	unsigned char data_base_addr;
	unsigned char intr_src_count:3;
	unsigned char reserved_1:2;
	unsigned char fn_version:2;
	unsigned char reserved_2:1;
	unsigned char fn_number;
} __packed;

struct rmi_addr {
	unsigned short query_base;
	unsigned short command_base;
	unsigned short control_base;
	unsigned short data_base;
};

struct firmware_status {
	unsigned short invalid_static_config:1;
	unsigned short need_disp_config:1;
	unsigned short need_app_config:1;
	unsigned short hdl_version:4;
	unsigned short need_open_short_config:1;
	unsigned short reserved:8;
} __packed;

struct zeroflash_hcd {
	bool has_hdl;
	bool f35_ready;
	bool has_open_short_config;
	const unsigned char *image;
	unsigned char *buf;
	const struct firmware *fw_entry;
	struct work_struct config_work;
	struct workqueue_struct *workqueue;
	struct kobject *sysfs_dir;
	struct rmi_addr f35_addr;
	struct image_info image_info;
	struct firmware_status fw_status;
	struct ovt_tcm_buffer out;
	struct ovt_tcm_buffer resp;
	struct ovt_tcm_hcd *tcm_hcd;
};

#endif /* _OMNIVISION_TCM_ZEROFLASH_H_ */
