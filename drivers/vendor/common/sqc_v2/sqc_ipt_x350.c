/*
 * Driver for Pixcir I2C touchscreen controllers.
 *
 * Copyright (C) 2010-2025 Pixcir, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temples Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/input.h>
#include <linux/proc_fs.h>
#include <linux/completion.h>
#include <linux/fs.h>
#include <linux/irq.h>
#include <linux/uaccess.h>
#include <linux/power_supply.h>
#include <linux/firmware.h>
#include "inc/tcpm.h"
#include <linux/pinctrl/consumer.h>
#include <vendor/common/sqc_common.h>
#include <vendor/common/zte_misc.h>
#include "sqc_netlink.h"
#include <mt-plat/mtk_boot.h>


//---------------------------------------------------------------------
#define IPT_I2C_DRIVER_NAME	 	"ipt_detect"
#define IPT_X350_DRIVER_VERSION	"BC1.2 driver v0.1"

#define IPT_X350_IMAGE_NAME	"ipt350_firmware.img"


//---------------------------------------------------------------------
#define CONFIG_IPT_X350_USE_TEST_I2C
#define IPT_X350_PRESS_USE_PM

#define IPT_X350_EEPROM_ADDR 			0x35

//---------------------------------------------------------------------
#define IPT_X350_DEBUG_ON 				1
#define IPT_X350_DEBUG_FUNC_ON 		0
#define IIC_EEPROM 					0
#define IPT_BURN_LEN 				4
#define IPT_FLASH_SIZE 				4096
//---------------------------------------------------------------------
#define IPT_X350_REG_ADSP_STRAT 			0x01

#define IPT_X350_REG_PROT_SEL 				0x02
#define IPT_X350_MASK_PROT_SEL				0xFF
#define IPT_X350_VAL_PROT_QC2D0_5V 		0x01
#define IPT_X350_VAL_PROT_QC2D0_9V 		0x02
#define IPT_X350_VAL_PROT_QC2D0_12V 		0x03
#define IPT_X350_VAL_PROT_QC3D0_5V 		0x04
#define IPT_X350_VAL_PROT_QC3DP_5V 		0x05

#define IPT_X350_REG_INTB_CTRL 			0x03
#define IPT_X350_MASK_INTB_CTRL			0xFF

#define IPT_X350_REG_SOFT_RESET 			0x04
#define IPT_X350_MASK_SOFT_RESET			0xFF

#define IPT_X350_REG_HVDCP_CTRL 			0x05
#define IPT_X350_MASK_HVDCP_CTRL			0xFF

#define IPT_X350_REG_BC1D2_CTRL 			0x06
#define IPT_X350_MASK_BC1D2_CTRL 			0xFF
#define IPT_X350_VAL_BC1D2_CTRL_DISABLE 	0x00
#define IPT_X350_VAL_BC1D2_CTRL_ENABLE 		0x01

#define IPT_X350_REG_SLEEP_CTRL 			0x07
#define IPT_X350_MASK_SLEEP_CTRL			0xFF
#define IPT_X350_VAL_SLEEP_CTRL_DISABLE 	0x00
#define IPT_X350_VAL_SLEEP_CTRL_ENABLE 		0x01

#define IPT_X350_REG_QC3D0_REGULATE_MODE	0x73
#define IPT_X350_MASK_QC3D0_DP_DM			BIT(15)
#define IPT_X350_SHIFT_QC3D0_DP_DM			15
#define IPT_X350_VAL_QC3D0_DM 				0x00
#define IPT_X350_VAL_QC3D0_DP			 	0x01
#define IPT_X350_MASK_QC3D0_CNT				0x7FFF
#define IPT_X350_SHIFT_QC3D0_CNT			0


#define IPT_X350_REG_QC3DP_REGULATE_MODE	0x83
#define IPT_X350_MASK_QC3DP_DP_DM			BIT(15)
#define IPT_X350_SHIFT_QC3DP_DP_DM			15
#define IPT_X350_VAL_QC3DP_DM 				0x00
#define IPT_X350_VAL_QC3DP_DP			 	0x01
#define IPT_X350_MASK_QC3DP_CNT				0x7FFF
#define IPT_X350_SHIFT_QC3DP_CNT			0

#define IPT_X350_REG_CHG_TYPE_AND_ERR		0x11
#define IPT_X350_MASK_CHG_TYPE				0xFF00
#define IPT_X350_SHIFT_CHG_TYPE				8
#define IPT_X350_VAL_CHG_TYPE_UNKNOWN		0x00
#define IPT_X350_VAL_CHG_TYPE_OCP			0x01
#define IPT_X350_VAL_CHG_TYPE_FLOAT			0x02
#define IPT_X350_VAL_CHG_TYPE_SDP			0x03
#define IPT_X350_VAL_CHG_TYPE_CDP			0x04
#define IPT_X350_VAL_CHG_TYPE_DCP			0x05
#define IPT_X350_VAL_CHG_TYPE_QC2D0			0x06
#define IPT_X350_VAL_CHG_TYPE_QC3D0			0x07
#define IPT_X350_VAL_CHG_TYPE_QC3DP_18W		0x08
#define IPT_X350_VAL_CHG_TYPE_QC3DP_27W		0x09
#define IPT_X350_VAL_CHG_TYPE_HVDCP			0x10
#define IPT_X350_VAL_CHG_TYPE_QC3DP_45W		0x12


#define IPT_X350_MASK_ERR_CODE				0x00FF
#define IPT_X350_SHIFT_ERR_CODE				0
#define IPT_X350_VAL_ERR_CODE_NOERR			0x00
#define IPT_X350_VAL_ERR_CODE_QC3D0_FAIL	0x01
#define IPT_X350_VAL_ERR_CODE_TA_FAIL		0x02
#define IPT_X350_VAL_ERR_CODE_TA_CAP_FAIL	0x03
#define IPT_X350_VAL_ERR_CODE_DET_SUCCESS	0x04

#define IPT_X350_REG_VBUS_ADC				0x12

#define IPT_X350_REG_VENDOR_ID				0x13

#define IPT_X350_REG_FIRMWARE_VERSION		0x14
#define IPT_X350_MASK_FIRMWARE_VERSION		0xFF00
#define IPT_X350_SHIFT_FIRMWARE_VERSION		8

//---------------------------------------------------------------------

#define IPT_X350_INFO(fmt,arg...)           pr_info("<<IPT_X350-INF>>[%s:%d] "fmt, __func__, __LINE__, ##arg)
#define IPT_X350_ERROR(fmt,arg...)          pr_info("<<IPT_X350-ERR>>[%s:%d] "fmt, __func__, __LINE__, ##arg)
#define IPT_X350_DEBUG(fmt,arg...)          do{\
                                         	if(IPT_X350_DEBUG_ON)\
                                         	pr_info("<<IPT_X350-DBG>>[%s:%d]"fmt"\n",__func__, __LINE__, ##arg);\
                                       		} while(0)

#define IPT_X350_DEBUG_FUNC()               do{\
                                         	if(IPT_X350_DEBUG_FUNC_ON)\
                                         	pr_info("<<IPT_X350-FUNC>> Func:%s@Line:%d\n",__func__,__LINE__);\
                                       	} while(0)
//---------------------------------------------------------------------
static const struct i2c_device_id ipt_id[] = {
	{IPT_I2C_DRIVER_NAME, 0},
	{}
};

static struct of_device_id ipt_match_table[] = {
	{.compatible = "ipt-x350",},
	{ },
};

enum charger_type {
	CHARGER_UNKNOWN = 0,
	STANDARD_HOST,		/* USB : 450mA */
	CHARGING_HOST,
	NONSTANDARD_CHARGER,	/* AC : 450mA~1A */
	STANDARD_CHARGER,	/* AC : ~1A */
	APPLE_2_1A_CHARGER, /* 2.1A apple charger */
	APPLE_1_0A_CHARGER, /* 1A apple charger */
	APPLE_0_5A_CHARGER, /* 0.5A apple charger */
	WIRELESS_CHARGER,
};

struct ipt_board_data {
	struct pinctrl *pinctrl_mode;
	const char *pwr_reg_name;
	const char *bus_reg_name;
	unsigned int power_delay_ms;
	unsigned int reset_delay_ms;
	unsigned int reset_active_ms;
	int irq_gpio;
	int irq_on_state;
	int irq_flags;
	int power_gpio;
	int power_on_state;
	int reset_gpio;
	int reset_on_state;
	int max_y_for_2d;
};

struct ipt_data {
	struct i2c_client *client;
	struct ipt_board_data bdata;
	struct delayed_work int_work;
	struct delayed_work det_work;
	struct delayed_work upgrade_work;
	struct workqueue_struct *ipt_int_queue;
	struct class *i2c_class;
	struct mutex i2c_mutex;
	struct notifier_block nb;
	const struct firmware *fw_entry;
	struct power_supply *psy;
	struct notifier_block otg_nb;
	struct tcpc_device *otg_tcpc_dev;
	unsigned int chg_type;
	unsigned int irq;
	unsigned short FirmwareVer;
	char image_name[64];
	bool appear;
	bool suspend;
	bool power_stat;
	bool alive_flag;
	bool float_recheck_flag;
	bool usbc_otg_attached;
	bool force_sleep;
};

void __attribute__ ((weak)) Charger_Detect_Init(void)
{
}

void __attribute__ ((weak)) Charger_Detect_Release(void)
{
}


static struct ipt_data *gIptX350 = NULL;

