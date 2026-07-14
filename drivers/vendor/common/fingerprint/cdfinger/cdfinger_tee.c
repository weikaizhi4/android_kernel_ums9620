/****************************

       cdfinger_tee.c

****************************/


#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
#include <linux/pm_wakeup.h>
#else
#include <linux/wakelock.h>
#endif
#include <linux/kthread.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/semaphore.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/signal.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/fb.h>
#include <linux/notifier.h>
#include <linux/cdev.h>

#include "cdfinger_tee.h"

#ifdef CONFIG_MTK_CLKMGR
#include "mach/mt_clkmgr.h"
#endif

static struct cdfinger_data {
#if defined(USE_SPI_BUS)
	struct spi_device *spi;
#elif defined(USE_PLATFORM_BUS)
	struct platform_device *spi;
#endif

	/*zte_fp_nav device*/
	struct platform_device *nav_dev;

	struct mutex buf_lock;
	unsigned int irq;
	int irq_enabled;
	u32 chip_id;

	u32 vdd_ldo_enable;
	u32 vio_ldo_enable;
	u32 config_spi_pin;
#ifdef DTS_USE_GPIO_PIN
	int pwr_gpio; //power gpio number
	int irq_gpio;
	int reset_gpio;
#endif

	/* 0 gpio , 1 ldo */
	int power_type;
	struct regulator *fp_reg;
	/* get the voltage from dts file */
	int power_voltage;

	struct pinctrl *fps_pinctrl;
	struct pinctrl_state *fps_reset_high;
	struct pinctrl_state *fps_reset_low;
	struct pinctrl_state *fps_power_on;
	struct pinctrl_state *fps_power_off;
	struct pinctrl_state *fps_vio_on;
	struct pinctrl_state *fps_vio_off;
	struct pinctrl_state *cdfinger_spi_miso;
	struct pinctrl_state *cdfinger_spi_mosi;
	struct pinctrl_state *cdfinger_spi_sck;
	struct pinctrl_state *cdfinger_spi_cs;
	struct pinctrl_state *cdfinger_irq;

	int thread_wakeup;
	int process_interrupt;
	int key_report;
	enum work_mode device_mode;
	struct input_dev *cdfinger_inputdev;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
	struct wakeup_source *cdfinger_lock;
#else
	struct wake_lock cdfinger_lock;
#endif

	struct task_struct *cdfinger_thread;
	struct fasync_struct *async_queue;
	uint8_t cdfinger_interrupt;
	struct notifier_block notifier;
	struct mt_spi_t * cdfinger_ms;
	int irq_request;
	int gpio_requested;
	int fops_ref;
}*g_cdfinger;

static DECLARE_WAIT_QUEUE_HEAD(waiter);
static DECLARE_WAIT_QUEUE_HEAD(cdfinger_waitqueue);

//clk will follow platform... pls check this when you poarting
//static void cdfinger_enable_clk(void)
/*static void cdfinger_enable_clk(struct cdfinger_data *cdfinger,u8 bonoff)
{
	struct mtk_spi *mdata = NULL;
	int ret = -1;
	static int count = 0;
	mdata = spi_master_get_devdata(cdfinger->spi->master);

	if (bonoff && (count == 0)) {
		ret = clk_prepare_enable(mdata->spi_clk);
		if (ret < 0) {
			cfp_debug(ERR_LOG, "%s failed to enable spi_clk (%d)\n", __func__, ret);
		}else {
			count = 1;
			cfp_debug(INFO_LOG, "%s sucess to enable spi_clk-- (%d)\n", __func__, ret);
		}
	} else if ((count > 0) && (bonoff == 0)) {
		clk_disable_unprepare(mdata->spi_clk);
		cfp_debug(INFO_LOG, "%s disable spi_clk!\n", __func__);
		count = 0;
	}
}
*/

/*static void cdfinger_disable_clk(struct cdfinger_data *cdfinger)
{
	#ifdef CDFINGER_EN_SPI_CLK
	struct mtk_spi *mdata = NULL;
	mdata = spi_master_get_devdata(g_cdfinger->spi->master);
	clk_disable_unprepare(mdata->spi_clk);
	//mtk_spi_clk_ctl(g_cdfinger->spi, 0);
	#else
	mt_spi_disable_clk(g_cdfinger->cdfinger_ms);
	#endif

	//struct mtk_spi *mdata = NULL;
	//mdata = spi_master_get_devdata(cdfinger->spi->master);
	//clk_disable_unprepare(mdata->spi_clk);
	//cfp_debug(INFO_LOG, "%s --disable spi clk!\n", __func__);
}*/

/*fp nav*/
static void cfp_report_uevent(struct cdfinger_data *cdfinger, char *str)
{

	char *envp[2];

	cfp_debug(DEBUG_LOG, "%s enter!\n", __func__);

	envp[0] = str;
	envp[1] = NULL;

	if (cdfinger->nav_dev) {
		cfp_debug(DEBUG_LOG, "%s cdfinger->nav_dev is not null!\n", __func__);
		kobject_uevent_env(&(cdfinger->nav_dev->dev.kobj), KOBJ_CHANGE, envp);
	} else {
		cfp_debug(ERR_LOG, "%s cdfinger->nav_dev is null!\n", __func__);
	}
}

static void cdfinger_disable_irq(struct cdfinger_data *cdfinger)
{
	if(cdfinger->irq_enabled == 1)
	{
		disable_irq_nosync(cdfinger->irq);
		cdfinger->irq_enabled = 0;
		cfp_debug(INFO_LOG, "%s : irq disable!\n", __func__);
	}
}

static void cdfinger_enable_irq(struct cdfinger_data *cdfinger)
{
	if(cdfinger->irq_enabled == 0)
	{
		enable_irq(cdfinger->irq);
		cdfinger->irq_enabled =1;
		cfp_debug(INFO_LOG, "%s : irq enable!\n", __func__);
	}
}

