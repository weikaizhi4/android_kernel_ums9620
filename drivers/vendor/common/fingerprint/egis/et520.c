/*
 * Simple synchronous userspace interface to SPI devices
 *
 * Copyright (C) 2006 SWAPP
 * Andrea Paterniani <a.paterniani@swapp-eng.it>
 * Copyright (C) 2007 David Brownell (simplification, cleanup)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
*
*  et512.spi.c
*  Date: 2016/03/16
*  Version: 0.9.0.1
*  Revise Date:  2016/03/24
*  Copyright (C) 2007-2016 Egis Technology Inc.
* -----------------  version history ------------------------
* <Author>		<Data>			<Desc>
* Kevin			20191015          1st verison
* -----------------------------------------------------------
*
*/


#include <linux/interrupt.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#endif

#include <linux/gpio.h>
#include <linux/mutex.h>
#include <linux/list.h>

//#include <mach/gpio.h>
//#include <plat/gpio-cfg.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#ifdef CONFIG_PM_WAKELOCKS
#include <linux/pm_wakeup.h>
#else
#include <linux/wakelock.h>
#endif

//#include <mt_spi_hal.h>
//#include <mt_spi.h>

//#include "mtk_spi.h"

//#include <mt-plat/mt_gpio.h>
#include "et520.h"
//#include "navi_input.h"

#define EGIS_NAVI_INPUT // 1:open ; 0:close
#ifdef EGIS_NAVI_INPUT
#include "ets_navi_input.h"
#endif

#define EDGE_TRIGGER_FALLING    0x0
#define EDGE_TRIGGER_RISING     0x1
#define LEVEL_TRIGGER_LOW       0x2
#define LEVEL_TRIGGER_HIGH      0x3

extern void mt_spi_enable_master_clk(struct spi_device *spidev);
extern void mt_spi_disable_master_clk(struct spi_device *spidev);

/*
 * FPS interrupt table
 */

struct interrupt_desc fps_ints = {0 , 0, "BUT0" , 0};


unsigned int bufsiz = 4096;

int gpio_irq;
int request_irq_done = 0;
int egistec_parse_dt_done = 0;
static int egistec_parse_dt(struct egistec_data *data);

#ifdef CONFIG_PM_WAKELOCKS
	struct wakeup_source ets_wake_lock;
#else
	struct wake_lock     ets_wake_lock;
#endif

int egistec_platformFree(struct egistec_data *egistec);

struct ioctl_cmd {
	int int_mode;
	int detect_period;
	int detect_threshold;
};

static void delete_device_node(void);
static struct egistec_data *g_data = NULL;

DECLARE_BITMAP(minors, N_SPI_MINORS);
LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);

static struct of_device_id egistec_match_table[] = {
	{ .compatible = "egis,egistec_pd",},
	{},
};

static struct of_device_id et512_spi_of_match[] = {
	{ .compatible = "egis,egistec_spi", },
	{}
};


MODULE_DEVICE_TABLE(of, et512_spi_of_match);

// add for dual sensor start
/*
static struct of_device_id fpswitch_match_table[] = {
	{ .compatible = "fp_id,fp_id",},
	{},
};
*/
//add for dual sensor end
MODULE_DEVICE_TABLE(of, egistec_match_table);


/* ------------------------------ Interrupt -----------------------------*/
/*
 * Interrupt description
 */

#define FP_INT_DETECTION_PERIOD  10
#define FP_DETECTION_THRESHOLD   10

static DECLARE_WAIT_QUEUE_HEAD(interrupt_waitq);
static void spi_clk_enable(u8 bonoff)
{
	if (bonoff) {
		egislog_d("enable spi clk\n");
		if (g_data->spi == NULL) {
			egislog_e("the spi pointer is NULL, please check the dts of spi\n");
		} else {
			mt_spi_enable_master_clk(g_data->spi);
		}
	} else {
		egislog_d("disable spi clk\n");
		if (g_data->spi == NULL) {
			egislog_e("the spi pointer is NULL, please check the dts of spi\n");
		} else {
			mt_spi_disable_master_clk(g_data->spi);
		}
	}
}

