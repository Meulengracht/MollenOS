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

#include <vaultlock.h>

static int
PerformVaultDebitTransfer(
    _In_ VaultLock_t*   Vault)
{
    int Transfer = atomic_load_explicit(&Vault->Transfer, memory_order_acquire);
    if (Transfer > 0) {
        Transfer = atomic_exchange_explicit(&Vault->Transfer, 0, memory_order_acq_rel);
    }
    return Transfer;
}

static int
TakeVaultCredit(
    _In_ VaultLock_t*   Vault)
{
    int TransferAmount = 0;
    if (Vault->Threshold != 0) {
        TransferAmount = atomic_exchange_explicit(&Vault->Credit, 0, memory_order_acq_rel);
    }
    else {
        TransferAmount = atomic_load_explicit(&Vault->Debit, memory_order_relaxed);
        atomic_store_explicit(&Vault->Debit, 0, memory_order_relaxed);
    }
    return TransferAmount;
}

static void
PerformVaultCreditTransfer(
    _In_ VaultLock_t*   Vault)
{
    int Credit  = TakeVaultCredit(Vault);
    int Waiters = 0;
    atomic_fetch_add_explicit(&Vault->Transfer, Credit, memory_order_acq_rel);
    atomic_thread_fence(memory_order_seq_cst);
    Waiters = atomic_exchange_explicit(&Vault->Waiting, 0, memory_order_acq_rel);
    SlimSemaphoreSignal(&Vault->TransferQueue, Waiters);
}

static void
SubVaultDebit(
    _In_ VaultLock_t*   Vault,
    _In_ int            Amount)
{
    if (Vault->Threshold != 0) {
        atomic_fetch_sub_explicit(&Vault->Debit, Amount, memory_order_acq_rel);
    }
    else {
        int PreviousDebit = atomic_load_explicit(&Vault->Debit, memory_order_acquire);
        atomic_store_explicit(&Vault->Debit, PreviousDebit - Amount, memory_order_relaxed);
    }
}

static int
FetchAddVaultCredit(
    _In_ VaultLock_t*   Vault,
    _In_ int            Amount)
{
    if (Vault->Threshold != 0) {
        return atomic_fetch_add_explicit(&Vault->Credit, Amount, memory_order_acq_rel);
    }
    else {
        int PreviousCredit = atomic_load_explicit(&Vault->Credit, memory_order_acquire);
        atomic_store_explicit(&Vault->Credit, PreviousCredit + Amount, memory_order_relaxed);
        return PreviousCredit;
    }
}

static OsStatus_t
TryAddVaultDebit(
    _In_ VaultLock_t*   Vault,
    _In_ int            Amount)
{
    int Capacity        = (int)Vault->Capacity;
    int PreviousDebit   = atomic_fetch_add(&Vault->Debit, Amount);
    if ((PreviousDebit + Amount) <= (Capacity - Vault->Threshold)) {
        return OsSuccess;
    }
    else {
        SubVaultDebit(Vault, Amount);
        return OsError;
    }
}

static OsStatus_t
TryReduceVaultDebit(
    _In_ VaultLock_t*   Vault)
{
    int Amount = PerformVaultDebitTransfer(Vault);
    if (Amount > 0) {
        SubVaultDebit(Vault, Amount);
    }
    return (Amount > 0) ? OsSuccess : OsError;
}

static OsStatus_t
CanBlockVault(
    _In_ VaultLock_t*   Vault,
    _In_ int            Amount,
    _In_ int            Capacity)
{
    int Debit;
    atomic_fetch_add_explicit(&Vault->Waiting, 1, memory_order_acq_rel);
    atomic_thread_fence(memory_order_seq_cst);
    TryReduceVaultDebit(Vault);
    Debit = atomic_load_explicit(&Vault->Debit, memory_order_relaxed);
    if ((Debit + Amount) <= Capacity) {
        atomic_fetch_sub_explicit(&Vault->Waiting, 1, memory_order_acq_rel);
        return OsError;
    }
    return OsSuccess;
}

/* DepositToVault
 * Deposits the given amount into the vault. If the amount pushes above the configured
 * threshold, all threads waiting for a withdrawal is woken up. */
void
DepositToVault(
    _In_ VaultLock_t*   Vault,
    _In_ int            Amount)
{
    int InitialCredit   = FetchAddVaultCredit(Vault, Amount);
    int Threshold       = (int)Vault->Threshold;
    if ((InitialCredit + Amount) >= Threshold && InitialCredit < Threshold) {
        PerformVaultCreditTransfer(Vault);
    }
}

/* WithdrawFromVault
 * Withdraws the requested amount from the vault, if the amount is not available the 
 * function will either block or return OsError based on configuration. */
OsStatus_t
WithdrawFromVault(
    _In_ VaultLock_t*   Vault,
    _In_ int            Amount)
{
    // First assert that we have enough credit for the transaction
    // otherwise it will block untill then
    if (TryAddVaultDebit(Vault, Amount) != OsSuccess) {
        int Capacity = (int)Vault->Capacity - Vault->Threshold;
        int Debit;
        while (1) {
            TryReduceVaultDebit(Vault);
            Debit = atomic_load_explicit(&Vault->Debit, memory_order_relaxed);
            if ((Debit + Amount <= Capacity) && 
                (TryAddVaultDebit(Vault, Amount) == OsSuccess)) {
                break;
            }

            // Block
            if (CanBlockVault(Vault, Amount, Capacity) == OsSuccess) {
                SlimSemaphoreWait(&Vault->TransferQueue, 0);
            }
        }
    }
    return OsSuccess;
}

/* ConstructVaultLock
 * Initializes a static instance of a vault lock with the given parameters. The percentage set
 * should be atleast 10% for a managed resource. */
void
ConstructVaultLock(
    _In_ VaultLock_t*   Vault,
    _In_ size_t         Capacity,
    _In_ size_t         ThresholdPercentage,
    _In_ int            AllowBlock)
{
    assert(Vault != NULL);
    assert(ThresholdPercentage > 0);

    // Zero the instance before initializing, right now the
    // allow-block parameter is not really used and currently always blocks.
    memset((void*)Vault, 0, sizeof(Vault));
    SlimSemaphoreConstruct(&Vault->TransferQueue, 0, 10);
    Vault->Capacity     = Capacity;
    Vault->Threshold    = Capacity / ThresholdPercentage;
    Vault->AllowsBlock  = AllowBlock;
}
