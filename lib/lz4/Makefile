# SPDX-License-Identifier: GPL-2.0-only
ccflags-y += -O3

# lz4.c implements both the compressor and the decompressor, so add it once
# even when CONFIG_LZ4_COMPRESS and CONFIG_LZ4_DECOMPRESS are both enabled.
ifneq ($(filter y m,$(CONFIG_LZ4_COMPRESS)),)
obj-$(CONFIG_LZ4_COMPRESS) += lz4.o
else ifneq ($(filter y m,$(CONFIG_LZ4_DECOMPRESS)),)
obj-$(CONFIG_LZ4_DECOMPRESS) += lz4.o
endif

obj-$(CONFIG_LZ4HC_COMPRESS) += lz4hc.o