/*
static int cdfinger_getirq_from_platform(struct cdfinger_data *cdfinger)
{
	if (cdfinger == NULL) {
		cfp_debug(ERR_LOG, "cdfinger is NULL!\n");
		return -1;
	}

	//if(!(cdfinger->spi->dev.of_node)){
	//	cfp_debug(ERR_LOG, "of node not exist!\n");
	//	return -1;
	//}

	cdfinger->irq = gpio_to_irq(cdfinger->irq_gpio);
	if(cdfinger->irq < 0)
	{
		cfp_debug(ERR_LOG, "parse irq failed! irq[%d]\n",cdfinger->irq);
		return -1;
	}
	cfp_debug(INFO_LOG, "get irq success! irq[%d]\n",cdfinger->irq);
	//pinctrl_select_state(cdfinger->fps_pinctrl, cdfinger->cdfinger_irq);

	return 0;
}

//#else
static int cdfinger_getirq_from_platform(struct cdfinger_data *cdfinger)
{
	if(!(cdfinger->spi->dev.of_node)){
		cfp_debug(ERR_LOG, "of node not exist!\n");
		return -1;
	}

	cdfinger->irq = irq_of_parse_and_map(cdfinger->spi->dev.of_node, 0);
	if(cdfinger->irq < 0)
	{
		cfp_debug(ERR_LOG, "parse irq failed! irq[%d]\n",cdfinger->irq);
		return -1;
	}
	cfp_debug(INFO_LOG, "get irq success! irq[%d]\n",cdfinger->irq);
	pinctrl_select_state(cdfinger->fps_pinctrl, cdfinger->cdfinger_irq);
	return 0;
}
*/
#ifdef DTS_USE_GPIO_PIN
static int cdfinger_parse_dts(struct cdfinger_data *cdfinger)
{
	int rc = 0;
	struct device *dev = NULL;
	struct device_node *np = NULL;

	cfp_debug(INFO_LOG, "%s enter!\n", __func__);

	if (cdfinger == NULL) {
		cfp_debug(ERR_LOG, "%s, cdfinger_dev  is NULL\n", __func__);
		return -ENODEV;
	}

	dev = &cdfinger->spi->dev;
	np = dev->of_node;

	   /*---reset gpio---*/
	cdfinger->reset_gpio = of_get_named_gpio(np, "rst-gpios", 0);
	cfp_debug(INFO_LOG, "cdfinger->reset_gpio : %d\n", cdfinger->reset_gpio);

	if (gpio_is_valid(cdfinger->reset_gpio)) {
		/*rc = devm_gpio_request(dev, cdfinger->reset_gpio, "cdfinger_reset");*/
		rc = gpio_request(cdfinger->reset_gpio, "cdfinger_reset");
		if (rc) {
			cfp_debug(ERR_LOG, "failed to request reset gpio!rc = %d\n", rc);
			goto err_reset;
		} else {
			cfp_debug(INFO_LOG, "success to request reset gpio!\n");
			gpio_direction_output(cdfinger->reset_gpio, 0);
		}
	}

	/*---irq gpio---*/
	cdfinger->irq_gpio = of_get_named_gpio(np, "irq-gpios", 0);
	cfp_debug(INFO_LOG, "cdfinger->irq_gpio : %d\n", cdfinger->irq_gpio);

	if (gpio_is_valid(cdfinger->irq_gpio)) {
		/*rc = devm_gpio_request(dev, cdfinger->irq_gpio, "cdfinger_irq");*/
		rc = gpio_request(cdfinger->irq_gpio, "cdfinger_irq");
		if (rc) {
			cfp_debug(ERR_LOG, "failed to request irq gpio!rc = %d\n", rc);
			goto err_irq;
		} else {
			cfp_debug(INFO_LOG, "success to request irq gpio!\n");
			gpio_direction_input(cdfinger->irq_gpio);
		}
	}

	/*---cdfinger power method---*/
	rc = of_property_read_u32(np, "power-type", &cdfinger->power_type);
	if (rc < 0) {
		cfp_debug(ERR_LOG, "%s:Power type get failed from dts, rc=%d\n", __func__, rc);
	}
	cfp_debug(INFO_LOG, "%s:Power type[%d]\n", __func__, cdfinger->power_type);

	if (1 == cdfinger->power_type) {
		/* powered by regulator */
		cfp_debug(INFO_LOG, "%s PWR_MODE_REGULATOR\n", __func__);
		rc = of_property_read_u32(np, "power-voltage", &cdfinger->power_voltage);
		if (rc < 0) {
			cfp_debug(ERR_LOG, "%s:Power voltage get failed from dts, rc=%d\n", __func__, rc);
		}
		cfp_debug(INFO_LOG, "%s:Power voltage[%d]\n", __func__, cdfinger->power_voltage);

		cfp_debug(INFO_LOG, "regulator_vdd");
		cdfinger->fp_reg = regulator_get(dev, "vdd");
		if (IS_ERR(cdfinger->fp_reg)) {
			cfp_debug(ERR_LOG, "%s:regulator_get vdd failed from dts\n", __func__);
			return IS_ERR(cdfinger->fp_reg);
		} else {
			cfp_debug(INFO_LOG, "%s:regulator_get vdd success from dts\n", __func__);
		}

		rc = regulator_set_voltage(cdfinger->fp_reg, cdfinger->power_voltage, cdfinger->power_voltage);
		if (rc) {
			cfp_debug(ERR_LOG, "%s:regulator_set_voltage failed, rc=%d\n", __func__, rc);
			goto err_pwr;
		} else {
			cfp_debug(INFO_LOG, "%s:regulator_set_voltage success\n", __func__);
		}
	} else {
		/*---pwr gpio---*/
		cfp_debug(INFO_LOG, "%s PWR_MODE_GPIO!\n", __func__);
		cdfinger->pwr_gpio = of_get_named_gpio(np, "pwr-gpios", 0);
		cfp_debug(INFO_LOG, "cdfinger->pwr_gpio : %d\n", cdfinger->pwr_gpio);

		if (gpio_is_valid(cdfinger->pwr_gpio)) {
			/*rc = devm_gpio_request(dev, cdfinger->irq_gpio, "cdfinger_pwr");*/
			rc = gpio_request(cdfinger->pwr_gpio, "cdfinger_pwr");
			if (rc) {
				cfp_debug(ERR_LOG, "failed to request pwr gpio!rc = %d\n", rc);
				goto err_pwr;
			} else {
				cfp_debug(INFO_LOG, "success to request pwr gpio!\n");
				gpio_direction_output(cdfinger->pwr_gpio, 0);
			}
		}
	}

	cfp_debug(INFO_LOG, "%s exit, rc=%d\n", __func__, rc);
	return rc;

err_pwr:
	if (1 == cdfinger->power_type) {
		regulator_put(cdfinger->fp_reg);
		cdfinger->fp_reg = NULL;
	} else {
		/*devm_gpio_free(dev, cdfinger->irq_gpio);*/
		gpio_free(cdfinger->irq_gpio);
		cdfinger->pwr_gpio = 0;
	}
err_irq:
	/*devm_gpio_free(dev, cdfinger->reset_gpio);*/
	gpio_free(cdfinger->reset_gpio);
	cdfinger->irq_gpio = 0;
err_reset:
	cdfinger->reset_gpio = 0;
	return rc;
}

