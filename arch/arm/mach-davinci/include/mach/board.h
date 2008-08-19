/*
 *  linux/include/asm-arm/arch-davinci/board.h
 *
 *  Information structures for board-specific data
 *
 *  Derived from OMAP board.h:
 *  	Copyright (C) 2004	Nokia Corporation
 *  	Written by Juha Yrjölä <juha.yrjola@nokia.com>
 */

#ifndef _DAVINCI_BOARD_H
#define _DAVINCI_BOARD_H

#include <linux/types.h>

/* Different peripheral ids */
#define DAVINCI_TAG_UART		0x4f01
#define DAVINCI_TAG_SERIAL_CONSOLE	0x4f02

struct davinci_serial_console_config {
	u8 console_uart;
	u32 console_speed;
};

struct davinci_uart_config {
	/* Bit field of UARTs present; bit 0 --> UART1 */
	unsigned int enabled_uarts;
};

struct davinci_board_config_entry {
	u16 tag;
	u16 len;
	u8  data[0];
};

struct davinci_board_config_kernel {
	u16 tag;
	const void *data;
};

extern const void *__davinci_get_config(u16 tag, size_t len, int nr);

#define davinci_get_config(tag, type) \
	((const type *) __davinci_get_config((tag), sizeof(type), 0))
#define davinci_get_nr_config(tag, type, nr) \
	((const type *) __davinci_get_config((tag), sizeof(type), (nr)))

extern const void *davinci_get_var_config(u16 tag, size_t *len);

extern struct davinci_board_config_kernel *davinci_board_config;
extern int davinci_board_config_size;

#endif
