/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SPRD_BT_TTY_H
#define __SPRD_BT_TTY_H

#define SPRD_BT_DST       3
#define SPRD_BT_CHANNEL   4
#define SPRD_BT_TX_BUFID  11
#define SPRD_BT_RX_BUFID  10

struct stty_init_data {
	char *name;
	u8 dst;
	u8 channel;
	u32 tx_bufid;
	u32 rx_bufid;
};

#endif
