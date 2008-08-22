/*
 * linux/arch/arm/mach-davinci/common.c
 *
 * Code common to all DaVinci machines.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/console.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial_8250.h>
#include <linux/serial_reg.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/mach/map.h>
#include <asm/setup.h>

#include <mach/hardware.h>
#include <mach/board.h>
#include <mach/serial.h>
#include <mach/mux.h>

#include <mach/clock.h>

#define NO_LENGTH_CHECK 0xffffffff

unsigned char davinci_bootloader_tag[1024];
int davinci_bootloader_tag_len;

struct davinci_board_config_kernel *davinci_board_config;
int davinci_board_config_size;

#ifdef CONFIG_DAVINCI_BOOT_TAG

static int __init parse_tag_davinci(const struct tag *tag)
{
	u32 size = tag->hdr.size - (sizeof(tag->hdr) >> 2);

	size <<= 2;
	if (size > sizeof(davinci_bootloader_tag))
		return -1;

	memcpy(davinci_bootloader_tag, tag->u.davinci.data, size);
	davinci_bootloader_tag_len = size;

	return 0;
}

__tagtable(ATAG_BOARD, parse_tag_davinci);

#endif

static const void *get_config(u16 tag, size_t len, int skip, size_t *len_out)
{
	struct davinci_board_config_kernel *kinfo = NULL;
	int i;

#ifdef CONFIG_DAVINCI_BOOT_TAG
	struct davinci_board_config_entry *info = NULL;

	if (davinci_bootloader_tag_len > 4)
		info = (struct davinci_board_config_entry *)
				davinci_bootloader_tag;
	while (info != NULL) {
		u8 *next;

		if (info->tag == tag) {
			if (skip == 0)
				break;
			skip--;
		}

		if ((info->len & 0x03) != 0) {
			/* We bail out to avoid an alignment fault */
			printk(KERN_ERR "DAVINCI peripheral config: Length (%d)"
				 "not word-aligned (tag %04x)\n", info->len,
				 info->tag);
			return NULL;
		}
		next = (u8 *) info + sizeof(*info) + info->len;
		if (next >= davinci_bootloader_tag + davinci_bootloader_tag_len)
			info = NULL;
		else
			info = (struct davinci_board_config_entry *) next;
	}
	if (info != NULL) {
		/* Check the length as a lame attempt to check for
		 * binary inconsistency. */
		if (len != NO_LENGTH_CHECK) {
			/* Word-align len */
			if (len & 0x03)
				len = (len + 3) & ~0x03;
			if (info->len != len) {
				printk(KERN_ERR "DaVinci peripheral config: "
					"Length mismatch with tag %x"
					" (want %d, got %d)\n", tag, len,
					 info->len);
				return NULL;
			}
		}
		if (len_out != NULL)
			*len_out = info->len;
		return info->data;
	}
#endif
	/* Try to find the config from the board-specific structures
	 * in the kernel. */
	for (i = 0; i < davinci_board_config_size; i++) {
		if (davinci_board_config[i].tag == tag) {
			if (skip == 0) {
				kinfo = &davinci_board_config[i];
				break;
			} else {
				skip--;
			}
		}
	}
	if (kinfo == NULL)
		return NULL;
	return kinfo->data;
}

const void *__davinci_get_config(u16 tag, size_t len, int nr)
{
	return get_config(tag, len, nr, NULL);
}
EXPORT_SYMBOL(__davinci_get_config);

const void *davinci_get_var_config(u16 tag, size_t *len)
{
	return get_config(tag, NO_LENGTH_CHECK, 0, len);
}
EXPORT_SYMBOL(davinci_get_var_config);

static int __init davinci_add_serial_console(void)
{
	const struct davinci_serial_console_config *con_info;
	const struct davinci_uart_config *uart_info;
	static char speed[11], *opt;
	int line, i, uart_idx;

	uart_info = davinci_get_config(DAVINCI_TAG_UART,
					 struct davinci_uart_config);
	con_info = davinci_get_config(DAVINCI_TAG_SERIAL_CONSOLE,
					struct davinci_serial_console_config);
	if (uart_info == NULL || con_info == NULL)
		return 0;

	if (con_info->console_uart == 0)
		return 0;

	if (con_info->console_speed) {
		snprintf(speed, sizeof(speed), "%u", con_info->console_speed);
		opt = speed;
	}

	uart_idx = con_info->console_uart - 1;
	if (uart_idx >= DAVINCI_MAX_NR_UARTS) {
		printk(KERN_INFO "Console: external UART#%d. "
			"Not adding it as console this time.\n",
			uart_idx + 1);
		return 0;
	}
	if (!(uart_info->enabled_uarts & (1 << uart_idx))) {
		printk(KERN_ERR "Console: Selected UART#%d is "
			"not enabled for this platform\n",
			uart_idx + 1);
		return -1;
	}
	line = 0;
	for (i = 0; i < uart_idx; i++) {
		if (uart_info->enabled_uarts & (1 << i))
			line++;
	}
	return add_preferred_console("ttyS", line, opt);
}
console_initcall(davinci_add_serial_console);
