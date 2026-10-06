#include "bcm.h"
#include <string.h>
#include <time.h>

#define BCM_OUTBOUND_TARGET      0x400C
#define BCM_LINK_STATUS          0x4068
#define BCM_OUTBOUND_BASE_LIMIT  0x4070
#define BCM_OUTBOUND_BASE_HIGH   0x4080
#define BCM_LINK_UP              0x30
#define BCM_ROOT_PORT            0x80
#define BCM_MEGABYTE             0x100000ULL

oserr_t
BcmPciDelay(
        _In_ long milliseconds)
{
    struct timespec interval = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (milliseconds % 1000) * 1000000
    };
    return thrd_sleep(&interval, NULL) == thrd_success ? OS_EOK : OS_EUNKNOWN;
}

oserr_t
BcmPciMapRegisters(
        _Out_ DeviceIo_t* io,
        _In_ uint64_t base,
        _In_ uint64_t length)
{
    oserr_t status;

    if (length == 0 || base > UINTPTR_MAX || length > SIZE_MAX ||
        length - 1 > UINTPTR_MAX - base) {
        return OS_ENOTSUPPORTED;
    }
    status = CreateDeviceMemoryIo(io, (uintptr_t)base, (size_t)length);
    if (status != OS_EOK) {
        return status;
    }
    status = AcquireDeviceIo(io);
    if (status != OS_EOK) {
        DestroyDeviceIo(io);
    }
    return status;
}

oserr_t
BcmPciRescal(
        _In_ const struct FdtPciDependencies* dependencies,
        _In_ int reuseCompleted)
{
    DeviceIo_t reset;
    oserr_t status;
    uint32_t value;
    unsigned int attempt;

    if (dependencies->ResetLength == 0) {
        return OS_EOK;
    }
    status = BcmPciMapRegisters(&reset, dependencies->ResetBase, dependencies->ResetLength);
    if (status != OS_EOK) {
        return status;
    }
    // Pi 5 ports share calibration. Discovery initializes them sequentially;
    // a completed calibration must not be restarted under a live sibling.
    value = (uint32_t)ReadDeviceIo(&reset, 8, 4);
    if (reuseCompleted && value != UINT32_MAX && (value & 1)) {
        goto cleanup;
    }
    value = (uint32_t)ReadDeviceIo(&reset, 0, 4);
    status = WriteDeviceIo(&reset, 0, value | 1, 4);
    if (status == OS_EOK && !(ReadDeviceIo(&reset, 0, 4) & 1)) {
        status = OS_EUNKNOWN;
    }
    for (attempt = 0; status == OS_EOK && attempt < 10; attempt++) {
        value = (uint32_t)ReadDeviceIo(&reset, 8, 4);
        if (value != UINT32_MAX && (value & 1)) {
            break;
        }
        status = BcmPciDelay(1);
    }
    if (status == OS_EOK && attempt == 10) {
        status = OS_EUNKNOWN;
    }
    value = (uint32_t)ReadDeviceIo(&reset, 0, 4);
    if (WriteDeviceIo(&reset, 0, value & ~1U, 4) != OS_EOK && status == OS_EOK) {
        status = OS_EUNKNOWN;
    }
cleanup:
    ReleaseDeviceIo(&reset);
    DestroyDeviceIo(&reset);
    return status;
}

oserr_t
BcmPciUpdate(
        _In_ PciHost_t* bus,
        _In_ size_t reg,
        _In_ uint32_t mask,
        _In_ uint32_t value)
{
    uint32_t previous = (uint32_t)ReadDeviceIo(&bus->IoSpace, reg, 4);

    return WriteDeviceIo(&bus->IoSpace, reg, (previous & ~mask) | value, 4);
}

int
BcmPciLinkUp(
        _In_ PciHost_t* bus)
{
    uint32_t status = (uint32_t)ReadDeviceIo(&bus->IoSpace, BCM_LINK_STATUS, 4);

    return status != UINT32_MAX &&
            (status & (BCM_LINK_UP | BCM_ROOT_PORT)) == (BCM_LINK_UP | BCM_ROOT_PORT);
}

