#ifndef _CHIPONE_CONFIG_H_
#define _CHIPONE_CONFIG_H_

#define CTS_MODULE1_ID                         0x0001
#define CTS_MODULE2_ID                         0x0002
#define CTS_MODULE3_ID                         0x0003

#define CTS_MODULE1_LCD_NAME                   "skyworth"
#define CTS_MODULE2_LCD_NAME                   "huaying"
#define CTS_MODULE3_LCD_NAME                   "easyquick"

/*default i2c*/
#define USE_SPI_BUS
#ifdef USE_SPI_BUS
/*define use spi num*/
#define SPI_NUM                                    3
#endif

#define CTS_REPORT_BY_ZTE_ALGO
#ifdef CTS_REPORT_BY_ZTE_ALGO
#define cts_left_edge_limit_v           4
#define cts_right_edge_limit_v          4
#define cts_left_edge_limit_h           4
#define cts_right_edge_limit_h          4
#define cts_left_edge_long_pess_v       14
#define cts_right_edge_long_pess_v      14
#define cts_left_edge_long_pess_h       28
#define cts_right_edge_long_pess_h      21
#define cts_long_press_max_count        80
#define cts_edge_long_press_check       1
#endif

/* #define CTS_LCD_OPERATE_TP_RESET */

#define CONFIG_CTS_CHARGER_DETECT

#define CFG_CTS_HEADSET_DETECT

#define CFG_CTS_ROTATION

#define CFG_CTS_GESTURE

#define CFG_USE_DEFAULT_ROWS_COLS
#ifdef CFG_USE_DEFAULT_ROWS_COLS
#define CFG_DEFAULT_ROWS    32
#define CFG_DEFAULT_COLS    18
#endif

#endif /* _CHIPONE_CONFIG_H_ */