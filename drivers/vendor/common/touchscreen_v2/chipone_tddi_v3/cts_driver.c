#define LOG_TAG         "Driver"

#include "cts_config.h"
#include "cts_platform.h"
#include "cts_core.h"
#include "cts_driver.h"
#include "cts_charger_detect.h"
#include "cts_earjack_detect.h"
#include "cts_sysfs.h"
#include "cts_cdev.h"
#include "cts_strerror.h"

#ifdef CFG_CTS_CHARGER_DETECT
#include <linux/power_supply.h>
#endif /* CFG_CTS_CHARGER_DETECT */

bool cts_show_debug_log = false;
module_param_named(debug_log, cts_show_debug_log, bool, 0660);
MODULE_PARM_DESC(debug_log, "Show debug log control");

/*zte_add*/
extern void cts_tpd_register_fw_class(struct cts_device *cts_dev);
static void cts_resume_work_func(struct work_struct *work);
static void cts_resume_work_func(struct work_struct *work)
{
	struct chipone_ts_data *cts_data = container_of(work, struct chipone_ts_data, ts_resume_work);

	cts_info("%s", __func__);
	cts_driver_resume(cts_data);
}

int cts_driver_suspend(struct chipone_ts_data *cts_data)
{
	int ret;

	cts_info("Suspend");

	cts_lock_device(&cts_data->cts_dev);
	ret = cts_suspend_device(&cts_data->cts_dev);
	cts_unlock_device(&cts_data->cts_dev);

	if (ret) {
		cts_err("Suspend device failed %d(%s)",
			ret, cts_strerror(ret));
		// TODO:
		//return ret;
	}

	ret = cts_stop_device(&cts_data->cts_dev);
	if (ret) {
		cts_err("Stop device failed %d(%s)",
			ret, cts_strerror(ret));
		return ret;
	}

#ifdef CFG_CTS_GESTURE
	/* Enable IRQ wake if gesture wakeup enabled */
	if (cts_is_gesture_wakeup_enabled(&cts_data->cts_dev)) {
		ret = cts_plat_enable_irq_wake(cts_data->pdata);
		if (ret) {
			cts_err("Enable IRQ wake failed %d(%s)",
			ret, cts_strerror(ret));
			return ret;
		}
		ret = cts_plat_enable_irq(cts_data->pdata);
		if (ret) {
			cts_err("Enable IRQ failed %d(%s)",
				ret, cts_strerror(ret));
			return ret;
		}
	}
#endif /* CFG_CTS_GESTURE */

#ifdef CONFIG_TOUCHSCREEN_POINT_REPORT_CHECK
	cancel_delayed_work_sync(&tpd_cdev->point_report_check_work);
#endif

	/** - To avoid waking up while not sleeping, delay 20ms to ensure reliability */
	msleep(20);

	return 0;
}

int cts_driver_resume(struct chipone_ts_data *cts_data)
{
	int ret;

	cts_info("Resume");

#ifdef CFG_CTS_GESTURE
	if (cts_is_gesture_wakeup_enabled(&cts_data->cts_dev)) {
		ret = cts_plat_disable_irq_wake(cts_data->pdata);
		if (ret) {
			cts_warn("Disable IRQ wake failed %d(%s)",
				ret, cts_strerror(ret));
			//return ret;
		}
		ret = cts_plat_disable_irq(cts_data->pdata);
		if (ret < 0) {
			cts_err("Disable IRQ failed %d(%s)",
				ret, cts_strerror(ret));
			//return ret;
		}
	}
#endif /* CFG_CTS_GESTURE */

	cts_lock_device(&cts_data->cts_dev);
	ret = cts_resume_device(&cts_data->cts_dev);
	cts_unlock_device(&cts_data->cts_dev);
	if (ret) {
		cts_warn("Resume device failed %d(%s)",
			ret, cts_strerror(ret));
		return ret;
	}

	ret = cts_start_device(&cts_data->cts_dev);
	if (ret) {
		cts_err("Start device failed %d(%s)",
			ret, cts_strerror(ret));
		return ret;
	}

	return 0;
}

