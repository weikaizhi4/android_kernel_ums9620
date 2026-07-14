#define pr_fmt(fmt)	"cpu_userate: %s: " fmt, __func__

#include <linux/cpumask.h>
#include <linux/kernel_stat.h>
#include <linux/tick.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/time.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pmic-voter.h>
#include <linux/sched/loadavg.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <vendor/common/zte_misc.h>
#include <linux/version.h>

#if (KERNEL_VERSION(4, 1, 0) <= LINUX_VERSION_CODE)
#define KERNEL_ABOVE_4_1_0
#endif

struct cpu_userate_info {
	struct device	*dev;
	struct workqueue_struct	*cpu_userate_workqueue;
	struct delayed_work		cpu_userate_work;
	struct notifier_block		nb;
	u64 time_interval_ms;
	u64 user_save;
	u64 nice_save;
	u64 system_save;
	u64 idle_save;
	u64 iowait_save;
	u64 irq_save;
	u64 softirq_save;
	u64 steal_save;
	atomic_t percent;
	struct votable		*fcc_votable;
	u32 *rate_config;
	u32 rate_length;
	u32 thresholds;
	u32 thresholds_clear;
	u32 vote_on_flag;
};

struct cpu_userate_info cpu_userate;
extern int screen_on;


#define CPU_RATE_DEBUG_ON 0
#define TIME_INTERVAL_MS 30000

#define CPU_RATE_VOTER		"CPU_RATE_VOTER"

#define DELTA_VALUE(x, y) (((x) > (y)) ? ((x) - (y)) : (0))

#define OF_READ_PROPERTY(store, dt_property, retval, default_val)	\
do {											\
	retval = of_property_read_u32(np,			\
					"cpurate," dt_property,		\
					&store);					\
											\
	if (retval == -EINVAL) {					\
		retval = 0;							\
		store = default_val;					\
	} else if (retval) {							\
		pr_info("Error reading " #dt_property	\
				" property rc = %d\n", retval);	\
	}										\
	pr_info("config: " #dt_property				\
				" property: [%d]\n", store);		\
} while (0)

#define OF_READ_PROPERTY_STRINGS(store, dt_property, retval, default_val)	\
do {											\
	retval = of_property_read_string(np,		\
					"cpurate," dt_property,		\
					&(store));				\
											\
	if (retval == -EINVAL) {					\
		retval = 0;							\
		store = default_val;					\
	} else if (retval) {							\
		pr_info("Error reading " #dt_property	\
				" property rc = %d\n", retval);	\
		return retval;						\
	}										\
	pr_info("config: " #dt_property				\
				" property: [%s]\n", store);		\
} while (0)

#define OF_READ_ARRAY_PROPERTY(prop_data, prop_length, prop_size, dt_property, retval) \
do { \
	if (of_find_property(np, "cpurate," dt_property, &prop_length)) { \
		prop_data = kzalloc(prop_length, GFP_KERNEL); \
		retval = of_property_read_u32_array(np, "cpurate," dt_property, \
				 (u32 *)prop_data, prop_length / sizeof(u32)); \
		if (retval) { \
			retval = -EINVAL; \
			pr_info("Error reading " #dt_property \
				" property rc = %d\n", retval); \
			kfree(prop_data); \
			prop_data = NULL; \
			prop_length = 0; \
			return retval; \
		} else { \
			prop_length = prop_length / prop_size; \
		} \
	} else { \
		retval = -EINVAL; \
		prop_data = NULL; \
		prop_length = 0; \
		pr_info("Error geting " #dt_property \
				" property rc = %d\n", retval); \
		return retval; \
	} \
	pr_info("config: " #dt_property \
				" prop_length: [%d]\n", prop_length);\
} while (0)



#ifndef arch_irq_stat_cpu
#define arch_irq_stat_cpu(cpu) 0
#endif
#ifndef arch_irq_stat
#define arch_irq_stat() 0
#endif

#ifdef arch_idle_time

static u64 get_idle_time(int cpu)
{
	u64 idle;

	idle = kcpustat_cpu(cpu).cpustat[CPUTIME_IDLE];
	if (cpu_online(cpu) && !nr_iowait_cpu(cpu))
		idle += arch_idle_time(cpu);
	return idle;
}

static u64 get_iowait_time(int cpu)
{
	u64 iowait;

	iowait = kcpustat_cpu(cpu).cpustat[CPUTIME_IOWAIT];
	if (cpu_online(cpu) && nr_iowait_cpu(cpu))
		iowait += arch_idle_time(cpu);
	return iowait;
}

#else

