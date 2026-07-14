/**
 * The device control driver for FocalTech's fingerprint sensor.
 *
 * Copyright (C) 2016-2017 FocalTech Systems Co., Ltd. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
**/

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/signal.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/bug.h>
#include <linux/types.h>
#include <linux/param.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#ifdef CONFIG_PM_WAKELOCKS
#include <linux/pm_wakeup.h>
#else
#include <linux/wakelock.h>
#endif

#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif
/* #include <transsion/hwinfo.h> */

#include "ff_log.h"
#include "ff_err.h"
#include "ff_ctl.h"
#include "ff_cfg.h"

#if(FF_CFG_BEANPOD_VERSION == 280)
typedef struct {
	uint32_t timeLow;
	uint16_t timeMid;
	uint16_t timeHiAndVersion;
	uint8_t clockSeqAndNode[8];
} TEEC_UUID;
#endif
#ifdef ANDROID_WAKELOCK
#include <linux/pm_wakeup.h>
static struct wakeup_source *wake;
#endif

#ifdef CONFIG_DSP_NOTIFIER
#define DRM_MODE_DPMS_ON        0
#define DRM_MODE_DPMS_STANDBY   1
#define DRM_MODE_DPMS_SUSPEND   2
#define DRM_MODE_DPMS_OFF       3
extern int dsp_register_client(struct notifier_block *nb);
extern int dsp_unregister_client(struct notifier_block *nb);
#endif
/*
 * Driver context definition and its singleton instance.
 */
typedef struct {
    struct miscdevice miscdev;
    struct work_struct work_queue;
    struct fasync_struct *async_queue;
#ifdef FF_FP_REGISTER_INPUT_DEV
    struct input_dev *input;
#endif
    struct notifier_block fb_notifier;
#ifdef ANDROID_WAKELOCK
    struct wakeup_source wake_lock;
#else 
    struct wake_lock wake_lock;
#endif
    bool b_driver_inited;
    bool b_config_dirtied;
} ff_ctl_context_t;
static ff_ctl_context_t *g_context = NULL;

/*
 * Driver configuration.
 */
static struct ff_driver_config_t driver_config;
struct ff_driver_config_t *g_config = NULL;
static ic_information_t ic_information;
ff_spidev_info_t ff_spidev_info;
////////////////////////////////////////////////////////////////////////////////
/// Logging driver to logcat through uevent mechanism.

#undef LOG_TAG
#define LOG_TAG "ff_ctl"

/*
 * Log level can be runtime configurable by 'FF_IOC_SYNC_CONFIG'.
 */
ff_log_level_t g_log_level = __FF_EARLY_LOG_LEVEL;

int ff_log_printf(ff_log_level_t level, const char *tag, const char *fmt, ...)
{
    /* Prepare the storage. */
    va_list ap;
    static char uevent_env_buf[128];
    char *ptr = uevent_env_buf;
    int n, available = sizeof(uevent_env_buf);

    /* Fill logging level. */
    available -= sprintf(uevent_env_buf, "FF_LOG=%1d", level);
    ptr += strlen(uevent_env_buf);

    /* Fill logging message. */
    va_start(ap, fmt);
    vsnprintf(ptr, available, fmt, ap);
    va_end(ap);

    /* Send to ff_device. */
    if (likely(g_context) && likely(g_config) && unlikely(g_config->logcat_driver)) {
        char *uevent_env[2] = {uevent_env_buf, NULL};
        kobject_uevent_env(&g_context->miscdev.this_device->kobj, KOBJ_CHANGE, uevent_env);
    }

    /* Native output. */
    switch (level) {
    case FF_LOG_LEVEL_ERR:
        n = printk(KERN_ERR FF_DRV_NAME": %s\n", ptr);
        break;
    case FF_LOG_LEVEL_WRN:
        n = printk(KERN_WARNING FF_DRV_NAME": %s\n", ptr);
        break;
    case FF_LOG_LEVEL_INF:
        n = printk(KERN_INFO FF_DRV_NAME": %s\n", ptr);
        break;
    case FF_LOG_LEVEL_DBG:
    case FF_LOG_LEVEL_VBS:
    default:
        n = printk(KERN_DEBUG FF_DRV_NAME": %s\n", ptr);
        break;
    }
    return n;
}

