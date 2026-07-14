/*
 * aw_spin.c   aw881xx spin module
 *
 * Copyright (c) 2020 AWINIC Technology CO., LTD
 *
 *  Author: Bruce zhao <zhaolei@awinic.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <sound/soc.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/version.h>
#include <linux/syscalls.h>
#include <sound/control.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include "aw881xx_spin.h"
#include "aw881xx.h"
#include "aw881xx_reg.h"

static DEFINE_MUTEX(g_aw_spin_lock);
static DEFINE_MUTEX(g_aw_dsp_lock);

static int g_rx_topo_id = AW_RX_TOPO_ID;
static int g_rx_port_id = AW_RX_PORT_ID;

static unsigned int g_spin_angle = AW_SPIN_0;
static unsigned int g_spin_mode = AW_SPIN_OFF_MODE;

static const char *const aw_spin[] = {"spin_0", "spin_90",
					"spin_180", "spin_270"};

/*#define AW_QCOM_PLATFORM_SPIN*/
/*#define AW_MTK_PLATFORM_SPIN*/

#ifdef AW_MTK_PLATFORM_SPIN
extern int mtk_spk_send_ipi_buf_to_dsp(void *data_buffer, uint32_t data_size);
extern int mtk_spk_recv_ipi_buf_from_dsp(int8_t *buffer, int16_t size, uint32_t *buf_len);
#elif defined AW_QCOM_PLATFORM_SPIN
extern int afe_get_topology(int port_id);
extern int aw_send_afe_cal_apr(uint32_t param_id,
	void *buf, int cmd_size, bool write);
#else
static int aw_send_afe_cal_apr(uint32_t param_id,
		void *buf, int cmd_size, bool write)
{
	return 0;
}

static int afe_get_topology(int port_id)
{
	return 0;
}

#endif

#ifdef AW_QCOM_PLATFORM_SPIN
extern void aw_set_port_id(int tx_port_id, int rx_port_id);
#else
static void aw_set_port_id(int tx_port_id, int rx_port_id) {
	return;
}
#endif

static int aw_get_msg_id(int dev_ch, uint32_t *msg_id)
{
	switch (dev_ch) {
	case AW_DEV_CH_PRI_L:
		*msg_id = AFE_MSG_ID_MSG_0;
		break;
	case AW_DEV_CH_PRI_R:
		*msg_id = AFE_MSG_ID_MSG_0;
		break;
	case AW_DEV_CH_SEC_L:
		*msg_id = AFE_MSG_ID_MSG_1;
		break;
	case AW_DEV_CH_SEC_R:
		*msg_id = AFE_MSG_ID_MSG_1;
		break;
	default:
		pr_err("%s: can not find msg num, channel %d \n", __func__, dev_ch);
		return -EINVAL;
	}

	pr_debug("%s: msg id[%d] \n", __func__, *msg_id);
	return 0;
}

#ifdef AW_MTK_PLATFORM_SPIN
static int aw_mtk_write_data_to_dsp(int msg_id, void *data, int size)
{
	int ret = -EINVAL;
	int32_t *dsp_data = NULL;
	struct aw_msg_hdr *hdr = NULL;

	dsp_data = kzalloc(sizeof(struct aw_msg_hdr) + size, GFP_KERNEL);
	if (!dsp_data) {
		pr_err("%s: kzalloc dsp_msg error\n", __func__);
		return -ENOMEM;
	}

	hdr = (struct aw_msg_hdr *)dsp_data;
	hdr->type = AW_DSP_MSG_TYPE_DATA;
	hdr->opcode_id = msg_id;
	hdr->version = AW_DSP_MSG_HDR_VER;

	memcpy(((char *)dsp_data) + sizeof(struct aw_msg_hdr), data, size);

	ret = mtk_spk_send_ipi_buf_to_dsp(dsp_data,
				sizeof(struct aw_msg_hdr) + size);
	if (ret < 0) {
		pr_err("%s: write data failed\n", __func__);
		kfree(dsp_data);
		dsp_data = NULL;
		return ret;
	}

	kfree(dsp_data);
	dsp_data = NULL;
	return 0;
}

