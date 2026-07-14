# Makefile for the common kernel device drivers.

obj-$(CONFIG_ZTE_LEDS_AW210XX)	+= aw210xx/
obj-$(CONFIG_ZTE_LEDS_AW2016)	+= aw2016/

ifeq ($(CONFIG_VENDOR_ZTE_MISC),m)
	ccflags-y += -DCONFIG_VENDOR_ZTE_MISC_COMMON
endif