static void egistec_power_onoff(struct egistec_data *egistec, int bonoff)
{
	if (bonoff) {
		egislog_d("power on\n");
		pinctrl_select_state(egistec->pinctrl_gpios, egistec->pins_power_high);
	} else {
		egislog_d("power off\n");
		pinctrl_select_state(egistec->pinctrl_gpios, egistec->pins_power_low);
	}
}

/*
 *	FUNCTION NAME.
 *		interrupt_timer_routine
 *
 *	FUNCTIONAL DESCRIPTION.
 *		basic interrupt timer inital routine
 *
 *	ENTRY PARAMETERS.
 *		gpio - gpio address
 *
 *	EXIT PARAMETERS.
 *		Function Return
 */

void interrupt_timer_routine(unsigned long _data)
{
	struct interrupt_desc *bdata = (struct interrupt_desc *)_data;

	egislog_i("interrupt count = %d \n", bdata->int_count);
	if (bdata->int_count >= bdata->detect_threshold) {
		bdata->finger_on = 1;
		egislog_i("FPS triggered !!!!!!!\n");
	} else {
		egislog_i("FPS not triggered !!!!!!!\n");
	}

	bdata->int_count = 0;
	wake_up_interruptible(&interrupt_waitq);
}


static irqreturn_t fp_eint_func(int irq, void *dev_id)
{
	if (!fps_ints.int_count) {
		mod_timer(&fps_ints.timer,jiffies + msecs_to_jiffies(fps_ints.detect_period));
	}

	fps_ints.int_count++;
	egislog_d("fps_ints.int_count=%d\n",fps_ints.int_count);
#ifdef CONFIG_PM_WAKELOCKS
	__pm_wakeup_event(&ets_wake_lock, msecs_to_jiffies(1500));
#else
	wake_lock_timeout(&ets_wake_lock, msecs_to_jiffies(1500));
#endif
	return IRQ_HANDLED;
}

static irqreturn_t fp_eint_func_ll(int irq , void *dev_id)
{
	egislog_i("fp_eint_func_ll\n");
	fps_ints.finger_on = 1;
	/*fps_ints.int_count = 0;*/
	disable_irq_nosync(gpio_irq);
	fps_ints.drdy_irq_flag = DRDY_IRQ_DISABLE;
	wake_up_interruptible(&interrupt_waitq);
#ifdef CONFIG_PM_WAKELOCKS
	__pm_wakeup_event(&ets_wake_lock, msecs_to_jiffies(1500));
#else
	wake_lock_timeout(&ets_wake_lock, msecs_to_jiffies(1500));
#endif
	return IRQ_RETVAL(IRQ_HANDLED);
}


/*
 *	FUNCTION NAME.
 *		Interrupt_Init
 *
 *	FUNCTIONAL DESCRIPTION.
 *		button initial routine
 *
 *	ENTRY PARAMETERS.
 *		int_mode - determine trigger mode
 *			EDGE_TRIGGER_FALLING     0x0
 *			EDGE_TRIGGER_RAISING     0x1
 *			LEVEL_TRIGGER_LOW        0x2
 *			LEVEL_TRIGGER_HIGH       0x3
 *
 *	EXIT PARAMETERS.
 *		Function Return int
 */