static int ipt_set_prop_by_name(const char *name, enum power_supply_property psp, int data)
{
	struct power_supply *psy = NULL;
	union power_supply_propval val = {0, };
	int rc = 0;

	if (name == NULL) {
		pr_info("psy name is NULL!!\n");
		goto failed_loop;
	}

	psy = power_supply_get_by_name(name);
	if (!psy) {
		pr_info("get %s psy failed!!\n", name);
		goto failed_loop;
	}

	val.intval = data;

	rc = power_supply_set_property(psy,
				psp, &val);
	if (rc < 0) {
		pr_info("Failed to set %s property:%d rc=%d\n", name, psp, rc);
		return rc;
	}

	power_supply_put(psy);

	return 0;

failed_loop:
	return -EINVAL;
}

static inline void print_mem(const unsigned char *buffer, unsigned int len)
{
	unsigned int i = 0;
	char buf[256] = {0,};

	memset(buf, 0, sizeof(buf));
	snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "IPT_X350-0x00: ");

	for (i = 0; i < len; i++) {
		snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "0x%02X ", buffer[i]);

		if ((i != 0) && ((i + 1) % 8 == 0)) {
			snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "	");
		}

		if ((i != 0) && ((i + 1) % 16 == 0)) {
			pr_info("%s\n", buf);
			memset(buf, 0, sizeof(buf));
			snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "IPT_X350-0x%02X: ", (i + 1));
		}
	}

	pr_info("%s\n", buf);

}


static inline int ipt_Bon(void *p)
{
	if (p == NULL)
		return 1;

	return 0;
}

static int ipt_gpio_setup(int gpio, bool config, int dir, int state)
{
	int retval = 0;
	unsigned char buf[16];

	IPT_X350_DEBUG_FUNC();

	if (config) {
		snprintf(buf, sizeof(buf), "ipt_gpio_%u\n", gpio);

		retval = gpio_request(gpio, buf);
		if (retval) {
			pr_err("%s: Failed to get gpio %d (code: %d)",
			       __func__, gpio, retval);
			return retval;
		}

		if (dir == 0)
			retval = gpio_direction_input(gpio);
		else
			retval = gpio_direction_output(gpio, state);
		if (retval) {
			pr_err("%s: Failed to set gpio %d direction",
			       __func__, gpio);
			return retval;
		}
	} else {
		gpio_free(gpio);
	}

	IPT_X350_DEBUG_FUNC();

	return retval;
}

static int ipt_parse_dt(struct device *dev, struct ipt_board_data *bdata)
{
	int retval;
	u32 value;
	const char *name;
	struct device_node *np = dev->of_node;

	IPT_X350_DEBUG_FUNC();

	if (of_find_property(np, "ipt,irq-gpio", NULL)) {
		bdata->irq_gpio = of_get_named_gpio_flags(np,
				  "ipt,irq-gpio", 0, NULL);
	} else
		bdata->irq_gpio = -1;

	IPT_X350_INFO("irq_gpio: %d\n", bdata->irq_gpio);

	retval = of_property_read_u32(np, "ipt,irq-on-state",
				      &value);
	if (retval < 0)
		bdata->irq_on_state = 0;
	else
		bdata->irq_on_state = value;

	IPT_X350_INFO("irq_on_state: %d\n", bdata->irq_on_state);

	retval = of_property_read_u32(np, "ipt,irq-flags", &value);
	if (retval < 0)
		return retval;
	else
		bdata->irq_flags = value;

	IPT_X350_INFO("irq_flags: %d\n", bdata->irq_flags);

	retval = of_property_read_string(np, "ipt,pwr-reg-name", &name);
	if (retval == -EINVAL)
		bdata->pwr_reg_name = NULL;
	else if (retval < 0)
		return retval;
	else
		bdata->pwr_reg_name = name;

	IPT_X350_INFO("pwr_reg_name: %s\n", bdata->pwr_reg_name);

	retval = of_property_read_string(np, "ipt,bus-reg-name", &name);
	if (retval == -EINVAL)
		bdata->bus_reg_name = NULL;
	else if (retval < 0)
		return retval;
	else
		bdata->bus_reg_name = name;

	IPT_X350_INFO("bus_reg_name: %s\n", bdata->bus_reg_name);

	if (of_find_property(np, "ipt,power-gpio", NULL)) {
		bdata->power_gpio = of_get_named_gpio_flags(np,
				    "ipt,power-gpio", 0, NULL);
		retval = of_property_read_u32(np, "ipt,power-on-state",
					      &value);
		if (retval < 0)
			return retval;
		else
			bdata->power_on_state = value;
	} else {
		bdata->power_gpio = -1;
	}

	IPT_X350_INFO("power_gpio: %d, power_on_state %d\n", bdata->power_gpio, bdata->power_on_state);

	if (of_find_property(np, "ipt,power-delay-ms", NULL)) {
		retval = of_property_read_u32(np, "ipt,power-delay-ms",
					      &value);
		if (retval < 0)
			return retval;
		else
			bdata->power_delay_ms = value;
	} else {
		bdata->power_delay_ms = 0;
	}

	IPT_X350_INFO("power_delay_ms: %d\n", bdata->power_delay_ms);

	if (of_find_property(np, "ipt,reset-gpio", NULL)) {
		bdata->reset_gpio = of_get_named_gpio_flags(np,
				    "ipt,reset-gpio", 0, NULL);
		retval = of_property_read_u32(np, "ipt,reset-on-state",
					      &value);
		if (retval < 0)
			return retval;
		else
			bdata->reset_on_state = value;
		retval = of_property_read_u32(np, "ipt,reset-active-ms",
					      &value);
		if (retval < 0)
			return retval;
		else
			bdata->reset_active_ms = value;
	} else {
		bdata->reset_gpio = -1;
	}

	if (of_find_property(np, "ipt,reset-delay-ms", NULL)) {
		retval = of_property_read_u32(np, "ipt,reset-delay-ms",
					      &value);
		if (retval < 0)
			return retval;
		else
			bdata->reset_delay_ms = value;
	} else {
		bdata->reset_delay_ms = 0;
	}

	IPT_X350_INFO("reset_gpio: %d, reset_on_state: %d, reset_active_ms %d, reset_delay_ms %d\n",
		   bdata->reset_gpio, bdata->reset_on_state, bdata->reset_active_ms, bdata->reset_delay_ms);

	IPT_X350_DEBUG_FUNC();

	return 0;
}

static int ipt_hw_reset(void)
{
	const struct ipt_board_data *bdata = &gIptX350->bdata;

	if (bdata->reset_gpio >= 0) {
		IPT_X350_INFO("reset on status");
		gpio_set_value(bdata->reset_gpio, bdata->reset_on_state);
		msleep(bdata->reset_active_ms);
		IPT_X350_INFO("reset off status");
		gpio_set_value(bdata->reset_gpio, !bdata->reset_on_state);
		msleep(bdata->reset_delay_ms);
		IPT_X350_INFO("reset exit status");
		return 0;
	}

	return -1;
}

static void ipt_i2c_comm_lock(void)
{
	IPT_X350_DEBUG_FUNC();

	if ((gIptX350 == NULL) || (gIptX350->appear == false))
		return;

	//IPT_X350_INFO("lock ++++++++++++");

	mutex_lock(&(gIptX350->i2c_mutex));

	IPT_X350_DEBUG_FUNC();

	return;
}

static void ipt_i2c_comm_unlock(void)
{
	IPT_X350_DEBUG_FUNC();

	if ((gIptX350 == NULL) || (gIptX350->appear == false))
		return;

	mutex_unlock(&(gIptX350->i2c_mutex));

	//IPT_X350_INFO("lock ----------------");

	IPT_X350_DEBUG_FUNC();

	return;
}

static int ipt_write_eeprom(struct i2c_client *client, unsigned char slv_addr,
					unsigned char reg, const unsigned char *datbuf, int ByteNo)
{
	unsigned char *buf = NULL;
	struct i2c_msg msg;
	int ret;
//	int count=0;

	//IPT_X350_INFO("i2c_client addr: 0x%02X",client->addr);

	if(!datbuf) {
		IPT_X350_ERROR("datbuf is null\n");
		return -EINVAL;
	}

	//reg = htons(reg);

//********************************************************
#ifdef IPT_X350_I2C_DATA_DEBUG
	buf = kmalloc(ByteNo + sizeof(slv_addr) + sizeof(reg),GFP_KERNEL);
	if(!buf) {
		IPT_X350_ERROR("kmalloc failed\n");
		return -EINVAL;
	}

	memcpy(buf, &slv_addr, sizeof(slv_addr));
	memcpy(buf + sizeof(slv_addr), &reg, sizeof(reg));
	memcpy(buf + sizeof(slv_addr) + sizeof(reg), datbuf, ByteNo);
	IPT_X350_INFO("=====write dump=====");
	print_mem(buf, ByteNo + sizeof(slv_addr) + sizeof(reg));
	kfree(buf);
	return 0;
#endif
//********************************************************

	buf = kmalloc(ByteNo + sizeof(reg),GFP_KERNEL);
	if(!buf) {
		IPT_X350_ERROR("kmalloc failed\n");
		return -EINVAL;
	}

	memset(buf, 0, ByteNo + sizeof(reg));
	buf[0] = reg & 0xff;
	memcpy(buf + sizeof(reg), datbuf, ByteNo);

	//IPT_X350_INFO("reg addr: 0x%02x,0x%02X",buf[0],buf[1]);

	msg.addr = slv_addr;
	msg.flags = 0;
	msg.len = ByteNo + sizeof(reg);
	msg.buf = buf;

	ipt_i2c_comm_lock();

	ret = i2c_transfer(client->adapter, &msg, 1);

	ipt_i2c_comm_unlock();

	if(ret < 0)
		IPT_X350_ERROR("i2c_master_send Error ! err_code:%d, reg: 0x%04X",ret, reg);
	//else
	//IPT_X350_INFO("i2c_master_send OK !");

	kfree(buf);

	return ret;
}

