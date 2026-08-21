// SPDX-License-Identifier: GPL-2.0
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

#include <linux/sipc.h>
#include <misc/wcn_bus.h>
#include <misc/wcn_integrate_platform.h>

#include "alignment/sitm.h"
#include "rfkill.h"
#include "tty.h"
#include "unisoc_bt_log.h"

#define STTY_MAX_DATA_LEN 4096

struct stty_device {
	struct stty_init_data pdata;
	struct tty_port port;
	struct tty_driver *driver;
	struct tty_struct *tty;
	struct mutex lock;
	bool opened;
};

struct device *ttyBT_dev;
static struct stty_device *stty_dev;

static void stty_rx_handler(int event, void *data)
{
	struct stty_device *stty = data;
	u8 *buf;
	int count;

	if (event != SBUF_NOTIFY_READ)
		return;

	buf = kmalloc(STTY_MAX_DATA_LEN, GFP_KERNEL);
	if (!buf)
		return;

	do {
		count = sbuf_read(stty->pdata.dst, stty->pdata.channel,
					  stty->pdata.rx_bufid, buf,
					  STTY_MAX_DATA_LEN, 0);
		if (count <= 0)
			break;
		mutex_lock(&stty->lock);
		if (stty->opened) {
			tty_insert_flip_string(&stty->port, buf, count);
			tty_schedule_flip(&stty->port);
		}
		mutex_unlock(&stty->lock);
	} while (count == STTY_MAX_DATA_LEN);

	kfree(buf);
}

static int stty_open(struct tty_struct *tty, struct file *filp)
{
	struct stty_device *stty = tty->driver->driver_state;
	int ret;

	if (sbuf_status(stty->pdata.dst, stty->pdata.channel))
		return -ENODEV;

	mutex_lock(&stty->lock);
	stty->tty = tty;
	tty->driver_data = stty;
	stty->opened = true;
	mutex_unlock(&stty->lock);

	ret = sitm_init();
	if (ret)
		goto err_close;
	ret = start_marlin(WCN_MARLIN_BLUETOOTH);
	if (!ret)
		return 0;

	dev_unisoc_bt_err(ttyBT_dev, "start Bluetooth failed: %d\n", ret);
	sitm_cleanup();
err_close:
	mutex_lock(&stty->lock);
	stty->opened = false;
	stty->tty = NULL;
	tty->driver_data = NULL;
	mutex_unlock(&stty->lock);
	return ret;
}

static void stty_close(struct tty_struct *tty, struct file *filp)
{
	struct stty_device *stty = tty->driver_data;

	if (!stty)
		return;
	mutex_lock(&stty->lock);
	stty->opened = false;
	stty->tty = NULL;
	mutex_unlock(&stty->lock);
	sitm_cleanup();
	stop_marlin(WCN_MARLIN_BLUETOOTH);
}

static int stty_transmit(u8 *data, size_t count)
{
	struct stty_device *stty = stty_dev;
	int written;
	size_t sent = 0;

	if (!stty || !stty->opened)
		return -ENODEV;

	while (sent < count) {
		written = sbuf_write(stty->pdata.dst, stty->pdata.channel,
					     stty->pdata.tx_bufid, data + sent,
					     count - sent, -1);
		if (written <= 0)
			return written ? written : -EIO;
		sent += written;
	}
	return count;
}

static int stty_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	return sitm_write(buf, count, stty_transmit);
}

static int stty_write_room(struct tty_struct *tty)
{
	return STTY_MAX_DATA_LEN;
}

static const struct tty_operations stty_ops = {
	.open = stty_open,
	.close = stty_close,
	.write = stty_write,
	.write_room = stty_write_room,
};

static int stty_probe(struct platform_device *pdev)
{
	struct stty_device *stty;
	const char *name;
	int ret;

	ret = of_property_read_string(pdev->dev.of_node, "sprd,name", &name);
	if (ret)
		return ret;

	stty = devm_kzalloc(&pdev->dev, sizeof(*stty), GFP_KERNEL);
	if (!stty)
		return -ENOMEM;
	stty->pdata.name = (char *)name;
	stty->pdata.dst = SPRD_BT_DST;
	stty->pdata.channel = SPRD_BT_CHANNEL;
	stty->pdata.tx_bufid = SPRD_BT_TX_BUFID;
	stty->pdata.rx_bufid = SPRD_BT_RX_BUFID;
	mutex_init(&stty->lock);
	tty_port_init(&stty->port);

	stty->driver = alloc_tty_driver(1);
	if (!stty->driver) {
		ret = -ENOMEM;
		goto err_port;
	}
	stty->driver->owner = THIS_MODULE;
	stty->driver->driver_name = stty->pdata.name;
	stty->driver->name = stty->pdata.name;
	stty->driver->major = 0;
	stty->driver->type = TTY_DRIVER_TYPE_SYSTEM;
	stty->driver->subtype = SYSTEM_TYPE_TTY;
	stty->driver->init_termios = tty_std_termios;
	stty->driver->driver_state = stty;
	tty_set_operations(stty->driver, &stty_ops);
	tty_port_link_device(&stty->port, stty->driver, 0);
	ret = tty_register_driver(stty->driver);
	if (ret)
		goto err_driver;

	ret = sbuf_register_notifier(stty->pdata.dst, stty->pdata.channel,
				     stty->pdata.rx_bufid, stty_rx_handler, stty);
	if (ret)
		goto err_tty;

	ttyBT_dev = &pdev->dev;
	stty_dev = stty;
	platform_set_drvdata(pdev, stty);
	ret = rfkill_bluetooth_init(pdev);
	if (ret)
		dev_unisoc_bt_err(ttyBT_dev, "rfkill registration failed: %d\n", ret);
	dev_info(&pdev->dev, "SIPC Bluetooth TTY ready (%s)\n", name);
	return 0;

err_tty:
	tty_unregister_driver(stty->driver);
err_driver:
	put_tty_driver(stty->driver);
err_port:
	tty_port_destroy(&stty->port);
	return ret;
}

static int stty_remove(struct platform_device *pdev)
{
	struct stty_device *stty = platform_get_drvdata(pdev);

	rfkill_bluetooth_remove();
	sbuf_register_notifier(stty->pdata.dst, stty->pdata.channel,
				       stty->pdata.rx_bufid, NULL, NULL);
	tty_unregister_driver(stty->driver);
	put_tty_driver(stty->driver);
	tty_port_destroy(&stty->port);
	stty_dev = NULL;
	ttyBT_dev = NULL;
	return 0;
}

static const struct of_device_id stty_match_table[] = {
	{ .compatible = "sprd,mtty" },
	{ .compatible = "sprd,wcn_bt" },
	{ },
};
MODULE_DEVICE_TABLE(of, stty_match_table);

static struct platform_driver stty_driver = {
	.probe = stty_probe,
	.remove = stty_remove,
	.driver = {
		.name = "ttyBT",
		.of_match_table = stty_match_table,
	},
};
module_platform_driver(stty_driver);

MODULE_DESCRIPTION("Unisoc SIPC Bluetooth TTY driver");
MODULE_LICENSE("GPL v2");
