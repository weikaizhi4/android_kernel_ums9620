#
# Chipone For Patform
#
ifdef CONFIG_VENDOR_SOC_SPRD_COMPILE
	ccflags-y += -I$(srctree)/drivers/spi
endif

ifdef CONFIG_VENDOR_SOC_MTK_COMPILE
	ccflags-y += -I$(srctree)/drivers/spi
	ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
	ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat
	ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
endif

obj-$(CONFIG_PLATFORM_FINGERPRINT_CHIPONE) += fpsensor_spi.o