#ifdef CONFIG_CTS_PM_FB_NOTIFIER
#ifdef CFG_CTS_DRM_NOTIFIER
static int fb_notifier_callback(struct notifier_block *nb,
		unsigned long action, void *data)
{
	volatile int blank;
	const struct cts_platform_data *pdata =
		container_of(nb, struct cts_platform_data, fb_notifier);
	struct chipone_ts_data *cts_data =
		container_of(pdata->cts_dev, struct chipone_ts_data, cts_dev);
	struct fb_event *evdata = data;

	cts_info("FB notifier callback");

	if (evdata && evdata->data) {
		if (action == MSM_DRM_EVENT_BLANK) {
			blank = *(int *)evdata->data;
			if (blank == MSM_DRM_BLANK_UNBLANK) {
				/*cts_driver_resume(cts_data);*/ //zte_modify
				queue_work(cts_data->workqueue, &cts_data->ts_resume_work);//zte_add
				return NOTIFY_OK;
			}
		} else if (action == MSM_DRM_EARLY_EVENT_BLANK) {
			blank = *(int *)evdata->data;
			if (blank == MSM_DRM_BLANK_POWERDOWN) {
				cts_driver_suspend(cts_data);
				return NOTIFY_OK;
			}
		}
	}

	return NOTIFY_DONE;
}
#elif defined(CONFIG_FB)
static int fb_notifier_callback(struct notifier_block *nb,
		unsigned long action, void *data)
{
	volatile int blank;
	const struct cts_platform_data *pdata =
		container_of(nb, struct cts_platform_data, fb_notifier);
	struct chipone_ts_data *cts_data =
		container_of(pdata->cts_dev, struct chipone_ts_data, cts_dev);
	struct fb_event *evdata = data;

	cts_info("FB notifier callback");

	if (evdata && evdata->data) {
		if (action == FB_EVENT_BLANK) {
			blank = *(int *)evdata->data;
			if (blank == FB_BLANK_UNBLANK) {
				/*cts_driver_resume(cts_data);*//* zte_add */
				queue_work(cts_data->workqueue, &cts_data->ts_resume_work);/* zte_add */
				return NOTIFY_OK;
			}
		} else if (action == FB_EARLY_EVENT_BLANK) {
			blank = *(int *)evdata->data;
			if (blank == FB_BLANK_POWERDOWN) {
				cts_driver_suspend(cts_data);
				return NOTIFY_OK;
			}
		}
	}

	return NOTIFY_DONE;
}
#endif

static int cts_init_pm_fb_notifier(struct chipone_ts_data *cts_data)
{
	cts_info("Init FB notifier");

#ifdef CFG_CTS_DRM_NOTIFIER
	cts_data->pdata->fb_notifier.notifier_call = fb_notifier_callback;
	return msm_drm_register_client(&cts_data->pdata->fb_notifier);
#elif defined(CONFIG_FB)
	cts_data->pdata->fb_notifier.notifier_call = fb_notifier_callback;
	return fb_register_client(&cts_data->pdata->fb_notifier);
#else
	return 0;
#endif
}

static int cts_deinit_pm_fb_notifier(struct chipone_ts_data *cts_data)
{
	cts_info("Deinit FB notifier");
#ifdef CFG_CTS_DRM_NOTIFIER
	return msm_drm_unregister_client(&cts_data->pdata->fb_notifier)
#elif defined(CONFIG_FB)
	return fb_unregister_client(&cts_data->pdata->fb_notifier);
#else
	return 0;
#endif
}
#endif /* CONFIG_CTS_PM_FB_NOTIFIER */

