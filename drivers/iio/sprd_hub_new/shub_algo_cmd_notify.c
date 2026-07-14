// License-Identifier: GPL-2.0
/*
 * File:shub_algo_cmd_notify.c
 *
 * Copyright (C) 2023 ZTE.
 *
 */

#include <linux/notifier.h>
#include <linux/export.h>

static BLOCKING_NOTIFIER_HEAD(shub_algo_cmd_notifier_list);

int register_shub_algo_cmd_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&shub_algo_cmd_notifier_list, nb);
}

int unregister_shub_algo_cmd_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&shub_algo_cmd_notifier_list, nb);
}

int call_shub_algo_cmd_notifier_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&shub_algo_cmd_notifier_list, val, v);
}
EXPORT_SYMBOL(call_shub_algo_cmd_notifier_chain);