static int ipt_read_eeprom(struct i2c_client *client, unsigned char slv_addr,
										unsigned char reg, unsigned char *datbuf, int ByteNo)
{
	struct i2c_msg msg[2];
	int ret = 0;
	unsigned char reg16[2] = {0, };
#ifdef IPT_X350_I2C_DATA_DEBUG
	char *buf = NULL;
#endif

	//IPT_X350_INFO("i2c_client addr: 0x%02X",client->addr);

	if(!datbuf) {
		IPT_X350_ERROR("datbuf is null\n");
		return -EINVAL;
	}

	//reg = htons(reg);
//********************************************************
#ifdef IPT_X350_I2C_DATA_DEBUG
	buf = kmalloc(ByteNo + sizeof(slv_addr) + sizeof(reg),GFP_KERNEL);
	if(!buf) {
		IPT_X350_ERROR("kmalloc failed\n");
		return -EINVAL;
	}

	memcpy(buf, &slv_addr, sizeof(slv_addr));
	memcpy(buf + sizeof(slv_addr), &reg, sizeof(reg));
	memcpy(buf + sizeof(slv_addr) + sizeof(reg), datbuf, ByteNo);
	IPT_X350_INFO("=====read dump=====");
	print_mem(buf, ByteNo + sizeof(slv_addr) + sizeof(reg));
	kfree(buf);
	return 0;
#endif
//********************************************************
	reg16[0] = reg & 0xff;

	//IPT_X350_INFO("reg addr: 0x%02X,0x%02X",reg16[0],reg16[1]);

	msg[0].addr = slv_addr;
	msg[0].flags = 0;
	msg[0].len = sizeof(reg);
	msg[0].buf = reg16;

	msg[1].addr = slv_addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = ByteNo;
	msg[1].buf = datbuf;

	ipt_i2c_comm_lock();

	ret = i2c_transfer(client->adapter, msg, 2);

	ipt_i2c_comm_unlock();

	if (ret < 0)
		IPT_X350_ERROR("i2c_master_send Error ! err_code:%d, reg: 0x%04X",ret, reg);
	//else
	//IPT_X350_INFO("i2c_transfer OK !\n");

	return ret;
}

static int ipt_i2c_write(struct i2c_client *client, u16 addr, int len, void *txbuf)
{
	int ret;

	IPT_X350_DEBUG_FUNC();

	if (txbuf == NULL) {
		IPT_X350_ERROR("txbuf is null write 0x%04X failed\n", addr);
		return 0;
	}

	ipt_i2c_comm_lock();

	ret = i2c_smbus_write_i2c_block_data(client, addr, len, txbuf);

	if (ret < 0) {
		IPT_X350_ERROR("ipt_i2c_write addr 0x%04X failed\n", addr);
		ipt_i2c_comm_unlock();
		return ret;
	}

	ipt_i2c_comm_unlock();

	IPT_X350_DEBUG_FUNC();

	return len;
}

static int ipt_i2c_read(struct i2c_client *client, u16 addr, u16 len, void *rxbuf)
{
	int ret = -1;
	//int i;
	struct i2c_msg msg[2];
	char i2c_buffer[128] = {0};

	IPT_X350_DEBUG_FUNC();

	memset(msg, 0, sizeof(msg));
	memset(i2c_buffer, 0, sizeof(i2c_buffer));

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = (unsigned char *)&addr;

	msg[1].addr = client->addr;//client->addr | I2C_DMA_FLAG;
	msg[1].flags = I2C_M_RD;
	msg[1].len = len;
	msg[1].buf = i2c_buffer;

	ipt_i2c_comm_lock();

	ret = i2c_transfer(client->adapter, msg, 2);

	if (ret < 0) {
		IPT_X350_ERROR("ipt_i2c_read addr 0x%04X failed\n", addr);
		ipt_i2c_comm_unlock();
		return ret;
	}

	ipt_i2c_comm_unlock();

	if ((rxbuf != NULL) && (len != 0)) {
		memcpy(rxbuf, i2c_buffer, len);
	}

	IPT_X350_DEBUG_FUNC();

	return len;
}

static int ipt_power_switch(bool SwFlag)
{

	struct ipt_board_data *bdata = NULL;

	if (gIptX350 == NULL)
		return -1;

	bdata = &gIptX350->bdata;

	if (gIptX350->power_stat == SwFlag) {
		IPT_X350_ERROR("power mode already in power %s\n", SwFlag ? "on" : "down");
		return 0;
	}

	if (SwFlag == true) {
		if (bdata->power_gpio >= 0) {
			gpio_set_value(bdata->power_gpio, bdata->power_on_state);
			msleep(bdata->power_delay_ms);
		}

		if (bdata->reset_gpio >= 0) {
			gpio_set_value(bdata->reset_gpio, !bdata->reset_on_state);
			//mdelay(bdata->reset_active_ms);
		}

		gIptX350->power_stat = true;

		IPT_X350_INFO("+++++++++power on\n");
	} else {
		if (bdata->reset_gpio >= 0) {
			gpio_set_value(bdata->reset_gpio, bdata->reset_on_state);
			mdelay(bdata->reset_active_ms);
		}

		if (bdata->power_gpio >= 0) {
			gpio_set_value(bdata->power_gpio, !bdata->power_on_state);
			msleep(bdata->power_delay_ms);
		}

		gIptX350->power_stat = false;

		IPT_X350_INFO("------------power down\n");
	}

	return 0;
}

static int ipt_set_gpio(struct ipt_data *ipt_data)
{
	int retval;
	const struct ipt_board_data *bdata = &ipt_data->bdata;

	IPT_X350_DEBUG_FUNC();

	retval = ipt_gpio_setup(
			 bdata->irq_gpio,
			 true, 0, 0);
	if (retval < 0) {
		IPT_X350_ERROR("Failed to configure irq GPIO\n");
		goto err_gpio_irq;
	}

	if (bdata->power_gpio >= 0) {
		retval = ipt_gpio_setup(
				 bdata->power_gpio,
				 true, 1, !bdata->power_on_state);
		if (retval < 0) {
			IPT_X350_ERROR("Failed to configure power GPIO\n");
			goto err_gpio_power;
		}
	}

	if (bdata->reset_gpio >= 0) {
		retval = ipt_gpio_setup(
				 bdata->reset_gpio,
				 true, 1, !bdata->reset_on_state);
		if (retval < 0) {
			IPT_X350_ERROR("Failed to configure reset GPIO\n");
			goto err_gpio_reset;
		}
	}

	if (bdata->power_gpio >= 0) {
		gpio_set_value(bdata->power_gpio, bdata->power_on_state);
		mdelay(bdata->power_delay_ms);
	}

	if (bdata->reset_gpio >= 0) {
		gpio_set_value(bdata->reset_gpio, bdata->reset_on_state);
		mdelay(bdata->reset_active_ms);
		gpio_set_value(bdata->reset_gpio, !bdata->reset_on_state);
		mdelay(bdata->reset_delay_ms);
	}

	gIptX350->power_stat = true;

	IPT_X350_DEBUG_FUNC();

	return 0;

err_gpio_irq:
	if (bdata->irq_gpio >= 0)
		ipt_gpio_setup(bdata->irq_gpio, false, 0, 0);
err_gpio_reset:
	if (bdata->power_gpio >= 0)
		ipt_gpio_setup(bdata->power_gpio, false, 0, 0);

err_gpio_power:
	ipt_gpio_setup(bdata->irq_gpio, false, 0, 0);

	return retval;
}

static int ipt_start_adsp(void)
{
	char enable = 0;

	if (gIptX350->alive_flag) {
		IPT_X350_INFO("### Detect already is running =====\n");
		return 0;
	}

	IPT_X350_INFO("### Detect is starting +++++\n");

	ipt_i2c_write(gIptX350->client, IPT_X350_REG_ADSP_STRAT, 0, &enable);

	gIptX350->alive_flag = true;

	return 0;
}

static int ipt_select_chg_protocol(unsigned char chg_type)
{
	IPT_X350_INFO("select_chg %d\n", chg_type);

	ipt_i2c_write(gIptX350->client, IPT_X350_REG_PROT_SEL,
			sizeof(chg_type), &chg_type);

	return 0;
}

static int ipt_intb_gpio_enable(unsigned char enable)
{
	IPT_X350_INFO("enable irq mode %d\n", enable);

	ipt_i2c_write(gIptX350->client, IPT_X350_REG_INTB_CTRL,
			sizeof(enable), &enable);

	return 0;
}


static int ipt_soft_reset(unsigned char enable)
{
	IPT_X350_INFO("soft_reset !!!\n");

	ipt_i2c_write(gIptX350->client, IPT_X350_REG_SOFT_RESET,
			sizeof(enable), &enable);

	return 0;
}

static int ipt_enter_sleep_mode(unsigned char enable)
{
	IPT_X350_INFO("enter sleep mode %d!!!\n", enable);

	ipt_i2c_write(gIptX350->client, IPT_X350_REG_SLEEP_CTRL,
			sizeof(enable), &enable);

	return 0;
}

static int ipt_enable_detect(unsigned char enable)
{
	IPT_X350_INFO("enter sleep mode %d!!!\n", enable);

	ipt_i2c_write(gIptX350->client, IPT_X350_REG_BC1D2_CTRL,
			sizeof(enable), &enable);

	return 0;
}

