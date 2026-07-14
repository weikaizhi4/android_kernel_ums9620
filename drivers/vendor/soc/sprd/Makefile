#
# Makefile for the kernel device drivers.
#
subdir-ccflags-y += -I$(srctree)/drivers/vendor/common/zlog/zlog_common
ifeq ($(CONFIG_VENDOR_ZTE_LOG_EXCEPTION),m)
		subdir-ccflags-y+= -DCONFIG_VENDOR_ZTE_LOG_EXCEPTION
endif

obj-y += board_state/
obj-y += zte_boot_reason/
obj-y += zte_uidip_debug/
