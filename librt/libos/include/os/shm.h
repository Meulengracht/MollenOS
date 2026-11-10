/**
 * Copyright 2023, Philip Meulengracht
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
 */

#ifndef __OS_SHM_H__
#define __OS_SHM_H__

#include <os/types/shm.h>
#include <os/types/grants.h>
#include <os/types/handle.h>

_CODE_BEGIN

/**
 * @brief A CPU access session for an SHM range. The session can be reused, and it
 * does not depend on any device mapping. Must be zeroed before first use. Calls on
 * a session must be serialized, and it must be closed once access has ended.
 * Concurrent calls return OS_EBUSY, also while a begin is waiting in an async child.
 * Sessions use kernel handles that only work in the security context that created
 * them. To access the buffer from another process, share the buffer and open a new
 * session there. The kernel keeps the storage allocated until the session is closed
 * or the process exits.
 * All CPU users of SHM storage that is also used for DMA must wrap their accesses
 * in SHMAccessBeginCpu and SHMAccessEndCpu, also users in other processes. Direct
 * pointer accesses are not trapped.
 */
struct SHMAccessSession {
    // Handle is the kernel access session handle. UUID_INVALID when the session
    // is not open.
    uuid_t Handle;
};

/**
 * @brief Opens a CPU access session for a nonempty range of an ordinary SHM buffer.
 *
 * @param buffer Existing SHM attachment of the buffer.
 * @param offset First byte relative to the whole buffer.
 * @param length Number of bytes within the buffer. Must be nonzero.
 * @param session Zeroed session. Calls on it must be serialized.
 * @return OS_EOK, OS_EBUSY if the session is already open, or an argument,
 *         buffer lookup or allocation error.
 */
CRTDECL(oserr_t,
SHMAccessOpen(
    _In_    const OSHandle_t*        buffer,
    _In_    size_t                   offset,
    _In_    size_t                   length,
    _InOut_ struct SHMAccessSession* session));

/**
 * @brief Begins CPU access to a shared range.
 * Wait behavior is described by SHMWaitMode.
 *
 * @param session Open CPU access session. Calls on it must be serialized.
 * @param access SHMCpuAccessRead, or SHMCpuAccessWrite for read/write access.
 * @param mode How to handle conflicts, see SHMWaitMode.
 * @param deadline Absolute timeout, or NULL to wait forever. Ignored for SHMWaitNonBlocking.
 * @return OS_EOK, an error from the shared wait, or an argument or kernel error.
 */
CRTDECL(oserr_t,
SHMAccessBeginCpu(
    _In_ struct SHMAccessSession* session,
    _In_ enum SHMCpuAccess        access,
    _In_ enum SHMWaitMode         mode,
    _In_ const OSTimestamp_t*     deadline));

/**
 * @brief Ends CPU access. Must be called before device work or conflicting CPU
 * access can start.
 *
 * @param session Open session with active CPU access. All memory accesses must be
 *                done before this call, and calls on the session must be serialized.
 * @return OS_EOK, OS_EINVALPARAMS if no CPU access is active, or a kernel error.
 */
CRTDECL(oserr_t,
SHMAccessEndCpu(
    _In_ struct SHMAccessSession* session));

/**
 * @brief Closes an idle CPU access session.
 *
 * @param session Session to close. Calls on it must be serialized. NULL or an empty
 *                session is a no-op. On success its handle is cleared.
 * @return OS_EOK, OS_EBUSY while access is active or queued, or a kernel error.
 *         On failure the session stays open, so the call can be retried.
 */
CRTDECL(oserr_t,
SHMAccessClose(
    _InOut_ struct SHMAccessSession* session));

/**
 * @brief Releases an SHM attachment. If cleanup fails, the handle is kept.
 * The last local reference unmaps the CPU view and detaches from the kernel.
 * DMA mappings keep their own reference to the storage, and must be unmapped
 * separately. A failure may leave the cleanup partly done. In that case, only
 * call SHMRelease again on the same attachment. A successful release clears the
 * handle. Release must be serialized against all other use of the handle.
 * 
 * @param handle SHM handle to release. NULL or empty handles are a no-op.
 * @return OS_EOK or a cleanup error. On failure the handle and its payload are kept.
 */
CRTDECL(oserr_t,
SHMRelease(
        _InOut_ OSHandle_t* handle));

