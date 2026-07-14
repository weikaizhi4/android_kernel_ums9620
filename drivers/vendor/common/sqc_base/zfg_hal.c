#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/input.h>
#include <linux/proc_fs.h>
#include <linux/completion.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/power_supply.h>
#include <zfg_hal.h>


struct zfg_ops *zfg_hal_data = NULL;

struct zfg_ops * zfg_ops_get(void)
{
	return zfg_hal_data;
}
EXPORT_SYMBOL_GPL(zfg_ops_get);

int zfg_ops_register(struct zfg_ops *ops)
{

	if (!zfg_hal_data) {
		zfg_hal_data = ops;
		pr_info("zfg_ops_register init done!!! %p %p\n", zfg_hal_data, ops);
	} else {
		pr_info("zfg_ops_register repeatedly register!!!\n ");
		return -ENODEV;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(zfg_ops_register);