static int aw_mtk_set_spin_angle(struct aw881xx *aw881xx, uint32_t spin_angle)
{
	int ret = -EINVAL;

	ret = aw_mtk_write_data_to_dsp(AW_MSG_ID_SPIN, &spin_angle, sizeof(uint32_t));
	if (ret)
		aw_dev_err(aw881xx->dev, "%s: write data to dsp failed\n", __func__);

	return ret;
}

static int aw_mtk_get_spin_angle(void *spin_angle, int size)
{
	int ret = -EINVAL;
	struct aw_msg_hdr hdr;

	hdr.type = AW_DSP_MSG_TYPE_CMD;
	hdr.opcode_id = AW_MSG_ID_SPIN;
	hdr.version = AW_DSP_MSG_HDR_VER;

	mutex_lock(&g_aw_dsp_lock);
	ret = mtk_spk_send_ipi_buf_to_dsp(&hdr, sizeof(struct aw_msg_hdr));
	if (ret < 0) {
		pr_err("%s:send cmd failed\n", __func__);
		mutex_unlock(&g_aw_dsp_lock);
		return ret;
	}

	ret = mtk_spk_recv_ipi_buf_from_dsp(spin_angle, size, &size);
	if (ret < 0) {
		pr_err("%s:get data failed\n", __func__);
		mutex_unlock(&g_aw_dsp_lock);
		return ret;
	}
	mutex_unlock(&g_aw_dsp_lock);

	return 0;
}

static int aw_mtk_set_mixer_en(struct aw881xx *aw881xx, uint32_t msg_id, int32_t is_enable)
{
	int ret = -EINVAL;
	int32_t *dsp_msg = NULL;
	struct aw_msg_hdr *hdr = NULL;

	dsp_msg = kzalloc(sizeof(struct aw_msg_hdr) + sizeof(int32_t), GFP_KERNEL);
	if (!dsp_msg) {
		aw_dev_err(aw881xx->dev, "%s: kzalloc dsp_msg error\n", __func__);
		return -ENOMEM;
	}
	hdr = (struct aw_msg_hdr *)dsp_msg;
	hdr->type = AW_DSP_MSG_TYPE_DATA;
	hdr->opcode_id = AW_INLINE_ID_AUDIO_MIX;
	hdr->version = AW_DSP_MSG_HDR_VER;

	memcpy(((char *)dsp_msg) + sizeof(struct aw_msg_hdr),
				(char*)&is_enable, sizeof(int32_t));

	ret = aw_mtk_write_data_to_dsp(msg_id, (void *)dsp_msg,
				sizeof(struct aw_msg_hdr) + sizeof(int32_t));
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: write data failed\n", __func__);
		kfree(dsp_msg);
		dsp_msg = NULL;
		return ret;
	}

	kfree(dsp_msg);
	dsp_msg = NULL;
	return 0;
}
#else
static int aw_check_dsp_ready(void)
{
	int ret = -EINVAL;

	ret = afe_get_topology(g_rx_port_id);

	pr_debug("%s: topo_id 0x%x\n", __func__, ret);

	if (ret != g_rx_topo_id)
		pr_err("%s: get rx_topo_id 0x%x ", __func__, ret);

	return 0;
}

static int aw_qcom_write_data_to_dsp(uint32_t msg_id, void *data, int size)
{
	int ret = -EINVAL;

	mutex_lock(&g_aw_dsp_lock);

	aw_check_dsp_ready();
	ret = aw_send_afe_cal_apr(msg_id, data, size, true);

	mutex_unlock(&g_aw_dsp_lock);

	return ret;
}

static int aw_qcom_read_data_from_dsp(uint32_t msg_id, void *data, int size)
{
	int ret = -EINVAL;

	mutex_lock(&g_aw_dsp_lock);

	aw_check_dsp_ready();
	ret = aw_send_afe_cal_apr(msg_id, data, size, false);

	mutex_unlock(&g_aw_dsp_lock);

	return ret;
}

static int aw_qcom_set_spin_angle(struct aw881xx *aw881xx,
					uint32_t spin_angle)
{
	int ret = -EINVAL;

	ret = aw_qcom_write_data_to_dsp(AW_MSG_ID_SPIN, &spin_angle, sizeof(uint32_t));
	if (ret)
		aw_dev_err(aw881xx->dev, "%s: write spin angle to dsp failed\n", __func__);
	else
		aw_dev_info(aw881xx->dev, "%s: write spin angle to dsp successful\n", __func__);

	return ret;
}

