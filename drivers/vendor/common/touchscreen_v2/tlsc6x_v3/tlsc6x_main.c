/*
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * * VERSION		DATE			AUTHOR		Note
 *
 */

#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <asm/uaccess.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
/* #include <soc/sprd/regulator.h> */
#include <linux/input/mt.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>

#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/err.h>

#include <linux/proc_fs.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/unistd.h>
#include <asm/io.h>
#include <linux/pm_runtime.h>
#include <linux/types.h>

#if defined(CONFIG_ADF)
#include <linux/notifier.h>
#include <video/adf_notifier.h>
#elif defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#elif defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#endif

#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/ioctl.h>

#ifdef CONFIG_PM_WAKELOCKS
#include <linux/pm_wakeup.h>
#else
#include <linux/wakelock.h>
#endif

#include <linux/suspend.h>
#include <linux/irq.h>

#ifdef MMI_TP_VERSION
#include <linux/string.h>
#endif
#include <linux/uaccess.h>
#include <linux/version.h>

#include "tlsc6x_main.h"
#include "ztp_common.h"

#define	TOUCH_VIRTUAL_KEYS
#define	MULTI_PROTOCOL_TYPE_B	1
#define	TS_MAX_FINGER		2

#define MAX_CHIP_ID	(10)
#define TS_NAME		"tlsc6x_ts"
unsigned char tlsc6x_chip_name[MAX_CHIP_ID][20] = {
"null", "tlsc6206a", "0x6306", "tlsc6206", "tlsc6324", "tlsc6332", "tlsc6440", "tlsc6432", "tlsc6424", "tlsc6448"
};

int g_is_telink_comp = 0;

struct tlsc6x_data *g_tp_drvdata = NULL;
static struct i2c_client *this_client;

#ifdef CONFIG_PM_WAKELOCKS
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
static struct wakeup_source *tlsc6x_wakelock;
#else
static struct wakeup_source tlsc6x_wakelock;
#endif
#else
static struct wake_lock tlsc6x_wakelock;
#endif

DEFINE_MUTEX(i2c_rw_access);

#if defined(CONFIG_ADF)
static int tlsc6x_adf_suspend(void);
static int tlsc6x_adf_resume(void);
#elif defined(CONFIG_HAS_EARLYSUSPEND)
static void tlsc6x_ts_suspend(struct early_suspend *handler);
static void tlsc6x_ts_resume(struct early_suspend *handler);
#endif

#ifdef TLSC_ESD_HELPER_EN
static int tpd_esd_flag = 0;
static struct hrtimer tpd_esd_kthread_timer;
static DECLARE_WAIT_QUEUE_HEAD(tpd_esd_waiter);
#endif

#ifdef TLSC_TPD_PROXIMITY
static int tlsc6x_prox_ctrl(int enable);
unsigned char tpd_prox_old_state = 0;
static int tpd_prox_active = 0;
static struct class *sprd_tpd_class;
static struct device *sprd_ps_cmd_dev;
#endif

#ifdef TOUCH_VIRTUAL_KEYS

static ssize_t virtual_keys_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct tlsc6x_data *data = i2c_get_clientdata(this_client);
	struct tlsc6x_platform_data *pdata = data->platform_data;

	return snprintf(buf, PAGE_SIZE, "%s:%s:%d:%d:%d:%d:%s:%s:%d:%d:%d:%d:%s:%s:%d:%d:%d:%d\n",
			__stringify(EV_KEY), __stringify(KEY_APPSELECT),
			pdata->virtualkeys[0], pdata->virtualkeys[1], pdata->virtualkeys[2],
		       pdata->virtualkeys[3]
		       , __stringify(EV_KEY), __stringify(KEY_HOMEPAGE), pdata->virtualkeys[4], pdata->virtualkeys[5],
		       pdata->virtualkeys[6], pdata->virtualkeys[7]
		       , __stringify(EV_KEY), __stringify(KEY_BACK), pdata->virtualkeys[8], pdata->virtualkeys[9],
		       pdata->virtualkeys[10], pdata->virtualkeys[11]);
}