static u64 get_idle_time(int cpu)
{
	u64 idle, idle_usecs = -1ULL;

	if (cpu_online(cpu))
		idle_usecs = get_cpu_idle_time_us(cpu, NULL);

	if (idle_usecs == -1ULL)
		/* !NO_HZ or cpu offline so we can rely on cpustat.idle */
		idle = kcpustat_cpu(cpu).cpustat[CPUTIME_IDLE];
	else
		idle = idle_usecs * NSEC_PER_USEC;

	return idle;
}

static u64 get_iowait_time(int cpu)
{
	u64 iowait, iowait_usecs = -1ULL;

	if (cpu_online(cpu))
		iowait_usecs = get_cpu_iowait_time_us(cpu, NULL);

	if (iowait_usecs == -1ULL)
		/* !NO_HZ or cpu offline so we can rely on cpustat.iowait */
		iowait = kcpustat_cpu(cpu).cpustat[CPUTIME_IOWAIT];
	else
		iowait = iowait_usecs * NSEC_PER_USEC;

	return iowait;
}

#endif

static int cpu_rate_get_prop_by_name(const char *name, enum power_supply_property psp, int *data)
{
	struct power_supply *psy = NULL;
	union power_supply_propval val = {0, };
	int rc = 0;

	if (name == NULL) {
		pr_info("psy name is NULL!!\n");
		goto failed_loop;
	}

	psy = power_supply_get_by_name(name);
	if (!psy) {
		pr_info("get %s psy failed!!\n", name);
		goto failed_loop;
	}

	rc = power_supply_get_property(psy,
				psp, &val);
	if (rc < 0) {
		pr_info("Failed to set %s property:%d rc=%d\n", name, psp, rc);
		return rc;
	}

	*data = val.intval;

#ifdef KERNEL_ABOVE_4_1_0
	power_supply_put(psy);
#endif

	return 0;

failed_loop:
	return -EINVAL;
}


static int show_cpu_rate(struct seq_file *p, void *v)
{
	unsigned long avnrun[3];

	get_avenrun(avnrun, FIXED_1/200, 0);

	seq_printf(p, "cpu(%d): %d%%\n",
						num_online_cpus(), atomic_read(&cpu_userate.percent));

	seq_printf(p, "loadavg: %lu.%02lu %lu.%02lu %lu.%02lu\n",
									LOAD_INT(avnrun[0]), LOAD_FRAC(avnrun[0]),
									LOAD_INT(avnrun[1]), LOAD_FRAC(avnrun[1]),
									LOAD_INT(avnrun[2]), LOAD_FRAC(avnrun[2]));

	return 0;
}

static ssize_t cpu_rate_write(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	char *kbuf = NULL;
	int time_ms = 0;

	kbuf = kzalloc(count + 2, GFP_KERNEL);
	if (!kbuf) {
		pr_info("kzalloc failed!!\n");
		goto ExitLoop;
	}

	if (copy_from_user(kbuf, user_buf, count)) {
		pr_info("copy_from_user failed!!\n");
		goto ExitLoop;
	}

	if(sscanf(kbuf, "%d", &time_ms) != 1) {
		pr_info("sscanf failed!!\n");
		goto ExitLoop;
	}

	time_ms = (time_ms < 100) ? 100 : time_ms;

	cpu_userate.time_interval_ms = time_ms;

	/*restart work*/
	cancel_delayed_work_sync(&cpu_userate.cpu_userate_work);

	if (cpu_userate.time_interval_ms) {
		queue_delayed_work(cpu_userate.cpu_userate_workqueue,
								&cpu_userate.cpu_userate_work,
								msecs_to_jiffies(cpu_userate.time_interval_ms));
	}

ExitLoop:
	kfree(kbuf);

	return count;
}


static int cpu_rate_open(struct inode *inode, struct file *file)
{
	return single_open(file, show_cpu_rate, NULL);
}