int Interrupt_Init(struct egistec_data *egistec,int int_mode,int detect_period,int detect_threshold)
{
	int err = 0;
	int status = 0;
    struct device_node *node = NULL;
	egislog_d("%s mode = %d period = %d threshold = %d\n",__func__,int_mode,detect_period,detect_threshold);
	egislog_i("%s request_irq_done = %d gpio_irq = %d  pin = %d  \n",__func__,request_irq_done,gpio_irq, egistec->irqPin);

	fps_ints.detect_period = detect_period;
	fps_ints.detect_threshold = detect_threshold;
	fps_ints.int_count = 0;
	fps_ints.finger_on = 0;
	if (request_irq_done == 0)
	{
		pinctrl_select_state(egistec->pinctrl_gpios, egistec->pins_irq);
		/*gpio_irq = gpio_to_irq(egistec->irqPin);*/
		/*printk("ttt-fp_irq number %d\n", node? 1:2);*/
		node = of_find_compatible_node(NULL, NULL,"egis,egistec_pd");

		if (node){
				gpio_irq = irq_of_parse_and_map(node, 0);
				egislog_i("fp_irq number %d\n", gpio_irq);
		} else {
				egislog_e("node = of_find_matching_node fail error  \n");
		}
		if (gpio_irq < 0) {
			egislog_e("gpio_to_irq failed\n");
			status = gpio_irq;
			goto done;
		}

		egislog_i("flag current: %d disable: %d enable: %d\n",
		fps_ints.drdy_irq_flag, DRDY_IRQ_DISABLE, DRDY_IRQ_ENABLE);

		if (int_mode == EDGE_TRIGGER_RISING){
			egislog_i("EDGE_TRIGGER_RISING\n");
			err = request_irq(gpio_irq, fp_eint_func,IRQ_TYPE_EDGE_RISING,"fp_detect-eint", egistec);
			if (err){
				egislog_e("request_irq failed==========\n");
				}
		}
		else if (int_mode == EDGE_TRIGGER_FALLING){
			egislog_i("EDGE_TRIGGER_FALLING\n");
			err = request_irq(gpio_irq, fp_eint_func,IRQ_TYPE_EDGE_FALLING,"fp_detect-eint", egistec);
			if (err){
				egislog_e("request_irq failed==========\n");
				}
		}
		else if (int_mode == LEVEL_TRIGGER_LOW) {
			egislog_i("LEVEL_TRIGGER_LOW\n");
			err = request_irq(gpio_irq, fp_eint_func_ll,IRQ_TYPE_LEVEL_LOW,"fp_detect-eint", egistec);
			if (err){
				egislog_e("request_irq failed==========\n");
				}
		}
		else if (int_mode == LEVEL_TRIGGER_HIGH){
			egislog_i("LEVEL_TRIGGER_HIGH\n");
			err = request_irq(gpio_irq, fp_eint_func_ll,IRQ_TYPE_LEVEL_HIGH,"fp_detect-eint", egistec);
			if (err){
				egislog_e("request_irq failed==========\n");
				}
		}
		egislog_d("request_irq return: %d, err=%d\n", gpio_irq, err);

		fps_ints.drdy_irq_flag = DRDY_IRQ_ENABLE;
		enable_irq_wake(gpio_irq);
		request_irq_done = 1;
	}

	if (fps_ints.drdy_irq_flag == DRDY_IRQ_DISABLE){
		fps_ints.drdy_irq_flag = DRDY_IRQ_ENABLE;
		enable_irq_wake(gpio_irq);
		enable_irq(gpio_irq);
	}
done:
	return 0;
}

/*
 *	FUNCTION NAME.
 *		Interrupt_Free
 *	FUNCTIONAL DESCRIPTION.
 *		free all interrupt resource
 *	EXIT PARAMETERS.
 *		Function Return int
 */

int Interrupt_Free(struct egistec_data *egistec)
{
	egislog_d("DONE\n");
	fps_ints.finger_on = 0;
	fps_ints.int_count = 0;

	if (fps_ints.drdy_irq_flag == DRDY_IRQ_ENABLE) {
		egislog_i(" (DISABLE IRQ)\n");
		disable_irq_nosync(gpio_irq);

		del_timer_sync(&fps_ints.timer);
		fps_ints.drdy_irq_flag = DRDY_IRQ_DISABLE;
	}
	return 0;
}

/*
 *	FUNCTION NAME.
 *		fps_interrupt_re d
 *	FUNCTIONAL DESCRIPTION.
 *		FPS interrupt read status
 *	ENTRY PARAMETERS.
 *		wait poll table structure
 *	EXIT PARAMETERS.
 *		Function Return int
 */

unsigned int fps_interrupt_poll(struct file *file, struct poll_table_struct *wait)
{
	unsigned int mask = 0;

	poll_wait(file, &interrupt_waitq, wait);
	if (fps_ints.finger_on) {
		mask |= POLLIN | POLLRDNORM;
	}
	return mask;
}

