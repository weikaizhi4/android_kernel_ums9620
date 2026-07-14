/* FS-Cache statistics viewing interface
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static unsigned int smartpa_cnt = 0;
static unsigned int smartpa_stat = 0;

/*
 * add the smartpa's state
 */
void smartpa_count_add(void)
{
	smartpa_cnt++;
}
EXPORT_SYMBOL(smartpa_count_add);

/*
 * update the smartpa's state
 */
void smartpa_stat_update(void)
{
	smartpa_stat++;
}
EXPORT_SYMBOL(smartpa_stat_update);

/*
 * show the smart's state
 */
static int smartpa_stat_show(struct seq_file *m, void *v)
{
	pr_info("%s, smartpa_cnt = %d smartpa_stat = %d", __func__, smartpa_cnt, smartpa_stat);
	if (smartpa_cnt > 0) {
		seq_printf(m, "%d\n", (smartpa_cnt == smartpa_stat));
	} else {
		seq_printf(m, "%d\n", 0);
	}

	return 0;
}

static int smartpa_stat_open(struct inode *inode, struct file *file)
{
	return single_open(file, smartpa_stat_show, NULL);
}

const struct file_operations smartpa_stat_fops = {
	.open		= smartpa_stat_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release   = single_release,
};

/*
 * initialise the /proc/driver/smartpa
 */
static int __init smartpa_stat_init(void)
{
	pr_info("%s enter", __func__);

	if (!proc_create_data("driver/smartpa", S_IRUGO, NULL, &smartpa_stat_fops, NULL)) {
		pr_err("%s: Failed to create /proc/driver/smartpa", __func__);
		return -ENOMEM;
	}

	pr_info("%s exit", __func__);
	return 0;
}

/*
 * clean up the /proc/driver/smartpa
 */
static void smartpa_stat_cleanup(void)
{
	remove_proc_entry("driver/smartpa", NULL);
}

module_init(smartpa_stat_init);
module_exit(smartpa_stat_cleanup);

MODULE_DESCRIPTION("ZTE smartpa board test");
MODULE_AUTHOR("weiguohua <wei.guohua@zte.com.cn>");
MODULE_LICENSE("GPL");