const char *ff_err_strerror(int err)
{
    static char errstr[32] = {'\0', };

    switch (err) {
    case FF_SUCCESS     : return "Success";
    case FF_ERR_INTERNAL: return "Internal error";

    /* Base on unix errno. */
    case FF_ERR_NOENT: return "No such file or directory";
    case FF_ERR_INTR : return "Interrupted";
    case FF_ERR_IO   : return "I/O error";
    case FF_ERR_AGAIN: return "Try again";
    case FF_ERR_NOMEM: return "Out of memory";
    case FF_ERR_BUSY : return "Resource busy / Timeout";

    /* Common error. */
    case FF_ERR_BAD_PARAMS  : return "Bad parameter(s)";
    case FF_ERR_NULL_PTR    : return "Null pointer";
    case FF_ERR_BUF_OVERFLOW: return "Buffer overflow";
    case FF_ERR_BAD_PROTOCOL: return "Bad protocol";
    case FF_ERR_SENSOR_SIZE : return "Wrong sensor dimension";
    case FF_ERR_NULL_DEVICE : return "Device not found";
    case FF_ERR_DEAD_DEVICE : return "Device is dead";
    case FF_ERR_REACH_LIMIT : return "Up to the limit";
    case FF_ERR_REE_TEMPLATE: return "Template store in REE";
    case FF_ERR_NOT_TRUSTED : return "Untrusted enrollment";

    default:
        sprintf(errstr, "%d", err);
        break;
    }

    return (const char *)errstr;
}

////////////////////////////////////////////////////////////////////////////////

/*fp nav*/
static void ff_report_uevent(struct ff_driver_config_t *g_config, char *str)
{
    char *envp[2];

    /* FF_LOGD("%s enter", __func__); */

    envp[0] = str;
    envp[1] = NULL;

    if (g_config->nav_dev) {
        /* FF_LOGD("%s g_config->nav_dev is not null", __func__); */
        kobject_uevent_env(&(g_config->nav_dev->dev.kobj), KOBJ_CHANGE, envp);
    } else {
        FF_LOGE("%s cdfinger->nav_dev is null", __func__);
    }
}

static void ff_ctl_report_key_event(ff_key_event_t *kevent)
{
    FF_LOGD("%s kevent->code=%d, kevent->value=%d", __func__, kevent->code, kevent->value);

    switch (kevent->code)
    {
        case KEYEVENT_UP:
            FF_LOGD("KEY_UP----------");
            ff_report_uevent(g_config, FF_FP_NAV_UP);
            break;
        case KEYEVENT_DOWN:
            FF_LOGD("KEY_DOWN----------");
            ff_report_uevent(g_config, FF_FP_NAV_DOWN);
            break;
        case KEYEVENT_RIGHT:
            FF_LOGD("KEY_RIGHT----------");
            ff_report_uevent(g_config, FF_FP_NAV_RIGHT);
            break;
        case KEYEVENT_LEFT:
            FF_LOGD("KEY_LEFT----------");
            ff_report_uevent(g_config, FF_FP_NAV_LEFT);
            break;
        default:
            FF_LOGE("Unsupport key code %d----------", kevent->code);
            break;
    }
}

static int ff_ctl_enable_irq(bool on)
{
    int err = 0;
    /* FF_LOGD("%s enter", __func__); */
    FF_LOGD("irq: %s", on ? "enable" : "disabled");

    if (unlikely(!g_config)) {
        return (-ENOSYS);
    }

    if (on) {
        enable_irq(g_config->irq_num);
    } else {
        disable_irq(g_config->irq_num);
    }

    /* FF_LOGD("%s leave", __func__); */
    return err;
}

static void ff_ctl_device_event(struct work_struct *ws)
{
    ff_ctl_context_t *ctx = container_of(ws, ff_ctl_context_t, work_queue);
    char *uevent_env[2] = {"FF_INTERRUPT", NULL};
    /* FF_LOGD("%s enter", __func__); */

    FF_LOGD("%s(irq = %d, ..) toggled", __func__, g_config->irq_num);

    kobject_uevent_env(&ctx->miscdev.this_device->kobj, KOBJ_CHANGE, uevent_env);

    /* FF_LOGD("%s leave", __func__); */
}