#else
static int cdfinger_parse_dts(struct cdfinger_data *cdfinger)
{
	int ret = -1;
	cfp_debug(DEBUG_LOG, "%s enter!\n", __func__);

	if(cdfinger->spi== NULL)
	{
		cfp_debug(ERR_LOG, "%s cdfinger->spi is NULL!\n", __func__);
		goto parse_err;
	}
#ifdef DTS_PROBE
	cdfinger->spi->dev.of_node = of_find_compatible_node(NULL,NULL,"mediatek,fps1098");
#endif

	//cdfinger->vdd_ldo_enable = 0;
	//cdfinger->vio_ldo_enable = 0;
	//cdfinger->config_spi_pin = 1;

	of_property_read_u32(cdfinger->spi->dev.of_node,"vdd_ldo_enable",&cdfinger->vdd_ldo_enable);
	of_property_read_u32(cdfinger->spi->dev.of_node,"vio_ldo_enable",&cdfinger->vio_ldo_enable);
	of_property_read_u32(cdfinger->spi->dev.of_node,"config_spi_pin",&cdfinger->config_spi_pin);


	cfp_debug(INFO_LOG, "vdd_ldo_enable[%d], vio_ldo_enable[%d], config_spi_pin[%d]\n",
		cdfinger->vdd_ldo_enable, cdfinger->vio_ldo_enable, cdfinger->config_spi_pin);

	cdfinger->fps_pinctrl = devm_pinctrl_get(&cdfinger->spi->dev);
	if (IS_ERR(cdfinger->fps_pinctrl)) {
		ret = PTR_ERR(cdfinger->fps_pinctrl);
		cfp_debug(ERR_LOG, "Cannot find fingerprint cdfinger->fps_pinctrl! ret = %d\n", ret);
		goto parse_err;
	}

	cdfinger->cdfinger_irq = pinctrl_lookup_state(cdfinger->fps_pinctrl,"eint");
	if (IS_ERR(cdfinger->cdfinger_irq))
	{
		ret = PTR_ERR(cdfinger->cdfinger_irq);
		cfp_debug(ERR_LOG, "cdfinger->cdfinger_irq fail! ret = %d\n", ret);
		goto parse_err;
	}
	cdfinger->fps_reset_low = pinctrl_lookup_state(cdfinger->fps_pinctrl,"finger_rst_low");
	if (IS_ERR(cdfinger->fps_reset_low))
	{
		ret = PTR_ERR(cdfinger->fps_reset_low);
		cfp_debug(ERR_LOG, "cdfinger->fps_reset_low fail!ret = %d\n", ret);
		goto parse_err;
	}
	cdfinger->fps_reset_high = pinctrl_lookup_state(cdfinger->fps_pinctrl,"finger_rst_high");
	if (IS_ERR(cdfinger->fps_reset_high))
	{
		ret = PTR_ERR(cdfinger->fps_reset_high);
		cfp_debug(ERR_LOG, "cdfinger->fps_reset_high fail!ret = %d\n", ret);
		goto parse_err;
	}

	if(cdfinger->config_spi_pin == 1)
	{
		cdfinger->cdfinger_spi_miso = pinctrl_lookup_state(cdfinger->fps_pinctrl,"spi_mi_low");
		if (IS_ERR(cdfinger->cdfinger_spi_miso))
		{
			ret = PTR_ERR(cdfinger->cdfinger_spi_miso);
			cfp_debug(ERR_LOG, "cdfinger->cdfinger_spi_miso fail!ret = %d\n", ret);
			goto parse_err;
		}
		cdfinger->cdfinger_spi_mosi = pinctrl_lookup_state(cdfinger->fps_pinctrl,"spi_mo_low");
		if (IS_ERR(cdfinger->cdfinger_spi_mosi))
		{
			ret = PTR_ERR(cdfinger->cdfinger_spi_mosi);
			cfp_debug(ERR_LOG, "cdfinger->cdfinger_spi_mosi fail!ret = %d\n", ret);
			goto parse_err;
		}
		cdfinger->cdfinger_spi_sck = pinctrl_lookup_state(cdfinger->fps_pinctrl,"spi_mclk_low");
		if (IS_ERR(cdfinger->cdfinger_spi_sck))
		{
			ret = PTR_ERR(cdfinger->cdfinger_spi_sck);
			cfp_debug(ERR_LOG, "cdfinger->cdfinger_spi_sck fail!ret = %d\n", ret);
			goto parse_err;
		}
		cdfinger->cdfinger_spi_cs = pinctrl_lookup_state(cdfinger->fps_pinctrl,"spi_cs_low");
		if (IS_ERR(cdfinger->cdfinger_spi_cs))
		{
			ret = PTR_ERR(cdfinger->cdfinger_spi_cs);
			cfp_debug(ERR_LOG, "cdfinger->cdfinger_spi_cs fail!ret = %d\n", ret);
			goto parse_err;
		}
	}

	if(cdfinger->vdd_ldo_enable == 1)
	{
		cdfinger->fps_power_on = pinctrl_lookup_state(cdfinger->fps_pinctrl,"fp_pwr_high");
		if (IS_ERR(cdfinger->fps_power_on))
		{
			ret = PTR_ERR(cdfinger->fps_power_on);
			cfp_debug(ERR_LOG, "cdfinger->fps_power_on fail!ret = %d\n", ret);
			goto parse_err;
		}

		cdfinger->fps_power_off = pinctrl_lookup_state(cdfinger->fps_pinctrl,"fp_pwr_low");
		if (IS_ERR(cdfinger->fps_power_off))
		{
			ret = PTR_ERR(cdfinger->fps_power_off);
			cfp_debug(ERR_LOG, "cdfinger->fps_power_off fail!ret = %d\n", ret);
			goto parse_err;
		}
	}

	if(cdfinger->vio_ldo_enable == 1)
	{
		cdfinger->fps_vio_on = pinctrl_lookup_state(cdfinger->fps_pinctrl,"fingerprint_vio_high");
		if (IS_ERR(cdfinger->fps_vio_on))
		{
			ret = PTR_ERR(cdfinger->fps_vio_on);
			cfp_debug(ERR_LOG, "cdfinger->fps_vio_on fail!ret = %d\n", ret);
			goto parse_err;
		}

		cdfinger->fps_vio_off = pinctrl_lookup_state(cdfinger->fps_pinctrl,"fingerprint_vio_low");
		if (IS_ERR(cdfinger->fps_vio_off))
		{
			ret = PTR_ERR(cdfinger->fps_vio_off);
			cfp_debug(ERR_LOG, "cdfinger->fps_vio_off fail!ret = %d\n", ret);
			goto parse_err;
		}
	}
	cfp_debug(DEBUG_LOG, "%s exit!\n", __func__);
	return 0;
parse_err:
	cfp_debug(ERR_LOG, "%s cdfinger parse dts fail!\n", __func__);

	return ret;
}
#endif

static int cdfinger_init_gpio(struct cdfinger_data *cdfinger)
{
	int ret =  - 1;
	cfp_debug(INFO_LOG, "%s enter!\n", __func__);

	if (cdfinger->gpio_requested == 1)
	{
		cfp_debug(INFO_LOG, "%s cdfinger gpio has request\n", __func__);
		return 0;
	}
	ret = cdfinger_parse_dts(cdfinger);
	if (ret != 0)
	{
		cfp_debug(ERR_LOG, "cdfinger_parse_dts fail! ret = %d\n", ret);
		return ret;
	}
	/*if (cdfinger->config_spi_pin == 1)
	{
		pinctrl_select_state(cdfinger->fps_pinctrl, cdfinger->cdfinger_spi_miso);
		pinctrl_select_state(cdfinger->fps_pinctrl, cdfinger->cdfinger_spi_mosi);
		pinctrl_select_state(cdfinger->fps_pinctrl, cdfinger->cdfinger_spi_sck);
		pinctrl_select_state(cdfinger->fps_pinctrl, cdfinger->cdfinger_spi_cs);
	}*/
	cdfinger->gpio_requested = 1 ;

	cfp_debug(INFO_LOG, "%s exit, ret=%d\n", __func__, ret);
	return ret;
}

