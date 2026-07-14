
#ifndef _OMNIVISION_CONFIG_H_
#define _OMNIVISION_CONFIG_H_

#define OVT_TCM_MODULE1_ID                         0x0001
#define OVT_TCM_MODULE2_ID                         0x0002
#define OVT_TCM_MODULE3_ID                         0x0003

#define OVT_TCM_MODULE1_LCD_NAME                   "easyquick"
#define OVT_TCM_MODULE2_LCD_NAME                   "boe_new"
#define OVT_TCM_MODULE3_LCD_NAME                   "boe"

/*default i2c*/
#define USE_SPI_BUS
#ifdef USE_SPI_BUS
/*define use spi num*/
#define SPI_NUM                                    3
#endif

#define OVT_TCM_REPORT_BY_ZTE_ALGO
#ifdef OVT_TCM_REPORT_BY_ZTE_ALGO
#define ovt_tcm_left_edge_limit_v           4
#define ovt_tcm_right_edge_limit_v          4
#define ovt_tcm_left_edge_limit_h           4
#define ovt_tcm_right_edge_limit_h          4
#define ovt_tcm_left_edge_long_pess_v       14
#define ovt_tcm_right_edge_long_pess_v      14
#define ovt_tcm_left_edge_long_pess_h       28
#define ovt_tcm_right_edge_long_pess_h      21
#define ovt_tcm_long_press_max_count        80
#define ovt_tcm_edge_long_press_check       1
#endif

#define OVT_TCM_LCD_OPERATE_TP_RESET

#define OVT_TCM_USB_DETECT_GLOBAL

#define OVT_DEFAULT_FW_IMAGE_NAME "ovt_boe_default_firmware.img"

#endif /* _OMNIVISION_CONFIG_H_ */
