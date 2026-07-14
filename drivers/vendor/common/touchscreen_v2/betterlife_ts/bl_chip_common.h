#ifndef BL_CHIP_COMMON_H
#define BL_CHIP_COMMON_H
#include "bl_chip_custom.h"
#include "ztp_common.h"

#define	BL8XXX_60	0x01	/* bl8105,bl8265,bl8335,bl8495,bl8810,bl8818h,bl8825,bl8858c,bl8868c */
#define	BL8XXX_61	0x02	/* bl8281,bl8331,bl8858s,bl8868s */
#define	BL8XXX_63	0x03	/* bl8668,bl8678,bl8818,bl8858h,bl8868h,bl8878 */
#define	BL6XX0		0x04	/* bl6090.bl6130,bl6280,bl6360 */
#define	BL6XX1		0x05	/* bl6391 */
#define	BL6XX3      0x06	/* bl6131z,bl6133u,blm13 */
#define	BL6XX6		0x07	/* bl7450 */
#define	BL7XX0		0x08	/* bl7450 */
#define	BL7XX1      0x09
#define	BL7XX3      0x0a

#define	BTL_FLASH_I2C_ADDR		    0x2c	/* 7bit addr */
#define	I2C_WRITE		0x00
#define	I2C_READ		0x01

#define SELF_CTP                    0x00
#define COMPATIBLE_CTP              0x01
#define SELF_INTERACTIVE_CTP        0x02

#define   INT_UPDATE_MODE           0x00
#define   I2C_UPDATE_MODE           0x01

#define   FLASH_WSIZE			    128
#define   FLASH_RSIZE			    128
#define   PROJECT_INFO_LEN          8	/* ProjectInfo */

#define ARGU_MARK  "chp_cfg_mark"

#if (TS_CHIP == BL8XXX_60)
#define	BTL_ARGUMENT_BASE_OFFSET	0x200
#define	MAX_POINT_NUM	2
#define	BTL_FLASH_ID	0x05
#define	MAX_FLASH_SIZE	0x8000
#define	PJ_ID_OFFSET	0xcb

#define	VERTIFY_START_OFFSET 0x3fc
#define	VERTIFY_END_OFFSET   0x3fd

#define	FW_CHECKSUM_DELAY_TIME	100

#define	CTP_TYPE                COMPATIBLE_CTP
#define	UPDATE_MODE             INT_UPDATE_MODE
enum BL8XXX_60_flash_cmd {
		ERASE_SECTOR_MAIN_CMD = 0x06,
		ERASE_ALL_MAIN_CMD = 0x09,
		RW_REGISTER_CMD = 0x0a,
		READ_MAIN_CMD = 0x0D,
		WRITE_MAIN_CMD = 0x0F,
		WRITE_RAM_CMD = 0x11,
		READ_RAM_CMD = 0x12,
};

#elif (TS_CHIP == BL8XXX_61)
#define	BTL_ARGUMENT_BASE_OFFSET	0x200
#define	MAX_POINT_NUM	5
#define	BTL_FLASH_ID	0x06
#define	MAX_FLASH_SIZE	0x8000
#define	PJ_ID_OFFSET	0xcb

#define	VERTIFY_START_OFFSET 0x3fc
#define	VERTIFY_END_OFFSET   0x3fd

#define	FW_CHECKSUM_DELAY_TIME	100

#define	CTP_TYPE                COMPATIBLE_CTP
#define	UPDATE_MODE             INT_UPDATE_MODE
enum BL8XXX_61_flash_cmd {
		ERASE_SECTOR_MAIN_CMD = 0x06,
		ERASE_ALL_MAIN_CMD = 0x09,
		RW_REGISTER_CMD = 0x0a,
		READ_MAIN_CMD = 0x0D,
		WRITE_MAIN_CMD = 0x0F,
		WRITE_RAM_CMD = 0x11,
		READ_RAM_CMD = 0x12,
};