static int cdfinger_free_gpio(struct cdfinger_data *cdfinger)
{
	int ret = 0;
	cfp_debug(INFO_LOG, "%s enter!\n", __func__);

#ifdef DTS_USE_GPIO_PIN
	if (cdfinger->gpio_requested == 1) {
		if (gpio_is_valid(cdfinger->irq_gpio)) {
			gpio_free(cdfinger->irq_gpio);
			cfp_debug(INFO_LOG, "cdfinger remove irq_gpio success\n");
			cdfinger->irq_gpio = 0;
		}
		if (gpio_is_valid(cdfinger->reset_gpio)) {
			gpio_set_value(cdfinger->reset_gpio, 0);
			gpio_free(cdfinger->reset_gpio);
			cfp_debug(INFO_LOG, "cdfinger set reset low and remove reset_gpio success\n");
			cdfinger->reset_gpio = 0;
		}
		if (1 == cdfinger->power_type) {
			/* powered by regulator */
			if (cdfinger->fp_reg != NULL) {
				ret = regulator_disable(cdfinger->fp_reg);
				regulator_put(cdfinger->fp_reg);
				cdfinger->fp_reg = NULL;
				cfp_debug(INFO_LOG, "%s:regulator_disable and regulator_put success\n", __func__);
			}
		} else {
			if (gpio_is_valid(cdfinger->pwr_gpio)) {
				gpio_set_value(cdfinger->pwr_gpio, 0);
				gpio_free(cdfinger->pwr_gpio);
				cfp_debug(INFO_LOG, "cdfinger set power low and remove pwr_gpio success\n");
				cdfinger->pwr_gpio = 0;
			}
		}
		cdfinger->gpio_requested = 0;
	}
#else
	devm_pinctrl_put(cdfinger->fps_pinctrl);
#endif

	cfp_debug(INFO_LOG, "%s exit, ret=%d\n", __func__, ret);
	return ret;
}

static void cdfinger_free_irq(struct cdfinger_data *cdfinger)
{
	cfp_debug(INFO_LOG, "%s enter!cdfinger->irq_request %d\n", __func__, cdfinger->irq_request);

	if (cdfinger->irq_request == 1)
	{
		disable_irq_wake(cdfinger->irq);
		free_irq(cdfinger->irq, cdfinger);
		cdfinger->irq_enabled = 0;
		cdfinger->irq_request =0;
	}
}

static irqreturn_t cdfinger_interrupt_handler(unsigned irq, void *arg)
{
	struct cdfinger_data *cdfinger = (struct cdfinger_data *)arg;

	cdfinger->cdfinger_interrupt = 1;
	if (cdfinger->process_interrupt == 1)
	{
		cdfinger->thread_wakeup = 1;
		wake_up_interruptible(&waiter);
	}

	return IRQ_HANDLED;
}

static int cdfinger_init_irq(struct cdfinger_data *cdfinger)
{
	unsigned int status = 0;
	cfp_debug(INFO_LOG, "%s enter!cdfinger->irq_request %d\n", __func__, cdfinger->irq_request);

	if (cdfinger->irq_request == 1)
		return 0;

#ifdef DTS_USE_GPIO_PIN
		cdfinger->irq = gpio_to_irq(cdfinger->irq_gpio);
		if(cdfinger->irq < 0)
		{
			cfp_debug(ERR_LOG, "gpio_to_irq failed! irq : %d\n",cdfinger->irq);
			return -1;
		}
#else
		pinctrl_select_state(cdfinger->fps_pinctrl, cdfinger->cdfinger_irq);
#endif

	status = request_threaded_irq(cdfinger->irq, (irq_handler_t)cdfinger_interrupt_handler, NULL,
								  IRQF_TRIGGER_RISING | IRQF_ONESHOT, "cdfinger-irq", cdfinger);
	if (status)
	{
		cfp_debug(ERR_LOG, "request_threaded_irq err\n");
		return status;
	} else {
		cfp_debug(INFO_LOG, "request_threaded_irq success\n");
	}
	enable_irq_wake(cdfinger->irq);
	cdfinger->irq_enabled = 1;
	cdfinger->irq_request =1;

	return status;
}

static int cdfinger_power_off(struct cdfinger_data *cdfinger)
{
	int rc = 0;

	cfp_debug(INFO_LOG, "%s enter!\n", __func__);

	/* TODO: add your power control here */
	if (1 == cdfinger->power_type) {
		/*  powered by regulator */
		if (cdfinger->fp_reg != NULL) {
			rc = regulator_disable(cdfinger->fp_reg);
			if (rc) {
				cfp_debug(ERR_LOG, "%s:regulator_disable failed, ret=%d\n", __func__, rc);
				return rc;
			} else {
				cfp_debug(INFO_LOG, "%s:regulator_disable success\n", __func__);
			}
		}
	} else {
		if (gpio_is_valid(cdfinger->pwr_gpio)) {
			gpio_set_value(cdfinger->pwr_gpio, 0);
			cfp_debug(INFO_LOG, "----cdfinger power off ok ----\n");
		}
	}

	return rc;
}


#ifdef DTS_USE_GPIO_PIN
static int cdfinger_power_on(struct cdfinger_data *cdfinger)
{
	int rc = 0;

	cfp_debug(INFO_LOG, "%s enter!\n", __func__);

	/* TODO: add your power control here */
	if (1 == cdfinger->power_type) {
		/*  powered by regulator */
		if (cdfinger->fp_reg != NULL) {
			rc = regulator_enable(cdfinger->fp_reg);
			if (rc) {
				cfp_debug(ERR_LOG, "%s:regulator_enable failed, rc=%d\n", __func__, rc);
				return rc;
			} else {
				cfp_debug(INFO_LOG, "%s:regulator_enable success\n", __func__);
			}
		}
	} else {
		if (gpio_is_valid(cdfinger->pwr_gpio)) {
			gpio_set_value(cdfinger->pwr_gpio, 1);
			usleep_range(2000, 2001);
			cfp_debug(INFO_LOG, "---- cdfinger power on ok ----\n");
		}
	}
	return rc;
}
#else
static void cdfinger_power_on(struct cdfinger_data *cdfinger)
{
	if(cdfinger->config_spi_pin == 1)
	{
		pinctrl_select_state(cdfinger->fps_pinctrl, cdfinger->cdfinger_spi_miso);
		pinctrl_select_state(cdfinger->fps_pinctrl, cdfinger->cdfinger_spi_mosi);
		pinctrl_select_state(cdfinger->fps_pinctrl, cdfinger->cdfinger_spi_sck);
		pinctrl_select_state(cdfinger->fps_pinctrl, cdfinger->cdfinger_spi_cs);
	}

	if(cdfinger->vdd_ldo_enable == 1)
	{
		pinctrl_select_state(cdfinger->fps_pinctrl, cdfinger->fps_power_on);
	}

	if(cdfinger->vio_ldo_enable == 1)
	{
		pinctrl_select_state(cdfinger->fps_pinctrl, cdfinger->fps_vio_on);
	}
}
#endif

