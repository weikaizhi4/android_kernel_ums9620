/****************************

       zte_fp_entry.h

****************************/

#ifndef __ZTE_FP_ENTRY__H__
#define __ZTE_FP_ENTRY__H__

typedef enum {
    ERR_LOG = 0,
    WARN_LOG,
    INFO_LOG,
    DEBUG_LOG,
    ALL_LOG,
} zte_fp_log_level_t;

static zte_fp_log_level_t zte_fp_log_level = INFO_LOG;

#define zte_fp_log(level, fmt, args...) do { \
			if (zte_fp_log_level >= level) {\
				pr_warn("[zte_fp_info] " fmt, ##args); \
			} \
		} while (0)

#ifdef CONFIG_PLATFORM_FINGERPRINT_CDFINGER
extern int cdfinger_fp_init(void);
extern void cdfinger_fp_exit(void);
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_CHIPONE
extern int fpsensor_init(void);
extern void fpsensor_exit(void);
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_SUNWAVE
extern int sf_ctl_driver_init(void);
extern void sf_ctl_driver_exit(void);
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_GOODIX
extern int gf_init(void);
extern void gf_exit(void);
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_FPC
extern int fpc_sensor_init(void);
extern void fpc_sensor_exit(void);
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_FPC1020
extern int fpc_init(void);
extern void fpc_exit(void);
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_SILEAD
extern int silfp_dev_init(void);
extern void silfp_dev_exit(void);
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_FOCALTECH
extern int focaltech_fp_driver_init(void);
extern void focaltech_fp_driver_exit(void);
#endif

#endif
