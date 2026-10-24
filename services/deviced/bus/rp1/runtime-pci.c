/**
 * Copyright, Philip Meulengracht
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
 */

//#define __TRACE

#include <bus/pci/config.h>
#include <bus/pci/device.h>
#include <bus/pci/host-private.h>
#include <internal/_syscalls.h>
#include <ddk/utils.h>
#include <string.h>

#include "runtime-private.h"

#define RP1_IRQ_CFG(source) (0x8U + 4U * (source))
#define RP1_IRQ_CLEAR        0xc00U
#define RP1_IRQ_ENABLE       1U
#define RP1_IRQ_IACK_ENABLE (1U << 3)

static uint32_t
__Rp1ConfigRead(
    _In_ void*  context,
    _In_ size_t offset,
    _In_ size_t width)
{
    struct Rp1Runtime* runtime = context;
    return PciDeviceRead(runtime->Bus->Parent, offset, width);
}

static void
__Rp1ConfigWrite(
    _In_ void*    context,
    _In_ size_t   offset,
    _In_ uint32_t value,
    _In_ size_t   width)
{
    struct Rp1Runtime* runtime = context;
    PciDeviceWrite(runtime->Bus->Parent, offset, value, width);
}

static oserr_t
__Rp1MapIo(
    _InOut_ DeviceIo_t* io,
    _In_    uint64_t    address,
    _In_    size_t      length)
{
    oserr_t status;

    TRACE("__Rp1MapIo(address=0x%llx, length=%zu)", (unsigned long long)address, length);
    
    // Check the whole address range before converting its start address.
    // The end of the range must fit too.
    if (address > UINTPTR_MAX || length - 1 > UINTPTR_MAX - address) {
        WARNING("__Rp1MapIo: unsupported address 0x%llx, length %zu", (unsigned long long)address, length);
        return OS_ENOTSUPPORTED;
    }

    // Keep the registered range if mapping it fails. Stop can then remove the
    // registration later.
    if (io->Id == UUID_INVALID) {
        status = CreateDeviceMemoryIo(io, (uintptr_t)address, length);
        if (status != OS_EOK) {
            ERROR("__Rp1MapIo: CreateDeviceMemoryIo address 0x%llx, length %zu failed: %u",
                  (unsigned long long)address, length, status);
            return status;
        }
    }
    
    status = AcquireDeviceIo(io);
    if (status != OS_EOK) {
        ERROR("__Rp1MapIo: AcquireDeviceIo id %u failed: %u; registered range retained", io->Id, status);
    }
    return status;
}

static oserr_t
__Rp1UnmapIo(
    _InOut_ DeviceIo_t* io)
{
    oserr_t status;

    TRACE("__Rp1UnmapIo(id=%u, mapping=0x%" PRIxIN ")", io->Id, io->Access.Memory.VirtualBase);
    // Remove the mapping before its registration. Keep track of both if either
    // step fails so Stop can try again.
    if (io->Access.Memory.VirtualBase != 0) {
        status = ReleaseDeviceIo(io);
        if (status != OS_EOK) {
            ERROR("__Rp1UnmapIo: ReleaseDeviceIo id %u failed: %u", io->Id, status);
            return status;
        }
        io->Access.Memory.VirtualBase = 0;
    }

    if (io->Id != UUID_INVALID) {
        status = DestroyDeviceIo(io);
        if (status != OS_EOK) {
            ERROR("__Rp1UnmapIo: DestroyDeviceIo id %u failed: %u", io->Id, status);
            return status;
        }
    }
    memset(io, 0, sizeof(*io));
    return OS_EOK;
}

static oserr_t
__Rp1TableMap(
    _In_  void*                       context,
    _In_  const struct PciMsixRegion* region,
    _Out_ void**                      mapping)
{
    struct Rp1Runtime* runtime = context;
    oserr_t status;

    status = __Rp1MapIo(&runtime->Pci.Table, region->Address, region->Length);
    if (status != OS_EOK) {
        ERROR("__Rp1TableMap: __Rp1MapIo table failed: %u", status);
        return status;
    }
    *mapping = &runtime->Pci.Table;
    return OS_EOK;
}

static oserr_t
__Rp1TableUnmap(
    _In_ void*  context,
    _In_ void*  mapping,
    _In_ size_t length)
{
    (void)context;
    (void)length;
    return __Rp1UnmapIo(mapping);
}

static uint32_t
__Rp1TableRead(
    _In_ void*  mapping,
    _In_ size_t offset)
{
    return (uint32_t)ReadDeviceIo(mapping, offset, 4);
}

static void
__Rp1TableWrite(
    _In_ void*    mapping,
    _In_ size_t   offset,
    _In_ uint32_t value)
{
    oserr_t status;

    // MSI-X writes do not report errors. The caller checks the table by reading
    // it back.
    status = WriteDeviceIo(mapping, offset, value, 4);
    if (status != OS_EOK) {
        ERROR("__Rp1TableWrite: WriteDeviceIo offset 0x%zx, value 0x%x failed: %u", offset, value, status);
    }
    // This callback cannot report errors. The MSI-X code checks the value by
    // reading it back.
    (void)ReadDeviceIo(mapping, offset, 4);
}