static irqreturn_t ff_ctl_device_irq(int irq, void *dev_id)
{
    /* ff_ctl_context_t *ctx = (ff_ctl_context_t *)dev_id; */
    disable_irq_nosync(irq);

    /* FF_LOGD("%s irq=%d, g_config->irq_num=%d", __func__, irq, g_config->irq_num); */
    if (likely(irq == g_config->irq_num)) {
        if (g_config && g_config->enable_fasync && g_context->async_queue) {
            /* FF_LOGD("%s kill_fasync", __func__); */
            kill_fasync(&g_context->async_queue, SIGIO, POLL_IN);
        } else {
            /* FF_LOGD("%s schedule_work", __func__); */
            schedule_work(&g_context->work_queue);
        }
    }
#ifdef ANDROID_WAKELOCK
    __pm_wakeup_event(wake, msecs_to_jiffies(1000));
#endif
    enable_irq(irq);
    return IRQ_HANDLED;
}

#ifdef FF_FP_REGISTER_INPUT_DEV
static int ff_ctl_report_key_event(struct input_dev *input, ff_key_event_t *kevent)
{
    int err = 0;
    FF_LOGD("%s enter", __func__);

    input_report_key(input, kevent->code, kevent->value);
    input_sync(input);

    FF_LOGD("%s leave", __func__);
    return err;
}
#endif

static const char *ff_ctl_get_version(void)
{
    static char version[FF_DRV_VERSION_LEN] = { 0 };
    /* FF_LOGD("%s enter", __func__); */

    snprintf(version, FF_DRV_VERSION_LEN, "%s-%s", FF_DRV_VERSION, ff_ctl_arch_str());
    FF_LOGD("version: %s", version);

    /* FF_LOGD("%s leave", __func__); */
    return (const char *)version;
}

static ff_spidev_info_t *ff_ctl_get_spidev_info(void)
{
	return (&ff_spidev_info);
}

#ifdef CONFIG_DSP_NOTIFIER
static int ff_ctl_fb_notifier_callback(struct notifier_block *nb, unsigned long action, void *data)
{
    char *uevent_env[2];
    int *p = data;
    int dsp_val=0;

    if (action != 0x10) {
        return NOTIFY_DONE;
    }
    if (p) dsp_val = *p;

    FF_LOGV("%s enter", __func__);

    switch (dsp_val) {
        case DRM_MODE_DPMS_ON:
	    uevent_env[0] = "FF_SCREEN_ON";
        break;
        case DRM_MODE_DPMS_OFF:
            uevent_env[0] = "FF_SCREEN_OFF";
        break;
	default:
	    uevent_env[0] = "FF_SCREEN_??";
        break;
    }
    uevent_env[1] = NULL;
    kobject_uevent_env(&g_context->miscdev.this_device->kobj, KOBJ_CHANGE, uevent_env);

    return NOTIFY_OK;
}
#endif

#ifdef FF_FP_REGISTER_INPUT_DEV
static int ff_ctl_register_input(void)
{
    int err = 0;
    FF_LOGV("%s enter", __func__);

    /* Allocate the input device. */
    g_context->input = input_allocate_device();
    if (!g_context->input) {
        FF_LOGE("input_allocate_device() failed");
        return (-ENOMEM);
    }

    /* Register the key event capabilities. */
    if (g_config) {
        input_set_capability(g_context->input, EV_KEY, g_config->keycode_nav_left    );
        input_set_capability(g_context->input, EV_KEY, g_config->keycode_nav_right   );
        input_set_capability(g_context->input, EV_KEY, g_config->keycode_nav_up      );
        input_set_capability(g_context->input, EV_KEY, g_config->keycode_nav_down    );
        input_set_capability(g_context->input, EV_KEY, g_config->keycode_double_click);
        input_set_capability(g_context->input, EV_KEY, g_config->keycode_click       );
        input_set_capability(g_context->input, EV_KEY, g_config->keycode_long_press  );
        input_set_capability(g_context->input, EV_KEY, g_config->keycode_simulation  );
    }

    /* Register the allocated input device. */
    g_context->input->name = "ff_key";
    err = input_register_device(g_context->input);
    if (err) {
        FF_LOGE("input_register_device(..) = %d", err);
        input_free_device(g_context->input);
        g_context->input = NULL;
        return (-ENODEV);
    }

    FF_LOGV("%s leave", __func__);
    return err;
}
#endif

