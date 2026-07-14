/*
 * GalaxyCore touchscreen driver
 *
 * Copyright (C) 2021 GalaxyCore Incorporated
 *
 * Copyright (C) 2021 Neo Chen <neo_chen@gcoreinc.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "gcore_drv_common.h"

#ifdef CONFIG_DRM_MSM
#include <drm/drm_panel.h>
#elif defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#endif
extern void gcore_ex_mode_recovery(void);
/*extern int gcore_idm_enter_gesture_mode(void); */

#if defined(CONFIG_ENABLE_GESTURE_WAKEUP) && defined(CONFIG_GESTURE_SPECIAL_INT)

int gcore_enable_irq_wake(struct gcore_dev *gdev)
{
	GTP_DEBUG("enable irq wake");

	if (gdev->ges_irq) {
		enable_irq_wake(gdev->ges_irq);
		return 0;
	}

	return -EPERM;
}
EXPORT_SYMBOL(gcore_enable_irq_wake);

void gcore_ges_irq_enable(struct gcore_dev *gdev)
{
	unsigned long flags;

	GTP_DEBUG("enable ges irq");

	spin_lock_irqsave(&gdev->irq_flag_lock, flags);

	if (gdev->ges_irq_en == false) {
		gdev->ges_irq_en = true;
		spin_unlock_irqrestore(&gdev->irq_flag_lock, flags);
		enable_irq(gdev->ges_irq);
	} else if (gdev->ges_irq_en == true) {
		spin_unlock_irqrestore(&gdev->irq_flag_lock, flags);
		GTP_ERROR("gesture Eint already enabled!");
	} else {
		spin_unlock_irqrestore(&gdev->irq_flag_lock, flags);
		GTP_ERROR("Invalid irq_flag %d!", gdev->irq_flag);
	}
	/*GTP_DEBUG("Enable irq_flag=%d",g_touch.irq_flag); */

}

void gcore_ges_irq_disable(struct gcore_dev *gdev)
{
	unsigned long flags;

	GTP_DEBUG("disable ges irq");

	spin_lock_irqsave(&gdev->irq_flag_lock, flags);

	if (gdev->ges_irq_en == false) {
		spin_unlock_irqrestore(&gdev->irq_flag_lock, flags);
		GTP_ERROR("gesture Eint already disable!");
		return;
	}

	gdev->ges_irq_en = false;

	spin_unlock_irqrestore(&gdev->irq_flag_lock, flags);

	disable_irq_nosync(gdev->ges_irq);
}
EXPORT_SYMBOL(gcore_ges_irq_disable);

#endif

void gcore_suspend(void)
{
	struct gcore_dev *gdev = fn_data.gdev;

	if (gdev->tp_suspend)  {
		GTP_INFO("tp  already in suspend, return");
		return;
	}
	GTP_INFO("enter gcore suspend");
#ifdef GCORE_WDT_RECOVERY_ENABLE
	gdev->ts_stat = 1;
	cancel_delayed_work_sync(&fn_data.gdev->wdt_work);
#endif

#if defined(CONFIG_ENABLE_GESTURE_WAKEUP)
	if (fn_data.gdev->gesture_wakeup_en) {
		/* gcore_idm_enter_gesture_mode(); */
		gcore_fw_event_notify(FW_GESTURE_ENABLE);
		enable_irq_wake(gdev->touch_irq);
		msleep(20);
		gdev->tp_suspend = true;
		return;
	}
#endif
	gdev->irq_disable(gdev);
	gdev->tp_suspend = true;
	msleep(20);
}

void gcore_resume(void)
{
	struct gcore_dev *gdev = fn_data.gdev;

	if (!gdev->tp_suspend)  {
		GTP_INFO("tp  already resume, return");
		return;
	}
	GTP_INFO("enter gcore resume");

#if defined(CONFIG_ENABLE_GESTURE_WAKEUP)
	if (gdev->gesture_wakeup_en) {
		GTP_INFO("disable irq wake");
		disable_irq_wake(gdev->touch_irq);
		gdev->irq_enable(gdev);
	} else {
		gdev->irq_enable(gdev);
	}
#else
	gdev->irq_enable(gdev);
#endif
#ifdef CONFIG_GCORE_AUTO_UPDATE_FW_HOSTDOWNLOAD
	queue_delayed_work(gdev->fwu_workqueue, &gdev->fwu_work, msecs_to_jiffies(100));
#endif
#ifdef GCORE_WDT_RECOVERY_ENABLE
	gdev->ts_stat = 0;
#endif
	gcore_touch_release_all_point(gdev->input_device);
	gcore_ex_mode_recovery();
	gdev->tp_suspend = false;
	if (mutex_is_locked(&gdev->transfer_lock)) {
		mutex_unlock(&gdev->transfer_lock);
		GTP_DEBUG("touch is locked, resume unlock");
	}
}

#ifdef CONFIG_DRM_MSM
int gcore_ts_drm_notifier_callback(struct notifier_block *self, unsigned long event, void *data)
{
	unsigned int blank;

	struct drm_panel_notifier *evdata = data;

	if (!evdata)
		return 0;

	blank = *(int *)(evdata->data);
	GTP_DEBUG("event = %d, blank = %d", event, blank);

	if (!(event == DRM_PANEL_EARLY_EVENT_BLANK || event == DRM_PANEL_EVENT_BLANK)) {
		GTP_DEBUG("event(%lu) do not need process\n", event);
		return 0;
	}

	switch (blank) {
	case DRM_PANEL_BLANK_POWERDOWN:
/* feiyu.zhu modify for tp suspend/resume */
		if (event == DRM_PANEL_EARLY_EVENT_BLANK) {
			gcore_suspend();
		}
		break;

	case DRM_PANEL_BLANK_UNBLANK:
		if (event == DRM_PANEL_EVENT_BLANK) {
			gcore_resume();
		}
		break;

	default:
		break;
	}
	return 0;
}
#elif defined(CONFIG_FB)
int gcore_ts_fb_notifier_callback(struct notifier_block *self, unsigned long event, void *data)
{
	unsigned int blank;

	struct fb_event *evdata = data;

	if (!evdata)
		return 0;

	blank = *(int *)(evdata->data);
	GTP_INFO("event = %d, blank = %d", event, blank);

	if (!(event == FB_EARLY_EVENT_BLANK || event == FB_EVENT_BLANK)) {
		GTP_DEBUG("event(%lu) do not need process\n", event);
		return 0;
	}

	switch (blank) {
	case FB_BLANK_POWERDOWN:
		if (event == FB_EARLY_EVENT_BLANK) {
			gcore_suspend();
		}
		break;

	case FB_BLANK_UNBLANK:
		if (event == FB_EVENT_BLANK) {
			gcore_resume();
		}
		break;

	default:
		break;
	}
	return 0;

}
#endif

int gcore_touch_driver_init(void)
{
	GTP_DEBUG("touch driver init.");

	if (gcore_touch_bus_init()) {
		GTP_ERROR("bus init fail!");
		return -EPERM;
	}

	return 0;
}

/* should never be called */
void  gcore_touch_driver_exit(void)
{
	gcore_touch_bus_exit();
}

