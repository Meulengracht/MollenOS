/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS x86-32 Descriptor Table
 * - Interrupt Descriptor Table
 */

#include <arch.h>
#include <idt.h>
#include <gdt.h>
#include <string.h>

#define irq_stringify(irq) irq_handler##irq

static IdtEntry_t IdtDescriptors[IDT_DESCRIPTORS] = { { 0 } };
IdtObject_t Idtptr;

static void InterruptInstallDefaultGates(void);

static void IdtInstallDescriptor(int IntNum, uintptr_t Base,
    uint16_t Selector, uint8_t Flags)
{
    IdtDescriptors[IntNum].BaseLow  = (Base & 0xFFFF);
    IdtDescriptors[IntNum].BaseHigh = ((Base >> 16) & 0xFFFF);
    IdtDescriptors[IntNum].Selector = Selector;
    IdtDescriptors[IntNum].Zero     = 0;
    IdtDescriptors[IntNum].Flags    = Flags;
}

/* IdtInitialize
 * Initialize the idt table with the 256 default
 * descriptors for entering shared interrupt handlers
 * and shared exception handlers */
void IdtInitialize(void)
{
    Idtptr.Limit = (sizeof(IdtEntry_t) * IDT_DESCRIPTORS) - 1;
    Idtptr.Base = (uint32_t)&IdtDescriptors[0];
    memset(&IdtDescriptors[0], 0, sizeof(IdtDescriptors));
    InterruptInstallDefaultGates();
    IdtInstallDescriptor(INTERRUPT_SYSCALL, (uintptr_t)syscall_entry, 
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_TRAP_GATE32);
    IdtInstall();
}