static int aw_qcom_get_spin_angle(uint32_t *spin_angle, int size)
{
	int ret = -EINVAL;

	ret = aw_qcom_read_data_from_dsp(AW_MSG_ID_SPIN, spin_angle, size);
	if (ret)
		pr_err("%s: get spin angle failed\n", __func__);
	else
		pr_info("%s: get spin angle successful\n", __func__);

	return ret;
}

static int aw_qcom_set_mixer_en(struct aw881xx *aw881xx,
						uint32_t msg_id, int32_t is_enable)
{
	int ret = -EINVAL;
	int32_t *dsp_msg;
	int msg_len = (int)(sizeof(struct aw_msg_hdr) + sizeof(int32_t));

	dsp_msg = kzalloc(msg_len, GFP_KERNEL);
	if (!dsp_msg) {
		aw_dev_err(aw881xx->dev, "%s: kzalloc dsp_msg error\n", __func__);
		return -ENOMEM;

	}
	dsp_msg[0] = AW_DSP_MSG_TYPE_DATA;
	dsp_msg[1] = AW_INLINE_ID_AUDIO_MIX;
	dsp_msg[2] = AW_DSP_MSG_HDR_VER;

	memcpy(dsp_msg + (sizeof(struct aw_msg_hdr) / sizeof(int32_t)),
		(char *)&is_enable, sizeof(int32_t));

	ret = aw_qcom_write_data_to_dsp(msg_id, (void *)dsp_msg, msg_len);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: write data to dsp failed\n", __func__);
		kfree(dsp_msg);
		dsp_msg = NULL;
		return ret;
	}

	aw_dev_dbg(aw881xx->dev, "%s: write data[%d] to dsp success\n", __func__, msg_len);
	kfree(dsp_msg);
	dsp_msg = NULL;
	return 0;
}
#endif

/***********************************spin_angle**********************************/
static int aw_set_adsp_spin_angle(struct aw881xx *aw881xx, uint32_t spin_angle)
{
	if (spin_angle >= AW_SPIN_MAX) {
		aw_dev_err(aw881xx->dev, "%s: spin_angle:%d not support\n",
				__func__, spin_angle);
		return -EINVAL;
	}

#ifdef AW_MTK_PLATFORM_SPIN
	return aw_mtk_set_spin_angle(aw881xx, spin_angle);
#else
	return aw_qcom_set_spin_angle(aw881xx, spin_angle);
#endif
}

static void aw_get_adsp_spin_angle(uint32_t *spin_angle)
{
#ifdef AW_MTK_PLATFORM_SPIN
	aw_mtk_get_spin_angle(spin_angle, sizeof(uint32_t));
#else
	aw_qcom_get_spin_angle(spin_angle, sizeof(uint32_t));
#endif
}
/*******************************************************************************/

/**********************************mixer_status*********************************/
static int aw_set_mixer_en(struct aw881xx *aw881xx, int32_t is_enable)
{
	int ret = -EINVAL;
	uint32_t msg_id;

	ret = aw_get_msg_id(aw881xx->channel, &msg_id);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: get msg_num failed\n", __func__);
		return ret;
	}

#ifdef AW_MTK_PLATFORM_SPIN
	ret = aw_mtk_set_mixer_en(aw881xx, msg_id, is_enable);
#else
	ret = aw_qcom_set_mixer_en(aw881xx, msg_id, is_enable);
#endif
	if (ret)
		aw_dev_err(aw881xx->dev, "%s: set mixer status failed\n", __func__);

	return ret;
}