#ifdef DTS_USE_GPIO_PIN
static int cdfinger_reset(int count)
{
	struct cdfinger_data *cdfinger = g_cdfinger;
	cfp_debug(DEBUG_LOG, "%s enter!\n", __func__);

	if (cdfinger == NULL) {
		cfp_debug(ERR_LOG, "%s Input buff is NULL!\n", __func__);
		return -1;
	}
	if (gpio_is_valid(cdfinger->reset_gpio)) {
		/* gpio_direction_output(cdfinger->reset_gpio, 1); */
		gpio_set_value(cdfinger->reset_gpio, 1);
		cfp_debug(DEBUG_LOG, "---- gpio_set_value reset 1 ----\n");
		usleep_range(count, count + 1);
		gpio_set_value(cdfinger->reset_gpio, 0);
		cfp_debug(DEBUG_LOG, "---- gpio_set_value reset 0 ----\n");
		usleep_range(count, count + 1);
		gpio_set_value(cdfinger->reset_gpio, 1);
		cfp_debug(DEBUG_LOG, "---- gpio_set_value reset 1 ----\n");
		usleep_range(count, count + 1);
		cfp_debug(DEBUG_LOG, "---- cdfinger reset ok ----\n");
	}
	return 0;
}

#else
static void cdfinger_reset(int count)
{
#ifdef HAS_RESET_PIN
	struct cdfinger_data *cdfinger = g_cdfinger;
	pinctrl_select_state(cdfinger->fps_pinctrl, cdfinger->fps_reset_low);
	mdelay(count);
	pinctrl_select_state(cdfinger->fps_pinctrl, cdfinger->fps_reset_high);
	mdelay(count);
#endif
}
#endif

static void cdfinger_release_wakelock(struct cdfinger_data *cdfinger)
{
	cfp_debug(DEBUG_LOG, "%s enter!\n", __func__);

#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
	__pm_relax(cdfinger->cdfinger_lock);
#else
	wake_unlock(&cdfinger->cdfinger_lock);
#endif

	cfp_debug(DEBUG_LOG, "%s exit!\n", __func__);
}

static void cdfinger_free_input(struct cdfinger_data *cdfinger)
{
	if (cdfinger->cdfinger_inputdev) {
		input_unregister_device(cdfinger->cdfinger_inputdev);
		input_free_device(cdfinger->cdfinger_inputdev);
		cdfinger->cdfinger_inputdev = NULL;
	}
}

static int cdfinger_create_inputdev(struct cdfinger_data *cdfinger)
{
	if (cdfinger->cdfinger_inputdev)
	{
		cfp_debug(INFO_LOG, "%s enter!cdfinger->cdfinger_inputdev created!\n", __func__);
		return 0;
	}
	cdfinger->cdfinger_inputdev = input_allocate_device();
	if (!cdfinger->cdfinger_inputdev) {
		cfp_debug(ERR_LOG, "cdfinger->cdfinger_inputdev create faile!\n");
		return -ENOMEM;
	}
	__set_bit(EV_KEY, cdfinger->cdfinger_inputdev->evbit);
	__set_bit(KEY_INTERRUPT, cdfinger->cdfinger_inputdev->keybit);
	__set_bit(KEY_F1, cdfinger->cdfinger_inputdev->keybit);
	__set_bit(KEY_F2, cdfinger->cdfinger_inputdev->keybit);
	__set_bit(KEY_F3, cdfinger->cdfinger_inputdev->keybit);
	__set_bit(KEY_F4, cdfinger->cdfinger_inputdev->keybit);
	__set_bit(KEY_VOLUMEUP, cdfinger->cdfinger_inputdev->keybit);
	__set_bit(KEY_VOLUMEDOWN, cdfinger->cdfinger_inputdev->keybit);
	__set_bit(KEY_PAGEUP, cdfinger->cdfinger_inputdev->keybit);
	__set_bit(KEY_PAGEDOWN, cdfinger->cdfinger_inputdev->keybit);
	__set_bit(KEY_UP, cdfinger->cdfinger_inputdev->keybit);
	__set_bit(KEY_LEFT, cdfinger->cdfinger_inputdev->keybit);
	__set_bit(KEY_RIGHT, cdfinger->cdfinger_inputdev->keybit);
	__set_bit(KEY_DOWN, cdfinger->cdfinger_inputdev->keybit);
	__set_bit(KEY_ENTER, cdfinger->cdfinger_inputdev->keybit);

	cdfinger->cdfinger_inputdev->id.bustype = BUS_HOST;
	cdfinger->cdfinger_inputdev->name = "cdfinger_inputdev";
	if (input_register_device(cdfinger->cdfinger_inputdev)) {
		cfp_debug(ERR_LOG, "register inputdev failed\n");
		input_free_device(cdfinger->cdfinger_inputdev);
		cdfinger->cdfinger_inputdev = NULL;
		return -1;
	}

	return 0;
}


static int cdfinger_mode_init(struct cdfinger_data *cdfinger, uint8_t arg, enum work_mode mode)
{
	cfp_debug(DEBUG_LOG, "%s enter!mode = 0x%x\n", __func__, mode);

	cdfinger->process_interrupt = 1;
	cdfinger->device_mode = mode;
	cdfinger->key_report = 0;

	return 0;
}

static void cdfinger_wake_lock(struct cdfinger_data *cdfinger, int arg)
{
	cfp_debug(DEBUG_LOG, "%s enter! arg=%d\n", __func__, arg);

	if(arg)
	{
		cfp_debug(DEBUG_LOG, "wake lock!\n");
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
		__pm_stay_awake(cdfinger->cdfinger_lock);
#else
		wake_lock(&cdfinger->cdfinger_lock);
#endif
	}
	else
	{
		cfp_debug(DEBUG_LOG, "wake unlock!\n");
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
		__pm_relax(cdfinger->cdfinger_lock);
#else
		wake_unlock(&cdfinger->cdfinger_lock);
#endif
	}
}

