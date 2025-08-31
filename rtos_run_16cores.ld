__stack_size = 0x50000;
__heap_size  = 0x100000;

MEMORY
{
    code_ram         (rx!rw) : ORIGIN = 0x80001000, LENGTH = 0x40000
    rodata_ram       (rw!x)  : ORIGIN = 0x80041000, LENGTH = 0x30000
    global_data_ram  (rw!x)  : ORIGIN = 0x80071000, LENGTH = 0x80000
    heap_data_ram     (rw!x)  : ORIGIN = 0x80151000, LENGTH = __heap_size
    private_data_ram0 (rw!x)  : ORIGIN = 0x80151000 + __heap_size,           LENGTH = __stack_size
    private_data_ram1 (rw!x)  : ORIGIN = 0x80151000 + __heap_size + 0x50000, LENGTH = __stack_size
    private_data_ram2 (rw!x)  : ORIGIN = 0x80151000 + __heap_size + 0xA0000, LENGTH = __stack_size
    private_data_ram3 (rw!x)  : ORIGIN = 0x80151000 + __heap_size + 0xF0000, LENGTH = __stack_size
    private_data_ram4 (rw!x)  : ORIGIN = 0x80151000 + __heap_size + 0x140000, LENGTH = __stack_size
    private_data_ram5 (rw!x)  : ORIGIN = 0x80151000 + __heap_size + 0x190000, LENGTH = __stack_size
    private_data_ram6 (rw!x)  : ORIGIN = 0x80151000 + __heap_size + 0x1E0000, LENGTH = __stack_size
    private_data_ram7 (rw!x)  : ORIGIN = 0x80151000 + __heap_size + 0x230000, LENGTH = __stack_size
    private_data_ram8 (rw!x)  : ORIGIN = 0x80151000 + __heap_size + 0x280000, LENGTH = __stack_size
    private_data_ram9 (rw!x)  : ORIGIN = 0x80151000 + __heap_size + 0x2D0000, LENGTH = __stack_size
    private_data_ram10 (rw!x) : ORIGIN = 0x80151000 + __heap_size + 0x320000, LENGTH = __stack_size
    private_data_ram11 (rw!x) : ORIGIN = 0x80151000 + __heap_size + 0x370000, LENGTH = __stack_size
    private_data_ram12 (rw!x) : ORIGIN = 0x80151000 + __heap_size + 0x3C0000, LENGTH = __stack_size
    private_data_ram13 (rw!x) : ORIGIN = 0x80151000 + __heap_size + 0x410000, LENGTH = __stack_size
    private_data_ram14 (rw!x) : ORIGIN = 0x80151000 + __heap_size + 0x460000, LENGTH = __stack_size
    private_data_ram15 (rw!x) : ORIGIN = 0x80151000 + __heap_size + 0x4B0000, LENGTH = __stack_size
}

ENTRY(crt0)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    .text :
    {
        *(.text*)
    } > code_ram :text

    .rodata :
    {
        *(.rodata*)
    } > rodata_ram :text

    .data :
    {
        *(.data)
    } > global_data_ram :data

    .bss :
    {
        __bss_start__ = .;  
        *(.bss)
        *(.sbss)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;    
    } > global_data_ram :data

    .heap : ALIGN(0x10)
    {
        __heap_start = .;
        . += __heap_size;
    } > heap_data_ram :data

    .stack0 : ALIGN(16)
    {
        . += __stack_size;
        . = ALIGN(16);
        __stack_top_0 = .;
    } > private_data_ram0 :data

    .stack1 : ALIGN(0x10)
    {
        . += __stack_size;
        . = ALIGN(16);
        __stack_top_1 = .;
    } > private_data_ram1 :data

    .stack2 : ALIGN(0x10)
    {
        . += __stack_size;
        . = ALIGN(16);
        __stack_top_2 = .;
    } > private_data_ram2 :data

    .stack3 : ALIGN(0x10)
    {
        . += __stack_size;
        . = ALIGN(16);
        __stack_top_3 = .;
    } > private_data_ram3 :data

    .stack4 : ALIGN(0x10)
    {
        . += __stack_size;
        . = ALIGN(16);
        __stack_top_4 = .;
    } > private_data_ram4 :data

    .stack5 : ALIGN(0x10)
    {
        . += __stack_size;
        . = ALIGN(16);
        __stack_top_5 = .;
    } > private_data_ram5 :data

    .stack6 : ALIGN(0x10)
    {
        . += __stack_size;
        . = ALIGN(16);
        __stack_top_6 = .;
    } > private_data_ram6 :data

    .stack7 : ALIGN(0x10)
    {
        . += __stack_size;
        . = ALIGN(16);
        __stack_top_7 = .;
    } > private_data_ram7 :data

    .stack8 : ALIGN(0x10)
    {
        . += __stack_size;
        . = ALIGN(16);
        __stack_top_8 = .;
    } > private_data_ram8 :data

    .stack9 : ALIGN(0x10)
    {
        . += __stack_size;
        . = ALIGN(16);
        __stack_top_9 = .;
    } > private_data_ram9 :data

    .stack10 : ALIGN(0x10)
    {
        . += __stack_size;
        . = ALIGN(16);
        __stack_top_10 = .;
    } > private_data_ram10 :data

    .stack11 : ALIGN(0x10)
    {
        . += __stack_size;
        . = ALIGN(16);
        __stack_top_11 = .;
    } > private_data_ram11 :data

    .stack12 : ALIGN(0x10)
    {
        . += __stack_size;
        . = ALIGN(16);
        __stack_top_12 = .;
    } > private_data_ram12 :data

    .stack13 : ALIGN(0x10)
    {
        . += __stack_size;
        . = ALIGN(16);
        __stack_top_13 = .;
    } > private_data_ram13 :data

    .stack14 : ALIGN(0x10)
    {
        . += __stack_size;
        . = ALIGN(16);
        __stack_top_14 = .;
    } > private_data_ram14 :data

    .stack15 : ALIGN(0x10)
    {
        . += __stack_size;
        . = ALIGN(16);
        __stack_top_15 = .;
    } > private_data_ram15 :data
}
