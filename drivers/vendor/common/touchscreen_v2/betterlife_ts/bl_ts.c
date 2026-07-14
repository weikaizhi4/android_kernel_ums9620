
#include "bl_ts.h"
#include <linux/power_supply.h>

static const char *btl_ts_name = "betterlife-ts";
static const char *btl_input_phys = "input/ts";
struct btl_ts_data *g_btl_ts = NULL;
#if defined(BTL_DEBUG_SUPPORT)
bool LogEn = true;
#endif
#if defined(BTL_FACTORY_TEST_EN)
char fwname[FILE_NAME_LENGTH] = { 0 };

int rawdata_tested_flag = -1;
int btl_log_level = 0;
#endif

#if defined(BTL_SUSPEND_MODE)
static int btl_register_powermanger(struct btl_ts_data *ts);
static int btl_unregister_powermanger(struct btl_ts_data *ts);
int btl_ts_suspend(struct btl_ts_data *ts);
void btl_ts_resume(struct btl_ts_data *ts);
#endif
extern int btl_get_fw(void);
extern void blt_tpd_register_fw_class(void);
#ifdef BTL_UPDATE_FIRMWARE_WITH_REQUEST_FIRMWARE
extern int btl_touch_fw_update_check(void);
#endif

int btl_i2c_write_read(struct i2c_client* client, unsigned char addr, unsigned char* writebuf, int writelen, unsigned char* readbuf, int readlen)
{
	int ret = 0;
	int retry = 3;

	while (retry--) {
		if (readlen > 0) {
			if (writelen > 0) {
				struct i2c_msg msgs[] = {
					{
					 .addr = addr,
					 .flags = 0,
					 .len = writelen,
					 .buf = writebuf,

					 },
					{
					 .addr = client->addr,
					 .flags = I2C_M_RD,
					 .len = readlen,
					 .buf = readbuf,

					 },
				};

				ret = i2c_transfer(client->adapter, msgs, 2) == 2 ? 0 : -1;
				if (ret < 0) {

					BTL_DEBUG("error.retry = %d", retry);
					continue;
				}

			} else {
				struct i2c_msg msgs[] = {
					{
					 .addr = addr,
					 .flags = I2C_M_RD,
					 .len = readlen,
					 .buf = readbuf,

					 },
				};

				ret = i2c_transfer(client->adapter, msgs, 1) == 1 ? 0 : -1;
				if (ret < 0) {
					BTL_DEBUG("error.retry = %d", retry);
					continue;
				}
			}
		}
		break;
	}
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		if (retry == 0) {
			tpd_zlog_record_notify(TP_I2C_R_ERROR_NO);
		}
#endif
	return ret;
}

int btl_i2c_write(struct i2c_client *client, unsigned char addr, unsigned char *writebuf, int writelen)
{
	int ret = 0;
	int retry = 3;

	struct i2c_msg msgs[] = {
		{
		 .addr = addr,
		 .flags = 0,
		 .len = writelen,
		 .buf = writebuf,
		 },
	};
	while (retry--) {
		if (writelen > 0) {
			ret = i2c_transfer(client->adapter, msgs, 1) == 1 ? 0 : -1;
			if (ret < 0) {
				BTL_DEBUG("error.retry = %d\n", retry);
				continue;
			}
		}
		break;
	}
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		if (retry == 0)
			tpd_zlog_record_notify(TP_I2C_W_ERROR_NO);
#endif
	return ret;
}

int btl_i2c_read(struct i2c_client *client, unsigned char addr, unsigned char *readbuf, int readlen)
{
	int ret = 0;
	int retry = 3;

	struct i2c_msg msgs[] = {
		{
		 .addr = addr,
		 .flags = I2C_M_RD,
		 .len = readlen,
		 .buf = readbuf,
		 },
	};
	while (retry--) {
		ret = i2c_transfer(client->adapter, msgs, 1) == 1 ? 0 : -1;
		if (ret < 0) {
			BTL_DEBUG("error.retry = %d\n", retry);
			continue;
		}
		break;
	}
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		if (retry == 0)
			tpd_zlog_record_notify(TP_I2C_R_ERROR_NO);
#endif
	return ret;
}

void btl_i2c_lock(void)
{
	mutex_lock(&g_btl_ts->i2c_lock);
}

void btl_i2c_unlock(void)
{
	mutex_unlock(&g_btl_ts->i2c_lock);
}

void btl_ts_set_intmode(char mode)
{
	struct btl_ts_data *ts = g_btl_ts;

	if (mode == 0) {
		BTL_GPIO_OUTPUT(ts->irq_gpio_number, 1);
		ts->irq_gpio_dir = 0;
		ts->irq_gpio_level = 1;
	} else if (mode == 1) {
		BTL_GPIO_AS_INT(ts->irq_gpio_number);
		ts->irq_gpio_dir = 1;
	}
}

void btl_ts_set_intup(char level)
{
	struct btl_ts_data *ts = g_btl_ts;

	ts->irq_gpio_dir = 0;
	if (level == 1) {
		BTL_GPIO_OUTPUT(ts->irq_gpio_number, 1);
		ts->irq_gpio_level = 1;
	} else if (level == 0) {
		BTL_GPIO_OUTPUT(ts->irq_gpio_number, 0);
		ts->irq_gpio_level = 0;
	}
}

#if defined(INT_PIN_WAKEUP)
void btl_ts_int_wakeup(void)
{
	struct btl_ts_data *ts = g_btl_ts;

	ts->irq_gpio_dir = 0;
	BTL_GPIO_OUTPUT(ts->irq_gpio_number, 1);
	ts->irq_gpio_level = 1;
	msleep(20);
	BTL_GPIO_OUTPUT(ts->irq_gpio_number, 0);
	ts->irq_gpio_level = 1;
	usleep_range(1000, 1010);
	BTL_GPIO_OUTPUT(ts->irq_gpio_number, 1);
	ts->irq_gpio_level = 1;
	msleep(20);
}
#endif

#if defined(RESET_PIN_WAKEUP)
void btl_ts_reset_wakeup(void)
{
	struct btl_ts_data *ts = g_btl_ts;

	BTL_GPIO_OUTPUT(ts->reset_gpio_number, 1);
	ts->reset_gpio_level = 1;
	MDELAY(10);
	BTL_GPIO_OUTPUT(ts->reset_gpio_number, 0);
	ts->reset_gpio_level = 0;
	MDELAY(10);
	BTL_GPIO_OUTPUT(ts->reset_gpio_number, 1);
	ts->reset_gpio_level = 1;
	MDELAY(20);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	tpd_cdev->tp_reset_timer = jiffies;
#endif
}
#endif

void btl_irq_disable(struct btl_ts_data *ts)
{
	unsigned long irqflags;

	BTL_DEBUG_FUNC();

	spin_lock_irqsave(&ts->irq_lock, irqflags);
	if (!ts->irq_is_disable) {
		ts->irq_is_disable = 1;
		disable_irq_nosync(ts->client->irq);
	}
	spin_unlock_irqrestore(&ts->irq_lock, irqflags);
}

void btl_irq_enable(struct btl_ts_data *ts)
{
	unsigned long irqflags = 0;

	BTL_DEBUG_FUNC();

	spin_lock_irqsave(&ts->irq_lock, irqflags);
	if (ts->irq_is_disable) {
		enable_irq(ts->client->irq);
		ts->irq_is_disable = 0;
	}
	spin_unlock_irqrestore(&ts->irq_lock, irqflags);
}

void btl_enable_irq_wake(struct btl_ts_data *ts, bool enable)
{
	static bool irqflags = false;

	if (!irqflags && enable) {
		enable_irq_wake(ts->client->irq);
		irqflags = true;
		BTL_DEBUG("enable irq wake\n");
	} else if (irqflags && !enable) {
		disable_irq_wake(ts->client->irq);
		irqflags = false;
		BTL_DEBUG("disable irq wake\n");
	}
}

#ifdef BTL_POWER_CONTROL_SUPPORT
#if defined(BTL_VCC_SUPPORT)
static int btl_vcc_on(struct i2c_client *client)
{
	struct btl_ts_data *ts = NULL;
	int ret = 0;

	ts = i2c_get_clientdata(client);

	BTL_DEBUG_FUNC();
	if (!ts->vcc_status) {
#if defined(BTL_VCC_LDO_SUPPORT)
		ret = regulator_enable(ts->vcc);
		if (ret != 0) {
			BTL_ERROR("regulator enable vcc failed\n");
			ret = -EREMOTEIO;
			goto err_vcc_on;
		}
#endif

#if defined(BTL_CUSTOM_VCC_LDO_SUPPORT)
		ret = BTL_GPIO_OUTPUT(ts->vcc_gpio_number, 1);
		if (ret < 0) {
			BTL_ERROR("custom vcc ldo enable failed\n");
			goto err_custom_vcc_on;
		}
#endif
		ts->vcc_status = 1;
	}
	return ret;

#if defined(BTL_VCC_LDO_SUPPORT)
err_vcc_on:
#endif

#if defined(BTL_CUSTOM_VCC_LDO_SUPPORT)
err_custom_vcc_on:
#endif
	return ret;
}

static int btl_vcc_off(struct i2c_client *client)
{
	struct btl_ts_data *ts = NULL;
	int ret = 0;

	ts = i2c_get_clientdata(client);

	BTL_DEBUG_FUNC();
	BTL_DEBUG("ts->vcc_status = %d\n", ts->vcc_status);
	if (ts->vcc_status) {
#if defined(BTL_VCC_LDO_SUPPORT)
		BTL_DEBUG("vcc off start\n");
		ret = regulator_disable(ts->vcc);
		BTL_DEBUG("vcc off end\n");
		if (ret != 0) {
			BTL_ERROR("regulator disable vcc failed\n");
			ret = -EREMOTEIO;
			goto err_vcc_off;
		}
#endif

#if defined(BTL_CUSTOM_VCC_LDO_SUPPORT)
		ret = BTL_GPIO_OUTPUT(ts->vcc_gpio_number, 0);
		if (ret < 0) {
			BTL_ERROR("custom vcc ldo disable failed\n");
			goto err_custom_vcc_off;
		}
#endif
		ts->vcc_status = 0;
	}
	return ret;

#if defined(BTL_VCC_LDO_SUPPORT)
err_vcc_off:
#endif

#if defined(BTL_CUSTOM_VCC_LDO_SUPPORT)
err_custom_vcc_off:
#endif
	return ret;
}
#endif

#if defined(BTL_IOVCC_SUPPORT)
static int btl_iovcc_on(struct i2c_client *client)
{
	struct btl_ts_data *ts = NULL;
	int ret = 0;

	ts = i2c_get_clientdata(client);

	BTL_DEBUG_FUNC();
	if (!ts->vcc_status) {
#if defined(BTL_IOVCC_LDO_SUPPORT)
		ret = regulator_enable(ts->iovcc);
		if (ret != 0) {
			BTL_ERROR("regulator enable iovcc failed\n");
			ret = -EREMOTEIO;
			goto err_iovcc_on;
		}
#endif

#if defined(BTL_CUSTOM_IOVCC_LDO_SUPPORT)
		ret = BTL_GPIO_OUTPUT(ts->iovcc_gpio_number, 1);
		if (ret < 0) {
			BTL_ERROR("custom iovcc ldo enable failed\n");
			goto err_custom_iovcc_on;
		}
#endif
		ts->iovcc_status = 1;
	}
	return ret;

#if defined(BTL_IOVCC_LDO_SUPPORT)
err_iovcc_on:
#endif

#if defined(BTL_CUSTOM_IOVCC_LDO_SUPPORT)
err_custom_iovcc_on:
#endif
	return ret;
}

static int btl_iovcc_off(struct i2c_client *client)
{
	struct btl_ts_data *ts = NULL;
	int ret = 0;

	ts = i2c_get_clientdata(client);

	BTL_DEBUG_FUNC();
	if (ts->vcc_status) {
#if defined(BTL_IOVCC_LDO_SUPPORT)
		ret = regulator_disable(ts->iovcc);
		if (ret != 0) {
			BTL_ERROR("regulator disable iovcc failed\n");
			ret = -EREMOTEIO;
			goto err_iovcc_off;
		}
#endif

#if defined(BTL_CUSTOM_IOVCC_LDO_SUPPORT)
		ret = BTL_GPIO_OUTPUT(ts->iovcc_gpio_number, 0);
		if (ret < 0) {
			BTL_ERROR("custom iovcc ldo disable failed\n");
			goto err_custom_iovcc_off;
		}
#endif
		ts->iovcc_status = 0;
	}
	return ret;

#if defined(BTL_IOVCC_LDO_SUPPORT)
err_iovcc_off:
#endif

#if defined(BTL_CUSTOM_IOVCC_LDO_SUPPORT)
err_custom_iovcc_off:
#endif
	return ret;
}
#else
static int btl_gpio_output(struct btl_ts_data *ts, int level)
{
	BTL_GPIO_OUTPUT(ts->reset_gpio_number, level);
	ts->reset_gpio_level = level;
	BTL_GPIO_OUTPUT(ts->irq_gpio_number, level);
	ts->irq_gpio_level = level;
	ts->irq_gpio_dir = 0;
	return 0;
}
#endif

static int btl_power_on(struct i2c_client *client)
{
	struct btl_ts_data *ts = NULL;
	int ret = 0;

	ts = i2c_get_clientdata(client);

#if defined(BTL_IOVCC_SUPPORT)
	ret = btl_iovcc_off(client);
	if (ret < 0) {
		BTL_ERROR("power off iovcc failed\n");
		goto Err_Power_On;
	}
#else
	ret = btl_gpio_output(ts, 0);
#endif
	MDELAY(10);

#if defined(BTL_VCC_SUPPORT)
	ret = btl_vcc_off(client);
	if (ret < 0) {
		BTL_ERROR("power off vcc failed\n");
		goto Err_Power_On;
	}
#endif
	MDELAY(50);

#if defined(BTL_VCC_SUPPORT)
	ret = btl_vcc_on(client);
	if (ret < 0) {
		BTL_ERROR("power on vcc failed\n");
		goto Err_Power_On;
	}
#endif
	MDELAY(20);

#if defined(BTL_IOVCC_SUPPORT)
	ret = btl_iovcc_on(client);
	if (ret < 0) {
		BTL_ERROR("power on iovcc failed\n");
		goto Err_Power_On;
	}
#else
	btl_gpio_output(ts, 1);
#endif
	MDELAY(20);
	BTL_GPIO_AS_INT(ts->irq_gpio_number);
	ts->irq_gpio_dir = 1;
	MDELAY(10);

Err_Power_On:
	return ret;
}

static int btl_power_off(struct i2c_client *client)
{
	return 0;
}
#endif