int aw881xx_hold_reg_spin_st(struct aw_spin_desc *spin_desc)
{
	struct aw881xx *aw881xx = container_of(spin_desc,
						struct aw881xx, spin_desc);
	uint16_t reg_val;

	if (aw881xx == NULL) {
		pr_err("%s: aw881xx is NULL\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&g_aw_spin_lock);
	if ((g_spin_mode == AW_REG_SPIN_MODE) ||
		(g_spin_mode == AW_REG_MIXER_SPIN_MODE)) {
		/*set rx*/
		aw881xx_reg_read(aw881xx,
			AW881XX_REG_I2SCTRL, &reg_val);
		reg_val &= AW881XX_BIT_I2SCTRL_CHS_MASK;
		reg_val |= spin_desc->spin_table[g_spin_angle].rx_val;
		aw881xx_reg_write(aw881xx,
			AW881XX_REG_I2SCTRL, reg_val);

		/*set tx*/
		aw881xx_reg_read(aw881xx,
			AW881XX_REG_I2SCFG1, &reg_val);
		reg_val &= AW881XX_BIT_I2SCFG1_CHS_TX_MASK;
		reg_val |= spin_desc->spin_table[g_spin_angle].tx_val;
		aw881xx_reg_write(aw881xx,
			AW881XX_REG_I2SCFG1, reg_val);
	}
	mutex_unlock(&g_aw_spin_lock);

	return 0;
}

int aw881xx_check_spin_mode(struct aw_spin_desc *spin_desc)
{
	int ret = -EINVAL;
	struct list_head *pos = NULL;
	struct list_head *dev_list = NULL;
	int spin_mode = AW_SPIN_OFF_MODE;
	struct aw881xx *local_pa = NULL;
	struct aw881xx *aw881xx = container_of(spin_desc,
						struct aw881xx, spin_desc);

	if (g_spin_mode == AW_SPIN_OFF_MODE) {
		aw881xx->spin_flag = AW_SPIN_OFF;
		return 0;
	}

	ret = aw881xx_get_list_head(&dev_list);
	if (ret) {
		pr_err("%s: get dev list failed\n", __func__);
		return ret;
	}

	list_for_each(pos, dev_list) {
		local_pa = container_of(pos, struct aw881xx, list_node);
		spin_mode = aw881xx->spin_desc.spin_mode;
		if (g_spin_mode != spin_mode) {
			pr_err("%s: dev[%d] spin mode:%d not equal g_spin_mode:%d, check failed\n",
				__func__, aw881xx->channel, spin_mode, g_spin_mode);
			aw881xx->spin_flag = AW_SPIN_OFF;
			return -EINVAL;
		}
	}
	aw881xx->spin_flag = AW_SPIN_ON;

	return 0;
}

int aw881xx_hold_dsp_spin_st(struct aw_spin_desc *spin_desc)
{
	int ret = -EINVAL;
	struct aw881xx *aw881xx = container_of(spin_desc,
						struct aw881xx, spin_desc);

	if (aw881xx == NULL) {
		pr_err("%s: aw_dev is NULL\n", __func__);
		return -EINVAL;
	}

	if (aw881xx->channel == 0) {
		if (g_spin_mode == AW_ADSP_SPIN_MODE) {
			ret = aw_set_adsp_spin_angle(aw881xx,
							g_spin_angle);
			if (ret < 0)
				return ret;
		}
	}

	return ret;
}

static int aw_set_channal_mode(struct aw881xx *aw881xx,
					uint32_t spin_angle)
{
	int ret = -EINVAL;
	struct aw_spin_ch *spin_ch = &aw881xx->spin_desc.spin_table[spin_angle];
	ret = aw881xx_reg_write_bits(aw881xx, AW881XX_REG_I2SCTRL,
				AW881XX_BIT_I2SCTRL_CHS_MASK, spin_ch->rx_val);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: set rx failed\n", __func__);
		return ret;
	}

	ret = aw881xx_reg_write_bits(aw881xx, AW881XX_REG_I2SCFG1,
				AW881XX_BIT_I2SCFG1_CHS_TX_MASK, spin_ch->tx_val);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: set tx failed\n", __func__);
		return ret;
	}

	aw_dev_dbg(aw881xx->dev, "%s: set channel mode done!\n", __func__);

	return 0;
}

static int aw_set_reg_spin_angle(struct aw881xx *aw881xx, uint32_t spin_angle)
{
	int ret = -EINVAL;
	struct list_head *pos = NULL;
	struct list_head *dev_list = NULL;
	struct aw881xx *local_pa = NULL;

	if (spin_angle >= ARRAY_SIZE(aw_spin)) {
		aw_dev_err(aw881xx->dev, "%s: spin_angle:%d not support\n",
			__func__, spin_angle);
		return -EINVAL;
	}

	ret = aw881xx_get_list_head(&dev_list);
	if (ret) {
		aw_dev_err(aw881xx->dev, "%s: get dev list failed\n", __func__);
		return ret;
	}

	list_for_each(pos, dev_list) {
		local_pa = container_of(pos, struct aw881xx, list_node);
		ret = aw_set_channal_mode(local_pa, spin_angle);
		if (ret < 0) {
			aw_dev_err(aw881xx->dev, "%s: set channal mode failed\n",
				__func__);
			return ret;
		}
	}

	return 0;
}

