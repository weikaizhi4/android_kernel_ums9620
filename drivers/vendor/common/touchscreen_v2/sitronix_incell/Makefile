
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen/sitronix_incell/firmware_config/$(CONFIG_TS_FIRMWARE)/

obj-$(CONFIG_TOUCHSCREEN_SITRONIX_INCELL)  += sitronix_touch.o
sitronix_touch-objs := sitronix_ts.o sitronix_st7123_i2c.o sitronix_st7123_spi.o sitronix_st7123_utility.o sitronix_st7123_mt.o sitronix_st7123_nodes.o sitronix_st7123_upgrade.o sitronix_st7123_test.o sitronix_common_interface.o




