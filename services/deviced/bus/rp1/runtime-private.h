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

#ifndef __DEVICED_RP1_RUNTIME_PRIVATE_H__
#define __DEVICED_RP1_RUNTIME_PRIVATE_H__

#include <ddk/types/interrupt.h>
#include <ddk/interrupts/bus.h>

#include <bus/rp1/runtime.h>
#include <bus/rp1/interrupt.h>
#include <bus/pci/msix.h>

/**
 * @brief A child's interrupt allocation and the message programmed for one RP1 source.
 * MessageReady records a successful group attachment. Programmed records whether
 * the source's MSI-X entry has been written.
 */
struct Rp1Source {
    uuid_t                   Allocation;
    struct Rp1Child*         Owner;
    uuid_t                   Principal;
    // Message returned by the kernel after the whole group is attached.
    DeviceInterruptMessage_t Message;
    // False until attachment succeeds. Programming is blocked during cleanup.
    int                      MessageReady;
    int                      Programmed;
};

struct Rp1CompletionContext {
    uint32_t Source;
};

/**
 * @brief A bus callback handle and the source number passed to the callback.
 * Keep the context valid until the handle has been released.
 */
struct Rp1Completion {
    uuid_t                      Handle;
    struct Rp1CompletionContext Context;
};

/**
 * @brief PCI access, interrupt allocation and register mappings used by RP1.
 * CommandTouched records whether setup may have changed the PCI command register.
 */
struct Rp1PciMessages {
    struct MsixDeviceOperations Access;
    struct PciMsixState*        State;
    DeviceIo_t                  Table;
    DeviceIo_t                  Apbs;
    uuid_t                      Allocation;
    int                         CommandTouched;
};

/**
 * @brief Interrupt controller state, child sources and bus callbacks for one RP1 bus.
 * Quiescing blocks new source requests while the bus is stopping.
 */
struct Rp1Runtime {
    struct Rp1Bus*                Bus;
    struct Rp1PciMessages         Pci;
    struct Rp1InterruptController Controller;
    struct Rp1Source              Sources[FDT_RP1_INTERRUPT_COUNT];
    struct Rp1Completion          Completions[FDT_RP1_INTERRUPT_COUNT];
    int                           Quiescing;
};

/**
 * @brief Reserve, map and program the complete RP1 MSI-X table.
 * Hold the PCI lock and Bus->Lock.
 *
 * @param runtime Runtime containing the kernel allocation.
 * @return OS_EOK on success. On failure, call Rp1PciMessagesStop to release
 *         any resources left by setup.
 */
__EXTERN oserr_t
Rp1PciMessagesStart(
    _InOut_ struct Rp1Runtime* runtime);

/**
 * @brief Release the MSI-X reservation and mappings after handlers are unbound.
 * Stop bus delivery first. Hold the PCI lock and Bus->Lock.
 *
 * @param runtime Runtime with bus delivery stopped.
 * @return OS_EOK on success. If cleanup fails, keep the runtime and retry.
 */
__EXTERN oserr_t
Rp1PciMessagesStop(
    _InOut_ struct Rp1Runtime* runtime);

/**
 * @brief Stop bus mastering and disable MSI-X before releasing interrupt resources.
 * Hold the PCI lock and Bus->Lock.
 *
 * @param runtime Runtime that may be partly initialized.
 * @return OS_EOK after the writes are verified, or an error requiring a retry.
 */
__EXTERN oserr_t
Rp1PciMessagesQuiesce(
    _InOut_ struct Rp1Runtime* runtime);

/**
 * @brief Mask a source's table entry before the child synchronizes its group.
 * Hold Bus->Lock.
 *
 * @param context Runtime with a mapped MSI-X table.
 * @param source Valid RP1 source number.
 * @return OS_EOK after mask readback matches, or a write or readback error.
 */
__EXTERN oserr_t
Rp1SourceMask(
    _In_ void*        context,
    _In_ unsigned int source);

/**
 * @brief Unmask a PCI entry after the child has enabled its kernel group.
 * Hold Bus->Lock.
 *
 * @param context Runtime with a mapped MSI-X table.
 * @param source Valid RP1 source number with a bound handler.
 * @return OS_EOK on success. On failure, disable the source before retrying.
 */
__EXTERN oserr_t
Rp1SourceUnmask(
    _In_ void*        context,
    _In_ unsigned int source);

/**
 * @brief Change an entry's mask, flush the write and check the result.
 * Hold Bus->Lock.
 *
 * @param runtime Runtime with a mapped MSI-X table.
 * @param source Valid RP1 source number.
 * @param mask Nonzero to block delivery.
 * @return OS_EOK after mask readback matches, or a write or readback error.
 */
__EXTERN oserr_t
Rp1SourceSetTableMask(
    _InOut_ struct Rp1Runtime* runtime,
    _In_    unsigned int      source,
    _In_    int               mask);

/**
 * @brief Register a level source's bus callback.
 * The bus must be stopped. Hold the PCI lock and Bus->Lock.
 *
 * @param runtime Runtime with a mapped APBS region.
 * @param source Validated firmware source with no existing callback handle.
 * @return OS_EOK on success. On failure, call Rp1CompletionsStop to release
 *         any handle created by this call.
 */
__EXTERN oserr_t
Rp1CompletionStart(
    _InOut_ struct Rp1Runtime* runtime,
    _In_    unsigned int      source);

/**
 * @brief Release all bus callback handles after child groups have detached.
 *
 * @param runtime Runtime containing the callback contexts and APBS mapping.
 * @return OS_EOK on success. If release fails, keep the runtime and retry.
 */
__EXTERN oserr_t
Rp1CompletionsStop(
    _InOut_ struct Rp1Runtime* runtime);

#endif  //!__DEVICED_RP1_RUNTIME_PRIVATE_H__
