/* STM32G070: 128 KB flash, 36 KB RAM. The vendor bootloader owns 0x08000000-0x08003FFF
 * (including the config page at 0x08003C00); the application starts at 0x08004000. */
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08004000, LENGTH = 112K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 36K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_min_stack = 2K;

SECTIONS
{
    .isr_vector :
    {
        KEEP(*(.isr_vector))
    } > FLASH

    .text :
    {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    .ARM.exidx :
    {
        *(.ARM.exidx*)
    } > FLASH

    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    ASSERT(ORIGIN(FLASH) == 0x08004000, "application must start behind the bootloader")
    ASSERT(_ebss + _min_stack <= _estack, "RAM overflow: not enough room for the stack")
}
