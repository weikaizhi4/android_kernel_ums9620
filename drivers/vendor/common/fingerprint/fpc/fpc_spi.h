/*
 * driver definition for sensor driver
 *
 * Coypright (c) 2017 Goodix
 */
#ifndef FPC_SPI_H
#define FPC_SPI_H

#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/wait.h>
#include <linux/fb.h>
#include <linux/notifier.h>
#include <linux/spi/spidev.h>
#include <linux/spi/spi.h>
#include <linux/version.h>


/*********************** wakelock setting **********************/
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
#include <linux/pm_wakeup.h>
#else
#include <linux/wakelock.h>
#endif

#ifdef CONFIG_VENDOR_SOC_SPRD_COMPILE
#define  USE_PLATFORM_BUS	1
#endif

#ifdef CONFIG_VENDOR_SOC_MTK_COMPILE
#define  USE_SPI_BUS   1
extern void mt_spi_enable_master_clk(struct spi_device *spidev);
extern void mt_spi_disable_master_clk(struct spi_device *spidev);
#endif

/**********************************************************/
enum FPC_MODE{
	FPC_IMAGE_MODE = 0,
	FPC_KEY_MODE,
	FPC_SLEEP_MODE,
	FPC_FF_MODE,
	FPC_DEBUG_MODE = 0x56
};

typedef enum {
	ERR_LOG = 0,
	WARN_LOG,
	INFO_LOG,
	DEBUG_LOG,
	ALL_LOG,
} fpc_debug_level_t;

static fpc_debug_level_t g_debug_level = INFO_LOG;

#define fpc_debug(level, fmt, args...) do { \
			if (g_debug_level >= level) {\
				pr_warn("[fpc_info] " fmt, ##args); \
			} \
		} while (0)

#define SUPPORT_NAV_EVENT

#if defined(SUPPORT_NAV_EVENT)
#define FPC_NAV_INPUT_UP		      KEY_UP
#define FPC_NAV_INPUT_DOWN	      KEY_DOWN
#define FPC_NAV_INPUT_LEFT	      KEY_LEFT
#define FPC_NAV_INPUT_RIGHT	      KEY_RIGHT
#define FPC_NAV_INPUT_CLICK	      KEY_VOLUMEDOWN
#define FPC_NAV_INPUT_DOUBLE_CLICK     KEY_VOLUMEUP
#define FPC_NAV_INPUT_LONG_PRESS	      KEY_SEARCH
#define FPC_NAV_INPUT_HEAVY	      KEY_CHAT
#endif

#define FPC_KEY_INPUT_HOME	      KEY_HOME
#define FPC_KEY_INPUT_MENU	      KEY_MENU
#define FPC_KEY_INPUT_BACK	      KEY_BACK
#define FPC_KEY_INPUT_POWER	      KEY_POWER
#define FPC_KEY_INPUT_CAMERA	      KEY_CAMERA

#if defined(SUPPORT_NAV_EVENT)
typedef enum fpc_nav_event {
	FPC_NAV_NONE = 0,
	FPC_NAV_FINGER_UP,
	FPC_NAV_FINGER_DOWN,
	FPC_NAV_UP = 251,
	FPC_NAV_DOWN,
	FPC_NAV_LEFT,
	FPC_NAV_RIGHT,
	FPC_NAV_CLICK,
	FPC_NAV_HEAVY,
	FPC_NAV_LONG_PRESS,
	FPC_NAV_DOUBLE_CLICK,
	FPC_NAV_MAX,
} fpc_nav_event_t;
#endif

typedef enum fpc_key_event {
	FPC_KEY_NONE = 0,
	FPC_KEY_HOME,
	FPC_KEY_POWER,
	FPC_KEY_MENU,
	FPC_KEY_BACK,
	FPC_KEY_CAMERA,
} fpc_key_event_t;

struct fpc_key {
	enum fpc_key_event key;
	uint32_t value;   /* key down = 1, key up = 0 */
};

struct fpc_key_map {
	unsigned int type;
	unsigned int code;
};

