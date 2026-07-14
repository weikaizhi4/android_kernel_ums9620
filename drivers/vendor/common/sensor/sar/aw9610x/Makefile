#
# Copyright (C) 2015 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#

# In case the platform does NOT support this type of sensors

ifdef CONFIG_VENDOR_SOC_MTK_COMPILE
ifeq ($(subst ",,$(CONFIG_MTK_SENSOR_ARCHITECTURE)), 1.0)
ccflags-y += -D SENSOR_ARCH_1_0
ccflags-y += -I$(srctree)/drivers/misc/mediatek/sensors-1.0/situation/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/sensors-1.0/hwmon/include
ccflags-y += -I$(srctree)/drivers/misc/mediatek/sensors-1.0/sensorHub/inc_v1
ccflags-y += -I$(srctree)/drivers/misc/mediatek/scp/$(CONFIG_MTK_PLATFORM)
endif
ifeq ($(subst ",,$(CONFIG_MTK_SENSOR_ARCHITECTURE)), 2.0)
ccflags-y += -I$(srctree)/drivers/misc/mediatek/sensor/2.0/core
ccflags-y += -D SENSOR_ARCH_2_0
subdir-ccflags-y += -Werror
KCOV_INSTRUMENT_lkdtm_rodata.o	:= n
CFLAGS_lkdtm_rodata.o		+= $(DISABLE_LTO)

OBJCOPYFLAGS :=
OBJCOPYFLAGS_lkdtm_rodata_objcopy.o := \
	--rename-section .text=.rodata,alloc,readonly,load
targets += lkdtm_rodata.o lkdtm_rodata_objcopy.o
$(obj)/lkdtm_rodata_objcopy.o: $(obj)/lkdtm_rodata.o FORCE
	$(call if_changed,objcopy)
endif
endif

mkfile_path := $(abspath $(lastword $(MAKEFILE_LIST)))
mkfile_dir := $(dir $(mkfile_path))

ccflags-y += -I$(mkfile_dir)/aw9610x_reg_config/$(CONFIG_SAR_PARA)/
$(warning "ljs with CONFIG_SAR_PARA=$(CONFIG_SAR_PARA)")

obj-$(CONFIG_SAR_SENSOR_AW9610X) += zte_sar.o
zte_sar-objs := aw_bin_parse.o aw9610x.o

obj-$(CONFIG_SAR_SENSOR_AW9610X_2) += zte_sar_2.o
zte_sar_2-objs += aw_bin_parse.o aw9610x_2.o