#ifdef CFG_CTS_CHARGER_DETECT
static int cts_get_charger_ststus(int *status)
{
	static struct power_supply *batt_psy;
	union power_supply_propval val = { 0, };

	if (batt_psy == NULL)
		batt_psy = power_supply_get_by_name("battery");
	if (batt_psy) {
		batt_psy->desc->get_property(batt_psy, POWER_SUPPLY_PROP_STATUS, &val);
	}
	if ((val.intval == POWER_SUPPLY_STATUS_CHARGING) ||
			(val.intval == POWER_SUPPLY_STATUS_FULL)){
		*status = 1;
	} else {
		*status = 0;
	}
	cts_info("charger status:%d", *status);
	return 0;
}

static void cts_delayed_work_charger(struct work_struct *work)
{
	int ret, status;
	struct chipone_ts_data *cts_data;

	cts_data = container_of(work, struct chipone_ts_data, charger_work.work);

	ret = cts_get_charger_ststus(&status);
	if (ret) {
		cts_err("get charger status err");
		return;
	} else {
		cts_info("charger status:%d", status);
	}
	if (!cts_is_device_enabled(&cts_data->cts_dev)) {
		cts_err("Charger status changed, but device is not enabled");
		cts_data->cts_dev.rtdata.charger_exist = status;
		return;
	}
	if (status) {
		cts_charger_plugin(&cts_data->cts_dev);
	} else {
		cts_charger_plugout(&cts_data->cts_dev);
	}
}

static int cts_charger_notify_call(struct notifier_block *nb, unsigned long event, void *data)
{
	struct power_supply *psy = data;
	const struct cts_platform_data *pdata = container_of(nb, struct cts_platform_data, charger_notifier);
	struct chipone_ts_data *cts_data = container_of(pdata->cts_dev, struct chipone_ts_data, cts_dev);

	if (event != PSY_EVENT_PROP_CHANGED) {
		return NOTIFY_DONE;
	}

	if ((strcmp(psy->desc->name, "usb") == 0)
		|| (strcmp(psy->desc->name, "ac") == 0)) {
		queue_delayed_work(cts_data->workqueue, &cts_data->charger_work, msecs_to_jiffies(500));
	}

	return NOTIFY_DONE;
}

static int cts_init_charger_notifier(struct chipone_ts_data *cts_data)
{
	int ret = 0;

	cts_info("Init Charger notifier");

	cts_data->pdata->charger_notifier.notifier_call = cts_charger_notify_call;
	ret = power_supply_reg_notifier(&cts_data->pdata->charger_notifier);
	return ret;
}

int cts_deinit_charger_notifier(struct chipone_ts_data *cts_data)
{
	cts_info("Deinit Charger notifier");

	power_supply_unreg_notifier(&cts_data->pdata->charger_notifier);
	return 0;
}
#endif /* CFG_CTS_CHARGER_DETECT */