static struct kobj_attribute virtual_keys_attr = {
	.attr = {
		 .name = "virtualkeys.tlsc6x_touch",
		 .mode = 0444,
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

static void tlsc6x_virtual_keys_init(void)
{
	int ret = 0;
	struct kobject *properties_kobj;

	TLSC_FUNC_ENTER();

	properties_kobj = kobject_create_and_add("board_properties", NULL);
	if (properties_kobj) {
		ret = sysfs_create_group(properties_kobj, &properties_attr_group);
	}
	if (!properties_kobj || ret) {
		tlsc_err("failed to create board_properties\n");
	}
}

#endif

void tlsc_irq_disable(void)
{
	TLSC_FUNC_ENTER();
	if (!g_tp_drvdata->irq_disabled) {
		disable_irq_nosync(this_client->irq);
		g_tp_drvdata->irq_disabled = true;
	}
}

void tlsc_irq_enable(void)
{
	TLSC_FUNC_ENTER();
	if (g_tp_drvdata->irq_disabled) {
		enable_irq(this_client->irq);
		g_tp_drvdata->irq_disabled = false;
	}
}
/*
    iic access interface
*/
int tlsc6x_i2c_read_sub(struct i2c_client *client, char *writebuf, int writelen, char *readbuf, int readlen)
{
	int ret = 0;

	if (client == NULL) {
		tlsc_err("[IIC][%s]i2c_client==NULL!\n", __func__);
		return -EINVAL;
	}

	if (readlen > 0) {
		if (writelen > 0) {
			struct i2c_msg msgs[] = {
				{
				 .addr = client->addr,
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
			ret = i2c_transfer(client->adapter, msgs, 2);
			if (ret < 0) {
				tlsc_err("[IIC]: i2c_transfer(2) error, addr= 0x%x!!\n", writebuf[0]);
				tlsc_err("[IIC]: i2c_transfer(2) error, ret=%d, rlen=%d, wlen=%d!!\n", ret, readlen,
				       writelen);
			}
		} else {
			struct i2c_msg msgs[] = {
				{
				 .addr = client->addr,
				 .flags = I2C_M_RD,
				 .len = readlen,
				 .buf = readbuf,
				 },
			};
			ret = i2c_transfer(client->adapter, msgs, 1);
			if (ret < 0) {
				tlsc_err("[IIC]: i2c_transfer(read) error, ret=%d, rlen=%d, wlen=%d!!", ret, readlen,
				       writelen);
			}
		}
	}

	return ret;
}

/* fail : <0 */
int tlsc6x_i2c_read(struct i2c_client *client, char *writebuf, int writelen, char *readbuf, int readlen)
{
	int ret = 0;

	/* lock in this function so we can do direct mode iic transfer in debug fun */
	mutex_lock(&i2c_rw_access);
	ret = tlsc6x_i2c_read_sub(client, writebuf, writelen, readbuf, readlen);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	if (ret < 0) {
		tpd_zlog_record_notify(TP_I2C_R_ERROR_NO);
	}
#endif
	mutex_unlock(&i2c_rw_access);

	return ret;
}

/* fail : <0 */
int tlsc6x_i2c_write_sub(struct i2c_client *client, char *writebuf, int writelen)
{
	int ret = 0;

	if (client == NULL) {
		tlsc_err("[IIC][%s]i2c_client==NULL!\n", __func__);
		return -EINVAL;
	}

	if (writelen > 0) {
		struct i2c_msg msgs[] = {
			{
			 .addr = client->addr,
			 .flags = 0,
			 .len = writelen,
			 .buf = writebuf,
			 },
		};
		ret = i2c_transfer(client->adapter, msgs, 1);
		if (ret < 0) {
			tlsc_err("[IIC]: i2c_transfer(write) error, ret=%d!!\n", ret);
		}
	}

	return ret;

}

/* fail : <0 */
int tlsc6x_i2c_write(struct i2c_client *client, char *writebuf, int writelen)
{
	int ret = 0;

	mutex_lock(&i2c_rw_access);
	ret = tlsc6x_i2c_write_sub(client, writebuf, writelen);
	mutex_unlock(&i2c_rw_access);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	if (ret < 0) {
		tpd_zlog_record_notify(TP_I2C_W_ERROR_NO);
	}
#endif
	return ret;

}

/* fail : <0 */
int tlsc6x_write_reg(struct i2c_client *client, u8 regaddr, u8 regvalue)
{
	unsigned char buf[2] = { 0 };

	buf[0] = regaddr;
	buf[1] = regvalue;

	return tlsc6x_i2c_write(client, buf, sizeof(buf));
}

/* fail : <0 */
int tlsc6x_read_reg(struct i2c_client *client, u8 regaddr, u8 *regvalue)
{
	return tlsc6x_i2c_read(client, &regaddr, 1, regvalue, 1);
}

static void tlsc6x_clear_report_data(struct tlsc6x_data *drvdata)
{
	int i;
	TLSC_FUNC_ENTER();
	for (i = 0; i < TS_MAX_FINGER; i++) {
#if MULTI_PROTOCOL_TYPE_B
		input_mt_slot(drvdata->input_dev, i);
		input_mt_report_slot_state(drvdata->input_dev, MT_TOOL_FINGER, false);
#endif
	}

	input_report_key(drvdata->input_dev, BTN_TOUCH, 0);
#if !MULTI_PROTOCOL_TYPE_B
	input_mt_sync(drvdata->input_dev);
#endif
	input_sync(drvdata->input_dev);
#ifdef TLSC_REPORT_BY_ZTE_ALGO
	if (tpd_cdev->zte_tp_algo) 
		tpd_clean_all_event();
#endif
}

#ifdef TLSC_APK_DEBUG
static unsigned char send_data_flag = 0;
static unsigned char get_data_flag = 0;
static int send_count = 0;
static unsigned char get_data_buf[10] = {0};
static char buf_in[1026] = {0};
static char buf_out[1026] = {0};

void get_data_start(unsigned char *local_buf)
{
    u8 writebuf[4];
    g_tp_drvdata->esdHelperFreeze = 1;
    tlsc6x_set_dd_mode();
    {
        writebuf[0] = 0x9f;
        writebuf[1] = 0x22;
        writebuf[2] = local_buf[2];
        writebuf[3] = local_buf[3];
        tlsc6x_i2c_write(this_client, writebuf, 4);

        writebuf[0] = 0x9f;
        writebuf[1] = 0x20;
        writebuf[2] = 61;
        tlsc6x_i2c_write(this_client, writebuf, 3);

        writebuf[0] = 0x9f;
        writebuf[1] = 0x24;
        writebuf[2] = 0x01;
        tlsc6x_i2c_write(this_client, writebuf, 3);
    }
}

void get_data_stop(void)
{
    u8 writebuf[4];

    writebuf[0] = 0x9f;
    writebuf[1] = 0x22;
    writebuf[2] = 0xff;
    writebuf[3] = 0xff;
    tlsc6x_i2c_write(this_client, writebuf, 4);
    msleep(20);
    g_tp_drvdata->esdHelperFreeze = 0;
    send_data_flag = 0;
    tlsc6x_set_nor_mode();
}

int tssc_get_debug_info(struct i2c_client *i2c_client, char *p_data)
{
    char writebuf[10] = {0};
    short size = 61;
    static unsigned int cnt;
    unsigned char loop, k;
    unsigned char cmd[2];
    unsigned short check, rel_size;
    char buft[128];
    unsigned short *p16_buf = (unsigned short *)buft;
    cmd[0] = 1;

    loop = 0;
    rel_size = size * 2;

    while (loop++ < 2) {
        writebuf[0] = 0x9f;
        writebuf[1] = 0x26;
        tlsc6x_i2c_read(i2c_client, writebuf,  2, buft, rel_size + 2);  //124 2->checksum
        for (k = 0, check = 0; k < size; k++) {
			check += p16_buf[k];
        }
        if (check == p16_buf[size]) {
			p16_buf[size] = 0x5555;
			break;
        } else {
			p16_buf[size] = 0xaaaa;
        }
    }
    buft[124] = (cnt) & 0xff;
    buft[125] = (cnt >> 8) & 0xff;
    buft[126] = (cnt >> 16) & 0xff;
    buft[127] = (cnt >> 24) & 0xff;
    cnt++;

    memcpy(&buf_in[send_count * 128], buft, (sizeof(char) * 128));
    if (send_count++ >= 7) {
        memcpy(buf_out, buf_in, (sizeof(char) * 1024));
        memset(buf_in, 0xff,(sizeof(char) * 1024));
        send_count = 0;
        get_data_flag = 1;
    }

    {
        static unsigned char msk_o;
        unsigned char msk = 0;
        unsigned short *p_point = &p16_buf[61 - 5];
        unsigned char tcnt = buft[61*2 - 2] & 0xf;
        unsigned short x0 = p_point[0];
        unsigned char id0 = (x0 & 0x8000) >> 15;
        unsigned short y0;
        unsigned char id1;
        unsigned short x1;
        unsigned short y1;
        unsigned char mch;
        unsigned char act;

        x0 = x0 & 0x3fff;
        y0 = p_point[1];
        if (x0 > 0 && y0 > 0) {
            msk = 1 << id0;
        }
        x1 = p_point[2];
        id1 = (x1 & 0x8000) >> 15;
        x1 = x1 & 0x3fff;
        y1 = p_point[3];
        if (x1 > 0 && y1 > 0) {
            msk |= 1 << id1;
        }
        mch = msk ^ msk_o;
        if ((3 == mch) && (1 == tcnt)) {
            tcnt = 0;
            msk = 0;
            mch = msk_o;
            x0 = x1 = 0;
        }
        msk_o = msk;
        memset(p_data, 0xff, 18);

        p_data[0] = 0;
        p_data[1] = 0;

#ifdef TLSC_TPD_PROXIMITY
        if (tpd_prox_active) {
            if(p_point[0]&0x4000) {
                p_data[1] = 0xC0;
            }
            else {
                p_data[1] = 0xE0;
            }
        }
#endif

        p_data[2] = tcnt;
        act = 0;
        if (x0 > 0 && y0 > 0) {
            act = (0 == (mch & (0x01 << id0))) ? 0x80 : 0;
        } else {
            id0 = !id1;
            act = 0x40;
        }
        p_data[3] = (act | (x0 >> 8));
        p_data[4] = (x0 & 0xff);
        p_data[5] = (id0 << 4) | (y0 >> 8);
        p_data[6] = (y0 & 0xff);
        p_data[7] = 0x0d;
        p_data[8] = 0x10;

        if (x1 > 0 && y1 > 0) {
            act = (0 == (mch & (0x01 << id1))) ? 0x80 : 0;
        } else {
            id1 = !id0;
            act = 0x40;
        }
        p_data[9] = (act | (x1 >> 8));
        p_data[10] = (x1 & 0xff);
        p_data[11] = (id1 << 4) | (y1 >> 8);
        p_data[12] = (y1 & 0xff);
        p_data[13] = 0x0d;
        p_data[14] = 0x10;
    }
    return 0;
}
#endif

static int tlsc6x_update_data(void)
{
    struct tlsc6x_data *data = i2c_get_clientdata(this_client);
    struct ts_event *event = &data->event;
    u8 buf[20] = { 0 };
    int ret = -1;
    int i;
    u16 x, y;
    u8 ft_pressure, ft_size;
    static char finger_down[TS_MAX_FINGER] = {0};
    u8 id = 0;

#ifdef TLSC_APK_DEBUG
    if (send_data_flag) {
        tssc_get_debug_info(this_client,buf);
    } else {
        ret = tlsc6x_i2c_read(this_client, buf, 1, buf, 18);
        if (ret < 0) {
            tlsc_err("%s read_data i2c_rxdata failed: %d\n", __func__, ret);
            return ret;
        }
    }
#else
    ret = tlsc6x_i2c_read(this_client, buf, 1, buf, 18);
    if (ret < 0) {
        tlsc_err("%s read_data i2c_rxdata failed: %d\n", __func__, ret);
        return ret;
    }
#endif

    memset(event, 0, sizeof(struct ts_event));
    event->touch_point = buf[2] & 0x07;

#ifdef TLSC_TPD_PROXIMITY
	if (tpd_prox_active && (event->touch_point == 0)) {
		if (((buf[1] == 0xc0) || (buf[1] == 0xe0)) && (tpd_prox_old_state != buf[1])) {
			input_report_abs(data->ps_input_dev, ABS_DISTANCE, (buf[1] == 0xc0) ? 0 : 1);
			input_mt_sync(data->ps_input_dev);
			input_sync(data->ps_input_dev);
			tlsc_info("%s proximity report is %d.\n", __func__, (buf[1] == 0xc0) ? 0 : 1);
		}
		tpd_prox_old_state = buf[1];
	}
#endif

	for (i = 0; i < TS_MAX_FINGER; i++) {
		if ((buf[6 * i + 3] & 0xc0) == 0xc0) {
			/*tlsc_info("%s:abnormal value %d -> 0x%x\n", __func__, i, buf[6 * i + 3]);*/
			continue;
		}
		x = (s16) (buf[6 * i + 3] & 0x0F) << 8 | (s16) buf[6 * i + 4];
		y = (s16) (buf[6 * i + 5] & 0x0F) << 8 | (s16) buf[6 * i + 6];
		ft_pressure = buf[6 * i + 7];
		if (ft_pressure > 127) {
			ft_pressure = 127;
		}
		ft_size = (buf[6 * i + 8] >> 4) & 0x0F;
		id = buf[6 * i + 5] >> 4;
		if ((buf[6 * i + 3] & 0x40) == 0x0) {
			if (!finger_down[id]) {
				finger_down[id] = 1;
				tlsc_info("touch down id: %d, coord [%d:%d]\n",id, x, y);
			}
#ifdef TLSC_REPORT_BY_ZTE_ALGO
		if (tpd_cdev->zte_tp_algo) {
			tpd_touch_press(data->input_dev, x, y, id, ft_size, 0);
		} else {
			input_mt_slot(data->input_dev, buf[6 * i + 5] >> 4);
			input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER, true);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, x);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, y);
#ifdef TLSC_REPORT_PRESSURE_EN
			input_report_abs(data->input_dev, ABS_MT_PRESSURE, 15);
#endif
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, ft_size);
			input_report_key(data->input_dev, BTN_TOUCH, 1);		
		}
#else
#if MULTI_PROTOCOL_TYPE_B
			input_mt_slot(data->input_dev, buf[6 * i + 5] >> 4);
			input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER, true);
#else
			input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, buf[6 * i + 5] >> 4);
#endif
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, x);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, y);
#ifdef TLSC_REPORT_PRESSURE_EN
			input_report_abs(data->input_dev, ABS_MT_PRESSURE, 15);
