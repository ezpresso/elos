/*
 * ███████╗██╗      ██████╗ ███████╗
 * ██╔════╝██║     ██╔═══██╗██╔════╝
 * █████╗  ██║     ██║   ██║███████╗
 * ██╔══╝  ██║     ██║   ██║╚════██║
 * ███████╗███████╗╚██████╔╝███████║
 * ╚══════╝╚══════╝ ╚═════╝ ╚══════╝
 * 
 * Copyright (c) 2017, Elias Zell
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <kern/system.h>
#include <device/device.h>
#include <arch/bus.h>
#include <arch/x86.h>

static uint8_t x86_io_readb(bus_res_t *res, bus_off_t off) {
	return inb(bus_res_get_addr(res) + off);
}

static uint16_t x86_io_readw(bus_res_t *res, bus_off_t off) {
	return inw(bus_res_get_addr(res) + off);
}

static uint32_t x86_io_readl(bus_res_t *res, bus_off_t off) {
	return inl(bus_res_get_addr(res) + off);
}

static void x86_io_writeb(bus_res_t *res, bus_off_t off, uint8_t val) {
	outb(bus_res_get_addr(res) + off, val);
}

static void x86_io_writew(bus_res_t *res, bus_off_t off, uint16_t val) {
	outw(bus_res_get_addr(res) + off, val);
}

static void x86_io_writel(bus_res_t *res, bus_off_t off, uint32_t val) {
	outl(bus_res_get_addr(res) + off, val);
}

static uint8_t x86_mem_readb(bus_res_t *res, bus_off_t off) {
	return *(volatile uint8_t *)(res->map.map + off);
}

static uint16_t x86_mem_readw(bus_res_t *res, bus_off_t off) {
	return *(volatile uint16_t *)(res->map.map + off);
}

static uint32_t x86_mem_readl(bus_res_t *res, bus_off_t off) {
	return *(volatile uint32_t *)(res->map.map + off);
}

static uint64_t x86_mem_readq(bus_res_t *res, bus_off_t off) {
	return *(volatile uint64_t *)(res->map.map + off);
}

static void x86_mem_writeb(bus_res_t *res, bus_off_t off, uint8_t val) {
	*(volatile uint8_t *)(res->map.map + off) = val;
}

static void x86_mem_writew(bus_res_t *res, bus_off_t off, uint16_t val) {
	*(volatile uint16_t *)(res->map.map + off) = val;
}

static void x86_mem_writel(bus_res_t *res, bus_off_t off, uint32_t val) {
	*(volatile uint32_t *)(res->map.map + off) = val;
}

static void x86_mem_writeq(bus_res_t *res, bus_off_t off, uint64_t val) {
	*(volatile uint64_t *)(res->map.map + off) = val;
}

bus_res_acc_t x86_io_acc = {
	.readb = x86_io_readb,
	.readw = x86_io_readw,
	.readl = x86_io_readl,
	.readq = NULL,
	.writeb = x86_io_writeb,
	.writew = x86_io_writew,
	.writel = x86_io_writel,
	.writeq = NULL,
}; 

bus_res_acc_t x86_mem_acc = {
	.readb = x86_mem_readb,
	.readw = x86_mem_readw,
	.readl = x86_mem_readl,
	.readq = x86_mem_readq,
	.writeb = x86_mem_writeb,
	.writew = x86_mem_writew,
	.writel = x86_mem_writel,
	.writeq = x86_mem_writeq,
}; 