int cts_driver_probe(struct device *device, enum cts_bus_type bus_type)
{
	struct chipone_ts_data *cts_data = NULL;
	int ret = 0;

	cts_info("%s enter", __func__);
	if (tpd_cdev->TP_have_registered) {
		cts_info("TP have registered by other TP.\n");
		return -EPERM;
	}
	cts_data = kzalloc(sizeof(struct chipone_ts_data), GFP_KERNEL);
	if (cts_data == NULL) {
		cts_err("Alloc chipone_ts_data failed");
		return -ENOMEM;
	}

	cts_data->pdata = kzalloc(sizeof(struct cts_platform_data), GFP_KERNEL);
	if (cts_data->pdata == NULL) {
		cts_err("Alloc cts_platform_data failed");
		ret = -ENOMEM;
		goto err_free_cts_data;
	}

	cts_data->cts_dev.bus_type = bus_type;
	dev_set_drvdata(device, cts_data);
	cts_data->device = device;

	ret = cts_init_platform_data(cts_data->pdata, device, bus_type);
	if (ret) {
		cts_err("Init platform data failed %d(%s)",
			ret, cts_strerror(ret));
		goto err_free_pdata;
	}

	cts_data->cts_dev.pdata = cts_data->pdata;
	cts_data->pdata->cts_dev = &cts_data->cts_dev;

	cts_data->workqueue =
		create_singlethread_workqueue(CFG_CTS_DEVICE_NAME "-workqueue");
	if (cts_data->workqueue == NULL) {
		cts_err("Create workqueue failed");
		ret = -ENOMEM;
		goto err_free_pdata;
	}

#ifdef CONFIG_CTS_ESD_PROTECTION
	cts_data->esd_workqueue =
		create_singlethread_workqueue(CFG_CTS_DEVICE_NAME "-esd_workqueue");
	if (cts_data->esd_workqueue == NULL) {
		cts_err("Create esd workqueue failed");
		ret = -ENOMEM;
		goto err_destroy_workqueue;
	}
#endif
	ret = cts_plat_request_resource(cts_data->pdata);
	if (ret < 0) {
		cts_err("Request resource failed %d(%s)",
			ret, cts_strerror(ret));
		goto err_destroy_esd_workqueue;
	}

	ret = cts_plat_reset_device(cts_data->pdata);
	if (ret < 0) {
		cts_err("Reset device failed %d(%s)",
			ret, cts_strerror(ret));
		goto err_free_resource;
	}

	ret = cts_probe_device(&cts_data->cts_dev);
	if (ret) {
		cts_err("Probe device failed %d(%s)",
			ret, cts_strerror(ret));
		goto err_free_resource;
	}

	ret = cts_plat_init_touch_device(cts_data->pdata);
	if (ret < 0) {
		cts_err("Init touch device failed %d(%s)",
			ret, cts_strerror(ret));
		goto err_free_resource;
	}

	ret = cts_plat_init_vkey_device(cts_data->pdata);
	if (ret < 0) {
		cts_err("Init vkey device failed %d(%s)",
			ret, cts_strerror(ret));
		goto err_deinit_touch_device;
	}

	ret = cts_plat_init_gesture(cts_data->pdata);
	if (ret < 0) {
		cts_err("Init gesture failed %d(%s)",
			ret, cts_strerror(ret));
		goto err_deinit_vkey_device;
	}

	cts_init_esd_protection(cts_data);

	ret = cts_tool_init(cts_data);
	if (ret < 0) {
		cts_warn("Init tool node failed %d(%s)",
			ret, cts_strerror(ret));
	}

	ret = cts_sysfs_add_device(device);
	if (ret < 0) {
		cts_warn("Add sysfs entry for device failed %d(%s)",
			ret, cts_strerror(ret));
	}

	ret = cts_init_cdev(cts_data);
	if (ret < 0) {
		cts_warn("Init cdev failed %d(%s)",
			ret, cts_strerror(ret));
	}

#ifdef CONFIG_CTS_PM_FB_NOTIFIER
	ret = cts_init_pm_fb_notifier(cts_data);
	if (ret) {
		cts_err("Init FB notifier failed %d", ret);
		goto err_deinit_sysfs;
	}
#endif /* CONFIG_CTS_PM_FB_NOTIFIER */

	ret = cts_plat_request_irq(cts_data->pdata);
	if (ret < 0) {
		cts_err("Request IRQ failed %d(%s)",
			ret, cts_strerror(ret));
		goto err_register_fb;
	}

	  /* Init firmware upgrade work and schedule */
	INIT_DELAYED_WORK(&cts_data->fw_upgrade_work, cts_firmware_upgrade_work);
	queue_delayed_work(cts_data->workqueue, &cts_data->fw_upgrade_work,
		msecs_to_jiffies(1000));

	/*zte_add*/
	INIT_WORK(&cts_data->ts_resume_work, cts_resume_work_func);

#ifdef CONFIG_CTS_CHARGER_DETECT
	ret = cts_init_charger_detect(cts_data);
	if (ret) {
		cts_err("Init charger detect failed %d(%s)",
			ret, cts_strerror(ret));
		// Ignore this error
	}
#endif

#ifdef CFG_CTS_CHARGER_DETECT
	INIT_DELAYED_WORK(&cts_data->charger_work, cts_delayed_work_charger);
	queue_delayed_work(cts_data->workqueue, &cts_data->charger_work, msecs_to_jiffies(1000));
	ret = cts_init_charger_notifier(cts_data);
	if (ret) {
		cts_err("Init Charger notifer failed %d", ret);
		goto err_unregister_charger_notifer;
	}
#endif /* CFG_CTS_CHARGER_DETECT */

#ifdef CONFIG_CTS_EARJACK_DETECT
	ret = cts_init_earjack_detect(cts_data);
	if (ret) {
		cts_err("Init earjack detect failed %d(%s)",
			ret, cts_strerror(ret));
		// Ignore this error
	}
#endif
#if 0
	ret = cts_start_device(&cts_data->cts_dev);
	if (ret) {
		cts_err("Start device failed %d(%s)",
			ret, cts_strerror(ret));
		goto err_deinit_earjack_detect;
	}
#endif
#ifdef CONFIG_MTK_PLATFORM
	//tpd_load_status = 1;
#endif /* CONFIG_MTK_PLATFORM */

	/*zte_add*/
	cts_tpd_register_fw_class(&cts_data->cts_dev);
	tpd_cdev->TP_have_registered = true;
	tpd_cdev->tp_chip_id = TS_CHIP_CHIPONE;
	cts_info("%s exit", __func__);
	return 0;
#if 0
err_deinit_earjack_detect:
#ifdef CONFIG_CTS_EARJACK_DETECT
	cts_deinit_earjack_detect(cts_data);
#endif
#endif
#ifdef CONFIG_CTS_CHARGER_DETECT
	cts_deinit_charger_detect(cts_data);
#endif

#ifdef CFG_CTS_CHARGER_DETECT
	cts_deinit_charger_notifier(cts_data);
err_unregister_charger_notifer:
#endif /* CFG_CTS_CHARGER_DETECT */

	cts_plat_free_irq(cts_data->pdata);
err_register_fb:
#ifdef CONFIG_CTS_PM_FB_NOTIFIER
	cts_deinit_pm_fb_notifier(cts_data);
err_deinit_sysfs:
#endif /* CONFIG_CTS_PM_FB_NOTIFIER */
	cts_sysfs_remove_device(device);
#ifdef CONFIG_CTS_LEGACY_TOOL
	cts_tool_deinit(cts_data);
#endif /* CONFIG_CTS_LEGACY_TOOL */

#ifdef CONFIG_CTS_ESD_PROTECTION
	cts_deinit_esd_protection(cts_data);
#endif /* CONFIG_CTS_ESD_PROTECTION */

#ifdef CFG_CTS_GESTURE
	cts_plat_deinit_gesture(cts_data->pdata);
#endif /* CFG_CTS_GESTURE */

err_deinit_vkey_device:
#ifdef CONFIG_CTS_VIRTUALKEY
	cts_plat_deinit_vkey_device(cts_data->pdata);
#endif /* CONFIG_CTS_VIRTUALKEY */

err_deinit_touch_device:
	cts_plat_deinit_touch_device(cts_data->pdata);

err_free_resource:
	cts_plat_free_resource(cts_data->pdata);
err_destroy_esd_workqueue:
#ifdef CONFIG_CTS_ESD_PROTECTION
	destroy_workqueue(cts_data->esd_workqueue);
err_destroy_workqueue:
#endif
	destroy_workqueue(cts_data->workqueue);

err_free_pdata:
	kfree(cts_data->pdata);
err_free_cts_data:
	kfree(cts_data);

	cts_err("Probe failed %d(%s)", ret, cts_strerror(ret));
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	if (tpd_cdev->tp_chip_id == TS_CHIP_CHIPONE)
		tpd_cdev->ztp_probe_fail_chip_id = TS_CHIP_CHIPONE;
#endif
	return ret;
}

