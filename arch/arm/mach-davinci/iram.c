/*
 * linux/arch/arm/mach-davinci/iram.c
 *
 * DaVinci iram allocation/free
 * Copyright (C) 2008 Boundary Devices.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <mach/memory.h>

/*
 * 2**14 (16K) / 2**5 (32) = 2**9 (512 bytes per bit)
 */
static atomic_t iram_mask;
int davinci_alloc_iram(unsigned size)
{
	unsigned int mask;
	unsigned int mask_prior;
	unsigned addr;
	unsigned cnt;
	cnt = (size + 511) >> 9;
	if (cnt >= 32)
		return 0;
	mask = atomic_read(&iram_mask);
	do {
		unsigned int need_mask = (1 << cnt) - 1;
		addr = DAVINCI_IRAM_BASE;
		while (mask & need_mask) {
			if (need_mask & (1<<31))
				return -ENOMEM;
			need_mask <<= 1;
			addr += 512;
		}
		mask_prior = mask;
		mask = atomic_cmpxchg(&iram_mask, mask, mask | need_mask);
	} while (mask != mask_prior);
	return addr;
}
EXPORT_SYMBOL(davinci_alloc_iram);

void davinci_free_iram(unsigned addr, unsigned size)
{
	unsigned mask;
	addr -= DAVINCI_IRAM_BASE;
	addr >>= 9;
	size = (size + 511) >> 9;
	if ((size + addr) >= 32)
		return;
	mask = ((1 << size) - 1) << addr;
	atomic_clear_mask(mask, (unsigned long *)&iram_mask.counter);
}
EXPORT_SYMBOL(davinci_free_iram);
