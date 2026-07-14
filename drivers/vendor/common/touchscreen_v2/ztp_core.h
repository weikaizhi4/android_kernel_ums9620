/*
 * file: ztp_core.h
 *
 */
#ifndef  _ZTP_CORE_H_
#define _ZTP_CORE_H_


#ifdef CONFIG_TOUCHSCREEN_ILITEK_TDDI_V3
extern int  ilitek_plat_dev_init(void);
extern void  ilitek_plat_dev_exit(void);
#endif
#ifdef CONFIG_TOUCHSCREEN_HIMAX_COMMON
extern int  himax_common_init(void);
extern void  himax_common_exit(void);
#endif
#ifdef CONFIG_TOUCHSCREEN_LCD_NOTIFY
extern void lcd_notify_register(void);
extern void lcd_notify_unregister(void);
#endif
#ifdef CONFIG_TOUCHSCREEN_CHIPONE
extern int  cts_i2c_driver_init(void);
extern void  cts_i2c_driver_exit(void);
#endif
#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_V2
extern int  goodix_ts_core_init(void);
extern void  goodix_ts_core_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_OMNIVISION_TCM
extern int  ovt_tcm_module_init(void);
extern void  ovt_tcm_module_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_CHIPONE_V2
extern int  cts_driver_init(void);
extern void  cts_driver_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_CHIPONE_V3
extern int  cts_driver_init(void);
extern void  cts_driver_exit(void);
#endif

#if defined(CONFIG_TOUCHSCREEN_FTS_V3_3) || defined(CONFIG_TOUCHSCREEN_FTS_UFP)
extern int  fts_ts_init(void);
extern void  fts_ts_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_CHSC5XXX
extern int  semi_i2c_device_init(void);
extern void  semi_i2c_device_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_TLSC6X_V3
extern int  tlsc6x_init(void);
extern void  tlsc6x_exit(void);
#endif

#if defined(CONFIG_TOUCHSCREEN_GCORE_TS) || defined(CONFIG_TOUCHSCREEN_GCORE_TS_V3)
int gcore_touch_driver_init(void);
void  gcore_touch_driver_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_BETTERLIFE_TS
int btl_ts_init(void);
void  btl_ts_exit(void);
#endif
#ifdef CONFIG_TOUCHSCREEN_SITRONIX_INCELL
extern int  sitronix_ts_init(void);
extern void  sitronix_ts_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
int ufp_mac_init(void);
void  ufp_mac_exit(void);
#endif
bool tp_ghost_check(void);
void ghost_check_reset(void);
void tpd_clean_all_event(void);
int tpd_report_work_init(void);
void tpd_report_work_deinit(void);
void tpd_resume_work_init(void);
void tpd_resume_work_deinit(void);

#endif

