
# Port-Linux-on-NEMU
## About

正在尝试整理笔记, 目前内容非常流水帐,目前linux部分的内容完全没整理

## 启动 linux 的多种方式

- `fsbl->opensbi->linux`
- `fsbl->opensbi->u-boot->linux`
- `uboot-spl->opensbi->u-boot->linux`

在 nemu 上都不用实现 fsbl, 所以可以选择最简单的方法: `opensbi->linux`

> 可以参考 `fpga/arine`
### About OpenSBI

> "硅基大陆的宪法仍在，城邦却铸造着各自的货币"

提供标准SBI接口、隔离硬件访问

1.虽然有统一的标准, 但是不同RISC-V硬件实现的差异还是太多了, 比如用多少个 `pmp` 寄存器, 相关硬件的早期初始化都不一样, opensbi 就是负责启动早期的工作的

2.抽象和安全
当计算机世界一个东西变得足够复杂的时候, 就创建一个抽象层来简化它

所以启动带 mmu 的 linux 一定要用 opensbi

![](./attachments/20250215_19h25m26s_grim.png)

1. A platform-specific firmware running in M-mode and a bootloader, a hypervisor or a general-purpose OS executing in S-mode or HS-mode.
2. A hypervisor running in HS-mode and a bootloader or a general-purpose OS executing in VS-mode.

### 所以 uboot 是干什么的?

我的理解:更高级的支持->支持命令行/从 `tftp` 服务器上下载文件, 更复杂的硬件和安全支持.
但显然目前在 `nemu/npc` 上不需要

### Opensbi 的多种模式

- `FW_PAYLOAD` 把下一个阶段的内容直接打包进 `opensbi` 生成的 `binary` 里面
- `FW_JUMP` 直接跳转到一个固定的地址
- `FW_DYNAMIC` 从上一个 booting-stage 获取信息 (比如上一个 stage 已经把 `opensbi` 和系统准备好了)

在 `nemu` 上用 ` FW_PAYLOAD ` 是最省力的

## 阅读手册

为啥不去看看 rv 手册呢 (Volume II, ch 1,2,3,10)

- `(Reserved Writes Preserve Values, Reads Ignore Value)WPRI`
- `(Write/Read Only Legal Values)WLRL`
- `(Write Any Values, Reads Legal Values)WARL`

Opensbi 在启动的过程中就会尝试给很多 csr 寄存器写数值, 然后再读取出来, 
- 如果寄存器没有实现, 就会抛出 `illegal instruction fault`, 这时候跳转到 `Opensbi` 自己的异常处理程序里面, 如果这个 csr 是必须的, 那么 opensbi 会抛一个异常停下来, 如果不是必须的, 那么接下来就不使用这个寄存器继续
- 如果硬件某些寄存器的位没有实现, Opensbi 会不使用这个位

`csr` 寄存器可以通过索引的高四位判断权限/RW 权限等等->硬件实现就简单了

### 思考: 我们需要实现哪些 `csr`?

如果目标仅仅是<我要把 `linux` 正常跑起来>的话

- 首先排除所有拓展
- 排除和安全相关的 `csr`
- 其实可以直接启动 `opensbi-FW_JUMP` 模式, 把 `opensbi` 的输出调好, 只要能正常跑到跳转的地方就说明 `csr` 已经实现的差不多了

## 更强的基础设施

### 实现 `difftest`

#### 阅读 `Spike` 源码

重要的文件
- `processor.h/state_t`：里面包含了 spike 的状态 (全部的寄存器)
- `../difftest.cc/difftest_init`: 里面包含了 spike 的初始化参数->只实例化 16 个 `pmpregions`
- `csrs.cc`：里面有各种 csr 寄存器的行为

##### 比较有意思的事情

- 是先 fetch->dedode->放进 icache 里面, 感觉这样能比较高效地利用程序的局部性来加速!
- Decode 使用了查找表加速!
- Instndecode-> `processor.cc->illegal_instruction` 使用 try-catch
- 有一些寄存器有 MASK (在 `csrrs.cc` 中)
- 指令的实现在 `riscv/insns/*.h` 中

#### 同步 nemu/spike
回想起之前手册的内容, 访问没有实现的 CSR 寄存器的时候会抛出 Illegal Instruction Fault,
那么我们就修改 Spike 的代码直接让执行这条指令的时候同步抛出这个异常就行了

