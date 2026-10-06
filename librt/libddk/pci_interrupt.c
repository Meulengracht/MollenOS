#include <ddk/busdevice.h>
#include <ddk/interrupt.h>

#define PCI_STATUS_CAPABILITIES_LIST  0x10U
#define PCI_CAPABILITIES_POINTER      0x34U
#define PCI_CAPABILITY_MIN_OFFSET     0x40U
#define PCI_CAPABILITY_MAX_OFFSET     0xFCU
#define PCI_CAPABILITY_MAX_VISITS     48U
#define PCI_CAPABILITY_MSI            0x05U
#define PCI_CAPABILITY_MSIX           0x11U
#define PCI_COMMAND_INTERRUPT_DISABLE (1U << 10)
#define PCI_MSI_CONTROL_ENABLE        (1U << 0)
#define PCI_MSI_CONTROL_MME_MASK      (7U << 4)
#define PCI_MSI_CONTROL_64BIT         (1U << 7)
#define PCI_MSI_CONTROL_MASKABLE      (1U << 8)
#define PCI_MSIX_CONTROL_ENABLE       (1U << 15)
#define PCI_MSIX_CONTROL_FUNCTION_MASK (1U << 14)
#define PCI_MSIX_TABLE_BIR_MASK       0x7U
#define PCI_MSIX_TABLE_OFFSET_MASK    (~0x7U)
#define PCI_MSIX_TABLE_ENTRY_SIZE     16U

static oserr_t
__ReadPciConfig(
    _In_  BusDevice_t* device,
    _In_  uint32_t     offset,
    _In_  size_t       width,
    _Out_ size_t*      valueOut)
{
    *valueOut = 0;
    return IoctlDeviceEx(
        device->Base.Id,
        __DEVICEMANAGER_IOCTL_EXT_READ,
        offset,
        valueOut,
        width
    );
}

static oserr_t
__WritePciConfig(
    _In_ BusDevice_t* device,
    _In_ uint32_t     offset,
    _In_ size_t       width,
    _In_ size_t       value)
{
    return IoctlDeviceEx(
        device->Base.Id,
        __DEVICEMANAGER_IOCTL_EXT_WRITE,
        offset,
        &value,
        width
    );
}

static oserr_t
__FindPciCapability(
    _In_  BusDevice_t* device,
    _In_  uint8_t      capabilityId,
    _Out_ uint8_t*     capabilityOffsetOut)
{
    size_t  status;
    size_t  pointer;
    size_t  header;
    oserr_t oserr;

    oserr = __ReadPciConfig(device, 0x06, sizeof(uint16_t), &status);
    if (oserr != OS_EOK) {
        return oserr;
    }
    if (!(status & PCI_STATUS_CAPABILITIES_LIST)) {
        return OS_ENOENT;
    }

    oserr = __ReadPciConfig(device, PCI_CAPABILITIES_POINTER, sizeof(uint8_t), &pointer);
    if (oserr != OS_EOK) {
        return oserr;
    }

    for (uint32_t visits = 0; pointer != 0 && visits < PCI_CAPABILITY_MAX_VISITS; visits++) {
        pointer &= ~0x3U;
        if (pointer < PCI_CAPABILITY_MIN_OFFSET || pointer > PCI_CAPABILITY_MAX_OFFSET) {
            return OS_EINVALPARAMS;
        }

        oserr = __ReadPciConfig(device, (uint32_t)pointer, sizeof(uint16_t), &header);
        if (oserr != OS_EOK) {
            return oserr;
        }
        if ((header & 0xFFU) == capabilityId) {
            *capabilityOffsetOut = (uint8_t)pointer;
            return OS_EOK;
        }
        pointer = (header >> 8) & 0xFFU;
    }
    return pointer == 0 ? OS_ENOENT : OS_EINVALPARAMS;
}

