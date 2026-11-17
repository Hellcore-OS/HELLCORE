ENTRY(kernel_entry)

PHDRS
{
    text   PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data   PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x101000;

    .text : ALIGN(16)
    {
        *(.text .text.*)
    } :text

    . = ALIGN(0x1000);

    .rodata : ALIGN(16)
    {
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(0x1000);

    .data : ALIGN(16)
    {
        *(.data .data.*)
        *(.got .got.*)
    } :data

    .bss (NOLOAD) : ALIGN(16)
    {
        *(COMMON)
        *(.bss .bss.*)
    } :data

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