#endif
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, ft_size);
			input_report_key(data->input_dev, BTN_TOUCH, 1);
#if !MULTI_PROTOCOL_TYPE_B
			input_mt_sync(data->input_dev);
#endif
#endif
		} else {
			if (finger_down[id] == 1) {
				finger_down[id] = 0;
				tlsc_info("touch up id: %d, coord [%d:%d]\n",id, x, y);
			}
#ifdef TLSC_REPORT_BY_ZTE_ALGO
			if (tpd_cdev->zte_tp_algo) {
			 	tpd_touch_release(data->input_dev, id);
			} else {
				input_mt_slot(data->input_dev, buf[6 * i + 5] >> 4);
				input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER, false);
			}
#else
#if MULTI_PROTOCOL_TYPE_B
			input_mt_slot(data->input_dev, buf[6 * i + 5] >> 4);
			input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER, false);
#endif
#endif
		}
	}
	if (event->touch_point == 0) {
		tlsc6x_clear_report_data(data);
	}
	input_sync(data->input_dev);

	return 0;

}

static irqreturn_t touch_event_thread_handler(int irq, void *devid)
{

#ifdef CONFIG_TLSC_POINT_REPORT_CHECK
	struct tlsc6x_data *data = i2c_get_clientdata(this_client);
#endif

	tlsc6x_update_data();

#ifdef CONFIG_TLSC_POINT_REPORT_CHECK
	cancel_delayed_work(&data->point_report_check_work);
	queue_delayed_work(data->tlsc_workqueue, &data->point_report_check_work, msecs_to_jiffies(200));
#endif

	return IRQ_HANDLED;
}

void tlsc6x_tpd_reset_force(void)
{
	struct tlsc6x_platform_data *pdata = g_tp_drvdata->platform_data;

	TLSC_FUNC_ENTER();
	gpio_direction_output(pdata->reset_gpio_number, 1);
	usleep_range(10000, 11000);
	gpio_set_value(pdata->reset_gpio_number, 0);
	usleep_range(5000, 5010);
	gpio_set_value(pdata->reset_gpio_number, 1);
	msleep(60);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	tpd_cdev->tp_reset_timer = jiffies;
#endif
}

static void tlsc6x_tpd_reset(void)
{
	TLSC_FUNC_ENTER();
	if (g_tp_drvdata->needKeepRamCode) {
		return;
	}

	tlsc6x_tpd_reset_force();
}

unsigned char real_suspend_flag = 0;

int tlsc6x_do_suspend(void)
{
	int ret = -1;

	TLSC_FUNC_ENTER();
	if (g_tp_drvdata->suspended) {
		tlsc_info("Already in suspend state.\n");
		return 0;
	}
	mutex_lock(&g_tp_drvdata->action_mutex);

#ifdef TLSC_ESD_HELPER_EN
	hrtimer_cancel(&tpd_esd_kthread_timer);
#endif

#ifdef TLSC_TPD_PROXIMITY
	if (tpd_prox_active) {
		real_suspend_flag = 0;
		enable_irq_wake(this_client->irq);
		g_tp_drvdata->suspended = true;
		mutex_unlock(&g_tp_drvdata->action_mutex);
		return 0;
	}
#endif

	tlsc_irq_disable();
	ret = tlsc6x_write_reg(this_client, 0xa5, 0x03);
	if (ret < 0) {
		tlsc_err("tlsc6x error::setup suspend fail!\n");
	}
	real_suspend_flag = 1;
	tlsc6x_clear_report_data(g_tp_drvdata);

	g_tp_drvdata->suspended = true;
	mutex_unlock(&g_tp_drvdata->action_mutex);

	return 0;
}

int tlsc6x_do_resume(void)
{
	TLSC_FUNC_ENTER();
#ifdef TLSC_ESD_HELPER_EN
	hrtimer_start(&tpd_esd_kthread_timer, ktime_set(3, 0), HRTIMER_MODE_REL);
#endif
	queue_work(g_tp_drvdata->tlsc_workqueue, &g_tp_drvdata->resume_work);
	return 0;
}

#if defined(CONFIG_ADF)
static int tlsc6x_adf_suspend(void)
{
	TLSC_FUNC_ENTER();

	return tlsc6x_do_suspend();
}

static int tlsc6x_adf_resume(void)
{
	TLSC_FUNC_ENTER();

	return tlsc6x_do_resume();
}
#elif defined(CONFIG_FB)
static void tlsc6x_ts_suspend(void)
{
	TLSC_FUNC_ENTER();

	tlsc6x_do_suspend();
}

static void tlsc6x_ts_resume(void)
{
	TLSC_FUNC_ENTER();

	tlsc6x_do_resume();
}

#elif defined(CONFIG_HAS_EARLYSUSPEND)
static void tlsc6x_ts_suspend(struct early_suspend *handler)
{
	TLSC_FUNC_ENTER();

	tlsc6x_do_suspend();
}

static void tlsc6x_ts_resume(struct early_suspend *handler)
{
	TLSC_FUNC_ENTER();

	tlsc6x_do_resume();
}
#endif

static void tlsc6x_resume_work(struct work_struct *work)
{
	TLSC_FUNC_ENTER();

	if (g_tp_drvdata->suspended == false) {
		tlsc_info("Already in awake state");
		return;
	}
	mutex_lock(&g_tp_drvdata->action_mutex);

	tlsc6x_tpd_reset();

	if (g_tp_drvdata->needKeepRamCode) {	/* need wakeup cmd in this mode */
		tlsc6x_write_reg(this_client, 0xa5, 0x00);
	}
	tlsc6x_clear_report_data(g_tp_drvdata);
#ifdef TLSC_TPD_PROXIMITY
	tlsc6x_prox_ctrl(tpd_prox_active);
	if (tpd_prox_active && (real_suspend_flag == 0)) {
		disable_irq_wake(this_client->irq);
		g_tp_drvdata->suspended = false;
		mutex_unlock(&g_tp_drvdata->action_mutex);
		return;
	}
#endif

	tlsc_irq_enable();

	real_suspend_flag = 0;
	g_tp_drvdata->suspended = false;

	mutex_unlock(&g_tp_drvdata->action_mutex);
}

#if defined(CONFIG_ADF)
/*
 * touchscreen's suspend and resume state should rely on screen state,
 * as fb_notifier and early_suspend are all disabled on our platform,
 * we can only use adf_event now
 */
