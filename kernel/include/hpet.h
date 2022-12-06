/**
 * Copyright 2017, Philip Meulengracht
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
 *
 * High Performance Event Timer (HPET) Driver
 *  - Contains the implementation of the HPET driver for mollenos
 */
#ifndef _HPET_H_
#define _HPET_H_

#include <os/osdefs.h>

/**
 * @brief Initializes the HPET if present. This requires the presence of ACPI tables.
 */
KERNELAPI void KERNELABI HPETInitialize(void);

/**
 * @brief Detects whether or not the HPET is present.
 * @return True if present, otherwise false.
 */
KERNELAPI bool KERNELABI HPETIsPresent(void);

/**
 * @brief Returns whether or not the hpet is configured to use legacy routings.
 * @return True if the HPET has legacy routings
 */
KERNELAPI bool KERNELABI HPETIsEmulatingLegacyController(void);

/**
 * @brief
 *
 * @param[In] index
 * @param[In] frequency
 * @param[In] periodic
 * @param[In] legacyIrq
 * @return
 */
KERNELAPI oserr_t KERNELABI
HPETComparatorStart(
    _In_ int      index,
    _In_ uint64_t frequency,
    _In_ int      periodic,
    _In_ int      legacyIrq);

/**
 * @brief Stops a previously started comparator.
 * @param index The comparator index to stop.
 * @return
 */
KERNELAPI oserr_t KERNELABI
HPETComparatorStop(
        _In_ int index);

#endif //!_HPET_H_
