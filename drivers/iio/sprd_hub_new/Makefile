#
# Makefile for sensor hub new drivers.
#
ifneq ($(filter y m,$(CONFIG_VENDOR_ZTE_LOG_EXCEPTION)),)
	ccflags-y += -DCONFIG_VENDOR_ZTE_LOG_EXCEPTION
	ccflags-y += -I$(srctree)/include/vendor/comdef/
endif

obj-$(CONFIG_SPRD_SENSOR_HUB_NEW)    += sensorhub.o
sensorhub-objs := shub_core.o shub_protocol.o shub_comm.o shub_algo_cmd_notify.o