static int ts_adf_event_handler(struct notifier_block *nb, unsigned long action, void *data)
{

	struct adf_notifier_event *event = data;
	int adf_event_data;

	if (action != ADF_EVENT_BLANK) {
		return NOTIFY_DONE;
	}
	adf_event_data = *(int *)event->data;
	tlsc_info("receive adf event with adf_event_data=%d", adf_event_data);

	switch (adf_event_data) {
	case DRM_MODE_DPMS_ON:
		tlsc6x_adf_resume();
		break;
	case DRM_MODE_DPMS_OFF:
		tlsc6x_adf_suspend();
		break;
	default:
		tlsc_info("receive adf event with error data, adf_event_data=%d", adf_event_data);
		break;
	}

	return NOTIFY_OK;
}
#elif defined(CONFIG_FB)
/*****************************************************************************
*  Name: fb_notifier_callback
*  Brief:
*  Input:
*  Output:
*  Return:
*****************************************************************************/
static int fb_notifier_callback(struct notifier_block *self, unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;
	struct tlsc6x_data *tlsc6x_data = container_of(self, struct tlsc6x_data, fb_notif);

	if (evdata && evdata->data && event == FB_EVENT_BLANK && tlsc6x_data && tlsc6x_data->client) {
		blank = evdata->data;
		if (*blank == FB_BLANK_UNBLANK) {
			tlsc_info("resume notifier.\n");
			tlsc6x_ts_resume();
		} else if (*blank == FB_BLANK_POWERDOWN) {
			tlsc_info("suspend notifier.\n");
			tlsc6x_ts_suspend();
		}
	}

	return 0;
}

#endif

static int tlsc6x_hw_init(struct tlsc6x_data *drvdata)
{
	struct regulator *reg_vdd;
	struct i2c_client *client = drvdata->client;
	struct tlsc6x_platform_data *pdata = drvdata->platform_data;

	TLSC_FUNC_ENTER();
	if (gpio_request(pdata->irq_gpio_number, NULL) < 0) {
		goto OUT;
	}
	if (gpio_request(pdata->reset_gpio_number, NULL) < 0) {
		goto OUT;
	}
	if (gpio_is_valid(pdata->power_gpio_number)) {
		if (gpio_request(pdata->power_gpio_number, "tp_power_gpio") < 0) {
			tlsc_err("tlsc6x_hw_init: request power gpio fail\n");
		} else {
			gpio_direction_output(pdata->power_gpio_number, 1);
		}
	}
	gpio_direction_output(pdata->reset_gpio_number, 1);
	gpio_direction_input(pdata->irq_gpio_number);

	if (pdata->vdd_name == NULL)
		goto tp_reset;
	reg_vdd = regulator_get(&client->dev, pdata->vdd_name);
	if (!WARN(IS_ERR(reg_vdd), "tlsc6x_hw_init regulator: failed to get %s.\n", pdata->vdd_name)) {
		#ifdef TLSC_VDD2V9_CONFIG
		regulator_set_voltage(reg_vdd, 2900000, 2900000);
		#else
		regulator_set_voltage(reg_vdd, 2800000, 2800000);
		#endif
		if (regulator_enable(reg_vdd)) {
			tlsc_info("tlsc6x_hw_init:regulator_enable return none zero\n");
		}
		if (regulator_is_enabled(reg_vdd) == 0) {
			tlsc_err("tlsc6x_hw_init:regulator_enable fail\n");
		}
		drvdata->reg_vdd = reg_vdd;
	} else {
		drvdata->reg_vdd = NULL;
	}
tp_reset:
	tlsc6x_tpd_reset();
	return 0;
OUT:
	return -EPERM;
}

#ifdef CONFIG_OF
static struct tlsc6x_platform_data *tlsc6x_parse_dt(struct device *dev)
{
	int ret;
	struct tlsc6x_platform_data *pdata;
	struct device_node *np = dev->of_node;

	TLSC_FUNC_ENTER();
	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		tlsc_err("Could not allocate struct tlsc6x_platform_data.\n");
		return NULL;
	}

	pdata->reset_gpio_number = of_get_gpio(np, 0);
	if (pdata->reset_gpio_number < 0) {
		tlsc_err("fail to get reset_gpio_number\n");
		goto fail;
	}

	pdata->irq_gpio_number = of_get_gpio(np, 1);
	if (pdata->irq_gpio_number < 0) {
		tlsc_err("fail to get irq_gpio_number\n");
		goto fail;
	}

	pdata->power_gpio_number = of_get_gpio(np, 2);
	if (pdata->power_gpio_number < 0) {
		tlsc_info("no power_gpio_number %d.\n", pdata->power_gpio_number);
	}

	ret = of_property_read_string(np, "vdd_name", &pdata->vdd_name);
	if (ret) {
		tlsc_err("fail to get vdd_name\n");
	}
	#ifdef TOUCH_VIRTUAL_KEYS
	ret = of_property_read_u32_array(np, "virtualkeys", pdata->virtualkeys, 12);
	if (ret) {
		tlsc_err("fail to get virtualkeys\n");
		/* goto fail; */
	}
	#endif
	ret = of_property_read_u32(np, "TP_MAX_X", &pdata->x_res_max);
	if (ret) {
		tlsc_err("fail to get TP_MAX_X\n");
		goto fail;
	}
	ret = of_property_read_u32(np, "TP_MAX_Y", &pdata->y_res_max);
	if (ret) {
		tlsc_err("fail to get TP_MAX_Y\n");
		goto fail;
	}

	return pdata;
fail:
	kfree(pdata);
	return NULL;
}
#endif
/* file interface - write*/
int tlsc6x_fif_write(char *fname, u8 *pdata, u16 len)
{
	tpd_copy_to_tp_firmware_data(pdata);
	return 0;
}
#if (defined TPD_AUTO_UPGRADE_PATH) || (defined TLSC_APK_DEBUG)
extern int tlsx6x_update_running_cfg(u16 *ptcfg);
extern int tlsx6x_update_burn_cfg(u16 *ptcfg);
extern int tlsc6x_load_ext_binlib(u8 *pcode, u16 len);
extern int tlsc6x_update_f_combboot(u8 *pdata, u16 len);
int auto_upd_busy = 0;

/* 0:success */
/* 1: no file OR open fail */
/* 2: wrong file size OR read error */
/* -1:op-fial */
int tlsc6x_proc_cfg_update(u8 *fw_name, int behave)
{
	int ret = 1;
	u8 *pbt_buf = NULL;
	u32 fileSize;
	struct ztp_device *cdev = tpd_cdev;

	TLSC_FUNC_ENTER();
	if (cdev->tp_firmware == NULL || cdev->tp_firmware->data == NULL) {
		tlsc_err("cdev->tp_firmware is NULL");
		return -EIO;
	}

	pbt_buf =  (u8 *)cdev->tp_firmware->data;;
	fileSize = cdev->tp_firmware->size;
	auto_upd_busy = 1;
	tlsc_irq_disable();
	msleep(100);
#ifdef CONFIG_PM_WAKELOCKS
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
	__pm_wakeup_event(tlsc6x_wakelock, 2000);
#else
	__pm_wakeup_event(&tlsc6x_wakelock, 2000);
#endif
#else
	wake_lock_timeout(&tlsc6x_wakelock, msecs_to_jiffies(2000));
#endif

	if (behave == 0) {
		if (fileSize == 204) {
			ret = tlsx6x_update_running_cfg((u16 *) pbt_buf);
		} else if (fileSize > 0x400) {
			tlsc6x_load_ext_binlib((u8 *) pbt_buf, (u16) fileSize);
		}
	} else if (behave == 1) {
		if (fileSize == 204) {
			ret = tlsx6x_update_burn_cfg((u16 *) pbt_buf);
		} else if (fileSize > 204) {
			ret = tlsc6x_update_f_combboot((u8 *) pbt_buf, (u16) fileSize);
		}
		tlsc6x_tpd_reset();
	}
	tlsc_irq_enable();
	auto_upd_busy = 0;

	return ret;
}

#endif

#ifdef TLSC_APK_DEBUG
#define PROC_OUT_BUF_SIZE 256
unsigned char proc_out_len;
unsigned char proc_out_buf[PROC_OUT_BUF_SIZE];

unsigned char debug_type;
unsigned char iic_reg[2];
unsigned char sync_flag_addr[3];
unsigned char sync_buf_addr[2];
unsigned char reg_len;

static struct proc_dir_entry *tlsc6x_proc_entry = NULL;

static int debug_read(char *writebuf, int writelen, char *readbuf, int readlen)
{
	int ret = 0;

	mutex_lock(&i2c_rw_access);
	tlsc6x_set_dd_mode_sub();

	ret = tlsc6x_i2c_read_sub(this_client, writebuf, writelen, readbuf, readlen);

	tlsc6x_set_nor_mode_sub();
	mutex_unlock(&i2c_rw_access);
	if (ret > 0) {
		ret = readlen;
	}
	return ret;
}

