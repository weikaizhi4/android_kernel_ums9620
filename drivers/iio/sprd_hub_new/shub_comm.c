// SPDX-License-Identifier: GPL-2.0
/*
 * File:shub_comm.c
 *
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 */

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include "shub_common.h"
#include "shub_core.h"
#include "shub_protocol.h"

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
#include "zlog_common_base.h"
extern struct zlog_client *zlog_sprd_sensorhub_client;
#endif

static void shub_get_data(struct cmd_data *packet)
{
	u16 data_number, count = 0;
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	int zlog_type;
#endif

	switch (packet->subtype) {
	case SHUB_LOG_SUBTYPE:
		dev_info(&g_sensor->sensor_pdev->dev,
			 " [CM4]> :%s\n", packet->buff);
		break;

	case SHUB_DATA_SUBTYPE:
		data_number =
		(packet->length)/sizeof(struct shub_sensor_event);
		while (count != data_number) {
			g_sensor->data_callback(g_sensor,
			packet->buff + count * sizeof(struct shub_sensor_event),
			sizeof(struct shub_sensor_event));
			count++;
		}

		break;

	case SHUB_CM4_OPERATE:
		memcpy(g_sensor->cm4_operate_data,
		       packet->buff,
		       sizeof(g_sensor->cm4_operate_data));
		break;

	case SHUB_GET_MAG_OFFSET:
		g_sensor->save_mag_offset(g_sensor, packet->buff,
					packet->length);
		break;

	case SHUB_GET_CALIBRATION_DATA_SUBTYPE:
	case SHUB_GET_LIGHT_RAWDATA_SUBTYPE:
	case SHUB_GET_PROXIMITY_RAWDATA_SUBTYPE:
	case SHUB_GET_FWVERSION_SUBTYPE:
		g_sensor->readcmd_callback(g_sensor, packet->buff,
					packet->length);
		break;

	case SHUB_SET_TIMESYNC_SUBTYPE:
		g_sensor->cm4_read_callback(g_sensor,
			packet->subtype,
			packet->buff,
			packet->length);
		break;

	case SHUB_GET_SENSORINFO_SUBTYPE:
		if (g_sensor->sensor_info_count >= ARRAY_SIZE(g_sensor->sensor_info_list)) {
			pr_err("Fail! sensor_info_count=%d out of range\n",
				g_sensor->sensor_info_count);
			break;
		}
		memcpy(&g_sensor->sensor_info_list[g_sensor->sensor_info_count],
		       packet->buff,
		       sizeof(struct sensor_info_t));
		g_sensor->sensor_info_count += 1;
		break;

	/* add for sensorhub init ready */
	case SHUB_SEND_SENSORHUB_INIT_READY_SUBTYPE:
		dev_info(&g_sensor->sensor_pdev->dev, "%s sensorhub init ready!\n",
				__func__);
		g_sensor->sensorhub_init_ready = true;
		wake_up_interruptible(&g_sensor->sensorhub_wq);
		break;

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	case SHUB_ZLOG_I2C_RW_ERROR_SUBTYPE:
		zlog_type = ZLOG_SENSOR_I2C_RW_ERROR_NO;
		if (g_sensor->sensorhub_init_ready != true)
			break;
	case SHUB_ZLOG_SPI_RW_ERROR_SUBTYPE:
		zlog_type = ZLOG_SENSOR_SPI_RW_ERROR_NO;
		if (g_sensor->sensorhub_init_ready != true)
			break;
	case SHUB_ZLOG_ESD_CHECK_TRI_SUBTYPE:
		zlog_type = ZLOG_SENSOR_ESD_CHECK_TRI_NO;
	case SHUB_ZLOG_FACTORY_CALI_SUBTYPE:
		zlog_type = ZLOG_SENSOR_FACTORY_CALI_NO;
	case SHUB_ZLOG_STEP_COUNTER_DEC_NO: {
		zlog_type = ZLOG_SENSOR_STEP_COUNTER_DEC_NO;

		zlog_client_record(zlog_sprd_sensorhub_client, "%s\n", packet->buff);
		zlog_client_notify(zlog_sprd_sensorhub_client, zlog_type);
		dev_info(&g_sensor->sensor_pdev->dev, "%s type=%d data:%s reported to zlog\n",
				__func__, zlog_type, packet->buff);
		break;
	}
#endif

	default:
		break;
	}
}

void shub_dispatch(struct cmd_data *packet)
{
	if (packet)
		shub_get_data(packet);
}

MODULE_DESCRIPTION("Sensorhub dispatch support");
MODULE_LICENSE("GPL v2");