static int aw_set_reg_mixer_spin_angle(struct aw881xx *aw881xx, uint32_t spin_angle)
{
	int ret = -EINVAL;

	if (spin_angle >= ARRAY_SIZE(aw_spin)) {
		aw_dev_err(aw881xx->dev, "%s: spin_angle:%d not support",
					__func__, spin_angle);
		return -EINVAL;
	}

	ret = aw_set_mixer_en(aw881xx, AW_AUDIO_MIX_ENABLE);
	if (ret)
		return ret;

	usleep_range(AW_100000_US, AW_100000_US + 10);

	aw_set_reg_spin_angle(aw881xx, spin_angle);

	ret = aw_set_mixer_en(aw881xx, AW_AUDIO_MIX_DISABLE);
	if (ret)
		return ret;

	return ret;
}

static void aw_get_reg_spin_angle(uint32_t *spin_angle)
{
	*spin_angle = g_spin_angle;

	pr_debug("%s: get spin:%s\n", __func__, aw_spin[g_spin_angle]);
}

static int aw_set_spin_angle(struct aw881xx *aw881xx, uint32_t spin_angle)
{
	switch (g_spin_mode) {
	case AW_REG_SPIN_MODE:
		return aw_set_reg_spin_angle(aw881xx, spin_angle);
	case AW_ADSP_SPIN_MODE:
		return aw_set_adsp_spin_angle(aw881xx, spin_angle);
	case AW_REG_MIXER_SPIN_MODE:
		return aw_set_reg_mixer_spin_angle(aw881xx, spin_angle);
	default:
		pr_err("%s: unsupported spin mode:%d\n", __func__, g_spin_mode);
		return -EINVAL;
	}
}

static void aw_set_spin_mode(int mode)
{
	g_spin_mode = mode;
}

static int aw_set_spin(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int ret = -EINVAL;
	struct aw881xx *aw881xx = (struct aw881xx *)kcontrol->private_value;
	uint32_t ctrl_value;

	aw_dev_dbg(aw881xx->dev, "%s: ucontrol->value.integer.value[0]=%ld\n",
			__func__, ucontrol->value.integer.value[0]);

	if (aw881xx->spin_flag == AW_SPIN_OFF) {
		aw_dev_dbg(aw881xx->dev, "%s: spin func not enable\n", __func__);
		return 0;
	}

	ctrl_value = ucontrol->value.integer.value[0];

	mutex_lock(&g_aw_spin_lock);
	if (aw881xx->pstream == AW881XX_AUDIO_START) {
		ret = aw_set_spin_angle(aw881xx, ctrl_value);
		if (ret < 0)
			aw_dev_err(aw881xx->dev, "%s: set spin error, ret=%d\n",
				__func__, ret);
	} else {
		if ((g_spin_mode == AW_REG_SPIN_MODE) || (g_spin_mode == AW_REG_MIXER_SPIN_MODE))
			aw_set_reg_spin_angle(aw881xx, ctrl_value);
		else
			aw_dev_info(aw881xx->dev, "%s: stream no start only record spin angle\n",
				__func__);
	}
	g_spin_angle = ctrl_value;
	mutex_unlock(&g_aw_spin_lock);

	return 0;
}

static void aw_get_spin_angle(uint32_t *spin_angle)
{
	if ((g_spin_mode == AW_REG_SPIN_MODE) || (g_spin_mode == AW_REG_MIXER_SPIN_MODE))
		aw_get_reg_spin_angle(spin_angle);
	else if (g_spin_mode == AW_ADSP_SPIN_MODE)
		aw_get_adsp_spin_angle(spin_angle);
}

