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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
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
#define I2C_DRIVER_NAME	 	"wt6670_detect"
#define WT6670_DRIVER_VERSION	"BC1.2 driver v0.1"

#define WT6670_IMAGE_NAME	"wt667f_firmware.img"


//---------------------------------------------------------------------
#define CONFIG_WT6670_USE_TEST_I2C
#define WT6670_PRESS_USE_PM

//---------------------------------------------------------------------
#define WT6670_DEBUG_ON 				1
#define WT6670_DEBUG_FUNC_ON 		0
#define IIC_EEPROM 					0
//---------------------------------------------------------------------
#define WT6670_REG_ADSP_STRAT 			0xA0

#define WT6670_REG_PROT_SEL 				0xA1
#define WT6670_MASK_PROT_SEL				0xFF
#define WT6670_VAL_PROT_QC2D0_5V 		0x01
#define WT6670_VAL_PROT_QC2D0_9V 		0x02
#define WT6670_VAL_PROT_QC2D0_12V 		0x03
#define WT6670_VAL_PROT_QC3D0_5V 		0x04
#define WT6670_VAL_PROT_QC3DP_5V 		0x05

#define WT6670_REG_INTB_CTRL 			0xA2
#define WT6670_MASK_INTB_CTRL			0xFF

#define WT6670_REG_SOFT_RESET 			0xA3
#define WT6670_MASK_SOFT_RESET			0xFF

#define WT6670_REG_FLOAT_CHGING 			0xA4
#define WT6670_MASK_FLOAT_CHGING			0xFF

#define WT6670_REG_SLEEP_CTRL 			0xA5
#define WT6670_MASK_SLEEP_CTRL			0xFF
#define WT6670_VAL_SLEEP_CTRL_DISABLE 	0x00
#define WT6670_VAL_SLEEP_CTRL_ENABLE 	0x01

#define WT6670_REG_DETECT_CTRL 			0xA6
#define WT6670_MASK_DETECT_CTRL			0xFF
#define WT6670_VAL_DETECT_CTRL_DISABLE 	0x00
#define WT6670_VAL_DETECT_CTRL_ENABLE 	0x01


#define WT6670_REG_QC3D0_REGULATE_MODE	0xAA
#define WT6670_MASK_QC3D0_DP_DM			BIT(15)
#define WT6670_SHIFT_QC3D0_DP_DM			15
#define WT6670_VAL_QC3D0_DM 				0x00
#define WT6670_VAL_QC3D0_DP			 	0x01
#define WT6670_MASK_QC3D0_CNT			0x7FFF
#define WT6670_SHIFT_QC3D0_CNT			0


#define WT6670_REG_QC3DP_REGULATE_MODE	0xAB
#define WT6670_MASK_QC3DP_DP_DM			BIT(15)
#define WT6670_SHIFT_QC3DP_DP_DM			15
#define WT6670_VAL_QC3DP_DM 				0x00
#define WT6670_VAL_QC3DP_DP			 	0x01
#define WT6670_MASK_QC3DP_CNT			0x7FFF
#define WT6670_SHIFT_QC3DP_CNT			0

#define WT6670_REG_CHG_TYPE_AND_ERR		0xB0
#define WT6670_MASK_CHG_TYPE				0xFF00
#define WT6670_SHIFT_CHG_TYPE			8
#define WT6670_VAL_CHG_TYPE_FLOAT		0x01
#define WT6670_VAL_CHG_TYPE_SDP			0x02
#define WT6670_VAL_CHG_TYPE_CDP			0x03
#define WT6670_VAL_CHG_TYPE_DCP			0x04
#define WT6670_VAL_CHG_TYPE_QC2D0		0x05
#define WT6670_VAL_CHG_TYPE_QC3D0		0x06
#define WT6670_VAL_CHG_TYPE_OCP			0x07
#define WT6670_VAL_CHG_TYPE_QC3DP_18W	0x08
#define WT6670_VAL_CHG_TYPE_QC3DP_27W	0x09
#define WT6670_VAL_CHG_TYPE_UNKNOWN		0x11
#define WT6670_VAL_CHG_TYPE_SLEEP_MODE	0x12


#define WT6670_MASK_ERR_CODE				0x00FF
#define WT6670_SHIFT_ERR_CODE			0
#define WT6670_VAL_ERR_CODE_NOERR		0x00
#define WT6670_VAL_ERR_CODE_ERR_OCCUR	0x03
#define WT6670_VAL_ERR_CODE_DET_SUCCESS	0x04

#define WT6670_REG_FIRMWARE_VERSION		0xB2
#define WT6670_MASK_FIRMWARE_VERSION		0xFF00
#define WT6670_SHIFT_FIRMWARE_VERSION	8

//---------------------------------------------------------------------

#define WT6670_INFO(fmt,arg...)           pr_info("<<WT6670-INF>>[%s:%d] "fmt, __func__, __LINE__, ##arg)
#define WT6670_ERROR(fmt,arg...)          pr_info("<<WT6670-ERR>>[%s:%d] "fmt, __func__, __LINE__, ##arg)
#define WT6670_DEBUG(fmt,arg...)          do{\
                                         	if(WT6670_DEBUG_ON)\
                                         	pr_info("<<WT6670-DBG>>[%s:%d]"fmt"\n",__func__, __LINE__, ##arg);\
                                       		} while(0)

#define WT6670_DEBUG_FUNC()               do{\
                                         	if(WT6670_DEBUG_FUNC_ON)\
                                         	pr_info("<<WT6670-FUNC>> Func:%s@Line:%d\n",__func__,__LINE__);\
                                       	} while(0)
//---------------------------------------------------------------------
static const struct i2c_device_id wt6670_id[] = {
	{I2C_DRIVER_NAME, 0},
	{}
};

static struct of_device_id wt6670_match_table[] = {
	{.compatible = "wt-wt6670",},
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

struct wt6670_board_data {
	struct pinctrl *pinctrl_mode;
	struct pinctrl_state *i2c_mode;
	struct pinctrl_state *gpio_mode;
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
	int scl_gpio;
	int sda_gpio;
};

struct wt6670_data {
	struct i2c_client *client;
	struct wt6670_board_data bdata;
	struct delayed_work int_work;
	struct delayed_work det_work;
	struct delayed_work upgrade_work;
	struct workqueue_struct *wt6670_int_queue;
	struct workqueue_struct *wt6670_det_queue;
	struct class *i2c_class;
	struct mutex i2c_mutex;
	struct notifier_block nb;
	const struct firmware *fw_entry;
	struct power_supply *psy;
	struct notifier_block otg_nb;
	struct tcpc_device *otg_tcpc_dev;
	unsigned int chg_type;
	unsigned int irq;
	unsigned int recheck_flag;
	unsigned short FirmwareVer;
	char image_name[64];
	bool appear;
	bool suspend;
	bool power_stat;
	bool alive_flag;
};

static struct wt6670_data *gWt6670 = NULL;

static int wt6670_enable_detect(unsigned char enable);
static int wt6670_get_firmware_version(unsigned short *fw_ver);

void __attribute__ ((weak)) Charger_Detect_Init(void)
{
}

void __attribute__ ((weak)) Charger_Detect_Release(void)
{
}

int __attribute__ ((weak))  sqc_get_otg_online(void)
{
	return 0;
}

int __attribute__ ((weak))  sqc_get_sdp_auth_status(void)
{
	return 0;
}

static int wt6670_set_prop_by_name(const char *name, enum power_supply_property psp, int data)
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
	snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "WT6670-0x00: ");

	for (i = 0; i < len; i++) {
		snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "0x%02X ", buffer[i]);

		if ((i != 0) && ((i + 1) % 8 == 0)) {
			snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "	");
		}

		if ((i != 0) && ((i + 1) % 16 == 0)) {
			pr_info("%s\n", buf);
			memset(buf, 0, sizeof(buf));
			snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "WT6670-0x%02X: ", (i + 1));
		}
	}

	pr_info("%s\n", buf);

}


static inline int wt6670_Bon(void *p)
{
	if (p == NULL)
		return 1;

	return 0;
}