void fps_interrupt_abort(void)
{
	egislog_i(" Enter \n");
	fps_ints.finger_on = 0;
	wake_up_interruptible(&interrupt_waitq);
}

/*-------------------------------------------------------------------------*/
static void egistec_reset(struct egistec_data *egistec)
{
	egislog_i(" Enter \n");

	#ifdef CONFIG_OF
	pinctrl_select_state(egistec->pinctrl_gpios, egistec->pins_reset_low);
	mdelay(25);
	pinctrl_select_state(egistec->pinctrl_gpios, egistec->pins_reset_high);
	mdelay(50);
	#endif
}

static ssize_t egistec_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	/*Implement by vendor if needed*/
	return 0;
}

static ssize_t egistec_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	/*Implement by vendor if needed*/
	return 0;
}

static long egistec_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{

	int retval = 0;
	struct egistec_data *egistec;
	struct ioctl_cmd data;
	int status = 0;

	memset(&data, 0, sizeof(data));
	egislog_i("cmd = 0x%X \n", cmd);
	egistec = filp->private_data;

	if(!egistec_parse_dt_done){
		status = egistec_parse_dt(egistec);
		if (status != 0) {
			egislog_e( "platforminit failed\n");
		}
	}

	switch (cmd) {
	case INT_TRIGGER_INIT:

		if (copy_from_user(&data, (int __user *)arg, sizeof(data))) {
			retval = -EFAULT;
		goto done;
		}
		egislog_d("fp_ioctl >>> fp Trigger function init\n");
		retval = Interrupt_Init(egistec, data.int_mode,data.detect_period,data.detect_threshold);
		egislog_i("fp_ioctl trigger init = %x\n", retval);
	break;

	case FP_POWER_ONOFF:
			egislog_d("fp_ioctl ioc->opcode == FP_POWER_ONOFF --");
			if (copy_from_user(&data, (int __user *)arg, sizeof(data))) {
			retval = -EFAULT;
			goto done;
			}
			egistec_power_onoff(egistec, data.int_mode);
		goto done;
	case FP_SENSOR_RESET:
			egislog_i("fp_ioctl ioc->opcode == FP_SENSOR_RESET \n");
			egistec_reset(egistec);
		goto done;
	case INT_TRIGGER_CLOSE:
			egislog_i("fp_ioctl <<< fp Trigger function close\n");
			retval = Interrupt_Free(egistec);
			egislog_d("fp_ioctl trigger close = %x\n", retval);
		goto done;
	case INT_TRIGGER_ABORT:
			egislog_i("fp_ioctl <<< fp Trigger function close\n");
			fps_interrupt_abort();
		goto done;
	case FP_FREE_GPIO:
			egislog_i("fp_ioctl <<< FP_FREE_GPIO  \n");
			egistec_platformFree(egistec);
		goto done;

	case FP_SPICLK_ENABLE:
			egislog_i("fp_ioctl <<< FP_SPICLK_ENABLE  \n");
			spi_clk_enable(1);
		goto done;
	case FP_SPICLK_DISABLE:
			egislog_i("fp_ioctl <<< FP_SPICLK_DISABLE  \n");
			spi_clk_enable(0);
		goto done;
	case DELETE_DEVICE_NODE:
			egislog_i("fp_ioctl <<< DELETE_DEVICE_NODE  \n");
			delete_device_node();
		goto done;
	default:
	retval = -ENOTTY;
	break;
	}
done:
	egislog_d("done\n");
	return (retval);
}

