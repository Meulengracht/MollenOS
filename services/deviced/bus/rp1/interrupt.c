/**
 * RP1's APBS interrupt source controls, relative to BAR1 + 0x108000.
 * Register definitions: Raspberry Pi Linux drivers/mfd/rp1.c (rpi-6.18.y).
 * SET/CLEAR aliases avoid read-modify-write of shared device state.
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
    _In_ unsigned int source,
    _In_ unsigned int alias,
    _In_ uint32_t bits)
{
    oserr_t status;

    status = WriteDeviceIo(controller->Registers, alias + RP1_IRQ_CFG(source), bits, 4);
    if (status == OS_EOK) {
        // Drain posted PCI writes before unmasking or returning to the parent.
        (void)ReadDeviceIo(controller->Registers, RP1_IRQ_CFG(source), 4);
    }
    return status;
}

oserr_t
Rp1InterruptInitialize(
    _Out_ struct Rp1InterruptController* controller,
    _In_ DeviceIo_t* registers,
    _In_ const struct Rp1InterruptParent* parent,
    _In_ Rp1InterruptHandler handler,
    _In_ void* context)
{
    unsigned int source;
    oserr_t status;

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
    for (source = 0; source < FDT_RP1_INTERRUPT_COUNT; source++) {
        parent->MaskAndSynchronize(parent->Context, source);
    }
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
    _In_ unsigned int source,
    _In_ unsigned int type)
{
    oserr_t status;

    if (controller == NULL || controller->Registers == NULL || source >= FDT_RP1_INTERRUPT_COUNT) {
        return OS_EINVALPARAMS;
    }
    if (!controller->Ready || (type != 1 && type != 4) || controller->Enabled[source]) {
        return OS_EINVALPARAMS;
    }
    status = __Rp1InterruptWrite(controller, source,
        type == 4 ? RP1_IRQ_SET : RP1_IRQ_CLEAR, RP1_IRQ_IACK_ENABLE);
    if (status == OS_EOK) {
        controller->Types[source] = (uint8_t)type;
    }
    return status;
}

oserr_t
Rp1InterruptEnable(
    _InOut_ struct Rp1InterruptController* controller,
    _In_ unsigned int source)
{
    oserr_t status;

    if (controller == NULL || controller->Registers == NULL || source >= FDT_RP1_INTERRUPT_COUNT) {
        return OS_EINVALPARAMS;
    }
    if (!controller->Ready || !controller->Types[source] || controller->Enabled[source]) {
        return OS_EINVALPARAMS;
    }
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
    _In_ unsigned int source)
{
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
    if (controller == NULL || controller->Registers == NULL || source >= FDT_RP1_INTERRUPT_COUNT) {
        return OS_EINVALPARAMS;
    }
    if (!controller->Ready || !controller->Enabled[source]) {
        return OS_ENOENT;
    }
    controller->Handler(controller->Context, source);
    if (controller->Types[source] == 4) {
        return __Rp1InterruptWrite(controller, source, RP1_IRQ_SET, RP1_IRQ_IACK);
    }
    return OS_EOK;
}

oserr_t
Rp1InterruptDestroy(
    _InOut_ struct Rp1InterruptController* controller)
{
    unsigned int source;
    oserr_t status;
    oserr_t result = OS_EOK;

    if (controller == NULL) {
        return OS_EINVALPARAMS;
    }
    if (controller->Registers == NULL) {
        return OS_EOK;
    }
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