static int debug_write(char *writebuf, int writelen)
{
	int ret = 0;

	mutex_lock(&i2c_rw_access);
	tlsc6x_set_dd_mode_sub();

	ret = tlsc6x_i2c_write_sub(this_client, writebuf, writelen);

	tlsc6x_set_nor_mode_sub();
	mutex_unlock(&i2c_rw_access);
	if (ret > 0) {
		ret = writelen;
	}
	return ret;
}

static int debug_read_sync(char *writebuf, int writelen, char *readbuf, int readlen)
{
	int ret = 0;

	mutex_lock(&i2c_rw_access);
	tlsc6x_set_dd_mode_sub();
	sync_flag_addr[2] = 1;
	ret = tlsc6x_i2c_write_sub(this_client, sync_flag_addr, 3);
	do {
		ret = tlsc6x_i2c_read_sub(this_client, sync_flag_addr, 2, &sync_flag_addr[2], 1);
		if (ret < 0) {
			return ret;
		}
	} while (sync_flag_addr[2] == 1);
	if (ret >= 0) {
		/* read data */
		ret = tlsc6x_i2c_read_sub(this_client, sync_buf_addr, 2, readbuf, readlen);
	}

	tlsc6x_set_nor_mode_sub();
	mutex_unlock(&i2c_rw_access);
	if (ret > 0) {
		ret = readlen;
	}
	return ret;
}

static ssize_t tlsc6x_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	int ret;
	int buflen = len;
	unsigned char *local_buf;
	if (buflen > 4100) {
		return -EFAULT;
	}
	local_buf = kmalloc(buflen+1, GFP_KERNEL);
	if (local_buf == NULL) {
		tlsc_err("%s,Can not malloc the buf!\n", __func__);
		return -EFAULT;
	}
	if (copy_from_user(local_buf, buff, buflen)) {
		tlsc_err("%s:copy from user error\n", __func__);
		kfree(local_buf);
		return -EFAULT;
	}
	ret = 0;
	debug_type = local_buf[0];
	/* format:cmd+para+data0+data1+data2... */
	switch (local_buf[0]) {
	case 0:		/* cfg version */
		proc_out_len = 4;
		proc_out_buf[0] = g_tlsc6x_cfg_ver;
		proc_out_buf[1] = g_tlsc6x_cfg_ver >> 8;
		proc_out_buf[2] = g_tlsc6x_cfg_ver >> 16;
		proc_out_buf[3] = g_tlsc6x_cfg_ver >> 24;
		break;
	case 1:
		local_buf[buflen] = '\0';
		if (tlsc6x_proc_cfg_update(&local_buf[2], 0) < 0) {
			len = -EIO;
		}
		break;
	case 2:
		local_buf[buflen] = '\0';
		if (tlsc6x_proc_cfg_update(&local_buf[2], 1) < 0) {
			len = -EIO;
		}
		break;
	case 3:
		ret = debug_write(&local_buf[1], len - 1);
		break;
	case 4:		/* read */
		reg_len = local_buf[1];
		iic_reg[0] = local_buf[2];
		iic_reg[1] = local_buf[3];
		break;
	case 5:		/* read with sync */
		ret = debug_write(&local_buf[1], 4);	/* write size */
		if (ret >= 0) {
			ret = debug_write(&local_buf[5], 4);	/* write addr */
		}
		sync_flag_addr[0] = local_buf[9];
		sync_flag_addr[1] = local_buf[10];
		sync_buf_addr[0] = local_buf[11];
		sync_buf_addr[1] = local_buf[12];
		break;
	case 8: // Force reset ic
		tlsc6x_tpd_reset_force();
		break;
	case 9: // Force reset ic
		ret=tlsc6x_rawdata_test_3535allch();
		break;
	case 14:	/* e, esd control */
		g_tp_drvdata->esdHelperFreeze = (int)local_buf[1];
		break;
	case 15:	
		memset(get_data_buf, 0x00, (sizeof(char) * 8));
		memcpy(get_data_buf, local_buf, (sizeof(char) * 8));
		get_data_start(get_data_buf);
		send_data_flag = 1;
		send_count = 0;
		get_data_flag = 0;
		break;
	case 16:
		get_data_stop();
		break;
	default:
		break;
	}
	if (ret < 0) {
		len = ret;
	}
	kfree(local_buf);
	return len;
}

static ssize_t tlsc6x_proc_read(struct file *filp, char __user *page, size_t len, loff_t *pos)
{
	int ret = 0;

	if (*pos != 0) {
		return 0;
	}
	switch (debug_type) {
	case 0:		/* version information */
		proc_out_len = 4;
		proc_out_buf[0] = g_tlsc6x_cfg_ver;
		proc_out_buf[1] = g_tlsc6x_cfg_ver >> 8;
		proc_out_buf[2] = g_tlsc6x_cfg_ver >> 16;
		proc_out_buf[3] = g_tlsc6x_cfg_ver >> 24;
		if (copy_to_user(page, proc_out_buf, proc_out_len)) {
			ret = -EFAULT;
		} else {
			ret = proc_out_len;
		}
		break;
	case 1:
		break;
	case 2:
		break;
	case 3:
		break;
	case 4:
		if (len > PROC_OUT_BUF_SIZE) {
			tlsc_err("%s len(%d) is invalid\n", __func__, len);
			return -EINVAL;
		}
		ret = debug_read(iic_reg, reg_len, proc_out_buf, len);
		if (ret > 0) {
			len = ret;
			if (copy_to_user(page, proc_out_buf, len)) {
				ret = -EFAULT;
			} else {
				ret = len;
			}
		}
		break;
	case 5:
		if (len > PROC_OUT_BUF_SIZE) {
			tlsc_err("%s len(%d) is invalid\n", __func__, len);
			return -EINVAL;
		}
		ret = debug_read_sync(iic_reg, reg_len, proc_out_buf, len);
		if (ret > 0) {
			len = ret;
			if (copy_to_user(page, proc_out_buf, len)) {
				ret = -EFAULT;
			} else {
				ret = len;
			}
		}
		break;
	case 9:
		if (proc_out_buf > 0){
			if (copy_to_user(page, proc_out_buf, len)) {
				ret = -EFAULT;
			} else {
				ret = proc_out_len;
			}
		}
		break;
	case 15:
		if (1 == get_data_flag) {
			get_data_flag = 0;
			if (copy_to_user(page, buf_out, len)) {
				ret = -EFAULT;
			} else {
				ret = len;
			}
		} else {
			ret = -EFAULT;
		}
		break;
	default:
		break;
	}
	if (ret > 0) {
		*pos += ret;
	}

	return ret;
}

static struct file_operations tlsc6x_proc_ops = {
	.owner = THIS_MODULE,
	.read = tlsc6x_proc_read,
	.write = tlsc6x_proc_write,
};

void tlsc6x_release_apk_debug_channel(void)
{
	if (tlsc6x_proc_entry) {
		remove_proc_entry("tlsc6x-debug", NULL);
	}
}

int tlsc6x_create_apk_debug_channel(struct i2c_client *client)
{
	tlsc6x_proc_entry = proc_create("tlsc6x-debug", 0644, NULL, &tlsc6x_proc_ops);

	if (tlsc6x_proc_entry == NULL) {
		tlsc_err("Couldn't create proc entry!\n");
		return -ENOMEM;
	}
	tlsc_info("Create proc entry success!\n");

	return 0;
}
#endif

#ifdef TLSC_ESD_HELPER_EN
unsigned char g_tlsc6x_esdtar = 0;
unsigned char g_tlsc6x_esdadr = 0xa3;
static int esd_check_work(void)
{
	int ret = -1;
	u8 test_val = 0;

	TLSC_FUNC_ENTER();

	if (g_tp_drvdata->esdHelperFreeze) {
		return 1;
	}

	ret = tlsc6x_read_reg(this_client, g_tlsc6x_esdadr, &test_val);

	if (ret < 0) {		/* maybe confused by some noise,so retry is make sense. */
		usleep_range(10000, 11000);
		tlsc6x_read_reg(this_client, g_tlsc6x_esdadr, &test_val);
		ret = tlsc6x_read_reg(this_client, g_tlsc6x_esdadr, &test_val);
	}

	if ((ret >= 0) && (g_tlsc6x_esdtar != 0)) {
		if (g_tlsc6x_esdtar != test_val) {
			ret = -EIO;
		}
	}
	if (ret < 0) {
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		tpd_zlog_record_notify(TP_ESD_CHECK_ERROR_NO);
#endif
		tlsc6x_tpd_reset_force();

		tlsc6x_clear_report_data(g_tp_drvdata);

		if (tlsc6x_read_reg(this_client, g_tlsc6x_esdadr, &g_tlsc6x_esdtar) < 0) {
			g_tlsc6x_esdtar = 0x00;
		}

#ifdef TLSC_TPD_PROXIMITY
		tlsc6x_prox_ctrl(tpd_prox_active);
#endif
	}
#ifdef TLSC_TPD_PROXIMITY
	if (tpd_prox_active) {
		/* ps-function enabled, but tp lost this cmd  */
		if (tlsc6x_read_reg(this_client, 0xb0, &test_val) >= 0) {
			if (test_val != 0x01) {
				tlsc6x_prox_ctrl(tpd_prox_active);
			}
		}
	}
#endif

	return ret;
}

