/*
 * writen by ZTE bsp light, 20220622.
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/device.h>

struct zcard_det_config_type {
	int gpio;
	unsigned int active_low;  //0 active high, 1 active low
};

static struct zcard_det_config_type zcard_det_config;

static int zcard_det_gpio_init( void )
{
	enum of_gpio_flags flags;
	struct device_node *node;

	node = of_find_node_with_property(NULL, "zcard-holder-det-gpios");
	if (node) {
		zcard_det_config.gpio = of_get_named_gpio_flags(node, "zcard-holder-det-gpios", 0, &flags);
		if (gpio_is_valid(zcard_det_config.gpio)) {
			zcard_det_config.active_low = flags & OF_GPIO_ACTIVE_LOW;
			return 0;
		} else
                        pr_err("%s: error get zcard-holder-det-gpios\n", __func__);
	}

	node = of_find_node_with_property(NULL, "cd-gpios");
	if (node) {
		zcard_det_config.gpio = of_get_named_gpio_flags(node, "cd-gpios", 0, &flags);
		if (gpio_is_valid(zcard_det_config.gpio)) {
			zcard_det_config.active_low = flags & OF_GPIO_ACTIVE_LOW;
			return 0;
		} else
                        pr_err("%s: error get cd-gpios\n", __func__);
	} else
		pr_err("%s: cannot get zte_card_holder node\n", __func__);

	return -ENODEV;
}

static ssize_t zcard_det_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff)
{
	unsigned int gpio_value = 0;

	if (gpio_is_valid(zcard_det_config.gpio))
		gpio_value = gpio_get_value(zcard_det_config.gpio);
	return sprintf(buff, "%d\n", (gpio_value ^ zcard_det_config.active_low));
}
static struct kobj_attribute zcard_det_attr = __ATTR(zcard_det, S_IRUGO, zcard_det_show, NULL);

static int init_zcard_det_sys_node()
{
	int ret = 0;
	struct kobject *kobj = kobject_create_and_add("zcard_det", NULL);
	if(kobj == NULL){
		pr_err("%s: kobject_create_and_add  zcard_det failed!!\n", __func__);
		return -EINVAL;
	}

	ret = sysfs_create_file(kobj, &zcard_det_attr.attr);
	if(ret)
		pr_err("%s: sysfs_create_file  zcard_det failed!!\n", __func__);

	return ret;
}

static int __init zte_card_holder_det_init (void)
{
	int ret = 0;

	pr_info("%s: start!!\n", __func__);

	ret = zcard_det_gpio_init();
	if (unlikely(ret))
		pr_info("%s: zcard_det_config.gpio_init failed!!\n", __func__);
	else
		init_zcard_det_sys_node();

	pr_info("%s: finished!!\n", __func__);
	return ret;
}

late_initcall(zte_card_holder_det_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ZTE light Inc.");