int cts_driver_remove(struct device *device)
{
	struct chipone_ts_data *cts_data;
	int ret = 0;

	cts_info("Remove");

	cts_data = (struct chipone_ts_data *)dev_get_drvdata(device);
	if (cts_data) {
		ret = cts_stop_device(&cts_data->cts_dev);
		if (ret) {
			cts_warn("Stop device failed %d(%s)",
				ret, cts_strerror(ret));
		}

#ifdef CONFIG_CTS_CHARGER_DETECT
		cts_deinit_charger_detect(cts_data);
#endif
#ifdef CONFIG_CTS_EARJACK_DETECT
		cts_deinit_earjack_detect(cts_data);
#endif

#ifdef CFG_CTS_CHARGER_DETECT
		cts_deinit_charger_notifier(cts_data);
#endif /* CFG_CTS_CHARGER_DETECT */

		cts_plat_free_irq(cts_data->pdata);

		cts_tool_deinit(cts_data);

		cts_deinit_cdev(cts_data);

		cts_sysfs_remove_device(device);

		cts_deinit_esd_protection(cts_data);

		cts_plat_deinit_touch_device(cts_data->pdata);

		cts_plat_deinit_vkey_device(cts_data->pdata);

		cts_plat_deinit_gesture(cts_data->pdata);

		cts_plat_free_resource(cts_data->pdata);

#ifdef CONFIG_CTS_ESD_PROTECTION
		if (cts_data->esd_workqueue) {
			destroy_workqueue(cts_data->esd_workqueue);
		}
#endif

		if (cts_data->workqueue) {
			destroy_workqueue(cts_data->workqueue);
		}

		if (cts_data->pdata) {
			kfree(cts_data->pdata);
		}
		kfree(cts_data);
	} else {
		cts_warn("Remove while chipone_ts_data = NULL");
		return -EINVAL;
	}

	return ret;
}