static int btl_get_basic_chip_info(struct btl_ts_data *ts)
{
	int ret = 0;
#if (CTP_TYPE == SELF_CTP)
	unsigned char cmdChannel = BTL_CHANNEL_RX_REG;
#endif
#if (CTP_TYPE == SELF_INTERACTIVE_CTP)
	unsigned char cmdChannel = BTL_CHANNEL_RX_REG;
#endif

	BTL_DEBUG_FUNC();
	btl_i2c_lock();
	ret = btl_i2c_write_read(ts->client, CTP_SLAVE_ADDR, &cmdChannel, 1, &ts->chipInfo.rxChannel, 1);
	if (ret < 0) {
		goto err;
	}
#if (CTP_TYPE == SELF_CTP)
	cmdChannel = BTL_CHANNEL_KEY_REG;
	ret = btl_i2c_write_read(ts->client, CTP_SLAVE_ADDR, &cmdChannel, 1, &ts->chipInfo.keyChannel, 1);
	if (ret < 0) {
		goto err;
	}
#endif
#if (CTP_TYPE == SELF_INTERACTIVE_CTP)
	cmdChannel = BTL_CHANNEL_TX_REG;
	ret = btl_i2c_write_read(ts->client, CTP_SLAVE_ADDR, &cmdChannel, 1, &ts->chipInfo.txChannel, 1);
	if (ret < 0) {
		goto err;
	}
#endif

err:
	btl_i2c_unlock();
#if (CTP_TYPE == SELF_CTP)
	BTL_DEBUG("rx_channel:%d,key_channel=%d\n", ts->chipInfo.rxChannel, ts->chipInfo.keyChannel);
#endif
#if (CTP_TYPE == SELF_INTERACTIVE_CTP)
	BTL_DEBUG("tx_channel:%d,rx_channel=%d\n", ts->chipInfo.txChannel, ts->chipInfo.rxChannel);
#endif

	return ret;
}

#if defined(BTL_DEBUGFS_SUPPORT)
#define BTL_DEBUGFS_SAVE_INFO(fmt, args...) do { \
	if (g_btl_ts->data) { \
		g_btl_ts->datalen += snprintf( \
		g_btl_ts->data + g_btl_ts->datalen, \
		DATA_BUFFER_LEN, \
		fmt, ##args);\
	} \
} while (0)

#define BTL_DEBUGFS_SAVE_ERR(fmt, args...)  do { \
	if (g_btl_ts->data && (g_btl_ts->datalen < DATA_BUFFER_LEN)) { \
		g_btl_ts->datalen += snprintf( \
		g_btl_ts->data + g_btl_ts->datalen, \
		DATA_BUFFER_LEN, \
		fmt, ##args);\
	} \
	BTL_ERROR("%s:"fmt"\n", __func__, ##args);\
} while (0)

static int btl_debugfs_malloc_free_data(struct btl_ts_data *ts, bool allocate)
{
	if (allocate == true) {
		ts->data = vmalloc(DATA_BUFFER_LEN);
		if (ts->data == NULL) {
			BTL_ERROR("ts->testresult malloc fail\n");
			return -ENOMEM;
		}
		ts->datalen = 0;
	} else {
		if (ts->data) {
			vfree(ts->data);
			ts->data = NULL;
		}
	}
	return 0;
}

static int btl_debugfs_clear_data(struct btl_ts_data *ts)
{
	if (ts->data) {
		ts->datalen = 0;
	}
	return 0;
}

static ssize_t btl_fwVersion_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned char fwVer[3] = { 0x00, 0x00, 0x00 };
	int ret = 0;

	if ((CTP_TYPE == SELF_CTP) || (CTP_TYPE == SELF_INTERACTIVE_CTP)) {
		btl_i2c_lock();
		ret = btl_get_fwArgPrj_id(fwVer);
		btl_i2c_unlock();
		return sprintf(buf, "fwVer:%d\nargVer:%d\n", fwVer[0], fwVer[1]);
	}
	btl_i2c_lock();
	ret = btl_get_prj_id(fwVer);
	btl_i2c_unlock();
	if (ret < 0)
		return sprintf(buf, "%s\n", "Read Version FAIL");
	return sprintf(buf, "fwVer:%d\n", fwVer[0]);
}

static DEVICE_ATTR(fwVersion, 0664, btl_fwVersion_show, NULL);

static ssize_t btl_chipID_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned char chipID = 0x00;
	int ret = 0;
	struct btl_ts_data *ts = g_btl_ts;

	btl_irq_disable(ts);
	ret = btl_get_chip_id(&chipID);
	btl_irq_enable(ts);
	if (ret < 0)
		return sprintf(buf, "%s\n", "Read ChipID FAIL");
	else
		return sprintf(buf, "ChipID = %x\n", chipID);
}

static DEVICE_ATTR(chipID, 0664, btl_chipID_show, NULL);
#if (CTP_TYPE == SELF_CTP)
static ssize_t btl_cobID_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned char cobID[6] = { 0x00 };
	int ret = 0;

	btl_i2c_lock();
	ret = btl_get_cob_id(cobID);
	btl_i2c_unlock();
	if (ret < 0)
		return sprintf(buf, "%s\n", "Read cobID FAIL");
	else
		return sprintf(buf, "cobID = %x:%x:%x:%x:%x:%x\n", cobID[0], cobID[1], cobID[2], cobID[3],
			       cobID[4], cobID[5]);
}

static DEVICE_ATTR(cobID, 0664, btl_cobID_show, NULL);
#endif
#if defined(BTL_UPDATE_FARMWARE_WITH_BIN)
static ssize_t btl_fwUpdate_store(struct device *dev, struct device_attribute *attr, const char *buf,
				  size_t size)
{
	int ret = 0;
	struct btl_ts_data *ts = g_btl_ts;

	btl_i2c_lock();
	btl_irq_disable(ts);
#if defined(BTL_ESD_PROTECT_SUPPORT)
	btl_esd_switch(ts, SWITCH_OFF);
#endif
#if defined(BTL_CHARGE_PROTECT_SUPPORT)
	btl_charge_switch(ts, SWITCH_OFF);
#endif

	ret = btl_fw_upgrade_with_bin_file(BTL_FIRMWARE_BIN_PATH);

#if defined(BTL_CHARGE_PROTECT_SUPPORT)
	btl_charge_switch(ts, SWITCH_ON);
#endif
#if defined(BTL_ESD_PROTECT_SUPPORT)
	btl_esd_switch(ts, SWITCH_ON);
#endif
	btl_irq_enable(ts);
	btl_i2c_unlock();
	return size;
}

static DEVICE_ATTR(fwUpdate, 0664, NULL, btl_fwUpdate_store);
#endif

static int btl_sysfs_write_reg(unsigned char *data, int len)
{
	int ret = 0;
	struct btl_ts_data *ts = g_btl_ts;
	int i = 0;
	int size = 0;
	int writeLen = 0;

	if (len <= FLASH_WSIZE) {
		ret = btl_i2c_write(ts->client, CTP_SLAVE_ADDR, data, len);
	} else {
		for (i = 0; i < len;) {
			size = len - i;
			if (size > FLASH_WSIZE) {
				writeLen = FLASH_WSIZE;

			} else {
				writeLen = size;
			}
			ret = btl_i2c_write(ts->client, CTP_SLAVE_ADDR, data + i, writeLen);
			i += writeLen;
			if (ret < 0) {
				break;
			}
		}
	}
	return ret;
}

static int btl_sysfs_read_reg(unsigned char *wdata, unsigned int wlen, unsigned char *rdata, int rlen)
{
	int ret = 0;
	struct btl_ts_data *ts = g_btl_ts;
	int i = 0;
	int size = 0;
	int readLen = 0;

	ret = btl_i2c_write(ts->client, CTP_SLAVE_ADDR, wdata, wlen);
	if (ret < 0) {
		return ret;
	}
	if (rlen <= FLASH_RSIZE) {
		ret = btl_i2c_read(ts->client, CTP_SLAVE_ADDR, rdata, rlen);
	} else {
		for (i = 0; i < rlen;) {
			size = rlen - i;
			if (size > FLASH_RSIZE) {
				readLen = FLASH_RSIZE;

			} else {
				readLen = size;
			}

			ret = btl_i2c_read(ts->client, CTP_SLAVE_ADDR, rdata + i, readLen);
			i += readLen;
			if (ret < 0) {
				break;
			}
		}
	}
	return ret;
}

static ssize_t btl_reg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct btl_ts_data *ts = g_btl_ts;

	if (ts->data == NULL) {
		return sprintf(buf, "%s\n", "data is NULL\n");
	} else {
		return sprintf(buf, "%s\n", ts->data);
	}
}

static ssize_t btl_reg_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = 0;
	int i = 0;
	char *next;
	unsigned int wrFlag = 0x00;
	unsigned int wlen = 0x00;
	unsigned int rlen = 0x00;
	unsigned char dataMode = 0x00;
	unsigned char *wdata = NULL;
	unsigned char *rdata = NULL;
	struct btl_ts_data *ts = g_btl_ts;

	btl_debugfs_clear_data(ts);
	wrFlag = simple_strtoul(buf,  &next , 16);
	wlen = simple_strtoul(next + 1, &next, 16);

	if (wlen <= 0) {
		BTL_DEBUGFS_SAVE_ERR("wlen <= 0\n");
		goto err_wlen;
	}
	wdata = kzalloc(wlen,GFP_KERNEL);
	for(i = 0; i < wlen; i++)
	{
		wdata[i] = simple_strtoul(next+1, &next, 16);
	}

	if (wrFlag) {
		rlen = simple_strtoul(next+1, &next, 16);
		dataMode = simple_strtoul(next+1, &next, 16);
		if (rlen <= 0) {
			BTL_DEBUGFS_SAVE_ERR("rlen <= 0\n");
			goto err_rlen;
		}
		rdata = kzalloc(rlen, GFP_KERNEL);
		BTL_DEBUGFS_SAVE_INFO("Read:regAddr = %-8x rlen=%-8d dataMode = %-8d\n", wdata[0], rlen, dataMode);
	}

	if (wrFlag == 1) {
		btl_i2c_lock();
		ret = btl_sysfs_read_reg(wdata, wlen, rdata, rlen);
		btl_i2c_unlock();
		if (ret == 0) {
			if (dataMode == 0) {
				for (i = 0; i < rlen; i++) {
					BTL_DEBUGFS_SAVE_INFO("0x%-8x", rdata[i]);
				}
			}

			if (dataMode == 1) {
				for (i = 0; i < rlen;) {
					BTL_DEBUGFS_SAVE_INFO("%-8d", (short)(rdata[i + 1] << 8) + rdata[i]);
					i += 2;
				}
			}
			if (dataMode == 2) {
				for (i = 0; i < rlen;) {
					BTL_DEBUGFS_SAVE_INFO("%-8d",
							      (unsigned short)(rdata[i + 1] << 8) + rdata[i]);
					i += 2;
				}
			}
		}
	} else {
		btl_i2c_lock();
		ret = btl_sysfs_write_reg(wdata, wlen);
		btl_i2c_unlock();
	}
	if (ret < 0) {
		BTL_DEBUGFS_SAVE_INFO("\nOperation reg Fail\n");
	} else {
		BTL_DEBUGFS_SAVE_INFO("\nOperation reg Success\n");
	}

	if (wrFlag) {
		kfree(rdata);
		rdata = NULL;
	}
err_rlen:
	kfree(wdata);
	wdata = NULL;
err_wlen:
	return size;
}

static DEVICE_ATTR(reg, 0664, btl_reg_show, btl_reg_store);

static int btl_sysfs_read_speial_reg(unsigned char regAddr, unsigned char *data, int len)
{
	int ret = 0;
	struct btl_ts_data *ts = g_btl_ts;
	int i = 0;
	int size = 0;
	int readLen = 0;
	unsigned char cmd[3] = { 0x00 };

	if (len <= FLASH_RSIZE) {
		ret = btl_i2c_write(ts->client, CTP_SLAVE_ADDR, &regAddr, 1);
		ret = btl_i2c_read(ts->client, CTP_SLAVE_ADDR, data, len);
	} else {
		for (i = 0; i < len;) {
			size = len - i;
			if (size > FLASH_RSIZE) {
				readLen = FLASH_RSIZE;
			} else {
				readLen = size;
			}
			cmd[0] = regAddr;
			cmd[1] = (i & 0xff00) >> 8;
			cmd[2] = i & 0x00ff;
			BTL_DEBUG("cmd[0] = %x cmd[1]=%x cmd[2]=%x i = %d readLen = %d\n", cmd[0], cmd[1],
				  cmd[2], i, readLen);
			ret = btl_i2c_write(ts->client, CTP_SLAVE_ADDR, cmd, sizeof(cmd));
			ret = btl_i2c_read(ts->client, CTP_SLAVE_ADDR, data + i, readLen);
			i += readLen;
			if (ret < 0) {
				break;
			}
		}
	}
	return ret;
}

static ssize_t btl_special_reg_store(struct device *dev, struct device_attribute *attr, const char *buf,
				     size_t size)
{
	int ret = 0;
	int i = 0;
	int j = 0;
	int tx_channel = 0;
	int rx_channel = 0;
	char *next;
	unsigned char regAddr = 0x00;
	unsigned int len = 0x00;
	unsigned char dataMode = 0x00;
	unsigned char *data = NULL;
	struct btl_ts_data *ts = g_btl_ts;

	btl_debugfs_clear_data(ts);
	BTL_DEBUGFS_SAVE_INFO("Read special reg start!\n");
	BTL_DEBUG("Read special reg start!\n");
	regAddr  = simple_strtoul(buf, &next, 16);
	len = simple_strtoul(next+1, &next, 16);
	dataMode = simple_strtoul(next+1, &next, 16);
#if (CTP_TYPE == SELF_INTERACTIVE_CTP)
	tx_channel = ts->chipInfo.txChannel;
	rx_channel = ts->chipInfo.rxChannel;
#endif
#if (CTP_TYPE==SELF_CTP)
	tx_channel = 1;
	rx_channel = ts->chipInfo.keyChannel + ts->chipInfo.rxChannel;
#endif
	BTL_DEBUGFS_SAVE_INFO("regAddr = 0x%-8x len = %-8d dataMode = %-8d tx=%-8d rx=%-8d\n", regAddr, len,
			      dataMode, tx_channel, ts->chipInfo.rxChannel);
	BTL_DEBUG("regAddr = 0x%-8x len = %-8d dataMode = %-8d tx=%-8d rx=%-8d\n", regAddr, len, dataMode,
		  tx_channel, ts->chipInfo.rxChannel);
	data = kzalloc(len, GFP_KERNEL);

	btl_i2c_lock();
	ret = btl_sysfs_read_speial_reg(regAddr, data, len);
	btl_i2c_unlock();
	if (ret == 0) {
		if (dataMode == 0) {
			for (j = 0; j < len; j++) {
				BTL_DEBUGFS_SAVE_INFO("0x%-8x", data[i]);
			}
		}
		if (dataMode == 1) {
			for (i = 0; i < tx_channel; i++) {
				BTL_DEBUGFS_SAVE_INFO("TX%d:", i);
				for(j = 0; j < rx_channel*2;)
				{
					BTL_DEBUGFS_SAVE_INFO("%-8d",(short)(data[2 * i * rx_channel+j+1]<<8) + data[2*i*rx_channel+j]);
					j += 2;
				}
				BTL_DEBUGFS_SAVE_INFO("\n");
			}
		}
		if (dataMode == 2) {
			for (i = 0; i < tx_channel; i++) {
				BTL_DEBUGFS_SAVE_INFO("TX%d:", i);
				for(j = 0; j < rx_channel*2;)
				{
					BTL_DEBUGFS_SAVE_INFO("%-8d",(unsigned short)(data[2 * i * rx_channel+j+1]<<8) + data[2*i*rx_channel+j]);
					j += 2;
				}
				BTL_DEBUGFS_SAVE_INFO("\n");
			}
		}
	}

	if (ret < 0) {
		BTL_DEBUGFS_SAVE_INFO("\nOperation special reg Fail");
	} else {
		BTL_DEBUGFS_SAVE_INFO("\nOperation special reg Success");
	}

	kfree(data);
	data = NULL;
	return size;
}

