# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_POWER_AVS)		+= avs/
obj-$(CONFIG_POWER_RESET)	+= reset/
obj-$(CONFIG_POWER_SUPPLY)	+= supply/
obj-$(CONFIG_ZTE_POWER_SUPPLY_COMMON)	+= zte_supply/

ifdef CONFIG_VENDOR_SQC_CHARGER
subdir-ccflags-y += -I$(srctree)/drivers/vendor/common/power
subdir-ccflags-y += -I$(srctree)/drivers/vendor/common/$(CONFIG_VENDOR_SQC_CHARGER_VERSION)
endif