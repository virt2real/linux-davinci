#
# Makefile for the linux kernel.
#
#

# Common objects
obj-y 			:= time.o irq.o clock.o serial.o io.o id.o psc.o \
			   gpio.o devices.o usb.o dma.o iram.o

obj-$(CONFIG_DAVINCI_MUX)		+= mux.o

# Chip specific
obj-$(CONFIG_ARCH_DAVINCI_DM644x)       += dm644x.o
obj-$(CONFIG_ARCH_DAVINCI_DM646x)       += dm646x.o
obj-$(CONFIG_ARCH_DAVINCI_DM355)        += dm355.o
obj-$(CONFIG_CP_INTC)			+= cp_intc.o

# Board specific
obj-$(CONFIG_MACH_DAVINCI_EVM)  	+= board-dm644x-evm.o
obj-$(CONFIG_MACH_DAVINCI_DM646X_EVM)	+= board-dm646x-evm.o
obj-$(CONFIG_MACH_DAVINCI_DM355_EVM)	+= board-dm355-evm.o
obj-$(CONFIG_MACH_SFFSDR)		+= board-sffsdr.o
