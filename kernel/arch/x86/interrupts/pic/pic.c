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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS X86-32 PIC (Programmable Interrupt Controller)
 *
 */

#include <arch/io.h>
#include <arch/x86/pic.h>

static int g_elcrInitialized = 0;

/* PicGetElcr 
 * Retrieves the elcr status register(s). */
oserr_t
PicGetElcr(
    _Out_ uint16_t* Elcr)
{
    // Variables
    uint16_t Status     = 0;
    size_t Storage      = 0;

    // Read registers and combine values
    if (ReadDirectIo(DeviceIoPortBased, PIC_PORT_ELCR + 1, 1, &Storage) != OsOK) {
        return OsError;
    }
    Status = (Storage & 0xFF) << 8;
    if (ReadDirectIo(DeviceIoPortBased, PIC_PORT_ELCR, 1, &Storage) != OsOK) {
        return OsError;
    }
    Status |= (Storage & 0xFF);

    // Update out and return
    *Elcr = Status;
    return OsOK;
}

/* PicInitialize
 * Initializes the legacy PIC and disables it. This is the preffered
 * behaviour at the moment as we only support APIC systems for now. */
void
PicInitialize(void)
{
    // Variables
    uint16_t Status     = 0;
    size_t Storage      = 0;

    // Initialize controller 1
    WriteDirectIo(DeviceIoPortBased, PIC_PORT_PRIMARY + PIC_REGISTER_ICW1, 1,    PIC_ICW1_SELECT | PIC_ICW1_ICW4); // Enable
    WriteDirectIo(DeviceIoPortBased, PIC_PORT_PRIMARY + PIC_REGISTER_ICW2, 1,    0x20); // Remap primary PIC to 0x20 - 0x28
    WriteDirectIo(DeviceIoPortBased, PIC_PORT_PRIMARY + PIC_REGISTER_ICW3, 1,    PIC_ICW3_CASCADE); // Cascade mode
    WriteDirectIo(DeviceIoPortBased, PIC_PORT_PRIMARY + PIC_REGISTER_ICW4, 1,    PIC_ICW4_MICROPC); // Enable i86 mode

    // Initialize controller 2
    WriteDirectIo(DeviceIoPortBased, PIC_PORT_SECONDARY + PIC_REGISTER_ICW1, 1,  PIC_ICW1_SELECT | PIC_ICW1_ICW4); // Enable
    WriteDirectIo(DeviceIoPortBased, PIC_PORT_SECONDARY + PIC_REGISTER_ICW2, 1,  0x28); // Remap Secondary PIC to 0x28 - 0x30
    WriteDirectIo(DeviceIoPortBased, PIC_PORT_SECONDARY + PIC_REGISTER_ICW3, 1,  PIC_ICW3_SLAVE); // Slave mode
    WriteDirectIo(DeviceIoPortBased, PIC_PORT_SECONDARY + PIC_REGISTER_ICW4, 1,  PIC_ICW4_MICROPC); // Enable i86 mode

    // Mask out controllers IMR
	WriteDirectIo(DeviceIoPortBased, PIC_PORT_PRIMARY + PIC_REGISTER_IMR,   1, 0xFF); // except for cascade? (0xFB)
    WriteDirectIo(DeviceIoPortBased, PIC_PORT_SECONDARY + PIC_REGISTER_IMR, 1, 0xFF);

    // Setup OCW's
    WriteDirectIo(DeviceIoPortBased, PIC_PORT_PRIMARY,   1, 0x20);
    WriteDirectIo(DeviceIoPortBased, PIC_PORT_SECONDARY, 1, 0x20);
    
    // Clear out any outstanding ISR's
    ReadDirectIo(DeviceIoPortBased, PIC_PORT_PRIMARY, 1, &Storage);
    WriteDirectIo(DeviceIoPortBased, PIC_PORT_PRIMARY, 1, 0x08 | 0x03);
    ReadDirectIo(DeviceIoPortBased, PIC_PORT_SECONDARY, 1, &Storage);
    WriteDirectIo(DeviceIoPortBased, PIC_PORT_SECONDARY, 1, 0x08 | 0x03);
    
    // Initialize the ELCR if present
    // We do this by verifying irq 0, 1, 2, 8 and 13 are edge triggered
    // If the bit is set, it's level triggered.
    PicGetElcr(&Status);
    if ((Status & (PIC_ELCR_MASK(0) | PIC_ELCR_MASK(1) | PIC_ELCR_MASK(2) |
        PIC_ELCR_MASK(8) | PIC_ELCR_MASK(13))) != 0) {
        // Not Present
        g_elcrInitialized = 0;
    }
    else {
        // Present
        g_elcrInitialized = 1;
    }
}

