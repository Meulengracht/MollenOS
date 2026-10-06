ENTRY(_kentry)
SECTIONS
{
    /* Physical entry address must match firmware config.txt kernel_address. */
    . = 0x80000;
    __rpi_loader_start = .;
    .text : { KEEP(*(.text.entry)) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        __bss_end = .;
    }
    /* This script is not preprocessed: 4096 mirrors the native page size. */
    .boot.stack ALIGN(4096) : { KEEP(*(.boot.stack)) }
    /* Put the 64-byte payload trailer at the end of an aligned page. */
    .boot.header ALIGN(4096) + 4096 - 64 : {
        __rpi_payload_header = .;
        /* Little-endian VALIRPI signature, format version 1 and trailer size. */
        LONG(0x494c4156) LONG(0x00495052)
        LONG(1) LONG(64)
        QUAD(__rpi_loader_end - __rpi_loader_start)
        QUAD(__rpi_loader_end - __rpi_loader_start)
        QUAD(0) QUAD(0)
        QUAD(0) QUAD(0)
        __rpi_loader_end = .;
    }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
ASSERT(_kentry == __rpi_loader_start, "firmware must enter the first byte")
ASSERT((__rpi_boot_stack_top & 15) == 0, "AAPCS64 stack alignment")
ASSERT(__bss_end <= __rpi_boot_stack_bottom, "BSS must exclude the active stack")
ASSERT((__rpi_loader_end & 4095) == 0, "payload alignment")