static int ipt_qc3d0_regulate(bool is_dp, unsigned int cnt)
{
	unsigned short reg_val = 0;

	IPT_X350_INFO("QC3.0 %s[%d]!!!\n", is_dp ? "DP" : "DM", cnt);

	reg_val = ((is_dp << IPT_X350_SHIFT_QC3D0_DP_DM) & IPT_X350_MASK_QC3D0_DP_DM)
		  | ((cnt << IPT_X350_SHIFT_QC3D0_CNT) & IPT_X350_MASK_QC3D0_CNT);

	reg_val = htons(reg_val);

	IPT_X350_INFO("reg val: 0x%04X\n", reg_val);

	ipt_i2c_write(gIptX350->client, IPT_X350_REG_QC3D0_REGULATE_MODE,
			sizeof(reg_val), &reg_val);

	return 0;
}

static int ipt_qc3dp_regulate(bool is_dp, unsigned int cnt)
{
	unsigned short reg_val = 0;

	IPT_X350_INFO("QC3.0+ %s[%d]!!!\n", is_dp ? "DP" : "DM", cnt);

	reg_val = ((is_dp << IPT_X350_SHIFT_QC3DP_DP_DM) & IPT_X350_MASK_QC3DP_DP_DM)
		  | ((cnt << IPT_X350_SHIFT_QC3DP_CNT) & IPT_X350_MASK_QC3DP_CNT);

	reg_val = htons(reg_val);

	IPT_X350_INFO("reg val: 0x%04X\n", reg_val);

	ipt_i2c_write(gIptX350->client, IPT_X350_REG_QC3DP_REGULATE_MODE,
			sizeof(reg_val), &reg_val);

	return 0;
}

static int ipt_get_chg_type(unsigned int *chg_type)
{
	unsigned short reg_val = 0, err_code = 0;


	ipt_i2c_read(gIptX350->client, IPT_X350_REG_CHG_TYPE_AND_ERR,
		       sizeof(reg_val), &reg_val);

	reg_val = ntohs(reg_val);

	IPT_X350_INFO("reg val: 0x%04X\n", reg_val);

	*chg_type = (reg_val & IPT_X350_MASK_CHG_TYPE) >> IPT_X350_SHIFT_CHG_TYPE;

	IPT_X350_INFO("chg_type: 0x%04X\n", *chg_type);

	err_code = (reg_val & IPT_X350_MASK_ERR_CODE) >> IPT_X350_SHIFT_ERR_CODE;

	IPT_X350_INFO("err_code: 0x%04X\n", err_code);

	if ((err_code == IPT_X350_VAL_ERR_CODE_NOERR)
			|| (err_code == IPT_X350_VAL_ERR_CODE_DET_SUCCESS)) {
		IPT_X350_INFO("Charger Detect Release\n", err_code);
		Charger_Detect_Release();
	} else if ((err_code == IPT_X350_VAL_ERR_CODE_TA_FAIL)
			|| (err_code == IPT_X350_VAL_ERR_CODE_TA_CAP_FAIL)) {
		IPT_X350_INFO("restarting adsp\n");
		/*ipt_start_adsp();*/
	}

	return 0;
}

static int ipt_get_firmware_version(unsigned short *fw_ver)
{
	unsigned short reg_val = 0;
	int ret = 0;

	ret = ipt_i2c_read(gIptX350->client, IPT_X350_REG_FIRMWARE_VERSION,
			     sizeof(reg_val), &reg_val);
	if (ret < 0) {
		IPT_X350_ERROR("reg val: 0x%04X\n", reg_val);
		return ret;
	}

	reg_val = ntohs(reg_val);

	IPT_X350_INFO("fw_ver: 0x%04X\n", reg_val);

	if (fw_ver)
		*fw_ver = reg_val;

	return 0;
}

#ifdef CONFIG_IPT_X350_USE_TEST_I2C
static int ipt_i2c_test(void)
{
	int ret = 0;

	IPT_X350_DEBUG_FUNC();

	ret = ipt_get_firmware_version(&gIptX350->FirmwareVer);
	if (ret < 0) {
		IPT_X350_ERROR("IPT_X350 device test failed!!!!\n");
		return ret;
	}

	gIptX350->appear = true;

	IPT_X350_DEBUG_FUNC();

	return 0;
}
#endif
static int ipt_inform_psy_changed(struct ipt_data *chip)
{
	int ret = 0;
	union power_supply_propval propval;
	enum charger_type mtk_chg_type = 0;

	switch (chip->chg_type) {
	case IPT_X350_VAL_CHG_TYPE_FLOAT:
		mtk_chg_type = NONSTANDARD_CHARGER;
		break;
	case IPT_X350_VAL_CHG_TYPE_SDP:
		mtk_chg_type = STANDARD_HOST;
		break;
	case IPT_X350_VAL_CHG_TYPE_CDP:
		mtk_chg_type = CHARGING_HOST;
		break;
	case IPT_X350_VAL_CHG_TYPE_DCP:
	case IPT_X350_VAL_CHG_TYPE_QC2D0:
	case IPT_X350_VAL_CHG_TYPE_QC3D0:
	case IPT_X350_VAL_CHG_TYPE_QC3DP_18W:
	case IPT_X350_VAL_CHG_TYPE_QC3DP_27W:
		mtk_chg_type = STANDARD_CHARGER;
		break;
	default:
		mtk_chg_type = CHARGER_UNKNOWN;
		break;
	}

	/* Get chg type det power supply */
	chip->psy = power_supply_get_by_name("charger");
	if (!chip->psy) {
		IPT_X350_ERROR("get charger power supply failed\n", __func__);
		return -EINVAL;
	}

	/* Inform chg det power supply */
	if (mtk_chg_type == 0)
		propval.intval = false;
	else
		propval.intval = true;
	ret = power_supply_set_property(chip->psy, POWER_SUPPLY_PROP_ONLINE, &propval);
	if (ret < 0)
		IPT_X350_ERROR("psy online failed, ret = %d\n", ret);

	propval.intval = mtk_chg_type;
	ret = power_supply_set_property(chip->psy, POWER_SUPPLY_PROP_CHARGE_TYPE, &propval);
	if (ret < 0)
		IPT_X350_ERROR("psy type failed, ret = %d\n", ret);

	power_supply_put(chip->psy);

	IPT_X350_INFO("Setting charger psy online & charger type successed %d\n", propval.intval);

	return ret;
}

static void ipt_int_handle_work(struct work_struct *work)
{
	char hvdcp_ctrl = 1;

	gIptX350->alive_flag = false;
	IPT_X350_INFO("### Detect is finished -----\n");

	ipt_get_chg_type(&gIptX350->chg_type);

	if (gIptX350->chg_type == IPT_X350_VAL_CHG_TYPE_FLOAT) {
		if (gIptX350->float_recheck_flag == false) {
			IPT_X350_INFO("float recheck status\n");
			queue_delayed_work(gIptX350->ipt_int_queue, &gIptX350->det_work,
									msecs_to_jiffies(500));
			gIptX350->float_recheck_flag = true;
		} else {
			IPT_X350_INFO("float recheck failed\n");
		}
	} else if (gIptX350->chg_type == IPT_X350_VAL_CHG_TYPE_HVDCP) {
		IPT_X350_INFO("starting check hvdcp\n");
		ipt_i2c_write(gIptX350->client, IPT_X350_REG_HVDCP_CTRL,
		       sizeof(hvdcp_ctrl), &hvdcp_ctrl);
	} else if (gIptX350->chg_type == IPT_X350_VAL_CHG_TYPE_UNKNOWN) {
		gIptX350->float_recheck_flag = false;
		IPT_X350_INFO("float recheck reinit\n");
	}

	if (get_boot_mode() == RECOVERY_BOOT) {
		ipt_inform_psy_changed(gIptX350);
	}

	IPT_X350_INFO("notify status changed %d\n", (gIptX350->chg_type != IPT_X350_VAL_CHG_TYPE_UNKNOWN));

	sqc_notify_daemon_changed(SQC_NOTIFY_USB,
					SQC_NOTIFY_USB_STATUS_CHANGED,
					(gIptX350->chg_type != IPT_X350_VAL_CHG_TYPE_UNKNOWN));
}

static irqreturn_t ipt_irq_handler(int irq, void *data)
{
	struct ipt_data *chip = data;

	IPT_X350_INFO("\n");

	queue_delayed_work(chip->ipt_int_queue, &chip->int_work, msecs_to_jiffies(100));

	return IRQ_HANDLED;
}

static int ipt_request_irq(struct ipt_data *chip)
{
	int ret = 0;

	IPT_X350_INFO("\n");

	chip->irq = gpio_to_irq(chip->bdata.irq_gpio);
	if (chip->irq < 0) {
		IPT_X350_ERROR("irq mapping fail(%d)\n", chip->irq);
		return ret;
	}

	/* Request threaded IRQ */
	ret = request_threaded_irq(chip->irq, NULL,
				   ipt_irq_handler, IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "ipt-irq",
				   chip);
	if (ret < 0) {
		IPT_X350_ERROR("request thread irq fail(%d)\n", ret);
		return ret;
	}

	enable_irq_wake(chip->irq);

	IPT_X350_INFO("irq = %d init finish\n", chip->irq);

	return 0;
}

