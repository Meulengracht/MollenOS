.text
.p2align 2
.globl _fpreset
_fpreset:
#ifndef LIBC_KERNEL
    msr fpcr, xzr
    msr fpsr, xzr
#endif
    ret
