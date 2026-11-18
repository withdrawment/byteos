OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x10000;

    .text : ALIGN(16)
    {
        *(.text.entry)
        *(.text*)
    }

    .rodata : ALIGN(16)
    {
        *(.rodata*)
    }

    .data : ALIGN(16)
    {
        *(.data*)
    }

    .bss : ALIGN(16)
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.llvm_addrsig)
    }
}
