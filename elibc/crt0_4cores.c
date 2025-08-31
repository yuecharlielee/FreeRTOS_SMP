extern int main(void);

extern char __bss_start__[];
extern char __bss_end__[];

extern unsigned int __stack_top_0;
extern unsigned int __stack_top_1;
extern unsigned int __stack_top_2;
extern unsigned int __stack_top_3;

unsigned int sp_store;
volatile unsigned int boot_release = 0;

void simple_delay(void) {
    for (int i = 0; i < 1000; ++i) {
        asm volatile("nop");
    }
}

void crt0(void)
{
    asm volatile ("la t0, sp_store");
    asm volatile ("sw sp, 0(t0)");

    int hart_id;
    asm volatile ("csrr %0, mhartid" : "=r"(hart_id));

    if (hart_id == 0) {
        asm volatile("la sp, __stack_top_0");
    } else if (hart_id == 1) {
        asm volatile("la sp, __stack_top_1");
    } else if (hart_id == 2) {
        asm volatile("la sp, __stack_top_2");
    } else if (hart_id == 3) {
        asm volatile("la sp, __stack_top_3");
    }
    main();

    asm volatile ("la t0, sp_store");
    asm volatile ("lw sp, 0(t0)");
}