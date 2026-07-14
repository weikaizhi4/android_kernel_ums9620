#ifndef __SQC_COMMON_H__
#define __SQC_COMMON_H__

#include <linux/power_supply.h>

#define SQC_MAX_CHARGER_NUM 3

enum sqc_msg_status {
	SQC_ADAPTER_OK = 0,
	SQC_ADAPTER_NOT_SUPPORT = -1,
	SQC_ADAPTER_TIMEOUT = -2,
	SQC_ADAPTER_REJECT = -3,
	SQC_ADAPTER_ERROR = -4,
	SQC_ADAPTER_ADJUST = -5,
};

enum sqc_pd_base_cap {
	SQC_PD_BASE_SUPPORT_5V = 0,
	SQC_PD_BASE_SUPPORT_9V = 1,
	SQC_PD_BASE_SUPPORT_12V = 2,
	SQC_PD_BASE_SUPPORT_15V = 3,
	SQC_PD_BASE_SUPPORT_20V = 4,
};

enum sqc_pd_type {
	SQC_NONE_TYPE = 0,
	SQC_PD3D0_BASE_TYPE,
	SQC_PD3D0_APDO_TYPE,
	SQC_PD3D0_RESET_TYPE,
	SQC_SDP_TYPE,
	SQC_CDP_TYPE,
	SQC_DCP_TYPE,
	SQC_FLOAT_TYPE,
	SQC_QC2D0_5V_TYPE,
	SQC_QC2D0_9V_TYPE,
	SQC_QC2D0_12V_TYPE,
	SQC_QC3D0_TYPE,
	SQC_QC3D0_PLUS_18W_TYPE,
	SQC_QC3D0_PLUS_27W_TYPE,
	SQC_QC3D0_PLUS_45W_TYPE,
	SQC_SLEEP_MODE_TYPE,
};

enum sqc_interface_number {
	SQC_IFS_NONE = 0,
	/*CHARGER PROTOCOL COMMON*/
	SQC_IFS_PTL_COM_INIT,
	SQC_IFS_PTL_COM_EXIT,
	SQC_IFS_PTL_COM_CHG_TYPE,
	SQC_IFS_PTL_COM_STATUS,
	SQC_IFS_PTL_COM_RESET,
	SQC_IFS_PTL_COM_VENDOR_ID,

	/*USB PD*/
	SQC_IFS_PTL_PD_APDO,
	SQC_IFS_PTL_CABLE_TYPE,

	/*USB QC3.0*/
	QC_IFS_PTL_QC3D0_DP,
	QC_IFS_PTL_QC3D0_DM,

	/*USB QC3.0+*/
	QC_IFS_PTL_QC3D0_PLUS_DP,
	QC_IFS_PTL_QC3D0_PLUS_DM,

	/*PMIC CHAGE*/
	SQC_IFS_PMIC_INIT,
	SQC_IFS_PMIC_STATUS_GET,
	SQC_IFS_PMIC_INT_GET,
	SQC_IFS_PMIC_ENABLE,
	SQC_IFS_PMIC_FCV,
	SQC_IFS_PMIC_FCC,
	SQC_IFS_PMIC_ICL,
	SQC_IFS_PMIC_TOPOFF,
	SQC_IFS_PMIC_RECHG_SOC,
	SQC_IFS_PMIC_RECHG_VOLT,
	SQC_IFS_PMIC_ROLE,
	SQC_IFS_PMIC_BATT_OVP,
	SQC_IFS_PMIC_BATT_OVP_ALM,
	SQC_IFS_PMIC_BATT_OCP,
	SQC_IFS_PMIC_BATT_OCP_ALM,
	SQC_IFS_PMIC_BATT_UCP_ALM,
	SQC_IFS_PMIC_AC_OVP,
	SQC_IFS_PMIC_USB_OVP,
	SQC_IFS_PMIC_USB_OVP_ALM,
	SQC_IFS_PMIC_USB_OCP,
	SQC_IFS_PMIC_USB_OCP_ALM,

	/*STATUS NOW*/
	SQC_IFS_VBUS_NOW,
	SQC_IFS_IBUS_NOW,
	SQC_IFS_VBAT_NOW,
	SQC_IFS_IBAT_NOW,

