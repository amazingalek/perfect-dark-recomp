/* The patches' own code and data go above the game's 8 MB of RDRAM, in the extra RAM the runtime gives them. */
RAMBASE = 0x80801000;
EXTRA_RAM_SIZE = 0x01000000;

MEMORY {
    extram : ORIGIN = RAMBASE, LENGTH = 64M
    rom : ORIGIN = 0, LENGTH = 64M
}

SECTIONS {
    .ctors         : { *(.ctors*) *(.init_array*)                } >extram AT >rom
    .dtors         : { *(.dtors*)                                } >extram AT >rom
    .text          : { *(.text*)                                 } >extram AT >rom
    .recomp_patch  : { *(.recomp_patch*) *(.recomp_force_patch*) } >extram AT >rom
    .recomp_export : { *(.recomp_export*)                        } >extram AT >rom
    .rodata        : { *(.rodata*)                               } >extram AT >rom
    .data          : { *(.data*)                                 } >extram AT >rom
    .bss  (NOLOAD) : { *(.bss*)  *(COMMON)                       } >extram
    ASSERT(. < RAMBASE + EXTRA_RAM_SIZE, "Maxed out recomp extra ram")

    .reloc 0 : { *(.reloc*) }
    .symtab 0 : { *(.symtab) }
    .strtab 0 : { *(.strtab) }
    .shstrtab 0 : { *(.shstrtab) }

    /DISCARD/ : { *(*); }
}
