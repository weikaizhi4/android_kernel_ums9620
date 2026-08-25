ifneq ($(filter y m,$(CONFIG_VENDOR_ZTE_LOG_EXCEPTION)),)
	ccflags-y += -DCONFIG_VENDOR_ZTE_LOG_EXCEPTION
endif
snd-soc-aw883xx-objs := aw883xx.o aw883xx_monitor.o aw883xx_bin_parse.o aw883xx_device.o aw883xx_init.o aw883xx_calib.o aw883xx_spin.o
obj-$(CONFIG_ZTE_AW883XX_SMART_PA) += snd-soc-aw883xx.o