static ssize_t btl_special_reg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct btl_ts_data *ts = g_btl_ts;

	if (ts->data == NULL) {
		return sprintf(buf, "%s\n", "data is NULL\n");
	} else {
		return sprintf(buf, "%s\n", ts->data);
	}
}

static DEVICE_ATTR(special_reg, 0664, btl_special_reg_show, btl_special_reg_store);

#if defined(BTL_DEBUG_SUPPORT)
static ssize_t btl_log_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", LogEn);
}

static ssize_t btl_log_enable_store(struct device *dev, struct device_attribute *attr, const char *buf,
				    size_t count)
{
	if (kstrtobool_from_user(buf, count, &LogEn)) {
		return -EINVAL;
	}
	return count;
}

static DEVICE_ATTR(logEn, 0664, btl_log_enable_show, btl_log_enable_store);
#endif

#if defined(BTL_FACTORY_TEST_EN)
static ssize_t btl_test_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", fwname);
}

static ssize_t btl_test_store(struct device *dev, struct device_attribute *attr, const char *buf,
			      size_t count)
{
	struct btl_ts_data *ts = g_btl_ts;

	if (ts->bl_is_suspend) {
		BTL_ERROR("In suspend, no test, return now");
		return -EINVAL;
	}

	memset(fwname, 0, sizeof(fwname));
	snprintf(fwname, FILE_NAME_LENGTH, "%s", buf);
	fwname[count - 1] = '\0';
	BTL_ERROR("fwname:%s.", fwname);

	btl_i2c_lock();
	btl_irq_disable(ts);

#if defined(BTL_ESD_PROTECT_SUPPORT)
	btl_esd_switch(ts, SWITCH_OFF);
#endif

#if defined(BTL_CHARGE_PROTECT_SUPPORT)
	btl_charge_switch(ts, SWITCH_OFF);
#endif

#if defined(RESET_PIN_WAKEUP)
	btl_ts_reset_wakeup();
#endif

	MDELAY(200);
	btl_test_entry(fwname);

#if defined(RESET_PIN_WAKEUP)
	btl_ts_reset_wakeup();
#endif

#if defined(BTL_ESD_PROTECT_SUPPORT)
	btl_esd_switch(ts, SWITCH_ON);
#endif

#if defined(BTL_CHARGE_PROTECT_SUPPORT)
	btl_esd_switch(ts, SWITCH_ON);
#endif

	MDELAY(200);
	btl_irq_enable(ts);
	btl_i2c_unlock();

	return count;
}

/*  test from test.ini
*    example:echo "***.ini" > btl_test
*/
static ssize_t rawdata_status_show(struct device *ddri, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", rawdata_tested_flag);
}

static ssize_t rawdata_status_store(struct device *dev, struct device_attribute *attr, const char *buf,
				    size_t size)
{
	BTL_DEBUG("bl_store_debug");
	rawdata_tested_flag = -1;
	return 1;
}

static ssize_t btl_show_log_level(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "log level:%d\n", btl_log_level);
}

static ssize_t btl_store_log_level(struct device *dev, struct device_attribute *attr, const char *buf,
				   size_t size)
{
	int value = 0;

	if (kstrtouint_from_user(buf, size, 10, &value)) {
		return -EINVAL;
	}
	btl_log_level = value;
	return size;
}

static DEVICE_ATTR(log_level, 0664, btl_show_log_level, btl_store_log_level);
static DEVICE_ATTR(btl_test, 0664, btl_test_show, btl_test_store);
static DEVICE_ATTR(rawdata_status, 0664, rawdata_status_show, rawdata_status_store);
#endif

#if defined(BTL_SYSFS_SUSPEND_SUPPORT)
static ssize_t btl_suspend_store(struct device *dev, struct device_attribute *attr, const char *buf,
				 size_t count)
{
	unsigned int input;
	struct btl_ts_data *ts = g_btl_ts;

	BTL_DEBUG_FUNC();
	if (kstrtouint(buf, 10, &input))
		return -EINVAL;
	if (ts == NULL) {
		BTL_ERROR("ts is NULL\n");
		return -ENOMEM;
	}
	if (input == 1)
		btl_ts_suspend(ts);
	else if (input == 0)
		btl_ts_resume(ts);
	else
		return -EINVAL;

	return count;
}

static ssize_t btl_suspend_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct btl_ts_data *ts = g_btl_ts;

	return snprintf(buf, PAGE_SIZE, "%s\n", ts->bl_is_suspend ? "suspend" : "resume");
}

static DEVICE_ATTR(ts_suspend, 0664, btl_suspend_show, btl_suspend_store);
#endif

static ssize_t btl_func_onoff_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct btl_ts_data *ts = g_btl_ts;
	char str[128] = "";
	int len = 0;

	len = snprintf(str, sizeof(str), "irq_en:%d\n", !ts->irq_is_disable);

#if defined(BTL_ESD_PROTECT_SUPPORT)
	len = snprintf(str + len, sizeof(str) - len, "esd_en:%d\n", ts->esd_running);
#else
	sprintf(str, "%sesd_en:%d\n", str, 0);
	len = snprintf(str + len, sizeof(str) - len, "esd_en:%d\n",0);
#endif

#if defined(BTL_CHARGE_PROTECT_SUPPORT)
	len = snprintf(str + len, sizeof(str) - len, "charge_en:%d\n", ts->charge_running);
#else
	len = snprintf(str + len, sizeof(str) - len, "charge_en:%d\n", 0);
#endif

	return sprintf(buf, "%s", str);
}

static ssize_t btl_func_onoff_store(struct device *dev, struct device_attribute *attr, const char *buf,
				    size_t size)
{
	int value = 0;
	struct btl_ts_data *ts = g_btl_ts;

	if (kstrtouint_from_user(buf, size, 10, &value)) {
		return -EINVAL;
	}
	if (value & 0x0001) {
		btl_irq_enable(ts);
	} else {
		btl_irq_disable(ts);
	}

#if defined(BTL_ESD_PROTECT_SUPPORT)
	if (value & 0x0002) {
		btl_esd_switch(ts, SWITCH_ON);
	} else {
		btl_esd_switch(ts, SWITCH_OFF);
	}
#endif

#if defined(BTL_CHARGE_PROTECT_SUPPORT)
	if (value & 0x0004) {
		btl_charge_switch(ts, SWITCH_ON);
	} else {
		btl_charge_switch(ts, SWITCH_OFF);
	}
#endif

	return size;
}

static DEVICE_ATTR(func_onoff, 0664, btl_func_onoff_show, btl_func_onoff_store);

static ssize_t btl_rst_op_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct btl_ts_data *ts = g_btl_ts;

	return sprintf(buf, "RST LEVEL:%d\n", ts->reset_gpio_level);
}

static ssize_t btl_rst_op_store(struct device *dev, struct device_attribute *attr, const char *buf,
				size_t size)
{
	int value = 0;
	struct btl_ts_data *ts = g_btl_ts;

	if (kstrtouint_from_user(buf, size, 10, &value)) {
		return -EINVAL;
	}
	if ((value == 0) || (value == 1)) {
		BTL_GPIO_OUTPUT(ts->reset_gpio_number, value);
		ts->reset_gpio_level = value;
	} else {
		return -EBADRQC;
	}
	return size;
}

static DEVICE_ATTR(rst_op, 0664, btl_rst_op_show, btl_rst_op_store);

static ssize_t btl_int_op_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct btl_ts_data *ts = g_btl_ts;

	return sprintf(buf, "IRQ LEVEL:%d;IRQ_DIR:%d\n", ts->irq_gpio_level, ts->irq_gpio_dir);
}

static ssize_t btl_int_op_store(struct device *dev, struct device_attribute *attr, const char *buf,
				size_t size)
{
	int value = 0;
	struct btl_ts_data *ts = g_btl_ts;

	if (kstrtoint_from_user(buf, size, 10, &value)) {
		return -EINVAL;
	}
	if (value == -1) {
		BTL_GPIO_AS_INPUT(ts->irq_gpio_number);
		ts->irq_gpio_level = 0;
		ts->irq_gpio_dir = 1;
	} else if ((value == 0) || (value == 1)) {
		BTL_GPIO_OUTPUT(ts->irq_gpio_number, value);
		ts->irq_gpio_level = value;
		ts->irq_gpio_dir = 0;
	} else {
		return -EBADRQC;
	}
	return size;
}

static DEVICE_ATTR(int_op, 0664, btl_int_op_show, btl_int_op_store);

#if defined(BTL_VCC_SUPPORT)
static ssize_t btl_vcc_op_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct btl_ts_data *ts = g_btl_ts;

	return sprintf(buf, "VCC STATUS:%d\n", ts->vcc_status);
}

static ssize_t btl_vcc_op_store(struct device *dev, struct device_attribute *attr, const char *buf,
				size_t size)
{
	int ret = 0;
	int value = 0;
	struct btl_ts_data *ts = g_btl_ts;

	if (kstrtouint_from_user(buf, size, 10, &value)) {
		return -EINVAL;
	}
	if (value == 1) {
		ret = btl_vcc_on(ts->client);
	}
	if (value == 0) {
		ret = btl_vcc_off(ts->client);
	} else {
		return -EBADRQC;
	}
	if (ret < 0) {
		return -EREMOTEIO;
	}
	return size;
}

static DEVICE_ATTR(vcc_op, 0664, btl_vcc_op_show, btl_vcc_op_store);
#endif

#if defined(BTL_IOVCC_SUPPORT)
static ssize_t btl_iovcc_op_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct btl_ts_data *ts = g_btl_ts;

	return sprintf(buf, "IOVCC STATUS:%d\n", ts->iovcc_status);
}

static ssize_t btl_iovcc_op_store(struct device *dev, struct device_attribute *attr, const char *buf,
				  size_t size)
{
	int ret = 0;
	int value = 0;
	struct btl_ts_data *ts = g_btl_ts;

	if (kstrtouint_from_user(buf, size, 10, &value)) {
		return -EINVAL;
	}
	if (value == 1) {
		ret = btl_iovcc_on(ts->client);
	} else if (value == 0) {
		ret = btl_iovcc_off(ts->client);
	} else {
		return -EBADRQC;
	}
	if (ret < 0) {
		return -EREMOTEIO;
	}
	return size;
}

static DEVICE_ATTR(iovcc_op, 0664, btl_iovcc_op_show, btl_iovcc_op_store);
#endif

static ssize_t btl_driver_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", BTL_DRIVER_VERSION);
}

static DEVICE_ATTR(driver_version, 0664, btl_driver_version_show, NULL);

static struct attribute *btl_debug_attrs[] = {
	&dev_attr_fwVersion.attr,
	&dev_attr_chipID.attr,
#if (CTP_TYPE == SELF_CTP)
	&dev_attr_cobID.attr,
#endif
#if defined(BTL_UPDATE_FARMWARE_WITH_BIN)
	&dev_attr_fwUpdate.attr,
#endif
	&dev_attr_reg.attr,
#if defined(BTL_DEBUG_SUPPORT)
	&dev_attr_logEn.attr,
#endif
#if defined(BTL_FACTORY_TEST_EN)
	&dev_attr_btl_test.attr,
	&dev_attr_rawdata_status.attr,
	&dev_attr_log_level.attr,
#endif
#if defined(BTL_SYSFS_SUSPEND_SUPPORT)
	&dev_attr_ts_suspend.attr,
#endif
	&dev_attr_special_reg.attr,
	&dev_attr_func_onoff.attr,
	&dev_attr_rst_op.attr,
	&dev_attr_int_op.attr,
#if defined(BTL_VCC_SUPPORT)
	&dev_attr_vcc_op.attr,
#endif
#if defined(BTL_IOVCC_SUPPORT)
	&dev_attr_iovcc_op.attr,
#endif
	&dev_attr_driver_version.attr,
	NULL
};

static const struct attribute_group btl_debug_attr_group = {
	.attrs = btl_debug_attrs,
};

static int btl_sysfs_create(struct btl_ts_data *ts)
{
	int ret = 0;

	ts->sysfs_obj = kobject_create_and_add(BTL_I2C_NAME, NULL);
	if (ts->sysfs_obj == NULL) {
		BTL_ERROR("%s sys node add error\n", BTL_I2C_NAME);
		return -EINVAL;
	}

	ret = sysfs_create_group(ts->sysfs_obj, &btl_debug_attr_group);
	if (ret) {
		BTL_ERROR("btl_debug_attr_group created error!");
		goto CREATE_ERR;
	}

	ret = sysfs_create_link(NULL, ts->sysfs_obj, "bl_touchscreen");
	if (ret < 0) {
		BTL_ERROR("%s sysfs_create_link failed!\n", __func__);
		goto LINK_ERR;
	}

	ret = btl_debugfs_malloc_free_data(ts, true);
	if (ret < 0) {
		BTL_ERROR("sysfs data alloc failed\n");
		goto MALLOC_ERR;
	}

	return ret;

MALLOC_ERR:
	sysfs_remove_link(NULL, "touchscreen");
LINK_ERR:
	sysfs_remove_group(ts->sysfs_obj, &btl_debug_attr_group);
CREATE_ERR:
	kobject_put(ts->sysfs_obj);
	return ret;
}

static void btl_sysfs_remove(struct btl_ts_data *ts)
{
	btl_debugfs_malloc_free_data(ts, false);
	sysfs_remove_link(NULL, "touchscreen");
	sysfs_remove_group(ts->sysfs_obj, &btl_debug_attr_group);
	kobject_put(ts->sysfs_obj);
}
#endif