#elif (TS_CHIP == BL8XXX_63)
#define	BTL_ARGUMENT_BASE_OFFSET	0x200
#define	MAX_POINT_NUM	2
#define	BTL_FLASH_ID	0x04
#define	MAX_FLASH_SIZE	0xc000
#define	PJ_ID_OFFSET	0xcb

#define VERTIFY_START_OFFSET 0x3fc
#define VERTIFY_END_OFFSET   0x3fd

#define	FW_CHECKSUM_DELAY_TIME	250

#define CTP_TYPE                COMPATIBLE_CTP
#define UPDATE_MODE             INT_UPDATE_MODE
enum BL8XXX_63_flash_cmd {
		ERASE_SECTOR_MAIN_CMD = 0x06,
		ERASE_ALL_MAIN_CMD = 0x09,
		RW_REGISTER_CMD = 0x0a,
		READ_MAIN_CMD = 0x0D,
		WRITE_MAIN_CMD = 0x0F,
		WRITE_RAM_CMD = 0x11,
		READ_RAM_CMD = 0x12,
};

#elif (TS_CHIP == BL6XX0)
#define	BTL_ARGUMENT_BASE_OFFSET	0x200
#define	MAX_POINT_NUM	2
#define	BTL_FLASH_ID	0x20
#define	MAX_FLASH_SIZE	0x8000
#define	PJ_ID_OFFSET	0xcb

#define VERTIFY_START_OFFSET 0x3fc
#define VERTIFY_END_OFFSET   0x3fd

#define	FW_CHECKSUM_DELAY_TIME	250

#define CTP_TYPE                SELF_CTP
#define UPDATE_MODE             INT_UPDATE_MODE
enum BL6XX0_flash_cmd {
		ERASE_SECTOR_MAIN_CMD = 0x06,
		ERASE_ALL_MAIN_CMD = 0x09,
		RW_REGISTER_CMD = 0x0a,
		READ_MAIN_CMD = 0x0D,
		WRITE_MAIN_CMD = 0x0F,
		WRITE_RAM_CMD = 0x11,
		READ_RAM_CMD = 0x12,
};

#elif (TS_CHIP == BL6XX1)
#define	BTL_ARGUMENT_BASE_OFFSET	0x200
#define	MAX_POINT_NUM	2
#define	BTL_FLASH_ID	0x21
#define	MAX_FLASH_SIZE	0x8000
#define	PJ_ID_OFFSET	0xcb

#define VERTIFY_START_OFFSET 0x3fc
#define VERTIFY_END_OFFSET   0x3fd

#define	FW_CHECKSUM_DELAY_TIME	250

#define CTP_TYPE                SELF_CTP
#define UPDATE_MODE             INT_UPDATE_MODE
enum BL6XX1_flash_cmd {
		ERASE_SECTOR_MAIN_CMD = 0x06,
		ERASE_ALL_MAIN_CMD = 0x09,
		RW_REGISTER_CMD = 0x0a,
		READ_MAIN_CMD = 0x0D,
		WRITE_MAIN_CMD = 0x0F,
		WRITE_RAM_CMD = 0x11,
		READ_RAM_CMD = 0x12,
};

#elif (TS_CHIP == BL6XX3)
#define	BTL_ARGUMENT_BASE_OFFSET	0x200
#define	MAX_POINT_NUM	1
#define	BTL_FLASH_ID	0x22
#define	MAX_FLASH_SIZE	0x8000
#define	PJ_ID_OFFSET	0xcb

#define VERTIFY_START_OFFSET 0x3fc
#define VERTIFY_END_OFFSET   0x3fd

#define	FW_CHECKSUM_DELAY_TIME	250

#define CTP_TYPE                SELF_CTP
#define UPDATE_MODE             INT_UPDATE_MODE
enum BL6XX3_flash_cmd {

