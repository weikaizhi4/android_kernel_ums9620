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

#ifdef CONFIG_DRM_PANEL_NOTIFIER
#include <drm/drm_panel.h>
#endif

void gcore_suspend(void)
{
	struct gcore_dev *gdev = fn_data.gdev;

	if (gdev->tp_suspend)  {
		GTP_INFO("tp  already in suspend, return");
		return;
	}
	GTP_DEBUG("enter gcore suspend");

#if GCORE_WDT_RECOVERY_ENABLE
	cancel_delayed_work_sync(&fn_data.gdev->wdt_work);
#endif
#ifdef CONFIG_TOUCHSCREEN_POINT_REPORT_CHECK
	cancel_delayed_work_sync(&tpd_cdev->point_report_check_work);
#endif
	cancel_delayed_work_sync(&fn_data.gdev->fwu_work);
	cancel_delayed_work_sync(&tpd_cdev->send_cmd_work);

#ifdef CONFIG_ENABLE_GESTURE_WAKEUP
	if (gdev->gesture_wakeup_en) {
		/* gcore_idm_enter_gesture_mode(); */
		gcore_fw_event_notify(FW_GESTURE_ENABLE);
		enable_irq_wake(gdev->touch_irq);
		msleep(20);
		gdev->ts_stat = TS_SUSPEND;
		gcore_touch_release_all_point(fn_data.gdev->input_device);
		gdev->tp_suspend = true;
		return;
	}
#endif

	fn_data.gdev->ts_stat = TS_SUSPEND;
	gcore_touch_release_all_point(fn_data.gdev->input_device);
	gdev->irq_disable(gdev);
	gdev->tp_suspend = true;
	GTP_DEBUG("gcore suspend end");

}

void gcore_resume(void)
{
	struct gcore_dev *gdev = fn_data.gdev;

	if (!gdev->tp_suspend)  {
		GTP_INFO("tp  already resume, return");
		return;
	}
	GTP_DEBUG("enter gcore resume");
#ifdef CONFIG_ENABLE_GESTURE_WAKEUP
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
	gcore_request_firmware_update_work(NULL);
#else
	gcore_touch_release_all_point(fn_data.gdev->input_device);
#endif

	queue_delayed_work(tpd_cdev->tpd_wq, &tpd_cdev->send_cmd_work, msecs_to_jiffies(300));
	gdev->ts_stat = TS_NORMAL;
	gdev->tp_suspend = false;
	GTP_DEBUG("gcore resume end");
}

#ifdef CONFIG_DRM_PANEL_NOTIFIER
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
#endif

int gcore_touch_driver_init(void)
{
	GTP_DEBUG("touch driver init.");

	if (get_tp_chip_id() == 0) {
		if ((tpd_cdev->tp_chip_id != TS_CHIP_MAX) && (tpd_cdev->tp_chip_id != TS_CHIP_GCORE)) {
			GTP_ERROR("this tp is not used,return.\n");
			return -EPERM;
		}
	}
	if (tpd_cdev->TP_have_registered) {
		GTP_ERROR("TP have registered by other TP.\n");
		return -EPERM;
	}
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