int cdfinger_report_key(struct cdfinger_data *cdfinger, unsigned long arg)
{
	key_report_t report;
	cfp_debug(DEBUG_LOG, "%s enter!\n", __func__);

	if (copy_from_user(&report, (key_report_t *)arg, sizeof(key_report_t)))
	{
		cfp_debug(ERR_LOG, "%s err!\n", __func__);
		return -1;
	}

	//cfp_debug(DEBUG_LOG, "KEY_UP = %d\n", KEY_UP);//103
	//cfp_debug(DEBUG_LOG, "KEY_VOLUMEDOWN = %d\n", KEY_VOLUMEDOWN);//114
	//cfp_debug(DEBUG_LOG, "KEY_DOWN = %d\n", KEY_DOWN);//108
	//cfp_debug(DEBUG_LOG, "KEY_VOLUMEUP = %d\n", KEY_VOLUMEUP);//115
	//cfp_debug(DEBUG_LOG, "KEY_RIGHT = %d\n", KEY_RIGHT);//106
	//cfp_debug(DEBUG_LOG, "KEY_PAGEUP = %d\n", KEY_PAGEUP);//104
	//cfp_debug(DEBUG_LOG, "KEY_LEFT = %d\n", KEY_LEFT);//105
	//cfp_debug(DEBUG_LOG, "KEY_PAGEDOWN = %d\n", KEY_PAGEDOWN);//109

	cfp_debug(INFO_LOG, "report.key = %d\n", report.key);
	switch(report.key)
	{
	case KEY_UP:
		cfp_debug(INFO_LOG, "KEY_LEFT----------\n");
		cfp_report_uevent(cdfinger, FP_NAV_LEFT);
		//report.key=KEY_VOLUMEDOWN;
		break;
	case KEY_DOWN:
		cfp_debug(INFO_LOG, "KEY_RIGHT----------\n");
		cfp_report_uevent(cdfinger, FP_NAV_RIGHT);
		//report.key=KEY_VOLUMEUP;
		break;
	case KEY_RIGHT:
		cfp_debug(INFO_LOG, "KEY_DOWN----------\n");
		cfp_report_uevent(cdfinger, FP_NAV_DOWN);
		//report.key=KEY_PAGEUP;
		break;
	case KEY_LEFT:
		cfp_debug(INFO_LOG, "KEY_UP----------\n");
		cfp_report_uevent(cdfinger, FP_NAV_UP);
		//report.key=KEY_PAGEDOWN;
		break;
	default:
		break;
	}

	//input_report_key(cdfinger->cdfinger_inputdev, report.key, !!report.value);
	//input_sync(cdfinger->cdfinger_inputdev);

	cfp_debug(DEBUG_LOG, "%s exit!\n", __func__);
	return 0;
}

static int cdfinger_report_key_legacy(struct cdfinger_data *cdfinger, uint8_t arg)
{
	cfp_debug(DEBUG_LOG, "%s enter!\n", __func__);

	input_report_key(cdfinger->cdfinger_inputdev, KEY_INTERRUPT, !!arg);
	input_sync(cdfinger->cdfinger_inputdev);

	cfp_debug(DEBUG_LOG, "%s exit!\n", __func__);
	return 0;
}
static unsigned int cdfinger_poll(struct file *filp, struct poll_table_struct *wait)
{
	int mask = 0;
	poll_wait(filp, &cdfinger_waitqueue, wait);
	if (sign_sync == 1)
	{
		mask |= POLLIN|POLLPRI;
	} else if (sign_sync == 2)
	{
		mask |= POLLOUT;
	}
	sign_sync = 0;
	cfp_debug(DEBUG_LOG, "%s mask %u\n", __func__, mask);

	return mask;
}

static long cdfinger_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct cdfinger_data *cdfinger = filp->private_data;
	int ret = 0;
	cfp_debug(DEBUG_LOG, "%s enter! cmd : 0x%x, arg : %lu\n", __func__, cmd, arg);

	if(cdfinger == NULL)
	{
		cfp_debug(ERR_LOG, "fingerprint please open device first!\n");
		return -EIO;
	}

	mutex_lock(&cdfinger->buf_lock);
	cfp_debug(DEBUG_LOG, "cdfinger_ioctl enter mutex_lock\n");
	if(cdfinger->fops_ref == 0)
	{
		cfp_debug(ERR_LOG, "please open device!\n");
		mutex_unlock(&cdfinger->buf_lock);
		return -EIO;
	}
	switch (cmd) {
		case CDFINGER_INIT:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_INIT\n");
			break;
		case CDFINGER_GETIMAGE:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_GETIMAGE\n");
			break;
		case CDFINGER_INITERRUPT_MODE:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_INITERRUPT_MODE\n");
			sign_sync = 0;
			isInKeyMode = 1;  // not key mode
			ret = cdfinger_mode_init(cdfinger,arg,CDFINGER_INTERRUPT_MODE);
			break;
		case CDFINGER_NEW_KEYMODE:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_NEW_KEYMODE\n");
			isInKeyMode = 0;
			ret = cdfinger_mode_init(cdfinger,arg,CDFINGER_INTERRUPT_MODE);
			break;
		case CDFINGER_INITERRUPT_FINGERUPMODE:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_INITERRUPT_FINGERUPMODE\n");
			ret = cdfinger_mode_init(cdfinger,arg,CDFINGER_FINGER_UP_MODE);
			break;
		case CDFINGER_RELEASE_WAKELOCK:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_RELEASE_WAKELOCK\n");
			cdfinger_release_wakelock(cdfinger);
			break;
		case CDFINGER_INITERRUPT_KEYMODE:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_INITERRUPT_KEYMODE\n");
			ret = cdfinger_mode_init(cdfinger,arg,CDFINGER_KEY_MODE);
			break;
		case CDFINGER_CHECK_INTERRUPT:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_CHECK_INTERRUPT\n");
			break;
		case CDFINGER_SET_SPI_SPEED:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_SET_SPI_SPEED\n");
			break;
		case CDFINGER_WAKE_LOCK:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_WAKE_LOCK\n");
			cdfinger_wake_lock(cdfinger,arg);
			break;
		case CDFINGER_REPORT_KEY:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_REPORT_KEY\n");
			ret = cdfinger_report_key(cdfinger, arg);
			break;
		case CDFINGER_REPORT_KEY_LEGACY:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_REPORT_KEY_LEGACY\n");
			ret = cdfinger_report_key_legacy(cdfinger, arg);
			break;
		case CDFINGER_POWERDOWN:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_POWERDOWN\n");
			break;
		case CDFINGER_ENABLE_IRQ:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_ENABLE_IRQ\n");
			cdfinger_enable_irq(cdfinger);
			break;
		case CDFINGER_DISABLE_IRQ:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_DISABLE_IRQ\n");
			cdfinger_disable_irq(cdfinger);
			break;
		case CDFINGER_ENABLE_CLK:
		case CDFINGER_SPI_CLK:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_SPI_CLK\n");
#if defined(USE_SPI_BUS)
			if (arg == 1)
				mt_spi_enable_master_clk(cdfinger->spi);
			else if (arg == 0)
				mt_spi_disable_master_clk(cdfinger->spi);
#endif
			break;
		case CDFINGER_HW_RESET:
		case CDFINGER_RESET:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_HW_RESET\n");
			cdfinger_reset(1000);
			break;
		case CDFINGER_SETID:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_SETID\n");
			cdfinger->chip_id = arg;
			cfp_debug(DEBUG_LOG, "set cdfinger chip id 0x%x\n",cdfinger->chip_id);
			break;
		case CDFINGER_GETID:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_GETID\n");
			ret = cdfinger->chip_id;
			cfp_debug(DEBUG_LOG, "get cdfinger chip id 0x%x\n",ret);
			break;
		case CDFINGER_GET_STATUS:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_GET_STATUS\n");
			ret = screen_status;
			break;
		case CDFINGER_POLL_TRIGGER:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_POLL_TRIGGER\n");
			sign_sync = 2;
			wake_up_interruptible(&cdfinger_waitqueue);
			ret = 0;
			break;
		case CDFINGER_CONTROL_IRQ:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_CONTROL_IRQ\n");
			if (1 == arg)
				cdfinger_enable_irq(cdfinger);
			else
				cdfinger_disable_irq(cdfinger);
			break;
		case CDFINGER_INIT_GPIO:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_INIT_GPIO\n");
			ret = cdfinger_init_gpio(cdfinger);
			break;
		case CDFINGER_POWER_ON:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_POWER_ON\n");
			ret = cdfinger_power_on(cdfinger);
			break;
		case CDFINGER_RELEASE_DEVICE:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_RELEASE_DEVICE\n");
			cdfinger_free_irq(cdfinger);
			cdfinger_free_gpio(cdfinger);
			cdfinger_free_input(cdfinger);
			break;
		case CDFINGER_INIT_IRQ:
			cfp_debug(DEBUG_LOG, "cdfinger_ioctl CDFINGER_INIT_IRQ\n");
			ret = cdfinger_init_irq(cdfinger);
			if (ret != 0)
				break;
			ret = cdfinger_create_inputdev(cdfinger);
			break;
		default:
			ret = -ENOTTY;
			break;
	}
	mutex_unlock(&cdfinger->buf_lock);

	cfp_debug(DEBUG_LOG, "%s exit!\n", __func__);
	return ret;
}

