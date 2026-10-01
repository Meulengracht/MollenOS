/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

.text
.p2align 2
.globl GetContext
GetContext:
    stp x0, x1, [x0, #0]
    stp x2, x3, [x0, #16]
    stp x4, x5, [x0, #32]
    stp x6, x7, [x0, #48]
    stp x8, x9, [x0, #64]
    stp x10, x11, [x0, #80]
    stp x12, x13, [x0, #96]
    stp x14, x15, [x0, #112]
    stp x16, x17, [x0, #128]
    stp x18, x19, [x0, #144]
    stp x20, x21, [x0, #160]
    stp x22, x23, [x0, #176]
    stp x24, x25, [x0, #192]
    stp x26, x27, [x0, #208]
    stp x28, x29, [x0, #224]
    str x30, [x0, #240]
    mov x9, sp
    str x9, [x0, #248]
    str x30, [x0, #256]
    mrs x10, nzcv
    str x10, [x0, #264]
    str x9, [x0, #272]
    str xzr, [x0, #280]
    stp xzr, xzr, [x0, #288]
    str xzr, [x0]
    mov w0, wzr
    ret
