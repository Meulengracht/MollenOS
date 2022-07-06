/**
 * MollenOS
 *
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
 * Advanced Host Controller Interface Driver
 * TODO:
 *    - Port Multiplier Support
 *    - Power Management
 */

#ifndef __AHCI_DISPATCH_H__
#define __AHCI_DISPATCH_H__

#include <os/osdefs.h>
#include "manager.h"

// Dispatcher Flags 
// Used to setup transfer flags for ahci-transactions
#define DISPATCH_MULTIPLIER(Pmp)        (Pmp & 0xF)
#define DISPATCH_WRITE                  0x10
#define DISPATCH_PREFETCH               0x20
#define DISPATCH_CLEARBUSY              0x40
#define DISPATCH_ATAPI                  0x80

/**
 * AhciDispatchRegisterFIS 
 * * Builds a new AHCI Transaction based on a register FIS
 */
__EXTERN oscode_t
AhciDispatchRegisterFIS(
    _In_ AhciController_t*  controller,
    _In_ AhciPort_t*        port,
    _In_ AhciTransaction_t* transaction);

#endif //!__AHCI_DISPATCH_H__