oserr_t
Rp1PciMessagesStart(
    _InOut_ struct Rp1Runtime* runtime)
{
    DeviceInterruptMessage_t message;
    struct PciMsixLayout     layout;
    uint16_t                 originalCommand;
    uint16_t                 command;
    uint32_t                 actualCommand;
    oserr_t                  status;

    struct PciInterruptRequest request = {
        .Operation = PciInterruptReserve,
        .Strategy = INTERRUPT_STRATEGY_MSIX,
        .Count = FDT_RP1_INTERRUPT_COUNT
    };

    TRACE("Rp1PciMessagesStart(bus=%p, allocation=%u)", runtime->Bus, runtime->Pci.Allocation);
    runtime->Pci.Access = (struct MsixDeviceOperations){
        .Config = {
            runtime,
            __Rp1ConfigRead,
            __Rp1ConfigWrite
        },
        .Bars = runtime->Bus->Bars,
        .Map = __Rp1TableMap,
        .Unmap = __Rp1TableUnmap,
        .Read = __Rp1TableRead,
        .Write = __Rp1TableWrite
    };
    
    // Check the device's MSI-X table location and size before changing PCI
    // settings or reserving interrupt messages.
    status = PciMsixProbe(&runtime->Pci.Access, &layout);
    if (status != OS_EOK) {
        ERROR("Rp1PciMessagesStart: PciMsixProbe failed: %u", status);
        return status;
    }
    
    if (layout.Count < FDT_RP1_INTERRUPT_COUNT) {
        WARNING("Rp1PciMessagesStart: MSI-X capacity %u is below required %u", layout.Count, FDT_RP1_INTERRUPT_COUNT);
        return OS_ENOTSUPPORTED;
    }
    
    // Keep device interrupts and bus mastering off while setting up the shared
    // messages and RP1 registers.
    originalCommand = (uint16_t)__Rp1ConfigRead(runtime, 4, 2);
    runtime->Pci.CommandTouched = 1;
    command = (originalCommand & ~PCI_COMMAND_BUSMASTER) | PCI_COMMAND_INTDISABLE;
    __Rp1ConfigWrite(runtime, 4, command, 2);
    
    actualCommand = __Rp1ConfigRead(runtime, 4, 2);
    if (actualCommand != command) {
        ERROR("Rp1PciMessagesStart: quiescing PCI command readback 0x%x, expected 0x%x", actualCommand, command);
        return OS_EUNKNOWN;
    }
    
    request.Allocation = runtime->Pci.Allocation;
    // Reserve all MSI-X entries together. Use one disabled message for entries
    // that are not assigned to a child.
    status = PciMsixControl(&runtime->Pci.Access, &runtime->Pci.State, &request);
    if (status != OS_EOK) {
        ERROR("Rp1PciMessagesStart: PciMsixControl Reserve allocation %u failed: %u", request.Allocation, status);
        return status;
    }

    status = __Rp1MapIo(
        &runtime->Pci.Apbs,
        runtime->Bus->Bars[1].CpuAddress + RP1_PCIE_APBS_OFFSET,
        RP1_PCIE_APBS_LENGTH
    );
    if (status != OS_EOK) {
        ERROR("Rp1PciMessagesStart: __Rp1MapIo APBS failed: %u", status);
        return status;
    }
    
    // Keep each RP1 interrupt disabled while pointing its MSI-X entry at the
    // shared disabled message. This prevents it from firing during setup.
    // All unused entries use the same disabled message, so read it only once.
    status = Syscall_InterruptGetMessage(runtime->Pci.Allocation, 0, &message);
    if (status != OS_EOK) {
        ERROR("Rp1PciMessagesStart: Syscall_InterruptGetMessage allocation %u failed: %u",
              runtime->Pci.Allocation, status);
        return status;
    }
    
    // Disable every RP1 interrupt before allowing MSI-X messages to be sent.
    for (unsigned int i = 0; i < FDT_RP1_INTERRUPT_COUNT; i++) {
        status = WriteDeviceIo(
            &runtime->Pci.Apbs,
            RP1_IRQ_CLEAR + RP1_IRQ_CFG(i),
            RP1_IRQ_ENABLE | RP1_IRQ_IACK_ENABLE,
            4
        );
        if (status != OS_EOK) {
            ERROR("Rp1PciMessagesStart: WriteDeviceIo source %u, APBS offset 0x%x failed: %u",
                  i, RP1_IRQ_CLEAR + RP1_IRQ_CFG(i), status);
            return status;
        }
        
        (void)ReadDeviceIo(&runtime->Pci.Apbs, RP1_IRQ_CFG(i), 4);
        request = (struct PciInterruptRequest){
            .Operation = PciInterruptProgram,
            .Allocation = runtime->Pci.Allocation,
            .Strategy = INTERRUPT_STRATEGY_MSIX,
            .Index = i,
            .AddressLow = message.AddressLow,
            .AddressHigh = message.AddressHigh,
            .Data = message.Data
        };
        
        status = PciMsixControl(&runtime->Pci.Access, &runtime->Pci.State, &request);
        if (status != OS_EOK) {
            ERROR("Rp1PciMessagesStart: PciMsixControl Program allocation %u, entry %u failed: %u",
                  request.Allocation, i, status);
            return status;
        }
    }

    request = (struct PciInterruptRequest){
        .Operation = PciInterruptEnable,
        .Allocation = runtime->Pci.Allocation,
        .Strategy = INTERRUPT_STRATEGY_MSIX
    };
    
    status = PciMsixControl(&runtime->Pci.Access, &runtime->Pci.State, &request);
    if (status != OS_EOK) {
        ERROR("Rp1PciMessagesStart: PciMsixControl Enable allocation %u failed: %u", request.Allocation, status);
        return status;
    }
    
    for (unsigned int i = 0; i < FDT_RP1_INTERRUPT_COUNT; i++) {
        status = Rp1SourceSetTableMask(runtime, i, 1);
        if (status != OS_EOK) {
            ERROR("Rp1PciMessagesStart: Rp1SourceSetTableMask source %u failed: %u", i, status);
            return status;
        }
    }

    // DMA settings for the host and its children were checked earlier.
    command = originalCommand | PCI_COMMAND_MMIO | PCI_COMMAND_BUSMASTER | PCI_COMMAND_INTDISABLE;
    __Rp1ConfigWrite(runtime, 4, command, 2);
   
    actualCommand = __Rp1ConfigRead(runtime, 4, 2);
    if (actualCommand != command) {
        ERROR("Rp1PciMessagesStart: enabling PCI command readback 0x%x, expected 0x%x", actualCommand, command);
        return OS_EUNKNOWN;
    }
    TRACE("Rp1PciMessagesStart: allocation %u ready with %u masked sources",
          runtime->Pci.Allocation, FDT_RP1_INTERRUPT_COUNT);
    return OS_EOK;
}

