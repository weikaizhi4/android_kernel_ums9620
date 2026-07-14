// License-Identifier: GPL-2.0
/*
 * File:shub_algo_cmd_notify.h
 *
 * Copyright (C) 2023 ZTE.
 *
 */

#include <linux/notifier.h>

enum {
	SHUB_ALGO_CMD_SET_BRIGHTNESS,
	SHUB_ALGO_CMD_SET_ACTUAL_FPS,
};

int register_shub_algo_cmd_notifier(struct notifier_block *nb);
int unregister_shub_algo_cmd_notifier(struct notifier_block *nb);
int call_shub_algo_cmd_notifier_chain(unsigned long val, void *v);
