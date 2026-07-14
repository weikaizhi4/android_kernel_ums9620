
/************************************************************************
*
* File Name: tlsc6x_config.h
*
*  Abstract: global configurations
*
*   Version: v1.0
*
************************************************************************/
#ifndef _LINUX_TLSC6X_CONFIG_H_
#define _LINUX_TLSC6X_CONFIG_H_

#define TLSC_TPD_PROXIMITY
#define TLSC_APK_DEBUG		/* apk debugger, close:undef */
#define TLSC_AUTO_UPGRADE
#define TLSC_ESD_HELPER_EN	/* esd helper, close:undef */
/* #define TLSC_GESTRUE */
#define TLSC_TP_PROC_SELF_TEST

/* #define TLSC_BUILDIN_BOOT */
/* #define TLSC_CHIP_NAME "chsc6440" */
#define CONFIG_TLSC_POINT_REPORT_CHECK
#define TLSC_REPORT_BY_ZTE_ALGO
#define HUB_TP_PS_ENABLE 1
#endif /* _LINUX_TLSC6X_CONFIG_H_ */
