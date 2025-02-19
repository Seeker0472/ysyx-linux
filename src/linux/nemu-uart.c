#include "linux/serial_core.h"
#include <linux/serial_core.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/console.h>

#include <linux/tty.h>
#include <linux/tty_flip.h>

#define DRIVER_NAME "nemu_uart"
//null functions
static int nemu_uart_startup(struct uart_port *port);

static void nemu_uart_set_termios(struct uart_port *port, struct ktermios *new,
				  struct ktermios *old)
{
	printk("NEMU_uart_set_termios!");
}
static void nemu_uart_stop_rx(struct uart_port *port)
{
	printk("NEMU_uart_stop_rx!");
}
static void nemu_uart_stop_tx(struct uart_port *port)
{
	printk("NEMU_uart_stop_tx!");
}
static void nemu_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	printk("NEMU_uart_set_mctrl");
}

static const char *nemu_uart_type(struct uart_port *port);
static void nemu_uart_start_tx(struct uart_port *port);
static unsigned int nemu_uart_tx_empty(struct uart_port *port);
static unsigned int nemu_uart_get_mctrl(struct uart_port *port);
static int nemu_uart_probe(struct platform_device *pdev);
static int nemu_uart_remove(struct platform_device *pdev);
static int __init nemu_uart_init(void);
static void __exit nemu_uart_exit(void);

static void nemu_uart_console_write(struct console *co, const char *s,
				    unsigned int count);
static int nemu_uart_console_setup(struct console *co, char *options);

static irqreturn_t nemu_uart_irq(int irq, void *dev_id);

static struct uart_driver nemu_uart_driver;

// for console
static struct console nemu_uart_console = {
	.name = "ttyNEMU",
	.write = nemu_uart_console_write,
	.device = uart_console_device,
	.setup = nemu_uart_console_setup,
	.flags = CON_PRINTBUFFER, // 允许输出内核启动日志
	.index = -1, // auto delegate
	.data = &nemu_uart_driver,
};

static const struct uart_ops nemu_uart_ops = {
	.tx_empty = nemu_uart_tx_empty,
	.set_mctrl = nemu_uart_set_mctrl,
	.get_mctrl = nemu_uart_get_mctrl,
	.start_tx = nemu_uart_start_tx,
	.stop_tx = nemu_uart_stop_tx,
	.stop_rx = nemu_uart_stop_rx,
	.startup = nemu_uart_startup,
	.set_termios = nemu_uart_set_termios,
	.type = nemu_uart_type,
};

static struct uart_driver nemu_uart_driver = {
	.owner = THIS_MODULE,
	.driver_name = DRIVER_NAME,
	.dev_name = "ttyNEMU",
	.major = 0,
	.minor = 64,
	.nr = 1,
	.cons = &nemu_uart_console,
	.state = NULL,
};

static struct uart_port nemu_uart_port = {
	.membase = (void __iomem *)0xa00003f8,
	.mapbase = 0xa00003f8,
	.irq = 0, // IRQ number
	.uartclk = 0, // clock rate
	.fifosize = 16, // FIFO size
	.iotype = UPIO_MEM,
	.line = 0,
	.type = 123,
	.ops = &nemu_uart_ops,
};

static const struct of_device_id nemu_uart_of_match[] = {
	{ .compatible = "seeker,nemu_uart" },
	{},
};
MODULE_DEVICE_TABLE(of, nemu_uart_of_match);

static struct platform_driver nemu_uart_platform_driver = {
    .probe = nemu_uart_probe,
    .remove = nemu_uart_remove,
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = nemu_uart_of_match,
    },
};
static int nemu_uart_startup(struct uart_port *port)
{
	printk("NEMU_uart_startup,registering irq_handlers...");
	int ret = request_irq(port->irq, nemu_uart_irq,
			      IRQF_TRIGGER_RISING, "nemu_uart",
			      port);
	if (ret){
    printk("register_irq_ERROR!");
    return ret;
  }
	return 0;
}
static irqreturn_t nemu_uart_irq(int irq, void *dev_id)
{
	//TODO:handles the intr
	//printk("nemu_uart_irq was called!");
  struct uart_port *port = dev_id;
  char ch = readb(port->membase);
  tty_insert_flip_char(&port->state->port,ch,TTY_NORMAL);
  tty_flip_buffer_push(&port->state->port);
  return IRQ_HANDLED;
}

static const char *nemu_uart_type(struct uart_port *port)
{
	return DRIVER_NAME;
}

static void nemu_uart_putchar(struct uart_port *port, int ch)
{
	writeb(ch, port->membase);
}

static void nemu_uart_start_tx(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;

	while (!uart_circ_empty(xmit)) {
		char ch = xmit->buf[xmit->tail];
		nemu_uart_putchar(port, ch);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
	}
	uart_write_wakeup(port);
}

static unsigned int nemu_uart_tx_empty(struct uart_port *port)
{
	// tx-fifo always empty!
	return TIOCSER_TEMT;
}

static unsigned int nemu_uart_get_mctrl(struct uart_port *port)
{
	// according to linux's low-level api,
	//  If the port does not support CTS, DCD or DSR,
	//  the driver should indicate that the signal is permanently active.
	return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
}

static int nemu_uart_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "No memory resource\n");
		return -ENODEV;
	}

	nemu_uart_port.mapbase = res->start;
	nemu_uart_port.membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(nemu_uart_port.membase)) {
		dev_err(&pdev->dev, "Failed to ioremap UART memory\n");
		return PTR_ERR(nemu_uart_port.membase);
	}

	nemu_uart_port.irq = platform_get_irq(pdev, 0);
	if (nemu_uart_port.irq <= 0) {
		dev_err(&pdev->dev, "No IRQ resource\n");
		return -ENODEV;
	}

	if (!nemu_uart_driver.state) {
		pr_debug("NEMU_uart: calling uart_register_driver()\n");
		ret = uart_register_driver(&nemu_uart_driver);
		if (ret < 0) {
			pr_err("NEMU_Failed to register UART driver\n");
			return ret;
		}
	}
	nemu_uart_port.dev = &pdev->dev;
	spin_lock_init(&(nemu_uart_port.lock));

	ret = uart_add_one_port(&nemu_uart_driver, &nemu_uart_port);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add UART port\n");
		return ret;
	}

	return 0;
}

static int nemu_uart_remove(struct platform_device *pdev)
{
	struct uart_port *port = platform_get_drvdata(pdev);
	uart_remove_one_port(&nemu_uart_driver, port);
	return 0;
}

static int __init nemu_uart_init(void)
{
	int ret;

	pr_debug("NEMU_uart: calling platform_driver_register()\n");
	ret = platform_driver_register(&nemu_uart_platform_driver);
	if (ret) {
		pr_err("Failed to register platform driver\n");
	}
	return 0;
}

static void __exit nemu_uart_exit(void)
{
	platform_driver_unregister(&nemu_uart_platform_driver);
	uart_unregister_driver(&nemu_uart_driver);
}
static void nemu_uart_console_write(struct console *co, const char *s,
				    unsigned int count)
{
	struct uart_port *port = &nemu_uart_port;
	// the function performs a character by character write, translating newlines to CRLF sequences.
	// turnning '\n' ->  '\n\r' ?
	uart_console_write(port, s, count, nemu_uart_putchar);
	uart_write_wakeup(port);
}

static int nemu_uart_console_setup(struct console *co, char *options)
{
	printk("calling nemu_console_setup!");
	return 0;
}

module_init(nemu_uart_init);
module_exit(nemu_uart_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Seeker");
MODULE_DESCRIPTION("NEMU UART Driver");
