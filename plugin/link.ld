/* Layout for a Switch NSO: text / rodata / data each start on a page boundary. */

/* Ryujinx loads rtld, main, subsdk0, subsdk1, subsdk9 (us), sdk back to back.
   main is 0x7D6B000 bytes, subsdk0 0xD81000, subsdk1 0x6BB000 -> main starts 0x91A7000 before us. */
MAIN_DELTA = 0x91A7000;

PHDRS
{
    text PT_LOAD FLAGS(5);
    ro   PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
    dyn  PT_DYNAMIC;
}

SECTIONS
{
    . = 0;
    __module_start = .;
    PROVIDE_HIDDEN(__main_start = __module_start - MAIN_DELTA);

    .text : {
        KEEP(*(.text.modstart))
        *(.text .text.*)
        *(.plt .plt.*)
    } :text

    . = ALIGN(0x1000);
    __ro_start = .;
    .rodata   : { *(.rodata .rodata.*) } :ro
    .dynsym   : { *(.dynsym) } :ro
    .dynstr   : { *(.dynstr) } :ro
    .hash     : { *(.hash) } :ro
    .rela.dyn : { *(.rela.dyn) *(.rela.*) } :ro
    .rela.plt : { *(.rela.plt) } :ro
    .gnu.version   : { *(.gnu.version) } :ro
    .gnu.version_d : { *(.gnu.version_d) } :ro
    .gnu.version_r : { *(.gnu.version_r) } :ro
    .eh_frame_hdr  : { *(.eh_frame_hdr) } :ro
    .eh_frame      : { KEEP(*(.eh_frame)) } :ro

    . = ALIGN(0x1000);
    __data_start = .;
    .data       : { *(.data .data.*) } :data
    .init_array : { KEEP(*(.init_array .init_array.*)) } :data
    .fini_array : { KEEP(*(.fini_array .fini_array.*)) } :data
    .dynamic    : { *(.dynamic) } :data :dyn
    .got        : { *(.got) } :data
    .got.plt    : { *(.got.plt) } :data

    . = ALIGN(8);
    __bss_start = .;
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        __nx_module_runtime = .;
        . += 0x100;
    } :data
    __bss_end = .;
    __module_end = .;

    /DISCARD/ : { *(.comment) *(.note.*) *(.interp) }
}
