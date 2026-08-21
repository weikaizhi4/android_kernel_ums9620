/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SPRD_BT_SITM_H
#define __SPRD_BT_SITM_H

#include <linux/types.h>

typedef int (*frame_complete_cb)(u8 *data, size_t len);

int sitm_init(void);
void sitm_cleanup(void);
int sitm_write(const u8 *buf, int count, frame_complete_cb frame_complete);

#endif
