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
/* #define CONFIG_TOUCH_DRIVER_RUN_ON_MTK_PLATFORM */
/* #define CONFIG_TOUCH_DRIVER_RUN_ON_RK_PLATFORM */
/* #define CONFIG_TOUCH_DRIVER_RUN_ON_SPRD_PLATFORM */
#define CONFIG_TOUCH_DRIVER_RUN_ON_QCOM_PLATFORM

/*
 * Note.
 * The below compile option is used to enable the chip type used
 */
/* #define CONFIG_ENABLE_CHIP_TYPE_GC7371 */
/* #define CONFIG_ENABLE_CHIP_TYPE_GC7271 */
/* #define CONFIG_ENABLE_CHIP_TYPE_GC7202 */
/* #define CONFIG_ENABLE_CHIP_TYPE_GC7372 */
/* #define CONFIG_ENABLE_CHIP_TYPE_GC7302 */
#define CONFIG_ENABLE_CHIP_TYPE_GC7202H

/*
 * Note.
 * The below compile option is used to enable the IC interface used
 */
/* #define CONFIG_TOUCH_DRIVER_INTERFACE_I2C */
#define CONFIG_TOUCH_DRIVER_INTERFACE_SPI

/*
 * Note.
 * The below compile option is used to enable the fw download type
 * Hostdownload for 0 flash,flashdownload for 1 flash
 */
#define CONFIG_GCORE_AUTO_UPDATE_FW_HOSTDOWNLOAD
/* #define CONFIG_GCORE_AUTO_UPDATE_FW_FLASHDOWNLOAD */

/*
 * Note.
 * The below compile option is used to enable function transfer fw raw data to App or Tool
 * By default,this compile option is disable
 * Connect to App or Tool function need another interface module, if needed, contact to our colleague
 */
#define CONFIG_ENABLE_FW_RAWDATA

/*
 * Note.
 * The below compile option is used to enable enable/disable multi-touch procotol for reporting touch point to input sub-system
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
#define CONFIG_ENABLE_REPORT_LOG  (0)

/*
 * Note.
 * The below compile option is used to enable GESTURE WAKEUP function
 */
#define CONFIG_ENABLE_GESTURE_WAKEUP

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

#define CONFIG_GCORE_HOSTDOWNLOAD_ESD_PROTECT

#define CONFIG_MTK_SPI_MULTUPLE_1024


#define GCORE_WDT_RECOVERY_ENABLE  1
#define GCORE_WDT_TIMEOUT_PERIOD   2500
#define MAX_FW_RETRY_NUM          2
/*
 * LCD Resolution
 */


#define TOUCH_SCREEN_X_MAX            (720)
#define TOUCH_SCREEN_Y_MAX            (1600)
#define GKI_CHECK
#define GTP_LCD_FPS_NOTIFY
#define GTP_REPORT_BY_ZTE_ALGO
/* #define GTP_GET_NOISE */
#define GTP_VENDOR_ID_0 0x61105
#define GTP_VENDOR_ID_1 0
#define GTP_VENDOR_ID_2 0
#define GTP_VENDOR_ID_3 0

#define GTP_VENDOR_0_NAME                         "skyworth"
#define GTP_VENDOR_1_NAME                          "unknown"
#define GTP_VENDOR_2_NAME                         "unknown"
#define GTP_VENDOR_3_NAME                         "unknown"

#define GCORE_DEFAULT_FIRMWARE        "gcore_6_517_default_firmware"

#endif
