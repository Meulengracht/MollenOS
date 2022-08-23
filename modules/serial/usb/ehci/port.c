/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * MollenOS MCore - Enhanced Host Controller Interface Driver
 * TODO:
 * - Power Management
 */
//#define __TRACE

#include <ddk/utils.h>
#include "ehci.h"
#include <assert.h>
#include <threads.h>
#include <string.h>

void
EhciPortClearBits(
    _In_ EhciController_t* Controller,
    _In_ int               Index,
    _In_ reg32_t           Bits)
{
    reg32_t PortBits = READ_VOLATILE(Controller->OpRegisters->Ports[Index]);
    PortBits        &= ~(EHCI_PORT_RWC);
    PortBits        &= ~(Bits);
    WRITE_VOLATILE(Controller->OpRegisters->Ports[Index], PortBits);
}

void
EhciPortSetBits(
    _In_ EhciController_t* Controller,
    _In_ int               Index,
    _In_ reg32_t           Bits)
{
    reg32_t PortBits = READ_VOLATILE(Controller->OpRegisters->Ports[Index]);
    PortBits        &= ~(EHCI_PORT_RWC);
    PortBits        |= Bits;
    WRITE_VOLATILE(Controller->OpRegisters->Ports[Index], PortBits);
}

oserr_t
HciPortReset(
    _In_ UsbManagerController_t* Controller, 
    _In_ int                     Index)
{
    EhciController_t* EhciHci = (EhciController_t*)Controller;
    reg32_t Temp              = READ_VOLATILE(EhciHci->OpRegisters->Ports[Index]);

    // If we are per-port handled, and power is not enabled
    // then switch it on, and give it some time to recover
    if (!(Temp & EHCI_PORT_POWER)) {
        EhciPortSetBits(EhciHci, Index, EHCI_PORT_POWER);
        thrd_sleepex(20);
    }

    // The USBSTS:HcHalted bit must be zero, hence, the schedule must be running
    assert((READ_VOLATILE(EhciHci->OpRegisters->UsbStatus) & EHCI_STATUS_HALTED) == 0);
    
    // We must set the port-reset and keep the signal asserted for atleast 50 ms
    // now, we are going to keep the signal alive for (much) longer due to 
    // some devices being slow AF
    
    // The EHCI documentation says we should 
    // disable enabled and assert reset together
    Temp = READ_VOLATILE(EhciHci->OpRegisters->Ports[Index]);
    Temp &= ~(EHCI_PORT_RWC | EHCI_PORT_ENABLED);
    Temp |= EHCI_PORT_RESET;
    WRITE_VOLATILE(EhciHci->OpRegisters->Ports[Index], Temp);

    // Wait 200 ms for reset
    thrd_sleepex(200);
    EhciPortClearBits(EhciHci, Index, EHCI_PORT_RESET);

    // Wait for deassertion: 
    // The reset process is actually complete when software reads a 
    // zero in the PortReset bit
    Temp = 0;
    WaitForConditionWithFault(Temp, (READ_VOLATILE(EhciHci->OpRegisters->Ports[Index]) & EHCI_PORT_RESET) == 0, 250, 10);
    if (Temp != 0) {
        ERROR("EHCI::Host controller failed to reset the port in time.");
        return OsError;
    }
    EhciPortSetBits(EhciHci, Index, EHCI_PORT_RWC);

    // Now, if the port has a high-speed 
    // device, the enabled port is set
    if (!(READ_VOLATILE(EhciHci->OpRegisters->Ports[Index]) & EHCI_PORT_ENABLED)) {
        if (EHCI_SPARAM_CCCOUNT(EhciHci->SParameters) != 0) {
            EhciPortSetBits(EhciHci, Index, EHCI_PORT_COMPANION_HC);
        }
        return OsError;
    }
    return OsOK;
}

void
HciPortGetStatus(
    _In_  UsbManagerController_t* controller,
    _In_  int                     index,
    _Out_ UsbHcPortDescriptor_t*  port)
{
    EhciController_t* ehciHci = (EhciController_t*)controller;
    reg32_t           status;

    if (!controller || !port) {
        return;
    }

    status = READ_VOLATILE(ehciHci->OpRegisters->Ports[index]);

    // Is port connected?
    port->Connected = (status & EHCI_PORT_CONNECTED) == 0 ? 0 : 1;
    port->Enabled   = (status & EHCI_PORT_ENABLED) == 0 ? 0 : 1;
    port->Speed     = USB_SPEED_HIGH; // Ehci only has high-speed root ports
}

oserr_t
EhciPortCheck(
    _In_ EhciController_t*          Controller,
    _In_ size_t                     Index)
{
    reg32_t Status = READ_VOLATILE(Controller->OpRegisters->Ports[Index]);

    // Clear all event bits
    EhciPortSetBits(Controller, Index, EHCI_PORT_RWC);

    // Over-current event. We should tell the usb-stack this port
    // is now disabled and to disable anything related to this device
    if (Status & EHCI_PORT_OC_EVENT) {
        ERROR("Port %u reported over current. TODO");
        return OsOK;
    }

    // Connection event?
    if (Status & EHCI_PORT_CONNECT_EVENT) {
        TRACE("EhciPortCheck(Index %u, Status 0x%x)", Index, Status);

        // Determine the type of connection event
        // because the port might not be for us
        // We have to release it in case of low-speed
        if (Status & EHCI_PORT_CONNECTED) {
            if (EHCI_PORT_LINESTATUS(Status) == EHCI_LINESTATUS_RELEASE) {
                if (EHCI_SPARAM_CCCOUNT(Controller->SParameters) != 0) {
                    EhciPortSetBits(Controller, Index, EHCI_PORT_COMPANION_HC);
                }
                return OsOK;
            }
        }
        return UsbEventPort(Controller->Base.Device->Base.Id, (uint8_t)(Index & 0xFF));
    }

    // Enable event. This can only happen when it gets disabled due to something
    // like suspend or that i imagine.
    if (Status & EHCI_PORT_ENABLE_EVENT) {
        ERROR("Port %u is now disabled. TODO");
        return OsOK;
    }
    return OsError;
}

void
EhciPortScan(
    _In_ EhciController_t*          Controller,
    _In_ reg32_t                    ChangeBits)
{
    for (size_t i = 0; i < Controller->Base.PortCount; i++) {
        if (ChangeBits & (1 << i)) {
            EhciPortCheck(Controller, i);
        }
    }
}