#ifdef CONFIG_COMPAT
static long egistec_compat_ioctl(struct file *filp,
	unsigned int cmd,
	unsigned long arg)
{
	return egistec_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#else
#define egistec_compat_ioctl NULL
#endif /* CONFIG_COMPAT */

static int egistec_open(struct inode *inode, struct file *filp)
{
	struct egistec_data *egistec;
	int    status = -ENXIO;
	egislog_d("Enter \n");

	mutex_lock(&device_list_lock);

	list_for_each_entry(egistec, &device_list, device_entry)
	{
		if (egistec->devt == inode->i_rdev) {
			status = 0;
			break;
		}
	}
	if (status == 0) {
		if (egistec->buffer == NULL) {
			egistec->buffer = kmalloc(bufsiz, GFP_KERNEL);
			if (egistec->buffer == NULL) {
				egislog_a("open/ENOMEM\n");
				status = -ENOMEM;
			}
		}
		if (status == 0) {
			egistec->users++;
			filp->private_data = egistec;
			nonseekable_open(inode, filp);
		}
	} else {
		egislog_d("nothing for minor %d\n", iminor(inode));
	}
	mutex_unlock(&device_list_lock);
	return status;
}

static int egistec_release(struct inode *inode, struct file *filp)
{
	struct egistec_data *egistec;
	egislog_d("Enter\n");

	mutex_lock(&device_list_lock);

	egistec = filp->private_data;
	filp->private_data = NULL;

	/* last close? */
	egistec->users--;
	egislog_d("egistec->users = %d\n", egistec->users);
	if (egistec->users == 0) {
		int	dofree;

		if (egistec != NULL) {
			egistec_platformFree(egistec);
		}

		kfree(egistec->buffer);
		egistec->buffer = NULL;

		/* ... after we unbound from the underlying device? */
		spin_lock_irq(&egistec->spi_lock);
		dofree = (egistec->pd == NULL);
		spin_unlock_irq(&egistec->spi_lock);

		if (dofree) {
			kfree(egistec);
			egislog_d("kfree done\n");
		}
	}
	mutex_unlock(&device_list_lock);
	egislog_d("release success, exit\n");
	return 0;

}

int egistec_platformFree(struct egistec_data *egistec)
{
	int status = 0;
	egislog_d("Enter \n");
	if (egistec != NULL) {
#ifndef IRQ_RELEASE
		if (request_irq_done==1)
		{
			if (gpio_irq > 0) {
				Interrupt_Free(egistec);
				egislog_d("free gpio_irq = %d\n", gpio_irq);
				free_irq(gpio_irq, egistec);
				request_irq_done = 0;
				gpio_irq = 0;
			}
		}
#endif
		if (egistec->pinctrl_gpios && egistec_parse_dt_done == 1) {
			devm_pinctrl_put(egistec->pinctrl_gpios);
			egistec_parse_dt_done = 0;
			egislog_d("pinctrl release successful\n");
		}
	}
	egislog_d("successful status=%d\n", status);
	return status;
}

static int egistec_parse_dt(struct egistec_data *data)
{
	int errorno = 0;
#ifdef CONFIG_OF
	int ret;
	struct device_node *navnode = NULL;
	struct platform_device *pnavdev = NULL;
	struct device_node *node = NULL;
	struct platform_device *pdev = NULL;

	egislog_i(" from dts pinctrl\n");

	navnode = of_find_compatible_node(NULL, NULL, "zte_fp_nav");
	if (navnode) {
		egislog_d("egistec find zte_fp_nav node\n");
		pnavdev = of_find_device_by_node(navnode);
		if (pnavdev) {
			egislog_d("egistec find zte_fp_nav device");
			if (data) {
				data->pnavdev = pnavdev;
			} else {
				egislog_e("data is null");
				return errorno;
			}
		} else {
			egislog_e("no find zte_fp_nav device");
		}
	} else {
		egislog_e("no find the zte_fp_nav node");
	}

	node = of_find_compatible_node(NULL, NULL, "egis,egistec_pd");
	if (node) {
		pdev = of_find_device_by_node(node);
		egislog_d("egistec find node enter \n");
		if (pdev) {
			data->pinctrl_gpios = devm_pinctrl_get(&pdev->dev);
			if (IS_ERR(data->pinctrl_gpios)) {
				ret = PTR_ERR(data->pinctrl_gpios);
				egislog_e("can't find fingerprint pinctrl\n");
				return ret;
			}
			data->pins_reset_high = pinctrl_lookup_state(data->pinctrl_gpios, "reset_high");
			if (IS_ERR(data->pins_reset_high)) {
				ret = PTR_ERR(data->pins_reset_high);
				egislog_e("can't find fingerprint pinctrl reset_high\n");
				return ret;
			}
			data->pins_reset_low = pinctrl_lookup_state(data->pinctrl_gpios, "reset_low");
			if (IS_ERR(data->pins_reset_low)) {
				ret = PTR_ERR(data->pins_reset_low);
				egislog_e("can't find fingerprint pinctrl reset_low\n");
				return ret;
			}
			data->pins_power_high = pinctrl_lookup_state(data->pinctrl_gpios, "power_high");
			if (IS_ERR(data->pins_power_high)) {
				ret = PTR_ERR(data->pins_power_high);
				egislog_e("can't find fingerprint pinctrl power_high\n");
				return ret;
			}
			data->pins_power_low = pinctrl_lookup_state(data->pinctrl_gpios, "power_low");
			if (IS_ERR(data->pins_power_low)) {
				ret = PTR_ERR(data->pins_power_low);
				egislog_e("can't find fingerprint pinctrl power_low\n");
				return ret;
			}
			data->pins_irq= pinctrl_lookup_state(data->pinctrl_gpios, "irq-init");
			if (IS_ERR(data->pins_irq)) {
				ret = PTR_ERR(data->pins_irq);
				egislog_e("can't find fingerprint pinctrl irq-init\n");
				return ret;
			}

			egislog_i("get pinctrl success!\n");
		} else {
			egislog_e("platform device is null\n");
			return errorno;
		}
	} else {
		egislog_e(" device node is null\n");
		return errorno;
	}
#endif
	egislog_d("is successful\n");
	egistec_parse_dt_done = 1;
	return errorno;
}

static const struct file_operations egistec_fops = {
	.owner = THIS_MODULE,
	.write = egistec_write,
	.read = egistec_read,
	.unlocked_ioctl = egistec_ioctl,
	.compat_ioctl = egistec_compat_ioctl,
	.open = egistec_open,
	.release = egistec_release,
	.llseek = no_llseek,
	.poll = fps_interrupt_poll
};
/*-------------------------------------------------------------------------*/
static struct class *egistec_class;
/*-------------------------------------------------------------------------*/
static int egistec_probe(struct platform_device *pdev);
static int egistec_remove(struct platform_device *pdev);
typedef struct {
	struct spi_device      *spi;
	struct class           *class;
	struct device          *device;
	/*struct cdev            cdev; */
	dev_t                  devno;
	u8                     *huge_buffer;
	size_t                 huge_buffer_size;
	struct input_dev       *input_dev;
} et512_data_t;

/* -------------------------------------------------------------------- */
static int et512_spi_probe(struct spi_device *spi)
{
	/*struct device *dev = &spi->dev;*/
	int error = 0;
	et512_data_t *et512 = NULL;
	/* size_t buffer_size; */
	egislog_d("Enter\n");

	et512 = kzalloc(sizeof(*et512), GFP_KERNEL);
	if (!et512) {
		egislog_e("failed to allocate memory for struct et512_data\n");
		return -ENOMEM;
	}

	if (!spi) {
		egislog_e("spi is NULL\n");
		return -EFAULT;
	}

	spi_set_drvdata(spi, et512);
	egislog_d("spi_set_drvdata id over\n");

	if (g_data == NULL) {
		egislog_e("g_data is NULL, please check egistec_probe\n");
		return -EFAULT;
	}
	g_data->spi = spi;
	egislog_d("exit\n");
	return error;
}

/* -------------------------------------------------------------------- */
static int et512_spi_remove(struct spi_device *spi)
{
	et512_data_t *et512 = spi_get_drvdata(spi);
	spi_clk_enable(0);
	egislog_d("remove\n");
/*
	et512_manage_sysfs(et512, spi, false);
	et512_sleep(et512, true);
	cdev_del(&et512->cdev);
	unregister_chrdev_region(et512->devno, 1);
	et512_cleanup(et512, spi);
*/
	kfree(et512);
	return 0;
}

static struct spi_driver spi_driver = {
	.driver = {
		.name	= "et512",
		.owner	= THIS_MODULE,
		.of_match_table = et512_spi_of_match,
        .bus	= &spi_bus_type,
	},
	.probe	= et512_spi_probe,
	.remove	= et512_spi_remove
};

/*
struct spi_board_info spi_board_devs[] __initdata = {
    [0] = {
        .modalias="et512",
        .bus_num = 0,
        //.chip_select = 1,
        .chip_select = 3,
        .mode = SPI_MODE_0,
    },
};
*/

static struct platform_driver egistec_driver = {
	.driver = {
		.name		= "et512",
		.owner		= THIS_MODULE,
		.of_match_table = egistec_match_table,
	},
    .probe =    egistec_probe,
    .remove =   egistec_remove,
};


static int egistec_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct egistec_data *egistec = dev_get_drvdata(dev);

	egislog_i(" Enter \n");
	free_irq(gpio_irq, NULL);

#ifdef CONFIG_PM_WAKELOCKS
	wakeup_source_trash(&ets_wake_lock);
#else
	wake_lock_destroy(&ets_wake_lock);
#endif

#ifdef EGIS_NAVI_INPUT
	uinput_egis_destroy(egistec);
	sysfs_egis_destroy(egistec);
#endif

	del_timer_sync(&fps_ints.timer);
	request_irq_done = 0;
	kfree(egistec);
    return 0;
}


