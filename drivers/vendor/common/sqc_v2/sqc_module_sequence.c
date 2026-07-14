#include <linux/delay.h>
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
#include <linux/firmware.h>
#include <linux/pinctrl/consumer.h>
#include <vendor/common/sqc_common.h>

#define TEB_INFO(fmt, arg...)           pr_info("<<TEB-INF>>[%s:%d] "fmt"", __func__, __LINE__, ##arg)
#define TEB_ERROR(fmt, arg...)          pr_info("<<TEB-ERR>>[%s:%d] "fmt"", __func__, __LINE__, ##arg)

#define HANDLE_DELAY_MS	0
#define SEQ_LIST_MAX	2


struct sqc_sequence_list{
	int (*funcp)(struct work_struct *work);
	bool force_exit;
};

struct sqc_sequence_load_t{
	struct delayed_work work;
	struct workqueue_struct *workqueue;
	struct mutex list_mutex;
	struct sqc_sequence_list list[SEQ_LIST_MAX];
	unsigned int wait_flag;
} ;

static struct sqc_sequence_load_t global_sqc_seq;

static DECLARE_WAIT_QUEUE_HEAD(sqc_seq_waitq);

void sqc_sequence_load_init(int (*func_handle)(struct work_struct *work),
				unsigned char num, bool force_exit)
{
	if (num >= SEQ_LIST_MAX) {
		TEB_ERROR("num is beyond max range");
		return;
	}

	mutex_lock(&global_sqc_seq.list_mutex);
	TEB_INFO("add list: %d\n", num);
	global_sqc_seq.list[num].funcp = func_handle;
	global_sqc_seq.list[num].force_exit = force_exit;
	mutex_unlock(&global_sqc_seq.list_mutex);
	

	wake_up_interruptible(&sqc_seq_waitq);
}
EXPORT_SYMBOL(sqc_sequence_load_init);

static void sqc_sequence_handle_work(struct work_struct *work)
{
	int retval = 0;
	bool force_exit = false;
	unsigned int handle_num = 0;
	int (*func_handle)(struct work_struct *work);

	while (true) {
		if (handle_num >= SEQ_LIST_MAX) {
			TEB_INFO("handle finished !!!\n");
			return;
		}

		mutex_lock(&global_sqc_seq.list_mutex);
		func_handle = global_sqc_seq.list[handle_num].funcp;
		force_exit = global_sqc_seq.list[handle_num].force_exit;
		mutex_unlock(&global_sqc_seq.list_mutex);

		TEB_INFO("####### handling list: %d\n", handle_num);

		/*TEB_INFO("lock + lock + lock + lock\n");*/
		if (func_handle != NULL) {
			retval = func_handle(NULL);
			handle_num++;
			if (!retval && force_exit) {
				TEB_INFO("####### force_exit, handle finished!!!\n");
				return;
			} else {
				continue;
			}
		}
		
		TEB_INFO("####### wait -------------!!!\n");
		retval = wait_event_interruptible_timeout(sqc_seq_waitq,
					global_sqc_seq.wait_flag, msecs_to_jiffies(1000));

		if (retval <= 0) {
			TEB_INFO("interruptible found, wait event failed!!!\n");
		}
	}
}

static int sqc_sequence_init(void)
{
	global_sqc_seq.workqueue = create_singlethread_workqueue("sqc_seq_wq");
	INIT_DELAYED_WORK(&global_sqc_seq.work, sqc_sequence_handle_work);

	queue_delayed_work(global_sqc_seq.workqueue,
			&global_sqc_seq.work,
			msecs_to_jiffies(HANDLE_DELAY_MS));

	return 0;
}

static void sqc_sequence_exit(void)
{

	pr_info("sqc_sequence_exit!\n");
}

subsys_initcall(sqc_sequence_init);
module_exit(sqc_sequence_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("zte.charger");
MODULE_DESCRIPTION("sqc charger misc daemon");

