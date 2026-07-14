/*
 * Ilitek TouchScreen oem config.
 */

 #ifndef _ILITEK_CONFIG_H_
 #define _ILITEK_CONFIG_H_

/* Options */
#define TDDI_INTERFACE			BUS_SPI /* BUS_I2C(0x18) or BUS_SPI(0x1C) */
#define VDD_VOLTAGE			1800000
#define VCC_VOLTAGE			1800000
#define SPI_CLK                         9      /* follow by clk list */
#define SPI_RETRY			5
#define IRQ_GPIO_NUM			66
#define TR_BUF_SIZE			(2*K) /* Buffer size of touch report */
#define TR_BUF_LIST_SIZE		(1*K) /* Buffer size of touch report for debug data */
#define SPI_TX_BUF_SIZE  		4096
#define SPI_RX_BUF_SIZE  		4096
#define WQ_ESD_DELAY			4000
#define WQ_BAT_DELAY			2000
#define AP_INT_TIMEOUT			600 /*600ms*/
#define MP_INT_TIMEOUT			5000 /*5s*/
#define MT_B_TYPE			ENABLE
#define TDDI_RST_BIND			DISABLE
#define MT_PRESSURE			DISABLE
#define ENABLE_WQ_ESD			DISABLE
#define ENABLE_WQ_BAT			DISABLE
#define ENABLE_GESTURE			ENABLE
#define REGULATOR_POWER			DISABLE
#define TP_SUSPEND_PRIO			ENABLE
#define RESUME_BY_DDI			DISABLE
#define BOOT_FW_UPDATE			DISABLE
#define MP_INT_LEVEL			DISABLE
#define PLL_CLK_WAKEUP_TP_RESUME	DISABLE
#define CHARGER_NOTIFIER_CALLBACK	DISABLE
#define ENABLE_EDGE_PALM_PARA		DISABLE
#define MULTI_REPORT_RATE		DISABLE
#define ENGINEER_FLOW			ENABLE
#define DMESG_SEQ_FILE			DISABLE
#define GENERIC_KERNEL_IMAGE	ENABLE/*follow gki */

/*Proximity mode options*/
#define PROXIMITY_NULL			0
#define PROXIMITY_SUSPEND_RESUME	1
#define PROXIMITY_BACKLIGHT		2
#define PROXIMITY_BY_FW_MODE	(PROXIMITY_NULL)

/*if current interface is spi, must to hostdownload */
#if (TDDI_INTERFACE == BUS_SPI)
#define HOST_DOWN_LOAD			ENABLE
#else
#define HOST_DOWN_LOAD			DISABLE
#endif

/* Plaform compatibility */
#define CONFIG_PLAT_SPRD		DISABLE
#define I2C_DMA_TRANSFER		DISABLE
#define SPI_DMA_TRANSFER_SPLIT		DISABLE
#define SPI_DMA_TRANSFER_SPLIT_OLD  DISABLE

/* define the width and heigth of a screen. */
#define TOUCH_SCREEN_X_MIN			0
#define TOUCH_SCREEN_Y_MIN			0
#define TOUCH_SCREEN_X_MAX			1080
#define TOUCH_SCREEN_Y_MAX			2408
#define MAX_TOUCH_NUM				10
#define ILITEK_KNUCKLE_ROI_FINGERS		2

/* define the range on space, don't change */
#define TPD_HEIGHT				2048
#define TPD_WIDTH				2048
#define ILITEK_REPORT_BY_ZTE_ALGO
#define ILI_MODULE_NUM	 1
#define ILI_VENDOR_ID_0  0x4
#define ILI_VENDOR_ID_1  0
#define ILI_VENDOR_ID_2  0
#define ILI_VENDOR_ID_3  0

#define ILI_VENDOR_0_NAME                         "tongxingda"
#define ILI_VENDOR_1_NAME                         "unknown"
#define ILI_VENDOR_2_NAME                         "unknown"
#define ILI_VENDOR_3_NAME                         "unknown"
#define ILITEK_DEFAULT_FIRMWARE "ilitek_6_58_default_firmware"
#endif