所有 csr 指令都会首先 get_csr, 如果 csr 不存在就抛异常, 所以只要在不打算实现的 csr 上抛出一个异常就行了
```c
bool difftest_dut_csr_notexist = false;

// Note that get_csr is sometimes called when read side-effects should not
// be actioned.  In other words, Spike cannot currently support CSRs with
// side effects on reads.
reg_t processor_t::get_csr(int which, insn_t insn, bool write, bool peek)
{
  if(difftest_dut_csr_notexist) {
    difftest_dut_csr_notexist=false;
    printf("spike:stepping DUT(nemu,npc)'s unimplemented csr\n");
    throw trap_illegal_instruction(insn.bits());
  }
  auto search = state.csrmap.find(which);
  if (search != state.csrmap.end()) {
    if (!peek)
      search->second->verify_permissions(insn, write);
    return search->second->read();
  }
  printf("spike:stepping REF(spike)'s unimplemented csr\n");
  // If we get here, the CSR doesn't exist.  Unimplemented CSRs always throw
  // illegal-instruction exceptions, not virtual-instruction exceptions.
  throw trap_illegal_instruction(insn.bits());
}
```
##### WARN: 不要使用 `ref_difftest_raise_intr`

```c
void difftest_step_raise(uint64_t NO) {
//step
  ref_difftest_exec(1);
//rasie intr
  ref_difftest_raise_intr(NO);
//set step
  difftest_skip_ref();
  ref_difftest_regcpy(&cpu, DIFFTEST_TO_REF);
}
```
有副作用的!

#### 实现 difftest_csr

- 修改了 `difftest_init` 的 api, 传入需要 diff 的 csr 的索引数组
- 怎加了一个 diff-csr 的 api, 把 csr 的内容按照数组的顺序反回
- 宏真好用 ()

### 接入 gdb

使用 lxy 大佬分享的项目可以轻松实现, 经过了一些小改动, 甚至可以传 target descx

`tmux split-window -h -p 65 "riscv64-unknown-linux-gnu-gdb -ex \"target remote localhost:1234\" $(ELF)"`

`ELFS :='-ex \"set confirm off\" -ex \"symbol-file ${PWD}/opensbi/build/platform/nemu/firmware/fw_payload.elf\" -ex \"add-symbol-file ${PWD}/linux/vmlinux\" -ex \"set confirm on\"'`

## 技术选型

**非常不建议完全按照我的方法走!**

一开始在感觉*给 `NEMU` “移植” ` linux ` 的过程中用`NEMU`来模拟硬件的行为是不是怪怪的*
所以我选择了不改动 `nemu` 的实现 (比如 ` uart `) ,而是给 `opensbi` / `linux` 写驱动 (但这样会花很多时间)