static int ipt_status_init(void)
{
	/*ipt_start_adsp();*/
	IPT_X350_INFO("reset off status");
	gpio_set_value(gIptX350->bdata.reset_gpio, !gIptX350->bdata.reset_on_state);
	msleep(gIptX350->bdata.reset_active_ms);

	return 0;
}

static int ipt_status_remove(void)
{
	IPT_X350_INFO("reset on status");
	gIptX350->chg_type = IPT_X350_VAL_CHG_TYPE_UNKNOWN;
	gpio_set_value(gIptX350->bdata.reset_gpio, gIptX350->bdata.reset_on_state);

	return 0;
}

static int ipt_get_charger_type(int *chg_type)
{
	switch (gIptX350->chg_type) {
	case IPT_X350_VAL_CHG_TYPE_OCP:
		*chg_type = SQC_DCP_TYPE;
		break;
	case IPT_X350_VAL_CHG_TYPE_FLOAT:
		*chg_type = SQC_FLOAT_TYPE;
		break;
	case IPT_X350_VAL_CHG_TYPE_SDP:
		*chg_type = SQC_SDP_TYPE;
		break;
	case IPT_X350_VAL_CHG_TYPE_CDP:
		*chg_type = SQC_CDP_TYPE;
		break;
	case IPT_X350_VAL_CHG_TYPE_DCP:
		*chg_type = SQC_DCP_TYPE;
		break;
	case IPT_X350_VAL_CHG_TYPE_HVDCP:
		*chg_type = SQC_DCP_TYPE;
		break;
	case IPT_X350_VAL_CHG_TYPE_QC2D0:
		*chg_type = SQC_QC2D0_5V_TYPE;
		break;
	case IPT_X350_VAL_CHG_TYPE_QC3D0:
		*chg_type = SQC_QC3D0_TYPE;
		break;
	case IPT_X350_VAL_CHG_TYPE_QC3DP_18W:
		*chg_type = SQC_QC3D0_PLUS_18W_TYPE;
		break;
	case IPT_X350_VAL_CHG_TYPE_QC3DP_27W:
		*chg_type = SQC_QC3D0_PLUS_27W_TYPE;
		break;
	case IPT_X350_VAL_CHG_TYPE_QC3DP_45W:
		*chg_type = SQC_QC3D0_PLUS_27W_TYPE;
		break;
	case IPT_X350_VAL_CHG_TYPE_UNKNOWN:
		*chg_type = SQC_NONE_TYPE;
		break;
	default:
		*chg_type = SQC_NONE_TYPE;
		break;
	}

	IPT_X350_INFO("ipt_type: %d, chg_type: %d\n", gIptX350->chg_type, *chg_type);

	return 0;
}

static int ipt_set_charger_type(int chg_type)
{
	unsigned char ipt_type = 0;

	switch (chg_type) {
	case SQC_QC2D0_5V_TYPE:
		ipt_type = IPT_X350_VAL_PROT_QC2D0_5V;
		break;
	case SQC_QC2D0_9V_TYPE:
		ipt_type = IPT_X350_VAL_PROT_QC2D0_9V;
		break;
	case SQC_QC2D0_12V_TYPE:
		ipt_type = IPT_X350_VAL_PROT_QC2D0_12V;
		break;
	case SQC_QC3D0_TYPE:
		ipt_type = IPT_X350_VAL_PROT_QC3D0_5V;
		break;
	case SQC_QC3D0_PLUS_18W_TYPE:
	case SQC_QC3D0_PLUS_27W_TYPE:
	case SQC_QC3D0_PLUS_45W_TYPE:
		ipt_type = IPT_X350_VAL_PROT_QC3DP_5V;
		break;
	case SQC_PD3D0_APDO_TYPE:
	case SQC_PD3D0_BASE_TYPE:
	case SQC_SDP_TYPE:
	case SQC_CDP_TYPE:
	case SQC_DCP_TYPE:
	case SQC_FLOAT_TYPE:
		IPT_X350_ERROR("charing type: not support\n");
		return 0;
	default:
		IPT_X350_ERROR("charing type: UNKNOWN\n");
		return 0;
	}

	ipt_select_chg_protocol(ipt_type);

	return 0;
}

static int ipt_get_protocol_status(unsigned int *status)
{
	return 0;
}

static int ipt_get_chip_vendor_id(unsigned int *vendor_id)
{
	return 0;
}

static int ipt_set_qc3d0_dp(unsigned int dp_cnt)
{
	return ipt_qc3d0_regulate(true, dp_cnt);
}

static int ipt_set_qc3d0_dm(unsigned int dm_cnt)
{
	return ipt_qc3d0_regulate(false, dm_cnt);
}

static int ipt_set_qc3d0_plus_dp(unsigned int dp_cnt)
{
	return ipt_qc3dp_regulate(true, dp_cnt);
}

static int ipt_set_qc3d0_plus_dm(unsigned int dm_cnt)
{
	return ipt_qc3dp_regulate(false, dm_cnt);
}


struct sqc_bc1d2_proto_ops ipt_proto_node = {
	.status_init = ipt_status_init,
	.status_remove = ipt_status_remove,
	.get_charger_type = ipt_get_charger_type,
	.set_charger_type = ipt_set_charger_type,
	.get_protocol_status = ipt_get_protocol_status,
	.get_chip_vendor_id = ipt_get_chip_vendor_id,
	.set_qc3d0_dp = ipt_set_qc3d0_dp,
	.set_qc3d0_dm = ipt_set_qc3d0_dm,
	.set_qc3d0_plus_dp = ipt_set_qc3d0_plus_dp,
	.set_qc3d0_plus_dm = ipt_set_qc3d0_plus_dm,
};

static void ipt_det_handle_work(struct work_struct *work)
{
	ipt_start_adsp();
}

static int ipt_upgrade_request_image(void)
{
	int retval = 0;

	IPT_X350_INFO("into");

#ifdef IPT_X350_I2C_DATA_DEBUG
	int i = 0;
	gIptX350->fw_entry->data = kmalloc(4 * 1024,GFP_KERNEL);
	gIptX350->fw_entry->size = 4 * 1024;

	for (i = 0; i < gIptX350->fw_entry->size; i++) {
		gIptX350->fw_entry->data[i] = i;
	}

	return 0;
#endif

	if (strlen(gIptX350->image_name) == 0) {
		snprintf(gIptX350->image_name, sizeof(gIptX350->image_name), "%s", IPT_X350_IMAGE_NAME);
	}

	retval = request_firmware(&gIptX350->fw_entry, gIptX350->image_name,
			&gIptX350->client->dev);
	if (retval != 0) {
		IPT_X350_ERROR("Firmware image %s not available\n", gIptX350->image_name);
		return -EINVAL;
	}

	IPT_X350_INFO("exit");

	return 0;
}

static int ipt_check_firmware_crc(unsigned int *file, unsigned int file_size)
{
	unsigned int crc = 0xFFFF1326;
	const unsigned int poly = 0x04C11DB6;
	unsigned int newbit, newword, rl_crc;
    unsigned short i = 0,j = 0;

	IPT_X350_INFO("into");

    for(i = 0; i < file_size / 4; i++) {
		for(j = 0; j < 32; j++) {
			newbit = ((crc>>31) ^ ((*file >> j) & 1)) & 1;
			if(newbit)
				newword=poly;
			else
				newword=0;
			rl_crc = (crc<<1) | newbit;
			crc = rl_crc ^ newword;
		}
		file++;
    }

    if(crc == 0xC704DD7B) {
		IPT_X350_ERROR("check crc ok\n");
		return 0; 
	}

	return -EINVAL;
}

static int ipt_upgrade_cmp_version(void)
{
	unsigned char lbit = 0, hbit = 0;
	unsigned short image_version = 0;
	unsigned int version_addr = 0;

	IPT_X350_INFO("into");

	version_addr = gIptX350->fw_entry->size - 8;

	hbit = gIptX350->fw_entry->data[version_addr];
	lbit = gIptX350->fw_entry->data[version_addr + 1];

	image_version = (hbit << 8) | lbit;

	IPT_X350_INFO("image_version: 0x%04X, ic_version: 0x%04X\n",
		image_version, gIptX350->FirmwareVer);

	if (image_version != gIptX350->FirmwareVer) {
		IPT_X350_INFO("firmware need upgrade\n");
		return false;
	}

	return true;
}

static int ipt_check_is_mi_firmware(void)
{
	unsigned char reg_val[4] = {0, };
	unsigned char check_val[4] = {0x49, 0x33, 0x35, 0x30};
	unsigned char data_val = 0;
	int ret = 0;

	ret = ipt_read_eeprom(gIptX350->client, IPT_X350_EEPROM_ADDR, IPT_X350_REG_VENDOR_ID, reg_val, sizeof(reg_val));
	if (ret < 0) {
		IPT_X350_ERROR("check is already test mode failed\n");
		return -ENODEV;
	}

	IPT_X350_INFO("reg_val[0x13]: 0x%02X, 0x%02X, 0x%02X, 0x%02X\n", reg_val[0], reg_val[1],
					reg_val[2], reg_val[3]);

	if (!memcmp(reg_val + 1, check_val + 1, sizeof(check_val) - 1)) {
		IPT_X350_INFO("check vendor id ok\n");
		data_val = 0x6B;
		ipt_write_eeprom(gIptX350->client, IPT_X350_EEPROM_ADDR, 0x40, &data_val, sizeof(data_val));
		ipt_read_eeprom(gIptX350->client, IPT_X350_EEPROM_ADDR, 0x40, &data_val, sizeof(data_val));
		if (data_val == 0x6B) {
			IPT_X350_INFO("check reg[0x40]:0x6B is ok\n");
			return 0;
		} else {
			IPT_X350_ERROR("check reg[0x40]:0x6B is failed\n");
			return -EINVAL;
		}
	}

	return -EINVAL;
}


