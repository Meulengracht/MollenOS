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
#include <ddk/interrupt-provider.h>

#include <bus/rp1/runtime.h>
#include <bus/rp1/interrupt.h>
#include <bus/pci/msix.h>

struct Rp1Source {
    uuid_t                   Allocation;
    struct Rp1Child*         Owner;
    uuid_t                   Principal;
    // Kernel-selected message returned when the whole group was attached.
    DeviceInterruptMessage_t Message;
    // False while a failed attachment still needs cleanup; block programming.
    int                      MessageReady;
    int                      Programmed;
};

/** Callback input kept unchanged until its completion handle is released. */
struct Rp1CompletionContext {
    uint32_t Source;
};

/** Completion resources have bus lifetime, independently of child reservations. */
struct Rp1Completion {
    uuid_t                      Handle;
    struct Rp1CompletionContext Context;
};

/** Private PCI resources; generic PCI clients never own or expose these BARs. */
struct Rp1PciMessages {
    struct MsixDeviceOperations Access;
    struct PciMsixState*        State;
    DeviceIo_t                  Table;
    DeviceIo_t                  Apbs;
    uuid_t                      Allocation;
    int                         CommandTouched;
};

/** Runtime lifetime follows the attachment, with setup/control under PCI lock. */
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
 * 
 * @param runtime PCI-locked owner of already allocated kernel groups.
 * @return OS_EOK or an error with partial resources retained for Stop.
 */
__EXTERN oserr_t
Rp1PciMessagesStart(
    _InOut_ struct Rp1Runtime* runtime);

/**
 * @brief Restore PCI configuration and free mappings after handlers are unbound.
 * 
 * @param runtime PCI-locked owner, already quiesced.
 * @return OS_EOK or a retryable error retaining unfinished resources.
 */
__EXTERN oserr_t
Rp1PciMessagesStop(
    _InOut_ struct Rp1Runtime* runtime);

/**
 * @brief Stop bus mastering and mask the function before dismantling delivery.
 * 
 * @param runtime PCI-locked owner, possibly only partly initialized.
 * @return OS_EOK after verified writes, or a retryable error.
 */
__EXTERN oserr_t
Rp1PciMessagesQuiesce(
    _InOut_ struct Rp1Runtime* runtime);

/**
 * @brief Mask a source's table entry before the child synchronizes its group.
 * 
 * @param context Runtime owner under the PCI lock.
 * @param source RP1 source number.
 * @return OS_EOK after both operations complete, otherwise their error.
 */
__EXTERN oserr_t
Rp1SourceMask(
    _In_ void*        context,
    _In_ unsigned int source);

/**
 * @brief Unmask a PCI entry after the child has enabled its kernel group.
 * 
 * @param context Runtime owner under the PCI lock.
 * @param source RP1 source number with a bound handler.
 * @return OS_EOK or an error requiring explicit disable before retry.
 */
__EXTERN oserr_t
Rp1SourceUnmask(
    _In_ void*        context,
    _In_ unsigned int source);

/**
 * @brief Change an entry's mask with posted-write flushing and readback.
 * 
 * @param runtime PCI-locked table owner.
 * @param source RP1 source number.
 * @param mask Nonzero to block delivery.
 * @return OS_EOK on verified state.
 */
__EXTERN oserr_t
Rp1SourceSetTableMask(
    _InOut_ struct Rp1Runtime* runtime,
    _In_    unsigned int       source,
    _In_    int                mask);

/**
 * @brief Register a level source's completion callback while the bus is stopped.
 * 
 * @param runtime PCI-locked owner, retaining partial creation on failure.
 * @param source Validated firmware source with immutable callback input.
 * @return OS_EOK or an error; Stop releases any created handle.
 */
__EXTERN oserr_t
Rp1CompletionStart(
    _InOut_ struct Rp1Runtime* runtime,
    _In_    unsigned int       source);

/**
 * @brief Release all completion handles after child groups have detached.
 * 
 * @param runtime Owner of callback contexts and the APBS mapping.
 * @return OS_EOK or an error retaining unreleased handles for retry.
 */
__EXTERN oserr_t
Rp1CompletionsStop(
    _InOut_ struct Rp1Runtime* runtime);

#endif  //!__DEVICED_RP1_RUNTIME_PRIVATE_H__