static int cdfinger_open(struct inode *inode, struct file *file)
{

	struct device_node *dnode = NULL;

	mutex_lock(&g_cdfinger->buf_lock);
	file->private_data = g_cdfinger;
	g_cdfinger->fops_ref++;
	cfp_debug(INFO_LOG, "%s enter! open fops_ref : %d\n", __func__, g_cdfinger->fops_ref);
        /*fp_nav*/
	dnode = of_find_compatible_node(NULL, NULL, "zte_fp_nav");
	if (dnode) {
		cfp_debug(DEBUG_LOG, "fp-nav device node found!\n");
		g_cdfinger->nav_dev = of_find_device_by_node(dnode);
		if (g_cdfinger->nav_dev)
		{
			cfp_debug(DEBUG_LOG, "fp-nav device uevent found!\n");
		} else {
			cfp_debug(ERR_LOG, "fp-nav device uevent not found!\n");
		}
	} else {
		cfp_debug(ERR_LOG, "fp-nav device node not found!\n");
	}

	mutex_unlock(&g_cdfinger->buf_lock);

	return 0;
}

static int cdfinger_release(struct inode *inode, struct file *file)
{
	struct cdfinger_data *cdfinger = file->private_data;
	mutex_lock(&cdfinger->buf_lock);
	cdfinger->fops_ref--;
	cfp_debug(INFO_LOG, "%s enter! release fops_ref : %d\n", __func__, cdfinger->fops_ref);
	if (cdfinger->fops_ref == 0) {
		cdfinger_free_irq(cdfinger);
		cdfinger_free_gpio(cdfinger);
		cdfinger_free_input(cdfinger);
		file->private_data = NULL;
	}
	mutex_unlock(&cdfinger->buf_lock);
	return 0;
}

static ssize_t cdfinger_write(struct file *file, const char *buff, size_t count, loff_t * ppos)
{
	return 0;
}

static int cdfinger_async_fasync(int fd, struct file *filp, int mode)
{
	struct cdfinger_data *cdfinger = g_cdfinger;
	cfp_debug(DEBUG_LOG, "%s enter!\n", __func__);

	return fasync_helper(fd, filp, mode, &cdfinger->async_queue);
}

static ssize_t cdfinger_read(struct file *file, char *buff, size_t count, loff_t * ppos)
{
	return 0;
}


static const struct file_operations cdfinger_fops = {
	.owner = THIS_MODULE,
	.open = cdfinger_open,
	.write = cdfinger_write,
	.read = cdfinger_read,
	.release = cdfinger_release,
	.fasync = cdfinger_async_fasync,
	.unlocked_ioctl = cdfinger_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = cdfinger_ioctl,
#endif
	.poll = cdfinger_poll,
};

static struct miscdevice cdfinger_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVICE_NAME,
	.fops = &cdfinger_fops,
};

static void cdfinger_async_Report(void)
{
	struct cdfinger_data *cdfinger = g_cdfinger;
	cfp_debug(DEBUG_LOG, "%s enter!\n", __func__);

	kill_fasync(&cdfinger->async_queue, SIGIO, POLL_IN);

	cfp_debug(DEBUG_LOG, "%s exit!\n", __func__);
}

static int cdfinger_thread_func(void *arg)
{
	struct cdfinger_data *cdfinger = (struct cdfinger_data *)arg;
	cfp_debug(DEBUG_LOG, "%s enter!\n", __func__);

	do {
		wait_event_interruptible(waiter, cdfinger->thread_wakeup != 0);
		cfp_debug(DEBUG_LOG, "%s thread_wakeup\n", __func__);
		cdfinger->thread_wakeup = 0;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
		__pm_wakeup_event(cdfinger->cdfinger_lock, 100);
#else
		wake_lock_timeout(&cdfinger->cdfinger_lock, msecs_to_jiffies(100));
#endif

		if (cdfinger->device_mode == CDFINGER_INTERRUPT_MODE) {
			cdfinger->process_interrupt = 0;
			sign_sync = 1;
			wake_up_interruptible(&cdfinger_waitqueue);
			cdfinger_async_Report();
			continue;
		} else if ((cdfinger->device_mode == CDFINGER_KEY_MODE) && (cdfinger->key_report == 0)) {
			input_report_key(cdfinger->cdfinger_inputdev, KEY_INTERRUPT, 1);
			input_sync(cdfinger->cdfinger_inputdev);
			cdfinger->key_report = 1;
		}

	}while(!kthread_should_stop());

	cfp_debug(DEBUG_LOG, "%s exit!\n", __func__);
	return -1;
}



static int cdfinger_fb_notifier_callback(struct notifier_block* self,
                                        unsigned long event, void* data)
{
    struct fb_event* evdata = data;
    unsigned int blank;
    int retval = 0;
    cfp_debug(DEBUG_LOG, "%s enter!\n", __func__);

    if (event != FB_EVENT_BLANK /* FB_EARLY_EVENT_BLANK */) {
        return 0;
    }
    blank = *(int*)evdata->data;
    switch (blank) {
        case FB_BLANK_UNBLANK:
			cfp_debug(INFO_LOG, "%s FB_BLANK_UNBLANK\n", __func__);
			mutex_lock(&g_cdfinger->buf_lock);
			screen_status = 1;
			if (isInKeyMode == 0) {
				sign_sync = 1;
				wake_up_interruptible(&cdfinger_waitqueue);
				cdfinger_async_Report();
			}
			mutex_unlock(&g_cdfinger->buf_lock);
            break;

        case FB_BLANK_POWERDOWN:
			cfp_debug(INFO_LOG, "%s FB_BLANK_POWERDOWN\n", __func__);
			mutex_lock(&g_cdfinger->buf_lock);
			screen_status = 0;
			if (isInKeyMode == 0) {
				sign_sync = 1;
				wake_up_interruptible(&cdfinger_waitqueue);
				cdfinger_async_Report();
			}
			mutex_unlock(&g_cdfinger->buf_lock);
            break;
        default:
            break;
    }

    cfp_debug(DEBUG_LOG, "%s exit!\n", __func__);
    return retval;
}

