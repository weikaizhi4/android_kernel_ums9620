
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/notifier.h>
#include <linux/mutex.h>
#include <sqc_common.h>


static BLOCKING_NOTIFIER_HEAD(sqc_notifier_list);

/**
 * sqc_register_notify - register a notifier callback whenever a usb change happens
 * @nb: pointer to the notifier block for the callback events.
 *
 * These changes are either USB devices or busses being added or removed.
 */
void sqc_register_notify(struct notifier_block *nb)
{
	blocking_notifier_chain_register(&sqc_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(sqc_register_notify);

/**
 * sqc_register_notify - unregister a notifier callback
 * @nb: pointer to the notifier block for the callback events.
 *
 * sqc_register_notify() must have been previously called for this function
 * to work properly.
 */
void sqc_unregister_notify(struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&sqc_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(sqc_unregister_notify);

void sqc_notify_changed(int type, void *val)
{
	blocking_notifier_call_chain(&sqc_notifier_list, type, val);
}
EXPORT_SYMBOL_GPL(sqc_notify_changed);



