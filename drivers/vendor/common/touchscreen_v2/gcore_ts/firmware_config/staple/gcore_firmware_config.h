/************************************************************************
*
* File Name: gcore_firmware_config.h
*
*  *   Version: v1.0
*
************************************************************************/
#ifndef _GCORE_FIRMWARE_CONFIG_H_
#define _GCORE_FIRMWARE_CONFIG_H_

/*----------------------------------------------------------*/
/* COMPILE OPTION DEFINITION                                */
/*----------------------------------------------------------*/
/*
 * Note.
 * The below compile option is used to enable the specific platform this driver running on.
 */
/*#define CONFIG_TOUCH_DRIVER_RUN_ON_MTK_PLATFORM */
/* #define CONFIG_TOUCH_DRIVER_RUN_ON_RK_PLATFORM */
/* #define CONFIG_TOUCH_DRIVER_RUN_ON_SPRD_PLATFORM */
#define CONFIG_TOUCH_DRIVER_RUN_ON_QCOM_PLATFORM
/*
 * Note.
 * The below compile option is used to enable the chip type used
 */
/* #define CONFIG_ENABLE_CHIP_TYPE_GC7371 */
/* #define CONFIG_ENABLE_CHIP_TYPE_GC7271 */
#define CONFIG_ENABLE_CHIP_TYPE_GC7202
/* #define CONFIG_ENABLE_CHIP_TYPE_GC7372 */
/*
 * Note.
 * The below compile option is used to enable the IC interface used
 */
#define CONFIG_TOUCH_DRIVER_INTERFACE_I2C
/*#define CONFIG_TOUCH_DRIVER_INTERFACE_SPI */
/*
 * Note.
 * The below compile option is used to enable the fw download type
 * Hostdownload for 0 flash,flashdownload for 1 flash
 * FW download function need another fw_update module, if needed, contact to our colleague
 */
/*#define CONFIG_GCORE_AUTO_UPDATE_FW_HOSTDOWNLOAD */
#define CONFIG_GCORE_AUTO_UPDATE_FW_FLASHDOWNLOAD
/*
 * Note.
 * The below compile option is used to enable function transfer fw raw data to App or Tool
 * By default,this compile option is disable
 * Connect to App or Tool function need another interface module, if needed, contact to our colleague
 */
#define CONFIG_ENABLE_FW_RAWDATA
/*
 * Note.
 * The below compile option is used to enable enable/disable multi-touch procotol
	for reporting touch point to input sub-system
 * If this compile option is defined, Type B procotol is enabled
 * Else, Type A procotol is enabled
 * By default, this compile option is enable
 */
#define CONFIG_ENABLE_TYPE_B_PROCOTOL
/*
 * Note.
 * The below compile option is used to enable/disable log output for touch driver
 * If is set as 1, the log output is enabled
 * By default, the debug log is set as 0
 */
#define CONFIG_ENABLE_DEBUG_LOG  (1)
/*
 * Note.
 * The below compile option is used to enable GESTURE WAKEUP function
 */
#define CONFIG_ENABLE_GESTURE_WAKEUP
/*
 * Note.
 * The below compile option is used for GESTURE WAKEUP special int,
 * Only for sprd platform
 */
/* #define CONFIG_GESTURE_SPECIAL_INT */
/*
 * Note.
 * The below compile option is used to enable update firmware by bin file
 * Else, the firmware is from array in driver
 */
#define CONFIG_UPDATE_FIRMWARE_BY_BIN_FILE
/*
 * Note.
 * This compile option is used for MTK platform only
 * This compile option is used for MTK legacy platform different
 */
/* #define CONFIG_MTK_LEGACY_PLATFORM */
/* #define CONFIG_GCORE_HOSTDOWNLOAD_ESD_PROTECT */
/* #define CONFIG_MTK_SPI_MULTUPLE_1024 */

/*
 * LCD Resolution
 */
#define GCORE_WDT_RECOVERY_ENABLE
#define GCORE_WDT_TIMEOUT_PERIOD   1500
#define TOUCH_SCREEN_X_MAX            (720)
#define TOUCH_SCREEN_Y_MAX            (1600)

#define GTP_REPORT_BY_ZTE_ALGO
#ifdef GTP_REPORT_BY_ZTE_ALGO
#define gtp_left_edge_limit_v		6
#define gtp_right_edge_limit_v		6
#define gtp_left_edge_limit_h		6
#define gtp_right_edge_limit_h		6
#define gtp_left_edge_long_pess_v		20
#define gtp_right_edge_long_pess_v	20
#define gtp_left_edge_long_pess_h		40
#define gtp_right_edge_long_pess_h	30
#define gtp_long_press_max_count		90
#define gtp_edge_long_press_check 1
#endif

#define GTP_LCD_FPS_NOTIFY
/* #define GTP_GET_NOISE */
#define GTP_VENDOR_ID_0 0x30705
#define GTP_VENDOR_ID_1 0x30b05
#define GTP_VENDOR_ID_2 0
#define GTP_VENDOR_ID_3 0

#define GTP_VENDOR_0_NAME                         "skyworth_panda"
#define GTP_VENDOR_1_NAME                         "skyworth"
#define GTP_VENDOR_2_NAME                         "unknown"
#define GTP_VENDOR_3_NAME                         "unknown"
#endif
