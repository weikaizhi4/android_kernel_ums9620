# Makefile for the focaltech touchscreen drivers.
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen/focaltech_touch_V3_3/focaltech_test/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen/focaltech_touch_V3_3/firmware_config/$(CONFIG_TS_FIRMWARE)/
obj-$(CONFIG_TOUCHSCREEN_FTS_V3_3)	+=  focaltech_core.o
obj-$(CONFIG_TOUCHSCREEN_FTS_V3_3)	+=  focaltech_ex_fun.o
obj-$(CONFIG_TOUCHSCREEN_FTS_V3_3)	+=  focaltech_ex_mode.o
obj-$(CONFIG_TOUCHSCREEN_FTS_V3_3)	+=  focaltech_gesture.o
obj-$(CONFIG_TOUCHSCREEN_FTS_V3_3)	+=  focaltech_esdcheck.o
obj-$(CONFIG_TOUCHSCREEN_FTS_V3_3)   +=  focaltech_point_report_check.o
obj-$(CONFIG_TOUCHSCREEN_FTS_V3_3)   +=  focaltech_test/
obj-$(CONFIG_TOUCHSCREEN_FTS_V3_3)	+=  focaltech_flash.o
obj-$(CONFIG_TOUCHSCREEN_FTS_V3_3)	+= focaltech_common_interface.o
obj-$(CONFIG_TOUCHSCREEN_FTS_V3_3)   +=  focaltech_spi.o
