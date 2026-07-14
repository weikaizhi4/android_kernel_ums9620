# SPDX-License-Identifier: GPL-2.0
ifeq ($(BSP_BUILD_DT_OVERLAY),y)
ifeq ($(BSP_BUILD_ANDROID_OS),y)

ifeq ($(BSP_BUILD_FAMILY),qogirn6pro)
dtbo-$(CONFIG_ARCH_SPRD) += \
	ums9620-2h10-overlay.dtbo

ums9620-2h10-overlay.dtbo-base := ums9620.dtb
endif

endif #//BSP_BUILD_ANDROID_OS
else #//AiAiAi or No DTBO
dtb-$(CONFIG_ARCH_SPRD) += \
	ums9620.dtb
dtbo-$(CONFIG_ARCH_SPRD) += \
	ums9620-2h10-overlay.dtbo
endif #//BSP_BUILD_DT_OVERLAY

always		:= $(dtb-y) $(dtbo-y)
subdir-y	:= $(dts-dirs)
clean-files	:= *.dtb *.dtbo
