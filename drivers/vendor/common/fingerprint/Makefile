#
# Makefile for zte fingerprint driver.
#

#zlog interface
ifeq ($(CONFIG_VENDOR_ZTE_LOG_EXCEPTION),m)
	ccflags-y += -DCONFIG_VENDOR_ZTE_LOG_EXCEPTION
	ccflags-y += -I$(srctree)/include/vendor/comdef/
endif

obj-$(CONFIG_ZTE_PLATFORM_FINGERPRINT) += zte_fp.o
zte_fp-y := zte_fp_entry.o

########### cdfinger ###############
zte_fp-$(CONFIG_PLATFORM_FINGERPRINT_CDFINGER)  += \
	cdfinger/cdfinger_tee.o

########### chipone ###############
zte_fp-$(CONFIG_PLATFORM_FINGERPRINT_CHIPONE)   += \
	chipone/fpsensor_spi.o

########### egis ###############
zte_fp-$(CONFIG_PLATFORM_FINGERPRINT_EGIS)      += \
	egis/et520.o \
	egis/ets_navi_input.o

########### goodix ###############
zte_fp-$(CONFIG_PLATFORM_FINGERPRINT_GOODIX)    += \
	goodix/gf_spi.o \
	goodix/platform.o \
	goodix/netlink.o

########### silead ###############
zte_fp-$(CONFIG_PLATFORM_FINGERPRINT_SILEAD)    += \
	silead/silead_fp.o \
	silead/silead_fp_platform.o

########### sunwave ###############
zte_fp-$(CONFIG_PLATFORM_FINGERPRINT_SUNWAVE)   += \
	sunwave/sf_ctl.o \
	sunwave/sf_hw.o

########### fpc ###############
zte_fp-$(CONFIG_PLATFORM_FINGERPRINT_FPC)   += \
	fpc/fpc_spi.o

zte_fp-$(CONFIG_PLATFORM_FINGERPRINT_FPC1020)   += \
	fpc1020/fpc1020.o

########### focaltech ###############
zte_fp-$(CONFIG_PLATFORM_FINGERPRINT_FOCALTECH)   += \
	focaltech/ff_ctl.o \
	focaltech/ff_hw.o

BOARD_VENDOR_RAMDISK_KERNEL_MODULES += 	$(BSP_KERNEL_MODULES_OUT)/zte_fp.ko