static int ff_ctl_free_driver(void)
{
    int err = 0;
    FF_LOGV("%s enter", __func__);

    /* Unregister framebuffer event notifier. */
#ifdef CONFIG_DSP_NOTIFIER
        dsp_unregister_client(&g_context->fb_notifier);
#endif

#ifdef FF_FP_REGISTER_INPUT_DEV
    /* De-initialize the input subsystem. */
    if (g_context->input) {
        /*
         * Once input device was registered use input_unregister_device() and
         * memory will be freed once last reference to the device is dropped.
         */
        input_unregister_device(g_context->input);
        g_context->input = NULL;
    }
#endif

    /* Release IRQ resource. */
    FF_LOGI("%s g_config->irq_num=%d", __func__, g_config->irq_num);
    if (g_config->irq_num > 0) {
        err = disable_irq_wake(g_config->irq_num);
        if (err) {
            FF_LOGE("disable_irq_wake(%d) failed, err=%d", g_config->irq_num, err);
        } else {
            FF_LOGI("disable_irq_wake(%d) success", g_config->irq_num);
        }
        /* trying to free already-free IRQ */
        free_irq(g_config->irq_num, g_config);
        g_config->irq_num = -1;
    }

    /* Release pins resource. */
    err = ff_ctl_free_pins();

    FF_LOGV("%s leave", __func__);
    return err;
}

static int ff_ctl_init_driver(void)
{
    int err = 0;
    FF_LOGV("%s enter", __func__);

    if (unlikely(!g_context)) {
        return (-ENOSYS);
    }

    do {
        /* Initialize the PWR/SPI/RST/INT pins resource. */
        err = ff_ctl_init_pins();
        if (err) {
            FF_LOGE("ff_ctl_init_pins(..) failed, err=%d", err);
            break;
        } else {
            FF_LOGI("ff_ctl_init_pins(..) success");
            g_context->b_config_dirtied = true;
        }

        /* Register IRQ. */
        FF_LOGI("request_irq g_config->irq_num=%d", g_config->irq_num);
        err = request_irq(g_config->irq_num, ff_ctl_device_irq,
                IRQF_TRIGGER_RISING | IRQF_ONESHOT, "ff_irq", g_config);
        if (err) {
            FF_LOGE("request_irq(..) failed, err=%d", err);
            break;
        } else {
            FF_LOGI("request_irq(..) success");
        }

        /* Wake up the system while receiving the interrupt. */
        err = enable_irq_wake(g_config->irq_num);
        if (err) {
            FF_LOGE("enable_irq_wake(%d) failed, err=%d", g_config->irq_num, err);
        } else {
            FF_LOGI("enable_irq_wake(%d) success", g_config->irq_num);
        }

    } while (0);

    if (err) {
        ff_ctl_free_driver();
        return err;
    }

#ifdef FF_FP_REGISTER_INPUT_DEV
    /* Initialize the input subsystem. */
    err = ff_ctl_register_input();
    if (err) {
        FF_LOGE("ff_ctl_init_input() failed, err=%d", err);
        //return err;
    } else {
        FF_LOGI("ff_ctl_init_input() success");
    }
#endif

#ifdef CONFIG_DSP_NOTIFIER
    /* Register screen on/off callback. */
    g_context->fb_notifier.notifier_call = ff_ctl_fb_notifier_callback;
    err = dsp_register_client(&g_context->fb_notifier);
#endif
 //   transsion_hwinfo_add("Fingerprint_sensor_vendor", "FOCALTECH");
 //   transsion_hwinfo_add("Fingerprint_sensor_ic", "FT9371s6_LCN");

    g_context->b_driver_inited = true;
    FF_LOGV("%s leave", __func__);
    return err;
}

////////////////////////////////////////////////////////////////////////////////
// struct file_operations fields.

static int ff_ctl_fasync(int fd, struct file *filp, int mode)
{
    int err = 0;
    FF_LOGV("%s enter", __func__);

    FF_LOGD("%s: mode = 0x%08x", __func__, mode);
    err = fasync_helper(fd, filp, mode, &g_context->async_queue);

    FF_LOGV("%s leave", __func__);
    return err;
}

static long ff_ctl_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int err = 0;
    /* struct miscdevice *dev = (struct miscdevice *)filp->private_data; */
    /* ff_ctl_context_t *ctx = container_of(dev, ff_ctl_context_t, miscdev); */
    ff_key_event_t kevent;
    /* FF_LOGV("%s enter", __func__); */