#ifdef CONFIG_CTS_SYSFS
static ssize_t driver_config_show(struct device_driver *driver, char *buf)
{
#define SEPARATION_LINE \
	"-----------------------------------------------\n"

	int count = 0;

	count += scnprintf(buf + count, PAGE_SIZE - count,
		SEPARATION_LINE
		"%-32s: "CFG_CTS_DRIVER_VERSION"\n"
		"%-32s: "CFG_CTS_DRIVER_NAME"\n"
		"%-32s: "CFG_CTS_DEVICE_NAME"\n",
		"Driver Version", "Driver Name", "Device Name");

	count += scnprintf(buf + count, PAGE_SIZE - count,
		"%-32s: %c\n", "CONFIG_CTS_OF",
#ifdef CONFIG_CTS_OF
		 'Y'
#else
		 'N'
#endif
	);
#ifdef CONFIG_CTS_OF
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  %-30s: "CFG_CTS_OF_DEVICE_ID_NAME"\n",
		"CFG_CTS_OF_DEVICE_ID_NAME");
#endif /* CONFIG_CTS_OF */
	count += scnprintf(buf + count, PAGE_SIZE - count,
		SEPARATION_LINE
		"%-32s: %c\n", "CONFIG_CTS_LEGACY_TOOL",
#ifdef CONFIG_CTS_LEGACY_TOOL
		 'Y'
#else
		 'N'
#endif
	);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"%-32s: %c\n", "CONFIG_CTS_SYSFS",
#ifdef CONFIG_CTS_SYSFS
		 'Y'
#else
		 'N'
#endif
	);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"%-32s: %c\n", "CFG_CTS_HANDLE_IRQ_USE_KTHREAD",
#ifdef CFG_CTS_HANDLE_IRQ_USE_KTHREAD
		 'Y'
#else
		 'N'
#endif
	);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"%-32s: %c\n", "CFG_CTS_MAKEUP_EVENT_UP",
#ifdef CFG_CTS_MAKEUP_EVENT_UP
		 'Y'
#else
		 'N'
#endif
	);

	/* Reset pin, i2c/spi bus */
	count += scnprintf(buf + count, PAGE_SIZE - count,
		SEPARATION_LINE
		"%-32s: %c\n", "CFG_CTS_HAS_RESET_PIN",
#ifdef CFG_CTS_HAS_RESET_PIN
		'Y'
#else
		'N'
#endif
	);