static int ipt_check_enter_test_mode(void)
{
	unsigned char reg_val[4] = {0, };
	unsigned char check_val[4] = {0x49, 0x33, 0x35, 0x30};
	unsigned char check_val2[4] = {0x00, 0x9F, 0x07, 0x30};
	unsigned char data_val = 0;
	int ret = 0;

	ret = ipt_i2c_read(gIptX350->client, IPT_X350_REG_VENDOR_ID,
		       sizeof(reg_val), reg_val);
	if (ret < 0) {
		IPT_X350_ERROR("read vendor id failed, check is already test mode?\n");
		goto is_already_test_mode;
	}

	IPT_X350_INFO("reg_val[0x13]: 0x%02X, 0x%02X, 0x%02X, 0x%02X\n", reg_val[0], reg_val[1],
					reg_val[2], reg_val[3]);

	if (!memcmp(reg_val + 1, check_val + 1, sizeof(check_val) - 1)) {
		IPT_X350_INFO("check vendor id ok\n");
		data_val = 0x6B;
		ipt_i2c_write(gIptX350->client, 0x40, sizeof(data_val), &data_val);
		ipt_read_eeprom(gIptX350->client, IPT_X350_EEPROM_ADDR, 0x40, &data_val, sizeof(data_val));
		if (data_val == 0x6B) {
			IPT_X350_INFO("check reg[0x40]:0x6B is ok\n");
			return 0;
		} else {
			IPT_X350_ERROR("check reg[0x40]:0x6B is failed\n");
			return -EINVAL;
		}
	}

is_already_test_mode:
	ret = ipt_read_eeprom(gIptX350->client, IPT_X350_EEPROM_ADDR, 0x41, reg_val, sizeof(reg_val));
	if (ret < 0) {
		IPT_X350_ERROR("check is already test mode failed\n");
		return -EINVAL;
	}

	IPT_X350_INFO("reg_val[0x41]: 0x%02X, 0x%02X, 0x%02X, 0x%02X\n", reg_val[0], reg_val[1],
					reg_val[2], reg_val[3]);

	if (!memcmp(reg_val, check_val2, sizeof(check_val2))) {
		IPT_X350_INFO("check chip characteristics ok\n");
		return 0;
	}

	ret = ipt_check_is_mi_firmware();
	if (ret < 0) {
		IPT_X350_ERROR("check is mi firmware failed\n");
		return -EINVAL;
	}

	return 0;
}

static int ipt_upgrade_init_program(void)
{
	unsigned char data = 0;
	int ret = 0;

	data = 0x1f;
	ret = ipt_write_eeprom(gIptX350->client, IPT_X350_EEPROM_ADDR, 0x42, &data, sizeof(data));
	if (ret < 0) {
		IPT_X350_ERROR("write reg[0x42]: 0x1f, failed!!!\n");
		return ret;
	}

	data = 0x9f;
	ret |= ipt_write_eeprom(gIptX350->client, IPT_X350_EEPROM_ADDR, 0x43, &data, sizeof(data));
	if (ret < 0) {
		IPT_X350_ERROR("write reg[0x43]: 0x9f, failed!!!\n");
		return ret;
	}

	data = 0x33;
	ret |= ipt_write_eeprom(gIptX350->client, IPT_X350_EEPROM_ADDR, 0x44, &data, sizeof(data));
	if (ret < 0) {
		IPT_X350_ERROR("write reg[0x44]: 0x33, failed!!!\n");
		return ret;
	}

	return 0;
}

static int ipt_upgrade_burn_flash(unsigned short addr, const unsigned char *data, unsigned int len)
{
	unsigned char write_data = 0;
	unsigned char read_data = 0;
	unsigned char burn_data[6] = {0, };
	int ret = 0, count = 0;

	/*Select memory block, configure address and data*/
	burn_data[0] = addr & 0xFF;
	burn_data[1] = (addr >> 8) & 0xFF;
	burn_data[2] = data[0];
	burn_data[3] = data[1];
	burn_data[4] = data[2];
	burn_data[5] = data[3];
	ret = ipt_write_eeprom(gIptX350->client, IPT_X350_EEPROM_ADDR, 0x45, burn_data, sizeof(burn_data));
	if (ret < 0) {
		IPT_X350_ERROR("write reg[0x45], addr: 0x%04X failed!!!\n", addr);
		return ret;
	}

	/*Write enable*/
	write_data = 1;
	ret = ipt_write_eeprom(gIptX350->client, IPT_X350_EEPROM_ADDR, 0x4B, &write_data, sizeof(write_data));
	if (ret < 0) {
		IPT_X350_ERROR("write reg[0x4B]: 0x1, failed!!!\n");
		return ret;
	}

	write_data = 0;
	ret = ipt_write_eeprom(gIptX350->client, IPT_X350_EEPROM_ADDR, 0x4B, &write_data, sizeof(write_data));
	if (ret < 0) {
		IPT_X350_ERROR("write reg[0x4B]: 0x0, failed!!!\n");
		return ret;
	}

	/*Wait for a data program to complete, The timeout is 50*2000us*/
	do {
		ret = ipt_read_eeprom(gIptX350->client, IPT_X350_EEPROM_ADDR, 0x4B, &read_data, sizeof(read_data));
		if (ret < 0) {
			IPT_X350_ERROR("read reg[0x4B] failed!!!\n");
			return ret;
		}

		if (!(read_data & BIT(7)))
			return 0;

		usleep_range(2, 5);
		
	} while(count++ < 100);

	IPT_X350_ERROR("Wait for a data program, timeout!!!\n");

	return -EINVAL;
}

static int ipt_upgrade_erase_flash_loop(void)
{
	unsigned int pos_head = 0, burn_len = 0, len = 4096;
	unsigned char zero_array[4] = {0x00, 0x00, 0x00, 0x00};
	int ret = 0;

	IPT_X350_INFO("into");
/*
	if (len != (4 * 1024)) {
		IPT_X350_ERROR("check fw_data len failed, 0x%04X\n", len);
		return -EINVAL;
	}
*/
	while(1) {
		if ((pos_head + 4) < len)
			burn_len = 4;
		else
			burn_len = len - pos_head;

		/*IPT_X350_INFO("LOOP[E]: pos_head 0x%04X/0x%04X, burn_len: 0x%04X\n", pos_head, len, burn_len);*/

		ret = ipt_upgrade_burn_flash(pos_head / 4, zero_array, burn_len);
		if (ret < 0) {
			IPT_X350_ERROR("program pos: 0x%04X failed!!!\n", pos_head);
			return ret;
		}

		pos_head += 4;

		if (pos_head >= len) {
			IPT_X350_INFO("BREAK[W]: pos_head 0x%04X/0x%04X, burn_len: 0x%04X\n", pos_head, len, burn_len);
			break;
		}
	}

	return 0;
}

static int ipt_upgrade_burn_flash_loop(const unsigned char *fw_data, unsigned int len)
{
	unsigned int pos_head = 0, burn_len = 0;
	unsigned char zero_array[IPT_BURN_LEN] = {0x00, 0x00, 0x00, 0x00};
	int ret = 0;

	IPT_X350_INFO("into");
/*
	if (len != (4 * 1024)) {
		IPT_X350_ERROR("check fw_data len failed, 0x%04X\n", len);
		return -EINVAL;
	}
*/
	IPT_X350_INFO("burn firmware\n");

	while(1) {
		if ((pos_head + IPT_BURN_LEN) < len)
			burn_len = IPT_BURN_LEN;
		else
			burn_len = len - pos_head;

		/*IPT_X350_INFO("LOOP[W]: pos_head 0x%04X/0x%04X, burn_len: 0x%04X\n", pos_head, len, burn_len);*/
		if (pos_head == 0) {
			ret = ipt_upgrade_burn_flash(pos_head / IPT_BURN_LEN, zero_array, burn_len);
		} else {
			ret = ipt_upgrade_burn_flash(pos_head / IPT_BURN_LEN, fw_data + pos_head, burn_len);
		}

		if (ret < 0) {
			IPT_X350_ERROR("program pos: 0x%04X failed!!!\n", pos_head);
			return ret;
		}

		pos_head += IPT_BURN_LEN;

		if (pos_head >= len) {
			IPT_X350_INFO("BREAK[W]: pos_head 0x%04X/0x%04X, burn_len: 0x%04X\n", pos_head, len, burn_len);
			break;
		}
	}

	IPT_X350_INFO("erase tail\n");

	while(1) {
		if ((pos_head + IPT_BURN_LEN) < len)
			burn_len = IPT_BURN_LEN;
		else
			burn_len = len - pos_head;

		/*PT_X350_INFO("LOOP[W]: pos_head 0x%04X/0x%04X, burn_len: 0x%04X\n", pos_head, len, burn_len);*/

		ret = ipt_upgrade_burn_flash(pos_head / IPT_BURN_LEN, zero_array, burn_len);
		if (ret < 0) {
			IPT_X350_ERROR("program pos: 0x%04X failed!!!\n", pos_head);
			return ret;
		}

		pos_head += IPT_BURN_LEN;

		if (pos_head >= IPT_FLASH_SIZE) {
			IPT_X350_INFO("BREAK[W]: pos_head 0x%04X/0x%04X, burn_len: 0x%04X\n", pos_head, len, burn_len);
			break;
		}
	}

	IPT_X350_INFO("enable firmware\n");

	ret = ipt_upgrade_burn_flash(0, fw_data, burn_len);
	if (ret < 0) {
		IPT_X350_ERROR("program pos: 0x%04X failed!!!\n", pos_head);
		return ret;
	}

	return 0;
}

