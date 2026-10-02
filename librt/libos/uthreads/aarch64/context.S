/* Enter a fresh userspace job on its own AAPCS64 stack. Never returns. */
.text
.p2align 2
.globl __usched_task_start
__usched_task_start:
    and x9, x0, #-16
    mov sp, x9
    mov x0, x1
    mov x29, xzr
    mov x30, xzr
    br x2