static int aw_get_spin(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct aw881xx *aw881xx = (struct aw881xx *)kcontrol->private_value;
	uint32_t ctrl_value = 0;

	mutex_lock(&g_aw_spin_lock);
	if (aw881xx->pstream == AW881XX_AUDIO_START) {
		aw_get_spin_angle(&ctrl_value);
		ucontrol->value.integer.value[0] = ctrl_value;
	} else {
		ucontrol->value.integer.value[0] = g_spin_angle;
		aw_dev_dbg(aw881xx->dev, "%s:no stream, use record value\n",
			__func__);
	}
	mutex_unlock(&g_aw_spin_lock);

	return 0;
}

static int aw_spin_info(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_info *uinfo)
{
	struct aw881xx *aw881xx = (struct aw881xx *)kcontrol->private_value;
	int count = 0;

	if (aw881xx == NULL) {
		pr_err("%s: get struct aw881xx failed\n", __func__);
		return -EINVAL;
	}

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	count = ARRAY_SIZE(aw_spin);

	uinfo->value.enumerated.items = count;
		if (uinfo->value.enumerated.item >= count)
			uinfo->value.enumerated.item = count - 1;

	strlcpy(uinfo->value.enumerated.name,
			aw_spin[uinfo->value.enumerated.item],
			strlen(aw_spin[uinfo->value.enumerated.item]) + 1);

	return 0;
}

static int aw_spin_control_create(struct aw881xx *aw881xx)
{
	int kcontrol_num = 1;
	struct snd_kcontrol_new *aw_spin_control = NULL;
	char *kctl_name = NULL;

	aw_spin_control = devm_kzalloc(aw881xx->codec->dev,
			sizeof(struct snd_kcontrol_new) * kcontrol_num, GFP_KERNEL);
	if (aw_spin_control == NULL) {
		aw_dev_err(aw881xx->codec->dev, "%s: kcontrol malloc failed!\n", __func__);
		return -ENOMEM;
	}

	kctl_name = devm_kzalloc(aw881xx->codec->dev, AW_NAME_BUF_MAX, GFP_KERNEL);
	if (kctl_name == NULL)
		return -ENOMEM;

	snprintf(kctl_name, AW_NAME_BUF_MAX, "aw_spin_switch");

	aw_spin_control[0].name = kctl_name;
	aw_spin_control[0].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	aw_spin_control[0].info = aw_spin_info;
	aw_spin_control[0].get = aw_get_spin;
	aw_spin_control[0].put = aw_set_spin;
	aw_spin_control[0].private_value = (unsigned long)aw881xx;

	kctl_name = devm_kzalloc(aw881xx->codec->dev, AW_NAME_BUF_MAX, GFP_KERNEL);
	if (!kctl_name)
		return -ENOMEM;

	aw881xx->codec_ops->aw_snd_soc_add_codec_controls(aw881xx->codec,
		aw_spin_control, kcontrol_num);

	return 0;
}

void aw881xx_add_spin_controls(void *aw_dev)
{
	struct aw881xx *aw881xx = (struct aw881xx *)aw_dev;

	if (aw881xx->spin_desc.spin_mode != AW_SPIN_OFF_MODE)
		aw_spin_control_create(aw881xx);
}

static int aw_parse_spin_table_dt(struct aw881xx *aw881xx,
					struct device_node *np)
{
	int ret = -EINVAL;
	const char *str_data = NULL;
	char spin_table_str[AW_SPIN_MAX] = { 0 };
	int i, spin_count = 0;

	ret = of_property_read_string(np, "spin-data", &str_data);
	if (ret < 0) {
		aw_dev_err(aw881xx->dev, "%s: get spin_data failed, close spin function\n",
			__func__);
		return ret;
	}

	if (str_data == NULL) {
		aw_dev_err(aw881xx->dev, "str_data is NULL");
		return -EINVAL;
	}


	ret = sscanf(str_data, "%c %c %c %c",
				&spin_table_str[AW_SPIN_0], &spin_table_str[AW_SPIN_90],
				&spin_table_str[AW_SPIN_180], &spin_table_str[AW_SPIN_270]);
	if  (ret != AW_SPIN_MAX) {
		aw_dev_err(aw881xx->dev, "%s: unsupported str:%s, close spin function\n",
				__func__, str_data);
		return -EINVAL;
	}