static oserr_t
__SetPciInterruptDisable(
    _In_ BusDevice_t* device,
    _In_ int          disable)
{
    size_t  command;
    oserr_t oserr;

    oserr = __ReadPciConfig(device, 0x04, sizeof(uint16_t), &command);
    if (oserr != OS_EOK) {
        return oserr;
    }
    if (disable) {
        command |= PCI_COMMAND_INTERRUPT_DISABLE;
    } else {
        command &= ~PCI_COMMAND_INTERRUPT_DISABLE;
    }
    return __WritePciConfig(device, 0x04, sizeof(uint16_t), command);
}

static oserr_t
__SetPciMsiEnabled(
    _In_ BusDevice_t* device,
    _In_ int          enable)
{
    uint8_t capabilityOffset;
    size_t  control;
    oserr_t oserr;

    oserr = __FindPciCapability(device, PCI_CAPABILITY_MSI, &capabilityOffset);
    if (oserr != OS_EOK) {
        return oserr == OS_ENOENT ? OS_EOK : oserr;
    }
    oserr = __ReadPciConfig(device, capabilityOffset + 2, sizeof(uint16_t), &control);
    if (oserr != OS_EOK) {
        return oserr;
    }
    if (enable) {
        control |= PCI_MSI_CONTROL_ENABLE;
    } else {
        control &= ~PCI_MSI_CONTROL_ENABLE;
    }
    return __WritePciConfig(device, capabilityOffset + 2, sizeof(uint16_t), control);
}

static oserr_t
__SetPciMsixState(
    _In_ BusDevice_t* device,
    _In_ int          enable,
    _In_ int          functionMask)
{
    uint8_t capabilityOffset;
    size_t  control;
    oserr_t oserr;

    oserr = __FindPciCapability(device, PCI_CAPABILITY_MSIX, &capabilityOffset);
    if (oserr != OS_EOK) {
        return oserr == OS_ENOENT ? OS_EOK : oserr;
    }
    oserr = __ReadPciConfig(device, capabilityOffset + 2, sizeof(uint16_t), &control);
    if (oserr != OS_EOK) {
        return oserr;
    }
    if (enable) {
        control |= PCI_MSIX_CONTROL_ENABLE;
    } else {
        control &= ~PCI_MSIX_CONTROL_ENABLE;
    }
    if (functionMask) {
        control |= PCI_MSIX_CONTROL_FUNCTION_MASK;
    } else {
        control &= ~PCI_MSIX_CONTROL_FUNCTION_MASK;
    }
    return __WritePciConfig(device, capabilityOffset + 2, sizeof(uint16_t), control);
}

static oserr_t
__ProgramPciMsi(
    _In_ BusDevice_t*       device,
    _In_ DeviceInterrupt_t* vector)
{
    uint8_t capabilityOffset;
    size_t  control;
    size_t  addressOffset;
    size_t  dataOffset;
    size_t  maskOffset;
    oserr_t oserr;

    oserr = __FindPciCapability(device, PCI_CAPABILITY_MSI, &capabilityOffset);
    if (oserr != OS_EOK) {
        return oserr;
    }
    oserr = __ReadPciConfig(device, capabilityOffset + 2, sizeof(uint16_t), &control);
    if (oserr != OS_EOK) {
        return oserr;
    }
    if (((control & PCI_MSI_CONTROL_64BIT) == 0 && (vector->MsiAddress >> 32) != 0) ||
        vector->MsiValue > UINT16_MAX) {
        return OS_ENOTSUPPORTED;
    }

    control &= ~PCI_MSI_CONTROL_ENABLE;
    oserr = __WritePciConfig(device, capabilityOffset + 2, sizeof(uint16_t), control);
    if (oserr != OS_EOK) {
        return oserr;
    }

    addressOffset = capabilityOffset + 4;
    dataOffset = capabilityOffset + ((control & PCI_MSI_CONTROL_64BIT) ? 12 : 8);
    if (control & PCI_MSI_CONTROL_MASKABLE) {
        maskOffset = dataOffset + 4;
        oserr = __WritePciConfig(device, (uint32_t)maskOffset, sizeof(uint32_t), UINT32_MAX);
        if (oserr != OS_EOK) {
            return oserr;
        }
    }

    oserr = __WritePciConfig(device, (uint32_t)addressOffset, sizeof(uint32_t),
                             (uint32_t)vector->MsiAddress);
    if (oserr != OS_EOK) {
        return oserr;
    }
    if (control & PCI_MSI_CONTROL_64BIT) {
        oserr = __WritePciConfig(device, (uint32_t)(addressOffset + 4), sizeof(uint32_t),
                                 (uint32_t)(vector->MsiAddress >> 32));
        if (oserr != OS_EOK) {
            return oserr;
        }
    }
    oserr = __WritePciConfig(device, (uint32_t)dataOffset, sizeof(uint16_t), vector->MsiValue);
    if (oserr != OS_EOK) {
        return oserr;
    }
    if (control & PCI_MSI_CONTROL_MASKABLE) {
        oserr = __WritePciConfig(device, (uint32_t)maskOffset, sizeof(uint32_t), 0);
        if (oserr != OS_EOK) {
            return oserr;
        }
    }

    oserr = __SetPciInterruptDisable(device, 1);
    if (oserr != OS_EOK) {
        return oserr;
    }
    control &= ~PCI_MSI_CONTROL_MME_MASK;
    control |= PCI_MSI_CONTROL_ENABLE;
    return __WritePciConfig(device, capabilityOffset + 2, sizeof(uint16_t), control);
}