#ifdef BTL_APK_SUPPORT
static int btl_apk_open(struct inode *inode, struct file *file)
{
	struct btl_ts_data *ts = g_btl_ts;

	ts->apk_data = kzalloc(10240, GFP_KERNEL);
	if (ts->apk_data == NULL) {
		BTL_DEBUG("btl can't allocate memory for apk\n");
		return -EPERM;
	}
	ts->is_entry_factory = 1;
	return 0;
}

static int btl_apk_close(struct inode *inode, struct file *file)
{
	struct btl_ts_data *ts = g_btl_ts;

	if (ts->apk_data != NULL) {
		kfree(ts->apk_data);
	}
	ts->is_entry_factory = 0;
	return 0;
}

static ssize_t btl_apk_write(struct file *file, const char __user *buff, size_t count, loff_t *offp)
{

	int ret = 0;
	unsigned short len;
	struct btl_ts_data *ts = g_btl_ts;

	if (copy_from_user(ts->apk_data, buff, count)) {
		return -EFAULT;
	}

	len = ts->apk_data[2] + (ts->apk_data[3] << 8);

	switch (ts->apk_data[0]) {

	case WRITE_CMD:{
			if (ts->is_apk_debug == 1) {
				wait_event_interruptible(ts->debug_queue, ts->debug_sync_flag);
				ts->debug_sync_flag = 0;
			} else {
				btl_i2c_lock();
				ret =
				    btl_i2c_write(ts->client, CTP_SLAVE_ADDR, &ts->apk_data[FRAME_CMD_LEN],
						  len);
				btl_i2c_unlock();
			}

			if (ret < 0) {
				BTL_DEBUG(" btl ___%s:i2c transfer error___\n", __func__);
				return -EPERM;
			}
		}
		break;

	case WAKE_CMD:{
			if (ts->apk_data[FRAME_CMD_LEN]) {
				SET_WAKEUP_HIGH;
			} else {
				SET_WAKEUP_LOW;
			}

		}
		break;

	case INTERRUPT_CMD:{
			if (ts->apk_data[FRAME_CMD_LEN]) {
				btl_irq_enable(ts);
			} else {
				btl_irq_disable(ts);
			}
		}
		break;

	case I2C_LOCK_CMD:{
			BTL_DEBUG("btl btl_I2C_LOCK_CMD=%d\n", ts->apk_data[FRAME_CMD_LEN]);
			if (ts->apk_data[FRAME_CMD_LEN]) {
				btl_i2c_lock();
			} else {
				btl_i2c_unlock();
			}
		}
		break;

	default:{
			return -EPERM;
		}
		break;

	}

	return count;
}

static ssize_t btl_apk_read(struct file *file, char __user *buff, size_t count, loff_t *offp)
{
	unsigned short len;
	int ret = 0;
	struct btl_ts_data *ts = g_btl_ts;

	if (copy_from_user(ts->apk_data, buff, FRAME_CMD_LEN)) {
		return -EFAULT;
	}

	len = ts->apk_data[2] + (ts->apk_data[3] << 8);
	switch (ts->apk_data[0]) {

	case READ_CMD:{
			if (ts->is_apk_debug == 1) {
				ts->debug_sync_flag = 0;
				BTL_DEBUG(" btl ___%s %d:i2c transfer\n", __func__, __LINE__);
				wait_event_interruptible(ts->debug_queue, ts->debug_sync_flag);
			} else {
				btl_i2c_lock();
				ret = btl_i2c_read(ts->client, CTP_SLAVE_ADDR, ts->apk_data, len);
				btl_i2c_unlock();
			}

			if (ret < 0) {
				BTL_DEBUG("btl ___%s %d:i2c transfer\n", __func__, __LINE__);
				return -EPERM;
			}
		}
		break;

	case DRIVER_INFO:{
			ts->apk_data[0] = 0x00;
		}
		break;

	default:{
			return -EPERM;
		}
		break;

	}

	if (copy_to_user(buff, ts->apk_data, len)) {
		return -EFAULT;
	}

	return count;
}

struct file_operations btl_apk_fops = {
	.owner = THIS_MODULE,
	.open = btl_apk_open,
	.release = btl_apk_close,
	.read = btl_apk_read,
	.write = btl_apk_write,
};

static int btl_apk_debug_init(struct btl_ts_data *ts)
{
	BTL_DEBUG_FUNC();
	ts->debug_sync_flag = 0;
	ts->is_apk_debug = 0;
	ts->proc_entry = proc_create(BTL_APK_PROC_DIRNAME, 0664, NULL, &btl_apk_fops);
	if (!ts->proc_entry) {
		return -ENOMEM;
	}
	return 0;
}

static void btl_apk_debug_exit(struct btl_ts_data *ts)
{
	BTL_DEBUG_FUNC();
	if (ts->proc_entry) {
		remove_proc_entry(BTL_APK_PROC_DIRNAME, NULL);
	}
}
#endif

#if defined(BTL_SYSFS_VIRTRUAL_KEY_SUPPORT)
static ssize_t virtual_keys_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%s:%s:%d:%d:%d:%d:%s:%s:%d:%d:%d:%d:%s:%s:%d:%d:%d:%d\n", __stringify(EV_KEY),
		       __stringify(KEY_MENU), g_btl_ts->virtualkeys[0], g_btl_ts->virtualkeys[1],
		       g_btl_ts->virtualkeys[2], g_btl_ts->virtualkeys[3]
		       , __stringify(EV_KEY), __stringify(KEY_HOMEPAGE), g_btl_ts->virtualkeys[4],
		       g_btl_ts->virtualkeys[5], g_btl_ts->virtualkeys[6], g_btl_ts->virtualkeys[7]
		       , __stringify(EV_KEY), __stringify(KEY_BACK), g_btl_ts->virtualkeys[8],
		       g_btl_ts->virtualkeys[9], g_btl_ts->virtualkeys[10], g_btl_ts->virtualkeys[11]);
}

static struct kobj_attribute virtual_keys_attr = {
	.attr = {
		 .name = "virtualkeys.btl_ts",
		 .mode = S_IRUGO,
		 },
	.show = &virtual_keys_show,
};

static struct attribute *properties_attrs[] = {
	&virtual_keys_attr.attr,
	NULL
};

static struct attribute_group properties_attr_group = {
	.attrs = properties_attrs,
};

static int btl_ts_virtrual_key_init(struct btl_ts_data *ts)
{
	int ret = 0;
	struct kobject *properties_kobj;

	ts->sysfs_key_obj = kobject_create_and_add(VIRTRUAL_KEY_SYSFS_PATH, NULL);
	if (ts->sysfs_key_obj == NULL) {
		BTL_ERROR("%s sys node add error\n", VIRTRUAL_KEY_SYSFS_PATH);
		return -EINVAL;
	}

	ret = sysfs_create_group(ts->sysfs_key_obj, &properties_attr_group);
	if (ret) {
		BTL_ERROR("btl_debug_attr_group created error!");
		goto CREATE_ERR;
	}

	return ret;

CREATE_ERR:
	kobject_put(ts->sysfs_key_obj);
	return ret;

}

static void btl_ts_virtrual_key_exit(struct btl_ts_data *ts)
{
	sysfs_remove_group(ts->sysfs_key_obj, &properties_attr_group);
	kobject_put(ts->sysfs_key_obj);
}
#endif

#if defined(BTL_PROXIMITY_SUPPORT)
static void btl_prox_ctrl(int enable)
{
	unsigned char cmdPsOn[] = { BTL_CTP_PROXIMITY_MODE_REG, 0x01 };
	unsigned char cmdPsOff[] = { BTL_CTP_PROXIMITY_MODE_REG, 0x00 };

	g_btl_ts->proximity_state = BTL_CTP_PROXIMITY_LEAVE;
	if (enable) {
		g_btl_ts->proximity_enable = 1;
		BTL_DEBUG("g_btl_ts->proximity_enable = %d\n", enable);
		btl_i2c_write(g_btl_ts->client, CTP_SLAVE_ADDR, cmdPsOn, sizeof(cmdPsOn));
	} else {
		g_btl_ts->proximity_enable = 0;
		BTL_DEBUG("g_btl_ts->proximity_enable = %d\n", enable);
		btl_i2c_write(g_btl_ts->client, CTP_SLAVE_ADDR, cmdPsOff, sizeof(cmdPsOff));
	}
}

static int btl_ps_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int btl_ps_release(struct inode *inode, struct file *file)
{
	btl_prox_ctrl(0);
	return 0;
}

static long btl_ps_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int flag;
	void __user *argp = (void __user *)arg;

	BTL_DEBUG_FUNC();

	switch (cmd) {
	case LTR_IOCTL_SET_PFLAG:
		if (copy_from_user(&flag, argp, sizeof(flag))) {
			return -EFAULT;
		}
		if (flag < 0 || flag > 1) {
			return -EINVAL;
		}
		btl_prox_ctrl(flag);
		BTL_DEBUG("flag = %d\n", flag);
		break;
	case LTR_IOCTL_GET_PFLAG:
		flag = (g_btl_ts->proximity_state == BTL_CTP_PROXIMITY_NEAR) ? 0 : 1;
		if (copy_to_user(argp, &flag, sizeof(flag))) {
			return -EFAULT;
		}
		break;
	default:
		break;
	}

	return 0;
}

static struct file_operations btl_ps_fops = {
	.owner = THIS_MODULE,
	.open = btl_ps_open,
	.release = btl_ps_release,
	.unlocked_ioctl = btl_ps_ioctl,
};

static struct miscdevice btl_ps_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ltr_558als",
	.fops = &btl_ps_fops,
};

static ssize_t tpd_psensor_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 64, "%d\n", g_btl_ts->proximity_enable);
}

static ssize_t tpd_psensor_enable_store(struct device *dev, struct device_attribute *attr, const char *buf,
					size_t count)
{
	unsigned int enable;
	int ret = 0;
	struct btl_ts_data *ts = g_btl_ts;

	ret = kstrtouint(buf, 0, &enable);
	if (ret)
		return -EINVAL;
	enable = (enable > 0) ? 1 : 0;
	if (!ts->bl_is_suspend ) {
		btl_prox_ctrl(enable);
		BTL_DEBUG("enable = %d\n", enable);
	} else {
		g_btl_ts->proximity_enable = enable;
		BTL_DEBUG("suspend enable = %d\n", enable);
	}

	return count;

}

static DEVICE_ATTR(enable, 0644, tpd_psensor_enable_show, tpd_psensor_enable_store);

static ssize_t tpd_psensor_flush_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 64, "%d\n", g_btl_ts->proximity_enable);
}

static ssize_t tpd_psensor_flush_store(struct device *dev, struct device_attribute *attr, const char *buf,
					size_t count)
{
	struct btl_ts_data *ts = g_btl_ts;

	input_report_abs(ts->ps_input_dev, ABS_DISTANCE, -1);
	input_mt_sync(ts->ps_input_dev);
	input_sync(ts->ps_input_dev);

	return count;
}

static DEVICE_ATTR(flush, 0644, tpd_psensor_flush_show, tpd_psensor_flush_store);

static struct attribute *tpd_attributes[] = {
	&dev_attr_enable.attr,
	&dev_attr_flush.attr,
	NULL
};

static struct attribute_group tpd_attribute_group = {
	.attrs = tpd_attributes
};


static int btl_prox_sensor_init(struct btl_ts_data *ts)
{
	int ret = 0;

	ret = misc_register(&btl_ps_device);
	if (ret < 0) {
		BTL_DEBUG("btl error::btl_ps_device register failed\n");
		return -ENODEV;
	}

	ts->ps_input_dev = input_allocate_device();
	if (ts->ps_input_dev == NULL) {
		ret = -ENOMEM;
		BTL_DEBUG("btl error::failed to allocate ps-input device\n");
		goto exit_input_allocate_device_failed;
	}
	ts->ps_input_dev->name = "proximity_tp"; /* alps_pxy*/
	set_bit(EV_ABS, ts->ps_input_dev->evbit);
	input_set_capability(ts->ps_input_dev, EV_ABS, ABS_DISTANCE);
	input_set_abs_params(ts->ps_input_dev, ABS_DISTANCE, 0, 1, 0, 0);
	ret = input_register_device(ts->ps_input_dev);
	if (ret < 0) {
		BTL_DEBUG("btl error::failed to register ps-input device\n");
		goto exit_input_register_device_failed;
	}
	ret = sysfs_create_group(&ts->ps_input_dev->dev.kobj, &tpd_attribute_group);
	if (ret) {
		BTL_ERROR("input create group failed");
		goto exit_input_register_device_failed;
	}
	return ret;
exit_input_register_device_failed:
	input_free_device(ts->ps_input_dev);
exit_input_allocate_device_failed:
	misc_deregister(&btl_ps_device);
	return ret;
}

static void btl_prox_sensor_exit(struct btl_ts_data *ts)
{
	input_unregister_device(ts->ps_input_dev);
	misc_deregister(&btl_ps_device);
}
#endif

#if defined(BTL_GESTURE_SUPPORT)
static int btl_ts_gesture_init(struct btl_ts_data *ts)
{
	BTL_DEBUG_FUNC();

	input_set_capability(ts->input_dev, EV_KEY, BTL_KEY_GSTURE_WAKE);
	input_set_capability(ts->input_dev, EV_KEY, BTL_KEY_GESTURE_U);
	input_set_capability(ts->input_dev, EV_KEY, BTL_KEY_GESTURE_UP);
	input_set_capability(ts->input_dev, EV_KEY, BTL_KEY_GESTURE_DOWN);
	input_set_capability(ts->input_dev, EV_KEY, BTL_KEY_GESTURE_LEFT);
	input_set_capability(ts->input_dev, EV_KEY, BTL_KEY_GESTURE_RIGHT);
	input_set_capability(ts->input_dev, EV_KEY, BTL_KEY_GESTURE_O);
	input_set_capability(ts->input_dev, EV_KEY, BTL_KEY_GESTURE_E);
	input_set_capability(ts->input_dev, EV_KEY, BTL_KEY_GESTURE_M);
	input_set_capability(ts->input_dev, EV_KEY, BTL_KEY_GESTURE_W);
	input_set_capability(ts->input_dev, EV_KEY, BTL_KEY_GESTURE_S);
	input_set_capability(ts->input_dev, EV_KEY, BTL_KEY_GESTURE_V);
	input_set_capability(ts->input_dev, EV_KEY, BTL_KEY_GESTURE_C);
	input_set_capability(ts->input_dev, EV_KEY, BTL_KEY_GESTURE_Z);
	__set_bit(BTL_KEY_GSTURE_WAKE, ts->input_dev->keybit);
	__set_bit(BTL_KEY_GESTURE_U, ts->input_dev->keybit);
	__set_bit(BTL_KEY_GESTURE_UP, ts->input_dev->keybit);
	__set_bit(BTL_KEY_GESTURE_DOWN, ts->input_dev->keybit);
	__set_bit(BTL_KEY_GESTURE_LEFT, ts->input_dev->keybit);
	__set_bit(BTL_KEY_GESTURE_RIGHT, ts->input_dev->keybit);
	__set_bit(BTL_KEY_GESTURE_O, ts->input_dev->keybit);
	__set_bit(BTL_KEY_GESTURE_E, ts->input_dev->keybit);
	__set_bit(BTL_KEY_GESTURE_M, ts->input_dev->keybit);
	__set_bit(BTL_KEY_GESTURE_W, ts->input_dev->keybit);
	__set_bit(BTL_KEY_GESTURE_S, ts->input_dev->keybit);
	__set_bit(BTL_KEY_GESTURE_V, ts->input_dev->keybit);
	__set_bit(BTL_KEY_GESTURE_C, ts->input_dev->keybit);
	__set_bit(BTL_KEY_GESTURE_Z, ts->input_dev->keybit);

	return 0;
}