	for (i = 0; i < AW_SPIN_MAX; i++) {
		if (spin_table_str[i] == 'l' || spin_table_str[i] == 'L') {
			aw881xx->spin_desc.spin_table[i].rx_val = AW881XX_BIT_I2SCTRL_CHS_RX_LEFT;
			aw881xx->spin_desc.spin_table[i].tx_val = AW881XX_BIT_I2SCFG1_CHS_TX_LEFT;
			spin_count++;
		} else if (spin_table_str[i] == 'r' || spin_table_str[i] == 'R') {
			aw881xx->spin_desc.spin_table[i].rx_val = AW881XX_BIT_I2SCTRL_CHS_RX_RIGHT;
			aw881xx->spin_desc.spin_table[i].tx_val = AW881XX_BIT_I2SCFG1_CHS_TX_RIGHT;
			spin_count++;
		} else {
			aw_dev_err(aw881xx->dev, "%s: unsupported str:%s, close spin function",
				__func__, str_data);
			return -EINVAL;
		}
	}

	if (spin_count != ARRAY_SIZE(aw_spin)) {
		aw_dev_err(aw881xx->dev, "%s:get spin_data failed, spin_count:%d\n",
			__func__, spin_count);
		return -EINVAL;
	}

	return 0;
}

static void aw_parse_topo_id_dt(struct aw881xx *aw881xx)
{
	int ret = -EINVAL;

	ret = of_property_read_u32(aw881xx->dev->of_node, "aw-rx-topo-id", &g_rx_topo_id);
	if (ret < 0) {
		g_rx_topo_id = AW_RX_TOPO_ID;
		aw_dev_info(aw881xx->dev, "read aw-rx-topo-id failed,use default");
	}

	aw_dev_info(aw881xx->dev, "rx-topo-id: 0x%x", g_rx_topo_id);
}

static void aw_parse_port_id_dt(struct aw881xx *aw881xx)
{
	int ret = -EINVAL;

	ret = of_property_read_u32(aw881xx->dev->of_node, "aw-rx-port-id", &g_rx_port_id);
	if (ret < 0) {
		g_rx_port_id = AW_RX_PORT_ID;
		aw_dev_info(aw881xx->dev, "read aw-rx-port-id failed,use default");
	}

	aw_set_port_id(0, g_rx_port_id);
	aw_dev_info(aw881xx->dev, "rx-port-id: 0x%x",
						g_rx_port_id);
}

static int aw_parse_spin_mode_dt(struct aw881xx *aw881xx)
{
	int ret = -EINVAL;
	const char *spin_mode = NULL;
	int mode;
	struct device_node *np = aw881xx->dev->of_node;

	ret = of_property_read_string(np, "spin-mode", &spin_mode);
	if (ret < 0) {
		aw_dev_info(aw881xx->dev,
			"%s: spin-mode get failed, spin switch off\n", __func__);
		aw881xx->spin_desc.spin_mode = AW_SPIN_OFF_MODE;
		return 0;
	}

	if (!strcmp(spin_mode, "dsp_spin"))
		mode = AW_ADSP_SPIN_MODE;
	else if (!strcmp(spin_mode, "reg_spin"))
		mode = AW_REG_SPIN_MODE;
	else if (!strcmp(spin_mode, "reg_mixer_spin"))
		mode = AW_REG_MIXER_SPIN_MODE;
	else
		mode = AW_SPIN_OFF_MODE;

	aw881xx->spin_desc.spin_mode = mode;

	aw_set_spin_mode(mode);

	if ((mode == AW_REG_SPIN_MODE) || (mode == AW_REG_MIXER_SPIN_MODE)) {
		ret = aw_parse_spin_table_dt(aw881xx, np);
		if (ret < 0) {
			aw881xx->spin_desc.spin_mode = AW_SPIN_OFF_MODE;
			aw_dev_err(aw881xx->dev,
				"%s: spin-table get failed, ret = %d\n", __func__, ret);
			return ret;
		}
	}

	aw_dev_info(aw881xx->dev, "%s: spin mode is %d\n", __func__, mode);

	return 0;
}

void aw881xx_spin_init(struct aw_spin_desc *spin_desc)
{
	struct aw881xx *aw881xx = container_of(spin_desc,
					struct aw881xx, spin_desc);

	aw_parse_spin_mode_dt(aw881xx);
	aw_parse_topo_id_dt(aw881xx);
	aw_parse_port_id_dt(aw881xx);
}