	ERASE_SECTOR_MAIN_CMD = 0x06,
	ERASE_ALL_MAIN_CMD = 0x09,
	RW_REGISTER_CMD = 0x0a,
	READ_MAIN_CMD = 0x0D,
	WRITE_MAIN_CMD = 0x0F,
	WRITE_RAM_CMD = 0x11,
	READ_RAM_CMD = 0x12,
};

#elif (TS_CHIP == BL6XX6)
#define	BTL_ARGUMENT_BASE_OFFSET	0xfc00
#define	MAX_POINT_NUM	2
#define	BTL_FLASH_ID	0x42
#define	MAX_FLASH_SIZE	0x10000
#define	PJ_ID_OFFSET	0xcb

#define VERTIFY_START_OFFSET 0x23
#define VERTIFY_END_OFFSET   0x24

#define	FW_CHECKSUM_DELAY_TIME	250

#define CTP_TYPE                SELF_CTP
#define UPDATE_MODE             I2C_UPDATE_MODE
enum BL6XX6_flash_cmd {
		ERASE_SECTOR_MAIN_CMD = 0x06,
		ERASE_ALL_MAIN_CMD = 0x09,
		RW_REGISTER_CMD = 0x0a,
		READ_MAIN_CMD = 0x0D,
		WRITE_MAIN_CMD = 0x0F,
		WRITE_RAM_CMD = 0x11,
		READ_RAM_CMD = 0x12,
};

#elif (TS_CHIP == BL7XX0)
#define	BTL_ARGUMENT_BASE_OFFSET	0xbc00
#define	MAX_POINT_NUM	5
#define	BTL_FLASH_ID	0x40
#define	MAX_FLASH_SIZE	0xc000
#define	PJ_ID_OFFSET	0xcb

#define VERTIFY_START_OFFSET 0x23
#define VERTIFY_END_OFFSET   0x24

#define	FW_CHECKSUM_DELAY_TIME	250

#define CTP_TYPE             SELF_INTERACTIVE_CTP
#define UPDATE_MODE             I2C_UPDATE_MODE
enum BL7XX0_flash_cmd {
		ERASE_SECTOR_MAIN_CMD = 0x06,
		ERASE_ALL_MAIN_CMD = 0x09,
		RW_REGISTER_CMD = 0x0a,
		READ_MAIN_CMD = 0x0D,
		WRITE_MAIN_CMD = 0x0F,
		WRITE_RAM_CMD = 0x11,
		READ_RAM_CMD = 0x12,
};

#elif (TS_CHIP == BL7XX1)
#define	BTL_ARGUMENT_BASE_OFFSET	0xfc00
#define	MAX_POINT_NUM	10
#define	BTL_FLASH_ID	0x41
#define	MAX_FLASH_SIZE	0x10000
#define	PJ_ID_OFFSET	0xcb

#define VERTIFY_START_OFFSET 0x23
#define VERTIFY_END_OFFSET   0x24

#define	FW_CHECKSUM_DELAY_TIME	250

#define CTP_TYPE             SELF_INTERACTIVE_CTP
#define UPDATE_MODE             I2C_UPDATE_MODE
enum BL7XX1_flash_cmd {
		ERASE_SECTOR_MAIN_CMD = 0x06,
		ERASE_ALL_MAIN_CMD = 0x09,
		RW_REGISTER_CMD = 0x0a,
		READ_MAIN_CMD = 0x0D,
		WRITE_MAIN_CMD = 0x0F,
		WRITE_RAM_CMD = 0x11,
		READ_RAM_CMD = 0x12,
};

#elif (TS_CHIP == BL7XX3)
#define	BTL_ARGUMENT_BASE_OFFSET	0xfc00
#define	MAX_POINT_NUM	10
#define	BTL_FLASH_ID	0x42
#define	MAX_FLASH_SIZE	0x10000
#define	PJ_ID_OFFSET	0xcb