#if 1
    if (g_log_level <= FF_LOG_LEVEL_DBG) {
        static const char *cmd_names[] = {
                "FF_IOC_INIT_DRIVER", "FF_IOC_FREE_DRIVER",
                "FF_IOC_RESET_DEVICE",
                "FF_IOC_ENABLE_IRQ", "FF_IOC_DISABLE_IRQ",
                "FF_IOC_ENABLE_SPI_CLK", "FF_IOC_DISABLE_SPI_CLK",
                "FF_IOC_ENABLE_POWER", "FF_IOC_DISABLE_POWER",
                "FF_IOC_REPORT_KEY_EVENT", "FF_IOC_SYNC_CONFIG",
                "FF_IOC_GET_VERSION", "unknown",
        };
        unsigned int _cmd = _IOC_NR(cmd);
        if (_cmd > _IOC_NR(FF_IOC_GET_VERSION)) {
            _cmd = _IOC_NR(FF_IOC_GET_VERSION) + 1;
        }
        FF_LOGD("%s(.., %s, ..) invoke", __func__, cmd_names[_cmd]);
    }
#endif

    switch (cmd) {
    case FF_IOC_INIT_DRIVER: {
        FF_LOGI("FF_IOC_INIT_DRIVER");
        if (g_context->b_driver_inited) {
            err = ff_ctl_free_driver();
        }
        if (!err) {
            err = ff_ctl_init_driver();
            // TODO: Sync the dirty configuration back to HAL.
        }
        break;
    }
    case FF_IOC_FREE_DRIVER:
        FF_LOGI("FF_IOC_FREE_DRIVER");
        if (g_context->b_driver_inited) {
            err = ff_ctl_free_driver();
            g_context->b_driver_inited = false;
        }
        break;
    case FF_IOC_RESET_DEVICE:
        FF_LOGI("FF_IOC_RESET_DEVICE");
        err = ff_ctl_reset_device();
        break;
    case FF_IOC_ENABLE_IRQ:
        FF_LOGI("FF_IOC_ENABLE_IRQ");
        err = ff_ctl_enable_irq(1);
        break;
    case FF_IOC_DISABLE_IRQ:
        FF_LOGI("FF_IOC_DISABLE_IRQ");
        err = ff_ctl_enable_irq(0);
        break;
    case FF_IOC_ENABLE_SPI_CLK:
        FF_LOGI("FF_IOC_ENABLE_SPI_CLK");
        err = ff_ctl_enable_spiclk(1);
        break;
    case FF_IOC_DISABLE_SPI_CLK:
        FF_LOGI("FF_IOC_DISABLE_SPI_CLK");
        err = ff_ctl_enable_spiclk(0);
        break;
    case FF_IOC_ENABLE_POWER:
        FF_LOGI("FF_IOC_ENABLE_POWER");
        err = ff_ctl_enable_power(1);
        break;
    case FF_IOC_DISABLE_POWER:
        FF_LOGI("FF_IOC_DISABLE_POWER");
        err = ff_ctl_enable_power(0);
        break;
    case FF_IOC_REPORT_KEY_EVENT: {
        FF_LOGI("FF_IOC_REPORT_KEY_EVENT");
        if (copy_from_user(&kevent, (ff_key_event_t *)arg, sizeof(ff_key_event_t))) {
            FF_LOGE("copy_from_user(..) failed");
            err = (-EFAULT);
            break;
        }
#ifdef FF_FP_REGISTER_INPUT_DEV
        err = ff_ctl_report_key_event(ctx->input, &kevent);
#endif
        ff_ctl_report_key_event(&kevent);
        break;
    }
    case FF_IOC_SYNC_CONFIG: {
        FF_LOGI("FF_IOC_SYNC_CONFIG");
        if (copy_from_user(&driver_config, (struct ff_driver_config_t *)arg, sizeof(struct ff_driver_config_t))) {
            FF_LOGE("copy_from_user(..) failed");
            err = (-EFAULT);
            break;
        }
        g_config = &driver_config;

        /* Take logging level effect. */
        g_log_level = g_config->log_level;
        break;
    }
    case FF_IOC_GET_VERSION: {
        FF_LOGI("FF_IOC_GET_VERSION");
        if (copy_to_user((void *)arg, ff_ctl_get_version(), FF_DRV_VERSION_LEN)) {
            FF_LOGE("copy_to_user(..) failed");
            err = (-EFAULT);
            break;
        }
        break;
    }
    case FF_IOC_SET_IC_INFORMATION: {
        FF_LOGI("FF_IOC_SET_IC_INFORMATION");
        if (copy_from_user(&ic_information, (ic_information_t *)arg, sizeof(ic_information_t))) {
            FF_LOGE("copy_from_user(..) failed");
            err = (-EFAULT);
            break;
        }
        break;
    }
    case FF_IOC_GET_IC_INFORMATION: {
        FF_LOGI("FF_IOC_GET_IC_INFORMATION");
        if (copy_to_user((void *)arg, &ic_information, sizeof(ic_information_t))) {
            FF_LOGE("copy_to_user(..) failed");
            err = (-EFAULT);
            break;
        }
        break;
    }
    case FF_IOC_GET_SPIDEV_INFO: {
        FF_LOGI("FF_IOC_GET_SPIDEV_INFO");
        if (copy_to_user((void *)arg, ff_ctl_get_spidev_info(), sizeof(ff_spidev_info_t))) {
            FF_LOGE("copy_to_user(..) failed");
            err = (-EFAULT);
            break;
        }
        break;
    }
    default:
        FF_LOGE("Unsupport ioctl cmd");
        err = (-EINVAL);
        break;
    }

    /* FF_LOGV("%s leave", __func__); */
    return err;
}