static int wt6670_gpio_setup(int gpio, bool config, int dir, int state)
{
	int retval = 0;
	unsigned char buf[16];

	WT6670_DEBUG_FUNC();

	if (config) {
		snprintf(buf, sizeof(buf), "wt6670_gpio_%u\n", gpio);

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

	WT6670_DEBUG_FUNC();

	return retval;
}

static int wt6670_parse_pinctrl(struct i2c_client *client, struct wt6670_board_data *bdata)
{
	int ret = 0;

	pr_info("%s, get info from dts pinctrl\n", __func__);

	bdata->pinctrl_mode = devm_pinctrl_get(&client->dev);
	if (IS_ERR(bdata->pinctrl_mode)) {
		ret = PTR_ERR(bdata->pinctrl_mode);
		pr_err("%s can't find pctrl\n", __func__);
		bdata->pinctrl_mode = NULL;
		return ret;
	} else {
		pr_info("%s platform device is null, ret:%d \n", __func__, ret);
	}

	bdata->i2c_mode = pinctrl_lookup_state(bdata->pinctrl_mode, "wt6670_i2c_mode");
	if (IS_ERR(bdata->i2c_mode)) {
		ret = PTR_ERR(bdata->i2c_mode);
		pr_err("%s pinctrl i2c_mode get fail ret:%d \n", __func__, ret);
		bdata->i2c_mode = NULL;
		return ret;
	}

	bdata->gpio_mode = pinctrl_lookup_state(bdata->pinctrl_mode, "wt6670_gpio_mode");
	if (IS_ERR(bdata->gpio_mode)) {
		ret = PTR_ERR(bdata->gpio_mode);
		pr_err("%s pinctrl wt6670_gpio_mode get fail ret:%d \n", __func__, ret);
		bdata->gpio_mode = NULL;
		return ret;
	}

	return 0;
}

static int wt6670_select_gpio_pinctrl(struct wt6670_board_data *bdata, bool flag)
{
	int ret = 0;
	struct pinctrl_state *s = NULL;

	if (flag)
		s = bdata->gpio_mode;
	else
		s = bdata->i2c_mode;

	ret = pinctrl_select_state(bdata->pinctrl_mode, s);
	if (ret < 0) {
		pr_err("%s Failed to select default pinstate, ret:%d", __func__, ret);
	}

	pr_info("%s, pinctrl select %s success.\n", __func__, flag ? "GPIO MODE" : "I2C MODE");

	return 0;
}

static int wt6670_parse_dt(struct device *dev, struct wt6670_board_data *bdata)
{
	int retval;
	u32 value;
	const char *name;
	struct device_node *np = dev->of_node;

	WT6670_DEBUG_FUNC();

	if (of_find_property(np, "wt6670,irq-gpio", NULL)) {
		bdata->irq_gpio = of_get_named_gpio_flags(np,
				  "wt6670,irq-gpio", 0, NULL);
	} else
		bdata->irq_gpio = -1;

	WT6670_INFO("irq_gpio: %d\n", bdata->irq_gpio);

	retval = of_property_read_u32(np, "wt6670,irq-on-state",
				      &value);
	if (retval < 0)
		bdata->irq_on_state = 0;
	else
		bdata->irq_on_state = value;

	WT6670_INFO("irq_on_state: %d\n", bdata->irq_on_state);

	retval = of_property_read_u32(np, "wt6670,irq-flags", &value);
	if (retval < 0)
		return retval;
	else
		bdata->irq_flags = value;

	WT6670_INFO("irq_flags: %d\n", bdata->irq_flags);

	retval = of_property_read_string(np, "wt6670,pwr-reg-name", &name);
	if (retval == -EINVAL)
		bdata->pwr_reg_name = NULL;
	else if (retval < 0)
		return retval;
	else
		bdata->pwr_reg_name = name;

	WT6670_INFO("pwr_reg_name: %s\n", bdata->pwr_reg_name);

	retval = of_property_read_string(np, "wt6670,bus-reg-name", &name);
	if (retval == -EINVAL)
		bdata->bus_reg_name = NULL;
	else if (retval < 0)
		return retval;
	else
		bdata->bus_reg_name = name;

	WT6670_INFO("bus_reg_name: %s\n", bdata->bus_reg_name);

	if (of_find_property(np, "wt6670,power-gpio", NULL)) {
		bdata->power_gpio = of_get_named_gpio_flags(np,
				    "wt6670,power-gpio", 0, NULL);
		retval = of_property_read_u32(np, "wt6670,power-on-state",
					      &value);
		if (retval < 0)
			return retval;
		else
			bdata->power_on_state = value;
	} else {
		bdata->power_gpio = -1;
	}

	WT6670_INFO("power_gpio: %d, power_on_state %d\n", bdata->power_gpio, bdata->power_on_state);

	if (of_find_property(np, "wt6670,power-delay-ms", NULL)) {
		retval = of_property_read_u32(np, "wt6670,power-delay-ms",
					      &value);
		if (retval < 0)
			return retval;
		else
			bdata->power_delay_ms = value;
	} else {
		bdata->power_delay_ms = 0;
	}

	WT6670_INFO("power_delay_ms: %d\n", bdata->power_delay_ms);

	if (of_find_property(np, "wt6670,reset-gpio", NULL)) {
		bdata->reset_gpio = of_get_named_gpio_flags(np,
				    "wt6670,reset-gpio", 0, NULL);
		retval = of_property_read_u32(np, "wt6670,reset-on-state",
					      &value);
		if (retval < 0)
			return retval;
		else
			bdata->reset_on_state = value;
		retval = of_property_read_u32(np, "wt6670,reset-active-ms",
					      &value);
		if (retval < 0)
			return retval;
		else
			bdata->reset_active_ms = value;
	} else {
		bdata->reset_gpio = -1;
	}

	if (of_find_property(np, "wt6670,reset-delay-ms", NULL)) {
		retval = of_property_read_u32(np, "wt6670,reset-delay-ms",
					      &value);
		if (retval < 0)
			return retval;
		else
			bdata->reset_delay_ms = value;
	} else {
		bdata->reset_delay_ms = 0;
	}

	WT6670_INFO("reset_gpio: %d, reset_on_state: %d, reset_active_ms %d, reset_delay_ms %d\n",
		   bdata->reset_gpio, bdata->reset_on_state, bdata->reset_active_ms, bdata->reset_delay_ms);

	if (of_find_property(np, "wt6670,scl-gpio", NULL)) {
		bdata->scl_gpio = of_get_named_gpio_flags(np,
				    "wt6670,scl-gpio", 0, NULL);
	} else {
		bdata->scl_gpio = -1;
	}

	if (of_find_property(np, "wt6670,sda-gpio", NULL)) {
		bdata->sda_gpio = of_get_named_gpio_flags(np,
				    "wt6670,sda-gpio", 0, NULL);
	} else {
		bdata->sda_gpio = -1;
	}

	WT6670_INFO("scl_gpio: %d, sda_gpio: %d\n",
		   bdata->scl_gpio, bdata->sda_gpio);

	WT6670_DEBUG_FUNC();

	return 0;
}

static int wt6670_hw_reset(void)
{
	const struct wt6670_board_data *bdata = &gWt6670->bdata;

	if (bdata->reset_gpio >= 0) {
		mdelay(bdata->reset_delay_ms);
		WT6670_INFO("reset on status");
		gpio_set_value(bdata->reset_gpio, bdata->reset_on_state);
		mdelay(bdata->reset_active_ms);
		WT6670_INFO("reset off status");
		gpio_set_value(bdata->reset_gpio, !bdata->reset_on_state);
		mdelay(bdata->reset_delay_ms);
		WT6670_INFO("reset exit status");

		wt6670_enable_detect(true);
		return 0;
	}

	return -1;
}

static void wt6670_i2c_comm_lock(void)
{
	WT6670_DEBUG_FUNC();

	if ((gWt6670 == NULL) || (gWt6670->appear == false))
		return;

	//WT6670_INFO("lock ++++++++++++");

	mutex_lock(&(gWt6670->i2c_mutex));

	WT6670_DEBUG_FUNC();

	return;
}

static void wt6670_i2c_comm_unlock(void)
{
	WT6670_DEBUG_FUNC();

	if ((gWt6670 == NULL) || (gWt6670->appear == false))
		return;

	mutex_unlock(&(gWt6670->i2c_mutex));

	//WT6670_INFO("lock ----------------");

	WT6670_DEBUG_FUNC();

	return;
}

static int wt6670_write_eeprom(struct i2c_client *client, unsigned char slv_addr,
					unsigned short reg, const unsigned char *datbuf, int ByteNo)
{
	unsigned char* buf;
	struct i2c_msg msg;
	int ret;
//	int count=0;

	//WT6670_INFO("i2c_client addr: 0x%02X",client->addr);

	if(!datbuf) {
		WT6670_ERROR("datbuf is null\n");
		return -EINVAL;
	}

	//reg = htons(reg);

//********************************************************
#ifdef WT6670_I2C_DATA_DEBUG
	buf = kmalloc(ByteNo + sizeof(slv_addr) + sizeof(reg),GFP_KERNEL);
	if(!buf) {
		WT6670_ERROR("kmalloc failed\n");
		return -EINVAL;
	}

	memcpy(buf, &slv_addr, sizeof(slv_addr));
	memcpy(buf + sizeof(slv_addr), &reg, sizeof(reg));
	memcpy(buf + sizeof(slv_addr) + sizeof(reg), datbuf, ByteNo);
	WT6670_INFO("=====write dump=====");
	print_mem(buf, ByteNo + sizeof(slv_addr) + sizeof(reg));
	kfree(buf);
	return 0;
#endif
//********************************************************

	buf = kmalloc(ByteNo + 2,GFP_KERNEL);
	if(!buf) {
		WT6670_ERROR("kmalloc failed\n");
		return -EINVAL;
	}

	memset(buf, 0, ByteNo + 2);
	buf[0] = (reg >> 8) & 0xff;
	buf[1] = reg & 0xff;
	memcpy(buf + 2, datbuf, ByteNo);

	//WT6670_INFO("reg addr: 0x%02x,0x%02X",buf[0],buf[1]);

	msg.addr = slv_addr;
	msg.flags = 0;
	msg.len = ByteNo + 2;
	msg.buf = buf;

	wt6670_i2c_comm_lock();

	ret = i2c_transfer(client->adapter, &msg, 1);

	wt6670_i2c_comm_unlock();

	if(ret < 0)
		WT6670_ERROR("i2c_master_send Error ! err_code:%d, reg: 0x%04X",ret, reg);
	//else
	//WT6670_INFO("i2c_master_send OK !");

	kfree(buf);

	return ret;
}

static int wt6670_read_eeprom(struct i2c_client *client, unsigned char slv_addr,
										unsigned short reg, unsigned char *datbuf, int ByteNo)
{
	struct i2c_msg msg[2];
	int ret = 0;
	unsigned char reg16[2];
#ifdef WT6670_I2C_DATA_DEBUG
	char *buf = NULL;
#endif

	//WT6670_INFO("i2c_client addr: 0x%02X",client->addr);

	if(!datbuf) {
		WT6670_ERROR("datbuf is null\n");
		return -EINVAL;
	}

	//reg = htons(reg);
//********************************************************
#ifdef WT6670_I2C_DATA_DEBUG
	buf = kmalloc(ByteNo + sizeof(slv_addr) + sizeof(reg),GFP_KERNEL);
	if(!buf) {
		WT6670_ERROR("kmalloc failed\n");
		return -EINVAL;
	}

	memcpy(buf, &slv_addr, sizeof(slv_addr));
	memcpy(buf + sizeof(slv_addr), &reg, sizeof(reg));
	memcpy(buf + sizeof(slv_addr) + sizeof(reg), datbuf, ByteNo);
	WT6670_INFO("=====read dump=====");
	print_mem(buf, ByteNo + sizeof(slv_addr) + sizeof(reg));
	kfree(buf);
	return 0;
#endif
//********************************************************
	reg16[0] = (reg >> 8) & 0xff;
	reg16[1] = reg & 0xff;

	//WT6670_INFO("reg addr: 0x%02X,0x%02X",reg16[0],reg16[1]);

	msg[0].addr = slv_addr;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = reg16;

	msg[1].addr = slv_addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = ByteNo;
	msg[1].buf = datbuf;

	wt6670_i2c_comm_lock();

	ret = i2c_transfer(client->adapter, msg, 2);

	wt6670_i2c_comm_unlock();

	if(ret<0)
		WT6670_ERROR("i2c_master_send Error ! err_code:%d, reg: 0x%04X",ret, reg);
	//else
	//WT6670_INFO("i2c_transfer OK !\n");

	return ret;
}

static int wt6670_i2c_write(struct i2c_client *client, u16 addr, int len, void *txbuf)
{
	int ret;

	WT6670_DEBUG_FUNC();

	if (txbuf == NULL) {
		WT6670_ERROR("txbuf is null write 0x%04X failed\n", addr);
		return 0;
	}

	wt6670_i2c_comm_lock();

	ret = i2c_smbus_write_i2c_block_data(client, addr, len, txbuf);

	if (ret < 0) {
		WT6670_ERROR("wt6670_i2c_write addr 0x%04X failed\n", addr);
		wt6670_i2c_comm_unlock();
		return ret;
	}

	wt6670_i2c_comm_unlock();

	WT6670_DEBUG_FUNC();

	return len;
}

static int wt6670_i2c_read(struct i2c_client *client, u16 addr, u16 len, void *rxbuf)
{
	int ret = -1;
	//int i;
	struct i2c_msg msg[2];
	char i2c_buffer[128] = {0};

	WT6670_DEBUG_FUNC();

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

	wt6670_i2c_comm_lock();

	ret = i2c_transfer(client->adapter, msg, 2);

	if (ret < 0) {
		WT6670_ERROR("wt6670_i2c_read addr 0x%04X failed\n", addr);
		wt6670_i2c_comm_unlock();
		return ret;
	}

	wt6670_i2c_comm_unlock();

	if ((rxbuf != NULL) && (len != 0)) {
		memcpy(rxbuf, i2c_buffer, len);
	}

	WT6670_DEBUG_FUNC();

	return len;
}

static int wt6670_power_switch(bool SwFlag)
{

	struct wt6670_board_data *bdata = NULL;

	if (gWt6670 == NULL)
		return -1;

	bdata = &gWt6670->bdata;

	if (gWt6670->power_stat == SwFlag) {
		WT6670_ERROR("power mode already in power %s\n", SwFlag ? "on" : "down");
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

		gWt6670->power_stat = true;

		WT6670_INFO("+++++++++power on\n");
	} else {
		if (bdata->reset_gpio >= 0) {
			gpio_set_value(bdata->reset_gpio, bdata->reset_on_state);
			mdelay(bdata->reset_active_ms);
		}

		if (bdata->power_gpio >= 0) {
			gpio_set_value(bdata->power_gpio, !bdata->power_on_state);
			msleep(bdata->power_delay_ms);
		}

		gWt6670->power_stat = false;

		WT6670_INFO("------------power down\n");
	}

	return 0;
}

static int wt6670_set_gpio(struct wt6670_data *wt6670_data)
{
	int retval;
	const struct wt6670_board_data *bdata = &wt6670_data->bdata;

	WT6670_DEBUG_FUNC();

	retval = wt6670_gpio_setup(
			 bdata->irq_gpio,
			 true, 0, 0);
	if (retval < 0) {
		WT6670_ERROR("Failed to configure irq GPIO\n");
		goto err_gpio_irq;
	}

	if (bdata->power_gpio >= 0) {
		retval = wt6670_gpio_setup(
				 bdata->power_gpio,
				 true, 1, !bdata->power_on_state);
		if (retval < 0) {
			WT6670_ERROR("Failed to configure power GPIO\n");
			goto err_gpio_power;
		}
	}

	if (bdata->reset_gpio >= 0) {
		retval = wt6670_gpio_setup(
				 bdata->reset_gpio,
				 true, 1, !bdata->reset_on_state);
		if (retval < 0) {
			WT6670_ERROR("Failed to configure reset GPIO\n");
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

	gWt6670->power_stat = true;

	WT6670_DEBUG_FUNC();

	return 0;

err_gpio_irq:
	if (bdata->irq_gpio >= 0)
		wt6670_gpio_setup(bdata->irq_gpio, false, 0, 0);
err_gpio_reset:
	if (bdata->power_gpio >= 0)
		wt6670_gpio_setup(bdata->power_gpio, false, 0, 0);

err_gpio_power:
	wt6670_gpio_setup(bdata->irq_gpio, false, 0, 0);

	return retval;
}

static int wt6670_start_adsp(void)
{
	char enable = 0;

	if (gWt6670->alive_flag) {
		WT6670_INFO("### Detect already is running =====\n");
		return 0;
	}

	WT6670_INFO("### Detect is starting +++++\n");

	wt6670_i2c_write(gWt6670->client, WT6670_REG_ADSP_STRAT, 0, &enable);

	gWt6670->alive_flag = true;

	return 0;
}

static int wt6670_select_chg_protocol(unsigned char chg_type)
{
	if (gWt6670->recheck_flag == 2) {
		WT6670_INFO("recheck status, return\n");
		return 0;
	}

	WT6670_INFO("select_chg %d\n", chg_type);

	wt6670_i2c_write(gWt6670->client, WT6670_REG_PROT_SEL,
			sizeof(chg_type), &chg_type);

	return 0;
}

static int wt6670_intb_gpio_enable(unsigned char enable)
{
	WT6670_INFO("enable irq mode %d\n", enable);

	wt6670_i2c_write(gWt6670->client, WT6670_REG_INTB_CTRL,
			sizeof(enable), &enable);

	return 0;
}

static int wt6670_soft_reset(unsigned char enable)
{
	WT6670_INFO("soft_reset !!!\n");

	gWt6670->chg_type = WT6670_VAL_CHG_TYPE_UNKNOWN;

	wt6670_i2c_write(gWt6670->client, WT6670_REG_SOFT_RESET,
			sizeof(enable), &enable);

	return 0;
}

static int wt6670_enter_sleep_mode(unsigned char enable)
{
	WT6670_INFO("enter sleep mode %d!!!\n", enable);

	wt6670_i2c_write(gWt6670->client, WT6670_REG_SLEEP_CTRL,
			sizeof(enable), &enable);

	return 0;
}

static int wt6670_enable_detect(unsigned char enable)
{
	WT6670_INFO("enter enable detect %d!!!\n", enable);

	wt6670_i2c_write(gWt6670->client, WT6670_REG_DETECT_CTRL,
			sizeof(enable), &enable);

	return 0;
}

static int wt6670_qc3d0_regulate(bool is_dp, unsigned int cnt)
{
	unsigned short reg_val = 0;

	WT6670_INFO("QC3.0 %s[%d]!!!\n", is_dp ? "DP" : "DM", cnt);

	reg_val = ((is_dp << WT6670_SHIFT_QC3D0_DP_DM) & WT6670_MASK_QC3D0_DP_DM)
		  | ((cnt << WT6670_SHIFT_QC3D0_CNT) & WT6670_MASK_QC3D0_CNT);

	reg_val = htons(reg_val);

	WT6670_INFO("reg val: 0x%04X\n", reg_val);

	wt6670_i2c_write(gWt6670->client, WT6670_REG_QC3D0_REGULATE_MODE,
			sizeof(reg_val), &reg_val);

	return 0;
}

static int wt6670_qc3dp_regulate(bool is_dp, unsigned int cnt)
{
	unsigned short reg_val = 0;

	WT6670_INFO("QC3.0+ %s[%d]!!!\n", is_dp ? "DP" : "DM", cnt);

	reg_val = ((is_dp << WT6670_SHIFT_QC3DP_DP_DM) & WT6670_MASK_QC3DP_DP_DM)
		  | ((cnt << WT6670_SHIFT_QC3DP_CNT) & WT6670_MASK_QC3DP_CNT);

	reg_val = htons(reg_val);

	WT6670_INFO("reg val: 0x%04X\n", reg_val);

	wt6670_i2c_write(gWt6670->client, WT6670_REG_QC3DP_REGULATE_MODE,
			sizeof(reg_val), &reg_val);

	return 0;
}

static int wt6670_get_chg_type(unsigned int *chg_type, int *err_code)
{
	unsigned short reg_val = 0;

	wt6670_i2c_read(gWt6670->client, WT6670_REG_CHG_TYPE_AND_ERR,
		       sizeof(reg_val), &reg_val);

	reg_val = ntohs(reg_val);

	WT6670_INFO("reg val: 0x%04X\n", reg_val);

	*chg_type = (reg_val & WT6670_MASK_CHG_TYPE) >> WT6670_SHIFT_CHG_TYPE;

	WT6670_INFO("chg_type: 0x%04X\n", *chg_type);

	*err_code = (reg_val & WT6670_MASK_ERR_CODE) >> WT6670_SHIFT_ERR_CODE;

	WT6670_INFO("err_code: 0x%04X\n", *err_code);

	wt6670_get_firmware_version(&gWt6670->FirmwareVer);

	return 0;
}

static int wt6670_get_firmware_version(unsigned short *fw_ver)
{
	unsigned short reg_val = 0;
	int ret = 0;

	ret = wt6670_i2c_read(gWt6670->client, WT6670_REG_FIRMWARE_VERSION,
			     sizeof(reg_val), &reg_val);
	if (ret < 0) {
		WT6670_ERROR("reg val: 0x%04X\n", reg_val);
		return ret;
	}

	reg_val = ntohs(reg_val);

	WT6670_INFO("reg val: 0x%04X\n", reg_val);

	reg_val = (reg_val & WT6670_MASK_FIRMWARE_VERSION) >> WT6670_SHIFT_FIRMWARE_VERSION;

	WT6670_INFO("fw_ver: 0x%04X\n", reg_val);

	if (fw_ver)
		*fw_ver = reg_val;

	return 0;
}

#ifdef CONFIG_WT6670_USE_TEST_I2C
static int wt6670_i2c_test(void)
{
	int ret = 0;

	WT6670_DEBUG_FUNC();

	ret = wt6670_get_firmware_version(&gWt6670->FirmwareVer);
	if (ret < 0) {
		WT6670_ERROR("WT6670 device test failed!!!!\n");
		return ret;
	}

	gWt6670->appear = true;

	WT6670_DEBUG_FUNC();

	return 0;
}
#endif

static int wt6670_inform_psy_changed(struct wt6670_data *chip)
{
	int ret = 0;
	union power_supply_propval propval;
	enum charger_type mtk_chg_type = 0;

	switch (chip->chg_type) {
	case WT6670_VAL_CHG_TYPE_FLOAT:
		mtk_chg_type = NONSTANDARD_CHARGER;
		break;
	case WT6670_VAL_CHG_TYPE_SDP:
		mtk_chg_type = STANDARD_HOST;
		break;
	case WT6670_VAL_CHG_TYPE_CDP:
		mtk_chg_type = CHARGING_HOST;
		break;
	case WT6670_VAL_CHG_TYPE_DCP:
	case WT6670_VAL_CHG_TYPE_QC2D0:
	case WT6670_VAL_CHG_TYPE_QC3D0:
	case WT6670_VAL_CHG_TYPE_QC3DP_18W:
	case WT6670_VAL_CHG_TYPE_QC3DP_27W:
		mtk_chg_type = STANDARD_CHARGER;
		break;
	default:
		mtk_chg_type = CHARGER_UNKNOWN;
		break;
	}

	/* Get chg type det power supply */
	chip->psy = power_supply_get_by_name("charger");
	if (!chip->psy) {
		WT6670_ERROR("get charger power supply failed\n", __func__);
		return -EINVAL;
	}

	/* Inform chg det power supply */
	if (mtk_chg_type == 0)
		propval.intval = false;
	else
		propval.intval = true;
	ret = power_supply_set_property(chip->psy, POWER_SUPPLY_PROP_ONLINE, &propval);
	if (ret < 0)
		WT6670_ERROR("psy online failed, ret = %d\n", ret);

	propval.intval = mtk_chg_type;
	ret = power_supply_set_property(chip->psy, POWER_SUPPLY_PROP_CHARGE_TYPE, &propval);
	if (ret < 0)
		WT6670_ERROR("psy type failed, ret = %d\n", ret);

	power_supply_put(chip->psy);

	WT6670_INFO("Setting charger psy online & charger type successed %d\n", propval.intval);

	return ret;
}

static void wt6670_int_handle_work(struct work_struct *work)
{
	int chg_type = 0, err_code = 0, delay_ms = 0;

	gWt6670->alive_flag = false;
	WT6670_INFO("### Detect is finished -----\n");

	/*exit sleep mode*/
	if (gWt6670->chg_type == WT6670_VAL_CHG_TYPE_SLEEP_MODE) {
		wt6670_set_prop_by_name("battery", POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, 1);
	}

	wt6670_get_chg_type(&chg_type, &err_code);

	if (sqc_get_otg_online() != 0) {
		WT6670_INFO("### otg is online\n");
		gWt6670->chg_type = WT6670_VAL_CHG_TYPE_UNKNOWN;
		return;
	}

	if (chg_type == WT6670_VAL_CHG_TYPE_UNKNOWN) {
		flush_delayed_work(&gWt6670->det_work);
		cancel_delayed_work_sync(&gWt6670->det_work);
		gWt6670->recheck_flag = 0;
		WT6670_INFO("### recheck is canceled, plugout, recheck_flag: 0\n");
	} else if (chg_type == WT6670_VAL_CHG_TYPE_SDP) {
		WT6670_INFO("SDP, Charger Detect Release\n");
		Charger_Detect_Release();
		if (gWt6670->recheck_flag == 0) {
			gWt6670->recheck_flag = 1;
			WT6670_INFO("### recheck chg type delay 2500ms, recheck_flag: 1\n");
			queue_delayed_work(gWt6670->wt6670_det_queue,
											&gWt6670->det_work,
											msecs_to_jiffies(2500));
		} else {
			gWt6670->recheck_flag = 3;
			WT6670_INFO("Charger Detect Release, recheck_flag: 3\n");
		}
	} else if ((chg_type == WT6670_VAL_CHG_TYPE_FLOAT)
				|| (chg_type == WT6670_VAL_CHG_TYPE_CDP)
				|| (chg_type == WT6670_VAL_CHG_TYPE_OCP)
				|| (chg_type == WT6670_VAL_CHG_TYPE_QC2D0)
				|| (chg_type == WT6670_VAL_CHG_TYPE_QC3D0)) {
		if (gWt6670->recheck_flag == 0) {
			gWt6670->recheck_flag = 1;
			delay_ms = (chg_type == WT6670_VAL_CHG_TYPE_FLOAT) ? 5000 : 200;
			WT6670_INFO("### recheck chg type delay %d ms\n", delay_ms);
			queue_delayed_work(gWt6670->wt6670_det_queue,
										&gWt6670->det_work,
										msecs_to_jiffies(delay_ms));
		} else {
			gWt6670->recheck_flag = 3;
			WT6670_INFO("Charger Detect Release, recheck_flag: 3\n");
		}
	} else if (chg_type == WT6670_VAL_CHG_TYPE_DCP) {
		WT6670_INFO("DCP, err_code: %d\n", err_code);
		if (err_code == 0x00) {
			if (gWt6670->recheck_flag == 0) {
				gWt6670->recheck_flag = 1;
				WT6670_INFO("### recheck chg type delay 200ms, recheck_flag: 1\n");
				queue_delayed_work(gWt6670->wt6670_det_queue,
											&gWt6670->det_work,
											msecs_to_jiffies(200));
			} else {
				gWt6670->recheck_flag = 3;
				WT6670_INFO("Charger Detect Release, recheck_flag: 3\n");
				Charger_Detect_Release();
			}
		} else if (err_code == 0x01) {
			WT6670_INFO("### hvdcp is checking..\n");
		} else {
			WT6670_INFO("### hvdcp is abnormal\n");
		}
	} else if ((chg_type == WT6670_VAL_CHG_TYPE_QC3DP_18W)
		|| (chg_type == WT6670_VAL_CHG_TYPE_QC3DP_27W)) {
		WT6670_INFO("QC3+, err_code: %d, recheck_flag: 3\n", err_code);
		gWt6670->recheck_flag = 3;
	}

	if (get_boot_mode() == RECOVERY_BOOT) {
		wt6670_inform_psy_changed(gWt6670);
	}

	WT6670_INFO("notify status changed %d\n", (chg_type != WT6670_VAL_CHG_TYPE_UNKNOWN));

	if (gWt6670->chg_type != chg_type) {
		sqc_notify_daemon_changed(SQC_NOTIFY_USB,
					SQC_NOTIFY_USB_STATUS_CHANGED,
					(gWt6670->chg_type != WT6670_VAL_CHG_TYPE_UNKNOWN));
		gWt6670->chg_type = chg_type;
	}

	if ((chg_type == WT6670_VAL_CHG_TYPE_QC2D0)
			|| (chg_type == WT6670_VAL_CHG_TYPE_QC3D0)
			|| (chg_type == WT6670_VAL_CHG_TYPE_QC3D0)
			|| (chg_type == WT6670_VAL_CHG_TYPE_QC3DP_18W)
			|| (chg_type == WT6670_VAL_CHG_TYPE_QC3DP_27W)) {
		sqc_notify_daemon_changed(SQC_NOTIFY_USB,
					SQC_NOTIFY_FAST_CHG_STATUS, 1);
	} else {
		sqc_notify_daemon_changed(SQC_NOTIFY_USB,
					SQC_NOTIFY_FAST_CHG_STATUS, 0);
	}
}

static irqreturn_t wt6670_irq_handler(int irq, void *data)
{
	struct wt6670_data *chip = data;

	WT6670_INFO("\n");

	queue_delayed_work(chip->wt6670_int_queue, &chip->int_work, msecs_to_jiffies(100));

	return IRQ_HANDLED;
}

static int wt6670_request_irq(struct wt6670_data *chip)
{
	int ret = 0;

	WT6670_INFO("\n");

	chip->irq = gpio_to_irq(chip->bdata.irq_gpio);
	if (chip->irq < 0) {
		WT6670_ERROR("irq mapping fail(%d)\n", chip->irq);
		return ret;
	}

	/* Request threaded IRQ */
	ret = request_threaded_irq(chip->irq, NULL,
				   wt6670_irq_handler, IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "wt6670-irq",
				   chip);
	if (ret < 0) {
		WT6670_ERROR("request thread irq fail(%d)\n", ret);
		return ret;
	}

	enable_irq_wake(chip->irq);

	WT6670_INFO("irq = %d init finish\n", chip->irq);

	return 0;
}

static int wt6670_status_init(void)
{
	WT6670_INFO("reinit ic\n");

	gWt6670->chg_type = WT6670_VAL_CHG_TYPE_UNKNOWN;
	flush_delayed_work(&gWt6670->det_work);
	cancel_delayed_work_sync(&gWt6670->det_work);
	gWt6670->recheck_flag = 0;
	WT6670_INFO("### hw_reset, recheck_flag: 0\n");

	wt6670_hw_reset();

	return 0;
}

static int wt6670_status_remove(void)
{
	WT6670_INFO("sleep on status\n");

	gWt6670->chg_type = WT6670_VAL_CHG_TYPE_UNKNOWN;

	wt6670_enter_sleep_mode(true);

	return 0;
}

static int wt6670_get_charger_type(int *chg_type)
{
	switch (gWt6670->chg_type) {
	case WT6670_VAL_CHG_TYPE_FLOAT:
		*chg_type = SQC_FLOAT_TYPE;
		break;
	case WT6670_VAL_CHG_TYPE_SDP:
		*chg_type = SQC_SDP_TYPE;
		break;
	case WT6670_VAL_CHG_TYPE_CDP:
		*chg_type = SQC_CDP_TYPE;
		break;
	case WT6670_VAL_CHG_TYPE_DCP:
		*chg_type = SQC_DCP_TYPE;
		break;
	case WT6670_VAL_CHG_TYPE_QC2D0:
		*chg_type = SQC_QC2D0_5V_TYPE;
		break;
	case WT6670_VAL_CHG_TYPE_QC3D0:
		*chg_type = SQC_QC3D0_TYPE;
		break;
	case WT6670_VAL_CHG_TYPE_OCP:
		*chg_type = SQC_DCP_TYPE;
		break;
	case WT6670_VAL_CHG_TYPE_QC3DP_18W:
		*chg_type = SQC_QC3D0_PLUS_18W_TYPE;
		break;
	case WT6670_VAL_CHG_TYPE_QC3DP_27W:
		*chg_type = SQC_QC3D0_PLUS_27W_TYPE;
		break;
	case WT6670_VAL_CHG_TYPE_UNKNOWN:
		*chg_type = SQC_NONE_TYPE;
		break;
	case WT6670_VAL_CHG_TYPE_SLEEP_MODE:
		*chg_type = SQC_SLEEP_MODE_TYPE;
		break;
	default:
		*chg_type = SQC_NONE_TYPE;
		break;
	}

	return 0;
}

static int wt6670_set_charger_type(int chg_type)
{
	unsigned char wt6670_type = 0;

	if (gWt6670->recheck_flag == 2) {
		WT6670_INFO("recheck status, return\n");
		return 0;
	}

	switch (chg_type) {
	case SQC_QC2D0_5V_TYPE:
		wt6670_type = WT6670_VAL_PROT_QC2D0_5V;
		break;
	case SQC_QC2D0_9V_TYPE:
		wt6670_type = WT6670_VAL_PROT_QC2D0_9V;
		break;
	case SQC_QC2D0_12V_TYPE:
		wt6670_type = WT6670_VAL_PROT_QC2D0_12V;
		break;
	case SQC_QC3D0_TYPE:
		wt6670_type = WT6670_VAL_PROT_QC3D0_5V;
		break;
	case SQC_QC3D0_PLUS_18W_TYPE:
	case SQC_QC3D0_PLUS_27W_TYPE:
	case SQC_QC3D0_PLUS_45W_TYPE:
		wt6670_type = WT6670_VAL_PROT_QC3DP_5V;
		break;
	case SQC_PD3D0_APDO_TYPE:
	case SQC_PD3D0_BASE_TYPE:
	case SQC_SDP_TYPE:
	case SQC_CDP_TYPE:
	case SQC_DCP_TYPE:
	case SQC_FLOAT_TYPE:
		WT6670_ERROR("charing type: not support\n");
		return 0;
	default:
		WT6670_ERROR("charing type: UNKNOWN\n");
		return 0;
	}

	wt6670_select_chg_protocol(wt6670_type);

	return 0;
}

static int wt6670_get_protocol_status(unsigned int *status)
{
	return 0;
}

static int wt6670_get_chip_vendor_id(unsigned int *vendor_id)
{
	return 0;
}

static int wt6670_set_qc3d0_dp(unsigned int dp_cnt)
{
	if (gWt6670->recheck_flag == 2) {
		WT6670_INFO("recheck status, return\n");
		return 0;
	}

	return wt6670_qc3d0_regulate(true, dp_cnt);
}

static int wt6670_set_qc3d0_dm(unsigned int dm_cnt)
{
	if (gWt6670->recheck_flag == 2) {
		WT6670_INFO("recheck status, return\n");
		return 0;
	}

	return wt6670_qc3d0_regulate(false, dm_cnt);
}

static int wt6670_set_qc3d0_plus_dp(unsigned int dp_cnt)
{
	if (gWt6670->recheck_flag == 2) {
		WT6670_INFO("recheck status, return\n");
		return 0;
	}

	return wt6670_qc3dp_regulate(true, dp_cnt);
}

static int wt6670_set_qc3d0_plus_dm(unsigned int dm_cnt)
{
	if (gWt6670->recheck_flag == 2) {
		WT6670_INFO("recheck status, return\n");
		return 0;
	}

	return wt6670_qc3dp_regulate(false, dm_cnt);
}


struct sqc_bc1d2_proto_ops wt6670_proto_node = {
	.status_init = wt6670_status_init,
	.status_remove = wt6670_status_remove,
	.get_charger_type = wt6670_get_charger_type,
	.set_charger_type = wt6670_set_charger_type,
	.get_protocol_status = wt6670_get_protocol_status,
	.get_chip_vendor_id = wt6670_get_chip_vendor_id,
	.set_qc3d0_dp = wt6670_set_qc3d0_dp,
	.set_qc3d0_dm = wt6670_set_qc3d0_dm,
	.set_qc3d0_plus_dp = wt6670_set_qc3d0_plus_dp,
	.set_qc3d0_plus_dm = wt6670_set_qc3d0_plus_dm,
};

static void wt6670_det_handle_work(struct work_struct *work)
{
	if (gWt6670->recheck_flag != 1) {
		WT6670_INFO("check recheck_flag failed, %d\n", gWt6670->recheck_flag);
		return;
	}

#ifdef ZTE_FEATURE_PV_AR
	WT6670_INFO("PV version, not recheck\n");
	gWt6670->recheck_flag = 3;
	return;
#endif

	if (gWt6670->chg_type == WT6670_VAL_CHG_TYPE_SDP) {
		if (sqc_get_sdp_auth_status()) {
			gWt6670->recheck_flag = 3;
			WT6670_INFO("### recheck sdp is finished, recheck_flag: 3\n");
			return;
		} else {
			Charger_Detect_Init();
			WT6670_INFO("### sdp auth failed, rechecking\n");
		}
	}

	if ((gWt6670->chg_type != WT6670_VAL_CHG_TYPE_UNKNOWN)
			&& (gWt6670->chg_type != WT6670_VAL_CHG_TYPE_QC3DP_18W)
			&& (gWt6670->chg_type != WT6670_VAL_CHG_TYPE_QC3DP_27W)) {
		WT6670_INFO("### recheck is starting, recheck_flag: 2\n");
		gWt6670->recheck_flag = 2;
		wt6670_start_adsp();
	}
}

static int wt6670_upgrade_request_image(void)
{
	int retval = 0;

	WT6670_INFO("into");

#ifdef WT6670_I2C_DATA_DEBUG
	int i = 0;
	gWt6670->fw_entry->data = kmalloc(4 * 1024,GFP_KERNEL);
	gWt6670->fw_entry->size = 4 * 1024;

	for (i = 0; i < gWt6670->fw_entry->size; i++) {
		gWt6670->fw_entry->data[i] = i;
	}

	return 0;
#endif

	if (strlen(gWt6670->image_name) == 0) {
		snprintf(gWt6670->image_name, sizeof(gWt6670->image_name), "%s", WT6670_IMAGE_NAME);
	}

	retval = request_firmware(&gWt6670->fw_entry, gWt6670->image_name,
			&gWt6670->client->dev);
	if (retval != 0) {
		WT6670_ERROR("Firmware image %s not available\n", gWt6670->image_name);
		return -EINVAL;
	}

	WT6670_INFO("exit");

	return 0;
}

static int wt6670_upgrade_cmp_version(void)
{
	unsigned char lbit = 0, hbit = 0;
	unsigned short image_version = 0;

	WT6670_INFO("into");

	lbit = gWt6670->fw_entry->data[0x0FFC];
	hbit = gWt6670->fw_entry->data[0x0FFD];

	image_version = (hbit << 8) | lbit;

	WT6670_INFO("image_version: 0x%04X, ic_version: 0x%04X\n",
		image_version, gWt6670->FirmwareVer);

	if (image_version != gWt6670->FirmwareVer) {
		WT6670_INFO("firmware need upgrade");
		return false;
	}

	return true;
}

static int wt6670_upgrade_enter_isp_mode(void)
{
	struct wt6670_board_data *bdata = &gWt6670->bdata;
	int scl_val = 0, i = 0;
	char sda_val[] = {0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0};
	int ret = 0;

	WT6670_INFO("into");

	if (bdata->reset_gpio <= 0) {
		WT6670_ERROR("reset chip failed!!!\n");
		return -EINVAL;
	}

	wt6670_select_gpio_pinctrl(bdata, true);

	msleep(10);

	ret = wt6670_gpio_setup(bdata->scl_gpio, true, 1, 0);
	if (ret < 0) {
		WT6670_ERROR("Failed to configure reset GPIO\n");
	}

	ret = wt6670_gpio_setup(bdata->sda_gpio, true, 1, 0);
	if (ret < 0) {
		WT6670_ERROR("Failed to configure reset GPIO\n");
	}

	gpio_set_value(bdata->reset_gpio, bdata->reset_on_state);
	msleep(20);
	gpio_set_value(bdata->reset_gpio, !bdata->reset_on_state);
	usleep_range(2500, 3000);

	gpio_set_value(bdata->scl_gpio, 0);
	gpio_set_value(bdata->sda_gpio, 0);

	do {
		gpio_set_value(bdata->scl_gpio, scl_val);

		if (!scl_val)
			gpio_set_value(bdata->sda_gpio, sda_val[i/2]);

		scl_val = !scl_val;
		udelay(4);
	} while(++i < sizeof(sda_val) * 2);

	gpio_set_value(bdata->scl_gpio, 0);
	gpio_set_value(bdata->sda_gpio, 0);

	usleep_range(10000, 12000);

	wt6670_gpio_setup(bdata->scl_gpio, false, 0, 0);

	wt6670_gpio_setup(bdata->sda_gpio, false, 0, 0);

	wt6670_select_gpio_pinctrl(bdata, false);

	return 0;
}

static int wt6670_upgrade_enable_isp_mode(void)
{
	unsigned short reg_addr = 0x5754;
	unsigned char data[] = {0x36, 0x36, 0x37, 0x30, 0x46};
	int ret = 0;

	WT6670_INFO("into");

	ret = wt6670_write_eeprom(gWt6670->client, 0x34, reg_addr, data, sizeof(data));
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static int wt6670_upgrade_check_chip_id(void)
{
	unsigned short reg_addr = 0x8000;
	unsigned char data = 0;

	WT6670_INFO("into");

	wt6670_read_eeprom(gWt6670->client, 0x34, reg_addr, &data, sizeof(data));

#ifdef WT6670_I2C_DATA_DEBUG
	return 0;
#endif

	if (data != 0x70) {
		WT6670_ERROR("check chipid 0x70 failed, 0x%02X\n", data);
		return -EINVAL;
	}

	return 0;
}

static int wt6670_upgrade_enable_isp_flash_mode(void)
{
	unsigned short reg_addr = 0x1002;
	unsigned char data[] = {0x08};
	int ret = 0;

	WT6670_INFO("into");

	ret = wt6670_write_eeprom(gWt6670->client, 0x34, reg_addr, data, sizeof(data));
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static int wt6670_upgrade_erase_flash(void)
{
	unsigned short reg_addr = 0x2000;
	unsigned char data[] = {0x00};
	int ret = 0;

	WT6670_INFO("into");

	ret = wt6670_write_eeprom(gWt6670->client, 0x34, reg_addr, data, sizeof(data));
	if (ret < 0) {
		return ret;
	}

	msleep(25);

	return 0;
}

static int wt6670_upgrade_burn_flash(unsigned short addr, const unsigned char *data, unsigned int len)
{
	unsigned short hi_reg_addr = 0x1001;
	unsigned char hi_burn_addr = addr >> 8;
	unsigned short lo_burn_addr = (0x41 << 8) | (addr & 0xFF);
	int ret = 0;

	/*WT6670_INFO("burn set hi addr");*/

	/*set hi addr*/
	ret = wt6670_write_eeprom(gWt6670->client, 0x34, hi_reg_addr, &hi_burn_addr, sizeof(hi_burn_addr));
	if (ret < 0) {
		return ret;
	}

	/*WT6670_INFO("burn start");*/
	/*burn start*/
	ret = wt6670_write_eeprom(gWt6670->client, 0x34, lo_burn_addr, data, len);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static int wt6670_upgrade_burn_flash_loop(const unsigned char *fw_data, unsigned int len)
{
	unsigned int pos_head = 0, burn_len = 0;
	int ret = 0;

	WT6670_INFO("into");

	if (len != (4 * 1024)) {
		WT6670_ERROR("check fw_data len failed, 0x%04X\n", len);
		return -EINVAL;
	}

	while(1) {
		if ((pos_head + 64) < len)
			burn_len = 64;
		else
			burn_len = len - pos_head;

		/*WT6670_INFO("LOOP[W]: pos_head 0x%04X/0x%04X, burn_len: 0x%04X\n", pos_head, len, burn_len);*/

		ret = wt6670_upgrade_burn_flash(pos_head, fw_data + pos_head, burn_len);
		if (ret < 0) {
			return ret;
		}

		pos_head += 64;

		if (pos_head >= len) {
			WT6670_INFO("BREAK[W]: pos_head 0x%04X/0x%04X, burn_len: 0x%04X\n", pos_head, len, burn_len);
			break;
		}
	}

	return 0;
}

static int wt6670_upgrade_burn_finish(void)
{
	unsigned short reg_addr = 0x0000;
	unsigned char data = 0x00;
	int ret = 0;

	WT6670_INFO("into");

	ret = wt6670_write_eeprom(gWt6670->client, 0x34, reg_addr, &data, sizeof(data));
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static int wt6670_upgrade_read_flash(unsigned short addr, char *data, unsigned int len)
{
	unsigned short hi_reg_addr = 0x1001;
	unsigned char hi_burn_addr = addr >> 8;
	unsigned short lo_burn_addr = (0x61 << 8) | (addr & 0xFF);
	int ret = 0;

	/*WT6670_INFO("check set hi addr");*/

	/*set hi addr*/
	ret = wt6670_write_eeprom(gWt6670->client, 0x34, hi_reg_addr, &hi_burn_addr, sizeof(hi_burn_addr));
	if (ret < 0) {
		return ret;
	}

	/*WT6670_INFO("check start");*/

#ifdef WT6670_I2C_DATA_DEBUG
	memcpy(data, gWt6670->fw_entry->data + addr, len);
#endif

	/*burn start*/
	ret = wt6670_read_eeprom(gWt6670->client, 0x34, lo_burn_addr, data, len);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static int wt6670_upgrade_check_flash_loop(const unsigned char *fw_data, unsigned int len)
{
	unsigned int pos_head = 0, burn_len = 0;
	char cmp_buf[64] = {0,};
	int ret = 0;

	WT6670_INFO("into");

	if (len != (4 * 1024)) {
		WT6670_ERROR("check fw_data len failed, 0x%04X\n", len);
		return -EINVAL;
	}

	while(1) {
		if ((pos_head + 64) < len)
			burn_len = 64;
		else
			burn_len = len - pos_head;

		/*WT6670_INFO("LOOP[R]: pos_head 0x%04X/0x%04X, burn_len: 0x%04X\n", pos_head, len, burn_len);*/

		memset(cmp_buf, 0, sizeof(cmp_buf));
		ret = wt6670_upgrade_read_flash(pos_head, cmp_buf, burn_len);
		if (ret < 0) {
			return ret;
		}

		if(memcmp(cmp_buf, fw_data + pos_head, burn_len)) {
			WT6670_ERROR("========\n", burn_len);
			print_mem(cmp_buf, burn_len);
			WT6670_ERROR("========\n", burn_len);
			print_mem(fw_data + pos_head, burn_len);
			WT6670_ERROR("========\n", burn_len);
			WT6670_ERROR("check fw_data failed, 0x%04X\n", burn_len);
			return -EINVAL;
		}

		pos_head += 64;

		if (pos_head >= len) {
			WT6670_INFO("BREAK[R]: pos_head 0x%04X/0x%04X, burn_len: 0x%04X\n", pos_head, len, burn_len);
			break;
		}
	}

	return 0;
}

static int wt6670_upgrade_end_cmd(void)
{
	unsigned short reg_addr = 0x1000;
	unsigned char data = 0x02;

	WT6670_INFO("into");

	wt6670_write_eeprom(gWt6670->client, 0x34, reg_addr, &data, sizeof(data));

	return 0;
}

static int wt6670_upgrade_clean_status(void)
{
	WT6670_INFO("into");

#ifdef WT6670_I2C_DATA_DEBUG
	kfree(gWt6670->fw_entry->data);
	return 0;
#endif

	if (gWt6670->fw_entry)
		release_firmware(gWt6670->fw_entry);

	return 0;
}

static void wt6670_upgrade_handle_work(struct work_struct *work)
{
	WT6670_INFO("upgrade into\n");

	if (wt6670_upgrade_request_image() != 0) {
		WT6670_ERROR("request image failed\n");
		goto failed_loop;
	}

	if (wt6670_upgrade_cmp_version() != 0) {
		WT6670_ERROR("enter cmp version failed\n");
		goto failed_loop;
	}

	if (wt6670_upgrade_enter_isp_mode() != 0) {
		WT6670_ERROR("enter isp mode failed\n");
		goto failed_loop;
	}

	if (wt6670_upgrade_enable_isp_mode() != 0) {
		WT6670_ERROR("enable isp mode failed\n");
		goto failed_loop;
	}

	if (wt6670_upgrade_check_chip_id() != 0) {
		WT6670_ERROR("check chip id failed\n");
		goto failed_loop;
	}

	if (wt6670_upgrade_enable_isp_flash_mode() != 0) {
		WT6670_ERROR("enable isp flash mode failed\n");
		goto failed_loop;
	}

	if (wt6670_upgrade_erase_flash() != 0) {
		WT6670_ERROR("erase flash failed\n");
		goto failed_loop;
	}

	if (wt6670_upgrade_burn_finish() != 0) {
		WT6670_ERROR("burn finish failed\n");
		goto failed_loop;
	}

	if (wt6670_upgrade_burn_flash_loop(gWt6670->fw_entry->data,
							gWt6670->fw_entry->size) != 0) {
		WT6670_ERROR("burn flash loop failed\n");
		goto failed_loop;
	}

	if (wt6670_upgrade_burn_finish() != 0) {
		WT6670_ERROR("burn finish failed\n");
		goto failed_loop;
	}

	if (wt6670_upgrade_check_flash_loop(gWt6670->fw_entry->data,
							gWt6670->fw_entry->size) != 0) {
		WT6670_ERROR("check flash loop failed\n");
		goto failed_loop;
	}

	if (wt6670_upgrade_end_cmd() != 0) {
		WT6670_ERROR("end cmd failed\n");
	}

	wt6670_hw_reset();

failed_loop:
	if (wt6670_upgrade_clean_status() != 0) {
		WT6670_ERROR("clean status failed\n");
	}

	WT6670_INFO("upgrade exit\n");
}

static int wt6670_otg_tcp_notifier_call(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct tcp_notify *noti = data;

	switch (event) {
	case TCP_NOTIFY_TYPEC_STATE:
		WT6670_INFO("%s, TCP_NOTIFY_TYPEC_STATE, old_state=%d, new_state=%d\n",
				__func__, noti->typec_state.old_state, noti->typec_state.new_state);

		if (noti->typec_state.new_state == TYPEC_UNATTACHED) {
			WT6670_INFO("Charger Detect Release\n");
			Charger_Detect_Release();
		}

		if ((noti->typec_state.new_state == TYPEC_ATTACHED_SNK)
					|| (noti->typec_state.new_state == TYPEC_ATTACHED_DEBUG)
					|| (noti->typec_state.new_state == TYPEC_ATTACHED_NORP_SRC)
					|| (noti->typec_state.new_state == TYPEC_ATTACHED_CUSTOM_SRC)) {
			WT6670_INFO("Charger Detect Init\n");
			Charger_Detect_Init();
		}
		break;
	}

	return NOTIFY_OK;
}


static int wt6670_register_notifier(void)
{
	int ret = 0;

	gWt6670->otg_tcpc_dev = tcpc_dev_get_by_name("type_c_port0");
	if (!gWt6670->otg_tcpc_dev) {
		WT6670_INFO("%s get tcpc device type_c_port0 fail\n", __func__);
		return -ENODEV;
	}

	gWt6670->otg_nb.notifier_call = wt6670_otg_tcp_notifier_call;
	ret = register_tcp_dev_notifier(gWt6670->otg_tcpc_dev, &gWt6670->otg_nb,
		TCP_NOTIFY_TYPE_USB|TCP_NOTIFY_TYPE_VBUS|TCP_NOTIFY_TYPE_MISC);
	if (ret < 0) {
		WT6670_INFO("%s register tcpc notifer fail\n", __func__);
		return -EINVAL;
	}


	return 0;
}

int wt6670_sleep_node_set(const char *val, const void *arg)
{
	int sleep_mode_enable = 0;

	if (!gWt6670) {
		WT6670_ERROR("gWt6670 is null\n");
		return -EINVAL;
	}

	sscanf(val, "%d", &sleep_mode_enable);

	WT6670_INFO("sleep_mode_enable = %d\n", sleep_mode_enable);

	if (sleep_mode_enable) {
		if (gWt6670->chg_type != WT6670_VAL_CHG_TYPE_SLEEP_MODE) {
			WT6670_INFO("sleep on status");

			/*disabel sqc-daemon*/
			gWt6670->chg_type = WT6670_VAL_CHG_TYPE_SLEEP_MODE;
			sqc_notify_daemon_changed(SQC_NOTIFY_USB, SQC_NOTIFY_USB_STATUS_CHANGED, 1);

			/*ic enter sleep mode*/
			/*wt6670_enter_sleep_mode(true);*/

			/*mtk enter sleep mode*/
			wt6670_set_prop_by_name("battery", POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, 0);
		}
	} else {
		if (gWt6670->chg_type != WT6670_VAL_CHG_TYPE_SLEEP_MODE) {
			WT6670_INFO("sleep off status");
		}
	}

	return 0;
}

int wt6670_sleep_node_get(char *val, const void *arg)
{
	int sleep_mode = 0;

	if (!gWt6670) {
		WT6670_ERROR("gWt6670 is null\n");
		return snprintf(val, PAGE_SIZE, "0");
	}

	if (gWt6670->chg_type == WT6670_VAL_CHG_TYPE_SLEEP_MODE)
		sleep_mode = 1;

	return snprintf(val, PAGE_SIZE, "%u", sleep_mode);
}

static struct zte_misc_ops qc3dp_sleep_mode_node = {
	.node_name = "qc3dp_sleep_mode",
	.set = wt6670_sleep_node_set,
	.get = wt6670_sleep_node_get,
	.free = NULL,
	.arg = NULL,
};

static int wt6670_chg_type_show(struct seq_file *m, void *v)
{
	int chg_val = 0, chg_type = 0;

	wt6670_get_charger_type(&chg_type);

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

static int wt6670_chg_type_open(struct inode *inode, struct file *file)
{
 return single_open(file, wt6670_chg_type_show, NULL);
}

static const struct file_operations wt6670_chg_type_node = {
	.owner = THIS_MODULE,
	.open = wt6670_chg_type_open,
	.read = seq_read,
	.llseek	= seq_lseek,
};

static int wt6670_chg_id_show(struct seq_file *m, void *v)
{

	seq_printf(m, "%d", !!gWt6670->FirmwareVer);

	return 0;
}

static int wt6670_chg_id_open(struct inode *inode, struct file *file)
{
 return single_open(file, wt6670_chg_id_show, NULL);
}

static const struct file_operations wt6670_chg_id_node = {
	.owner = THIS_MODULE,
	.open = wt6670_chg_id_open,
	.read = seq_read,
	.llseek	= seq_lseek,
};

static int wt6670_probe_work(struct work_struct *work)
{
	if (wt6670_set_gpio(gWt6670)) {
		WT6670_ERROR("Failed to set up GPIO's\n");
		goto err_loop;
	}

	mutex_init(&(gWt6670->i2c_mutex));

	gWt6670->wt6670_int_queue = create_singlethread_workqueue("wt6670_int_queue");
	INIT_DELAYED_WORK(&gWt6670->int_work, wt6670_int_handle_work);

	if (wt6670_request_irq(gWt6670)) {
		WT6670_ERROR("Failed to request irq\n");
		goto err_loop;
	}

	gWt6670->wt6670_det_queue = create_singlethread_workqueue("wt6670_det_queue");
	INIT_DELAYED_WORK(&gWt6670->det_work, wt6670_det_handle_work);

	INIT_DELAYED_WORK(&gWt6670->upgrade_work, wt6670_upgrade_handle_work);

#ifdef CONFIG_WT6670_USE_TEST_I2C
	if (wt6670_i2c_test()) {

		wt6670_upgrade_handle_work(NULL);

		msleep(30);

		if (wt6670_i2c_test()) {
			WT6670_ERROR("force upgrade failed\n");
			wt6670_power_switch(false);
			goto err_loop;
		}
	} else {
		wt6670_upgrade_handle_work(NULL);
	}
#else
	gWt6670->appear = true;
#endif

	wt6670_intb_gpio_enable(true);

	sqc_hal_bc1d2_register(&wt6670_proto_node);

	wt6670_register_notifier();

	proc_create_data("driver/chg_type", 0664, NULL,
			&wt6670_chg_type_node, NULL);

	proc_create_data("driver/chg_id", 0664, NULL,
			&wt6670_chg_id_node, NULL);

	zte_misc_register_callback(&qc3dp_sleep_mode_node, gWt6670);

	/*check the usb is already online ?*/
	WT6670_INFO("### check usb is already online ?\n");
	wt6670_hw_reset();

	return 0;

err_loop:
	mutex_destroy(&(gWt6670->i2c_mutex));

	free_irq(gWt6670->irq, gWt6670);

	if (gWt6670->bdata.irq_gpio > 0)
		wt6670_gpio_setup(gWt6670->bdata.irq_gpio, false, 0, 0);

	if (gWt6670->bdata.reset_gpio > 0)
		wt6670_gpio_setup(gWt6670->bdata.reset_gpio, false, 0, 0);

	if (gWt6670->bdata.power_gpio > 0)
		wt6670_gpio_setup(gWt6670->bdata.power_gpio, false, 0, 0);

	kfree(gWt6670);
	gWt6670 = NULL;

	return -ENODEV;
}


static int wt6670_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	WT6670_INFO("WT6670 Driver Version: %s\n", WT6670_DRIVER_VERSION);
	WT6670_INFO("WT6670 I2C Address: 0x%02x\n", client->addr);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		WT6670_ERROR("I2C check functionality failed.\n");
		return -ENODEV;
	}

	gWt6670 = kzalloc(sizeof(struct wt6670_data), GFP_KERNEL);
	if (!gWt6670) {
		WT6670_ERROR("Failed to alloc mem for rmi4_data\n");
		return -ENOMEM;
	}

	gWt6670->client = client;

	if (wt6670_parse_dt(&client->dev, &gWt6670->bdata)) {
		WT6670_ERROR("Failed to parse dt\n");
		goto err_parse_dt;
	}

	if (wt6670_parse_pinctrl(client, &gWt6670->bdata)) {
		WT6670_ERROR("Failed to parse dt\n");
		goto err_parse_dt;
	}

	sqc_sequence_load_init(wt6670_probe_work, 1, 1);

	return 0;

err_parse_dt:
	kfree(gWt6670);
	gWt6670 = NULL;

	return 0;
}

static int wt6670_remove(struct i2c_client *client)
{
	WT6670_INFO();

	if (gWt6670 == NULL) {
		WT6670_INFO("wt6670 is not initialized!!!");
		return 0;
	}

	WT6670_INFO("### wt6670 removing ###\n");

	sqc_hal_bc1d2_unregister();

	wt6670_enable_detect(false);

	if (gWt6670->bdata.reset_gpio >= 0) {
		WT6670_INFO("reset on status");
		gpio_set_value(gWt6670->bdata.reset_gpio, gWt6670->bdata.reset_on_state);
	}

	return 0;
}

static void wt6670_shutdown(struct i2c_client *client)
{
	WT6670_INFO();

	if (gWt6670 == NULL) {
		WT6670_INFO("wt6670 is not initialized!!!");
		return;
	}

	WT6670_INFO("### wt6670 shuting down ###\n");

	sqc_hal_bc1d2_unregister();

	wt6670_enable_detect(false);

	if (gWt6670->bdata.reset_gpio >= 0) {
		WT6670_INFO("reset on status +++++");
		gpio_set_value(gWt6670->bdata.reset_gpio, gWt6670->bdata.reset_on_state);
	}
}


static struct i2c_driver wt6670_driver = {
	.probe = wt6670_probe,
	.remove = wt6670_remove,
	.shutdown = wt6670_shutdown,
	.id_table = wt6670_id,
	.driver = {
		.name = I2C_DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = wt6670_match_table,
	},
};

static int __init wt6670_init(void)
{
	WT6670_DEBUG_FUNC();

	pr_info("%s init\n", __func__);

	return i2c_add_driver(&wt6670_driver);
}

static void __exit wt6670_exit(void)
{
	WT6670_DEBUG_FUNC();

	return i2c_del_driver(&wt6670_driver);
}

late_initcall(wt6670_init);
module_exit(wt6670_exit);

MODULE_AUTHOR("ztecharger@zte.com.cn");
MODULE_DESCRIPTION("WT6670 Driver For SQC");
MODULE_LICENSE("GPL");