#define VERTIFY_START_OFFSET 0x23
#define VERTIFY_END_OFFSET   0x24

#define	FW_CHECKSUM_DELAY_TIME	250

#define CTP_TYPE             SELF_INTERACTIVE_CTP
#define UPDATE_MODE          I2C_UPDATE_MODE
enum BL7XX3_flash_cmd {
		ERASE_SECTOR_MAIN_CMD = 0x06,
		ERASE_ALL_MAIN_CMD = 0x09,
		RW_REGISTER_CMD = 0x0a,
		READ_MAIN_CMD = 0x0D,
		WRITE_MAIN_CMD = 0x0F,
		WRITE_RAM_CMD = 0x11,
		READ_RAM_CMD = 0x12,
};

#endif
enum fw_reg {
	WORK_MODE_REG = 0x00,
	TS_DATA_REG = 0x01,
	CHECKSUM_REG = 0x3f,
	CHECKSUM_CAL_REG = 0x8a,
	AC_REG = 0x8b,
	RESOLUTION_REG = 0x98,
	LPM_REG = 0xa5,
	PROXIMITY_REG = 0xb0,
	PROXIMITY_FLAG_REG = 0xB1,
	CALL_REG = 0xb2,
	BTL_CHIP_ID_REG = 0xb8,
	BTL_FWVER_PJ_ID_REG = 0xb6,
	BTL_PRJ_INFO_REG = 0xb4,
	CHIP_ID_REG = 0xe7,
	BTL_PRJ_ID_REG = 0xb5,
	COB_ID_REG = 0x33,
	BTL_PROTECT_REG = 0xee,
	BTL_ESD_REG = 0xf9,
	BTL_GESTURE_REG = 0xd0,
#if (CTP_TYPE == SELF_CTP)
	BTL_CHANNEL_RX_REG   = 0x14,
	BTL_CHANNEL_KEY_REG  = 0x15,
#endif
#if (CTP_TYPE == SELF_INTERACTIVE_CTP)
	BTL_CHANNEL_RX_REG   = 0x24,
	BTL_CHANNEL_TX_REG  = 0x23,
    #endif

};
enum work_mode {
	NORMAL_MODE = 0x00,
	FACTORY_MODE = 0x40,
};
enum lpm {
	ACTIVE_MODE = 0x00,
	MONITOR_MODE = 0x01,
	STANDBY_MODE = 0x02,
	SLEEP_MODE = 0x03,
	GESTURE_MODE = 0x04,
};
enum checksum {
	CHECKSUM_READY = 0x01,
	CHECKSUM_CAL = 0xaa,
	CHECKSUM_ARG = 0xba,
};
enum update_type {
	NONE_UPDATE = 0x00,
	FW_ARG_UPDATE = 0x01,
};
enum firmware_file_type {
	HEADER_FILE_UPDATE = 0x00,
	BIN_FILE_UPDATE = 0x01,
	FIRMWARE_UPDATE = 0x02,
};
typedef enum {
	CTP_DOWN = 0,
	CTP_UP = 1,
	CTP_MOVE = 2,
	CTP_RESERVE = 3,
} ctp_pen_state_enum;

#define  TD_STAT_ADDR		    0x1
#define  TD_STAT_NUMBER_TOUCH	0x07
#define  CTP_PATTERN	            0xAA

#define OUTPUT	1
#define INPUT	0

#ifdef BTL_UPDATE_FIRMWARE_ENABLE
#define		BTL_FWVER_MAIN_OFFSET	(0x2a)
#define		BTL_FWVER_ARGU_OFFSET	(0x2b)
#define		BTL_PROJECT_ID_OFFSET	(0x2c)
#define     BTL_PROJECT_INFO_OFFSET  (0x0f)
#define     BTL_COB_ID_OFFSET        (0x34)
#define     BTL_COB_ID_LEN           (12)
#define     BTL_ARGUMENT_FLASH_SIZE  (1024)
#define     PRJ_INFO_LEN            (0x08)
#define     FLASH_PAGE_SIZE         (512)
/* Update firmware through driver probe procedure with h and c file */
/* #define       BTL_AUTO_UPDATE_FARMWARE */
#ifdef BTL_AUTO_UPDATE_FARMWARE