#ifdef CONFIG_CTS_I2C_HOST
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"%-32s: Y\n"
		"  %-30s: %u\n",
		"CONFIG_CTS_I2C_HOST",
		"CFG_CTS_MAX_I2C_XFER_SIZE", CFG_CTS_MAX_I2C_XFER_SIZE);
#endif /* CONFIG_CTS_I2C_HOST */

#ifdef CONFIG_CTS_SPI_HOST
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"%-32s: Y\n"
		"  %-30s: %u\n"
		"  %-30s: %uKbps\n",
		"CONFIG_CTS_SPI_HOST",
		"CFG_CTS_MAX_SPI_XFER_SIZE", CFG_CTS_MAX_SPI_XFER_SIZE,
		"CFG_CTS_SPI_SPEED_KHZ", CFG_CTS_SPI_SPEED_KHZ);
#endif /* CONFIG_CTS_I2C_HOST */

	/* Firmware */
	count += scnprintf(buf + count, PAGE_SIZE - count,
		SEPARATION_LINE
		"%-32s: %c\n", "CFG_CTS_DRIVER_BUILTIN_FIRMWARE",
#ifdef CFG_CTS_DRIVER_BUILTIN_FIRMWARE
		'Y'
#else
		'N'
#endif
	);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"%-32s: %c\n", "CFG_CTS_FIRMWARE_IN_FS",
#ifdef CFG_CTS_FIRMWARE_IN_FS
		'Y'
#else
		'N'
#endif
	);
#ifdef CFG_CTS_FIRMWARE_IN_FS
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"%-32s: "CFG_CTS_FIRMWARE_FILEPATH"\n",
		"CFG_CTS_FIRMWARE_FILEPATH");
#endif /* CFG_CTS_FIRMWARE_IN_FS */

	/* Input device & features */
	count += scnprintf(buf + count, PAGE_SIZE - count,
		SEPARATION_LINE
		"%-32s: %c\n", "CONFIG_CTS_SLOTPROTOCOL",
#ifdef CONFIG_CTS_SLOTPROTOCOL
		 'Y'
#else
		 'N'
#endif
	);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"%-32s: %d\n", "CFG_CTS_MAX_TOUCH_NUM",
		CFG_CTS_MAX_TOUCH_NUM);

	count += scnprintf(buf + count, PAGE_SIZE - count,
		"%-32s: %c\n"
		"%-32s: %c\n"
		"%-32s: %c\n",
		"CFG_CTS_SWAP_XY",
#ifdef CFG_CTS_SWAP_XY
		'Y',
#else
		'N',
#endif
		"CFG_CTS_WRAP_X",
#ifdef CFG_CTS_WRAP_X
		'Y',
#else
		'N',
#endif
		"CFG_CTS_WRAP_Y",
#ifdef CFG_CTS_WRAP_Y
		'Y'
#else
		'N'
#endif
	);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"%-32s: %c\n", "CONFIG_CTS_GLOVE",
#ifdef CONFIG_CTS_GLOVE
	   'Y'
#else
	   'N'
#endif
	);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"%-32s: %c\n", "CFG_CTS_GESTURE",
#ifdef CFG_CTS_GESTURE
	   'Y'
#else
	   'N'
#endif
	);

	/* Charger detect */
	count += scnprintf(buf + count, PAGE_SIZE - count,
		SEPARATION_LINE
		"%-32s: %c\n", "CONFIG_CTS_CHARGER_DETECT",
#ifdef CONFIG_CTS_CHARGER_DETECT
	   'Y'
#else
	   'N'
#endif
	);

	/* Earjack detect */
	count += scnprintf(buf + count, PAGE_SIZE - count,
		SEPARATION_LINE
		"%-32s: %c\n", "CONFIG_CTS_EARJACK_DETECT",
#ifdef CONFIG_CTS_EARJACK_DETECT
	   'Y'
#else
	   'N'
#endif
	);

	/* ESD protection */
	count += scnprintf(buf + count, PAGE_SIZE - count,
		SEPARATION_LINE
		"%-32s: %c\n", "CONFIG_CTS_ESD_PROTECTION",
#ifdef CONFIG_CTS_ESD_PROTECTION
		'Y'
#else
		'N'
#endif
		);
