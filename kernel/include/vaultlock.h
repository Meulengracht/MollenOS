/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * MollenOS Vault Synchronization
 *   - Synchronizes access to a finite contigious resource.
 */

#ifndef __VAULT_LOCK__
#define __VAULT_LOCK__


#include <os/osdefs.h>
#include <semaphore_slim.h>

typedef struct _VaultLock {
    SlimSemaphore_t         TransferQueue;
    size_t                  Capacity;
    size_t                  Threshold;
    int                     AllowsBlock;
    atomic_int              Waiting;
    atomic_int              Credit;
    atomic_int              Debit;
    atomic_int              Transfer;
} VaultLock_t;

/* ConstructVaultLock
 * Initializes a static instance of a vault lock with the given parameters. The percentage set
 * should be atleast 10% for a managed resource. */
KERNELAPI void KERNELABI
ConstructVaultLock(
    _In_ VaultLock_t*   Vault,
    _In_ size_t         Capacity,
    _In_ size_t         ThresholdPercentage,
    _In_ int            AllowBlock);

/* WithdrawFromVault
 * Withdraws the requested amount from the vault, if the amount is not available the 
 * function will either block or return OsError based on configuration. */
KERNELAPI OsStatus_t KERNELABI
WithdrawFromVault(
    _In_ VaultLock_t*   Vault,
    _In_ int            Amount);

/* DepositToVault
 * Deposits the given amount into the vault. If the amount pushes above the configured
 * threshold, all threads waiting for a withdrawal is woken up. */
KERNELAPI void KERNELABI
DepositToVault(
    _In_ VaultLock_t*   Vault,
    _In_ int            Amount);

#endif // !__VAULT_LOCK__
