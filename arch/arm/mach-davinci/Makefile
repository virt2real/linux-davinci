#
# Makefile for the linux kernel.
#
#

# Common objects
obj-y 			:= time.o irq.o clock.o serial.o io.o id.o psc.o \
			   gpio.o devices.o usb.o dma.o iram.o

obj-$(CONFIG_DAVINCI_MUX)		+= mux.o mux_cfg.o

# Board specific
obj-$(CONFIG_MACH_DAVINCI_EVM)  	+= board-dm644x-evm.o
obj-$(CONFIG_MACH_DAVINCI_DM646X_EVM)	+= board-dm646x-evm.o
obj-$(CONFIG_MACH_DAVINCI_DM355_EVM)	+= board-dm355-evm.o
obj-$(CONFIG_MACH_SFFSDR)		+= board-sffsdr.o