	/*other*/
	SQC_IFS_POWERPATH_ENABLE,

	/*Keep it as the last line*/
	SQC_IFS_MAX,
	SQC_IFS_DIVISION = 256,
};

enum sqc_pmic_charger_type {
	SQC_CHARGER_PRIMARY = 0,
	SQC_CHARGER_CP1,
	SQC_CHARGER_CP2,
};

enum sqc_notify_dev_type {
	SQC_NOTIFY_CHG_BASE = 0,
	SQC_NOTIFY_CHG_CP1,
	SQC_NOTIFY_CHG_CP2,
	SQC_NOTIFY_USBPD,
	SQC_NOTIFY_USB,
};


enum sqc_notify_msg {
	SQC_ERR_VBAT_OVP = 0,
	SQC_ERR_IBAT_OCP = 1,
	SQC_ERR_VAC_OVP = 2,/*vbus before FET*/
	SQC_ERR_VDR_OVP = 3,/*voltage delta of FET*/
	SQC_ERR_VBUS_OVP = 4,
	SQC_ERR_IBUS_OCP = 5,
	SQC_ERR_ENGINE_OCP = 6,/*charger ocp*/
	SQC_ERR_ENGINE_OTP = 7,/*charger otp*/
	SQC_ERR_TSBUS_OTP = 8,/*usb interface otp*/
	SQC_ERR_TSBAT_OTP = 9,/*battery otp*/

	SQC_ERR_VBUS_HIHG = 10,
	SQC_ERR_VBUS_LOW = 11,
	SQC_ERR_VOUT_OVP = 12,
	SQC_ERR_VBUS_UVP = 13,

	SQC_ERR_VBAT_OVP_ALM = 14,
	SQC_ERR_IBAT_OCP_ALM = 15,
	SQC_ERR_IBAT_UCP_ALM = 16,
	SQC_ERR_VBUS_OVP_ALM = 17,
	SQC_ERR_IBUS_OCP_ALM = 18,
	SQC_ERR_ENGINE_OTP_ALM = 19,/*charger otp*/
	SQC_ERR_TSBAT_TSBUS_OTP_ALM = 20,

	SQC_ERR_VDELTA_RATIO_ALM = 21,

	SQC_NOTIFY_USB_STATUS_CHANGED = 22,

	SQC_NOTIFY_FAST_CHG_STATUS = 23,

};

enum sqc_cable_type {
	SQC_CABLE_TYPE_NONE = 0,
	SQC_CABLE_TYPE_A2C,
	SQC_CABLE_TYPE_C2C,
	SQC_CABLE_TYPE_MAX,
};

struct sqc_pd_proto_ops {
	void *arg;
	int (*status_init)(void);
	int (*status_remove)(void);
	int (*get_charger_type)(int *chg_type);
	int (*get_cable_type)(int *cable_type);
	int (*set_apdo_cap)(int mV, int mA);
	int (*get_apdo_cap)(int *max_vol_mV, int *max_curr_mA);
};

struct sqc_bc1d2_proto_ops {
	/*arg*/
	void *arg;

	int (*status_init)(void);
	int (*status_remove)(void);
	int (*get_charger_type)(int *chg_type);
	int (*set_charger_type)(int chg_type);
	int (*get_protocol_status)(unsigned int *status);
	int (*get_chip_vendor_id)(unsigned int *vendor_id);
	int (*set_qc3d0_dp)(unsigned int dp_cnt);
	int (*set_qc3d0_dm)(unsigned int dm_cnt);
	int (*set_qc3d0_plus_dp)(unsigned int dp_cnt);
	int (*set_qc3d0_plus_dm)(unsigned int dm_cnt);
};

struct sqc_pmic_chg_ops {
	/*pmic priv data*/
	void *arg;

	/*init*/
	int (*init_pmic_charger)(void *arg);

	/*linear charging parameter*/
	int (*set_chging_fcv)(void *arg, unsigned int mV);
	int (*get_chging_fcv)(void *arg, unsigned int *mV);
	int (*set_chging_fcc)(void *arg, unsigned int mA);
	int (*get_chging_fcc)(void *arg, unsigned int *mA);