void InterruptInstallDefaultGates(void)
{
    IdtInstallDescriptor(0, (uint32_t)&irq_handler0,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(1, (uint32_t)&irq_handler1,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(2, (uint32_t)&irq_handler2,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(3, (uint32_t)&irq_handler3,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(4, (uint32_t)&irq_handler4,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(5, (uint32_t)&irq_handler5,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(6, (uint32_t)&irq_handler6,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(7, (uint32_t)&irq_handler7,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(8, (uint32_t)&irq_handler8,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(9, (uint32_t)&irq_handler9,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(10, (uint32_t)&irq_handler10,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(11, (uint32_t)&irq_handler11,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(12, (uint32_t)&irq_handler12,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(13, (uint32_t)&irq_handler13,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(14, (uint32_t)&irq_handler14,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(15, (uint32_t)&irq_handler15,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(16, (uint32_t)&irq_handler16,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(17, (uint32_t)&irq_handler17,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(18, (uint32_t)&irq_handler18,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(19, (uint32_t)&irq_handler19,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(20, (uint32_t)&irq_handler20,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(21, (uint32_t)&irq_handler21,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(22, (uint32_t)&irq_handler22,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(23, (uint32_t)&irq_handler23,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(24, (uint32_t)&irq_handler24,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(25, (uint32_t)&irq_handler25,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(26, (uint32_t)&irq_handler26,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(27, (uint32_t)&irq_handler27,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(28, (uint32_t)&irq_handler28,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(29, (uint32_t)&irq_handler29,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(30, (uint32_t)&irq_handler30,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(31, (uint32_t)&irq_handler31,
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);

    IdtInstallDescriptor(32, (uint32_t)&irq_stringify(32), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(33, (uint32_t)&irq_stringify(33), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(34, (uint32_t)&irq_stringify(34), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(35, (uint32_t)&irq_stringify(35), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(36, (uint32_t)&irq_stringify(36), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(37, (uint32_t)&irq_stringify(37), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(38, (uint32_t)&irq_stringify(38), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(39, (uint32_t)&irq_stringify(39), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(40, (uint32_t)&irq_stringify(40), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(41, (uint32_t)&irq_stringify(41), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(42, (uint32_t)&irq_stringify(42), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(43, (uint32_t)&irq_stringify(43), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(44, (uint32_t)&irq_stringify(44), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(45, (uint32_t)&irq_stringify(45), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(46, (uint32_t)&irq_stringify(46), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(47, (uint32_t)&irq_stringify(47), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(48, (uint32_t)&irq_stringify(48), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(49, (uint32_t)&irq_stringify(49), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(50, (uint32_t)&irq_stringify(50), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(51, (uint32_t)&irq_stringify(51), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(52, (uint32_t)&irq_stringify(52), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(53, (uint32_t)&irq_stringify(53), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(54, (uint32_t)&irq_stringify(54), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(55, (uint32_t)&irq_stringify(55), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(56, (uint32_t)&irq_stringify(56), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(57, (uint32_t)&irq_stringify(57), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(58, (uint32_t)&irq_stringify(58), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(59, (uint32_t)&irq_stringify(59), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(60, (uint32_t)&irq_stringify(60), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(61, (uint32_t)&irq_stringify(61), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(62, (uint32_t)&irq_stringify(62), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(63, (uint32_t)&irq_stringify(63), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(64, (uint32_t)&irq_stringify(64), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(65, (uint32_t)&irq_stringify(65), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(66, (uint32_t)&irq_stringify(66), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(67, (uint32_t)&irq_stringify(67), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(68, (uint32_t)&irq_stringify(68), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(69, (uint32_t)&irq_stringify(69), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(70, (uint32_t)&irq_stringify(70), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(71, (uint32_t)&irq_stringify(71), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(72, (uint32_t)&irq_stringify(72), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(73, (uint32_t)&irq_stringify(73), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(74, (uint32_t)&irq_stringify(74), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(75, (uint32_t)&irq_stringify(75), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(76, (uint32_t)&irq_stringify(76), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(77, (uint32_t)&irq_stringify(77), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(78, (uint32_t)&irq_stringify(78), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(79, (uint32_t)&irq_stringify(79), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(80, (uint32_t)&irq_stringify(80), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(81, (uint32_t)&irq_stringify(81), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(82, (uint32_t)&irq_stringify(82), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(83, (uint32_t)&irq_stringify(83), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(84, (uint32_t)&irq_stringify(84), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(85, (uint32_t)&irq_stringify(85), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(86, (uint32_t)&irq_stringify(86), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(87, (uint32_t)&irq_stringify(87), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(88, (uint32_t)&irq_stringify(88), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(89, (uint32_t)&irq_stringify(89), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(90, (uint32_t)&irq_stringify(90), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(91, (uint32_t)&irq_stringify(91), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(92, (uint32_t)&irq_stringify(92), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(93, (uint32_t)&irq_stringify(93), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(94, (uint32_t)&irq_stringify(94), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(95, (uint32_t)&irq_stringify(95), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(96, (uint32_t)&irq_stringify(96), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(97, (uint32_t)&irq_stringify(97), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(98, (uint32_t)&irq_stringify(98), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(99, (uint32_t)&irq_stringify(99), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(100, (uint32_t)&irq_stringify(100), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(101, (uint32_t)&irq_stringify(101), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(102, (uint32_t)&irq_stringify(102), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(103, (uint32_t)&irq_stringify(103), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(104, (uint32_t)&irq_stringify(104), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(105, (uint32_t)&irq_stringify(105), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(106, (uint32_t)&irq_stringify(106), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(107, (uint32_t)&irq_stringify(107), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(108, (uint32_t)&irq_stringify(108), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(109, (uint32_t)&irq_stringify(109), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(110, (uint32_t)&irq_stringify(110), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(111, (uint32_t)&irq_stringify(111), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(112, (uint32_t)&irq_stringify(112), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(113, (uint32_t)&irq_stringify(113), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(114, (uint32_t)&irq_stringify(114), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(115, (uint32_t)&irq_stringify(115), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(116, (uint32_t)&irq_stringify(116), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(117, (uint32_t)&irq_stringify(117), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(118, (uint32_t)&irq_stringify(118), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(119, (uint32_t)&irq_stringify(119), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(120, (uint32_t)&irq_stringify(120), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(121, (uint32_t)&irq_stringify(121), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(122, (uint32_t)&irq_stringify(122), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(123, (uint32_t)&irq_stringify(123), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(124, (uint32_t)&irq_stringify(124), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(125, (uint32_t)&irq_stringify(125), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(126, (uint32_t)&irq_stringify(126), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(127, (uint32_t)&irq_stringify(127), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(128, (uint32_t)&irq_stringify(128), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(129, (uint32_t)&irq_stringify(129), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(130, (uint32_t)&irq_stringify(130), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(131, (uint32_t)&irq_stringify(131), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(132, (uint32_t)&irq_stringify(132), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(133, (uint32_t)&irq_stringify(133), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(134, (uint32_t)&irq_stringify(134), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(135, (uint32_t)&irq_stringify(135), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(136, (uint32_t)&irq_stringify(136), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(137, (uint32_t)&irq_stringify(137), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(138, (uint32_t)&irq_stringify(138), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(139, (uint32_t)&irq_stringify(139), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(140, (uint32_t)&irq_stringify(140), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(141, (uint32_t)&irq_stringify(141), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(142, (uint32_t)&irq_stringify(142), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(143, (uint32_t)&irq_stringify(143), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(144, (uint32_t)&irq_stringify(144), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(145, (uint32_t)&irq_stringify(145), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(146, (uint32_t)&irq_stringify(146), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(147, (uint32_t)&irq_stringify(147), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(148, (uint32_t)&irq_stringify(148), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(149, (uint32_t)&irq_stringify(149), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(150, (uint32_t)&irq_stringify(150), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(151, (uint32_t)&irq_stringify(151), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(152, (uint32_t)&irq_stringify(152), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(153, (uint32_t)&irq_stringify(153), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(154, (uint32_t)&irq_stringify(154), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(155, (uint32_t)&irq_stringify(155), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(156, (uint32_t)&irq_stringify(156), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(157, (uint32_t)&irq_stringify(157), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(158, (uint32_t)&irq_stringify(158), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(159, (uint32_t)&irq_stringify(159), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(160, (uint32_t)&irq_stringify(160), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(161, (uint32_t)&irq_stringify(161), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(162, (uint32_t)&irq_stringify(162), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(163, (uint32_t)&irq_stringify(163), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(164, (uint32_t)&irq_stringify(164), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(165, (uint32_t)&irq_stringify(165), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(166, (uint32_t)&irq_stringify(166), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(167, (uint32_t)&irq_stringify(167), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(168, (uint32_t)&irq_stringify(168), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(169, (uint32_t)&irq_stringify(169), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(170, (uint32_t)&irq_stringify(170), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(171, (uint32_t)&irq_stringify(171), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(172, (uint32_t)&irq_stringify(172), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(173, (uint32_t)&irq_stringify(173), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(174, (uint32_t)&irq_stringify(174), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(175, (uint32_t)&irq_stringify(175), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(176, (uint32_t)&irq_stringify(176), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(177, (uint32_t)&irq_stringify(177), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(178, (uint32_t)&irq_stringify(178), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(179, (uint32_t)&irq_stringify(179), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(180, (uint32_t)&irq_stringify(180), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(181, (uint32_t)&irq_stringify(181), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(182, (uint32_t)&irq_stringify(182), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(183, (uint32_t)&irq_stringify(183), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(184, (uint32_t)&irq_stringify(184), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(185, (uint32_t)&irq_stringify(185), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(186, (uint32_t)&irq_stringify(186), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(187, (uint32_t)&irq_stringify(187), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(188, (uint32_t)&irq_stringify(188), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(189, (uint32_t)&irq_stringify(189), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(190, (uint32_t)&irq_stringify(190), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(191, (uint32_t)&irq_stringify(191), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(192, (uint32_t)&irq_stringify(192), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(193, (uint32_t)&irq_stringify(193), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(194, (uint32_t)&irq_stringify(194), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(195, (uint32_t)&irq_stringify(195), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(196, (uint32_t)&irq_stringify(196), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(197, (uint32_t)&irq_stringify(197), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(198, (uint32_t)&irq_stringify(198), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(199, (uint32_t)&irq_stringify(199), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(200, (uint32_t)&irq_stringify(200), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(201, (uint32_t)&irq_stringify(201), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(202, (uint32_t)&irq_stringify(202), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(203, (uint32_t)&irq_stringify(203), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(204, (uint32_t)&irq_stringify(204), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(205, (uint32_t)&irq_stringify(205), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(206, (uint32_t)&irq_stringify(206), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(207, (uint32_t)&irq_stringify(207), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(208, (uint32_t)&irq_stringify(208), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(209, (uint32_t)&irq_stringify(209), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(210, (uint32_t)&irq_stringify(210), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(211, (uint32_t)&irq_stringify(211), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(212, (uint32_t)&irq_stringify(212), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(213, (uint32_t)&irq_stringify(213), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(214, (uint32_t)&irq_stringify(214), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(215, (uint32_t)&irq_stringify(215), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(216, (uint32_t)&irq_stringify(216), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(217, (uint32_t)&irq_stringify(217), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(218, (uint32_t)&irq_stringify(218), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(219, (uint32_t)&irq_stringify(219), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(220, (uint32_t)&irq_stringify(220), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(221, (uint32_t)&irq_stringify(221), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(222, (uint32_t)&irq_stringify(222), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(223, (uint32_t)&irq_stringify(223), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(224, (uint32_t)&irq_stringify(224), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(225, (uint32_t)&irq_stringify(225), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(226, (uint32_t)&irq_stringify(226), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(227, (uint32_t)&irq_stringify(227), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(228, (uint32_t)&irq_stringify(228), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(229, (uint32_t)&irq_stringify(229), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(230, (uint32_t)&irq_stringify(230), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(231, (uint32_t)&irq_stringify(231), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(232, (uint32_t)&irq_stringify(232), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(233, (uint32_t)&irq_stringify(233), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(234, (uint32_t)&irq_stringify(234), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(235, (uint32_t)&irq_stringify(235), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(236, (uint32_t)&irq_stringify(236), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(237, (uint32_t)&irq_stringify(237), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(238, (uint32_t)&irq_stringify(238), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(239, (uint32_t)&irq_stringify(239), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(240, (uint32_t)&irq_stringify(240), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(241, (uint32_t)&irq_stringify(241), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(242, (uint32_t)&irq_stringify(242), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(243, (uint32_t)&irq_stringify(243), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(244, (uint32_t)&irq_stringify(244), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(245, (uint32_t)&irq_stringify(245), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(246, (uint32_t)&irq_stringify(246), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(247, (uint32_t)&irq_stringify(247), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(248, (uint32_t)&irq_stringify(248), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(249, (uint32_t)&irq_stringify(249), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(250, (uint32_t)&irq_stringify(250), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(251, (uint32_t)&irq_stringify(251), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(252, (uint32_t)&irq_stringify(252), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(253, (uint32_t)&irq_stringify(253), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(254, (uint32_t)&irq_stringify(254), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
    IdtInstallDescriptor(255, (uint32_t)&irq_stringify(255), GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
}