static void
__BcmDestroy(
        _InOut_ PciHost_t* bus)
{
    BcmPciDestroy(bus, bus->OpContext);
}

static int
__BcmConfigOffset(
        _In_ PciHost_t* bus,
        _In_ unsigned int number,
        _In_ unsigned int slot,
        _In_ unsigned int function,
        _In_ size_t reg,
        _In_ size_t width,
        _Out_ size_t* offset)
{
    struct BcmPciHost* controller = bus->OpContext;

    if (!controller->Ready || number < (unsigned int)bus->Identification.BusStart ||
        number > (unsigned int)bus->Identification.BusEnd || slot >= 32 || function >= 8 ||
        (width != 1 && width != 2 && width != 4) || reg >= 4096 ||
        width > 4096 - reg || (reg & (width - 1)) != 0) {
        return 0;
    }
    if (number == (unsigned int)bus->Identification.BusStart) {
        if (slot != 0 || function != 0) {
            return 0;
        }
        *offset = reg;
        return 1;
    }
    if ((number == (unsigned int)bus->Identification.BusStart + 1 && slot != 0) || !BcmPciLinkUp(bus)) {
        return 0;
    }
    if (WriteDeviceIo(&bus->IoSpace, controller->Variant->ConfigIndex,
            (number << 20) | (slot << 15) | (function << 12), 4) != OS_EOK) {
        return 0;
    }
    (void)ReadDeviceIo(&bus->IoSpace, controller->Variant->ConfigIndex, 4);
    if (!BcmPciLinkUp(bus)) {
        return 0;
    }
    *offset = controller->Variant->ConfigData + reg;
    return 1;
}

static size_t
__BcmRead(
        _In_ PciHost_t* bus,
        _In_ unsigned int number,
        _In_ unsigned int slot,
        _In_ unsigned int function,
        _In_ size_t reg,
        _In_ size_t width)
{
    struct BcmPciHost* controller = bus->OpContext;
    size_t offset;
    size_t value = (size_t)-1;

    if (mtx_lock(&controller->ConfigLock) != thrd_success) {
        return value;
    }
    if (__BcmConfigOffset(bus, number, slot, function, reg, width, &offset)) {
        value = ReadDeviceIo(&bus->IoSpace, offset, width);
    }
    mtx_unlock(&controller->ConfigLock);
    return value;
}

static void
__BcmWrite(
        _In_ PciHost_t* bus,
        _In_ unsigned int number,
        _In_ unsigned int slot,
        _In_ unsigned int function,
        _In_ size_t reg,
        _In_ size_t value,
        _In_ size_t width)
{
    struct BcmPciHost* controller = bus->OpContext;
    size_t offset;

    if (mtx_lock(&controller->ConfigLock) != thrd_success) {
        return;
    }
    if (__BcmConfigOffset(bus, number, slot, function, reg, width, &offset)) {
        (void)WriteDeviceIo(&bus->IoSpace, offset, value, width);
    }
    mtx_unlock(&controller->ConfigLock);
}

static oserr_t
__BcmTranslate(
        _In_ PciHost_t* bus,
        _In_ uint32_t space,
        _In_ uint64_t address,
        _In_ uint64_t length,
        _Out_ uint64_t* physical)
{
    struct BcmPciHost* controller = bus->OpContext;

    return FdtTranslatePciAddress(&controller->Firmware, space, address, length, physical);
}

static oserr_t
__BcmInterrupt(
        _In_ PciHost_t* bus,
        _In_ unsigned int number,
        _In_ unsigned int slot,
        _In_ unsigned int function,
        _In_ unsigned int pin,
        _Out_ int* line,
        _Out_ unsigned int* flags)
{
    struct BcmPciHost* controller = bus->OpContext;

    return FdtResolvePciInterrupt(&controller->Firmware, number, slot, function, pin, line, flags);
}

