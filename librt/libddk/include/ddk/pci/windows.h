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

#ifndef __DDK_PCI_WINDOWS_H__
#define __DDK_PCI_WINDOWS_H__

#include <ddk/ddkdefs.h>

// The values match the PCI address-space encoding used by device-tree ranges,
// so they can be copied directly from the device-tree.
enum PciAddressSpace {
    PciAddressIo = 1,
    PciAddressMemory32 = 2,
    PciAddressMemory64 = 3
};

#define PCI_WINDOW_PREFETCHABLE 0x40000000U

/**
 * @brief Describes one address window that the host bridge forwards between
 * the CPU and the PCI bus. BusBase is the address as seen on the PCI bus, which
 * is the value we program into a BAR, and PhysicalBase is where the CPU sees
 * that same byte. Length is the size of both ranges. Attributes only carries
 * the prefetchable flag for now.
 *
 * The windows are only used for CPU access to device resources. DMA ranges are
 * described separately.
 */
struct PciAddressWindow {
    uint32_t Space;
    uint32_t Attributes;
    uint64_t BusBase;
    uint64_t PhysicalBase;
    uint64_t Length;
};

/**
 * @brief Checks that the window uses a known address space, has a non-zero
 * length, and that its last byte does not overflow on either the bus side or
 * the CPU side.
 *
 * @param window The window to check.
 * @return Non-zero if the window is valid, otherwise 0.
 */
static inline int
__PciWindowValid(
    _In_ const struct PciAddressWindow* window)
{
    if (window->Space < PciAddressIo || window->Space > PciAddressMemory64) {
        return 0;
    }

    if (!window->Length || window->Length - 1 > UINT64_MAX - window->BusBase) {
        return 0;
    }
    
    return window->Length - 1 <= UINT64_MAX - window->PhysicalBase;
}

/**
 * @brief Translates the full range of a BAR from a PCI bus address to a CPU
 * physical address. The whole range must fit inside a single window. A 64-bit
 * BAR is allowed to sit below 4 GiB, so we do not use the width of a memory BAR
 * to decide which memory windows it can go through.
 *
 * @param windows The host windows to search.
 * @param count The number of windows.
 * @param space The address space of the BAR.
 * @param attributes The raw BAR attribute bits. Bit 3 is set if the BAR is prefetchable.
 * @param address The PCI bus address of the first byte of the BAR.
 * @param length The full size of the BAR.
 * @param physicalOut Set to the CPU address of the first byte. Only written on success.
 * @return OS_EOK on success, OS_ENOENT if no window covers the range, or
 *         OS_EINVALPARAMS if the input is invalid or more than one window covers it.
 */
static inline oserr_t
__PciTranslateWindow(
    _In_  const struct PciAddressWindow* windows,
    _In_  size_t                         count,
    _In_  uint32_t                       space,
    _In_  uint32_t                       attributes,
    _In_  uint64_t                       address,
    _In_  uint64_t                       length,
    _Out_ uint64_t*                      physicalOut)
{
    uint64_t physical = 0;
    int      found = 0;

    if (space < PciAddressIo || space > PciAddressMemory64 || !length) {
        return OS_EINVALPARAMS;
    }

    if (length - 1 > UINT64_MAX - address) {
        return OS_EOVERFLOW;
    }

    for (size_t i = 0; i < count; i++) {
        const struct PciAddressWindow* window = &windows[i];
        uint64_t                       offset;
        
        if (!__PciWindowValid(window)) {
            return OS_EINVALPARAMS;
        }

        if ((space == PciAddressIo) != (window->Space == PciAddressIo) ||
            address < window->BusBase) {
            continue;
        }

        offset = address - window->BusBase;
        if (offset >= window->Length || length > window->Length - offset) {
            continue;
        }

        // If two windows both cover the range, we cannot tell which one the
        // firmware meant, even if they translate to the same address. Picking
        // the first match would make the result depend on table order, so we
        // reject the firmware description in this case.
        if (found) {
            return OS_EINVALPARAMS;
        }

        // The CPU may do speculative reads through a prefetchable window, so a
        // BAR that is not marked as prefetchable cannot be mapped through it.
        if (space != PciAddressIo && !(attributes & 8) &&
            (window->Attributes & PCI_WINDOW_PREFETCHABLE)) {
            return OS_ENOTSUPPORTED;
        }

        physical = window->PhysicalBase + offset;
        found = 1;
    }

    if (!found) {
        return OS_ENOENT;
    }

    *physicalOut = physical;
    return OS_EOK;
}

#endif //!__DDK_PCI_WINDOWS_H__
