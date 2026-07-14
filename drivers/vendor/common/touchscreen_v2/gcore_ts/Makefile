#
# Makefile for the GalaxyCore touchscreen driver.
#
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen/gcore_ts/firmware_config/$(CONFIG_TS_FIRMWARE)/
obj-$(CONFIG_TOUCHSCREEN_GCORE_TS)	+=  gcore_drv_main.o
obj-$(CONFIG_TOUCHSCREEN_GCORE_TS)	+=  gcore_drv_qcom.o
obj-$(CONFIG_TOUCHSCREEN_GCORE_TS)	+=  gcore_drv_fw_update.o
obj-$(CONFIG_TOUCHSCREEN_GCORE_TS)	+=  gcore_drv_interface.o
obj-$(CONFIG_TOUCHSCREEN_GCORE_TS)	+=  gcore_drv_mp_test.o
obj-$(CONFIG_TOUCHSCREEN_GCORE_TS)	+=  gcore_common_interface.o