static int egistec_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct egistec_data *egistec;
	int status = 0;
	unsigned long minor;

	egislog_d("probe entey\n");
	BUILD_BUG_ON(N_SPI_MINORS > 256);
	status = register_chrdev(ET512_MAJOR, "et512", &egistec_fops);
	if (status < 0) {
			egislog_e("register_chrdev error.\n");
			return status;
	}

	egistec_class = class_create(THIS_MODULE, "et512");
	if (IS_ERR(egistec_class)) {
		egislog_e(" class_create error.\n");
		unregister_chrdev(ET512_MAJOR, egistec_driver.driver.name);
		return PTR_ERR(egistec_class);
	}

	/* Allocate driver data */
	egistec = kzalloc(sizeof(*egistec), GFP_KERNEL);
	if (egistec== NULL) {
		egislog_e(" - Failed to kzalloc\n");
		return -ENOMEM;
	}

/*
	// device tree call
	if (pdev->dev.of_node) {
		status = egistec_parse_dt(egistec);
		if (status) {
			egislog_e(" - Failed to parse DT\n");
			goto egistec_probe_parse_dt_failed;
		}
	}
*/

	/* Initialize the driver data */
	egistec->pd = pdev;
	g_data = egistec;

	egislog_d("egistec_module_probe\n");
	spin_lock_init(&egistec->spi_lock);
	mutex_init(&egistec->buf_lock);
	mutex_init(&device_list_lock);

	INIT_LIST_HEAD(&egistec->device_entry);

	fps_ints.drdy_irq_flag = DRDY_IRQ_DISABLE;

