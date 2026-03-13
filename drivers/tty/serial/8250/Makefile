# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the 8250 serial device drivers.
#
ccflags-y += -I$(srctree)/drivers/tty/serial/8250
ccflags-y += -Wno-declaration-after-statement -Wno-attribute-warning -Werror
obj-$(CONFIG_DEVICE_MODULES_SERIAL_8250_MT6577)	+= 8250_mtk.o