struct fpc_ioc_chip_info {
	unsigned char vendor_id;
	unsigned char mode;
	unsigned char operation;
	unsigned char reserved[5];
};

#define FPC_IOC_MAGIC    'f'     /* define magic number */
#define FPC_IOC_INIT             _IOR(FPC_IOC_MAGIC, 0, uint8_t)
#define FPC_IOC_EXIT             _IO(FPC_IOC_MAGIC, 1)
#define FPC_IOC_RESET            _IO(FPC_IOC_MAGIC, 2)
#define FPC_IOC_ENABLE_IRQ       _IO(FPC_IOC_MAGIC, 3)
#define FPC_IOC_DISABLE_IRQ      _IO(FPC_IOC_MAGIC, 4)
#define FPC_IOC_ENABLE_SPI_CLK   _IOW(FPC_IOC_MAGIC, 5, uint32_t)
#define FPC_IOC_DISABLE_SPI_CLK  _IO(FPC_IOC_MAGIC, 6)
#define FPC_IOC_ENABLE_POWER     _IO(FPC_IOC_MAGIC, 7)
#define FPC_IOC_DISABLE_POWER    _IO(FPC_IOC_MAGIC, 8)
#define FPC_IOC_INPUT_KEY_EVENT  _IOW(FPC_IOC_MAGIC, 9, struct gf_key)
#define FPC_IOC_ENTER_SLEEP_MODE _IO(FPC_IOC_MAGIC, 10)
#define FPC_IOC_GET_FW_INFO      _IOR(FPC_IOC_MAGIC, 11, uint8_t)
#define FPC_IOC_REMOVE           _IO(FPC_IOC_MAGIC, 12)
#define FPC_IOC_CHIP_INFO        _IOW(FPC_IOC_MAGIC, 13, struct gf_ioc_chip_info)

#if defined(SUPPORT_NAV_EVENT)
#define FPC_IOC_NAV_EVENT	_IOW(FPC_IOC_MAGIC, 14, uint16_t)
#define FPC_IOC_MAXNR    15  /* THIS MACRO IS NOT USED NOW... */
#else
#define FPC_IOC_MAXNR    14  /* THIS MACRO IS NOT USED NOW... */
#endif

//#define GF_FASYNC   1*/	/*If support fasync mechanism.*/
#define FPC_NETLINK_ENABLE 1
#define FPC_NET_EVENT_IRQ 1
#define FPC_NET_EVENT_FB_BLACK 2
#define FPC_NET_EVENT_FB_UNBLACK 3
#define NETLINK_TEST 25

#define FP_NAV_UP	   "fp_nav_event_up=true"
#define FP_NAV_DOWN	   "fp_nav_event_down=true"
#define FP_NAV_LEFT	   "fp_nav_event_left=true"
#define FP_NAV_RIGHT   "fp_nav_event_right=true"

#ifdef USE_PIN_CTL
static const char * const pctl_names[] = {
	"fpsensor_fpc_rst_low",
	"fpsensor_fpc_rst_high",
};
#endif

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
#include "zlog_common_base.h"
#endif

struct fpc_data {
	struct device *dev;

#if defined(USE_PLATFORM_BUS)
	struct platform_device *spi;
#elif defined(USE_SPI_BUS)
	struct spi_device *spi;
#endif

#ifdef USE_PIN_CTL
	struct pinctrl *pinctrl_fpc;
	struct pinctrl_state *pinctrl_state[ARRAY_SIZE(pctl_names)];
#endif

	unsigned int users;

	int irq_num;

	int irq_gpio;
	int rst_gpio;
	int power_gpio;

    /* 0 fingerprint use system gpio control  power, 1 pmic power */
	int power_type;
	struct regulator *fp_reg;
	int power_voltage;

	/*zte_fp_nav device*/
	struct platform_device *nav_dev;

	bool wakeup_enabled;
	struct wakeup_source *ttw_wl;
	bool clocks_enabled;

	int sys_created;

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	struct zlog_client *zlog_fp_client;
#endif

};

#endif /*FPC_SPI_H*/