static int ff_ctl_open(struct inode *inode, struct file *filp)
{
    struct device_node *dnode = NULL;

    FF_LOGD("%s enter", __func__);

    dnode = of_find_compatible_node(NULL, NULL, "zte_fp_nav");
    if (dnode) {
        FF_LOGD("fp-nav device node found!");
        g_config->nav_dev = of_find_device_by_node(dnode);
        if (g_config->nav_dev)
        {
            FF_LOGD("fp-nav device uevent found!");
        } else {
            FF_LOGE("fp-nav device uevent not found!");
        }
    } else {
        FF_LOGE("fp-nav device node not found!");
    }

    FF_LOGD("%s exit", __func__);

    return 0;
}

static int ff_ctl_release(struct inode *inode, struct file *filp)
{
    int err = 0;
    FF_LOGV("%s enter", __func__);

    /* Remove this filp from the asynchronously notified filp's. */
    err = ff_ctl_fasync(-1, filp, 0);

    if (g_context->b_driver_inited) {
        FF_LOGI("%s ff_ctl_free_driver", __func__);
        err = ff_ctl_free_driver();
        g_context->b_driver_inited = false;
    }

    FF_LOGV("%s leave", __func__);
    return err;
}

#ifdef CONFIG_COMPAT
static long ff_ctl_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int err = 0;
    FF_LOGV("focal %s enter", __func__);

    err = ff_ctl_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));

    FF_LOGV("%s leave", __func__);
    return err;
}
#endif
///////////////////////////////////////////////////////////////////////////////

static struct file_operations ff_ctl_fops = {
    .owner          = THIS_MODULE,
    .fasync         = ff_ctl_fasync,
    .unlocked_ioctl = ff_ctl_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl   = ff_ctl_compat_ioctl,
#endif
    .open           = ff_ctl_open,
    .release        = ff_ctl_release,
};

static ff_ctl_context_t ff_ctl_context = {
    .miscdev = {
        .minor = MISC_DYNAMIC_MINOR,
        .name  = FF_DRV_NAME,
        .fops  = &ff_ctl_fops,
    },
};

#if defined(USE_SPI_BUS)
static int focaltech_fp_probe(struct spi_device *dev)
#elif defined(USE_PLATFORM_BUS)
static int focaltech_fp_probe(struct platform_device *dev)
#endif
{
    FF_LOGV("%s enter", __func__);

    g_config = kzalloc(sizeof(struct ff_driver_config_t), GFP_KERNEL);
    if (!g_config) {
        FF_LOGE("%s alloc g_config failed!", __func__);
        return -ENOMEM;;
    } else {
        FF_LOGI("%s alloc g_config success!", __func__);
    }
#if defined(USE_SPI_BUS)
    g_config->spi_dev = dev;
#elif defined(USE_PLATFORM_BUS)
    g_config->platform_dev = dev;
#endif

    g_config->power_type        = 0;
    g_config->fp_reg            = NULL;
    g_config->power_voltage     = 2800000;/*default fp voltage*/

    FF_LOGV("%s exit", __func__);
    return 0;
}