#endif

/* Update firmware through adb */
/* #define     BTL_UPDATE_FARMWARE_WITH_BIN */
#ifdef BTL_UPDATE_FARMWARE_WITH_BIN
#define BTL_FIRMWARE_BIN_PATH    "/sdcard/BTL.bin"
#endif
/* Update firmware through request firmware interface */

#endif

/*************Betterlife ic virtrual key***********/
#if defined(BTL_VIRTRUAL_KEY_SUPPORT)
#define BTL_VIRTRUAL_KEY_NUM     3
#define BTL_VIRTRUAL_KEY_DATA	 {\
									{KEY_MENU, 80, 900},\
									{KEY_HOMEPAGE, 240, 900},\
									{KEY_BACK, 400, 900} }
#endif

/*************Betterlife ic sysfs virtrual key***********/
#if defined(BTL_DEBUGFS_SUPPORT)
#define     DATA_BUFFER_LEN     (1024 * 80 * 5)
#endif

/*************Betterlife ic sysfs virtrual key***********/
#if defined(BTL_SYSFS_VIRTRUAL_KEY_SUPPORT)
#define     VIRTRUAL_KEY_SYSFS_PATH     "board_properties"
#endif

/*************Betterlife ic gesture ***********/
#if defined(BTL_GESTURE_SUPPORT)
#define	BTL_U_LEFT					0x0D
#define	BTL_U_RIGHT					0x0E
#define	BTL_U_UP						0x0F
#define	BTL_U_DOWN					0x10
#define	BTL_MOVE_UP					0x15
#define	BTL_MOVE_DOWN				0x02
#define	BTL_MOVE_LEFT				0x03
#define	BTL_MOVE_RIGHT				0x04
#define	BTL_O_GEST					0x07
#define	BTL_E_GEST					0x08
#define	BTL_M_GEST					0x06
#define	BTL_W_GEST					0x05
#define	BTL_S_GEST					0x0a
#define	BTL_V_LEFT					0x11
#define	BTL_V_RIGHT					0x12
#define	BTL_V_UP						0x13
#define	BTL_V_DOWN					0x14
#define	BTL_C_GEST					0x09
#define	BTL_Z_GEST					0x0b
#define	BTL_DOUBLE_CLICK			0x0c
#define	BTL_SINGLE_CLICK            0xcc

#define	BTL_KEY_GESTURE_U			KEY_U
#define	BTL_KEY_GESTURE_UP		KEY_UP
#define	BTL_KEY_GESTURE_DOWN	KEY_DOWN
#define	BTL_KEY_GESTURE_LEFT		KEY_LEFT
#define	BTL_KEY_GESTURE_RIGHT	KEY_RIGHT
#define	BTL_KEY_GESTURE_O		KEY_O
#define	BTL_KEY_GESTURE_E			KEY_E
#define	BTL_KEY_GESTURE_M		KEY_M
#define	BTL_KEY_GESTURE_L			KEY_L
#define	BTL_KEY_GESTURE_W		KEY_W
#define	BTL_KEY_GESTURE_S			KEY_S
#define	BTL_KEY_GESTURE_V			KEY_V
#define	BTL_KEY_GESTURE_C			KEY_C
#define	BTL_KEY_GESTURE_Z			KEY_Z
#define	BTL_KEY_GSTURE_WAKE		KEY_POWER
#endif

/*************Betterlife ic proximity support ***********/
#if defined(BTL_PROXIMITY_SUPPORT)
#define	BTL_CTP_PROXIMITY_MODE_REG		0xB0
#define	BTL_CTP_PROXIMITY_FLAG_REG		0xB1
#define	BTL_CTP_PROXIMITY_NEAR			0xC0
#define	BTL_CTP_PROXIMITY_LEAVE			0xE0