static oserr_t
__ProgramPciMsix(
    _In_ BusDevice_t*       device,
    _In_ DeviceInterrupt_t* vectors,
    _In_ uint32_t           count)
{
    DeviceIo_t* tableIo;
    uint8_t     capabilityOffset;
    uint32_t    tableBir;
    uint32_t    tableOffset;
    uint32_t    tableSize;
    size_t      control;
    size_t      table;
    size_t      entryOffset;
    size_t      requiredLength;
    oserr_t     oserr;

    oserr = __FindPciCapability(device, PCI_CAPABILITY_MSIX, &capabilityOffset);
    if (oserr != OS_EOK) {
        return oserr;
    }
    oserr = __ReadPciConfig(device, capabilityOffset + 2, sizeof(uint16_t), &control);
    if (oserr != OS_EOK) {
        return oserr;
    }
    tableSize = (uint32_t)(control & 0x7FFU) + 1U;
    if (count > tableSize) {
        return OS_ENOTSUPPORTED;
    }
    oserr = __ReadPciConfig(device, capabilityOffset + 4, sizeof(uint32_t), &table);
    if (oserr != OS_EOK) {
        return oserr;
    }

    tableBir = (uint32_t)table & PCI_MSIX_TABLE_BIR_MASK;
    tableOffset = (uint32_t)table & PCI_MSIX_TABLE_OFFSET_MASK;
    if (tableBir >= __DEVICEMANAGER_MAX_IOSPACES) {
        return OS_EINVALPARAMS;
    }
    tableIo = &device->IoSpaces[tableBir];
    if (tableIo->Type != DeviceIoMemoryBased) {
        return OS_EINVALPARAMS;
    }
    requiredLength = (size_t)tableSize * PCI_MSIX_TABLE_ENTRY_SIZE;
    if (tableOffset > tableIo->Access.Memory.Length ||
        requiredLength > tableIo->Access.Memory.Length - tableOffset) {
        return OS_EINVALPARAMS;
    }

    control &= ~PCI_MSIX_CONTROL_ENABLE;
    control |= PCI_MSIX_CONTROL_FUNCTION_MASK;
    oserr = __WritePciConfig(device, capabilityOffset + 2, sizeof(uint16_t), control);
    if (oserr != OS_EOK) {
        return oserr;
    }

    for (uint32_t i = 0; i < tableSize; i++) {
        entryOffset = tableOffset + (size_t)i * PCI_MSIX_TABLE_ENTRY_SIZE;
        oserr = WriteDeviceIo(tableIo, entryOffset + 12, 1, sizeof(uint32_t));
        if (oserr != OS_EOK) {
            return oserr;
        }
    }

    for (uint32_t i = 0; i < count; i++) {
        entryOffset = tableOffset + (size_t)i * PCI_MSIX_TABLE_ENTRY_SIZE;
        if (vectors[i].MsiValue > UINT32_MAX) {
            return OS_ENOTSUPPORTED;
        }
        oserr = WriteDeviceIo(tableIo, entryOffset, (uint32_t)vectors[i].MsiAddress, sizeof(uint32_t));
        if (oserr != OS_EOK) {
            return oserr;
        }
        oserr = WriteDeviceIo(tableIo, entryOffset + 4, (uint32_t)(vectors[i].MsiAddress >> 32), sizeof(uint32_t));
        if (oserr != OS_EOK) {
            return oserr;
        }
        oserr = WriteDeviceIo(tableIo, entryOffset + 8, vectors[i].MsiValue, sizeof(uint32_t));
        if (oserr != OS_EOK) {
            return oserr;
        }
        wmb();
        oserr = WriteDeviceIo(tableIo, entryOffset + 12, 0, sizeof(uint32_t));
        if (oserr != OS_EOK) {
            return oserr;
        }
    }

    oserr = __SetPciInterruptDisable(device, 1);
    if (oserr != OS_EOK) {
        return oserr;
    }
    wmb();
    control |= PCI_MSIX_CONTROL_ENABLE;
    control &= ~PCI_MSIX_CONTROL_FUNCTION_MASK;
    return __WritePciConfig(device, capabilityOffset + 2, sizeof(uint16_t), control);
}