#if defined(USE_SPI_BUS)
static int focaltech_fp_remove(struct spi_device *dev)
#elif defined(USE_PLATFORM_BUS)
static int focaltech_fp_remove(struct platform_device *dev)
#endif
{
    FF_LOGV("%s enter", __func__);

    if (g_config) {
	    kfree(g_config);
	    g_config = NULL;
    }
    FF_LOGV("%s exit", __func__);
    return 0;
}

static struct of_device_id focaltech_fp_of_match[] = {
	{ .compatible = FF_COMPATIBLE_NODE },
	{}
};
MODULE_DEVICE_TABLE(of, focaltech_fp_of_match);

#if defined(USE_SPI_BUS)
static struct spi_driver focaltech_fp_driver = {
#elif defined(USE_PLATFORM_BUS)
static struct platform_driver focaltech_fp_driver = {
#endif
	.driver = {
		.name = FF_DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = focaltech_fp_of_match,
	},
	.probe = focaltech_fp_probe,
	.remove = focaltech_fp_remove,
};

int focaltech_fp_driver_init(void)
{
    int err = 0;
    FF_LOGV("%s enter, driver_time:2022-12-20", __func__);

    /* Register as a miscellaneous device. */
    err = misc_register(&ff_ctl_context.miscdev);
    if (err) {
        FF_LOGE("misc_register(..) failed, err=%d", err);
        return err;
    } else {
        FF_LOGI("misc_register(..) success");
    }

    /* Init the interrupt workqueue. */
    INIT_WORK(&ff_ctl_context.work_queue, ff_ctl_device_event);

    /* Init the wake lock. */
#ifdef ANDROID_WAKELOCK
//    wakeup_source_init(&ff_ctl_context.wake_lock, "ff_wake_lock");
	wake = wakeup_source_register(NULL, "ff_wake_lock");
	if (!wake) {
        FF_LOGE("wakeup_source_register ff_wake_lock failed");
		return -ENOMEM;
	} else {
        FF_LOGI("wakeup_source_register ff_wake_lock success");
    }
#else
    wake_lock_init(&ff_ctl_context.wake_lock, WAKE_LOCK_SUSPEND, "ff_wake_lock");
#endif

    /* Assign the context instance. */
    g_context = &ff_ctl_context;
    g_context->b_driver_inited = false;

#if defined(USE_PLATFORM_BUS)
    FF_LOGI("%s platform_driver_register", __func__);
    err = platform_driver_register(&focaltech_fp_driver);
#elif defined(USE_SPI_BUS)
    FF_LOGI("%s spi_register_driver", __func__);
    err = spi_register_driver(&focaltech_fp_driver);
#endif

    /* FF_LOGI("FocalTech fingerprint device control driver registered"); */
    FF_LOGV("%s leave", __func__);
    return err;
}

void focaltech_fp_driver_exit(void)
{

    /* Release the HW resources if needs. */
    if (g_context->b_driver_inited) {
        ff_ctl_free_driver();
        g_context->b_driver_inited = false;
    }

    /* De-init the wake lock. */
#ifdef ANDROID_WAKELOCK
/*  wakeup_source_trash(&g_context->wake_lock); */
    wakeup_source_unregister(wake);
#else
    wake_lock_destroy(&g_context->wake_lock);
#endif
    /* Unregister the miscellaneous device. */
    misc_deregister(&g_context->miscdev);

    /* 'g_context' could not use any more. */
    g_context = NULL;

#if defined(USE_PLATFORM_BUS)
    FF_LOGI("%s platform_driver_unregister", __func__);
    platform_driver_unregister(&focaltech_fp_driver);
#elif defined(USE_SPI_BUS)
    FF_LOGI("%s spi_unregister_driver", __func__);
    spi_unregister_driver(&focaltech_fp_driver);
#endif

    FF_LOGI("FocalTech fingerprint device control driver released");
}

/*module_init(focaltech_fp_driver_init);
module_exit(focaltech_fp_driver_exit);*/

MODULE_DESCRIPTION("The device control driver for FocalTech's fingerprint sensor.");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("FocalTech Fingerprint R&D department");

