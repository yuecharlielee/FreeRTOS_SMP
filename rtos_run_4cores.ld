

__stack_size = 0x50000;
__heap_size  = 0x100000;

MEMORY
{
    code_ram         (rx!rw) : ORIGIN = 0x80001000, LENGTH = 0x40000
    rodata_ram       (rw!x)  : ORIGIN = 0x80041000, LENGTH = 0x30000
    global_data_ram  (rw!x)  : ORIGIN = 0x80071000, LENGTH = 0x80000
    heap_data_ram     (rw!x)  : ORIGIN = 0x80151000, LENGTH = __heap_size
    private_data_ram0 (rw!x)  : ORIGIN = 0x80151000 + __heap_size,          LENGTH = __stack_size
    private_data_ram1 (rw!x)  : ORIGIN = 0x80151000 + __heap_size + 0x50000, LENGTH = __stack_size
    private_data_ram2 (rw!x)  : ORIGIN = 0x80151000 + __heap_size + 0xA0000, LENGTH = __stack_size
    private_data_ram3 (rw!x)  : ORIGIN = 0x80151000 + __heap_size + 0xF0000, LENGTH = __stack_size
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
}

