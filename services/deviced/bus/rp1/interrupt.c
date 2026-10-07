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
 * RP1 interrupt source registers are in APBS, at offset 0x108000 within BAR1.
 * SET and CLEAR are separate write locations, so changing one source does not
 * require reading and rewriting a register that other sources also use.
 * Register layout follows Raspberry Pi Linux drivers/mfd/rp1.c (rpi-6.18.y).
 * 
 */

#include "interrupt.h"
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
    // unmasks a vector or returns to code that depends on the change.
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

    // The controller needs a valid register mapping, all parent vectors, and
    // callbacks for stopping and delivering interrupts.
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
    
    // Leave every RP1 source disabled. A write failure keeps parent vectors
    // masked and the mapping owned, allowing Destroy to retry cleanup.
    for (source = 0; source < FDT_RP1_INTERRUPT_COUNT; source++) {
        status = __Rp1InterruptWrite(controller, source, RP1_IRQ_CLEAR,
            RP1_IRQ_ENABLE | RP1_IRQ_IACK_ENABLE);
        if (status != OS_EOK) {
            // Parent vectors remain masked. Retain ownership so Destroy can retry.
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

    // Choose the trigger mode before enabling the source. This controller
    // supports rising-edge and high-level interrupts; changing an active source
    // could lose or misreport an interrupt.
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

    // Enable only after a trigger mode has been selected, so RP1 never delivers
    // a source with unknown settings.
    if (controller == NULL || controller->Registers == NULL || source >= FDT_RP1_INTERRUPT_COUNT) {
        return OS_EINVALPARAMS;
    }
    if (!controller->Ready || !controller->Types[source] || controller->Enabled[source]) {
        return OS_EINVALPARAMS;
    }
    
    // Turn on the RP1 source first. Unmask its parent vector only after this
    // change reaches RP1, or the parent could deliver too early.
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
    // Stop new parent deliveries and wait for a running handler before changing
    // the RP1 source or its saved state.
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
    // Ignore vectors received during setup or after their source was disabled;
    // they must not call a child handler that is no longer ready.
    if (controller == NULL || controller->Registers == NULL || source >= FDT_RP1_INTERRUPT_COUNT) {
        return OS_EINVALPARAMS;
    }
    if (!controller->Ready || !controller->Enabled[source]) {
        return OS_ENOENT;
    }
    
    controller->Handler(controller->Context, source);
    if (controller->Types[source] == 4) {
        // A level stays asserted until the device clears its cause. Acknowledge
        // it only after the child handler has had a chance to do that.
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

    // Keep retryable state if any source cannot be shut down. Do not release the
    // register mapping until every parent vector is quiet.
    if (controller == NULL) {
        return OS_EINVALPARAMS;
    }
    if (controller->Registers == NULL) {
        return OS_EOK;
    }
    
    // Try every source even if one fails, so teardown can make as much progress
    // as possible before returning an error.
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
