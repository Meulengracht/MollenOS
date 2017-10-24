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
* MollenOS X86-32 PIC (Programmable Interrupt Controller)
* 
*/
#ifndef _X86_PIC_H_
#define _X86_PIC_H_

/* Includes 
 * - Library */
#include <os/osdefs.h>

/* PIC/EISA Bit Definitons 
 * Specified defines and constants for the PIC/EISA Systems */
#define PIC_PORT_PRIMARY        0x20
#define PIC_PORT_SECONDARY      0xA0
#define PIC_PORT_ELCR           0x4D0

#define PIC_REGISTER_ICW1       0x00
#define PIC_REGISTER_ICW2       0x01
#define PIC_REGISTER_ICW3       0x01
#define PIC_REGISTER_ICW4       0x01
#define PIC_REGISTER_IMR        0x01

#define PIC_ICW1_ICW4           0x01
#define PIC_ICW1_SELECT         0x10

#define PIC_ICW3_SLAVE          0x02
#define PIC_ICW3_CASCADE        0x04 // Enable Cascaded Mode

#define PIC_ICW4_MICROPC        0x01

#define PIC_ELCR_MASK(Irq)      (1 << (Irq))

/* PicInitialize
 * Initializes the legacy PIC and disables it. This is the preffered
 * behaviour at the moment as we only support APIC systems for now. */
KERNELAPI
void
KERNELABI
PicInitialize(void);

/* PicConfigureLine
 * Configures an interrupt line by enabling it and specifying
 * an interrupt mode for the pin. The interrupt mode can only be
 * specified on ELCR systems. */
KERNELAPI
void
KERNELABI
PicConfigureLine(
    _In_ int Irq,
    _In_ int Enable,
    _In_ int LevelTriggered);

/* PicGetConfiguration
 * Retrieves the current configuration for the given Irq and
 * its current status. */
KERNELAPI
void
KERNELABI
PicGetConfiguration(
    _In_ int Irq,
    _Out_ int *Enabled,
    _Out_ int *LevelTriggered);

#endif //!_X86_PIC_H_