#ifdef CONFIG_PM_WAKELOCKS
	wakeup_source_init(&ets_wake_lock, "egistec wakelock");
#else
	wake_lock_init(&ets_wake_lock, WAKE_LOCK_SUSPEND, "egistec wakelock");
#endif

	/*
	 * If we can allocate a minor number, hook up this device.
	 * Reusing minors is fine so long as udev or mdev is working.
	 */
	mutex_lock(&device_list_lock);
	minor = find_first_zero_bit(minors, N_SPI_MINORS);
	if (minor < N_SPI_MINORS) {
		struct device *fdev;
		egistec->devt = MKDEV(ET512_MAJOR, minor);
		fdev = device_create(egistec_class, &pdev->dev, egistec->devt,
					egistec, "esfp0");
		status = IS_ERR(fdev) ? PTR_ERR(fdev) : 0;
	} else {
		egislog_e("no minor number available!\n");
		status = -ENODEV;
	}
	if (status == 0) {
		set_bit(minor, minors);
		list_add(&egistec->device_entry, &device_list);
	}

	mutex_unlock(&device_list_lock);

	if (status == 0){
		dev_set_drvdata(dev, egistec);
	}
	else {
		goto egistec_probe_failed;
	}

	/*gpio_request later*/
	/*egistec_reset(egistec);*/

	fps_ints.drdy_irq_flag = DRDY_IRQ_DISABLE;

	/* the timer is for ET310 */
	setup_timer(&fps_ints.timer, interrupt_timer_routine,(unsigned long)&fps_ints);
	add_timer(&fps_ints.timer);