#if defined(USE_SPI_BUS)
static int cdfinger_probe(struct spi_device *spi)
#elif defined(USE_PLATFORM_BUS)
static int cdfinger_probe(struct platform_device *spi)
#endif
{
	struct cdfinger_data *cdfinger = NULL;
	int status = -ENODEV;
	cfp_debug(INFO_LOG, "%s enter!\n", __func__);

	cdfinger = kzalloc(sizeof(struct cdfinger_data), GFP_KERNEL);
	if (!cdfinger) {
		cfp_debug(ERR_LOG, "%s alloc cdfinger failed!\n", __func__);
		return -ENOMEM;;
	}
	g_cdfinger = cdfinger;

	cdfinger->spi = spi;

	cdfinger->nav_dev= NULL;/*fp_nav*/
	/* set defualt fingerprint powered by gpio */
	cdfinger->power_type        = 0;
	cdfinger->fp_reg            = NULL;
	cdfinger->power_voltage     = 2800000;/*default fp voltage*/

	mutex_init(&cdfinger->buf_lock);

#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
	cdfinger->cdfinger_lock = wakeup_source_register(NULL, "cdfinger_wakeup_source");
#else
	wake_lock_init(&cdfinger->cdfinger_lock, WAKE_LOCK_SUSPEND, "cdfinger wakelock");
#endif

	status = misc_register(&cdfinger_dev);
	if (status < 0) {
		cfp_debug(ERR_LOG, "%s cdev register failed!\n", __func__);
		goto free_lock;
	}

	cdfinger->cdfinger_thread = kthread_run(cdfinger_thread_func, cdfinger, "cdfinger_thread");
	if (IS_ERR(cdfinger->cdfinger_thread)) {
		cfp_debug(ERR_LOG, "%s kthread_run is failed!\n", __func__);
		goto free_device;
	}
	cdfinger->notifier.notifier_call = cdfinger_fb_notifier_callback;
	fb_register_client(&cdfinger->notifier);

	cfp_debug(INFO_LOG, "%s exit!\n", __func__);
	return 0;

free_device:
	misc_deregister(&cdfinger_dev);
free_lock:
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
	wakeup_source_unregister(cdfinger->cdfinger_lock);
#else
	wake_lock_destroy(&cdfinger->cdfinger_lock);
#endif
	mutex_destroy(&cdfinger->buf_lock);
	kfree(cdfinger);
	cdfinger = NULL;

	return -1;
}

static int cdfinger_suspend (struct device *dev)
{
	return 0;
}

static int cdfinger_resume (struct device *dev)
{
	return 0;
}

#if defined(USE_SPI_BUS)
static int cdfinger_remove(struct spi_device *spi)
#elif defined(USE_PLATFORM_BUS)
static int cdfinger_remove(struct platform_device *spi)
#endif
{
	cfp_debug(INFO_LOG, "%s enter!\n", __func__);

	cdfinger_power_off(g_cdfinger);
	cdfinger_free_irq(g_cdfinger);
	cdfinger_free_gpio(g_cdfinger);
	cdfinger_free_input(g_cdfinger);
	misc_deregister(&cdfinger_dev);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
	wakeup_source_unregister(g_cdfinger->cdfinger_lock);
#else
	wake_lock_destroy(&g_cdfinger->cdfinger_lock);
#endif
	mutex_destroy(&g_cdfinger->buf_lock);
	fb_unregister_client(&g_cdfinger->notifier);
	kfree(g_cdfinger);
	g_cdfinger = NULL;

	cfp_debug(INFO_LOG, "%s exit!\n", __func__);
	return 0;
}

static const struct dev_pm_ops cdfinger_pm = {
	.suspend = cdfinger_suspend,
	.resume = cdfinger_resume
};
struct of_device_id cdfinger_of_match[] = {
	{ .compatible = "cdfinger,fps998e", },
	{ .compatible = "cdfinger,fps1098", },
	{ .compatible = "cdfinger,fps998", },
	{ .compatible = "cdfinger,fps980", },
	{ .compatible = "cdfinger,fps956", },
	{},
};
MODULE_DEVICE_TABLE(of, cdfinger_of_match);

#if defined(USE_SPI_BUS)
static const struct spi_device_id cdfinger_id[] = {
	{SPI_DRV_NAME, 0},
	{}
};
MODULE_DEVICE_TABLE(spi, cdfinger_id);
#elif defined(USE_PLATFORM_BUS)
static const struct platform_device_id cdfinger_id[] = {
	{SPI_DRV_NAME, 0},
	{}
};
MODULE_DEVICE_TABLE(platform, cdfinger_id);
#endif

#if defined(USE_SPI_BUS)
static struct spi_driver cdfinger_driver = {
#elif defined(USE_PLATFORM_BUS)
static struct platform_driver cdfinger_driver = {
#endif
	.driver = {
		.name = SPI_DRV_NAME,
		.bus = &spi_bus_type,
		.owner = THIS_MODULE,
		.pm = &cdfinger_pm,
		.of_match_table = of_match_ptr(cdfinger_of_match),
	},
	.id_table = cdfinger_id,
	.probe = cdfinger_probe,
	.remove = cdfinger_remove,
};

#ifndef DTS_PROBE
static struct spi_board_info spi_board_cdfinger[] __initdata = {
	[0] = {
		.modalias = "cdfinger",
		.bus_num = 0,
		.chip_select = 0,
		.mode = SPI_MODE_0,
		.max_speed_hz = 6000000,
		.controller_data = &spi_conf,
	},
};
#endif

int cdfinger_fp_init(void)
{
	cfp_debug(INFO_LOG, "%s enter, driver_time:2022-12-20\n", __func__);

#ifndef DTS_PROBE
	spi_register_board_info(spi_board_cdfinger, ARRAY_SIZE(spi_board_cdfinger));
#endif

#if defined(USE_PLATFORM_BUS)
	cfp_debug(INFO_LOG, "%s cdfinger platform_driver_register!\n", __func__);
	return platform_driver_register(&cdfinger_driver);
#elif defined(USE_SPI_BUS)
	cfp_debug(INFO_LOG, "%s cdfinger spi_register_driver!\n", __func__);
	return spi_register_driver(&cdfinger_driver);
#endif
}

void cdfinger_fp_exit(void)
{
	cfp_debug(INFO_LOG, "%s enter!\n", __func__);

#if defined(USE_PLATFORM_BUS)
	cfp_debug(INFO_LOG, "%s cdfinger platform_driver_unregister!\n", __func__);
	platform_driver_unregister(&cdfinger_driver);
#elif defined(USE_SPI_BUS)
	cfp_debug(INFO_LOG, "%s cdfinger spi_unregister_driver!\n", __func__);
	spi_unregister_driver(&cdfinger_driver);
#endif
}

/*late_initcall_sync(cdfinger_spi_init);
module_init(cdfinger_fp_init);
module_exit(cdfinger_fp_exit);*/

MODULE_DESCRIPTION("cdfinger tee Driver");
MODULE_AUTHOR("shuaitao@cdfinger.com");
MODULE_LICENSE("GPL");
MODULE_ALIAS("cdfinger");
