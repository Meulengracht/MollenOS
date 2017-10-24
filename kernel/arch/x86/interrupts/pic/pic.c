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
 * MollenOS X86-32 PIC (Programmable Interrupt Controller)
 *
 */

/* Includes 
 * - System */
#include <arch.h>
#include <pic.h>

/* Globals
 * State keeping variables. We need to keep track of some things. */
static int GlbElcrInitialized = 0;

/* PicInitialize
 * Initializes the legacy PIC and disables it. This is the preffered
 * behaviour at the moment as we only support APIC systems for now. */
void
PicInitialize(void)
{
    // Variables
    uint16_t Status = 0;

    // Init for safety
    GlbElcrInitialized = 0;

    // Initialize controller 1
    outb(PIC_PORT_PRIMARY + PIC_REGISTER_ICW1,  PIC_ICW1_SELECT | PIC_ICW1_ICW4); // Enable
    outb(PIC_PORT_PRIMARY + PIC_REGISTER_ICW2,  0x20); // Remap primary PIC to 0x20 - 0x28
    outb(PIC_PORT_PRIMARY + PIC_REGISTER_ICW3,  PIC_ICW3_CASCADE); // Cascade mode
    outb(PIC_PORT_PRIMARY + PIC_REGISTER_ICW4,  PIC_ICW4_MICROPC); // Enable i86 mode

    // Initialize controller 2
    outb(PIC_PORT_SECONDARY + PIC_REGISTER_ICW1,  PIC_ICW1_SELECT | PIC_ICW1_ICW4); // Enable
    outb(PIC_PORT_SECONDARY + PIC_REGISTER_ICW2,  0x28); // Remap Secondary PIC to 0x28 - 0x30
    outb(PIC_PORT_SECONDARY + PIC_REGISTER_ICW3,  PIC_ICW3_SLAVE); // Slave mode
    outb(PIC_PORT_SECONDARY + PIC_REGISTER_ICW4,  PIC_ICW4_MICROPC); // Enable i86 mode

    // Mask out controllers IMR
	outb(PIC_PORT_PRIMARY + PIC_REGISTER_IMR,     0xFF); // except for cascade? (0xFB)
    outb(PIC_PORT_SECONDARY + PIC_REGISTER_IMR,   0xFF);

    // Setup OCW's
    outb(PIC_PORT_PRIMARY, 0x20);
    outb(PIC_PORT_SECONDARY, 0x20);
    
    // Clear out any outstanding ISR's
    inb(PIC_PORT_PRIMARY);
    outb(PIC_PORT_PRIMARY, 0x08 | 0x03);
    inb(PIC_PORT_SECONDARY);
    outb(PIC_PORT_SECONDARY, 0x08 | 0x03);
    
    // Initialize the ELCR if present
    // We do this by verifying irq 0, 1, 2, 8 and 13 are edge triggered
    // If the bit is set, it's level triggered.
    Status = (inb(PIC_PORT_ELCR + 1) << 8) | inb(PIC_PORT_ELCR);
    if ((Status & (PIC_ELCR_MASK(0) | PIC_ELCR_MASK(1) | PIC_ELCR_MASK(2) |
        PIC_ELCR_MASK(8) | PIC_ELCR_MASK(13))) != 0) {
        // Not Present
    }
    else {
        // Present
        GlbElcrInitialized = 1;
    }
}

/* PicConfigureLine
 * Configures an interrupt line by enabling it and specifying
 * an interrupt mode for the pin. The interrupt mode can only be
 * specified on ELCR systems. */
void
PicConfigureLine(
    _In_ int Irq,
    _In_ int Enable,
    _In_ int LevelTriggered)
{
    // Configure for either level/edge
    if (GlbElcrInitialized == 1) {
        uint16_t Status = (inb(PIC_PORT_ELCR + 1) << 8) | inb(PIC_PORT_ELCR);
        if (LevelTriggered == 1) {
            Status |= PIC_ELCR_MASK(Irq);
        }
        else {
            Status &= ~(PIC_ELCR_MASK(Irq));
        }
        if (Irq >= 8) {
            outb(PIC_PORT_ELCR + 1, Status >> 8);
        }
        else {
            outb(PIC_PORT_ELCR, Status & 0xff);
        }
    }

    // Determine whether or not mask/unmask
    if (Enable == 0) {
        // Mask
    }
    else {
        // Unmask
    }
}

/* PicGetConfiguration
 * Retrieves the current configuration for the given Irq and
 * its current status. */
void
PicGetConfiguration(
    _In_ int Irq,
    _Out_ int *Enabled,
    _Out_ int *LevelTriggered)
{
    // Set initial
    *Enabled = 0;
    *LevelTriggered = 0;

    // Configure for either level/edge
    if (GlbElcrInitialized == 1) {
        uint16_t Status = (inb(PIC_PORT_ELCR + 1) << 8) | inb(PIC_PORT_ELCR);
        if (Status & PIC_ELCR_MASK(Irq)) {
            *LevelTriggered = 1;
        }
    }

    // Determine whether or not mask/unmask
}
