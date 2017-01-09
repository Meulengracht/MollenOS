/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* along with this program.If not, see <http://www.gnu.org/licenses/>.
*
*
* MollenOS x86-32 Exception Handlers & Init
*/

#ifndef _x86_EXCEPTIONS_H_
#define _x86_EXCEPTIONS_H_

/* Exception Includes */
#include <crtdefs.h>
#include <stdint.h>

/* Exception Definitions */


/* Exception Prototypes */
__CRT_EXTERN void ExceptionsInit(void);

/* Exception Assembly Externs */
__CRT_EXTERN void irq_handler0(void);
__CRT_EXTERN void irq_handler1(void);
__CRT_EXTERN void irq_handler2(void);
__CRT_EXTERN void irq_handler3(void);
__CRT_EXTERN void irq_handler4(void);
__CRT_EXTERN void irq_handler5(void);
__CRT_EXTERN void irq_handler6(void);
__CRT_EXTERN void irq_handler7(void);
__CRT_EXTERN void irq_handler8(void);
__CRT_EXTERN void irq_handler9(void);
__CRT_EXTERN void irq_handler10(void);
__CRT_EXTERN void irq_handler11(void);
__CRT_EXTERN void irq_handler12(void);
__CRT_EXTERN void irq_handler13(void);
__CRT_EXTERN void irq_handler14(void);
__CRT_EXTERN void irq_handler15(void);
__CRT_EXTERN void irq_handler16(void);
__CRT_EXTERN void irq_handler17(void);
__CRT_EXTERN void irq_handler18(void);
__CRT_EXTERN void irq_handler19(void);
__CRT_EXTERN void irq_handler20(void);
__CRT_EXTERN void irq_handler21(void);
__CRT_EXTERN void irq_handler22(void);
__CRT_EXTERN void irq_handler23(void);
__CRT_EXTERN void irq_handler24(void);
__CRT_EXTERN void irq_handler25(void);
__CRT_EXTERN void irq_handler26(void);
__CRT_EXTERN void irq_handler27(void);
__CRT_EXTERN void irq_handler28(void);
__CRT_EXTERN void irq_handler29(void);
__CRT_EXTERN void irq_handler30(void);
__CRT_EXTERN void irq_handler31(void);

#endif // !_x86_GDT_H_