const struct PciHostOperations PciBcmOperations = {
    .Read = __BcmRead,
    .Write = __BcmWrite,
    .Translate = __BcmTranslate,
    .ResolveInterrupt = __BcmInterrupt,
    .Destroy = __BcmDestroy
};

static oserr_t
__BcmValidateOutbound(
        _In_ const struct FdtPciHost* firmware)
{
    const struct FdtPciWindow* window;
    uint32_t index;
    uint32_t previous;
    uint64_t length;
    uint64_t previousLength;

    if (firmware->WindowCount == 0 || firmware->WindowCount > 4 ||
        firmware->BusStart >= firmware->BusEnd) {
        return OS_ENOTSUPPORTED;
    }
    for (index = 0; index < firmware->WindowCount; index++) {
        window = &firmware->Windows[index];
        if ((window->Space != 2 && window->Space != 3) || window->Length == 0 ||
            ((window->PhysicalBase | window->BusBase) & (BCM_MEGABYTE - 1)) ||
            window->PhysicalBase >= (1ULL << 40) ||
            window->Length > (1ULL << 40) - window->PhysicalBase ||
            window->Length - 1 > UINT64_MAX - window->BusBase) {
            return OS_ENOTSUPPORTED;
        }
        // Hardware decodes whole MiB blocks, but firmware may reserve bytes at
        // the end (Pi 5 uses 0xfffffffc). Round only our overlap/limit checks;
        // the original description remains the authority for usable bytes.
        length = (window->Length + BCM_MEGABYTE - 1) & ~(BCM_MEGABYTE - 1);
        if (length - 1 > UINT64_MAX - window->BusBase ||
            (!(window->Attributes & FDT_PCI_PREFETCHABLE) &&
             (window->BusBase >= (1ULL << 32) || length > (1ULL << 32) - window->BusBase))) {
            return OS_ENOTSUPPORTED;
        }
        for (previous = 0; previous < index; previous++) {
            previousLength = (firmware->Windows[previous].Length + BCM_MEGABYTE - 1) &
                    ~(BCM_MEGABYTE - 1);
            if ((window->PhysicalBase <= firmware->Windows[previous].PhysicalBase + previousLength - 1 &&
                firmware->Windows[previous].PhysicalBase <= window->PhysicalBase + length - 1) ||
                (window->BusBase <= firmware->Windows[previous].BusBase + previousLength - 1 &&
                firmware->Windows[previous].BusBase <= window->BusBase + length - 1)) {
                return OS_ENOTSUPPORTED;
            }
        }
    }
    return OS_EOK;
}