oserr_t
DeviceInterruptProgram(
    _In_ BusDevice_t*       device,
    _In_ DeviceInterrupt_t* vectors,
    _In_ uint32_t           count,
    _In_ uint32_t           strategy)
{
    oserr_t oserr;

    if (device == NULL || !device->IsPci || vectors == NULL || count == 0 ||
        count > INTERRUPT_MAXVECTORS) {
        return OS_EINVALPARAMS;
    }
    if ((strategy == INTERRUPT_STRATEGY_INTx || strategy == INTERRUPT_STRATEGY_MSI) && count != 1) {
        return OS_EINVALPARAMS;
    }
    if (strategy != INTERRUPT_STRATEGY_INTx && strategy != INTERRUPT_STRATEGY_MSI &&
        strategy != INTERRUPT_STRATEGY_MSIX) {
        return OS_EINVALPARAMS;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (!vectors[i].IsPci || vectors[i].DeviceId != device->Base.Id ||
            vectors[i].Segment != device->Segment ||
            vectors[i].Bus != device->Bus || vectors[i].Slot != device->Slot ||
            vectors[i].Function != device->Function) {
            return OS_EINVALPARAMS;
        }
    }

    oserr = __SetPciInterruptDisable(device, 1);
    if (oserr != OS_EOK) {
        return oserr;
    }
    oserr = __SetPciMsiEnabled(device, 0);
    if (oserr != OS_EOK) {
        return oserr;
    }
    oserr = __SetPciMsixState(device, 0, 1);
    if (oserr != OS_EOK) {
        return oserr;
    }

    switch (strategy) {
        case INTERRUPT_STRATEGY_INTx:
            return __SetPciInterruptDisable(device, 0);
        case INTERRUPT_STRATEGY_MSI:
            return __ProgramPciMsi(device, &vectors[0]);
        case INTERRUPT_STRATEGY_MSIX:
            return __ProgramPciMsix(device, vectors, count);
        default:
            return OS_EINVALPARAMS;
    }
}

oserr_t
DeviceInterruptUnprogram(
    _In_ BusDevice_t* device)
{
    oserr_t oserr;

    if (device == NULL || !device->IsPci) {
        return OS_EINVALPARAMS;
    }
    oserr = __SetPciInterruptDisable(device, 1);
    if (oserr != OS_EOK) {
        return oserr;
    }
    oserr = __SetPciMsiEnabled(device, 0);
    if (oserr != OS_EOK) {
        return oserr;
    }
    return __SetPciMsixState(device, 0, 1);
}