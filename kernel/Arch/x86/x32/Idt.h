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
* MollenOS x86-32 Interrupt Descriptor Table
*/

#ifndef _x86_IDT_H_
#define _x86_IDT_H_

/* IDT Includes */
#include <crtdefs.h>
#include <stdint.h>

/* IDT Definitions */
#define X86_IDT_DESCRIPTORS			256

/* Types */
#define X86_IDT_INTERRUPT_GATE16	0x6
#define X86_IDT_TRAP_GATE16			0x7
#define X86_IDT_TASK_GATE32			0x5		/* Task gates are for switching hardware tasks, not used */
#define X86_IDT_INTERRUPT_GATE32	0xE		/* Interrupt gates automatically disable interrupts */
#define X86_IDT_TRAP_GATE32			0xF		/* Trap gates do not */

/* Priveligies */
#define X86_IDT_RING0				0x00
#define X86_IDT_RING1				0x20
#define X86_IDT_RING2				0x40
#define X86_IDT_RING3				0x60	/* Always set this if interrupt can occur from userland */

/* Flags */
#define X86_IDT_STORAGE_SEGMENT		0x10	/* Should not be set for interrupts gates */
#define X86_IDT_PRESENT				0x80	/* Always set this! */

/* IDT Structures */
#pragma pack(push, 1)
typedef struct _IdtObject
{
	/* Size */
	uint16_t Limit;

	/* Pointer to table */
	uint32_t Base;
} Idt_t;
#pragma pack(pop)

/* IDT Entry */
#pragma pack(push, 1)
typedef struct _IdtEntry
{
	/* Base 0:15 */
	uint16_t BaseLow;

	/* Selector */
	uint16_t Selector;

	/* Zero */
	uint8_t Zero;

	/* Descriptor Type 0:3 
	 * Storage Segment 4 
	 * DPL 5:6
	 * Present 7 */
	uint8_t Info;

	/* Base 16:31 */
	uint16_t BaseHigh;

} IdtEntry_t;
#pragma pack(pop)

/* IDT Prototypes */
_CRT_EXTERN void IdtInit(void);
_CRT_EXTERN void IdtInstallDescriptor(uint32_t IntNum, uint32_t Base, 
										uint16_t Selector, uint8_t Flags);

/* Should be called by AP cores */
_CRT_EXTERN void IdtInstall(void);