static int esd_checker_handler(void *unused)
{
	ktime_t ktime;

	if (g_mccode == 0) {
		g_tlsc6x_esdadr = 0xe0;
	} else {
		g_tlsc6x_esdadr = 0xa3;
	}
	if (tlsc6x_read_reg(this_client, g_tlsc6x_esdadr, &g_tlsc6x_esdtar) < 0) {
		g_tlsc6x_esdtar = 0x00;
	}

	do {
		wait_event_interruptible(tpd_esd_waiter, tpd_esd_flag != 0);
		tpd_esd_flag = 0;

		ktime = ktime_set(4, 0);
		hrtimer_start(&tpd_esd_kthread_timer, ktime, HRTIMER_MODE_REL);

		if (g_tp_drvdata->esdHelperFreeze) {
			continue;
		}
#if (defined TPD_AUTO_UPGRADE_PATH) || (defined TLSC_APK_DEBUG)
		if (auto_upd_busy) {
			continue;
		}
#endif
		esd_check_work();

	} while (!kthread_should_stop());

	return 0;
}

enum hrtimer_restart tpd_esd_kthread_hrtimer_func(struct hrtimer *timer)
{
	tpd_esd_flag = 1;
	wake_up_interruptible(&tpd_esd_waiter);

	return HRTIMER_NORESTART;
}
#endif

#ifdef TLSC_TP_PROC_SELF_TEST
int tlsc6x_chip_self_test(void)
{
	int ret = 0;

	TLSC_FUNC_ENTER();
	auto_upd_busy = 1;
	g_tp_drvdata->esdHelperFreeze = 1;
	tlsc_irq_disable();
#ifdef CONFIG_PM_WAKELOCKS
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
	__pm_wakeup_event(tlsc6x_wakelock, 2000);
#else
	__pm_wakeup_event(&tlsc6x_wakelock, 2000);
#endif
#else
	wake_lock_timeout(&tlsc6x_wakelock, msecs_to_jiffies(2000));
#endif
	ret = tlsc6x_chip_self_test_sub();

	auto_upd_busy = 0;
	g_tp_drvdata->esdHelperFreeze = 0;
	tlsc_irq_enable();

	return ret;
}

#endif

#ifdef TLSC_TPD_PROXIMITY
int tlsc6x_set_suspend_in_proximity(void)
{
	int ret = -1;

	TLSC_FUNC_ENTER();

	disable_irq_wake(this_client->irq);
	tlsc_irq_disable();
	ret = tlsc6x_write_reg(this_client, 0xa5, 0x03);
	if (ret < 0) {
		tlsc_err("tlsc6x error::setup suspend fail!\n");
	}
	real_suspend_flag = 1;
	tlsc6x_clear_report_data(g_tp_drvdata);

	return 0;
}

static int tlsc6x_prox_ctrl(int enable)
{
	TLSC_FUNC_ENTER();
	/* default is not far, not near */
	tpd_prox_old_state = 0xf0;
	if (enable == 1) {
		tpd_prox_active = 1;
		tlsc6x_write_reg(this_client, 0xb0, 0x01);
	} else if (enable == 0) {
		tpd_prox_active = 0;
		tlsc6x_write_reg(this_client, 0xb0, 0x00);
	}

	tlsc_info("%s: tpd_prox_active is %d\n", __func__, tpd_prox_active);
	return 1;
}

static ssize_t tlsc6x_prox_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "ps enable %d\n", tpd_prox_active);
}

static ssize_t tlsc6x_prox_enable_store(struct device *dev, struct device_attribute *attr, const char *buf,
					size_t count)
{
	unsigned int enable;
	int ret = 0;

	TLSC_FUNC_ENTER();
	ret = kstrtouint(buf, 0, &enable);
	if (ret)
		return -EINVAL;
	enable = (enable > 0) ? 1 : 0;

	mutex_lock(&g_tp_drvdata->action_mutex);
	if (!real_suspend_flag) {
		tlsc6x_prox_ctrl(enable);
		if (!enable && g_tp_drvdata->suspended) {
			tlsc6x_set_suspend_in_proximity();
		}
	} else {
		tpd_prox_active = enable;
	}
	mutex_unlock(&g_tp_drvdata->action_mutex);

	return count;

}

static DEVICE_ATTR(proximity, 0664, tlsc6x_prox_enable_show, tlsc6x_prox_enable_store);

/* default cmd interface(refer to sensor HAL):"/sys/class/sprd-tpd/device/proximity" */
static void tlsc6x_prox_cmd_path_init(void)
{
	sprd_tpd_class = class_create(THIS_MODULE, "sprd-tpd");
	if (IS_ERR(sprd_tpd_class)) {
		tlsc_err("tlsc6x error::create sprd-tpd fail\n");
	} else {
		sprd_ps_cmd_dev = device_create(sprd_tpd_class, NULL, 0, NULL, "device");
		if (IS_ERR(sprd_ps_cmd_dev)) {
			tlsc_err("tlsc6x error::create ges&ps cmd-io fail\n");
		} else {
			/* sys/class/sprd-tpd/device/proximity */
			if (device_create_file(sprd_ps_cmd_dev, &dev_attr_proximity) < 0) {
				tlsc_err("tlsc6x error::create device file fail(%s)!\n", dev_attr_proximity.attr.name);
			}
		}
	}

}

static ssize_t tpd_psensor_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 64, "%d\n", tpd_prox_active);
}

static ssize_t tpd_psensor_enable_store(struct device *dev, struct device_attribute *attr, const char *buf,
					size_t count)
{
	unsigned int enable;
	int ret = 0;

	TLSC_FUNC_ENTER();

	ret = kstrtouint(buf, 0, &enable);
	if (ret)
		return -EINVAL;

	enable = (enable > 0) ? 1 : 0;
	mutex_lock(&g_tp_drvdata->action_mutex);
	if (!real_suspend_flag) {
		tlsc6x_prox_ctrl(enable);
		if (!enable && g_tp_drvdata->suspended) {
			tlsc6x_set_suspend_in_proximity();
		}
	} else {
		tpd_prox_active = enable;
	}
	mutex_unlock(&g_tp_drvdata->action_mutex);

	return count;

}

static ssize_t tpd_psensor_flush_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 64, "%d\n", tpd_prox_active);
}

static ssize_t tpd_psensor_flush_store(struct device *dev, struct device_attribute *attr, const char *buf,
					size_t count)
{
	struct tlsc6x_data *data = i2c_get_clientdata(this_client);

	input_report_abs(data->ps_input_dev, ABS_DISTANCE, -1);
	input_mt_sync(data->ps_input_dev);
	input_sync(data->ps_input_dev);

	return count;

}

static DEVICE_ATTR(enable, 0644, tpd_psensor_enable_show, tpd_psensor_enable_store);
static DEVICE_ATTR(flush, 0644, tpd_psensor_flush_show, tpd_psensor_flush_store);

static struct attribute *tpd_attributes[] = {
	&dev_attr_enable.attr,
	&dev_attr_flush.attr,
	NULL
};

static struct attribute_group tpd_attribute_group = {
	.attrs = tpd_attributes
};
#endif
static int tlsc6x_request_irq_work(void)
{
	int ret = 0;

	this_client->irq = gpio_to_irq(g_tp_drvdata->platform_data->irq_gpio_number);
	tlsc_info("The irq node num is %d", this_client->irq);

	ret = request_threaded_irq(this_client->irq,
				   NULL, touch_event_thread_handler,
				   IRQF_TRIGGER_FALLING | IRQF_ONESHOT | IRQF_NO_SUSPEND,
				   "tlsc6x_tpd_irq", NULL);
	if (ret < 0) {
		tlsc_err("Request irq thread error!");
		return  ret;
	}

	return ret;
}