#define LTR_IOCTL_MAGIC 0x1C
#define LTR_IOCTL_GET_PFLAG _IOR(LTR_IOCTL_MAGIC, 1, int)
#define LTR_IOCTL_GET_LFLAG _IOR(LTR_IOCTL_MAGIC, 2, int)
#define LTR_IOCTL_SET_PFLAG _IOW(LTR_IOCTL_MAGIC, 3, int)
#define LTR_IOCTL_SET_LFLAG _IOW(LTR_IOCTL_MAGIC, 4, int)
#define LTR_IOCTL_GET_DATA _IOW(LTR_IOCTL_MAGIC, 5, unsigned char)
#endif

/*************Betterlife ic ESD protect ***********/
#if defined(BTL_ESD_PROTECT_SUPPORT) || defined(BTL_CHARGE_PROTECT_SUPPORT)
#define	SWITCH_OFF		(0)
#define	SWITCH_ON		(1)
#endif

/*************Betterlife driver suspend mode select ***********/
#if defined(BTL_SUSPEND_MODE)
#define     BTL_SYSFS_SUSPEND_SUPPORT
/* #define     BTL_EARLYSUSPEND_SUPPORT */
/* #define     BTL_FB_SUPPORT */
/* #define     BTL_PM_SUPPORT */
/* #define     BTL_ADF_SUPPORT */
#endif

/*************Betterlife driver with BTL_CONFIG_OF close ***********/
#if !defined(BTL_CONFIG_OF)
#define BTL_RST_PORT    16	/* S5PV210_GPJ3(6) */
#define BTL_INT_PORT    17	/* S5PV210_GPH1(3) */
#if defined(BTL_POWER_CONTROL_SUPPORT)
#if defined(BTL_CUSTOM_VCC_LDO_SUPPORT)
#define BTL_VCC_PORT	18
#endif
#if defined(BTL_CUSTOM_IOVCC_LDO_SUPPORT)
#define BTL_IOVCC_PORT	19
#endif
#endif

#define     TPD_RES_X        240
#define     TPD_RES_Y        240
#endif

/*************Betterlife factory test ***********/
#if defined(BTL_FACTORY_TEST_EN)
#define FILE_NAME_LENGTH               128
#endif

/*************Betterlife factory test ***********/
#if defined(BTL_APK_SUPPORT)
enum apk_cmd {
	WRITE_CMD = 0x00,
	READ_CMD = 0x01,
	WAKE_CMD = 0x02,
	INTERRUPT_CMD = 0x03,
	DRIVER_INFO = 0x04,
	I2C_LOCK_CMD = 0x05,
};

#define FRAME_CMD_LEN       0x04
#define BTL_APK_PROC_DIRNAME    "Betterlife_ts"
#endif

/*************Betterlife ic debug***********/
#if defined(BTL_DEBUG_SUPPORT)
extern bool LogEn;

#define BTL_TAG                        "[btl_tp]"
#define BTL_ERROR(fmt, arg...) do {\
					if (LogEn)\
						pr_err(BTL_TAG "<%s><%d>" fmt "\n", __func__, __LINE__, ##arg);\
					} while (0)
#define BTL_DEBUG(fmt, arg...) do {\
					if (LogEn)\
						pr_notice(BTL_TAG "<%s><%d>" fmt "\n", __func__, __LINE__, ##arg);\
					} while (0)
#define BTL_DEBUG_FUNC() do {\
							if (LogEn)\
							pr_notice(BTL_TAG "<%s><%d>\n", __func__, __LINE__);\
						} while (0)
#else
#define BTL_ERROR(fmt, arg...)
#define BTL_DEBUG(fmt, arg...)
#define BTL_DEBUG_FUNC()
#endif

#endif
