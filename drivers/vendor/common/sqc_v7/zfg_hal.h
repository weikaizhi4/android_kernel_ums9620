struct zfg_ops {
	/*arg*/
	void *arg;
	int (*zfg_get_bat_present)(void);
	int (*zfg_get_bat_voltage)(void);
	int (*zfg_get_bat_current)(void);
	int (*zfg_get_bat_soc)(void);
	int (*zfg_get_bat_high_accuracy_soc)(void);
	int (*zfg_get_bat_temperature)(void);
	int (*zfg_get_bat_avg_current)(void);
	int (*zfg_get_bat_cycle_count)(void);
	int (*zfg_get_bat_charge_full)(void);
	int (*zfg_get_bat_charge_full_design)(void);
	int (*zfg_get_bat_charge_counter)(void);
	int (*zfg_get_bat_health)(void);
	int (*zfg_get_bat_time_to_full_now)(void);
	int (*zfg_get_bat_time_to_empty_now)(void);
	int (*zfg_backup_uisoc)(u8 uisoc);
	int (*zfg_restore_uisoc)(u8 * uisoc);
	int (*zfg_get_bat_remain_cap)(void);
	int (*zfg_get_bat_full_chg_cap)(void);
};

int zfg_ops_register(struct zfg_ops *ops);
struct zfg_ops * zfg_ops_get(void);