/* PicConfigureLine
 * Configures an interrupt line by enabling it and specifying
 * an interrupt mode for the pin. The interrupt mode can only be
 * specified on ELCR systems. */
void
PicConfigureLine(
    _In_ int        Irq,
    _In_ int        Enable,
    _In_ int        LevelTriggered)
{
    // Variables
    uint16_t Status     = 0;
    size_t Mask         = 0;

    // Configure for either level/edge
    if (g_elcrInitialized == 1 && LevelTriggered != -1) {
        PicGetElcr(&Status);
        if (LevelTriggered == 1) {
            Status |= PIC_ELCR_MASK(Irq);
        }
        else {
            Status &= ~(PIC_ELCR_MASK(Irq));
        }
        if (Irq >= 8) {
            WriteDirectIo(DeviceIoPortBased, PIC_PORT_ELCR + 1, 1, Status >> 8);
        }
        else {
            WriteDirectIo(DeviceIoPortBased, PIC_PORT_ELCR, 1, Status & 0xff);
        }
    }

    // Determine whether or not mask/unmask
    if (Enable != -1) {
        if (Enable == 0) {
            if (Irq >= 8) {
                ReadDirectIo(DeviceIoPortBased, PIC_PORT_SECONDARY + PIC_REGISTER_IMR, 1, &Mask);
                WriteDirectIo(DeviceIoPortBased, PIC_PORT_SECONDARY + PIC_REGISTER_IMR, 1, Mask | (1 << (Irq - 8)));
            }
            else {
                ReadDirectIo(DeviceIoPortBased, PIC_PORT_PRIMARY + PIC_REGISTER_IMR, 1, &Mask);
                WriteDirectIo(DeviceIoPortBased, PIC_PORT_PRIMARY + PIC_REGISTER_IMR, 1,  Mask | (1 << Irq));
            }
        }
        else {
            if (Irq >= 8) {
                ReadDirectIo(DeviceIoPortBased, PIC_PORT_SECONDARY + PIC_REGISTER_IMR, 1, &Mask);
                WriteDirectIo(DeviceIoPortBased, PIC_PORT_SECONDARY + PIC_REGISTER_IMR, 1, Mask & ~(1 << (Irq - 8)));
            }
            else {
                ReadDirectIo(DeviceIoPortBased, PIC_PORT_PRIMARY + PIC_REGISTER_IMR, 1, &Mask);
                WriteDirectIo(DeviceIoPortBased, PIC_PORT_PRIMARY + PIC_REGISTER_IMR, 1,  Mask & ~(1 << Irq));
            }
        }
    }
}

/* PicGetConfiguration
 * Retrieves the current configuration for the given Irq and
 * its current status. */
void
PicGetConfiguration(
    _In_  int       Irq,
    _Out_ int*      Enabled,
    _Out_ int*      LevelTriggered)
{
    // Variables
    uint16_t Status     = 0;
    size_t Mask         = 0;

    // Set initial
    *Enabled            = 0;
    *LevelTriggered     = 0;

    // Configure for either level/edge
    if (g_elcrInitialized == 1) {
        PicGetElcr(&Status);
        if (Status & PIC_ELCR_MASK(Irq)) {
            *LevelTriggered = 1;
        }
    }

    // Determine whether or not mask/unmask
    if (Irq >= 8) {
        ReadDirectIo(DeviceIoPortBased, PIC_PORT_SECONDARY + PIC_REGISTER_IMR, 1, &Mask);
        Irq -= 8;
    }
    else {
        ReadDirectIo(DeviceIoPortBased, PIC_PORT_PRIMARY + PIC_REGISTER_IMR, 1, &Mask);
    }
    *Enabled = (Mask & (1 << Irq)) == 0 ? 1 : 0;
}

/* PicSendEoi
 * Signals end of interrupt service for the appropriate pic chip */
void
PicSendEoi(
    _In_ int        Irq)
{
	if(Irq >= 8) {
        WriteDirectIo(DeviceIoPortBased, PIC_PORT_SECONDARY, 1, 0x20);
    }
    WriteDirectIo(DeviceIoPortBased, PIC_PORT_PRIMARY, 1, 0x20);
}
