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
 * Controls interrupts raised by hardware blocks inside the RP1 chip.
 * Each interrupt source has a number from 0 to 60. The PCI code sets up MSI-X,
 * a mechanism that delivers interrupts to CPUs through PCI memory writes.
 * This code configures and gates sources. The kernel acknowledges delivery.
 * 
 */

#ifndef __DEVICED_RP1_INTERRUPT_H__
#define __DEVICED_RP1_INTERRUPT_H__

#include <bus/rp1/rp1.h>
#include <ddk/io.h>

/**
 * @brief Callbacks provided by the PCI code to control interrupt delivery to CPUs.
 *
 * A vector is an allocated interrupt entry. The PCI code must allocate and
 * configure all 61 MSI-X entries before Rp1InterruptInitialize is called.
 * Callback numbers identify RP1 sources (0 through 60), not CPU interrupt numbers.
 *
 * Mask blocks new PCI messages. Unmask allows delivery again. The child driver
 * synchronizes its kernel group after Mask, before releasing handler resources.
 * Both return a status. Failure retains controller ownership for a disable or
 * destroy retry; an unsuccessful Unmask may already have allowed delivery.
 *
 * The controller copies this structure. Keep the callback context and allocated
 * interrupt entries available until Rp1InterruptDestroy succeeds.
 */
struct Rp1InterruptParent {
    void*        Context;
    unsigned int VectorCount;

    oserr_t (*Mask)(void*, unsigned int);
    oserr_t (*Unmask)(void*, unsigned int);
};

/**
 * @brief Register access, callbacks, and settings for one RP1 interrupt controller.
 *
 * Types and Enabled record the settings for each source, indexed from 0 to 60.
 * Ready means initialization completed. Registers points to memory supplied by
 * the caller; keep it mapped and keep this object available until Destroy
 * succeeds. Run setup and control calls one at a time, without overlapping them.
 */
struct Rp1InterruptController {
    DeviceIo_t*               Registers;
    struct Rp1InterruptParent Parent;
    int                       Ready;
    uint8_t                   Types[FDT_RP1_INTERRUPT_COUNT];
    uint8_t                   Enabled[FDT_RP1_INTERRUPT_COUNT];
};

/**
 * @brief Prepare an RP1 interrupt controller, leaving every source disabled.
 *
 * @param controller Inactive object initialized to zero. If this call fails
 *                   after setting Registers, call Rp1InterruptDestroy and wait
 *                   for success before releasing the object or register mapping.
 * @param registers Acquired memory mapping of RP1's peripheral-control registers
 *                  (APBS), at least RP1_PCIE_APBS_LENGTH bytes long.
 * @param parent PCI callbacks and interrupt entries, already allocated,
 *               configured, and blocked from delivery (masked).
 * @return OS_EOK on success, OS_EINVALPARAMS for invalid inputs,
 *         OS_ENOTSUPPORTED for missing parent callbacks or too few interrupt
 *         entries, or a register-write error.
 */
__EXTERN oserr_t
Rp1InterruptInitialize(
    _Out_ struct Rp1InterruptController*   controller,
    _In_  DeviceIo_t*                      registers,
    _In_  const struct Rp1InterruptParent* parent);

/**
 * @brief Select how a disabled source signals an interrupt.
 *
 * @param controller Initialized interrupt controller.
 * @param source RP1 source number, 0 through 60; it must currently be disabled.
 * @param type 1 for a rising edge (the signal changes from low to high), or
 *             4 for a high level (the signal stays high until the device is handled).
 * @return OS_EOK on success, OS_EINVALPARAMS for invalid inputs or controller
 *         state, or a register-write error. The saved type changes only on success.
 */
__EXTERN oserr_t
Rp1InterruptConfigure(
    _InOut_ struct Rp1InterruptController* controller,
    _In_    unsigned int                   source,
    _In_    unsigned int                   type);

/**
 * @brief Allow a configured RP1 source to deliver interrupts.
 *
 * @param controller Initialized controller whose handler is ready for this source.
 * @param source RP1 source number, 0 through 60; configure it before enabling it.
 *               Its handler may run immediately when delivery is allowed.
 * @return OS_EOK on success, OS_EINVALPARAMS for invalid inputs, an unconfigured
 *         or already enabled source, or a register-write error.
 */
__EXTERN oserr_t
Rp1InterruptEnable(
    _InOut_ struct Rp1InterruptController* controller,
    _In_    unsigned int                   source);

/**
 * @brief Mask PCI delivery and disable the RP1 source.
 *
 * @param controller Controller with its register mapping and PCI callbacks set.
 * @param source RP1 source number, 0 through 60.
 * @return OS_EOK on success, OS_EINVALPARAMS for invalid inputs, or a register-write
 *         error. If the write fails, PCI delivery remains blocked; keep the
 *         mapping available so shutdown can be retried.
 */
__EXTERN oserr_t
Rp1InterruptDisable(
    _InOut_ struct Rp1InterruptController* controller,
    _In_    unsigned int                   source);

/**
 * @brief Stop all interrupt delivery and clear the controller's saved state.
 *
 * Call after child drivers have disabled and released their kernel groups. On
 * success, the caller may release the register mapping and PCI interrupt entries;
 * this function does not free either. If it fails, keep them and the controller
 * available and retry this function.
 *
 * @param controller Controller to shut down, including one only partly initialized.
 * @return OS_EOK if shutdown succeeds or no register mapping is set,
 *         OS_EINVALPARAMS for NULL, or a register-write error.
 */
__EXTERN oserr_t
Rp1InterruptDestroy(
    _InOut_ struct Rp1InterruptController* controller);

#endif