#ifdef CONFIG_CTS_ESD_PROTECTION
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  %-30s: %uHz\n"
		"  %-30s: %u\n",
		"CFG_CTS_ESD_PROTECTION_CHECK_PERIOD",
		"CFG_CTS_ESD_FAILED_CONFIRM_CNT",
		CFG_CTS_ESD_PROTECTION_CHECK_PERIOD,
		CFG_CTS_ESD_FAILED_CONFIRM_CNT);
#endif /* CONFIG_CTS_ESD_PROTECTION */

	count += scnprintf(buf + count, PAGE_SIZE - count, SEPARATION_LINE);

	return count;
#undef SEPARATION_LINE
}

#if KERNEL_VERSION(4, 14, 0) > LINUX_VERSION_CODE
static DRIVER_ATTR(driver_config, S_IRUGO, driver_config_show, NULL);
#else
static DRIVER_ATTR_RO(driver_config);
#endif

static struct attribute *cts_driver_config_attrs[] = {
	&driver_attr_driver_config.attr,
	NULL
};

static const struct attribute_group cts_driver_config_group = {
	.name = "config",
	.attrs = cts_driver_config_attrs,
};

const struct attribute_group *cts_driver_config_groups[] = {
	&cts_driver_config_group,
	NULL,
};
#endif /* CONFIG_CTS_SYSFS */

#ifdef CONFIG_CTS_OF
const struct of_device_id cts_driver_of_match_table[] = {
	{.compatible = CFG_CTS_OF_DEVICE_ID_NAME,},
	{ },
};
MODULE_DEVICE_TABLE(of, cts_driver_of_match_table);
#endif /* CONFIG_CTS_OF */

int cts_driver_init(void)
{
	int ret = 0;

	cts_info("Chipone touch driver init, version: "CFG_CTS_DRIVER_VERSION);

/*zte_add*/
	if (get_tp_chip_id() == 0) {
		if ((tpd_cdev->tp_chip_id != TS_CHIP_MAX) && (tpd_cdev->tp_chip_id != TS_CHIP_CHIPONE)) {
			cts_err("%s:this tp is not used, return", __func__);
			return -EPERM;
		}
	}

#ifdef CONFIG_CTS_I2C_HOST
	cts_info(" - Register i2c driver");
	ret = i2c_add_driver(&cts_i2c_driver);
	if (ret) {
		cts_info("Register i2c driver failed %d(%s)",
			ret, cts_strerror(ret));
	}
#endif /* CONFIG_CTS_I2C_HOST */

#ifdef CONFIG_CTS_SPI_HOST
	cts_info(" - Register spi driver");
	ret = spi_register_driver(&cts_spi_driver);
	if (ret) {
		cts_info("Register spi driver failed %d(%s)",
			ret, cts_strerror(ret));
	}
#endif /* CONFIG_CTS_SPI_HOST */

	return 0;
}

void cts_driver_exit(void)
{
	cts_info("Exit");

#ifdef CONFIG_CTS_I2C_HOST
	cts_info(" - Delete i2c driver");
	i2c_del_driver(&cts_i2c_driver);
#endif /* CONFIG_CTS_I2C_HOST */

#ifdef CONFIG_CTS_SPI_HOST
	cts_info(" - Delete spi driver");
	spi_unregister_driver(&cts_spi_driver);
#endif /* CONFIG_CTS_SPI_HOST */
}

/*module_init(cts_driver_init);
module_exit(cts_driver_exit);*/

MODULE_DESCRIPTION("Chipone TDDI touchscreen Driver for QualComm platform");
MODULE_VERSION(CFG_CTS_DRIVER_VERSION);
MODULE_AUTHOR("Miao Defang <dfmiao@chiponeic.com>");
MODULE_LICENSE("GPL");