static const struct file_operations proc_cpu_rate_operations = {
	.open		= cpu_rate_open,
	.read		= seq_read,
	.write 		= cpu_rate_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void cpu_userate_handler_work(struct work_struct *work)
{
	int i = 0, thermal_level = 0, screen_on = 0, usb_present = 0;
	u64 user, nice, system, idle, iowait, irq, softirq, steal;
	u64 numerator, denominator, percent_now;
	struct timespec64 boottime;
	static u64 last_time = 0;

	cpu_rate_get_prop_by_name("usb",
				POWER_SUPPLY_PROP_PRESENT, &usb_present);

	if (!usb_present) {
		pr_info("usb not present now, return\n");
		vote(cpu_userate.fcc_votable, CPU_RATE_VOTER, false, 0);
		return;
	}

	get_monotonic_boottime64(&boottime);

	user = nice = system = idle = 0;
	iowait = irq = softirq = steal = 0;
	numerator = denominator = 0;

	if (!cpu_userate.fcc_votable) {
		cpu_userate.fcc_votable = find_votable("FCC");
		if (!cpu_userate.fcc_votable) {
			pr_info("Couldn't find FCC votable\n");
		}
	}

	for_each_possible_cpu(i) {
		user += kcpustat_cpu(i).cpustat[CPUTIME_USER];
		nice += kcpustat_cpu(i).cpustat[CPUTIME_NICE];
		system += kcpustat_cpu(i).cpustat[CPUTIME_SYSTEM];
		idle += get_idle_time(i);
		iowait += get_iowait_time(i);
		irq += kcpustat_cpu(i).cpustat[CPUTIME_IRQ];
		softirq += kcpustat_cpu(i).cpustat[CPUTIME_SOFTIRQ];
		steal += kcpustat_cpu(i).cpustat[CPUTIME_STEAL];
	}

	user = nsec_to_clock_t(user);
	nice = nsec_to_clock_t(nice);
	system = nsec_to_clock_t(system);
	idle = nsec_to_clock_t(idle);
	iowait = nsec_to_clock_t(iowait);
	irq = nsec_to_clock_t(irq);
	softirq = nsec_to_clock_t(softirq);
	steal = nsec_to_clock_t(steal);

#if CPU_RATE_DEBUG_ON
	pr_info("%lld, %lld, %lld, %lld, %lld, %lld, %lld, %lld\n",
						user, nice, system, idle, iowait, irq, softirq, steal);
#endif

	numerator = DELTA_VALUE(user, cpu_userate.user_save)
				+ DELTA_VALUE(nice, cpu_userate.nice_save)
				+ DELTA_VALUE(system, cpu_userate.system_save)
				+ DELTA_VALUE(iowait, cpu_userate.iowait_save)
				+ DELTA_VALUE(irq, cpu_userate.irq_save)
				+ DELTA_VALUE(softirq, cpu_userate.softirq_save)
				+ DELTA_VALUE(steal, cpu_userate.steal_save);

	denominator = numerator + DELTA_VALUE(idle, cpu_userate.idle_save);

#if CPU_RATE_DEBUG_ON
	pr_info("numerator: %llu, denominator: %llu\n", numerator, denominator);

	pr_info("time delta: %d\n", boottime.tv_sec - last_time);
#endif

	percent_now = (denominator) ? div64_u64(numerator * 100, denominator) : 1;

	cpu_userate.user_save = user;
	cpu_userate.nice_save = nice;
	cpu_userate.system_save = system;
	cpu_userate.idle_save = idle;
	cpu_userate.iowait_save = iowait;
	cpu_userate.irq_save = irq;
	cpu_userate.softirq_save = softirq;
	cpu_userate.steal_save = steal;

	last_time = (u64)boottime.tv_sec;

	cpu_rate_get_prop_by_name("battery",
				POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT, &thermal_level);

	screen_on = zte_misc_get_node_val("screen_on");

	thermal_level = thermal_level % cpu_userate.rate_length;

	thermal_level = (thermal_level > (cpu_userate.rate_length - 1)) ? 0 : thermal_level;

	pr_info("rate: %lld%%, thermal_level[%d]: %d, screen_on: %d\n", percent_now,
				thermal_level, cpu_userate.rate_config[thermal_level], screen_on);

	if (cpu_userate.fcc_votable) {
		if (screen_on > 0) {
			if (!cpu_userate.vote_on_flag) {
				if (percent_now >= cpu_userate.thresholds) {
					pr_info("vote fcc true\n");
					vote(cpu_userate.fcc_votable, CPU_RATE_VOTER,
								true, cpu_userate.rate_config[thermal_level]);
					cpu_userate.vote_on_flag = true;
				} else {
					pr_info("vote fcc false\n");
					vote(cpu_userate.fcc_votable, CPU_RATE_VOTER, false, 0);
					cpu_userate.vote_on_flag = false;
				}
			} else {
				if (percent_now <= cpu_userate.thresholds_clear) {
					pr_info("vote fcc false\n");
					vote(cpu_userate.fcc_votable, CPU_RATE_VOTER, false, 0);
					cpu_userate.vote_on_flag = false;
				} else {
					pr_info("vote fcc true\n");
					vote(cpu_userate.fcc_votable, CPU_RATE_VOTER,
								true, cpu_userate.rate_config[thermal_level]);
					cpu_userate.vote_on_flag = true;
				}
			}
		} else {
			pr_info("vote fcc false\n");
			vote(cpu_userate.fcc_votable, CPU_RATE_VOTER, false, 0);
			cpu_userate.vote_on_flag = false;
		}
	}

	atomic_set(&cpu_userate.percent, (int)percent_now);

	if (cpu_userate.time_interval_ms) {
		queue_delayed_work(cpu_userate.cpu_userate_workqueue,
								&cpu_userate.cpu_userate_work,
								msecs_to_jiffies(cpu_userate.time_interval_ms));
	}
}

static int cpu_rate_usb_status_changed(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct power_supply *psy = data;
	const char *name = NULL;

	if (event != PSY_EVENT_PROP_CHANGED) {
		return NOTIFY_DONE;
	}

	if (delayed_work_pending(&cpu_userate.cpu_userate_work))
		return NOTIFY_OK;

#ifdef KERNEL_ABOVE_4_1_0
	name = psy->desc->name;
#else
	name = psy->name;
#endif

	if ((strcmp(name, "battery") == 0)
			|| (strcmp(name, "usb") == 0)) {
		/* pr_policy("Notify, update status\n"); */
		queue_delayed_work(cpu_userate.cpu_userate_workqueue,
								&cpu_userate.cpu_userate_work,
								msecs_to_jiffies(100));
	}

	return NOTIFY_OK;
}


static int cpu_rate_register_notifier(struct cpu_userate_info *cpu_userate)
{
	int rc = 0;

	if (!cpu_userate) {
		return -EINVAL;
	}

	cpu_userate->nb.notifier_call = cpu_rate_usb_status_changed;

	rc = power_supply_reg_notifier(&cpu_userate->nb);
	if (rc < 0) {
		pr_info("Couldn't register psy notifier rc = %d\n", rc);
		return -EINVAL;
	}

	return 0;
}


static int cpu_rate_parse_dt(struct cpu_userate_info *cpurate_info)
{
	int retval = 0, i = 0;
	struct device_node *np = cpurate_info->dev->of_node;

	OF_READ_PROPERTY(cpurate_info->thresholds,
			"thresholds", retval, 25);

	OF_READ_PROPERTY(cpurate_info->thresholds_clear,
			"thresholds-clear", retval, 20);

	OF_READ_ARRAY_PROPERTY(cpurate_info->rate_config,
								cpurate_info->rate_length,
								sizeof(u32),
								"rate-data", retval);

	for (i = 0; i < cpurate_info->rate_length; i++) {
		pr_info("rate[%u]: %u\n", i, cpurate_info->rate_config[i]);
	}

	return 0;
}

static int cpu_rate_probe(struct platform_device *pdev)
{
	pr_info("cpu rate calc Driver Probe Begin.....\n");

	cpu_userate.dev = &pdev->dev;

	platform_set_drvdata(pdev, &cpu_userate);

	if (cpu_rate_parse_dt(&cpu_userate) < 0) {
		pr_info("Parse dts failed!!!!\n");
		goto parse_dt_failed;
	}

	cpu_userate.cpu_userate_workqueue = create_singlethread_workqueue("cpu_userate_thread");
	INIT_DELAYED_WORK(&cpu_userate.cpu_userate_work, cpu_userate_handler_work);

	atomic_set(&cpu_userate.percent, 0);

	cpu_userate.time_interval_ms = TIME_INTERVAL_MS;

	if (cpu_rate_register_notifier(&cpu_userate)) {
		pr_info("register notifier failed!!!!\n");
		goto parse_dt_failed;
	}

	queue_delayed_work(cpu_userate.cpu_userate_workqueue,
								&cpu_userate.cpu_userate_work,
								msecs_to_jiffies(30000));

	proc_create("cpu_rate", 0, NULL, &proc_cpu_rate_operations);


	pr_info("cpu rate calc Driver Probe success!!!\n");

parse_dt_failed:

	return 0;
}

static int cpu_rate_remove(struct platform_device *pdev)
{
	/*struct cpu_userate_info *info = platform_get_drvdata(pdev);*/

	cancel_delayed_work(&cpu_userate.cpu_userate_work);

	return 0;
}

static const struct of_device_id match_table[] = {
	{ .compatible = "zte,cpu-rate-calc", },
	{ },
};


static struct platform_driver cpu_rate_driver = {
	.driver		= {
		.name		= "zte,cpu-rate-calc",
		.owner		= THIS_MODULE,
		.of_match_table	= match_table,
	},
	.probe		= cpu_rate_probe,
	.remove		= cpu_rate_remove,
};

static inline int cpu_rate_init(void)
{
	pr_info("cpu rate calc Driver init!!!\n");

	return platform_driver_register(&cpu_rate_driver);
}

static inline void cpu_rate_exit(void)
{
	pr_info("cpu rate calc Driver exit!!!\n");

	platform_driver_unregister(&cpu_rate_driver);
}


late_initcall_sync(cpu_rate_init);
module_exit(cpu_rate_exit);


