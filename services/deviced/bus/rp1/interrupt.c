/**
 * Copyright 2026, Philip Meulengracht
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
 * RP1 interrupt registers are in its peripheral-control area (APBS), at offset
 * 0x108000 within BAR1, a PCI register describing memory assigned to RP1.
 * SET and CLEAR are separate addresses for setting or clearing register bits,
 * so each change can leave the other bits untouched.
 * Register layout follows Raspberry Pi Linux drivers/mfd/rp1.c (rpi-6.18.y).
 * 
 */

#include <bus/rp1/interrupt.h>
#include <string.h>

#define RP1_IRQ_CFG(source) (0x8U + 4U * (source))
#define RP1_IRQ_SET 0x800U
#define RP1_IRQ_CLEAR 0xc00U
#define RP1_IRQ_ENABLE 1U
#define RP1_IRQ_IACK (1U << 2)
#define RP1_IRQ_IACK_ENABLE (1U << 3)

static oserr_t
__Rp1InterruptWrite(
    _In_ struct Rp1InterruptController* controller,
    _In_ unsigned int                   source,
    _In_ unsigned int                   alias,
    _In_ uint32_t                       bits)
{
    oserr_t status;

    // A successful PCI write may still be waiting at a bridge. Read the source's
    // normal register to make sure the change reached RP1 before the caller
    // allows interrupt delivery or returns to code that depends on the change.
    status = WriteDeviceIo(controller->Registers, alias + RP1_IRQ_CFG(source), bits, 4);
    if (status == OS_EOK) {
        // This read only waits for the earlier write to arrive; its value is unused.
        (void)ReadDeviceIo(controller->Registers, RP1_IRQ_CFG(source), 4);
    }
    return status;
}

oserr_t
Rp1InterruptInitialize(
    _Out_ struct Rp1InterruptController*   controller,
    _In_  DeviceIo_t*                      registers,
    _In_  const struct Rp1InterruptParent* parent,
    _In_  Rp1InterruptHandler              handler,
    _In_  void*                            context)
{
    unsigned int source;
    oserr_t      status;

    // The controller needs mapped registers, all 61 PCI interrupt entries, and
    // callbacks that can block delivery, wait for handlers, and allow delivery again.
    if (controller == NULL || registers == NULL || parent == NULL || handler == NULL) {
        return OS_EINVALPARAMS;
    }
    
    if (controller->Registers != NULL || registers->Type != DeviceIoMemoryBased ||
        !registers->Access.Memory.VirtualBase || registers->Access.Memory.Length < RP1_PCIE_APBS_LENGTH) {
        return OS_EINVALPARAMS;
    }
   
    if (parent->VectorCount < FDT_RP1_INTERRUPT_COUNT ||
        parent->MaskAndSynchronize == NULL || parent->Unmask == NULL) {
        return OS_ENOTSUPPORTED;
    }
    
    memset(controller, 0, sizeof(*controller));
    controller->Registers = registers;
    controller->Parent = *parent;
    controller->Handler = handler;
    controller->Context = context;
    
    // Stop delivery before touching any source so no handler can run while only
    // part of the controller has been initialized.
    for (source = 0; source < FDT_RP1_INTERRUPT_COUNT; source++) {
        parent->MaskAndSynchronize(parent->Context, source);
    }
    
    // Leave every RP1 source disabled. If a register write fails, PCI interrupt
    // delivery stays blocked and we keep the register pointer so Destroy can
    // retry shutdown. The caller must keep those registers mapped.
    for (source = 0; source < FDT_RP1_INTERRUPT_COUNT; source++) {
        status = __Rp1InterruptWrite(controller, source, RP1_IRQ_CLEAR,
            RP1_IRQ_ENABLE | RP1_IRQ_IACK_ENABLE);
        if (status != OS_EOK) {
            // Keep delivery blocked and the saved state available for a shutdown retry.
            return status;
        }
    }
    
    controller->Ready = 1;
    return OS_EOK;
}

