/****************************

       cdfinger_tee.h

****************************/

#ifndef __CDFINGER_TEE_H__
#define __CDFINGER_TEE_H__

#include <linux/input.h>
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/platform_device.h>

#define DTS_USE_GPIO_PIN
#define DTS_PROBE
#define HAS_RESET_PIN

#define VERSION                "cdfinger version 3.2"
#define DEVICE_NAME            "fpsdev0"
#define SPI_DRV_NAME           "cdfinger"

static int isInKeyMode = 0;    // key mode
static int screen_status = 1;  // screen on
static int sign_sync = 0;      // for poll

#ifdef CONFIG_VENDOR_SOC_SPRD_COMPILE
#define  USE_PLATFORM_BUS	1
#endif

#ifdef CONFIG_VENDOR_SOC_MTK_COMPILE
/*#define AP_CONTROL_CLK       1*/
#define  USE_SPI_BUS   1
extern void mt_spi_enable_master_clk(struct spi_device *spidev);
extern void mt_spi_disable_master_clk(struct spi_device *spidev);
#endif


typedef struct key_report{
    int key;
    int value;
}key_report_t;

typedef enum {
    ERR_LOG = 0,
    WARN_LOG,
    INFO_LOG,
    DEBUG_LOG,
    ALL_LOG,
} fp_debug_level_t;

static fp_debug_level_t cfp_debug_level = INFO_LOG;

#define cfp_debug(level, fmt, args...) do { \
			if (cfp_debug_level >= level) {\
				pr_warn("[cdfinger_info] " fmt, ##args); \
			} \
		} while (0)

/*static u8 cdfinger_debug = 0xFF;
#define CDFINGER_DBG(fmt, args...) \
	do{ \
		if(cdfinger_debug & 0x01) \
			printk( "[DBG][cdfinger]:%5d: <%s>" fmt, __LINE__,__func__,##args ); \
	}while(0)
#define CDFINGER_FUNCTION(fmt, args...) \
	do{ \
		if(cdfinger_debug & 0x02) \
			printk( "[DBG][cdfinger]:%5d: <%s>" fmt, __LINE__,__func__,##args ); \
	}while(0)
#define CDFINGER_REG(fmt, args...) \
	do{ \
		if(cdfinger_debug & 0x04) \
			printk( "[DBG][cdfinger]:%5d: <%s>" fmt, __LINE__,__func__,##args ); \
	}while(0)
#define CDFINGER_ERR(fmt, args...) \
    do{ \
		printk( "[DBG][cdfinger]:%5d: <%s>" fmt, __LINE__,__func__,##args ); \
    }while(0)
#define CDFINGER_PROBE(fmt, args...) \
    do{ \
		printk( "[PROBE][cdfinger]:%5d: <%s>" fmt, __LINE__,__func__,##args ); \
    }while(0)
*/

/*zte_fp_nav*/
#define FP_NAV_UP	   "fp_nav_event_up=true"
#define FP_NAV_DOWN	   "fp_nav_event_down=true"
#define FP_NAV_LEFT	   "fp_nav_event_left=true"
#define FP_NAV_RIGHT	   "fp_nav_event_right=true"

enum work_mode {
    CDFINGER_MODE_NONE       = 1<<0,
    CDFINGER_INTERRUPT_MODE  = 1<<1,
    CDFINGER_KEY_MODE        = 1<<2,
    CDFINGER_FINGER_UP_MODE  = 1<<3,
    CDFINGER_READ_IMAGE_MODE = 1<<4,
    CDFINGER_MODE_MAX
};

#define CDFINGER_IOCTL_MAGIC_NO          0xFB
#define CDFINGER_INIT                    _IOW(CDFINGER_IOCTL_MAGIC_NO, 0, uint8_t)
#define CDFINGER_GETIMAGE                _IOW(CDFINGER_IOCTL_MAGIC_NO, 1, uint8_t)
#define CDFINGER_INITERRUPT_MODE         _IOW(CDFINGER_IOCTL_MAGIC_NO, 2, uint8_t)
#define CDFINGER_INITERRUPT_KEYMODE      _IOW(CDFINGER_IOCTL_MAGIC_NO, 3, uint8_t)
#define CDFINGER_INITERRUPT_FINGERUPMODE _IOW(CDFINGER_IOCTL_MAGIC_NO, 4, uint8_t)
#define CDFINGER_RELEASE_WAKELOCK        _IO(CDFINGER_IOCTL_MAGIC_NO, 5)
#define CDFINGER_CHECK_INTERRUPT         _IO(CDFINGER_IOCTL_MAGIC_NO, 6)
#define CDFINGER_SET_SPI_SPEED           _IOW(CDFINGER_IOCTL_MAGIC_NO, 7, uint8_t)
#define	CDFINGER_GETID                   _IO(CDFINGER_IOCTL_MAGIC_NO,9)
#define	CDFINGER_SETID                   _IOW(CDFINGER_IOCTL_MAGIC_NO,8,uint8_t)
#define CDFINGER_REPORT_KEY_LEGACY       _IOW(CDFINGER_IOCTL_MAGIC_NO, 10, uint8_t)
#define CDFINGER_POWERDOWN               _IO(CDFINGER_IOCTL_MAGIC_NO, 11)
#define CDFINGER_ENABLE_IRQ              _IO(CDFINGER_IOCTL_MAGIC_NO, 12)
#define CDFINGER_DISABLE_IRQ             _IO(CDFINGER_IOCTL_MAGIC_NO, 13)
#define CDFINGER_HW_RESET                _IOW(CDFINGER_IOCTL_MAGIC_NO, 14, uint8_t)
#define CDFINGER_GET_STATUS              _IO(CDFINGER_IOCTL_MAGIC_NO, 15)
#define CDFINGER_SPI_CLK                 _IOW(CDFINGER_IOCTL_MAGIC_NO, 16, uint8_t)
#define CDFINGER_REPORT_KEY              _IOW(CDFINGER_IOCTL_MAGIC_NO, 19, key_report_t)
#define CDFINGER_INIT_GPIO               _IO(CDFINGER_IOCTL_MAGIC_NO, 20)
#define CDFINGER_INIT_IRQ                _IO(CDFINGER_IOCTL_MAGIC_NO, 21)
#define CDFINGER_POWER_ON                _IO(CDFINGER_IOCTL_MAGIC_NO, 22)
#define CDFINGER_RESET                   _IO(CDFINGER_IOCTL_MAGIC_NO, 23)
#define CDFINGER_RELEASE_DEVICE          _IO(CDFINGER_IOCTL_MAGIC_NO, 25)
#define CDFINGER_WAKE_LOCK               _IOW(CDFINGER_IOCTL_MAGIC_NO,26,uint8_t)
#define CDFINGER_ENABLE_CLK              _IOW(CDFINGER_IOCTL_MAGIC_NO, 30, uint8_t)
#define CDFINGER_POLL_TRIGGER            _IO(CDFINGER_IOCTL_MAGIC_NO,31)
#define CDFINGER_NEW_KEYMODE             _IOW(CDFINGER_IOCTL_MAGIC_NO, 37, uint8_t)
#define CDFINGER_CONTROL_IRQ             _IOW(CDFINGER_IOCTL_MAGIC_NO, 38, uint8_t)
#define KEY_INTERRUPT                    KEY_F11

#endif
