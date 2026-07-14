ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen/chipone_tddi_v2/firmware_config/$(CONFIG_TS_FIRMWARE)/

obj-$(CONFIG_TOUCHSCREEN_CHIPONE_V2) += chipone-tddi.o

chipone-tddi-objs := cts_i2c_driver.o
chipone-tddi-objs += cts_core.o cts_sfctrlv2.o cts_spi_flash.o cts_firmware.o cts_test.o
#chipone-tddi-objs += cts_charger_detect.o
chipone-tddi-objs += cts_hostcomm.o
chipone-tddi-objs += cts_tcs.o
chipone-tddi-objs += cts_platform.o
chipone-tddi-objs += cts_tool.o
chipone-tddi-objs += cts_sysfs.o
chipone-tddi-objs += cts_common_interface.o
chipone-tddi-objs += cts_selftest.o
chipone-tddi-objs += cts_selftest_ini.o
chipone-tddi-objs += cts_strerror.o