/*
	struct device_node *node = NULL;
	int value;

	node = of_find_compatible_node(NULL, NULL, "goodix,goodix-fp");
	mt_spi_enable_master_clk(gf_dev->spi);
*/

#ifdef EGIS_NAVI_INPUT
	/*
	 * William Add.
	 */
	sysfs_egis_init(egistec);
	uinput_egis_init(egistec);
#endif

	egislog_i("probe success\n");

	request_irq_done = 0;
	return status;

egistec_probe_failed:

#ifdef CONFIG_PM_WAKELOCKS
	wakeup_source_trash(&ets_wake_lock);
#else
	wake_lock_destroy(&ets_wake_lock);
#endif

	device_destroy(egistec_class, egistec->devt);
	class_destroy(egistec_class);

	/*egistec_probe_platformInit_failed:*/
	/*egistec_probe_parse_dt_failed:*/
	kfree(egistec);
	egislog_e("egistec probe is failed\n");
	return status;
}

static void delete_device_node(void)
{
	egislog_i("Enter \n");
	/*del_timer_sync(&fps_ints.timer);*/
	/*spi_clk_enable(0);*/
	device_destroy(egistec_class, g_data->devt);
	egislog_d("device_destroy \n");
	list_del(&g_data->device_entry);
	egislog_d("list_del \n");
	class_destroy(egistec_class);
	egislog_d("class_destroy\n");
	/*spi_unregister_driver(&spi_driver);*/
	/*DEBUG_PRINT("spi_unregister_driver\n");*/
	unregister_chrdev(ET512_MAJOR, egistec_driver.driver.name);
	egislog_d("unregister_chrdev\n");
	g_data = NULL;
	/*platform_driver_unregister(&egistec_driver);*/
	/*DEBUG_PRINT("platform_driver_unregister\n");*/
}


static int __init egis512_init(void)
{
	int status = 0;

/*
	int sim_gpio = -1;
	int sim_val = 1;
	struct device_node *node = NULL;
	node = of_find_compatible_node(NULL, NULL, "mediatek,fp_id");
	sim_gpio = of_get_named_gpio(node, "fp-id-gpios", 0);
	sim_val = __gpio_get_value(sim_gpio);
	egislog_d("Get FP_ID from GPIO_PIN is FP_ID = %d.\n", sim_val);
	if(sim_val==1){
		egislog_i("Need to register egistec FP driver\n");
	}else{
		egislog_e("Don't need to register egistec FP driver\n");
		return status;
	}
*/

	egislog_d("entery\n");

	status = platform_driver_register(&egistec_driver);
	if(status < 0){
		egislog_e("register spi driver fail\n");
		return -EINVAL;
	}
	egislog_d("platform_driver_register ok\n");
/*	fpc_irq_platform_device = platform_device_register_simple(
							FPC_IRQ_DEV_NAME,
							0,
							NULL,
							0);

	if (IS_ERR(fpc_irq_platform_device))
		return PTR_ERR(fpc_irq_platform_device); */
/*
	status= spi_register_board_info(spi_board_devs, ARRAY_SIZE(spi_board_devs));

	if (status)
	{
		egislog_e("spi register board info\n");
		return -EINVAL;
	}
	egislog_d("spi register board ok\n");
*/
	if (spi_register_driver(&spi_driver))
	{
		egislog_e("register spi driver fail%s\n", __func__);
		return -EINVAL;
	}
	egislog_d("spi_register_driver ok\n");
	egislog_d("module init exit = %d\n", status);
	return status;
}

static void __exit egis512_exit(void)
{

	platform_driver_unregister(&egistec_driver);
	spi_unregister_driver(&spi_driver);
}

module_init(egis512_init);
module_exit(egis512_exit);

MODULE_AUTHOR("Wang YuWei, <robert.wang@egistec.com>");
MODULE_DESCRIPTION("SPI Interface for ET512");
MODULE_LICENSE("GPL");
