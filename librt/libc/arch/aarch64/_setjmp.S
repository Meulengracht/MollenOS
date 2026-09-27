/**
 * Vali AAPCS64 context save/restore.
 * x18 remains platform-owned.
 */
.text
.p2align 2
.globl _setjmp
.globl _setjmp3
_setjmp:
_setjmp3:
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    stp x29, x30, [x0, #80]
    mov x9, sp
    str x9, [x0, #96]
#ifndef LIBC_KERNEL
    mrs x9, fpcr
    mrs x10, fpsr
    stp x9, x10, [x0, #104]
    stp d8, d9, [x0, #128]
    stp d10, d11, [x0, #144]
    stp d12, d13, [x0, #160]
    stp d14, d15, [x0, #176]
#endif
    mov w0, #0
    ret

.p2align 2
.globl longjmp
longjmp:
    mov x9, x0
    cmp w1, #0
    csinc w0, w1, wzr, ne
    ldp x19, x20, [x9, #0]
    ldp x21, x22, [x9, #16]
    ldp x23, x24, [x9, #32]
    ldp x25, x26, [x9, #48]
    ldp x27, x28, [x9, #64]
    ldp x29, x30, [x9, #80]
#ifndef LIBC_KERNEL
    ldp x10, x11, [x9, #104]
    msr fpcr, x10
    msr fpsr, x11
    ldp d8, d9, [x9, #128]
    ldp d10, d11, [x9, #144]
    ldp d12, d13, [x9, #160]
    ldp d14, d15, [x9, #176]
#endif
    ldr x10, [x9, #96]
    mov sp, x10
    ret
