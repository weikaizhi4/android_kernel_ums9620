/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UNISOC_BT_LOG_H
#define __UNISOC_BT_LOG_H

#include <linux/device.h>

#define dev_unisoc_bt_err(dev, fmt, ...) \
	do { if (dev) dev_err(dev, fmt, ##__VA_ARGS__); else pr_err(fmt, ##__VA_ARGS__); } while (0)
#define dev_unisoc_bt_info(dev, fmt, ...) \
	do { if (dev) dev_info(dev, fmt, ##__VA_ARGS__); else pr_info(fmt, ##__VA_ARGS__); } while (0)

#endif
