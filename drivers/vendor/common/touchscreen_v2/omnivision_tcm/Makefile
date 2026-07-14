#
# Makefile for the Omnivision TCM touchscreen driver.
#
# Each configuration option enables a list of files.

ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen/omnivision_tcm/firmware_config/$(CONFIG_TS_FIRMWARE)/

obj-$(CONFIG_TOUCHSCREEN_OMNIVISION_TCM) += omnivision_tcm.o

omnivision_tcm-objs := omnivision_common_interface.o
omnivision_tcm-objs += omnivision_tcm_core.o
omnivision_tcm-objs += omnivision_tcm_touch.o
omnivision_tcm-objs += omnivision_tcm_device.o
omnivision_tcm-objs += omnivision_tcm_testing.o
omnivision_tcm-objs += omnivision_tcm_recovery.o
omnivision_tcm-objs += omnivision_tcm_diagnostics.o

obj-$(CONFIG_TOUCHSCREEN_OMNIVISION_TCM_SPI) += omnivision_tcm_spi.o omnivision_tcm_zeroflash.o
obj-$(CONFIG_TOUCHSCREEN_OMNIVISION_TCM_I2C) += omnivision_tcm_i2c.o omnivision_tcm_reflash.o