/* IRQS */
_CRT_EXTERN void irq_handler32(void); 
_CRT_EXTERN void irq_handler33(void);
_CRT_EXTERN void irq_handler34(void);
_CRT_EXTERN void irq_handler35(void);
_CRT_EXTERN void irq_handler36(void);
_CRT_EXTERN void irq_handler37(void);
_CRT_EXTERN void irq_handler38(void);
_CRT_EXTERN void irq_handler39(void);
_CRT_EXTERN void irq_handler40(void);
_CRT_EXTERN void irq_handler41(void);
_CRT_EXTERN void irq_handler42(void);
_CRT_EXTERN void irq_handler43(void);
_CRT_EXTERN void irq_handler44(void);
_CRT_EXTERN void irq_handler45(void);
_CRT_EXTERN void irq_handler46(void);
_CRT_EXTERN void irq_handler47(void);
_CRT_EXTERN void irq_handler48(void);
_CRT_EXTERN void irq_handler49(void);
_CRT_EXTERN void irq_handler50(void);
_CRT_EXTERN void irq_handler51(void);
_CRT_EXTERN void irq_handler52(void);
_CRT_EXTERN void irq_handler53(void);
_CRT_EXTERN void irq_handler54(void);
_CRT_EXTERN void irq_handler55(void);
_CRT_EXTERN void irq_handler56(void);
_CRT_EXTERN void irq_handler57(void);
_CRT_EXTERN void irq_handler58(void);
_CRT_EXTERN void irq_handler59(void);
_CRT_EXTERN void irq_handler60(void);
_CRT_EXTERN void irq_handler61(void);
_CRT_EXTERN void irq_handler62(void);
_CRT_EXTERN void irq_handler63(void);
_CRT_EXTERN void irq_handler64(void);
_CRT_EXTERN void irq_handler65(void);
_CRT_EXTERN void irq_handler66(void);
_CRT_EXTERN void irq_handler67(void);
_CRT_EXTERN void irq_handler68(void);
_CRT_EXTERN void irq_handler69(void);
_CRT_EXTERN void irq_handler70(void);
_CRT_EXTERN void irq_handler71(void);
_CRT_EXTERN void irq_handler72(void);
_CRT_EXTERN void irq_handler73(void);
_CRT_EXTERN void irq_handler74(void);
_CRT_EXTERN void irq_handler75(void);
_CRT_EXTERN void irq_handler76(void);
_CRT_EXTERN void irq_handler77(void);
_CRT_EXTERN void irq_handler78(void);
_CRT_EXTERN void irq_handler79(void);
_CRT_EXTERN void irq_handler80(void);
_CRT_EXTERN void irq_handler81(void);
_CRT_EXTERN void irq_handler82(void);
_CRT_EXTERN void irq_handler83(void);
_CRT_EXTERN void irq_handler84(void);
_CRT_EXTERN void irq_handler85(void);
_CRT_EXTERN void irq_handler86(void);
_CRT_EXTERN void irq_handler87(void);
_CRT_EXTERN void irq_handler88(void);
_CRT_EXTERN void irq_handler89(void);
_CRT_EXTERN void irq_handler90(void);
_CRT_EXTERN void irq_handler91(void);
_CRT_EXTERN void irq_handler92(void);
_CRT_EXTERN void irq_handler93(void);
_CRT_EXTERN void irq_handler94(void);
_CRT_EXTERN void irq_handler95(void);
_CRT_EXTERN void irq_handler96(void);
_CRT_EXTERN void irq_handler97(void);
_CRT_EXTERN void irq_handler98(void);
_CRT_EXTERN void irq_handler99(void);
_CRT_EXTERN void irq_handler100(void);
_CRT_EXTERN void irq_handler101(void);
_CRT_EXTERN void irq_handler102(void);
_CRT_EXTERN void irq_handler103(void);
_CRT_EXTERN void irq_handler104(void);
_CRT_EXTERN void irq_handler105(void);
_CRT_EXTERN void irq_handler106(void);
_CRT_EXTERN void irq_handler107(void);
_CRT_EXTERN void irq_handler108(void);
_CRT_EXTERN void irq_handler109(void);
_CRT_EXTERN void irq_handler110(void);
_CRT_EXTERN void irq_handler111(void);
_CRT_EXTERN void irq_handler112(void);
_CRT_EXTERN void irq_handler113(void);
_CRT_EXTERN void irq_handler114(void);
_CRT_EXTERN void irq_handler115(void);
_CRT_EXTERN void irq_handler116(void);
_CRT_EXTERN void irq_handler117(void);
_CRT_EXTERN void irq_handler118(void);
_CRT_EXTERN void irq_handler119(void);
_CRT_EXTERN void irq_handler120(void);
_CRT_EXTERN void irq_handler121(void);
_CRT_EXTERN void irq_handler122(void);
_CRT_EXTERN void irq_handler123(void);
_CRT_EXTERN void irq_handler124(void);
_CRT_EXTERN void irq_handler125(void);
_CRT_EXTERN void irq_handler126(void);
_CRT_EXTERN void irq_handler127(void);
_CRT_EXTERN void irq_handler128(void);
_CRT_EXTERN void irq_handler129(void);
_CRT_EXTERN void irq_handler130(void);
_CRT_EXTERN void irq_handler131(void);
_CRT_EXTERN void irq_handler132(void);
_CRT_EXTERN void irq_handler133(void);
_CRT_EXTERN void irq_handler134(void);
_CRT_EXTERN void irq_handler135(void);
_CRT_EXTERN void irq_handler136(void);
_CRT_EXTERN void irq_handler137(void);
_CRT_EXTERN void irq_handler138(void);
_CRT_EXTERN void irq_handler139(void);
_CRT_EXTERN void irq_handler140(void);
_CRT_EXTERN void irq_handler141(void);
_CRT_EXTERN void irq_handler142(void);
_CRT_EXTERN void irq_handler143(void);
_CRT_EXTERN void irq_handler144(void);
_CRT_EXTERN void irq_handler145(void);
_CRT_EXTERN void irq_handler146(void);
_CRT_EXTERN void irq_handler147(void);
_CRT_EXTERN void irq_handler148(void);
_CRT_EXTERN void irq_handler149(void);
_CRT_EXTERN void irq_handler150(void);
_CRT_EXTERN void irq_handler151(void);
_CRT_EXTERN void irq_handler152(void);
_CRT_EXTERN void irq_handler153(void);
_CRT_EXTERN void irq_handler154(void);
_CRT_EXTERN void irq_handler155(void);
_CRT_EXTERN void irq_handler156(void);
_CRT_EXTERN void irq_handler157(void);
_CRT_EXTERN void irq_handler158(void);
_CRT_EXTERN void irq_handler159(void);
_CRT_EXTERN void irq_handler160(void);
_CRT_EXTERN void irq_handler161(void);
_CRT_EXTERN void irq_handler162(void);
_CRT_EXTERN void irq_handler163(void);
_CRT_EXTERN void irq_handler164(void);
_CRT_EXTERN void irq_handler165(void);
_CRT_EXTERN void irq_handler166(void);
_CRT_EXTERN void irq_handler167(void);
_CRT_EXTERN void irq_handler168(void);
_CRT_EXTERN void irq_handler169(void);
_CRT_EXTERN void irq_handler170(void);
_CRT_EXTERN void irq_handler171(void);
_CRT_EXTERN void irq_handler172(void);
_CRT_EXTERN void irq_handler173(void);
_CRT_EXTERN void irq_handler174(void);
_CRT_EXTERN void irq_handler175(void);
_CRT_EXTERN void irq_handler176(void);
_CRT_EXTERN void irq_handler177(void);
_CRT_EXTERN void irq_handler178(void);
_CRT_EXTERN void irq_handler179(void);
_CRT_EXTERN void irq_handler180(void);
_CRT_EXTERN void irq_handler181(void);
_CRT_EXTERN void irq_handler182(void);
_CRT_EXTERN void irq_handler183(void);
_CRT_EXTERN void irq_handler184(void);
_CRT_EXTERN void irq_handler185(void);
_CRT_EXTERN void irq_handler186(void);
_CRT_EXTERN void irq_handler187(void);
_CRT_EXTERN void irq_handler188(void);
_CRT_EXTERN void irq_handler189(void);
_CRT_EXTERN void irq_handler190(void);
_CRT_EXTERN void irq_handler191(void);
_CRT_EXTERN void irq_handler192(void);
_CRT_EXTERN void irq_handler193(void);
_CRT_EXTERN void irq_handler194(void);
_CRT_EXTERN void irq_handler195(void);
_CRT_EXTERN void irq_handler196(void);
_CRT_EXTERN void irq_handler197(void);
_CRT_EXTERN void irq_handler198(void);
_CRT_EXTERN void irq_handler199(void);
_CRT_EXTERN void irq_handler200(void);
_CRT_EXTERN void irq_handler201(void);
_CRT_EXTERN void irq_handler202(void);
_CRT_EXTERN void irq_handler203(void);
_CRT_EXTERN void irq_handler204(void);
_CRT_EXTERN void irq_handler205(void);
_CRT_EXTERN void irq_handler206(void);
_CRT_EXTERN void irq_handler207(void);
_CRT_EXTERN void irq_handler208(void);
_CRT_EXTERN void irq_handler209(void);
_CRT_EXTERN void irq_handler210(void);
_CRT_EXTERN void irq_handler211(void);
_CRT_EXTERN void irq_handler212(void);
_CRT_EXTERN void irq_handler213(void);
_CRT_EXTERN void irq_handler214(void);
_CRT_EXTERN void irq_handler215(void);
_CRT_EXTERN void irq_handler216(void);
_CRT_EXTERN void irq_handler217(void);
_CRT_EXTERN void irq_handler218(void);
_CRT_EXTERN void irq_handler219(void);
_CRT_EXTERN void irq_handler220(void);
_CRT_EXTERN void irq_handler221(void);
_CRT_EXTERN void irq_handler222(void);
_CRT_EXTERN void irq_handler223(void);
_CRT_EXTERN void irq_handler224(void);
_CRT_EXTERN void irq_handler225(void);
_CRT_EXTERN void irq_handler226(void);
_CRT_EXTERN void irq_handler227(void);
_CRT_EXTERN void irq_handler228(void);
_CRT_EXTERN void irq_handler229(void);
_CRT_EXTERN void irq_handler230(void);
_CRT_EXTERN void irq_handler231(void);
_CRT_EXTERN void irq_handler232(void);
_CRT_EXTERN void irq_handler233(void);
_CRT_EXTERN void irq_handler234(void);
_CRT_EXTERN void irq_handler235(void);
_CRT_EXTERN void irq_handler236(void);
_CRT_EXTERN void irq_handler237(void);
_CRT_EXTERN void irq_handler238(void);
_CRT_EXTERN void irq_handler239(void);
_CRT_EXTERN void irq_handler240(void);
_CRT_EXTERN void irq_handler241(void);
_CRT_EXTERN void irq_handler242(void);
_CRT_EXTERN void irq_handler243(void);
_CRT_EXTERN void irq_handler244(void);
_CRT_EXTERN void irq_handler245(void);
_CRT_EXTERN void irq_handler246(void);
_CRT_EXTERN void irq_handler247(void);
_CRT_EXTERN void irq_handler248(void);
_CRT_EXTERN void irq_handler249(void);
_CRT_EXTERN void irq_handler250(void);
_CRT_EXTERN void irq_handler251(void);
_CRT_EXTERN void irq_handler252(void);
_CRT_EXTERN void irq_handler253(void);
_CRT_EXTERN void irq_handler254(void);
_CRT_EXTERN void irq_handler255(void);

#endif // !_x86_GDT_H_