static void btl_ts_gesture_report(struct input_dev *input_dev, int gesture_id)
{
	int gesture;

	if (gesture_id == 0)
		return;
	BTL_DEBUG("gesture_id==0x%x ", gesture_id);

	switch (gesture_id) {

	case BTL_U_LEFT:
		gesture = BTL_KEY_GESTURE_U;
		break;

	case BTL_U_RIGHT:
		gesture = BTL_KEY_GESTURE_U;
		break;

	case BTL_U_UP:
		gesture = BTL_KEY_GESTURE_U;
		break;

	case BTL_U_DOWN:
		gesture = BTL_KEY_GESTURE_U;
		break;

	case BTL_MOVE_UP:
		gesture = BTL_KEY_GESTURE_UP;
		break;

	case BTL_MOVE_DOWN:
		gesture = BTL_KEY_GESTURE_DOWN;
		break;

	case BTL_MOVE_LEFT:
		gesture = BTL_KEY_GESTURE_LEFT;
		break;

	case BTL_MOVE_RIGHT:
		gesture = BTL_KEY_GESTURE_RIGHT;
		break;

	case BTL_O_GEST:
		gesture = BTL_KEY_GESTURE_O;
		break;
	case BTL_E_GEST:
		gesture = BTL_KEY_GESTURE_E;
		break;

	case BTL_M_GEST:
		gesture = BTL_KEY_GESTURE_M;
		break;

	case BTL_W_GEST:
		gesture = BTL_KEY_GESTURE_W;
		break;

	case BTL_S_GEST:
		gesture = BTL_KEY_GESTURE_S;
		break;

	case BTL_V_LEFT:
		gesture = BTL_KEY_GESTURE_V;
		break;

	case BTL_V_UP:
		gesture = BTL_KEY_GESTURE_V;
		break;

	case BTL_V_DOWN:
		gesture = BTL_KEY_GESTURE_V;
		break;

	case BTL_V_RIGHT:
		gesture = BTL_KEY_GESTURE_V;
		break;

	case BTL_C_GEST:
		gesture = BTL_KEY_GESTURE_C;
		break;

	case BTL_Z_GEST:
		gesture = BTL_KEY_GESTURE_Z;
		break;

	case BTL_DOUBLE_CLICK:
		gesture = BTL_KEY_GSTURE_WAKE;
		break;

	case BTL_SINGLE_CLICK:
		gesture = BTL_KEY_GSTURE_WAKE;
		break;

	default:
		gesture = -1;
		break;
	}

	if (gesture != -1) {
		BTL_DEBUG("Gesture Code=0x%x", gesture);
		input_report_key(input_dev, gesture, 1);
		input_sync(input_dev);
		input_report_key(input_dev, gesture, 0);
		input_sync(input_dev);
	}
}

static int btl_gesture_mode_enter(struct btl_ts_data *ts)
{
	unsigned char cmdGestureEn[] = { BTL_GESTURE_REG, 0x01 };
	int ret = 0;

	ret = btl_i2c_write(ts->client, CTP_SLAVE_ADDR, cmdGestureEn, sizeof(cmdGestureEn));
	if (ret < 0) {
		BTL_ERROR("Enter gesture mode error!");
	} else {
		btl_enable_irq_wake(ts, true);
		irq_set_irq_type(ts->client->irq, IRQF_TRIGGER_LOW | IRQF_NO_SUSPEND | IRQF_ONESHOT);
	}

	return ret;
}

static int btl_gesture_mode_exit(struct btl_ts_data *ts)
{
	unsigned char cmdGestureDis[] = { BTL_GESTURE_REG, 0x00 };
	int ret = 0;

	irq_set_irq_type(ts->client->irq, IRQF_TRIGGER_LOW | IRQF_NO_SUSPEND | IRQF_ONESHOT);
	ret = btl_i2c_write(g_btl_ts->client, CTP_SLAVE_ADDR, cmdGestureDis, sizeof(cmdGestureDis));
	if (ret < 0) {
		BTL_ERROR("Exit gesture mode error!");
	}
	return ret;
}
#endif

#if defined(BTL_ESD_PROTECT_SUPPORT)
static void btl_esd_recovery(struct btl_ts_data *ts)
{
	BTL_DEBUG("start\n");
	btl_irq_disable(ts);
#if defined(BTL_POWER_CONTROL_SUPPORT)
	btl_power_on(ts->client);
#else
#if defined(RESET_PIN_WAKEUP)
	btl_ts_reset_wakeup();
#endif
#endif
	btl_irq_enable(ts);
	BTL_DEBUG("end\n");
}

static void btl_esd_check_work(struct work_struct *work)
{
	int ret = -1;
	unsigned char cmdEsd = BTL_ESD_REG;
	unsigned char buf[4] = { 0x00 };
	struct btl_ts_data *ts = NULL;

	ts = container_of(work, struct btl_ts_data, esd_work.work);

	if (ts->esd_need_block) {
		BTL_DEBUG("Esd need block!\n");
		goto ESD_PROTECT_ERROR;
	}
	if (ts->bl_is_suspend) {
		BTL_DEBUG("Esd suspended!\n");
		goto ESD_PROTECT_ERROR;
	}

	btl_i2c_lock();
	ret = btl_i2c_write_read(ts->client, CTP_SLAVE_ADDR, &cmdEsd, 1, buf, sizeof(buf));
	btl_i2c_unlock();
	if (ret < 0) {
		BTL_DEBUG("i2c module abnormal need recovery!\n");
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		tpd_zlog_record_notify(TP_ESD_CHECK_ERROR_NO);
#endif
		btl_esd_recovery(ts);
	} else {
		if (memcmp(ts->esd_value, buf, sizeof(buf)) == 0) {
			BTL_DEBUG("IC abnormal need recovery!\n");
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		tpd_zlog_record_notify(TP_ESD_CHECK_ERROR_NO);
#endif
			btl_esd_recovery(ts);
		}
		memcpy(ts->esd_value, buf, sizeof((buf)));
	}

ESD_PROTECT_ERROR:
	if (!ts->bl_is_suspend) {
		queue_delayed_work(ts->btl_assist_wq, &ts->esd_work, ts->clk_tick_cnt_esd);
	} else {
		BTL_DEBUG("Esd suspended!");
	}
}

static int btl_ts_esd_check_init(struct btl_ts_data *ts)
{
	int ret = 0;

	INIT_DELAYED_WORK(&ts->esd_work, btl_esd_check_work);
	ts->clk_tick_cnt_esd = 3 * HZ;
	ts->esd_need_block = 0;
	ts->esd_running = 0;
	memset(ts->esd_value, 0, sizeof(ts->esd_value));
	spin_lock_init(&ts->esd_lock);
	return ret;
}

void btl_esd_switch(struct btl_ts_data *ts, s32 on)
{
	spin_lock(&ts->esd_lock);
	if (on == SWITCH_ON) {
		if (!ts->esd_running) {
			ts->esd_running = 1;
			spin_unlock(&ts->esd_lock);
			BTL_DEBUG("started");
			queue_delayed_work(ts->btl_assist_wq, &ts->esd_work, ts->clk_tick_cnt_esd);
		} else {
			spin_unlock(&ts->esd_lock);
		}

	} else {
		if (ts->esd_running) {
			ts->esd_running = 0;
			spin_unlock(&ts->esd_lock);
			BTL_DEBUG("cancelled");
			cancel_delayed_work_sync(&ts->esd_work);
		} else {
			spin_unlock(&ts->esd_lock);
		}
	}
}
#endif

#if defined(BTL_CHARGE_PROTECT_SUPPORT)
static unsigned char btl_get_charge_status(void)
{
	static struct power_supply *batt_psy;
	union power_supply_propval val = { 0, };
	unsigned char status = false;

	if (batt_psy == NULL)
		batt_psy = power_supply_get_by_name("battery");
	if (batt_psy) {
		batt_psy->desc->get_property(batt_psy, POWER_SUPPLY_PROP_STATUS, &val);
	}
	if ((val.intval == POWER_SUPPLY_STATUS_CHARGING) ||
		(val.intval == POWER_SUPPLY_STATUS_FULL)) {
		status = 1;
		BTL_DEBUG("Charging!");
	} else {
		status =0;
		BTL_DEBUG("DisCharging!");
	}
	return status;
}

static void btl_charge_project_work(struct work_struct *work)
{
	int ret = -1;
	unsigned char cmdCharge[2] = { AC_REG, 0x00 };
	unsigned char buf = 0x00;
	unsigned char chrStatus = 0;
	struct btl_ts_data *ts = NULL;

	ts = container_of(work, struct btl_ts_data, charge_work.work);

	if (ts->charge_need_block) {
		BTL_DEBUG("Charge need block!\n");
		goto CHARGE_PROTECT_ERROR;
	}
	if (ts->bl_is_suspend) {
		BTL_DEBUG("Charge suspended!\n");
		goto CHARGE_PROTECT_ERROR;
	}

	chrStatus = btl_get_charge_status();

	btl_i2c_lock();
	ret = btl_i2c_write_read(ts->client, CTP_SLAVE_ADDR, cmdCharge, 1, &buf, sizeof(buf));
	btl_i2c_unlock();
	if (ret < 0) {
		BTL_ERROR("obtain value of AC_REG error!\n");
		goto CHARGE_PROTECT_ERROR;
	}

	if (chrStatus != buf) {
		cmdCharge[1] = chrStatus;
		btl_i2c_lock();
		ret = btl_i2c_write(ts->client, CTP_SLAVE_ADDR, cmdCharge, sizeof(cmdCharge));
		btl_i2c_unlock();
		if (ret < 0) {
			BTL_ERROR("set value of AC_REG error!\n");
			goto CHARGE_PROTECT_ERROR;
		}
		btl_i2c_lock();
		ret = btl_i2c_write_read(ts->client, CTP_SLAVE_ADDR, cmdCharge, 1, &buf, sizeof(buf));
		btl_i2c_unlock();
		if (ret < 0) {
			BTL_ERROR("get previous set value of AC_REG error!\n");
			goto CHARGE_PROTECT_ERROR;
		}
		if (buf == chrStatus) {
			BTL_ERROR("charge switch success!\n");
		} else {
			BTL_ERROR("charge switch fail!\n");
		}
	}

CHARGE_PROTECT_ERROR:
	if (!ts->bl_is_suspend) {
		queue_delayed_work(ts->btl_assist_wq, &ts->charge_work, ts->clk_tick_cnt_charge);
	} else {
		BTL_DEBUG("Charge suspended!");
	}
}

static int btl_ts_charge_protect_init(struct btl_ts_data *ts)
{
	int ret = 0;

	INIT_DELAYED_WORK(&ts->charge_work, btl_charge_project_work);
	ts->clk_tick_cnt_charge = 3 * HZ;
	ts->charge_need_block = 0;
	ts->charge_running = 0;
	spin_lock_init(&ts->charge_lock);
	return ret;
}

void btl_charge_switch(struct btl_ts_data *ts, s32 on)
{
	spin_lock(&ts->charge_lock);
	if (on == SWITCH_ON) {
		if (!ts->charge_running) {
			ts->charge_running = 1;
			spin_unlock(&ts->charge_lock);
			BTL_DEBUG("started");
			queue_delayed_work(ts->btl_assist_wq, &ts->charge_work, ts->clk_tick_cnt_charge);
		} else {
			spin_unlock(&ts->charge_lock);
		}

	} else {
		if (ts->charge_running) {
			ts->charge_running = 0;
			spin_unlock(&ts->charge_lock);
			BTL_DEBUG("cancelled");
			cancel_delayed_work_sync(&ts->charge_work);
		} else {
			spin_unlock(&ts->charge_lock);
		}
	}
}
#endif

#if defined(BTL_VIRTRUAL_KEY_SUPPORT)
void btl_ts_report_virtrual_key_data(struct btl_ts_data *ts, s32 x, s32 y)
{
	int keyCode = KEY_CNT;
	unsigned char i;

	BTL_DEBUG_FUNC();
	BTL_DEBUG("x=%d;y=%d\n", x, y);
	for (i = 0; i < BTL_VIRTRUAL_KEY_NUM; i++) {
		if ((x == ts->btl_key_data[i].x) || (y == ts->btl_key_data[i].y)) {
			keyCode = ts->btl_key_data[i].code;
		}
		BTL_DEBUG("xKey=%d;yKey=%d;key=%x;keyCode=%x\n", ts->btl_key_data[i].x, ts->btl_key_data[i].y,
			  ts->btl_key_data[i].code, keyCode);
	}
	input_report_key(ts->input_dev, keyCode, 1);
	input_sync(ts->input_dev);
	input_report_key(ts->input_dev, keyCode, 0);
	input_sync(ts->input_dev);
}

static int btl_ts_virtrual_key_init(struct btl_ts_data *ts)
{
	int ret = 0;
	int i = 0;
	int data[BTL_VIRTRUAL_KEY_NUM][3] = BTL_VIRTRUAL_KEY_DATA;

	for (i = 0; i < BTL_VIRTRUAL_KEY_NUM; i++) {
		ts->btl_key_data[i].code = data[i][0];
		ts->btl_key_data[i].x = data[i][1];
		ts->btl_key_data[i].y = data[i][2];
	}
	return ret;
}
#endif

static void btl_touch_down(struct btl_ts_data *ts, s32 id, s32 x, s32 y, s32 w, s8 pressure)
{
#if defined(BTL_CTP_SUPPORT_TYPEB_PROTOCOL)
	input_mt_slot(ts->input_dev, id);
	input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, true);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);
	input_report_key(ts->input_dev, BTN_TOUCH, 1);
#else
	input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, id);
	input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 1);
	input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 1);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);
	input_report_key(ts->input_dev, BTN_TOUCH, 1);
	input_mt_sync(ts->input_dev);
#endif
}

static void btl_touch_up(struct btl_ts_data *ts, s32 id, s32 x, s32 y)
{
#if defined(BTL_CTP_SUPPORT_TYPEB_PROTOCOL)
	input_mt_slot(ts->input_dev, id);
	input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
#endif
	/* BTL_DEBUG("ID:%d", id); */
}

