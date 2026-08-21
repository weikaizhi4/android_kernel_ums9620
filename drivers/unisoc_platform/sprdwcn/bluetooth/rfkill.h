/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SPRD_BT_RFKILL_H
#define __SPRD_BT_RFKILL_H

int rfkill_bluetooth_init(struct platform_device *pdev);
void rfkill_bluetooth_remove(void);

#endif