/**
 * Creates a new page aligned memory buffer and provides the initial attachment.
 * The attachment will already be mapped into current address space and provided mappings.
 * 
 * @param shm    [In] The information related to the buffer that should be created.
 * @param handle [In] The structure to fill with the attachment information.
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
SHMCreate(
        _In_  SHM_t*      shm,
        _Out_ OSHandle_t* handleOut));

/**
 * Attach to a memory buffer handle, but does not perform further actions.
 * 
 * @param shmID  [In] The memory buffer handle to attach to.
 * @param handle [In] The structure to fill with the attachment information.
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
SHMAttach(
        _In_  uuid_t      shmID,
        _Out_ OSHandle_t* handleOut));

/**
 * @brief Attaches to the full range and scope of a grant token, without mapping it.
 * The kernel creates a separate restricted handle. Later SHM offsets are relative
 * to the token range. Existing mappings stay valid if the token is revoked.
 *
 * A single-use token is used up when the kernel imports it, which happens before
 * the attachment is created and before anything is mapped. So the token stays
 * used up even if creating the attachment fails. If the result cannot be copied
 * back, the attachment is closed but the token is not restored.
 * A token that has been revoked or used up returns OS_ECANCELLED.
 *
 * @param token Token to verify for the current security group.
 * @param handleOut Receives the attachment on success. Unchanged on failure.
 * @return OS_EOK, or a token validation, permission, copy or allocation error.
 */
CRTDECL(oserr_t,
SHMAttachToken(
    _In_  const GrantToken_t* token,
    _Out_ OSHandle_t*         handleOut));

/**
 * @brief Maps an SHM buffer or updates its current CPU mapping.
 * For ordinary attachments, changing only the length keeps the buffer address.
 * Changing the offset replaces the mapping and invalidates the old pointer.
 * New access flags apply to the entire mapping.
 *
 * Restricted token attachments may get a new address on every successful call, also
 * for remaps at the same offset and for resizes. Call SHMBuffer again after success.
 * Their offsets are relative to the token, and every CPU page that gets mapped must
 * fit inside the token range. CPU read/write access requires both scope bits.
 * Executable access is denied.
 * 
 * @param handle SHM attachment to map.
 * @param offset First byte of the mapping, relative to the attachment.
 * @param length Number of bytes to map.
 * @param flags Memory access flags. Must not exceed the access allowed when the
 *              buffer was created.
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
SHMMap(
        _In_ OSHandle_t*  handle,
        _In_ size_t       offset,
        _In_ size_t       length,
        _In_ unsigned int flags));

/**
 * Commits the address by allocating physical page to backup the virtual address
 * 
 * @param handle  [In] The memory buffer attachment that should be resized.
 * @param address [In] The starting virtual address to commit from.
 * @param length  [In] The number of bytes to commit (will be rounded up to page-size).
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
SHMCommit(
        _In_ OSHandle_t* handle,
        _In_ vaddr_t     address,
        _In_ size_t      length));

/**
 * Remove the mapping that has been previously created by its counterpart.
 * 
 * @param handle [In] The memory buffer attachment to unmap from current addressing space.
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
SHMUnmap(
        _In_ OSHandle_t* handle,
        _In_ void*       address,
        _In_ size_t      length));

/**
 * @brief Returns the mapped buffer address represented by a shared-memory handle.
 * 
 * @param handle Shared-memory handle to query.
 * @return The mapped buffer address, or NULL if it is not mapped.
 */
CRTDECL(void*,
SHMBuffer(
        _In_ OSHandle_t* handle));

/**
 * @brief Returns the current mapped length of a shared-memory handle.
 * 
 * @param handle Shared-memory handle to query.
 * @return The mapped length in bytes.
 */
CRTDECL(size_t,
SHMBufferLength(
        _In_ OSHandle_t* handle));

/**
 * @brief Returns the capacity of a shared-memory handle.
 * 
 * @param handle Shared-memory handle to query.
 * @return The buffer capacity in bytes.
 */
CRTDECL(size_t,
SHMBufferCapacity(
        _In_ OSHandle_t* handle));

/**
 * @brief Returns the offset of the current mapping within the SHM buffer.
 * SHMCreate or SHMMap must have succeeded on this handle before the offset is read.
 * 
 * @param handle Handle of the mapped SHM buffer.
 * @return Byte offset of the mapping within the buffer.
 */
CRTDECL(size_t,
SHMBufferOffset(
        _In_ OSHandle_t* handle));

/**
 * @brief Returns the physical segments of a local SHM handle.
 * Restricted handles only return their allowed byte range, with the first and last
 * page clipped to that range. These are CPU physical addresses, which can differ
 * from device addresses. Use DMAMapSHM to get device addresses.
 * A maxCount of 0 or below returns all segments. The caller must free the entries.
 * 
 * @param handle SHM attachment to query.
 * @param sgTable Receives the segment table. The caller must free its Entries.
 * @param maxCount Maximum number of entries. Use 0 or a negative value for all entries.
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
SHMGetSGTable(
        _In_ OSHandle_t*   handle,
        _In_ SHMSGTable_t* sgTable,
        _In_ int           maxCount));

_CODE_END
#endif //!__OS_SHM_H__