static void btl_release_all_points(struct btl_ts_data *ts)
{
#if defined(BTL_CTP_SUPPORT_TYPEB_PROTOCOL)
	unsigned char i;
#endif

#if defined(BTL_CTP_SUPPORT_TYPEB_PROTOCOL)
	for (i = 0; i < MAX_POINT_NUM; i++) {
		btl_touch_up(ts, i, 0, 0);
	}
#endif
	input_report_key(ts->input_dev, BTN_TOUCH, 0);
#ifndef BTL_CTP_SUPPORT_TYPEB_PROTOCOL
	input_mt_sync(ts->input_dev);
#endif
	input_sync(ts->input_dev);
#ifdef BTL_REPORT_BY_ZTE_ALGO
	if (tpd_cdev->zte_tp_algo) {
		tpd_clean_all_event();
	}
#endif
}

static void btl_ts_update_data(struct btl_ts_data *ts)
{
	u8 cmd = TS_DATA_REG;
	u8 point_data[2 + 6 * MAX_POINT_NUM] = { 0 };
	u8 touch_num = 0;
	u8 pressure = 0x0;
	s32 input_x = 0;
	s32 input_y = 0;
	s32 id = 0;
	u8 eventFlag = 0;
	s32 i = 0;
	s32 ret = 0;
#if defined(BTL_PROXIMITY_SUPPORT)
	u8 proxValue = BTL_CTP_PROXIMITY_LEAVE;
	u8 proxReg = BTL_CTP_PROXIMITY_FLAG_REG;
#endif

#if defined(BTL_ESD_PROTECT_SUPPORT)
	if (ts->esd_need_block == 0) {
		ts->esd_need_block = 1;
	}
#endif
#if defined(BTL_CHARGE_PROTECT_SUPPORT)
	if (ts->charge_need_block == 0) {
		ts->charge_need_block = 1;
	}
#endif
#ifdef CONFIG_TOUCHSCREEN_POINT_REPORT_CHECK
	cancel_delayed_work_sync(&tpd_cdev->point_report_check_work);
	queue_delayed_work(tpd_cdev->tpd_report_wq, &tpd_cdev->point_report_check_work, msecs_to_jiffies(150));
#endif

	if (ts->enter_update) {
		goto exit_work_func;
	}

	ret = btl_i2c_write_read(ts->client, CTP_SLAVE_ADDR, &cmd, 1, point_data, 2 + 6 * MAX_POINT_NUM);
	if (ret < 0) {
		BTL_ERROR("I2C transfer error. errno:%d\n ", ret);
		goto exit_work_func;
	}
#if defined(BTL_GESTURE_SUPPORT)
	if (ts->bl_is_suspend) {
		if (point_data[0]) {
			btl_ts_gesture_report(ts->input_dev, point_data[0]);
			goto exit_work_func;
		}
	}
#endif

#if defined(BTL_PROXIMITY_SUPPORT)
	if (ts->proximity_enable) {
		btl_i2c_write_read(ts->client, CTP_SLAVE_ADDR, &proxReg, 1, &proxValue, 1);
		if ((proxValue == BTL_CTP_PROXIMITY_NEAR) && (ts->proximity_state != proxValue)) {
			input_report_abs(ts->ps_input_dev, ABS_DISTANCE, 0);
			input_mt_sync(ts->ps_input_dev);
			input_sync(ts->ps_input_dev);
			ts->proximity_state = proxValue;
			goto exit_work_func;
		} else if ((proxValue == BTL_CTP_PROXIMITY_LEAVE) && (ts->proximity_state != proxValue)) {
			input_report_abs(ts->ps_input_dev, ABS_DISTANCE, 1);
			input_mt_sync(ts->ps_input_dev);
			input_sync(ts->ps_input_dev);
			ts->proximity_state = proxValue;
		}
	}
#endif

	touch_num = point_data[1] & 0x0f;
	if ((touch_num > MAX_POINT_NUM) || (touch_num == 0)) {
		goto exit_work_func;
	}
#if defined(BTL_CTP_PRESSURE)
	pressure = point_data[7];
#endif

#if defined(BTL_VIRTRUAL_KEY_SUPPORT)
	input_x = ((point_data[2] & 0x0f) << 8) | point_data[3];
	input_y = ((point_data[4] & 0x0f) << 8) | point_data[5];
	if (input_y > ts->TP_MAX_Y) {
		btl_ts_report_virtrual_key_data(ts, input_x, input_y);
		goto exit_work_func;
	}
#endif

	for (i = 0; i < MAX_POINT_NUM; i++) {
		eventFlag = point_data[2 + 6 * i] >> 6;
		id = point_data[4 + 6 * i] >> 4;
		input_x = ((point_data[2 + 6 * i] & 0x0f) << 8) | point_data[3 + 6 * i];
		input_y = ((point_data[4 + 6 * i] & 0x0f) << 8) | point_data[5 + 6 * i];
#ifdef BTL_REPORT_BY_ZTE_ALGO
		if ((id != 0x0f) && ((eventFlag == CTP_MOVE) || (eventFlag == CTP_DOWN))) {
			if (tpd_cdev->zte_tp_algo) {
				tpd_touch_press(ts->input_dev, input_x, input_y, id, 1, 0);
			} else {
				btl_touch_down(ts, id, input_x, input_y, 1, pressure);
			}
		} else if (eventFlag == CTP_UP) {
			if (tpd_cdev->zte_tp_algo) {
			 	tpd_touch_release(ts->input_dev, id);
			} else {
				btl_touch_up(ts, id, input_x, input_y);
			}
		}
#else
		if ((id != 0x0f) && ((eventFlag == CTP_MOVE) || (eventFlag == CTP_DOWN))) {
			btl_touch_down(ts, id, input_x, input_y, 1, pressure);
		} else if (eventFlag == CTP_UP) {
			btl_touch_up(ts, id, input_x, input_y);
		}
#endif
	}

	input_sync(ts->input_dev);
	return;
exit_work_func:
#if defined(BTL_ESD_PROTECT_SUPPORT)
	if (ts->esd_need_block == 1) {
		ts->esd_need_block = 0;
	}
#endif
#if defined(BTL_CHARGE_PROTECT_SUPPORT)
	if (ts->charge_need_block == 1) {
		ts->charge_need_block = 0;
	}
#endif
	btl_release_all_points(ts);
}

static void btl_ts_work_func(struct work_struct *work)
{
	struct btl_ts_data *ts = NULL;

	BTL_DEBUG_FUNC();
	ts = container_of(work, struct btl_ts_data, work);
	btl_i2c_lock();
	btl_ts_update_data(ts);
	btl_i2c_unlock();
}

static enum hrtimer_restart btl_ts_timer_handler(struct hrtimer *timer)
{
	struct btl_ts_data *ts = container_of(timer, struct btl_ts_data, timer);

	BTL_DEBUG_FUNC();

	queue_work(ts->btl_wq, &ts->work);
	hrtimer_start(&ts->timer, ktime_set(0, (BTL_POLL_TIME + 6) * 1000000), HRTIMER_MODE_REL);
	return HRTIMER_NORESTART;
}

#if defined(BTL_WAIT_QUEUE)
static int touch_event_handler(void *unused)
{
	struct btl_ts_data *ts = g_btl_ts;
	struct sched_param param = {.sched_priority = 4 };

	sched_setscheduler(current, SCHED_RR, &param);
	do {
		set_current_state(TASK_INTERRUPTIBLE);
		wait_event_interruptible(ts->waiter, (ts->tpd_flag != 0));
		ts->tpd_flag = 0;
		set_current_state(TASK_RUNNING);
		btl_i2c_lock();
		btl_ts_update_data(ts);
		btl_i2c_unlock();

	} while (!kthread_should_stop());

	return 0;
}
#endif

static irqreturn_t btl_ts_irq_handler(int irq, void *dev_id)
{
	struct btl_ts_data *ts = g_btl_ts;

#ifdef BTL_WAIT_QUEUE
	ts->tpd_flag = 1;
	wake_up_interruptible(&ts->waiter);
	return IRQ_HANDLED;
#endif
#ifdef BTL_WORK_QUEUE
	if (!work_pending(&ts->work)) {
		queue_work(ts->btl_wq, &ts->work);
	}
	return IRQ_HANDLED;
#endif
#ifdef BTL_THREADED_IRQ
	btl_i2c_lock();
	btl_ts_update_data(ts);
	btl_i2c_unlock();
	return IRQ_HANDLED;
#endif
}

static void btl_free_platform_resource(struct btl_ts_data *ts)
{
	BTL_GPIO_FREE(ts->irq_gpio_number);
	BTL_GPIO_FREE(ts->reset_gpio_number);
#if defined(BTL_CUSTOM_VCC_LDO_SUPPORT)
	BTL_GPIO_FREE(ts->vcc_gpio_number);
#endif
#if defined(BTL_VCC_LDO_SUPPORT)
	regulator_put(ts->vcc);
	ts->vcc = NULL;
#endif
#if defined(BTL_CUSTOM_IOVCC_LDO_SUPPORT)
	BTL_GPIO_FREE(ts->iovcc_gpio_number);
#endif
#if defined(BTL_IOVCC_LDO_SUPPORT)
	regulator_put(ts->iovcc);
	ts->iovcc = NULL;
#endif

}

static int btl_request_platform_resource(struct btl_ts_data *ts)
{
	int ret = 0;

	BTL_DEBUG_FUNC();
	ret = BTL_GPIO_REQUEST(ts->irq_gpio_number, "BTL INT IRQ");
	if (ret < 0) {
		BTL_ERROR("Failed to request GPIO Int:%d, ERRNO:%d", (s32) ts->irq_gpio_number, ret);
		return -ENODEV;
	}
	BTL_GPIO_AS_INT(ts->irq_gpio_number);
	ts->client->irq = gpio_to_irq(ts->irq_gpio_number);

#if defined(RESET_PIN_WAKEUP)
	ret = BTL_GPIO_REQUEST(ts->reset_gpio_number, "BTL RST PORT");
	if (ret < 0) {
		BTL_ERROR("Failed to request GPIO rst:%d, ERRNO:%d", (s32) ts->reset_gpio_number, ret);
		ret = -ENODEV;
		goto RST_REQUEST_ERR;
	}
#endif

#if defined(BTL_VCC_LDO_SUPPORT)
	ts->vcc = regulator_get(&ts->client->dev, ts->vcc_name);
	if (IS_ERR(ts->vcc)) {
		ret = -ENODEV;
		BTL_ERROR("Failed to request regulator vcc\n");
		goto VCC_REQUEST_LDO_ERR;
	}
	ret = regulator_set_voltage(ts->vcc, 2800000, 2800000);
	if (ret != 0) {
		BTL_ERROR("Failed to set vcc voltage\n");
		ret = -EREMOTEIO;
		goto VCC_SET_LDO_VOL_ERR;
	}
#endif

#if defined(BTL_CUSTOM_VCC_LDO_SUPPORT)
	ret = BTL_GPIO_REQUEST(ts->vcc_gpio_number, "BTL VCC PORT");
	if (ret < 0) {
		BTL_ERROR("Failed to request GPIO vcc:%d, ERRNO:%d", (s32) ts->vcc_gpio_number, ret);
		ret = -ENODEV;
		goto VCC_REQUEST_ERR;
	}
#endif

#if defined(BTL_IOVCC_LDO_SUPPORT)
	ts->iovcc = regulator_get(&ts->client->dev, ts->iovcc_name);
	if (IS_ERR(ts->iovcc)) {
		ret = -ENODEV;
		BTL_ERROR("Failed to request regulator iovcc\n");
		goto IOVCC_REQUEST_LDO_ERR;
	}
	ret = regulator_set_voltage(ts->iovcc, 2800000, 2800000);
	if (ret != 0) {
		BTL_ERROR("Failed to set iovcc voltage\n");
		ret = -EREMOTEIO;
		goto IOVCC_SET_LDO_VOL_ERR;
	}
#endif

#if defined(BTL_CUSTOM_IOVCC_LDO_SUPPORT)
	ret = BTL_GPIO_REQUEST(ts->iovcc_gpio_number, "BTL IOVCC PORT");
	if (ret < 0) {
		BTL_ERROR("Failed to request GPIO iovcc:%d, ERRNO:%d", (s32) ts->iovcc_gpio_number, ret);
		ret = -ENODEV;
		goto IOVCC_REQUEST_ERR;
	}
#endif

	return ret;

#if defined(BTL_CUSTOM_IOVCC_LDO_SUPPORT)
IOVCC_REQUEST_ERR:
#endif

#if defined(BTL_IOVCC_LDO_SUPPORT)
IOVCC_SET_LDO_VOL_ERR:
	regulator_put(ts->iovcc);
IOVCC_REQUEST_LDO_ERR:
	ts->iovcc = NULL;
#endif

#if defined(BTL_CUSTOM_VCC_LDO_SUPPORT)
	BTL_GPIO_FREE(ts->vcc_gpio_number);
VCC_REQUEST_ERR:
#if defined(RESET_PIN_WAKEUP)
	BTL_GPIO_FREE(ts->reset_gpio_number);
#endif
#endif

#if defined(BTL_VCC_LDO_SUPPORT)
VCC_SET_LDO_VOL_ERR:
	regulator_put(ts->vcc);
VCC_REQUEST_LDO_ERR:
	ts->vcc = NULL;
#if defined(RESET_PIN_WAKEUP)
	BTL_GPIO_FREE(ts->reset_gpio_number);
#endif
#endif

#if defined(RESET_PIN_WAKEUP)
RST_REQUEST_ERR:
#endif
	BTL_GPIO_FREE(ts->irq_gpio_number);
	return ret;
}

static int btl_request_irq(struct btl_ts_data *ts)
{
	int ret = -1;
	const int irq_table[] = BTL_IRQ_TAB;

	BTL_DEBUG_FUNC();
	BTL_GPIO_AS_INT(ts->irq_gpio_number);
	ts->irq_gpio_dir = 1;
	ts->int_trigger_type = 0;
	BTL_DEBUG("INT trigger type:%x", ts->int_trigger_type);
#ifdef BTL_THREADED_IRQ
	ret = request_threaded_irq(ts->client->irq,
				   NULL,
				   btl_ts_irq_handler, irq_table[ts->int_trigger_type], ts->client->name, ts);
#else
	ret = request_irq(ts->client->irq,
			  btl_ts_irq_handler, irq_table[ts->int_trigger_type], ts->client->name, ts);
#endif
	if (ret) {
		BTL_ERROR("Request IRQ failed!ERRNO:%d.", ret);
		BTL_GPIO_AS_INPUT(ts->irq_gpio_number);
		ts->irq_gpio_dir = 1;
		hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		ts->timer.function = btl_ts_timer_handler;
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
		return -EPERM;
	}
	btl_irq_disable(ts);
	ts->use_irq = 1;
	return 0;
}