static oserr_t
__BcmBridgeWindow(
        _In_ PciHost_t* bus,
        _In_ const struct FdtPciHost* firmware)
{
    uint64_t base[2] = { UINT64_MAX, UINT64_MAX };
    uint64_t limit[2] = { 0, 0 };
    uint64_t end;
    uint32_t index;
    unsigned int kind;
    uint32_t memory = 0x0000FFF0;
    uint32_t prefetch = 0x0000FFF0;

    for (index = 0; index < firmware->WindowCount; index++) {
        kind = (firmware->Windows[index].Attributes & FDT_PCI_PREFETCHABLE) != 0;
        if (firmware->Windows[index].BusBase < base[kind]) {
            base[kind] = firmware->Windows[index].BusBase;
        }
        end = firmware->Windows[index].BusBase + firmware->Windows[index].Length - 1;
        if (end > limit[kind]) {
            limit[kind] = end;
        }
    }
    if (base[0] != UINT64_MAX) {
        memory = ((uint32_t)(base[0] >> 16) & 0xFFF0) | ((uint32_t)limit[0] & 0xFFF00000);
    }
    if (base[1] != UINT64_MAX) {
        // Type 1 in both fields selects the bridge's 64-bit prefetch window.
        prefetch = ((uint32_t)(base[1] >> 16) & 0xFFF0) |
                ((uint32_t)limit[1] & 0xFFF00000) | 0x00010001;
    }
    if (WriteDeviceIo(&bus->IoSpace, 0x20, memory, 4) != OS_EOK ||
        WriteDeviceIo(&bus->IoSpace, 0x24, prefetch, 4) != OS_EOK ||
        WriteDeviceIo(&bus->IoSpace, 0x28, base[1] == UINT64_MAX ? 0 : base[1] >> 32, 4) != OS_EOK ||
        WriteDeviceIo(&bus->IoSpace, 0x2C, limit[1] >> 32, 4) != OS_EOK ||
        WriteDeviceIo(&bus->IoSpace, 0x1C, 0x00F0, 2) != OS_EOK ||
        WriteDeviceIo(&bus->IoSpace, 0x04,
            (ReadDeviceIo(&bus->IoSpace, 0x04, 2) & ~PCI_COMMAND_PORTIO) |
                PCI_COMMAND_MMIO | PCI_COMMAND_BUSMASTER, 2) != OS_EOK) {
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

static oserr_t
__BcmWindows(
        _In_ PciHost_t* bus,
        _In_ const struct FdtPciHost* firmware)
{
    const struct FdtPciWindow* window;
    uint64_t base;
    uint64_t limit;
    uint32_t index;

    for (index = 0; index < 4; index++) {
        if (WriteDeviceIo(&bus->IoSpace, BCM_OUTBOUND_BASE_LIMIT + index * 4, 0xFFF0, 4) != OS_EOK ||
            WriteDeviceIo(&bus->IoSpace, BCM_OUTBOUND_BASE_HIGH + index * 8, 0, 4) != OS_EOK ||
            WriteDeviceIo(&bus->IoSpace, BCM_OUTBOUND_BASE_HIGH + index * 8 + 4, 0, 4) != OS_EOK) {
            return OS_EUNKNOWN;
        }
    }
    for (index = 0; index < firmware->WindowCount; index++) {
        window = &firmware->Windows[index];
        base = window->PhysicalBase >> 20;
        limit = (window->PhysicalBase + window->Length - 1) >> 20;
        if (WriteDeviceIo(&bus->IoSpace, BCM_OUTBOUND_TARGET + index * 8,
                (uint32_t)window->BusBase, 4) != OS_EOK ||
            WriteDeviceIo(&bus->IoSpace, BCM_OUTBOUND_TARGET + index * 8 + 4,
                (uint32_t)(window->BusBase >> 32), 4) != OS_EOK ||
            WriteDeviceIo(&bus->IoSpace, BCM_OUTBOUND_BASE_HIGH + index * 8,
                (uint32_t)(base >> 12), 4) != OS_EOK ||
            WriteDeviceIo(&bus->IoSpace, BCM_OUTBOUND_BASE_HIGH + index * 8 + 4,
                (uint32_t)(limit >> 12), 4) != OS_EOK ||
            WriteDeviceIo(&bus->IoSpace, BCM_OUTBOUND_BASE_LIMIT + index * 4,
                ((uint32_t)base & 0xFFF) << 4 | ((uint32_t)limit & 0xFFF) << 20, 4) != OS_EOK) {
            return OS_EUNKNOWN;
        }
    }
    return OS_EOK;
}

const struct BcmPciVariant*
BcmPciGetVariant(
        _In_ enum FdtPciHostType type)
{
    switch (type) {
        case FdtPciHostBcm2711:
            return &Bcm2711PciVariant;
        case FdtPciHostBcm2712:
            return &Bcm2712PciVariant;
        default:
            return NULL;
    }
}

oserr_t
BcmPciInitialize(
        _InOut_ PciHost_t* bus,
        _Out_ struct BcmPciHost* controller,
        _In_ const struct FdtPciHost* firmware)
{
    unsigned int dmaOrder;
    unsigned int attempt;
    oserr_t status;
    const struct BcmPciVariant* variant;

    if (bus == NULL || controller == NULL || firmware == NULL) {
        return OS_EINVALPARAMS;
    }
    variant = BcmPciGetVariant(firmware->Type);
    if (variant == NULL) {
        return OS_ENOTSUPPORTED;
    }
    if (bus->OpContext != NULL || bus->Operations != NULL) {
        return OS_EINVALPARAMS;
    }
    if (bus->IoSpace.Type != DeviceIoMemoryBased ||
        bus->IoSpace.Access.Memory.Length < variant->RegisterLength) {
        return OS_EINVALPARAMS;
    }
    status = variant->Validate(firmware, &dmaOrder);
    if (status != OS_EOK) {
        return status;
    }
    status = __BcmValidateOutbound(firmware);
    if (status != OS_EOK) {
        return status;
    }
    memset(controller, 0, sizeof(*controller));
    controller->Firmware = *firmware;
    controller->Variant = variant;
    if (mtx_init(&controller->ConfigLock, mtx_plain) != thrd_success) {
        return OS_EOOM;
    }
    bus->OpContext = controller;
    bus->DriversBlocked = 1;
    bus->IoResourcePolicy = PciIoResourceMemory;
    bus->IsExtended = 1;
    bus->Identification.BusStart = firmware->BusStart;
    bus->Identification.BusEnd = firmware->BusEnd;
    bus->Identification.Segment = firmware->Segment;
    status = variant->Prepare(bus, firmware);
    if (status != OS_EOK) {
        goto failure;
    }
    status = __BcmWindows(bus, firmware);
    if (status != OS_EOK) {
        goto failure;
    }
    status = variant->ProgramInbound(bus, firmware, dmaOrder);
    if (status != OS_EOK) {
        goto failure;
    }
    status = __BcmBridgeWindow(bus, firmware);
    if (status != OS_EOK) {
        goto failure;
    }
    status = variant->Start(bus, firmware);
    if (status != OS_EOK) {
        goto failure;
    }
    for (attempt = 0; attempt <= 20; attempt++) {
        if (BcmPciLinkUp(bus)) {
            controller->Ready = 1;
            bus->Operations = &PciBcmOperations;
            bus->Firmware = &controller->Firmware;
            return OS_EOK;
        }
        if (attempt == 20) {
            break;
        }
        status = BcmPciDelay(5);
        if (status != OS_EOK) {
            goto failure;
        }
    }
    status = OS_EUNKNOWN;
failure:
    variant->Stop(bus);
    bus->OpContext = NULL;
    bus->Firmware = NULL;
    bus->Operations = NULL;
    mtx_destroy(&controller->ConfigLock);
    return status;
}

void
BcmPciDestroy(
        _InOut_ PciHost_t* bus,
        _InOut_ struct BcmPciHost* controller)
{
    if (bus == NULL || controller == NULL || bus->OpContext != controller) {
        return;
    }
    mtx_lock(&controller->ConfigLock);
    controller->Ready = 0;
    controller->Variant->Stop(bus);
    bus->Operations = NULL;
    bus->OpContext = NULL;
    bus->Firmware = NULL;
    mtx_unlock(&controller->ConfigLock);
    mtx_destroy(&controller->ConfigLock);
}

oserr_t
BcmPciDmaAddress(
        _In_ const struct BcmPciHost* controller,
        _In_ uint64_t physical,
        _In_ uint64_t length,
        _Out_ uint64_t* address)
{
    const struct FdtPciWindow* window;
    uint64_t displacement;
    uint32_t index;

    if (controller == NULL || address == NULL || !controller->Ready ||
        length == 0 || length - 1 > UINT64_MAX - physical) {
        return OS_EINVALPARAMS;
    }
    for (index = 0; index < controller->Firmware.DmaWindowCount; index++) {
        window = &controller->Firmware.DmaWindows[index];
        if (window->Kind != FdtDmaWindowRam || physical < window->PhysicalBase) {
            continue;
        }
        displacement = physical - window->PhysicalBase;
        if (displacement >= window->Length || length > window->Length - displacement ||
            displacement > UINT64_MAX - window->BusBase ||
            length - 1 > UINT64_MAX - (window->BusBase + displacement)) {
            continue;
        }
        *address = window->BusBase + displacement;
        return OS_EOK;
    }
    return OS_ENOENT;
}
