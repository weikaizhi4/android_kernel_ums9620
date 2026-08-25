# SPDX-License-Identifier: GPL-2.0
ccflags-y += -I$(srctree)/include/misc
ccflags-y += -DOTT_UWE
ccflags-y += -DWOBLE_FUN

obj-$(CONFIG_SPRD_WCN_BT_TTY) += sprdbt_tty.o

sprdbt_tty-y := tty.o rfkill.o lpm.o woble.o alignment/sitm.o dump.o
