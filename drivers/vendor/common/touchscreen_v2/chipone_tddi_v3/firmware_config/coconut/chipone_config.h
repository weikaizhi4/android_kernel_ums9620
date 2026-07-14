#ifndef _CHIPONE_CONFIG_H_
#define _CHIPONE_CONFIG_H_

#define CTS_MODULE1_ID	0x9f
#define CTS_MODULE2_ID	0xA3
#define CTS_MODULE3_ID	0x00

#define CTS_MODULE1_LCD_NAME		"tongxingda"
#define CTS_MODULE2_LCD_NAME		"easyquick"
#define CTS_MODULE3_LCD_NAME		"unknown"

#define CONFIG_CTS_PM_FB_NOTIFIER
/*default i2c*/
/* #define CONFIG_CTS_I2C_HOST */
#define CONFIG_CTS_SPI_HOST
#ifdef CONFIG_CTS_SPI_HOST
/*define use spi num*/
#define SPI_NUM   3
#endif

/* this macro need be configured refer to module*/
#define CTS_DEFAULT_FIRMWARE        "cts_6_517_default_firmware"
#define CTS_REPORT_BY_ZTE_ALGO

/* #define CTS_LCD_OPERATE_TP_RESET */

#define CFG_CTS_CHARGER_DETECT

#define CFG_CTS_EARJACK_DETECT

 #define CFG_CTS_ROTATION

#define CFG_CTS_GESTURE

/* #define CONFIG_CTS_VIRTUALKEY */

#endif /* _CHIPONE_CONFIG_H_ */

