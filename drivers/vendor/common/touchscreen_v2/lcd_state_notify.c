
#include <linux/notifier.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/kernel.h>

#ifdef CONFIG_TOUCHSCREEN_LCD_NOTIFY
static BLOCKING_NOTIFIER_HEAD(lcd_notifier_list);
static BLOCKING_NOTIFIER_HEAD(tpd_notifier_list);

/* ####################################################*/
int lcd_notifier_register_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&lcd_notifier_list, nb);
}
EXPORT_SYMBOL(lcd_notifier_register_client);

int lcd_notifier_unregister_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&lcd_notifier_list, nb);
}
EXPORT_SYMBOL(lcd_notifier_unregister_client);


int lcd_notifier_call_chain(unsigned long val)
{
	return blocking_notifier_call_chain(&lcd_notifier_list, val, NULL);
}
EXPORT_SYMBOL(lcd_notifier_call_chain);

/* ####################################################*/
int tpd_notifier_register_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&tpd_notifier_list, nb);
}
EXPORT_SYMBOL(tpd_notifier_register_client);

int tpd_notifier_unregister_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&tpd_notifier_list, nb);
}
EXPORT_SYMBOL(tpd_notifier_unregister_client);

int tpd_notifier_call_chain(unsigned long val)
{
	return blocking_notifier_call_chain(&tpd_notifier_list, val, NULL);
}
EXPORT_SYMBOL_GPL(tpd_notifier_call_chain);

int __init lcd_state_notify_init(void)
{
	pr_notice("%s into\n", __func__);
	return 0;
}

static void __exit lcd_state_notify_exit(void)
{
	pr_notice("%s into\n", __func__);
}

module_init(lcd_state_notify_init);
module_exit(lcd_state_notify_exit);

MODULE_AUTHOR("zte");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("lcd state notify");
#endif

