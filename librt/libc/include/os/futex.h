
// +---+---+-----------+-----------+
// |op |cmp|   oparg   |  cmparg   |
// +---+---+-----------+-----------+
//   4   4       12          12    <== # of bits

#ifndef __OS_FUTEX_H__
#define __OS_FUTEX_H__

#define FUTEX_OP_SET        0  /* uaddr2 = oparg; */
#define FUTEX_OP_ADD        1  /* uaddr2 += oparg; */
#define FUTEX_OP_OR         2  /* uaddr2 |= oparg; */
#define FUTEX_OP_ANDN       3  /* uaddr2 &= ~oparg; */
#define FUTEX_OP_XOR        4  /* uaddr2 ^= oparg; */

#define FUTEX_OP_ARG_SHIFT  8  /* Use (1 << oparg) as operand */

#define FUTEX_OP_CMP_EQ     0  /* if (oldval == cmparg) wake */
#define FUTEX_OP_CMP_NE     1  /* if (oldval != cmparg) wake */
#define FUTEX_OP_CMP_LT     2  /* if (oldval < cmparg) wake */
#define FUTEX_OP_CMP_LE     3  /* if (oldval <= cmparg) wake */
#define FUTEX_OP_CMP_GT     4  /* if (oldval > cmparg) wake */
#define FUTEX_OP_CMP_GE     5  /* if (oldval >= cmparg) wake */

#define FUTEX_OP(op, oparg, cmp, cmparg) \
                   (((op & 0xf) << 28) | \
                   ((cmp & 0xf) << 24) | \
                   ((oparg & 0xfff) << 12) | \
                   (cmparg & 0xfff))

#define FUTEX_WAIT_PRIVATE      0x1
#define FUTEX_WAIT_OP           0x2
#define FUTEX_WAKE_PRIVATE      0x4
#define FUTEX_WAKE_OP           0x8

//int futex(int *uaddr, int op, int val, const struct timespec *timeout,
//          int *uaddr2, int val3);

#endif //!__OS_FUTEX_H__
