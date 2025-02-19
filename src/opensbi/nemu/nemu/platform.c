/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2019 Western Digital Corporation or its affiliates.
 */

#include <sbi/riscv_asm.h>
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_const.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_platform.h>
#include <sbi/sbi_domain.h>
#include <sbi_utils/fdt/fdt_helper.h>
#include <sbi_utils/fdt/fdt_fixup.h>


/*
 * Include these files as needed.
 * See objects.mk PLATFORM_xxx configuration parameters.
 */
/*
#include <sbi_utils/ipi/aclint_mswi.h>
#include <sbi_utils/irqchip/plic.h>
#include <sbi_utils/serial/uart8250.h>
#include <sbi_utils/timer/aclint_mtimer.h>

#define PLATFORM_PLIC_ADDR		0xc000000
#define PLATFORM_PLIC_SIZE		(0x200000 + \
					 (PLATFORM_HART_COUNT * 0x1000))
#define PLATFORM_PLIC_NUM_SOURCES	128
#define PLATFORM_HART_COUNT		4
#define PLATFORM_CLINT_ADDR		0x2000000
#define PLATFORM_ACLINT_MTIMER_FREQ	10000000
#define PLATFORM_ACLINT_MSWI_ADDR	(PLATFORM_CLINT_ADDR + \
					 CLINT_MSWI_OFFSET)
#define PLATFORM_ACLINT_MTIMER_ADDR	(PLATFORM_CLINT_ADDR + \
					 CLINT_MTIMER_OFFSET)
#define PLATFORM_UART_ADDR		0x09000000
#define PLATFORM_UART_INPUT_FREQ	10000000
#define PLATFORM_UART_BAUDRATE		115200


static struct plic_data plic = {
	.addr = PLATFORM_PLIC_ADDR,
	.size = PLATFORM_PLIC_SIZE,
	.num_src = PLATFORM_PLIC_NUM_SOURCES,
	.context_map = {
		[0] = { 0, 1 },
		[1] = { 2, 3 },
		[2] = { 4, 5 },
		[3] = { 6, 7 },
	},
};

static struct aclint_mswi_data mswi = {
	.addr = PLATFORM_ACLINT_MSWI_ADDR,
	.size = ACLINT_MSWI_SIZE,
	.first_hartid = 0,
	.hart_count = PLATFORM_HART_COUNT,
};

static struct aclint_mtimer_data mtimer = {
	.mtime_freq = PLATFORM_ACLINT_MTIMER_FREQ,
	.mtime_addr = PLATFORM_ACLINT_MTIMER_ADDR +
		      ACLINT_DEFAULT_MTIME_OFFSET,
	.mtime_size = ACLINT_DEFAULT_MTIME_SIZE,
	.mtimecmp_addr = PLATFORM_ACLINT_MTIMER_ADDR +
			 ACLINT_DEFAULT_MTIMECMP_OFFSET,
	.mtimecmp_size = ACLINT_DEFAULT_MTIMECMP_SIZE,
	.first_hartid = 0,
	.hart_count = PLATFORM_HART_COUNT,
	.has_64bit_mmio = true,
};
*/

static int uart_getch(void)
{
	return -1;
}
static void uart_putch(char ch)
{
	char *serial_base = (char *)0xa0000000 + 0x00003f8;
	*serial_base	  = ch;
}

static struct sbi_console_device my_uart = { .name	   = "nemu_uart",
					     .console_putc = uart_putch,
					     .console_getc = uart_getch };

/*
 * Platform early initialization.
 */
static int platform_early_init(bool cold_boot)
{
	if (!cold_boot)
		return 0;

	sbi_console_set_device(&my_uart);
	return sbi_domain_root_add_memrange(0x10000000, PAGE_SIZE, PAGE_SIZE,
					    (SBI_DOMAIN_MEMREGION_MMIO |
					    SBI_DOMAIN_MEMREGION_SHARED_SURW_MRW));
	return 0;
}

/*
 * Platform final initialization.
 */
static int platform_final_init(bool cold_boot)
{
	//void *fdt;

	if (!cold_boot)
		return 0;

	//fdt = fdt_get_address_rw();

	//fdt_cpu_fixup(fdt);
	//fdt_fixups(fdt);

	return 0;
	return 0;
}

/*
 * Initialize the platform interrupt controller during cold boot.
 */
static int platform_irqchip_init(void)
{
	/* Example if the generic PLIC driver is used */
	//return plic_cold_irqchip_init(&plic);
	return 0;
}

/*
 * Initialize IPI during cold boot.
 */
static int platform_ipi_init(void)
{
	/* Example if the generic ACLINT driver is used */
	//return aclint_mswi_cold_init(&mswi);
	return 0;
}

/*
 * Initialize platform timer during cold boot.
 */
static int platform_timer_init(void)
{
	/* Example if the generic ACLINT driver is used */
	//return aclint_mtimer_cold_init(&mtimer, NULL);
	return 0;
}

/*
 * Platform descriptor.
 */
const struct sbi_platform_operations platform_ops = {
	.early_init   = platform_early_init,
	.final_init   = platform_final_init,
	.irqchip_init = platform_irqchip_init,
	.ipi_init     = platform_ipi_init,
	.timer_init   = platform_timer_init
};
const struct sbi_platform platform = {
	.opensbi_version   = OPENSBI_VERSION,
	.platform_version  = SBI_PLATFORM_VERSION(0x0, 0x00),
	.name		   = "nemu",
	.features	   = SBI_PLATFORM_DEFAULT_FEATURES,
	.hart_count	   = 1,
	.hart_stack_size   = SBI_PLATFORM_DEFAULT_HART_STACK_SIZE,
	.heap_size	   = SBI_PLATFORM_DEFAULT_HEAP_SIZE(1),
	.platform_ops_addr = (unsigned long)&platform_ops
};
