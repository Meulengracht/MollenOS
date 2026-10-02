// RPI4 entry point
.section .text.entry, "ax"
.global _kentry
_kentry:
    // Set x1 to Pi4 version
    mov x1, #4
    // Jump to the shared initialization routine for rpi
    b _rpi_initialize
