#
# Makefile for the input core drivers.
#

# Each configuration option enables a list of files.
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen/tlsc6x_v3/firmware_config/$(CONFIG_TS_FIRMWARE)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen/tlsc6x_v3/tlsc_chip3535/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen/tlsc6x_v3/tlsc_chip3536/
obj-$(CONFIG_TOUCHSCREEN_TLSC6X_V3)  += tlsc6x_ts.o
tlsc6x_ts-objs := tlsc6x_main.o
tlsc6x_ts-objs += tlsc6x_comp.o
tlsc6x_ts-objs += tlsc6x_common_interface.o