static void tlsc_fw_upgrade(struct work_struct *work)
{
	TLSC_FUNC_ENTER();
	tlsc6x_do_update_ifneed();
}

#if defined (HUB_TP_PS_ENABLE) && (HUB_TP_PS_ENABLE == 1)
static struct class ps_sensor_class = {
	.name = "tp_ps",
	.owner = THIS_MODULE,
};

static ssize_t delay_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	return snprintf(buf, 8, "%d\n", 200);
}

static ssize_t delay_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	return count;
}

static CLASS_ATTR_RW(delay);


static ssize_t enable_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	return snprintf(buf, 8, "%d\n", tpd_prox_active);
}

static ssize_t enable_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	unsigned int enable;
	int ret = 0;
	int handle;
	TLSC_FUNC_ENTER();

	ret = sscanf(buf, "%d %d\n", &handle, &enable);

	if (ret != 2) {
		tlsc_err("%s: sscanf tp_ps enable data error!!! ret = %d \n", __func__, ret);
		return -EINVAL;
	}

	enable = (enable > 0) ? 1 : 0;
	mutex_lock(&g_tp_drvdata->action_mutex);
	if (!real_suspend_flag) {
		tlsc6x_prox_ctrl(enable);
		if (!enable && g_tp_drvdata->suspended) {
			tlsc6x_set_suspend_in_proximity();
		}
	} else {
		tpd_prox_active = enable;
	}
	mutex_unlock(&g_tp_drvdata->action_mutex);

	return count;
}

static CLASS_ATTR_RW(enable);

static ssize_t flush_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	return snprintf(buf, 64, "%d\n", tpd_prox_active);
}

static ssize_t flush_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	struct tlsc6x_data *data = i2c_get_clientdata(this_client);
	static int flush_count = 0;
	tlsc_err("%s:flush_store!!!\n", __func__);
	if (flush_count % 2 == 0) {
		input_report_abs(data->ps_input_dev, ABS_DISTANCE, -1);
		flush_count = 1;
	} else {
		input_report_abs(data->ps_input_dev, ABS_DISTANCE, -2);
		flush_count = 0;
	}
	input_mt_sync(data->ps_input_dev);
	input_sync(data->ps_input_dev);
	return count;
}

static CLASS_ATTR_RW(flush);

static ssize_t batch_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	return count;
}

static CLASS_ATTR_WO(batch);
#endif

#ifdef CONFIG_TLSC_POINT_REPORT_CHECK
static void tlsc_point_report_check(struct work_struct *work)
{

	struct tlsc6x_data *data = NULL;
	TLSC_FUNC_ENTER();
	data = container_of(work, struct tlsc6x_data, point_report_check_work.work);

	tlsc6x_clear_report_data(data);
}
#endif

static int tlsc6x_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = -1;
	int err = 0;
	struct input_dev *input_dev;
	struct tlsc6x_platform_data *pdata = NULL;

	TLSC_FUNC_ENTER();
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto exit_alloc_platform_data_failed;
	}
#ifdef CONFIG_OF		/* NOTE:THIS IS MUST!!! */
	if (client->dev.of_node) {
		pdata = tlsc6x_parse_dt(&client->dev);
		if (pdata) {
			client->dev.platform_data = pdata;
		}
	}
#endif

	if (pdata == NULL) {
		err = -ENOMEM;
		tlsc_err("%s: no platform data!!!\n", __func__);
		goto exit_alloc_platform_data_failed;
	}

	g_tp_drvdata = kzalloc(sizeof(*g_tp_drvdata), GFP_KERNEL);	/* auto clear */
	if (!g_tp_drvdata) {
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}

	this_client = client;
	g_tp_drvdata->client = client;
	g_tp_drvdata->platform_data = pdata;

	err = tlsc6x_hw_init(g_tp_drvdata);
	if (err < 0) {
		goto exit_gpio_request_failed;
	}

	i2c_set_clientdata(client, g_tp_drvdata);

	/* #ifdef CONFIG_I2C_SPRD */
	/* sprd_i2c_ctl_chg_clk(client->adapter->nr, 400000); */
	/* #endif */
	g_is_telink_comp = tlsc6x_tp_dect(client);

	g_tp_drvdata->needKeepRamCode = g_needKeepRamCode;

	if (g_is_telink_comp) {
		tlsc6x_tpd_reset();
	} else {
		tlsc_err("tlsc6x:%s, no tlsc6x!\n", __func__);
		err = -ENODEV;
		goto exit_chip_check_failed;
	}

	input_dev = input_allocate_device();
	if (!input_dev) {
		err = -ENOMEM;
		tlsc_err("tlsc6x error::failed to allocate input device\n");
		goto exit_input_dev_alloc_failed;
	}
	g_tp_drvdata->input_dev = input_dev;

	__set_bit(ABS_MT_TOUCH_MAJOR, input_dev->absbit);
	__set_bit(ABS_MT_POSITION_X, input_dev->absbit);
	__set_bit(ABS_MT_POSITION_Y, input_dev->absbit);
	__set_bit(ABS_MT_WIDTH_MAJOR, input_dev->absbit);
	__set_bit(KEY_MENU, input_dev->keybit);
	__set_bit(KEY_BACK, input_dev->keybit);
	__set_bit(KEY_HOMEPAGE, input_dev->keybit);
	__set_bit(BTN_TOUCH, input_dev->keybit);
	__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);

#if MULTI_PROTOCOL_TYPE_B
	input_mt_init_slots(input_dev, TS_MAX_FINGER, 0);
#else
	input_set_abs_params(input_dev, ABS_MT_TRACKING_ID, 0, 255, 0, 0);
#endif
	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0, pdata->x_res_max, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, pdata->y_res_max, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, 15, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR, 0, 15, 0, 0);
#ifdef TLSC_REPORT_PRESSURE_EN
	input_set_abs_params(input_dev, ABS_MT_PRESSURE, 0, 127, 0, 0);
#endif
	input_set_abs_params(input_dev, ABS_DISTANCE, -1, 1, 0, 0);	/* give this capability aways */
	set_bit(EV_ABS, input_dev->evbit);
	set_bit(EV_KEY, input_dev->evbit);

	input_dev->name = "tlsc6x_touch";
	err = input_register_device(input_dev);
	if (err) {
		tlsc_err("tlsc6x error::failed to register input device: %s\n", dev_name(&client->dev));
		goto exit_input_register_device_failed;
	}

#ifdef TLSC_TPD_PROXIMITY
	tlsc6x_prox_cmd_path_init();
	g_tp_drvdata->ps_input_dev = input_allocate_device();
	if (!g_tp_drvdata->ps_input_dev) {
		err = -ENOMEM;
		tlsc_err("tlsc6x error::failed to allocate ps-input device\n");
		goto exit_unregister_device;
	}
	g_tp_drvdata->ps_input_dev->name = "proximity_tp";

#if defined (HUB_TP_PS_ENABLE) && (HUB_TP_PS_ENABLE == 1)
	/*add class sysfs for tp_ps*/
	err = class_register(&ps_sensor_class);
	if (err < 0) {
		tlsc_err("%s,Create fsys class failed (%d)\n", err);
		goto err_class_creat;
	}

	err = class_create_file(&ps_sensor_class, &class_attr_delay);
	if (err < 0) {
		tlsc_err("%s, Create delay file failed (%d)\n", err);
		goto exit_unregister_class;
	}

	err = class_create_file(&ps_sensor_class, &class_attr_enable);
	if (err < 0) {
		tlsc_err("%s, Create enable file failed (%d)\n", err);
		goto exit_unregister_class;
	}

	err = class_create_file(&ps_sensor_class, &class_attr_flush);
	if (err < 0) {
		tlsc_err("%s, Create flush file failed (%d)\n", err);
		goto exit_unregister_class;
	}

	err = class_create_file(&ps_sensor_class, &class_attr_batch);
	if (err < 0) {
		tlsc_err("%s, Create batch file failed (%d)\n", err);
		goto exit_unregister_class;
	}
#endif
	set_bit(EV_ABS, g_tp_drvdata->ps_input_dev->evbit);
	input_set_capability(g_tp_drvdata->ps_input_dev, EV_ABS, ABS_DISTANCE);
	input_set_abs_params(g_tp_drvdata->ps_input_dev, ABS_DISTANCE, -2, 1, 0, 0);
	err = input_register_device(g_tp_drvdata->ps_input_dev);
	if (err) {
		tlsc_err("tlsc6x error::failed to register ps-input device: %s\n", dev_name(&client->dev));
		goto exit_register_ps_device_failed;
	}
	err = sysfs_create_group(&g_tp_drvdata->ps_input_dev->dev.kobj, &tpd_attribute_group);
	if (err) {
		tlsc_err("input create group failed.\n");
		goto exit_sysfs_create_group_failed;
	}