static int ipt_upgrade_burn_finish(void)
{
	unsigned char data = 0x30;
	int ret = 0;

	IPT_X350_INFO("into");

	ret = ipt_write_eeprom(gIptX350->client, IPT_X350_EEPROM_ADDR, 0x44, &data, sizeof(data));
	if (ret < 0) {
		IPT_X350_ERROR("write reg[0x44]: 0x30, failed!!!\n");
		return ret;
	}

	return 0;
}

static int ipt_upgrade_verify_init(void)
{
	unsigned char data = 0;
	int ret = 0;

	IPT_X350_INFO("into");

	/*CPU reset*/
	data = 0x1f;
	ret = ipt_write_eeprom(gIptX350->client, IPT_X350_EEPROM_ADDR, 0x42, &data, sizeof(data));
	if (ret < 0) {
		IPT_X350_ERROR("write reg[0x42]: 0x1f, failed!!!\n");
		return ret;
	}

	/*close watchdog,open memory clock*/
	data = 0x9f;
	ret = ipt_write_eeprom(gIptX350->client, IPT_X350_EEPROM_ADDR, 0x43, &data, sizeof(data));
	if (ret < 0) {
		IPT_X350_ERROR("write reg[0x43]: 0x9f, failed!!!\n");
		return ret;
	}

	/*write READ to 1*/
	data = 0x37;
	ret = ipt_write_eeprom(gIptX350->client, IPT_X350_EEPROM_ADDR, 0x44, &data, sizeof(data));
	if (ret < 0) {
		IPT_X350_ERROR("write reg[0x44]: 0x37, failed!!!\n");
		return ret;
	}

	return 0;
}

static int ipt_upgrade_verify_end(void)
{
	unsigned char data = 0;
	int ret = 0;

	IPT_X350_INFO("into");

	/*write READ to 1*/
	data = 0x30;
	ret = ipt_write_eeprom(gIptX350->client, IPT_X350_EEPROM_ADDR, 0x44, &data, sizeof(data));
	if (ret < 0) {
		IPT_X350_ERROR("write reg[0x44]: 0x30, failed!!!\n");
		return ret;
	}

	return 0;
}

static int ipt_upgrade_read_flash(unsigned short addr, char *data, unsigned int len)
{
	int ret = 0;

	/*set addr*/
	ret = ipt_write_eeprom(gIptX350->client, IPT_X350_EEPROM_ADDR, 0x45, (char *)&addr, sizeof(addr));
	if (ret < 0) {
		IPT_X350_ERROR("write reg[0x45]: 0x%04X, failed!!!\n", addr);
		return ret;
	}

#ifdef IPT_X350_I2C_DATA_DEBUG
	memcpy(data, gIptX350->fw_entry->data + addr, len);
#endif

	/*read data*/
	ret = ipt_read_eeprom(gIptX350->client, IPT_X350_EEPROM_ADDR, 0x4C, data, len);
	if (ret < 0) {
		IPT_X350_ERROR("read reg[0x4B]: failed!!!\n");
		return ret;
	}

	return 0;
}

static int ipt_upgrade_check_flash_loop(const unsigned char *fw_data, unsigned int len)
{
	unsigned int pos_head = 0, burn_len = 0;
	char cmp_buf[4] = {0,};
	int ret = 0;

	IPT_X350_INFO("into");
/*
	if (len != (4 * 1024)) {
		IPT_X350_ERROR("check fw_data len failed, 0x%04X\n", len);
		return -EINVAL;
	}
*/
	while(1) {
		if ((pos_head + 4) < len)
			burn_len = 4;
		else
			burn_len = len - pos_head;

		/*IPT_X350_INFO("LOOP[R]: pos_head 0x%04X/0x%04X, burn_len: 0x%04X\n", pos_head, len, burn_len);*/

		memset(cmp_buf, 0, sizeof(cmp_buf));
		ret = ipt_upgrade_read_flash(pos_head / 4, cmp_buf, burn_len);
		if (ret < 0) {
			IPT_X350_ERROR("read flash failed!!!\n");
			return ret;
		}

		if(memcmp(cmp_buf, fw_data + pos_head, burn_len)) {
			IPT_X350_ERROR("========\n", burn_len);
			print_mem(cmp_buf, burn_len);
			IPT_X350_ERROR("========\n", burn_len);
			print_mem(fw_data + pos_head, burn_len);
			IPT_X350_ERROR("========\n", burn_len);
			IPT_X350_ERROR("check fw_data failed, 0x%04X\n", burn_len);
			return -EINVAL;
		}

		pos_head += 4;

		if (pos_head >= len) {
			IPT_X350_INFO("BREAK[R]: pos_head 0x%04X/0x%04X, burn_len: 0x%04X\n", pos_head, len, burn_len);
			break;
		}
	}

	return 0;
}

static int ipt_upgrade_clean_status(void)
{
	IPT_X350_INFO("into");

#ifdef IPT_X350_I2C_DATA_DEBUG
	kfree(gIptX350->fw_entry->data);
	return 0;
#endif

	if (gIptX350->fw_entry)
		release_firmware(gIptX350->fw_entry);

	return 0;
}

static void ipt_upgrade_handle_work(struct work_struct *work)
{
	IPT_X350_INFO("upgrade into\n");

	if (ipt_upgrade_request_image() != 0) {
		IPT_X350_ERROR("request image failed\n");
		goto failed_loop;
	}

	if (ipt_check_firmware_crc((unsigned int *)gIptX350->fw_entry->data,
							gIptX350->fw_entry->size) != 0) {
		IPT_X350_ERROR("check firmware crc failed\n");
		goto failed_loop;
	}

	if (ipt_upgrade_cmp_version() != 0) {
		IPT_X350_ERROR("enter cmp version failed\n");
		goto failed_loop;
	}

	if (ipt_check_enter_test_mode() != 0) {
		IPT_X350_ERROR("enter test mode failed\n");
		goto failed_loop;
	}

	if (ipt_upgrade_init_program() != 0) {
		IPT_X350_ERROR("enable init program failed\n");
		goto failed_loop;
	}
/*
	if (ipt_upgrade_erase_flash_loop() != 0) {
		IPT_X350_ERROR("erase flash loop failed\n");
		goto failed_loop;
	}
*/
	if (ipt_upgrade_burn_flash_loop(gIptX350->fw_entry->data,
							gIptX350->fw_entry->size) != 0) {
		IPT_X350_ERROR("burn flash loop failed\n");
		goto failed_loop;
	}

	if (ipt_upgrade_burn_finish() != 0) {
		IPT_X350_ERROR("burn finish failed\n");
		goto failed_loop;
	}
	
	if (ipt_upgrade_verify_init() != 0) {
		IPT_X350_ERROR("burn verify_init failed\n");
		goto failed_loop;
	}

	if (ipt_upgrade_check_flash_loop(gIptX350->fw_entry->data,
							gIptX350->fw_entry->size) != 0) {
		IPT_X350_ERROR("check flash loop failed\n");
		goto failed_loop;
	}

	if (ipt_upgrade_verify_end() != 0) {
		IPT_X350_ERROR("burn verify_end failed\n");
		goto failed_loop;
	}

failed_loop:
	if (ipt_upgrade_clean_status() != 0) {
		IPT_X350_ERROR("clean status failed\n");
	}

	ipt_hw_reset();

	IPT_X350_INFO("upgrade exit\n");
}

static int ipt_otg_tcp_notifier_call(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct tcp_notify *noti = data;

	switch (event) {
	case TCP_NOTIFY_TYPEC_STATE:
		IPT_X350_INFO("%s, TCP_NOTIFY_TYPEC_STATE, old_state=%d, new_state=%d\n",
				__func__, noti->typec_state.old_state,
				noti->typec_state.new_state);

		if (noti->typec_state.old_state == TYPEC_UNATTACHED &&
			noti->typec_state.new_state == TYPEC_ATTACHED_SNK) {
			IPT_X350_INFO("Charger Detect Init\n");
			Charger_Detect_Init();
		}

		if (noti->typec_state.old_state == TYPEC_UNATTACHED &&
			noti->typec_state.new_state == TYPEC_ATTACHED_SRC) {
			IPT_X350_INFO("%s OTG Plug in\n", __func__);
			gIptX350->usbc_otg_attached = true;
			if (gIptX350->bdata.reset_gpio >= 0) {
				IPT_X350_INFO("%s ipt350 reset on status\n", __func__);
				gpio_set_value(gIptX350->bdata.reset_gpio, gIptX350->bdata.reset_on_state);
			}
		} else if ((noti->typec_state.old_state == TYPEC_ATTACHED_SRC ||
			noti->typec_state.old_state == TYPEC_ATTACHED_SNK) &&
			noti->typec_state.new_state == TYPEC_UNATTACHED) {
			if (gIptX350->usbc_otg_attached) {
				IPT_X350_INFO("%s OTG Plug out\n", __func__);
				gIptX350->usbc_otg_attached = false;
			}

			if (gIptX350->bdata.reset_gpio >= 0) {
				IPT_X350_INFO("%s ipt350 reset off status\n", __func__);
				gpio_set_value(gIptX350->bdata.reset_gpio, !gIptX350->bdata.reset_on_state);
			}
		}
		break;

	}

	return NOTIFY_OK;
}

