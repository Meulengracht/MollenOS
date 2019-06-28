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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Advanced Host Controller Interface Driver
 * TODO:
 *    - Port Multiplier Support
 *    - Power Management
 */

#include "manager.h"

static OsStatus_t
AllocateCommandSlot(
    _In_  AhciDevice_t* Device,
    _Out_ int*          SlotOut)
{
    OsStatus_t Status = OsError;
    int        Slots;
    int        i;
    
    while (Status != OsSuccess) {
        Slots = atomic_load(&Device->Slots);
        
        for (i = 0; i < Device->SlotCount; i++) {
            // Check availability status on this command slot
            if (Slots & (1 << i)) {
                continue;
            }

            if (atomic_compare_exchange_strong(&Device->Slots, &Slots, Slots | (1 << i))) {
                Status   = OsSuccess;
                *SlotOut = i;
            }
            break;
        }
    }
    return Status;
}

static void
FreeCommandSlot(
    _In_ AhciDevice_t* Device,
    _In_ int           Slot)
{
    
}

OsStatus_t
AhciDeviceQueueTransaction(
    _In_ AhciDevice_t*      Device,
    _In_ AhciTransaction_t* Transaction)
{
    OsStatus_t Status;
    
    // OK so the transaction we just recieved needs to be queued up,
    // so we must initally see if we can allocate a new slot on the port
    if (AllocateCommandSlot(Device, &Transaction->Slot) != OsSuccess) {
        Transaction->State = TransactionQueued;
        return CollectionAppend(Device->Transactions, &Transaction->Header);
    }
    
    // If we reach here we've successfully allocated a slot, now we should dispatch 
    // the transaction
    Status = AhciDispatchRegisterFIS(Device, Transaction);
    if (Status != OsSuccess) {
        FreeCommandSlot(Device, Transaction->Slot);
    }
    return Status;
}