oserr_t
Rp1PciMessagesQuiesce(
    _InOut_ struct Rp1Runtime* runtime)
{
    uint16_t                   command;
    uint32_t                   actualCommand;
    oserr_t                    status;
    struct PciInterruptRequest request = {
        .Operation = PciInterruptDisable,
        .Allocation = runtime->Pci.Allocation,
        .Strategy = INTERRUPT_STRATEGY_MSIX
    };

    TRACE("Rp1PciMessagesQuiesce(bus=%p, allocation=%u)", runtime->Bus, runtime->Pci.Allocation);
    if (runtime->Pci.CommandTouched) {
        // Stop new interrupt writes and DMA before disabling MSI-X or removing
        // mappings used by its table.
        command = (uint16_t)__Rp1ConfigRead(runtime, 4, 2);
        command = (command & ~PCI_COMMAND_BUSMASTER) | PCI_COMMAND_INTDISABLE;
        __Rp1ConfigWrite(runtime, 4, command, 2);
        
        actualCommand = __Rp1ConfigRead(runtime, 4, 2);
        if (actualCommand != command) {
            ERROR("Rp1PciMessagesQuiesce: PCI command readback 0x%x, expected 0x%x", actualCommand, command);
            return OS_EUNKNOWN;
        }
    }

    if (runtime->Pci.State != NULL) {
        status = PciMsixControl(&runtime->Pci.Access, &runtime->Pci.State, &request);
        if (status != OS_EOK) {
            ERROR("Rp1PciMessagesQuiesce: PciMsixControl Disable allocation %u failed: %u",
                  request.Allocation, status);
            return status;
        }
    }
    return OS_EOK;
}

oserr_t
Rp1PciMessagesStop(
    _InOut_ struct Rp1Runtime* runtime)
{
    oserr_t                    status;
    struct PciInterruptRequest request = {
        .Operation = PciInterruptRelease,
        .Allocation = runtime->Pci.Allocation,
        .Strategy = INTERRUPT_STRATEGY_MSIX
    };

    TRACE("Rp1PciMessagesStop(bus=%p, allocation=%u)", runtime->Bus, runtime->Pci.Allocation);
    // Release the allocation before unmapping its table. Keep the table mapped
    // if release fails so cleanup can be retried.
    if (runtime->Pci.State != NULL) {
        status = PciMsixControl(&runtime->Pci.Access, &runtime->Pci.State, &request);
        if (status != OS_EOK) {
            ERROR("Rp1PciMessagesStop: PciMsixControl Release allocation %u failed: %u", request.Allocation, status);
            return status;
        }
    }

    // Mapping may register an I/O range before failing to acquire it.
    status = __Rp1UnmapIo(&runtime->Pci.Table);
    if (status != OS_EOK) {
        ERROR("Rp1PciMessagesStop: __Rp1UnmapIo table failed: %u", status);
        return status;
    }
    
    status = __Rp1UnmapIo(&runtime->Pci.Apbs);
    if (status != OS_EOK) {
        ERROR("Rp1PciMessagesStop: __Rp1UnmapIo APBS failed: %u", status);
        return status;
    }
    return OS_EOK;
}
