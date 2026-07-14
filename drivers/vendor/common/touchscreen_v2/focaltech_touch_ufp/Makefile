# Makefile for the focaltech touchscreen drivers.
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/focaltech_touch_ufp/focaltech_test/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/focaltech_touch_ufp/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
obj-$(CONFIG_TOUCHSCREEN_FTS_UFP)	+=  focaltech_core.o
obj-$(CONFIG_TOUCHSCREEN_FTS_UFP)	+=  focaltech_ex_fun.o
obj-$(CONFIG_TOUCHSCREEN_FTS_UFP)	+=  focaltech_ex_mode.o
obj-$(CONFIG_TOUCHSCREEN_FTS_UFP)	+=  focaltech_gesture.o
obj-$(CONFIG_TOUCHSCREEN_FTS_UFP)	+=  focaltech_esdcheck.o
obj-$(CONFIG_TOUCHSCREEN_FTS_UFP)   +=  focaltech_point_report_check.o
obj-$(CONFIG_TOUCHSCREEN_FTS_UFP)   +=  focaltech_test/


obj-$(CONFIG_TOUCHSCREEN_FTS_UFP)	+=  focaltech_flash.o
obj-$(CONFIG_TOUCHSCREEN_FTS_UFP)   +=  focaltech_flash/
obj-$(CONFIG_TOUCHSCREEN_FTS_UFP)	+=  focaltech_common_interface.o




obj-$(CONFIG_TOUCHSCREEN_FTS_UFP)   +=  focaltech_spi.o
