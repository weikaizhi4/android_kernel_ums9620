obj-$(CONFIG_VENDOR_ZTE_MISC) += zte_misc.o
obj-$(CONFIG_VENDOR_POWER_VOTER) += pmic-voter.o
obj-$(CONFIG_VENDOR_CHARGER_POLICY_SERVICE) += charger_policy_service.o
obj-$(CONFIG_VENDOR_BATTERY_CAPACITY_LEARNING) += battery_capacity_learning.o
obj-$(CONFIG_VENDOR_CHARGE_ARBITRATE_SERVICE) += charge_arbitrate_service.o
obj-$(CONFIG_VENDOR_CPU_RATE_CTRL) += cpu_rate.o

ifeq ($(CONFIG_VENDOR_CHARGER_POLICY_SERVICE),m)
	ccflags-y += -DCONFIG_VENDOR_CHARGER_POLICY_SERVICE_COMMON
endif

ifeq ($(CONFIG_VENDOR_ZTE_MISC),m)
	ccflags-y += -DCONFIG_VENDOR_ZTE_MISC_COMMON
endif