static int ipt_register_notifier(void)
{
	int ret = 0;

	gIptX350->otg_tcpc_dev = tcpc_dev_get_by_name("type_c_port0");
	if (!gIptX350->otg_tcpc_dev) {
		IPT_X350_INFO("%s get tcpc device type_c_port0 fail\n", __func__);
		return -ENODEV;
	}

	gIptX350->otg_nb.notifier_call = ipt_otg_tcp_notifier_call;
	ret = register_tcp_dev_notifier(gIptX350->otg_tcpc_dev, &gIptX350->otg_nb,
		TCP_NOTIFY_TYPE_USB|TCP_NOTIFY_TYPE_VBUS|TCP_NOTIFY_TYPE_MISC);
	if (ret < 0) {
		IPT_X350_INFO("%s register tcpc notifer fail\n", __func__);
		return -EINVAL;
	}


	return 0;
}

int ipt_sleep_node_set(const char *val, const void *arg)
{
	int sleep_mode_enable = 0;
	const struct ipt_board_data *bdata = NULL;

	if (!gIptX350) {
		IPT_X350_ERROR("gIptX350 is null\n");
		return -EINVAL;
	}

	bdata = &gIptX350->bdata;

	sscanf(val, "%d", &sleep_mode_enable);

	IPT_X350_INFO("force_sleep = %d\n", gIptX350->force_sleep);

	if (gIptX350->force_sleep != sleep_mode_enable) {
		gIptX350->force_sleep = sleep_mode_enable;
		if (gIptX350->bdata.reset_gpio >= 0) {
			if (sleep_mode_enable) {
				IPT_X350_INFO("reset on status");
				/*ipt_enter_sleep_mode(true);*/
				gpio_set_value(bdata->reset_gpio, bdata->reset_on_state);
				ipt_set_prop_by_name("battery", POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, 0);
			} else {
				IPT_X350_INFO("reset off status");
				/*ipt_enter_sleep_mode(false);*/
				gpio_set_value(bdata->reset_gpio, !bdata->reset_on_state);
				ipt_set_prop_by_name("battery", POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, 1);
			}
		}
	}

	return 0;
}

int ipt_sleep_node_get(char *val, const void *arg)
{
	if (!gIptX350) {
		IPT_X350_ERROR("gIptX350 is null\n");
		return snprintf(val, PAGE_SIZE, "0");
	}

	return snprintf(val, PAGE_SIZE, "%u", gIptX350->force_sleep);
}

static struct zte_misc_ops qc3dp_sleep_mode_node = {
	.node_name = "qc3dp_sleep_mode",
	.set = ipt_sleep_node_set,
	.get = ipt_sleep_node_get,
	.free = NULL,
	.arg = NULL,
};

static int ipt_chg_type_show(struct seq_file *m, void *v)
{
	int chg_val = 0, chg_type = 0;

	ipt_get_charger_type(&chg_type);

	switch (chg_type) {
	case SQC_QC3D0_TYPE:
	case SQC_QC3D0_PLUS_18W_TYPE:
	case SQC_QC3D0_PLUS_27W_TYPE:
	case SQC_QC3D0_PLUS_45W_TYPE:
		chg_val = 1;
		break;
	default:
		chg_val = 0;
		break;
	}

	seq_printf(m, "%d", chg_val);

	return 0;
}

static int ipt_chg_type_open(struct inode *inode, struct file *file)
{
 return single_open(file, ipt_chg_type_show, NULL);
}

static const struct file_operations ipt_chg_type_node = {
	.owner = THIS_MODULE,
	.open = ipt_chg_type_open,
	.read = seq_read,
	.llseek	= seq_lseek,
};

static int ipt_chg_id_show(struct seq_file *m, void *v)
{

	seq_printf(m, "%d", !!gIptX350->FirmwareVer);

	return 0;
}

static int ipt_chg_id_open(struct inode *inode, struct file *file)
{
 return single_open(file, ipt_chg_id_show, NULL);
}

static const struct file_operations ipt_chg_id_node = {
	.owner = THIS_MODULE,
	.open = ipt_chg_id_open,
	.read = seq_read,
	.llseek	= seq_lseek,
};


static int ipt_probe_work(struct work_struct *work)
{
	if (ipt_set_gpio(gIptX350)) {
		IPT_X350_ERROR("Failed to set up GPIO's\n");
		goto err_loop;
	}

	mutex_init(&(gIptX350->i2c_mutex));

	gIptX350->ipt_int_queue = create_singlethread_workqueue("ipt_int_queue");
	INIT_DELAYED_WORK(&gIptX350->int_work, ipt_int_handle_work);

	if (ipt_request_irq(gIptX350)) {
		IPT_X350_ERROR("Failed to request irq\n");
		goto err_loop;
	}

	disable_irq(gIptX350->irq);

	INIT_DELAYED_WORK(&gIptX350->det_work, ipt_det_handle_work);

	INIT_DELAYED_WORK(&gIptX350->upgrade_work, ipt_upgrade_handle_work);

#ifdef CONFIG_IPT_X350_USE_TEST_I2C
	if (ipt_i2c_test()) {

		ipt_upgrade_handle_work(NULL);

		msleep(30);

		if (ipt_i2c_test()) {
			IPT_X350_ERROR("force upgrade failed\n");
			ipt_power_switch(false);
			goto err_loop;
		}
	} else {
		ipt_upgrade_handle_work(NULL);
	}
	
#else
	gIptX350->appear = true;
#endif

	ipt_intb_gpio_enable(true);

	sqc_hal_bc1d2_register(&ipt_proto_node);

	ipt_enable_detect(true);

	/*check the usb is already online ?*/
	IPT_X350_INFO("### check usb is already online ?\n");
	queue_delayed_work(gIptX350->ipt_int_queue,
			   &gIptX350->det_work, msecs_to_jiffies(500));

	ipt_register_notifier();

	proc_create_data("driver/chg_type", 0664, NULL,
			&ipt_chg_type_node, NULL);

	proc_create_data("driver/chg_id", 0664, NULL,
			&ipt_chg_id_node, NULL);

	zte_misc_register_callback(&qc3dp_sleep_mode_node, gIptX350);

	enable_irq(gIptX350->irq);

	return 0;

err_loop:
	mutex_destroy(&(gIptX350->i2c_mutex));

	free_irq(gIptX350->irq, gIptX350);

	if (gIptX350->bdata.irq_gpio > 0)
		ipt_gpio_setup(gIptX350->bdata.irq_gpio, false, 0, 0);

	if (gIptX350->bdata.reset_gpio > 0)
		ipt_gpio_setup(gIptX350->bdata.reset_gpio, false, 0, 0);

	if (gIptX350->bdata.power_gpio > 0)
		ipt_gpio_setup(gIptX350->bdata.power_gpio, false, 0, 0);

	kfree(gIptX350);
	gIptX350 = NULL;

	IPT_X350_INFO("### IPT ERROR DONE ###\n");

	return -ENODEV;
}


static int ipt_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	IPT_X350_INFO("IPT_X350 Driver Version: %s\n", IPT_X350_DRIVER_VERSION);
	IPT_X350_INFO("IPT_X350 I2C Address: 0x%02x\n", client->addr);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		IPT_X350_ERROR("I2C check functionality failed.\n");
		return -ENODEV;
	}

	gIptX350 = kzalloc(sizeof(struct ipt_data), GFP_KERNEL);
	if (!gIptX350) {
		IPT_X350_ERROR("Failed to alloc mem for rmi4_data\n");
		return -ENOMEM;
	}

	gIptX350->client = client;

	if (ipt_parse_dt(&client->dev, &gIptX350->bdata)) {
		IPT_X350_ERROR("Failed to parse dt\n");
		goto err_parse_dt;
	}

	sqc_sequence_load_init(ipt_probe_work, 0, 1);

	return 0;

err_parse_dt:
	kfree(gIptX350);
	gIptX350 = NULL;
	return 0;
}

static int ipt_remove(struct i2c_client *client)
{
	IPT_X350_INFO();

	if (gIptX350 == NULL) {
		IPT_X350_INFO("ipt350 is not initialized!!!");
		return 0;
	}

	sqc_hal_bc1d2_unregister();

	if (gIptX350->bdata.reset_gpio >= 0) {
		IPT_X350_INFO("reset on status +++++");
		gpio_set_value(gIptX350->bdata.reset_gpio, gIptX350->bdata.reset_on_state);
	}

	return 0;
}

static void ipt_shutdown(struct i2c_client *client)
{
	IPT_X350_INFO();

	if (gIptX350 == NULL) {
		IPT_X350_INFO("ipt350 is not initialized!!!");
		return;
	}

	IPT_X350_INFO("### ipt350 shuting down ###\n");

	sqc_hal_bc1d2_unregister();

	ipt_enable_detect(false);
}

static struct i2c_driver ipt_driver = {
	.probe = ipt_probe,
	.remove = ipt_remove,
	.shutdown = ipt_shutdown,
	.id_table = ipt_id,
	.driver = {
		.name = IPT_I2C_DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = ipt_match_table,
	},
};

static int __init ipt_init(void)
{
	IPT_X350_DEBUG_FUNC();

	pr_info("%s init\n", __func__);

	return i2c_add_driver(&ipt_driver);
}

static void __exit ipt_exit(void)
{
	IPT_X350_DEBUG_FUNC();

	return i2c_del_driver(&ipt_driver);
}

late_initcall(ipt_init);
module_exit(ipt_exit);

MODULE_AUTHOR("ztecharger@zte.com.cn");
MODULE_DESCRIPTION("IPT_X350 Driver For SQC");
MODULE_LICENSE("GPL");

