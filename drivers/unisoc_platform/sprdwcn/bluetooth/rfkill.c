// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rfkill.h>

#include <misc/wcn_integrate_platform.h>

#include "rfkill.h"
#include "unisoc_bt_log.h"

extern struct device *ttyBT_dev;

static struct rfkill *bt_rfk;

static int bluetooth_set_power(void *data, bool blocked)
{
	int ret;

	if (blocked)
		ret = stop_marlin(WCN_MARLIN_BLUETOOTH);
	else
		ret = start_marlin(WCN_MARLIN_BLUETOOTH);

	dev_unisoc_bt_info(ttyBT_dev, "rfkill blocked=%d ret=%d\n", blocked, ret);
	return ret;
}

static const struct rfkill_ops bluetooth_rfkill_ops = {
	.set_block = bluetooth_set_power,
};

int rfkill_bluetooth_init(struct platform_device *pdev)
{
	int ret;

	bt_rfk = rfkill_alloc("bluetooth", &pdev->dev, RFKILL_TYPE_BLUETOOTH,
			      &bluetooth_rfkill_ops, NULL);
	if (!bt_rfk)
		return -ENOMEM;

	rfkill_init_sw_state(bt_rfk, false);
	ret = rfkill_register(bt_rfk);
	if (ret) {
		rfkill_destroy(bt_rfk);
		bt_rfk = NULL;
	}
	return ret;
}

void rfkill_bluetooth_remove(void)
{
	if (!bt_rfk)
		return;
	rfkill_unregister(bt_rfk);
	rfkill_destroy(bt_rfk);
	bt_rfk = NULL;
}
