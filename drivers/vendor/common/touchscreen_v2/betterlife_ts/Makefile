ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen/betterlife_ts/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen/betterlife_ts/firmware_config/$(CONFIG_TS_FIRMWARE)/
obj-$(CONFIG_TOUCHSCREEN_BETTERLIFE_TS)  += betterlife_ts.o
betterlife_ts-objs := bl_ts.o
betterlife_ts-objs += fw_update.o
betterlife_ts-objs += bl_common_interface.o
obj-$(CONFIG_TOUCHSCREEN_BETTERLIFE_TS_FACTORY_TEST) += factory_test/


