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

#include <arch/utils.h>
#include <arch/output.h>
#include <component/cpu.h>
#include <component/ic.h>
#include <machine.h>
#include <memoryspace.h>
#include <string.h>
#include "../common/private.h"

struct Arm64Platform g_arm64Platform;

struct __CpuDescription {
    uint64_t     Affinity;
    uint64_t     ReleaseAddress;
    // Method used to enable the CPU (e.g., spin-table, PSCI).
    // Read from the device tree "enable-method" property.
    unsigned int EnableMethod;
};

static oserr_t
__ReadCpu(
    _In_  const DeviceTreeNode_t*  node,
    _Out_ struct __CpuDescription* cpu)
{
    const char* method = DeviceTreeReadString(node, "enable-method");
    oserr_t     status;

    status = DeviceTreeReadInteger(node, "reg", &cpu->Affinity);
    if (status != OS_EOK || (cpu->Affinity & ~ARM64_MPIDR_AFFINITY_MASK)) {
        return OS_EINVALPARAMS;
    }
    
    cpu->EnableMethod = 0;
    cpu->ReleaseAddress = 0;
    if (method && !strcmp(method, "spin-table")) {
        cpu->EnableMethod = ARM64_CPU_ENABLE_SPIN_TABLE;
        status = DeviceTreeReadInteger(node, "cpu-release-addr", &cpu->ReleaseAddress);
        if (status != OS_EOK || !cpu->ReleaseAddress) {
            return OS_EINVALPARAMS;
        }
        if (cpu->ReleaseAddress & (ARM64_CPU_RELEASE_ALIGNMENT - 1)) {
            return OS_EINVALPARAMS;
        }
    } else if (method && !strcmp(method, "psci")) {
        cpu->EnableMethod = ARM64_CPU_ENABLE_PSCI;
    } else if (method) {
        return OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

static oserr_t
__ReadGic(
    _In_  const DeviceTreeNode_t* node,
    _Out_ struct Arm64Platform*   platform)
{
    uint64_t                    cells;
    oserr_t                     status;
    const DeviceTreeProperty_t* controller;

    controller = DeviceTreeGetProperty(node, "interrupt-controller");
    if (!controller) {
        return OS_EINVALPARAMS;
    }
    
    status = DeviceTreeReadInteger(node, "#interrupt-cells", &cells);
    if (status != OS_EOK || cells != ARM64_DT_GIC_CELLS) {
        return OS_EINVALPARAMS;
    }
    
    // Read the GIC distributor and CPU interface registers from the device tree.
    status = DeviceTreeReadRegister(
        node,
        0,
        &platform->Distributor,
        &platform->DistributorLength
    );
    if (status != OS_EOK) {
        return status;
    }

    status = DeviceTreeReadRegister(
        node,
        1,
        &platform->CpuInterface,
        &platform->CpuInterfaceLength
    );
    if (status != OS_EOK) {
        return status;
    }

    if (platform->DistributorLength < ARM64_GIC_REGION_MINIMUM ||
        platform->CpuInterfaceLength < ARM64_GIC_REGION_MINIMUM) {
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

static oserr_t
__FindPhysicalTimerIndex(
    _In_  const DeviceTreeNode_t* node,
    _Out_ unsigned int*           indexOut)
{
    const DeviceTreeProperty_t* names;
    unsigned int                index = 0;
    uint32_t                    offset = 0;
    const char*                 name;
    const char*                 end;

    // Without names the binding orders secure physical, nonsecure physical,
    // virtual, then hypervisor timers. A named list may omit the secure timer.
    names = DeviceTreeGetProperty(node, "interrupt-names");
    if (!names) {
        *indexOut = ARM64_DT_PHYSICAL_TIMER_INDEX;
        return OS_EOK;
    }
    
    while (offset < names->Length) {
        name = (const char*)names->Value + offset;
        end = memchr(name, 0, names->Length - offset);
        if (!end) {
            return OS_EINVALPARAMS;
        }
        
        if (!strcmp(name, "phys")) {
            *indexOut = index;
            return OS_EOK;
        }
        
        offset += (uint32_t)(end - name) + 1;
        index++;
    }
    return OS_ENOTSUPPORTED;
}

static oserr_t
__ReadTimer(
    _In_  const DeviceTree_t* tree,
    _In_  const DeviceTreeNode_t* node,
    _In_  const DeviceTreeNode_t* gic,
    _Out_ struct Arm64Platform* platform)
{
    DeviceTreeReference_t interrupt;
    unsigned int          index;
    unsigned int          trigger;
    oserr_t               status;

    status = __FindPhysicalTimerIndex(node, &index);
    if (status != OS_EOK) {
        return status;
    }

    status = DeviceTreeReadInterrupt(tree, node, index, &interrupt);
    if (status != OS_EOK) {
        return status;
    }

    if (interrupt.Provider != gic || interrupt.CellCount != ARM64_DT_GIC_CELLS) {
        return OS_ENOTSUPPORTED;
    }

    if (interrupt.Cells[0] != ARM64_DT_GIC_PPI || interrupt.Cells[1] >= ARM64_GIC_PPI_BASE) {
        return OS_ENOTSUPPORTED;
    }

    trigger = interrupt.Cells[2] & ARM64_DT_GIC_TRIGGER_MASK;
    if (trigger != ARM64_DT_GIC_LEVEL_HIGH && trigger != ARM64_DT_GIC_LEVEL_LOW) {
        return OS_ENOTSUPPORTED;
    }

    platform->TimerInterrupt = interrupt.Cells[1] + ARM64_GIC_PPI_BASE;
    platform->TimerFlags = interrupt.Cells[2];
    return OS_EOK;
}

static oserr_t
__ReadPsci(
    _In_    const DeviceTreeNode_t* node,
    _InOut_ struct Arm64Platform*   platform)
{
    const char* method;

    method = DeviceTreeReadString(node, "method");
    if (!method) {
        return OS_EINVALPARAMS;
    }

    if (!strcmp(method, "smc")) {
        platform->PsciConduit = VBOOT_PSCI_SMC;
    } else if (!strcmp(method, "hvc")) {
        platform->PsciConduit = VBOOT_PSCI_HVC;
    } else {
        return OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

// Collect these resources before initializing the platform.
struct __PlatformDescription {
    struct __CpuDescription Cpus[ARM64_CPU_COUNT];
    struct Arm64Platform    Platform;
    const DeviceTreeNode_t* Gic;
    const DeviceTreeNode_t* Timer;
    unsigned int            Count;
    unsigned int            BootIndex;
};

static oserr_t
__DiscoverPlatform(
    _In_    const DeviceTree_t*           tree,
    _InOut_ struct __PlatformDescription* description)
{
    const DeviceTreeNode_t* node;
    const DeviceTreeNode_t* cpuRoot;
    const char*             type;
    uint64_t                bootAffinity;
    unsigned int            index;
    oserr_t                 status;

    cpuRoot = DeviceTreeFindPath(tree, "/cpus");
    if (!cpuRoot) {
        return OS_ENOENT;
    }

    bootAffinity = CpuCorePlatformBlock(GetMachine()->Processor.Cores)->Affinity;
    for (node = tree->Nodes; node; node = node->NextNode) {
        type = DeviceTreeReadString(node, "device_type");

        if (!DeviceTreeIsEnabled(node)) {
            continue;
        }
        
        if (node->Parent == cpuRoot && type && !strcmp(type, "cpu")) {
            if (description->Count == ARM64_CPU_COUNT) {
                return OS_EOVERFLOW;
            }
            
            status = __ReadCpu(node, &description->Cpus[description->Count]);
            if (status != OS_EOK) {
                return status;
            }
            
            for (index = 0; index < description->Count; index++) {
                if (description->Cpus[index].Affinity == description->Cpus[description->Count].Affinity) {
                    return OS_EINVALPARAMS;
                }
            }
            
            if (description->Cpus[description->Count].Affinity == bootAffinity) {
                description->BootIndex = description->Count;
            }
            description->Count++;
        } else if (DeviceTreeIsCompatible(node, "arm,gic-400") ||
                   DeviceTreeIsCompatible(node, "arm,cortex-a15-gic")) {
            if (description->Gic) {
                return OS_ENOTSUPPORTED;
            }
            description->Gic = node;
        } else if (DeviceTreeIsCompatible(node, "arm,armv8-timer")) {
            if (description->Timer) {
                return OS_EINVALPARAMS;
            }
            description->Timer = node;
        } else if (DeviceTreeIsCompatible(node, "arm,psci-0.2") ||
                   DeviceTreeIsCompatible(node, "arm,psci-1.0")) {
            status = __ReadPsci(node, &description->Platform);
            if (status != OS_EOK) {
                return status;
            }
        }
    }
    
    if (description->BootIndex == ARM64_CPU_COUNT || 
            !description->Gic || !description->Timer) {
        return OS_ENOENT;
    }
    return OS_EOK;
}

static oserr_t
__ReadConsole(
    _In_    const DeviceTree_t*   tree,
    _InOut_ struct Arm64Platform* platform)
{
    const DeviceTreeNode_t* chosen = DeviceTreeFindPath(tree, "/chosen");
    const DeviceTreeNode_t* console;
    const char*             path;
    oserr_t                 status;

    if (!chosen) {
        return OS_EOK;
    }
    
    path = DeviceTreeReadString(chosen, "stdout-path");
    console = path ? DeviceTreeFindPath(tree, path) : NULL;
    if (console && DeviceTreeIsEnabled(console) && DeviceTreeIsCompatible(console, "arm,pl011")) {
        status = DeviceTreeReadRegister(console, 0, &platform->Uart, &platform->UartLength);
        if (status != OS_EOK || platform->UartLength < ARM64_UART_REGION_MINIMUM) {
            return OS_EINVALPARAMS;
        }
    }
    return OS_EOK;
}

static void
__PublishCores(
    _In_ const struct __PlatformDescription* description)
{
    unsigned int            coreId = 1;
    unsigned int            index;
    PlatformCpuCoreBlock_t* core;

    // Keep the already constructed primary at zero. Full affinity values
    // remain in the platform blocks for PSCI/spin-table use.
    GetMachine()->Processor.NumberOfCores = description->Count;
    for (index = 0; index < description->Count; index++) {
        if (index == description->BootIndex) {
            core = CpuCorePlatformBlock(GetMachine()->Processor.Cores);
        } else {
            CpuCoreRegister(&GetMachine()->Processor, coreId, CpuStateShutdown, 0);
            core = CpuCorePlatformBlock(GetProcessorCore(coreId));
            coreId++;
        }
        
        core->Affinity = description->Cpus[index].Affinity;
        core->ReleaseAddress = description->Cpus[index].ReleaseAddress;
        core->EnableMethod = description->Cpus[index].EnableMethod;
    }
}

/**
 * @brief Builds the AArch64 platform and CPU configuration from the live device tree.
 *
 * Validates the CPUs, GIC, physical timer, and secondary-CPU start methods
 * before publishing them to shared kernel state. Then prepares optional serial
 * output and registers the interrupt controller. The boot CPU's platform block
 * must already contain its hardware affinity. This does not start other CPUs
 * or configure GIC delivery; those operations run later during kernel startup.
 *
 * @param tree Parsed firmware device tree whose nodes remain available during setup.
 * @return OS_EOK on success, or an error from discovery, resource validation,
 *         or controller registration. Serial initialization errors are ignored.
 *         Failure after publication does not undo the published platform state.
 */
oserr_t
ArchDeviceTreeInitialize(
    _In_ const DeviceTree_t* tree)
{
    unsigned int index;
    oserr_t      status;

    // Collect resources locally so invalid firmware descriptions do not become
    // visible as a partly configured platform. BootIndex starts outside the
    // valid index range, allowing discovery to detect a missing boot CPU node.
    struct __PlatformDescription description = { 
        .BootIndex = ARM64_CPU_COUNT
    };

    // Find enabled CPU nodes and the supported GIC, timer, and optional PSCI
    // interface. Match the running CPU's hardware affinity rather than assuming
    // that firmware lists it first or that its hardware ID is zero.
    status = __DiscoverPlatform(tree, &description);
    if (status != OS_EOK) {
        return status;
    }

    // Validate the controller's interrupt format and both register ranges
    // before using it as the provider for the scheduler timer's interrupt.
    status = __ReadGic(description.Gic, &description.Platform);
    if (status != OS_EOK) {
        return status;
    }

    // Select the nonsecure physical timer and require its interrupt to belong
    // to this GIC, use a private peripheral ID, and request level triggering.
    // The scheduler cannot run with an interrupt description this port cannot use.
    status = __ReadTimer(tree, description.Timer, description.Gic, &description.Platform);
    if (status != OS_EOK) {
        return status;
    }

    // Every additional configured CPU needs a usable way to leave firmware
    // startup. Reject unsupported descriptions now, rather than advertising
    // CPUs that the later secondary-start path cannot bring online.
    for (index = 0; index < description.Count; index++) {
        // Firmware has already started the boot CPU; it does not need an
        // enable-method merely to describe the CPU that is executing this code.
        if (index == description.BootIndex) {
            continue;
        }

        // Without spin-table or PSCI there is no supported request we can send
        // to make this secondary CPU begin executing the kernel entry point.
        if (!description.Cpus[index].EnableMethod) {
            return OS_ENOTSUPPORTED;
        }

        // A CPU marked PSCI also needs the firmware's SMC/HVC call selection.
        // Knowing its affinity alone is not enough to issue the CPU_ON request.
        if (description.Cpus[index].EnableMethod == ARM64_CPU_ENABLE_PSCI && !description.Platform.PsciConduit) {
            return OS_ENOTSUPPORTED;
        }
    }

    // A missing or unsupported chosen console is allowed. If a supported PL011
    // is selected, validate its register range before publishing its address.
    // Malformed selected register data is still an initialization error.
    status = __ReadConsole(tree, &description.Platform);
    if (status != OS_EOK) {
        return status;
    }

    // Validation is complete. Publish register resources before later mapping
    // helpers use them, and assign kernel CPU IDs with the boot CPU kept at zero.
    // Publishing secondary descriptions does not start those CPUs yet.
    g_arm64Platform = description.Platform;
    __PublishCores(&description);

    // The raspberry pi does not have NUMA domains, set it to Uma mode.
    SetMachineUmaMode();

    // Debug serial output is optional. Use the now-published UART address,
    // but do not fail platform initialization if no console exists or mapping
    // it fails. The serial helper preserves firmware's clock and pin settings.
    SerialPortInitialize();

    // Register the GIC with the shared kernel controller list, using its device
    // tree phandle as the ID and its physical distributor address as the base.
    // IDs start at zero and cover the kernel's supported interrupt range; the
    // actual hardware count and register setup are determined later by the GIC
    // initialization path. Return registration failure even though CPU/platform
    // state has already been published; this startup path does not roll it back.
    return CreateInterruptController(
        description.Gic->Phandle,
        0,
        MAX_SUPPORTED_INTERRUPTS,
        description.Platform.Distributor
    );
}

uintptr_t
Arm64MapDevice(
    _In_ uint64_t physical,
    _In_ uint64_t length)
{
    uintptr_t offset = physical & (ARM64_PAGE_SIZE - 1);
    uintptr_t virtual;
    oserr_t   status;

    status = MemorySpaceMap(
        GetDomainMemorySpace(),
        &(struct MemorySpaceMapOptions) {
            .PhysicalStart = physical - offset,
            .Length = (length + offset + ARM64_PAGE_SIZE - 1) & ~(ARM64_PAGE_SIZE - 1ULL),
            .Flags = MAPPING_COMMIT | MAPPING_NOCACHE | MAPPING_DEVICE | MAPPING_PERSISTENT,
            .PlacementFlags = MAPPING_PHYSICAL_CONTIGUOUS | MAPPING_VIRTUAL_GLOBAL
        },
        &virtual
    );
    if (status != OS_EOK) {
        return 0;
    }
    
    return virtual + offset;
}
