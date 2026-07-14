#ifndef BL_CHIP_CUSTOM_H
#define BL_CHIP_CUSTOM_H
#define     TS_CHIP          BL6XX6
#define     BTL_CHIP_NAME    "BL6416"
#define     BTL_THREADED_IRQ
#define     CTP_SLAVE_ADDR		0x2c
/* #define     BTL_POWER_CONTROL_SUPPORT */
#define     BTL_CHECK_CHIPID
#define     RESET_PIN_WAKEUP
/* #define     INT_PIN_WAKEUP */
#define     BTL_UPDATE_FIRMWARE_ENABLE
#define     BTL_DEBUG_SUPPORT
#define     BTL_CONFIG_OF
/* #define     BTL_CTP_PRESSURE */
#define     BTL_CTP_SUPPORT_TYPEB_PROTOCOL
/* #define     BTL_VIRTRUAL_KEY_SUPPORT */
/* #define     BTL_SYSFS_VIRTRUAL_KEY_SUPPORT */
/* #define     BTL_GESTURE_SUPPORT */
#define     BTL_PROXIMITY_SUPPORT
#define     HUB_TP_PS_ENABLE 1
#define     BTL_ESD_PROTECT_SUPPORT
#define     BTL_CHARGE_PROTECT_SUPPORT
/* #define     BTL_FACTORY_TEST_EN */
#define     BTL_SUSPEND_MODE
#define     BTL_DEBUGFS_SUPPORT
#define     BTL_APK_SUPPORT
#ifdef BTL_POWER_CONTROL_SUPPORT
/* #define     BTL_VCC_SUPPORT */
#if defined(BTL_VCC_SUPPORT)
/* #define     BTL_VCC_LDO_SUPPORT */
/* #define     BTL_CUSTOM_VCC_LDO_SUPPORT */
#endif
/* #define     BTL_IOVCC_SUPPORT */
#if defined(BTL_IOVCC_SUPPORT)
/* #define     BTL_IOVCC_LDO_SUPPORT */
/* #define     BTL_CUSTOM_IOVCC_LDO_SUPPORT */
#endif
#endif
/*************Betterlife module info***********/
#define BTL_FACTORY_TEST_EN
#define BTL_UPDATE_FIRMWARE_WITH_REQUEST_FIRMWARE
#define BTL_REPORT_BY_ZTE_ALGO
#define BTL_VENDOR_ID_0 0
#define BTL_VENDOR_ID_1 1
#define BTL_VENDOR_ID_2 2
#define BTL_VENDOR_ID_3 3

#define BTL_VENDOR_0_NAME                         "skyworth"
#define BTL_VENDOR_1_NAME                         "unknown"
#define BTL_VENDOR_2_NAME                         "unknown"
#define BTL_VENDOR_3_NAME                         "unknown"
#endif