~~然后写 `linux-uart` 驱动的时候发现自己小看了 `linux` 的复杂程度 (:-~~


写到 PLIC 的时候才发现 nemu 的 uart 可以轻松修改兼容标准
## 移植 `Opensbi` 

主要参考了 `opensbi/docs/platform_guide.md` ,但是,如果 `nemu` 模拟了 `UART16550` 的话, 其实更推荐使用 Opensbi 官方提供的 [`Generic Platform`](https://github.com/riscv-software-src/opensbi/blob/master/docs/platform/generic.md) ,根据官网介绍可以直接按照设备树来自行加载驱动

### 创建一个新的 platform

从 `platform/template` 里面复制然后稍作修改

### 设置 `Makefile` 的参数
```
PLATFORM_RISCV_XLEN = 32
PLATFORM_RISCV_ABI = ilp32
PLATFORM_RISCV_ISA = rv32ima_zicsr_zifencei
PLATFORM_RISCV_CODE_MODEL = medany

FW_DYNAMIC=n

FW_JUMP=y
FW_TEXT_START=0x80000000
FW_JUMP_ADDR=0x0
```
这里可以先把 `FW_JUMP_ADDR` 设置成 0, 如果执行 `mret` 之后跳转到了 0 就说明 ` opensbi ` 执行完了

```
make CROSS_COMPILE=riscv64-unknown-linux-gnu- PLATFORM=nemu
```
生成的二进制文件: `./build/platform/nemu/firmware/fw_jump.bin`

### 让 `opensbi` 正常输出字符 (适配 `nemu-uart` )
主要参考 `int uart8250_init(unsigned long base, u32 in_freq, u32 baudrate, u32 reg_shift,u32 reg_width, u32 reg_offset)` 这个函数的代码, 主要要调用 `sbi_console_set_device` `sbi_domain_root_add_memrange` 这两个函数, 然后自己实现一个 `nemu-uart` 的驱动, 这样就能看到字符的正常输出了
```c
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
```

如果实现比较正常, 那么你应该能看见输出信息 (要么是 `Opensbi` 的欢迎界面, 要么是 `Opensbi` 报错某个寄存器没有实现)
```
system_opcode_insn: Failed to access CSR 0x104 from M-mode
sbi_trap_error: hart0: trap0: illegal instruction handler failed (error -1)
```
### 阅读 `Opensbi` 的源码

`sbi_csr_detect.h/csr_read_allowed//csr_write_allowed` 检测寄存器是否支持读写!
`sbi_hart` 里面 `hart_detect_features` 会检测平台支持的寄存器是否存在等, 它包括异常处理, 允许后续恢复现场

如果看了 opensbi 的汇编代码, 会发现 `csr_read_num` 等函数里面有很多 `csr` 寄存器, 但其实不一定都要实现

### 向 `nemu` 添加更多的寄存器

我不选择"一口气把所有手册中定义的 csr 全部实现"因为感觉会陷入名为<细节>的黑洞

听北京基地的某位大佬说香山的 `nemu` 的 `csr` 实现的非常巧妙, 感兴趣可以参考, 但我没看 (:-

#### 宏魔法

在实现过程中可能要频繁修改 `csr` 寄存器的列表, 我希望通过宏定义实现相对统一的寄存器管理: 在头文件中添加了一个寄存器之后：
- 自动为寄存器的索引生成一个常量
- `Difftest` 的时候会自动比较这个寄存器
- `gdb/sdb` 能读取/显示/打断点这个寄存器
所以我使用了 `define` 和 `undef` 组合, 让一个宏有多种展开方式

```c
#define CSR_LIST \
  GenCSR(MHARTID, 0xf14) \
  GenCSR(MSTATUS, 0x300) \
  ...
  
  #define GenCSR(name, paddr) \
  static const uint32_t NEMU_CSR_V_##name = paddr; \
  static const uint32_t NEMU_CSR_##name = paddr;
	CSR_LIST
  #undef GenCSR
...
  static const char *difftest_csr_name[] = {
#define GenCSR(NAME,IDX) #NAME,
  CSR_DIFF_LIST
#undef GenCSR
};
...

#define GenCSR(name, paddr) \
  "<reg name=\"" #name "\" bitsize=\"32\" type=\"int\" regnum=\"" #paddr "\" />\n"
  ...
  
```

#### 位域

```c
typedef union {
  struct {
    unsigned int       : 1;
    unsigned int SSIE  : 1;
    unsigned int       : 1;
    unsigned int MSIE  : 1;
    unsigned int       : 1;
    unsigned int STIE  : 1;
    unsigned int       : 1;
    unsigned int MTIE  : 1;
    unsigned int       : 1;
    unsigned int SEIE  : 1;
    unsigned int       : 1;
    unsigned int MEIE  : 1;
    unsigned int       : 1;
    unsigned int LCOFIE: 1;
    unsigned int       : 18;
  } bits;
  uint32_t value;
} mie_t;
#define NEMU_mie ((mie_t *)(&cpu.csr[NEMU_CSR_MIE]))

//使用
NEMU_mie->bits.STIE = xxx;
NEMU_mie->value = xxx;
```

> TODO: 这是不是 UB?

#### 寄存器的细节
指令运行执行过程中**当前正在执行的指令直接触发**的异常一般是**同步异常（Synchronous Exception）**, 要立刻阻塞当前的指令执行流, 并且指令本身不应该产生其他的副作用。
`word_t isa_raise_intr(word_t NO, vaddr_t epc)` didn't work!
当然我们可以用一个参数来表示是否成功, 但是
考虑这一个指令
`INSTPAT("??????? ????? ????? 001 ????? 11100 11", csrrw , I, R(rd)=CSRR(imm&0xfff,s);CSRW(imm&0xfff,s)=src1);`
可能会发生什么呢?
- 访问的 csr 不存在, 抛出 illegal instruction fault
- 没有权限访问 csr, 抛出 illegal instruction fault
- 取指过程中出现 page fault, 抛出Instruction page fault
对于 L/S, 还可能会抛出 `Load page fault` / `Store/AMO page fault`
这么多不同的地方会抛出这么多不同的错误, 这也太不"优雅"了!
所以 Spike 选择用 try-catch, 但是我们的 c 没有😭

回忆 15-213 ,老师似乎讲过一个 none-local-jump 的东西, 允许程序直接从一个很深的调用栈里面直接跳出跳转到某个位置, 查询资料, 找到了 `set-jump` 函数, 完美地满足了我的要求

```c
int isa_exec_once(Decode *s) {
  int jump_value = setjmp(memerr_jump_buffer);
  if(jump_value!=0){
    return exception_exec(jump_value,s);
  }
  ...
}
```

## Linux!

> TODO:好像从 linux 内核 `6.x` 开始 `menuconfig` 就没有 `riscv32编译选项了`？

建议先从 `defconfig` 改动, 而不是 `tinyconfig` 改动, 先把 linux 跑起来再说

### 统计 linux 需要多少 csr

为啥不先看看 linux 访问了那些寄存器呢?

但注意:有一个 time (timeh) 寄存器反汇编出来的指令是 rdtime/rdtimeh


> TODO: 内部中断和外部中断

首先 Objdump 出 `vmlinux` 的内容, 然后可以写一个简单的 Python 脚本来统计总共访问了哪些 csr 寄存器

```python
import re
import sys

def find_csr_registers(disassembly):
    csr_pattern = re.compile(r'.*csr[a-z]{1,2}\t.*')
    csrr_pattern = re.compile(r'.*csrr\t.*')
    csr_registers = set()

    for line in disassembly.split('\n'):
        match = csr_pattern.search(line)
        csrr_match = csrr_pattern.search(line)
        if csrr_match:
            result=re.split(r'[,\t]+',line)[-1]
            csr_registers.add(result)
        elif match:
            result=re.split(r'[,\t]+',line)[-2]
            csr_registers.add(result)

    return sorted(csr_registers)

if __name__ == "__main__":

    with open("./result.txt", 'r') as f:
        disassembly = f.read()

    csr_list = find_csr_registers(disassembly)

    print("Used CSR registers:")
    for csr in csr_list:
        print(f"- {csr}")
```

#### 来自真实系统的 tradeoff

在真实的系统中, 时钟一般不会设计成一个寄存器/csr 的形式, 因为会有多个 hart 同步/关机/动态调频的问题, 一般设计成 MMIO

来自 riscv-spec
```
Accurate real-time clocks (RTCs) are relatively expensive to provide (requiring a crystal or
MEMS oscillator) and have to run even when the rest of system is powered down, and so
there is usually only one in a system located in a different frequency/voltage domain from
the processors. Hence, the RTC must be shared by all the harts in a system and accesses to
the RTC will potentially incur the penalty of a voltage-level-shifter and clock-domain
crossing. It is thus more natural to expose mtime as a memory-mapped register than as a
CSR.
```

需要给设备树的 `cpus` 节点加一个
```
		timebase-frequency = <1000000>;
```
```
#2  0x8091d2d8 in panic (fmt=fmt@entry=0x81410b78 <payload_bin+12651384> "\0014RISC-V system with no 'timebase-frequency' in DTS\n")
    at kernel/panic.c:443
```
### Kernel 跑着跑着 hit good (bad) trap 了?

看汇编发现指令中混入了一个 ebreak!

为什么会 call ebreak: 因为有 BUG_ON 宏触发了, 通常是 menuconfig 有问题

```
BUG_ON((PAGE_OFFSET % PGDIR_SIZE) != 0);

BUG_ON()
#define BUG_ON(condition) do { if (unlikely(condition)) BUG(); } while (0)
#define __BUG_FLAGS(flags) do {					\
	__asm__ __volatile__ ("ebreak\n");			\
} while (0)

#define BUG() do {						\
	__BUG_FLAGS(0);						\
	unreachable();						\
} while (0)
```
```c
asmlinkage void __init setup_vm(uintptr_t dtb_pa)
{
...
	/* Sanity check alignment and size */
	BUG_ON((PAGE_OFFSET % PGDIR_SIZE) != 0);
	BUG_ON((kernel_map.phys_addr % PMD_SIZE) != 0);
...
}
```


Pte 0 似乎放在了 0 x 80400000=>不是放在了这里, 这是一个叶子节点!

> TODO: 这里贴一段映射空间的代码, 包含判断要映射大页的逻辑
### 设备树

> TODO: 补上作用

第一次学设备树会觉得很抽象, 其实可以直接额参考文档/其他设备的 example
设备"树"有很多种写法, 和 `json` 很像, 但也有区别

> TODO: 详细写一下设备树的理解

可以参考
- [`elinux.org/device_tree_usage`](https://elinux.org/Device_Tree_Usage)
- [`k210 的 devicetree`](https://github.com/riscv-software-src/opensbi/blob/555055d14534e436073c818e04f4a5f0d3c141dc/platform/kendryte/k210/k210.dts)
- [`野火的文档`](https://doc.embedfire.com/linux/imx6/driver/zh/latest/linux_driver/driver_tree.html)
- [`sifive-hifive的 devicetree(for PLIC)`](https://github.com/riscv-non-isa/riscv-device-tree-doc/blob/master/examples/sifive-hifive_unleashed-microsemi.dts)

需要有什么:
```
        +---------------------------+
        |         Root Node         | / {
        |---------------------------|
        | #address-cells = <1>      |
        | #size-cells    = <1>      |
        | compatible = "seeker_nemu"|
        +---------------------------+
                   |
      +------------+-------------+-------------+------------------+
      |            |             |             |                  |
+----------+ +-----------+ +-------------+ +---------------+ +------------------+
| chosen   | | cpus      | | plic0       | | uart@a00003f8 | | memory@80000000  |
|----------| |-----------| |-------------| |---------------| |------------------|
| bootargs | | timebase  | |@0xC000000   | | reg=0xA00003F8| | reg=0x80000000   |
|          | | frequency | | interrupts  | | status=okay   | |   -0x87FFFFFF    |
+----------+ |           | | extended    | +---------------+ +------------------+
             +-----------+ +-------------+   |
               | --------------^     ^       |
               | cpu@0         |     |       |
               |---------------|     |       |
               | riscv,isa     |   +------+  |
               | interrupts    +-->| PLIC |<-+
               | controller    |   +------+
               +---------------+
```


### Linux 适配 nemu-uart 驱动!
主要参考 [`linux 内核 driver-api/serial/driver`](https://docs.kernel.org/driver-api/serial/driver.html#uart-ops)
同时可以参考 [`linux 内核的 uart-lite 的驱动`](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/drivers/tty/serial/uartlite.c?h=v5.15.178)

这一段写的台烂了, 需要大规模修改!

启用内核驱动的位置在 `tinyconfig→ Device Drivers → Character devices->tty->xxx`
#### 添加 nemu-uart 驱动
- 创建 `nemu-uart.c` 文件 
- Kconfig 添加项 
- Makefile 添加项
- Menuconfig 里面勾选
Obj-$(CONFIG_SERIAL_NEMUUART) += nemu-uart. O
#### 如何注册一个驱动?
使用 platform_driver 代表一个平台驱动程序, 用于管理和控制 platform_device。

Linux 驱动主要包含几个结构体




> TODO: 合并后面的一个章节

#### 驱动如何注册?

```

static struct uart_driver nemu_uart_driver = {
	.owner = THIS_MODULE,
	.driver_name = DRIVER_NAME,
	.dev_name = "ttyNEMU",
	.major = TTY_MAJOR,
	.minor = 2472,
	.nr = 1,
};

for (curr = chrdevs[i]; curr; prev = curr, curr = curr->next) {
	if (curr->major < major)
		continue;

	if (curr->major > major)
		break;

	if (curr->baseminor + curr->minorct <= baseminor)
		continue;

	if (curr->baseminor >= baseminor + minorct)
		break;

	goto out;
}

设备号冲突导致跑不起来

太诡异了!
tty_port_link_device
0driver(ttydriver
)->ports
uart_register_driver里面初始化
__tty_alloc_driver


看看uart_startup
```
内核似乎一直不调用 tty 输出函数，发现是没有实现一些关键函数和 config

参考:
- [`kernel_docs/low_level_serial_api->uart_ops`](https://docs.kernel.org/driver-api/serial/driver.html)
- [`kernel_docs/core-api/genericirq->request_irq()`](https://docs.kernel.org/core-api/genericirq.html)
- [`kernel_docs/driver-api/tty_buffer->tty_insert_flip_char/tty_flip_buffer_push`](https://docs.kernel.org/driver-api/tty/tty_buffer.html)
- [`kernel_docs/driver-api/console->console`](https://docs.kernel.org/driver-api/tty/console.html#console)
- [`kernel_docs/driver-api/infra->`](https://docs.kernel.org/driver-api/infrastructure.html)

https://docs.kernel.org/devicetree/kernel-api.html
https://docs.kernel.org/devicetree/usage-model.html
https://docs.kernel.org/arch/arm/interrupts.html#interrupts
https://www.kernel.org/doc/Documentation/driver-model/platform.txt

Uart-lite
- [`uartlite's dt`](https://www.kernel.org/doc/Documentation/devicetree/bindings/serial/xlnx%2Copb-uartlite.txt)
- [`uartlite's docs`](https://docs.amd.com/v/u/en-US/pg142-axi-uartlite)


### 思考: opensbi 是如何把设备树地址传递给 linux 的

首先给 linux 的起始地址打上断点, 会发现 a 1 寄存器就是!
可以扫描内存看看，魔数对不对
```
#ifdef CONFIG_BUILTIN_DTB
	la a0, __dtb_start
#else
	mv a0, s1
#endif /* CONFIG_BUILTIN_DTB */
	/* Set trap vector to spin forever to help debug */
	la a3, .Lsecondary_park
	csrw CSR_TVEC, a3
	call setup_vm
```


之后我们追踪一下这个数字往那跑(`head.s`), 发现传递给了 ` setup_vm `, 然后映射这段内存!

`0x3e400000`

一开始地址没有传对导致设备树没有加载!
```
status = early_init_dt_verify(params);
if (!status)
	return false;
```

Fdt 32_ld

在设备树读取到内存节点的时候, 会调用 `early_init_dt_add_memory_arch` 之后调用 `memblock_add` 存储地址进 `memblock.memory` 以便之后读取

 `memblock.reserved` 是啥?
##### 地址转换问题 :
完全没看懂这里在干嘛

```
	dtb_early_va = (void *)fix_fdt_va + (dtb_pa & (PMD_SIZE - 1));
```

为什么不这样写来强制对齐?

```
	dtb_early_va = (void *)(fix_fdt_va & ~(PMD_SIZE-1) ) + (dtb_pa & (PMD_SIZE - 1));
```

这也太奇怪了...

暂时把设备树放在 `0x80400000` 作为一个 workwround

之后会调用

```
void __init paging_init(void)
{
	setup_bootmem();
	setup_vm_final();

	/* Depend on that Linear Mapping is ready */
	memblock_allow_resize();
}
```
 setup_bootmem ？

来重新初始化虚拟内存系统

但是在 `setup_vm_final();` (`riscv/mm/init.c`)里面的这一行中

```
//	if (end >= __pa(PAGE_OFFSET) + memory_limit)
//		end = __pa(PAGE_OFFSET) + memory_limit;
```

`	for_each_mem_range(i, &start, &end) ` 宏

```
#define for_each_mem_range(i, p_start, p_end)                                  \
	__for_each_mem_range (i, &memblock.memory, NULL, NUMA_NO_NODE,         \
			      MEMBLOCK_HOTPLUG, p_start, p_end, NULL)

// Expands to
for (i = 0, __next_mem_range(&i, (-1), MEMBLOCK_HOTPLUG, &memblock.memory,
			     ((void *)0), &start, &end, ((void *)0));
     i != (u64)(~0ULL);
     __next_mem_range(&i, (-1), MEMBLOCK_HOTPLUG, &memblock.memory, ((void *)0),
		      &start, &end, ((void *)0)))
```
遍历!

Ebreak 调试大法

```
asm volatile (
    "mv a0, %0\n\t"    // 将 start 的值加载到 a0 寄存器
    "mv a1, %1\n\t"    // 将 end 的值加载到 a1 寄存器
    "ebreak"           // 执行 ebreak 指令
    :
    : "r"(start), "r"(end) // 输入操作数：将 start 和 end 传递给寄存器
    : "a0", "a1"       // 声明 a0 和 a1 寄存器会被修改
);
```

### Linux 内核在哪里调用了 nemu-uart 的初始化函数?

已经比较晚了, 之前应该调用更早的 earlycon 来传递 log
```
#0  nemu_uart_init () at drivers/tty/serial/nemu-uart.c:66
#1  0x80c00cbc in do_one_initcall (fn=0x80c0cfe4 <nemu_uart_init>) at init/main.c:1302
#2  0x80c01044 in do_initcall_level (command_line=0x82015610 "earlycon", level=6) at init/main.c:1375
#3  do_initcalls () at init/main.c:1391
#4  do_basic_setup () at init/main.c:1410
#5  kernel_init_freeable () at init/main.c:1615
#6  0x8092817c in kernel_init (unused=<optimized out>) at init/main.c:1506
#7  0x80801910 in payload_bin () at arch/riscv/kernel/entry.S:232

```

### 某些细节

Ecall 的时候 mtval 清零

`mstatus/sstatus` & `sie/mie` 的某些位应该是硬件上的相同 bit, 根据手册定义
> A restricted view of mstatus appears as the sstatus register in the S-level ISA.

#### 设备树被改了（TODO：为什么会修改设备树, 那两个 fixup 函数是干什么的?）!

首先发现一直卡在这个 die 函数
```
#6  0x80802330 in die (regs=0x81bffae0 <payload_bin+20970208>, str=0x81410c1c <payload_bin+12651548> "Oops - illegal instruction")
    at arch/riscv/kernel/traps.c:48

```
然后查看第一次 illegal_instruction 的位置, 发现是在 `__delay` 函数, 打上断点查看是什么时候调用了 delay 函数
```
#3  0x80c01904 in init_IRQ () at arch/riscv/kernel/irq.c:23
```


草了, 发现 opensbi 改了我的设备树! ->又被 copy-paste code 给害了

```c
hartid = riscv_of_parent_hartid(node);
if (hartid < 0) {
	pr_warn("unable to find hart id for %pOF\n", node);
	return 0;
}
```
要保证 plic 的父节点是一个 cpu 核心, 不然 plic 就加载不起来


### 思考: 设备树是如何解析调用驱动的?

看了一下 `drivers/of/fdt.c`, 里面的 `early_init_dt_scan_nodes`,

```c
void __init early_init_dt_scan_nodes(void)
{
	int rc = 0;

	/* Initialize {size,address}-cells info */
	of_scan_flat_dt(early_init_dt_scan_root, NULL);

	/* Retrieve various information from the /chosen node */
	rc = of_scan_flat_dt(early_init_dt_scan_chosen, boot_command_line);
	if (!rc)
		pr_warn("No chosen node found, continuing without\n");

	/* Setup memory, calling early_init_dt_add_memory_arch */
	of_scan_flat_dt(early_init_dt_scan_memory, NULL);

	/* Handle linux,usable-memory-range property */
	early_init_dt_check_for_usable_mem_range();
}
```
似乎在这里只初始化内存, 不初始化设备?=>是的

### 没有日志输出(menuconfig 里面没有开, 我是🤡)?

```
Kernel hacking-> printk and dmesg options

→ General setup → Configure standard kernel features (expert users) -> Enable support for printk  
```
### 向文件系统进发!我们需要一个 initramfs

之前的内容跑到这里就说明成功了
```
#2  0x8091d1f4 in panic (
    fmt=fmt@entry=0x81410748 <payload_bin+12650312> "No working init found.  Try passing init= option to kernel. See Linux Documentation/admin-guide/init.rst for guidance.") at kernel/panic.c:443

```

```
-> General setup -> Initial RAM filesystem and RAM disk (initramfs/initrd) support 
```
### Rubbish

经过排查, 发现是 `for_each_mem_range` 压根就没执行! `__next_mem_range` 第一次就反回了 0, 正在排查原因

一件奇怪的事情: 似乎 `FDT` 要放在指定的位置
在

发现虚拟地址转换后的结果为 `0x8021c000`，而正确的是 `0x8001c000`

一个

```
create_pgd_mapping(early_pg_dir, fix_fdt_va,
		   pa, MAX_FDT_SIZE, PAGE_KERNEL);
		   
0x3e200000 -> 0x80000000
```


### PLIC 的适配

[`PLIC Spec`](https://github.com/riscv/riscv-plic-spec/blob/master/riscv-plic.adoc)

PLIC(Platform-Level Interrupt Controller) 用来管理外部设备中断，协调多个外部中断源, 分配优先级, 抢占, 屏蔽, 路由, 完成通知,...
> TODO: 细化Plic 是什么,什么时候需要 PLIC

有什么寄存器? 可以对照寄存器/图片讲

![](./attachments/Pasted%20image%2020250215230008.png)


要实现 uart 输入, 那么就必须适配中断了，首先需要修改设备树,
```
uart: uart@a00003f8 {
	compatible = "seeker,nemu_uart";
	reg = <0xa00003f8 0x1>;
	interrupts = <1>;         // 使用PLIC中断源1（可随便定义，但需<=riscv,ndev-1）
	interrupt-parent = <&plic0>;  // 关联到PLIC
	status = "okay";
};
```

#### 注册中断
Probe 的时候获取中断号 (这里要判断一下是否正常, 否则等到 platform_get_irq 的时候会 fail)

```
nemu_uart_port.irq = platform_get_irq(pdev, 0);
```

然后 startup 的时候注册中断

```
int ret = request_irq(port->irq, nemu_uart_irq,
		      IRQF_TRIGGER_RISING, "nemu_uart",
		      port);int ret = request_irq(port->irq, nemu_uart_irq,
		      IRQF_TRIGGER_RISING, "nemu_uart",
		      port);
```
#### 简化实现
给 plic 加一个 trace, 发现读写的地址有:
```
0xc002080->Hart 1 M-mode enables
0xc002084->same area
0xc201000->Hart 1 M-mode priority threshold

0xc000004-> source 1 pirority（越大越好）

```
Intr pending

阅读手册, 可以知道大概的流程是
Uart中断传送到PLIC->设置pendingbit->抛出异常(M/S external interrupt)->linux 进行异常处理 (PLIC)->claim read（claim/complete reg）(反回highest prigority or zero)->进行异常处理->结束以后(write claim/complete reg)(on success->clear pending bit)

只有一个信号!

- 所以需要定期读进一个 buffer, 并检查这个 buffer 非空
- 如果检查到这个 buffer 非空, 检查是否有 disable
- 没有 disable, 产生中断 (这时要设置 pending 和 claim/complete)
- 检测到读信号不处理
- 检测到写信号清除 pending claim/complete（`0x0C201004`）

再简化:
直接 fix claim/complete ,pending 
有数据就发中断

思考, plic 也是通过一根线链接到 cpu 的吗, 和 timer intr 有什么区别？

### 异常处理的细节

其实没有完全实现正确也能跑
这里是目前 difftest 的框架没有办法 diff 到的地方.

具体参考手册有关 `medeleg` & `mideleg`
如何选择 M/S-Mode intr?
默认情况下会把所有异常/中断都交给 M-Mod 处理, 然后让 M-mod 的程序来选择是自己处理还是, 但是为了提高性能, 可以把某一些中断交给 S-Mod

`mie` & `mip`?
![20250215_19h10m54s_grim.png](./attachments/20250215_19h10m54s_grim.png)


### 交叉工具链
Busybox 和 newlib 兼容性不太好, 如果工具链用了 newlib 会找不到头文件

如果传递了 `--enable-multilib` 可能会编译出 c 拓展的指令

正确的编译选项
```
//right!
./configure --prefix=/opt/riscv1 --with-arch=rv32ima --with-abi=ilp32
make linux
```
### Initramfs 的打包

可以先写一个死循环来测试, 然后再 initscript 

Init 要有执行权限！
```
//TODO:这里去查一下相关规定
mkdir --parents /usr/src/initramfs/{bin,dev,etc,lib,lib64,mnt/root,proc,root,sbin,sys,run}

(cd initramfs-root && find . | cpio -o --format=newc | gzip > ../initramfs.cpio.gz)
```
### 编译 `busybox`
```
make CROSS_COMPILE=riscv32-unknown-linux-gnu- ARCH=riscv  CONFIG_PREFIX=/root/initramfs install
```
Difftest 问题:
TODO: 脏位检查?!?

这里 riscv 有一个细节: 他允许硬件替换, 也允许软件替换 
(hade)???
```
reg_t ad = PTE_A | ((type == STORE) * PTE_D);

if ((pte & ad) != ad) {
  if (hade) {
    // set accessed and possibly dirty bits.
    pte_store(pte_paddr, pte | ad, addr, virt, type, vm.ptesize);
  } else {
    // take exception if access or possibly dirty bit is not set.
    break;
  }
}
```
```
walk: load_slow_path_intrapage->translate->walk
```


#### tinyconfig 做修改->最小化内核
```
→ General setup → Configure standard kernel features (expert users) -> Enable support for printk 
→ General setup->Initial RAM filesystem and RAM disk (initramfs/initrd) support
→ Platform type ->Base ISA 
→ Boot options -> UEFI runtime support 
→ Platform type->Emit compressed instructions when building Linux  
→ Kernel hacking → printk and dmesg options->Show timing information on printks 
→ Kernel hacking → Compile-time checks and compiler options -> Compile the kernel with debug info 
→ Device Drivers → Character devices ->Enable TTY -> Early console using RISC-V SBI
→ Device Drivers → Character devices ->Enable TTY ->  NEMU uartlite serial port support   
→ Executable file formats->Kernel support for scripts starting with #! 
→ Device Drivers → IRQ chip support->SiFive Platform-Level Interrupt Controller
```

## 为什么不跑一个发行版呢?

看看远方的 Riscv64 吧!
- "32-bit architectures are being killed off one-by-one, not being added." (from debian mail-list)
- "**What needs to be done**: Get riscv32 running somehow (fails due to bugs in qemu user mode emulation)" (from gentoo wiki)
- fedora wiki : not even mentioned yet.













# 杂项(TODO:Delete)



## PMP

`pmp<n>cfg`: `L0A | XWR` L: locked->(addr&entry) O:reserved    A: Access Type


Permissions-error:
- Instruction access fault
- load access-fault
- Store access-fault

AccessType:
- 0-关闭
- 1-TOR (TOP of Range),TOR 模式通过两个相邻的 `pmpaddr` 寄存器定义一个连续的地址范围
	- Matches `pmaddr(i-1)<y<pmaddr(i)`, 如果大于则无效
- 2->NA4 (Naturally aligned four-byte region),定义一个 **4 字节对齐** 的极小内存区域
- 3->NAOT (Naturally aligned power-of-two region, ≥8 bytes)NAPOT 模式定义一个 **2 的幂次方大小且自然对齐** 的内存区域->看末尾有多少个 1?




```c
bool pmpcfg_csr_t::unlogged_write(const reg_t val) noexcept {
	//没有实现pmp寄存器
  if (proc->n_pmp == 0)
    return false;

  bool write_success = false;
  //rlb和mml是mseccfg中的位,这是拓展!没有必要实现?
  const bool rlb = state->mseccfg->get_rlb();
  const bool mml = state->mseccfg->get_mml();
  
  for (size_t i0 = (address - CSR_PMPCFG0) * 4, i = i0; i < i0 + proc->get_xlen() / 8; i++) {
    if (i < proc->n_pmp) {
      const bool locked = (state->pmpaddr[i]->cfg & PMP_L);
      if (rlb || !locked) {
        uint8_t cfg = (val >> (8 * (i - i0))) & (PMP_R | PMP_W | PMP_X | PMP_A | PMP_L);
        // Drop R=0 W=1 when MML = 0
        // Remove the restriction when MML = 1
        if (!mml) {
          cfg &= ~PMP_W | ((cfg & PMP_R) ? PMP_W : 0);
        }
        // Disallow A=NA4 when granularity > 4
        if (proc->lg_pmp_granularity != PMP_SHIFT && (cfg & PMP_A) == PMP_NA4)
          cfg |= PMP_NAPOT;
        /*
         * Adding a rule with executable privileges that either is M-mode-only or a locked Shared-Region
         * is not possible and such pmpcfg writes are ignored, leaving pmpcfg unchanged.
         * This restriction can be temporarily lifted e.g. during the boot process, by setting mseccfg.RLB.
         */
        const bool cfgx = cfg & PMP_X;
        const bool cfgw = cfg & PMP_W;
        const bool cfgr = cfg & PMP_R;
        if (rlb || !(mml && ((cfg & PMP_L)      // M-mode-only or a locked Shared-Region
                && !(cfgx && cfgw && cfgr)      // RWX = 111 is allowed
                && (cfgx || (cfgw && !cfgr))    // X=1 or RW=01 is not allowed
        ))) {
          state->pmpaddr[i]->cfg = cfg;
        }
      }
      write_success = true;
    }
  }
  proc->get_mmu()->flush_tlb();
  return write_success;
}


```


这是Smepmp拓展! 感觉没必要实现
`pmpcfg` 和 `pmpaddr` 共同维护一条 PMP
### `mseccfg.RLB`(Rule Locking Bypass)

### `mseccfg.MML` (Machine Mode Lockdown)