oserr_t
Rp1InterruptConfigure(
    _InOut_ struct Rp1InterruptController* controller,
    _In_    unsigned int                   source,
    _In_    unsigned int                   type)
{
    oserr_t status;

    // Choose how the signal triggers an interrupt before enabling the source:
    // when it changes from low to high (rising edge), or while it stays high
    // (high level). Changing an active source could lose or misreport an interrupt.
    if (controller == NULL || controller->Registers == NULL || source >= FDT_RP1_INTERRUPT_COUNT) {
        return OS_EINVALPARAMS;
    }
    
    if (!controller->Ready || (type != 1 && type != 4) || controller->Enabled[source]) {
        return OS_EINVALPARAMS;
    }
    
    status = __Rp1InterruptWrite(
        controller,
        source,
        type == 4 ? RP1_IRQ_SET : RP1_IRQ_CLEAR, RP1_IRQ_IACK_ENABLE
    );
    if (status == OS_EOK) {
        controller->Types[source] = (uint8_t)type;
    }
    return status;
}

oserr_t
Rp1InterruptEnable(
    _InOut_ struct Rp1InterruptController* controller,
    _In_    unsigned int                   source)
{
    oserr_t status;

    // Configure how the source signals an interrupt before enabling it, so RP1
    // never delivers an interrupt with unknown settings.
    if (controller == NULL || controller->Registers == NULL || source >= FDT_RP1_INTERRUPT_COUNT) {
        return OS_EINVALPARAMS;
    }
    if (!controller->Ready || !controller->Types[source] || controller->Enabled[source]) {
        return OS_EINVALPARAMS;
    }
    
    // Turn on the RP1 source first. Allow PCI interrupt delivery only after
    // this change reaches RP1, or a handler could run before setup is complete.
    status = __Rp1InterruptWrite(controller, source, RP1_IRQ_SET, RP1_IRQ_ENABLE);
    if (status == OS_EOK) {
        controller->Enabled[source] = 1;
        controller->Parent.Unmask(controller->Parent.Context, source);
    }
    return status;
}

oserr_t
Rp1InterruptDisable(
    _InOut_ struct Rp1InterruptController* controller,
    _In_    unsigned int                   source)
{
    // Block new interrupts from PCI and wait for any running handler to finish
    // before changing the RP1 source or its saved state.
    if (controller == NULL || controller->Registers == NULL || source >= FDT_RP1_INTERRUPT_COUNT) {
        return OS_EINVALPARAMS;
    }
    controller->Parent.MaskAndSynchronize(controller->Parent.Context, source);
    controller->Enabled[source] = 0;
    return __Rp1InterruptWrite(controller, source, RP1_IRQ_CLEAR, RP1_IRQ_ENABLE);
}

oserr_t
Rp1InterruptHandle(
    _InOut_ struct Rp1InterruptController* controller,
    _In_ unsigned int source)
{
    // Ignore interrupts received during setup or after their source was disabled;
    // they must not call a device handler that is no longer ready.
    if (controller == NULL || controller->Registers == NULL || source >= FDT_RP1_INTERRUPT_COUNT) {
        return OS_EINVALPARAMS;
    }
    if (!controller->Ready || !controller->Enabled[source]) {
        return OS_ENOENT;
    }
    
    controller->Handler(controller->Context, source);
    if (controller->Types[source] == 4) {
        // A high-level interrupt keeps its signal high until the device's
        // interrupt condition is cleared. Tell RP1 that handling is done only
        // after the device handler has had a chance to clear that condition.
        return __Rp1InterruptWrite(controller, source, RP1_IRQ_SET, RP1_IRQ_IACK);
    }
    return OS_EOK;
}

oserr_t
Rp1InterruptDestroy(
    _InOut_ struct Rp1InterruptController* controller)
{
    unsigned int source;
    oserr_t      status;
    oserr_t      result = OS_EOK;

    // If any source cannot be shut down, keep its saved state so we can retry.
    // The caller must keep the registers mapped until shutdown succeeds.
    if (controller == NULL) {
        return OS_EINVALPARAMS;
    }
    if (controller->Registers == NULL) {
        return OS_EOK;
    }
    
    // Try to disable every source even if one fails, so shutdown can make as
    // much progress as possible before returning an error.
    for (source = 0; source < FDT_RP1_INTERRUPT_COUNT; source++) {
        status = Rp1InterruptDisable(controller, source);
        if (status != OS_EOK) {
            result = status;
        }
    }
    
    controller->Ready = 0;
    if (result == OS_EOK) {
        memset(controller, 0, sizeof(*controller));
    }
    return result;
}
