/****************************

       zte_fp.c

****************************/

#include <linux/init.h>
#include <linux/module.h>

#include "zte_fp_entry.h"

static int __init zte_fp_init(void)
{
	zte_fp_log(INFO_LOG, "%s enter!\n", __func__);

#ifdef CONFIG_PLATFORM_FINGERPRINT_CDFINGER
	zte_fp_log(INFO_LOG, "cdfinger_fp_init enter!\n");
	cdfinger_fp_init();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_CHIPONE
	zte_fp_log(INFO_LOG, "fpsensor_init enter!\n");
	fpsensor_init();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_SUNWAVE
	zte_fp_log(INFO_LOG, "sf_ctl_driver_init enter!\n");
	sf_ctl_driver_init();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_GOODIX
	zte_fp_log(INFO_LOG, "goodix_driver_init enter!\n");
	gf_init();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_FPC
	zte_fp_log(INFO_LOG, "fpc_sensor_init enter!\n");
	fpc_sensor_init();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_FPC1020
	zte_fp_log(INFO_LOG, "fpc_init enter!\n");
	fpc_init();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_SILEAD
	zte_fp_log(INFO_LOG, "silfp_dev_init enter!\n");
	silfp_dev_init();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_FOCALTECH
	zte_fp_log(INFO_LOG, "focaltech_fp_driver_init enter!\n");
	focaltech_fp_driver_init();
#endif

	return 0;
}


static void __exit zte_fp_exit(void)
{
	zte_fp_log(INFO_LOG, "%s enter!\n", __func__);

#ifdef CONFIG_PLATFORM_FINGERPRINT_CDFINGER
	zte_fp_log(INFO_LOG, "cdfinger_fp_exit enter!\n");
	cdfinger_fp_exit();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_CHIPONE
	zte_fp_log(INFO_LOG, "fpsensor_exit enter!\n");
	fpsensor_exit();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_SUNWAVE
	zte_fp_log(INFO_LOG, "sf_ctl_driver_exit enter!\n");
	sf_ctl_driver_exit();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_GOODIX
	zte_fp_log(INFO_LOG, "goodix_driver_exit enter!\n");
	gf_exit();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_FPC
	zte_fp_log(INFO_LOG, "fpc_sensor_exit enter!\n");
	fpc_sensor_exit();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_FPC1020
	zte_fp_log(INFO_LOG, "fpc_exit enter!\n");
	fpc_exit();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_SILEAD
	zte_fp_log(INFO_LOG, "silfp_dev_exit enter!\n");
	silfp_dev_exit();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_FOCALTECH
	zte_fp_log(INFO_LOG, "focaltech_fp_driver_exit enter!\n");
	focaltech_fp_driver_exit();
#endif

}

module_init(zte_fp_init);
module_exit(zte_fp_exit);

MODULE_DESCRIPTION("ZTE Fp Driver Entry");
MODULE_AUTHOR("***@zte.com");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ZTE");


