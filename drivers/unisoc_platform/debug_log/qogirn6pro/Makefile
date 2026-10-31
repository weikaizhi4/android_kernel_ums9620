# The parent Makefile pulls this directory in with
#   obj-$(CONFIG_SPRD_MIPI_LOG_QOGIRN6PRO) += qogirn6pro/
# i.e. in module context.  With a hard-coded "obj-y" here kbuild still
# compiled main.o and stuffed it into built-in.a, but the object was then
# linked nowhere at all: the driver ended up neither in vmlinux nor in a .ko.
# The vendor ships it as the standalone mipi_driver.ko, so use obj-m.
obj-$(CONFIG_SPRD_MIPI_LOG_QOGIRN6PRO) += mipi_driver.o
mipi_driver-objs := main.o