#endif

#ifdef TOUCH_VIRTUAL_KEYS
	tlsc6x_virtual_keys_init();
#endif

	INIT_WORK(&g_tp_drvdata->resume_work, tlsc6x_resume_work);
	INIT_DELAYED_WORK(&g_tp_drvdata->fw_upgrade_work, tlsc_fw_upgrade);
	g_tp_drvdata->tlsc_workqueue = create_singlethread_workqueue("tlsc6x_work");
	if (!g_tp_drvdata->tlsc_workqueue) {
		err = -ESRCH;
		goto exit_create_workqueue_failed;
	}

	mutex_init(&g_tp_drvdata->action_mutex);
#ifdef CONFIG_PM_WAKELOCKS
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
	tlsc6x_wakelock = wakeup_source_register(NULL, "tlsc6x_wakelock");
#else
	wakeup_source_init(&tlsc6x_wakelock, "tlsc6x_wakelock");
#endif
#else
	wake_lock_init(&tlsc6x_wakelock, WAKE_LOCK_SUSPEND, "tlsc6x_wakelock");
#endif

	err = tlsc6x_request_irq_work();
	if (err < 0) {
		tlsc_err("tlsc6x error::request irq failed %d\n", err);
		goto exit_irq_request_failed;
	}
#if defined(CONFIG_ADF)
	g_tp_drvdata->fb_notif.notifier_call = ts_adf_event_handler;
	g_tp_drvdata->fb_notif.priority = 1000;
	ret = adf_register_client(&g_tp_drvdata->fb_notif);
	if (ret) {
		tlsc_err("tlsc6x error::unable to register fb_notifier: %d", ret);
	}
#elif defined(CONFIG_FB)
	g_tp_drvdata->fb_notif.notifier_call = fb_notifier_callback;
	ret = fb_register_client(&g_tp_drvdata->fb_notif);
	if (ret) {
		tlsc_err("tlsc6x error::Unable to register fb_notifier: %d", ret);
	}
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	g_tp_drvdata->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	g_tp_drvdata->early_suspend.suspend = tlsc6x_ts_suspend;
	g_tp_drvdata->early_suspend.resume = tlsc6x_ts_resume;
	register_early_suspend(&g_tp_drvdata->early_suspend);
#endif

#ifdef TLSC_APK_DEBUG
	tlsc6x_create_apk_debug_channel(client);
#endif

#ifdef TLSC_ESD_HELPER_EN
	{			/* esd issue: i2c monitor thread */
		ktime_t ktime = ktime_set(30, 0);

		hrtimer_init(&tpd_esd_kthread_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		tpd_esd_kthread_timer.function = tpd_esd_kthread_hrtimer_func;
		hrtimer_start(&tpd_esd_kthread_timer, ktime, HRTIMER_MODE_REL);
		kthread_run(esd_checker_handler, 0, "tlsc6x_esd_helper");
	}
#endif

#ifdef CONFIG_TLSC_POINT_REPORT_CHECK
	INIT_DELAYED_WORK(&g_tp_drvdata->point_report_check_work, tlsc_point_report_check);
#endif

	tlsc6x_get_tp_vendor_info();
	tlsc6x_tpd_register_fw_class();
	if (g_tlsc6x_cfg_ver) {
		queue_delayed_work(g_tp_drvdata->tlsc_workqueue,
			&g_tp_drvdata->fw_upgrade_work, msecs_to_jiffies(100));
	} else {
		tlsc_err("get cfg-ver fail not fw upgrade\n");
	}
	tpd_cdev->TP_have_registered = true;
	tpd_cdev->tp_chip_id = TS_CHIP_TLSC;
	tlsc_info(" tlsc6x_probe success!");
	return 0;

exit_irq_request_failed:
	destroy_workqueue(g_tp_drvdata->tlsc_workqueue);
	mutex_destroy(&g_tp_drvdata->action_mutex);	
exit_create_workqueue_failed:
#ifdef TLSC_TPD_PROXIMITY
exit_sysfs_create_group_failed:
	input_unregister_device(g_tp_drvdata->ps_input_dev);
exit_register_ps_device_failed:
#if defined (HUB_TP_PS_ENABLE) && (HUB_TP_PS_ENABLE == 1)
exit_unregister_class:
	dev_info(&client->dev, "unregister tp_ps_sensor_class.\n");
	class_unregister(&ps_sensor_class);
err_class_creat:
#endif
	input_free_device(g_tp_drvdata->ps_input_dev);
exit_unregister_device:
#endif
	input_unregister_device(input_dev);
exit_input_register_device_failed:
	input_free_device(input_dev);
exit_input_dev_alloc_failed:
exit_chip_check_failed:
	if (g_tp_drvdata->reg_vdd) {
		regulator_disable(g_tp_drvdata->reg_vdd);
		regulator_put(g_tp_drvdata->reg_vdd);
	}
	gpio_free(pdata->irq_gpio_number);
	gpio_free(pdata->reset_gpio_number);
	if (gpio_is_valid(pdata->power_gpio_number))
		gpio_free(pdata->power_gpio_number);
exit_gpio_request_failed:
	kfree(g_tp_drvdata);
exit_alloc_data_failed:
	if (pdata != NULL) {
		kfree(pdata);
	}
	g_tp_drvdata = NULL;
	i2c_set_clientdata(client, g_tp_drvdata);
exit_alloc_platform_data_failed:
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	if (tpd_cdev->tp_chip_id == TS_CHIP_TLSC)
		tpd_cdev->ztp_probe_fail_chip_id = TS_CHIP_TLSC;
#endif
	return err;
}

static int tlsc6x_remove(struct i2c_client *client)
{
	struct tlsc6x_data *drvdata = i2c_get_clientdata(client);

	TLSC_FUNC_ENTER();

#ifdef TLSC_APK_DEBUG
	tlsc6x_release_apk_debug_channel();
#endif

#ifdef TLSC_ESD_HELPER_EN
	hrtimer_cancel(&tpd_esd_kthread_timer);
#endif

	if (drvdata == NULL) {
		return 0;
	}
#if defined(CONFIG_ADF)
	adf_unregister_client(&g_tp_drvdata->fb_notif);
#elif defined(CONFIG_FB)
	fb_unregister_client(&g_tp_drvdata->fb_notif);
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&drvdata->early_suspend);
#endif

	free_irq(client->irq, drvdata);
	input_unregister_device(drvdata->input_dev);
	input_free_device(drvdata->input_dev);
	mutex_destroy(&drvdata->action_mutex);

	cancel_work_sync(&drvdata->resume_work);
	cancel_delayed_work(&drvdata->fw_upgrade_work);
	destroy_workqueue(drvdata->tlsc_workqueue);

	if (drvdata->reg_vdd) {
		regulator_disable(drvdata->reg_vdd);
		regulator_put(drvdata->reg_vdd);
	}
	kfree(drvdata);
	drvdata = NULL;
	i2c_set_clientdata(client, drvdata);

	return 0;
}

static const struct i2c_device_id tlsc6x_id[] = {
	{TS_NAME, 0}, {}
};

MODULE_DEVICE_TABLE(i2c, tlsc6x_id);

static const struct of_device_id tlsc6x_of_match[] = {
	{.compatible = "tlsc6x,tlsc6x_ts",},
	{}
};

MODULE_DEVICE_TABLE(of, tlsc6x_of_match);
static struct i2c_driver tlsc6x_driver = {
	.probe = tlsc6x_probe,
	.remove = tlsc6x_remove,
	.id_table = tlsc6x_id,
	.driver = {
		   .name = TS_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = tlsc6x_of_match,
		   },
};

int  tlsc6x_init(void)
{
	tlsc_info("%s: ++\n", __func__);
	if (get_tp_chip_id() == 0) {
		if ((tpd_cdev->tp_chip_id != TS_CHIP_MAX) && (tpd_cdev->tp_chip_id != TS_CHIP_TLSC)) {
			tlsc_err("this tp is not used,return.\n");
			return -EPERM;
		}
	}
	if (tpd_cdev->TP_have_registered) {
		tlsc_err("TP have registered by other TP.\n");
		return -EPERM;
	}
	return i2c_add_driver(&tlsc6x_driver);
}

void  tlsc6x_exit(void)
{
	i2c_del_driver(&tlsc6x_driver);
}