	int (*set_chging_icl)(void *arg, unsigned int mA);
	int (*get_chging_icl)(void *arg, unsigned int *mA);
	int (*set_chging_topoff)(void *arg, unsigned int mA);
	int (*get_chging_topoff)(void *arg, unsigned int *mA);
	int (*set_rechg_soc)(void *arg, unsigned int raw_soc);
	int (*get_rechg_soc)(void *arg, unsigned int *raw_soc);
	int (*set_rechg_volt)(void *arg, unsigned int rechg_volt);
	int (*get_rechg_volt)(void *arg, unsigned int *rechg_volt);

	/*func*/
	int (*chg_enable)(void *arg, unsigned int enable);
	int (*chg_enable_get)(void *arg, unsigned int *enable);

	int (*get_chg_status)(void *arg, unsigned int *charing_status);
	int (*get_int_status)(void *arg, unsigned int *interrupt_status);

	int (*chg_role_set)(void *arg, unsigned int master);
	int (*chg_role_get)(void *arg, unsigned int *master);

	/*battery*/
	int (*batt_ovp_volt_set)(void *arg, unsigned int mV);
	int (*batt_ovp_volt_get)(void *arg, unsigned int *mV);
	int (*batt_ovp_alm_volt_set)(void *arg, unsigned int mV);
	int (*batt_ovp_alm_volt_get)(void *arg, unsigned int *mV);
	int (*batt_ocp_curr_set)(void *arg, unsigned int mA);
	int (*batt_ocp_curr_get)(void *arg, unsigned int *mA);
	int (*batt_ocp_alm_curr_set)(void *arg, unsigned int mA);
	int (*batt_ocp_alm_curr_get)(void *arg, unsigned int *mA);
	int (*batt_ucp_alm_curr_set)(void *arg, unsigned int mA);
	int (*batt_ucp_alm_curr_get)(void *arg, unsigned int *mA);
	int (*batt_ibat_get)(void *arg, unsigned int *mA);
	int (*batt_vbat_get)(void *arg, unsigned int *mV);

	/*ac*/
	int (*ac_ovp_volt_set)(void *arg, unsigned int mV);
	int (*ac_ovp_volt_get)(void *arg, unsigned int *mV);

	/*usb bus*/
	int (*usb_ovp_volt_set)(void *arg, unsigned int mV);
	int (*usb_ovp_volt_get)(void *arg, unsigned int *mV);
	int (*usb_ovp_alm_volt_set)(void *arg, unsigned int mV);
	int (*usb_ovp_alm_volt_get)(void *arg, unsigned int *mV);
	int (*usb_ocp_curr_set)(void *arg, unsigned int mA);
	int (*usb_ocp_curr_get)(void *arg, unsigned int *mA);
	int (*usb_ocp_alm_curr_set)(void *arg, unsigned int mA);
	int (*usb_ocp_alm_curr_get)(void *arg, unsigned int *mA);
	int (*usb_ibus_get)(void *arg, unsigned int *mA);
	int (*usb_vbus_get)(void *arg, unsigned int *mV);

	/*other*/
	int (*enable_path_set)(void *arg, int enabled);
	int (*enable_path_get)(void *arg, int *enabled);
};

int sqc_hal_charger_register(struct sqc_pmic_chg_ops * ops, int chg_type);

int sqc_hal_pd_register(struct sqc_pd_proto_ops * ops);

int sqc_hal_bc1d2_register(struct sqc_bc1d2_proto_ops * ops);
int sqc_hal_bc1d2_unregister(void);
void sqc_register_notify(struct notifier_block *nb);
void sqc_unregister_notify(struct notifier_block *nb);
void sqc_notify_changed(int type, void *val);
int sqc_get_property(enum power_supply_property psp,
	union power_supply_propval *val);
int sqc_set_property(enum power_supply_property psp,
	const union power_supply_propval *val);
void sqc_send_raw_capacity_event(int fast_capacity);
void sqc_sequence_load_init(int (*func_handle)(struct work_struct *work),
	unsigned char num, bool force_exit);



#endif

