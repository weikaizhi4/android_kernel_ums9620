# SPDX-License-Identifier: GPL-2.0

ifeq ($(BSP_BUILD_FAMILY),sharkl3)
dtbo-$(CONFIG_ARCH_SPRD) += \
	sp9863a-1h10-go_32b-overlay.dtbo

sp9863a-1h10-go_32b-overlay.dtbo-base := sp9863a.dtb
endif


always      := $(dtb-y) $(dtbo-y)
subdir-y	:= $(dts-dirs)
clean-files := *.dtb *.dtbo