static int btl_request_input_dev(struct btl_ts_data *ts)
{
	int ret = -1;

	BTL_DEBUG_FUNC();

	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) {
		BTL_ERROR("Failed to allocate input device.");
		return -ENOMEM;
	}

	ts->input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	ts->input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	__set_bit(INPUT_PROP_DIRECT, ts->input_dev->propbit);
#if defined(BTL_VIRTRUAL_KEY_SUPPORT) || defined(BTL_SYSFS_VIRTRUAL_KEY_SUPPORT)
	set_bit(KEY_MENU, ts->input_dev->keybit);
	set_bit(KEY_HOMEPAGE, ts->input_dev->keybit);
	set_bit(KEY_BACK, ts->input_dev->keybit);
#endif
#if defined(BTL_CTP_SUPPORT_TYPEB_PROTOCOL)
	input_mt_init_slots(ts->input_dev, MAX_POINT_NUM, INPUT_MT_DIRECT);
#endif
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, ts->TP_MAX_X, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, ts->TP_MAX_Y, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TRACKING_ID, 0, MAX_POINT_NUM, 0, 0);
#if defined(BTL_CTP_PRESSURE)
	input_set_abs_params(ts->input_dev, ABS_MT_PRESSURE, 0, 255, 0, 0);
#endif
	ts->input_dev->name = btl_ts_name;
	ts->input_dev->phys = btl_input_phys;
	ts->input_dev->id.bustype = BUS_I2C;
	ts->input_dev->id.vendor = 0xDEAD;
	ts->input_dev->id.product = 0xBEEF;
	ts->input_dev->id.version = 10427;

	ret = input_register_device(ts->input_dev);
	if (ret) {
		BTL_ERROR("Register %s input device failed", ts->input_dev->name);
		input_free_device(ts->input_dev);
		return -ENODEV;
	}
	return 0;
}

#if defined(BTL_CONFIG_OF)
static int btl_parse_dt(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct i2c_client *client = to_i2c_client(dev);
	struct btl_ts_data *ts = i2c_get_clientdata(client);
	int ret = 0;
#if defined(BTL_SYSFS_VIRTRUAL_KEY_SUPPORT)
	int i = 0;
	u32 data[12] = { 0 };
#endif
	u32 tmp_val_u32;

	BTL_DEBUG_FUNC();
#if defined(RESET_PIN_WAKEUP)
	ts->reset_gpio_number = of_get_gpio(np, 0);
	if (ts->reset_gpio_number < 0) {
		BTL_ERROR("fail to get reset_gpio_number\n");
		goto fail;
	}
#endif

	ts->irq_gpio_number = of_get_gpio(np, 1);
	if (ts->irq_gpio_number < 0) {
		BTL_ERROR("fail to get irq_gpio_number\n");
		return -ENODEV;
	}
#if defined(BTL_VCC_LDO_SUPPORT)
	ret = of_property_read_string(np, "vcc_name", &ts->vcc_name);
	if (ret) {
		BTL_ERROR("fail to get ldo for vcc_name\n");
		goto fail;
	}
#endif
#if defined(BTL_CUSTOM_VCC_LDO_SUPPORT)
	ts->vcc_gpio_number = of_get_gpio(np, 2);
	if (ts->vcc_gpio_number < 0) {
		BTL_ERROR("fail to get vcc_gpio_number\n");
		goto fail;
	}
#endif

#if defined(BTL_IOVCC_LDO_SUPPORT)
	ret = of_property_read_string(np, "iovcc_name", &ts->iovcc_name);
	if (ret) {
		BTL_ERROR("fail to get ldo for iovcc_name\n");
		goto fail;
	}
#endif
#if defined(BTL_CUSTOM_IOVCC_LDO_SUPPORT)
	ts->iovcc_gpio_number = of_get_gpio(np, 3);
	if (ts->iovcc_gpio_number < 0) {
		BTL_ERROR("fail to get iovcc_gpio_number\n");
		goto fail;
	}
#endif

#if defined(BTL_SYSFS_VIRTRUAL_KEY_SUPPORT)
	ret = of_property_read_u32_array(np, "virtualkeys", data, 12);
	if (ret) {
		BTL_ERROR("fail to get virtualkeys\n");
		goto fail;
	}
	for (i = 0; i < 12; i++) {
		g_btl_ts->virtualkeys[i] = data[i];
	}
#endif

	ret = of_property_read_u32(np, "TP_MAX_X", &tmp_val_u32);
	if (ret) {
		BTL_ERROR("fail to get TP_MAX_X\n");
		goto fail;
	}
	g_btl_ts->TP_MAX_X = tmp_val_u32;
	ret = of_property_read_u32(np, "TP_MAX_Y", &tmp_val_u32);
	if (ret) {
		BTL_ERROR("fail to get TP_MAX_Y\n");
		goto fail;
	}
	g_btl_ts->TP_MAX_Y = tmp_val_u32;

	return 0;
fail:
	return -EPERM;

}
#endif

static int btl_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	s32 ret = -1;
	struct btl_ts_data *ts;

	BTL_DEBUG_FUNC();
	BTL_DEBUG("BTL Driver Version: %s", BTL_DRIVER_VERSION);
	BTL_DEBUG("BTL I2C Address: 0x%02x", client->addr);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		BTL_ERROR("I2C check functionality failed.");
		return -ENODEV;
	}
	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) {
		BTL_ERROR("Alloc GFP_KERNEL memory failed.");
		return -ENOMEM;
	}
	g_btl_ts = ts;
	ts->client = client;
	i2c_set_clientdata(client, ts);

#if defined(BTL_CONFIG_OF)
	if (client->dev.of_node) {
		ret = btl_parse_dt(&ts->client->dev);
		if (ret < 0) {
			BTL_ERROR("BTL parse dt failed!\n");
			goto ERROR_PARSE_DT;
		}
	}
#else
	ts->reset_gpio_number = BTL_RST_PORT;
	ts->irq_gpio_number = BTL_INT_PORT;
	ts->TP_MAX_X = TPD_RES_X;
	ts->TP_MAX_Y = TPD_RES_Y;
#if defined(BTL_VCC_LDO_SUPPORT)
	ts->vcc_name = "vdd28";
#endif
#if defined(BTL_CUSTOM_VCC_LDO_SUPPORT)
	ts->vcc_gpio_number = BTL_VCC_PORT;
#endif
#if defined(BTL_IOVCC_LDO_SUPPORT)
	ts->iovcc_name = "vio18";
#endif
#if defined(BTL_CUSTOM_IOVCC_LDO_SUPPORT)
	ts->iovcc_gpio_number = BTL_IOVCC_PORT;
#endif
#endif

	BTL_DEBUG("TP_MAX_X = %d TP_MAX_Y= %d rst = %d int = %d\n", ts->TP_MAX_X, ts->TP_MAX_Y,
		  ts->reset_gpio_number, ts->irq_gpio_number);
	ts->client = client;
	spin_lock_init(&ts->irq_lock);

	mutex_init(&ts->i2c_lock);

	ret = btl_request_platform_resource(ts);
	if (ret < 0) {
		BTL_ERROR("BTL request platform_resource failed.");
		goto ERR_REQUEST_PLT_RC;
	}
#if defined(BTL_POWER_CONTROL_SUPPORT)
	ret = btl_power_on(client);
	if (ret) {
		BTL_ERROR("BTL power on failed.");
		goto ERR_POWER_SWITCH;
	}
#endif

#if defined(RESET_PIN_WAKEUP)
	btl_ts_reset_wakeup();
#endif
	msleep(20);
	if (&ts->chipInfo.chipID == NULL) {
		BTL_ERROR("BTL power on failed.");
		goto ERR_CHECK_ID;
	}
#if defined(BTL_CHECK_CHIPID)
	ret = btl_get_chip_id(&ts->chipInfo.chipID);
	if (ret < 0) {
		BTL_ERROR("I2C communication ERROR!");
		goto ERR_CHECK_ID;
	} else if (ts->chipInfo.chipID != BTL_FLASH_ID) {
		BTL_ERROR("Please specify the IC model:0x%x!", ts->chipInfo.chipID);
		ret = -1;
		goto ERR_CHECK_ID;
	} else {
		BTL_DEBUG("I2C communication success:chipID = %x!", ts->chipInfo.chipID);
	}
#endif

#ifdef BTL_UPDATE_FIRMWARE_WITH_REQUEST_FIRMWARE
	btl_get_fw();
	ret = btl_touch_fw_update_check();
	if (ret < 0) {
		BTL_ERROR("Create update thread error.");
	}
 #elif defined(BTL_AUTO_UPDATE_FARMWARE)
	btl_i2c_lock();
	ret = btl_auto_update_fw();
	btl_i2c_unlock();
	if (ret < 0)
	{
		BTL_ERROR("Create update thread error.");
	}
#endif

#if defined(BTL_SYSFS_VIRTRUAL_KEY_SUPPORT) || defined(BTL_VIRTRUAL_KEY_SUPPORT)
	ret = btl_ts_virtrual_key_init(ts);
	if (ret < 0) {
		BTL_ERROR("BTL request input dev failed");
		goto ERR_KEY_INIT;
	}
#endif

	ret = btl_request_input_dev(ts);
	if (ret < 0) {
		BTL_ERROR("BTL request input dev failed");
		goto ERR_REGISTER_INPUT;
	}
#if defined(BTL_PROXIMITY_SUPPORT)
	ret = btl_prox_sensor_init(ts);
	if (ret < 0) {
		BTL_ERROR("btl proximity sensor init faled!\n");
		goto ERR_PROX_SENSOR_INIT;
	}
#endif

#if defined(BTL_GESTURE_SUPPORT)
	btl_ts_gesture_init(ts);
#endif

	ts->btl_assist_wq = create_singlethread_workqueue("btl_assist_wq");
	if (!ts->btl_assist_wq) {
		BTL_DEBUG("Create btl_assist_wq workqueue error!");
		ret = -EINVAL;
		goto ERR_ASSIST_WQ;
	}
#if defined(BTL_FACTORY_TEST_EN)
	ret = btl_test_init();
	if (ret) {
		BTL_DEBUG("btl_test_init fail");
	}
#endif

#if defined(BTL_ESD_PROTECT_SUPPORT)
	ret = btl_ts_esd_check_init(ts);
	if (ret < 0) {
		BTL_DEBUG("Create esd workqueue error!");
		goto ERR_ESD_INIT;
	}
#endif

#if defined(BTL_CHARGE_PROTECT_SUPPORT)
	ret = btl_ts_charge_protect_init(ts);
	if (ret < 0) {
		BTL_DEBUG("Create charge workqueue error!");
		goto ERR_CHARGE_INIT;
	}
#endif
	INIT_WORK(&ts->work, btl_ts_work_func);

	ts->btl_wq = create_singlethread_workqueue("btl_wq");
	if (!ts->btl_wq) {
		BTL_ERROR("Creat workqueue failed.");
		ret = -EINVAL;
		goto ERR_BTL_WQ;
	}
#if defined(BTL_WAIT_QUEUE)
	ts->btl_thread = kthread_run(touch_event_handler, 0, "btl-wait-queue");
	if (IS_ERR(ts->btl_thread)) {
		BTL_ERROR("failed to create kernel thread!\n");
		ret = -EINVAL;
		goto ERR_CREATE_KTHREAD;
	}
	init_waitqueue_head(&ts->waiter);
#endif

	ret = btl_request_irq(ts);
	if (ret < 0) {
		BTL_DEBUG("BTL works in polling mode.");
	} else {
		BTL_DEBUG("BTL works in interrupt mode.");
	}

	if (ts->use_irq) {
		btl_irq_enable(ts);
	}
#if defined(BTL_ESD_PROTECT_SUPPORT)
	btl_esd_switch(ts, SWITCH_ON);
#endif

#if defined(BTL_CHARGE_PROTECT_SUPPORT)
	btl_charge_switch(ts, SWITCH_ON);
#endif

#if defined(BTL_SUSPEND_MODE)
	ret = btl_register_powermanger(ts);
	if (ret) {
		BTL_ERROR("btl_register_powerment register error");
		goto ERR_REGISTER_PM;
	}
#endif
	ret = btl_get_basic_chip_info(ts);
	if (ret < 0) {
		BTL_ERROR("get basic chip information error!");
	}
#if defined(BTL_DEBUGFS_SUPPORT)
	ret = btl_sysfs_create(ts);
	if (ret) {
		BTL_ERROR("sysfs created error!");
		goto ERR_CREATE_SYSFS;
	}
#endif
#if defined(BTL_APK_SUPPORT)
	ret = btl_apk_debug_init(ts);
	if (ret) {
		BTL_ERROR("apk debug proc dir created error!");
		goto ERR_CREATE_PROC;
	}
#endif
	tpd_cdev->TP_have_registered = true;
	tpd_cdev->tp_chip_id = TS_CHIP_BTL;
	blt_tpd_register_fw_class();
	return 0;

#if defined(BTL_APK_SUPPORT)
ERR_CREATE_PROC:
#if defined(BTL_DEBUGFS_SUPPORT)
	btl_sysfs_remove(ts);
#endif
#endif

#if defined(BTL_DEBUGFS_SUPPORT)
ERR_CREATE_SYSFS:
#if defined(BTL_SUSPEND_MODE)
	btl_unregister_powermanger(ts);
#endif
#endif

#if defined(BTL_SUSPEND_MODE)
ERR_REGISTER_PM:
#endif
#if defined(BTL_CHARGE_PROTECT_SUPPORT)
	btl_charge_switch(ts, SWITCH_OFF);
#endif

#if defined(BTL_ESD_PROTECT_SUPPORT)
	btl_esd_switch(ts, SWITCH_OFF);
#endif

	if (ts->use_irq) {
		free_irq(client->irq, ts);
	} else {
		hrtimer_cancel(&ts->timer);
	}

#if defined(BTL_WAIT_QUEUE)
	kthread_stop(ts->btl_thread);
#endif
	if (ts->btl_wq) {
		destroy_workqueue(ts->btl_wq);
	}
#if defined(BTL_WAIT_QUEUE)
ERR_CREATE_KTHREAD:
#endif

ERR_BTL_WQ:

#if defined(BTL_ESD_PROTECT_SUPPORT)
ERR_ESD_INIT:
#endif

#if defined(BTL_FACTORY_TEST_EN)
	btl_test_exit();
#endif

#if defined(BTL_CHARGE_PROTECT_SUPPORT)
ERR_CHARGE_INIT:
#endif
	destroy_workqueue(ts->btl_assist_wq);

ERR_ASSIST_WQ:
#if defined(BTL_PROXIMITY_SUPPORT)
	btl_prox_sensor_exit(ts);
#endif

#if defined(BTL_PROXIMITY_SUPPORT)
ERR_PROX_SENSOR_INIT:
#endif
	input_unregister_device(ts->input_dev);

ERR_REGISTER_INPUT:
#if defined(BTL_SYSFS_VIRTRUAL_KEY_SUPPORT)
	btl_ts_virtrual_key_exit(ts);
#endif

#if defined(BTL_SYSFS_VIRTRUAL_KEY_SUPPORT) || defined(BTL_VIRTRUAL_KEY_SUPPORT)
ERR_KEY_INIT:
#endif

#if defined(BTL_CHECK_CHIPID)
ERR_CHECK_ID:
#endif
#if defined(BTL_POWER_CONTROL_SUPPORT)
	btl_power_off(client);
#endif

#if defined(BTL_POWER_CONTROL_SUPPORT)
ERR_POWER_SWITCH:
#endif
	btl_free_platform_resource(ts);

ERR_REQUEST_PLT_RC:
	i2c_set_clientdata(client, NULL);

#ifdef BTL_CONFIG_OF
ERROR_PARSE_DT:
#endif
	g_btl_ts = NULL;
	kfree(ts);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	if (tpd_cdev->tp_chip_id == TS_CHIP_BTL)
		tpd_cdev->ztp_probe_fail_chip_id = TS_CHIP_BTL;
#endif
	return ret;
}

static int btl_ts_remove(struct i2c_client *client)
{
	struct btl_ts_data *ts = i2c_get_clientdata(client);

	BTL_DEBUG_FUNC();
#if defined(BTL_APK_SUPPORT)
	btl_apk_debug_exit(ts);
#endif
#if defined(BTL_DEBUGFS_SUPPORT)
	btl_sysfs_remove(ts);
#endif
#if defined(BTL_SUSPEND_MODE)
	btl_unregister_powermanger(ts);
#endif

#if defined(BTL_CHARGE_PROTECT_SUPPORT)
	btl_charge_switch(ts, SWITCH_OFF);
#endif

#if defined(BTL_ESD_PROTECT_SUPPORT)
	btl_esd_switch(ts, SWITCH_OFF);
#endif

#if defined(BTL_FACTORY_TEST_EN)
	btl_test_exit();
#endif

	if (ts) {
		if (ts->use_irq) {
			BTL_GPIO_AS_INPUT(ts->irq_gpio_number);
			ts->irq_gpio_dir = 1;
			BTL_GPIO_FREE(ts->irq_gpio_number);
			free_irq(client->irq, ts);
		} else {
			hrtimer_cancel(&ts->timer);
		}
	}
#if defined(BTL_WAIT_QUEUE)
	kthread_stop(ts->btl_thread);
#endif

	if (ts->btl_wq) {
		cancel_work_sync(&ts->work);
		destroy_workqueue(ts->btl_wq);
	}

	BTL_DEBUG("btl driver removing...");
	destroy_workqueue(ts->btl_assist_wq);
#if defined(BTL_SYSFS_VIRTRUAL_KEY_SUPPORT)
	btl_ts_virtrual_key_exit(ts);
#endif
#if defined(BTL_PROXIMITY_SUPPORT)
	btl_prox_sensor_exit(ts);
#endif
	input_unregister_device(ts->input_dev);
#if defined(BTL_POWER_CONTROL_SUPPORT)
	btl_power_off(client);
#endif
	BTL_GPIO_FREE(ts->irq_gpio_number);
	BTL_GPIO_FREE(ts->reset_gpio_number);
	i2c_set_clientdata(client, NULL);
	g_btl_ts = NULL;
	kfree(ts);

	return 0;
}

#if defined(BTL_SUSPEND_MODE)
static int btl_i2c_test(void)
{
	int ret = 0;
	unsigned char testCmd = 0x01;

	ret = btl_i2c_write(g_btl_ts->client, CTP_SLAVE_ADDR, &testCmd, 1);
	return ret;
}

__attribute__ ((unused))
static int btl_enter_sleep(struct btl_ts_data *ts)
{
	int ret = 0;
	unsigned char cmd[] = { LPM_REG, 0x03 };

	BTL_DEBUG_FUNC();

	ret = btl_i2c_write(ts->client, CTP_SLAVE_ADDR, cmd, 2);
	if (ret < 0) {
		BTL_ERROR("send sleep cmd failed!\n");
		goto err_send_sleep_cmd;
	}
	msleep(20);

	BTL_DEBUG("BTL send sleep cmd success.\n");
err_send_sleep_cmd:
	return ret;
}

__attribute__ ((unused))
static int btl_wakeup_sleep(struct btl_ts_data *ts)
{
	int ret = -1;

	BTL_DEBUG_FUNC();

#if defined(RESET_PIN_WAKEUP)
	btl_ts_reset_wakeup();
#endif
#if defined(INT_PIN_WAKEUP)
	btl_ts_int_wakeup();
	btl_ts_set_intmode(1);
#endif

	ret = btl_i2c_test();
	if (ret == 0) {
		BTL_DEBUG("BTL wakeup sleep success.");
		return ret;
	}

	BTL_ERROR("BTL wakeup sleep failed.");
	return ret;
}

int btl_ts_suspend(struct btl_ts_data *ts)
{
	int ret = 0;

	BTL_DEBUG_FUNC();
	if (ts->enter_update) {
		BTL_ERROR("Touch is in update mode");
		return ret;
	}
#if defined(BTL_PROXIMITY_SUPPORT)
	if (ts->proximity_enable) {
		ts->bl_is_suspend = 0;
		btl_enable_irq_wake(ts, true);
		BTL_ERROR("Touch is in proximity mode");
		return ret;
	}
#endif

#if defined(BTL_CHARGE_PROTECT_SUPPORT)
	btl_charge_switch(ts, SWITCH_OFF);
#endif

#if defined(BTL_ESD_PROTECT_SUPPORT)
	btl_esd_switch(ts, SWITCH_OFF);
#endif

#if defined(BTL_GESTURE_SUPPORT)
	ret = btl_gesture_mode_enter(ts);
	if (ret < 0) {
		BTL_ERROR("BTL enter gesture mode failed.");
	}
#else
	ret = btl_enter_sleep(ts);
	if (ret < 0) {
		BTL_ERROR("BTL suspend failed.");
	}
	if (ts->use_irq) {
		btl_irq_disable(ts);
	} else {
		hrtimer_cancel(&ts->timer);
	}
#endif
#ifdef CONFIG_TOUCHSCREEN_POINT_REPORT_CHECK
	cancel_delayed_work_sync(&tpd_cdev->point_report_check_work);
#endif
	btl_release_all_points(ts);
	ts->bl_is_suspend = 1;
	BTL_DEBUG("System suspend.");
	return ret;
}

int btl_exit_sleep(struct btl_ts_data *ts)
{
	int ret = 0;

	BTL_DEBUG_FUNC();

	if (ts->enter_update) {
		BTL_ERROR("Touch is in update mode");
		return ret;
	}
#if defined(BTL_CHARGE_PROTECT_SUPPORT)
	btl_charge_switch(ts, SWITCH_ON);
#endif

#if defined(BTL_ESD_PROTECT_SUPPORT)
	btl_esd_switch(ts, SWITCH_ON);
#endif
#if defined(BTL_PROXIMITY_SUPPORT)
	if (ts->proximity_enable && !ts->bl_is_suspend) {
		BTL_ERROR("Touch is in proximity mode");
		btl_prox_ctrl(ts->proximity_enable);
		btl_enable_irq_wake(ts, false);
		goto btl_release_all_points;
	}
#endif
#if defined(BTL_GESTURE_SUPPORT)
	ret = btl_gesture_mode_exit(ts);
	if (ret < 0) {
		BTL_ERROR("BTL exit gesture mode failed.");
	}
#else
	ret = btl_wakeup_sleep(ts);
	if (ret < 0) {
		BTL_ERROR("BTL resume failed.");
	}
	if (ts->use_irq) {
		btl_irq_enable(ts);
	} else {
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
	}
#endif
btl_release_all_points:
	btl_release_all_points(ts);
	ts->bl_is_suspend = 0;
	BTL_DEBUG("System resume complete!\n");
	return ret;
}

static void btl_resume_work(struct work_struct *work)
{
	struct btl_ts_data *ts = NULL;

	ts = container_of(work, struct btl_ts_data, resume_work);
	BTL_DEBUG_FUNC();

	btl_exit_sleep(ts);
}

void btl_ts_resume(struct btl_ts_data *ts)
{
	queue_work(ts->btl_resume_wq, &ts->resume_work);
}

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(BTL_EARLYSUSPEND_SUPPORT)
static void btl_ts_early_suspend(struct early_suspend *h)
{
	struct btl_ts_data *ts = container_of(h, struct btl_ts_data, early_suspend);

	if (ts) {
		BTL_DEBUG("Suspend by earlysuspend module.");
		btl_ts_suspend(ts);
	}
}

static void btl_ts_late_resume(struct early_suspend *h)
{
	struct btl_ts_data *ts = container_of(h, struct btl_ts_data, early_suspend);

	if (ts) {
		BTL_DEBUG("Resume by earlysuspend module.");
		btl_ts_resume(ts);
	}
}
#endif
#if defined(CONFIG_FB) && defined(BTL_FB_SUPPORT)
static int btl_fb_notifier_callback(struct notifier_block *noti, unsigned long event, void *data)
{
	struct fb_event *ev_data = data;
	struct btl_ts_data *ts = container_of(noti, struct btl_ts_data, notifier);

	int *blank;

	BTL_DEBUG("event = %x\n", event);
	if (ev_data && ev_data->data && event == FB_EVENT_BLANK && ts) {
		blank = ev_data->data;
		if (*blank == FB_BLANK_UNBLANK) {
			BTL_DEBUG("Resume by fb notifier.");
			btl_ts_resume(ts);

		} else if (*blank == FB_BLANK_POWERDOWN) {
			BTL_DEBUG("Suspend by fb notifier.");
			btl_ts_suspend(ts);
		}
	}

	return 0;
}
#endif
#if defined(CONFIG_PM) && defined(BTL_PM_SUPPORT)
static int btl_pm_suspend(struct device *dev)
{
	struct btl_ts_data *ts = g_btl_ts;

	if (ts) {
		BTL_DEBUG("Suspend by i2c pm.");
		btl_ts_suspend(ts);
	}

	return 0;
}

static int btl_pm_resume(struct device *dev)
{
	struct btl_ts_data *ts = g_btl_ts;

	if (ts) {
		BTL_DEBUG("Resume by i2c pm.");
		btl_ts_resume(ts);
	}

	return 0;
}

static struct dev_pm_ops btl_pm_ops = {
	.suspend = btl_pm_suspend,
	.resume = btl_pm_resume,
};
#endif
#if defined(CONFIG_ADF) && defined(BTL_ADF_SUPPORT)
static int btl_adf_notifier_callback(struct notifier_block *noti, unsigned long event, void *data)
{
	struct adf_notifier_event *ev_data = data;
	int adf_event_data;
	struct btl_ts_data *ts = container_of(noti, struct btl_ts_data, notifier);

	if (ev_data && ev_data->data && event == ADF_EVENT_BLANK) {
		adf_event_data = *(int *)ev_data->data;

		if (adf_event_data == DRM_MODE_DPMS_ON) {
			BTL_DEBUG("Resume by adf notifier.");
			btl_ts_resume(ts);
		} else if (adf_event_data == DRM_MODE_DPMS_OFF) {
			BTL_DEBUG("Suspend by adf notifier.");
			btl_ts_suspend(ts);
		}
	}
	return 0;
}
#endif

static int btl_register_powermanger(struct btl_ts_data *ts)
{
	int ret = 0;

	INIT_WORK(&ts->resume_work, btl_resume_work);
	ts->btl_resume_wq = create_singlethread_workqueue("btl_resume_wq");
	if (!ts->btl_resume_wq) {
		BTL_DEBUG("Create btl_resume_wq workqueue error!");
		ret = -EINVAL;
	}
#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(BTL_EARLYSUSPEND_SUPPORT)
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = btl_ts_early_suspend;
	ts->early_suspend.resume = btl_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif
#if defined(CONFIG_FB) && defined(BTL_FB_SUPPORT)
	ts->notifier.notifier_call = btl_fb_notifier_callback;
	ret = fb_register_client(&ts->notifier);
	if (ret) {
		BTL_ERROR("register fb_register_client error");
		ERR_PTR(ret);
	}
#endif
#if defined(CONFIG_ADF) && defined(BTL_ADF_SUPPORT)
	ts->notifier.notifier_call = btl_adf_notifier_callback;
	ts->notifier.priority = 1000;
	adf_register_client(&ts->notifier);
	if (ret) {
		BTL_ERROR("register adf_register_client error");
		ERR_PTR(ret);
	}
#endif

	return ret;
}

static int btl_unregister_powermanger(struct btl_ts_data *ts)
{
#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(BTL_EARLYSUSPEND_SUPPORT)
	unregister_early_suspend(&ts->early_suspend);
#endif
#if defined(CONFIG_FB) && defined(BTL_FB_SUPPORT)
	fb_unregister_client(&ts->notifier);
#endif
#if defined(CONFIG_ADF) && defined(BTL_ADF_SUPPORT)
	adf_unregister_client(&ts->notifier);
#endif
	cancel_work_sync(&ts->resume_work);
	destroy_workqueue(ts->btl_resume_wq);
	return 0;
}
#endif

#ifdef BTL_CONFIG_OF
static const struct of_device_id btl_match_table[] = {
	{.compatible = "BTL,betterlife_ts",},
	{},
};
#endif

static const struct i2c_device_id btl_ts_id[] = {
	{BTL_I2C_NAME, 0},
	{}
};

static struct i2c_driver btl_ts_driver = {
	.probe = btl_ts_probe,
	.remove = btl_ts_remove,
	.id_table = btl_ts_id,
	.driver = {
		   .name = BTL_I2C_NAME,
		   .owner = THIS_MODULE,
#ifdef BTL_CONFIG_OF
		   .of_match_table = btl_match_table,
#endif
#if !defined(CONFIG_FB) && defined(CONFIG_PM) && defined(BTL_PM_SUPPORT)
		   .pm = &btl_pm_ops,
#endif
		   },
};

int btl_ts_init(void)
{
	s32 ret;

	BTL_DEBUG_FUNC();
	BTL_DEBUG("BTL driver installing...");
	if (get_tp_chip_id() == 0) {
		if ((tpd_cdev->tp_chip_id != TS_CHIP_MAX) && (tpd_cdev->tp_chip_id != TS_CHIP_BTL)) {
			BTL_ERROR("this tp is not used,return.\n");
			return -EPERM;
		}
	}
	if (tpd_cdev->TP_have_registered) {
		BTL_ERROR("TP have registered by other TP.\n");
		return -EPERM;
	}
	ret = i2c_add_driver(&btl_ts_driver);
	return ret;
}

void  btl_ts_exit(void)
{
	BTL_DEBUG_FUNC();
	BTL_DEBUG("BTL driver exited.");
	i2c_del_driver(&btl_ts_driver);
}

