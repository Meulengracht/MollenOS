/**
 * MollenOS
 *
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

#include <handles/global.h>
#include <handles/grants.h>
#include <handles/handle.h>
#include <heap.h>
#include <memoryspace.h>
#include <spinlock.h>
#include <string.h>
#include "memory/private.h"

// TODO: Replace the fixed key with a separate, unpredictable secret for each export.
static const uint8_t g_grantTokenSecret[16] = {
    0x56, 0x61, 0x6C, 0x69, 0x47, 0x72, 0x61, 0x6E,
    0x74, 0x54, 0x6F, 0x6B, 0x65, 0x6E, 0x30, 0x31
};

/**
 * @brief Stores the export options, secret and SHM handle reference.
 * The lock protects Active and the resource reference during verification and revoke.
 */
struct __GrantTokenRecord {
    GrantTokenOptions_t Options;
    uint8_t             Secret[16];
    uuid_t              ResourceHandleID;
    Spinlock_t          Lock;
    bool                Active;
};

static bool
__GrantTokenScopeValid(
    _In_ GrantTokenScope_t scope)
{
    unsigned int knownFlags = GrantTokenScopeRead | GrantTokenScopeWrite;

    return scope != 0 && ((unsigned int)scope & ~knownFlags) == 0;
}

static oserr_t
__GrantTokenValidateOptions(
    _In_ const GrantTokenOptions_t* options,
    _In_ const struct SHMBuffer*    buffer)
{
    if (!__GrantTokenScopeValid(options->Scope)) {
        return OS_EINVALPARAMS;
    }

    if (options->TargetType == GrantTokenTargetTypeAny) {
        if (options->TargetGlobalHandleID != UUID_INVALID) {
            return OS_EINVALPARAMS;
        }
    } else if (options->TargetType == GrantTokenTargetTypeSpecific) {
        if (options->TargetGlobalHandleID == UUID_INVALID) {
            return OS_EINVALPARAMS;
        }
    } else {
        return OS_EINVALPARAMS;
    }

    if (buffer->Flags & SHM_PRIVATE) {
        return OS_EPERMISSIONS;
    }

    // The range must fit within the buffer's logical length. The helper uses
    // subtraction to avoid overflow when checking offset + length.
    if (options->Length == 0) {
        return OS_EINVALPARAMS;
    }
    if (!__SHMRangeContains(0, buffer->Length, options->Offset, options->Length)) {
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

static bool
__GrantTokenMatches(
    _In_ const GrantToken_t*              token,
    _In_ const struct __GrantTokenRecord* record)
{
    volatile uint8_t difference = 0;

    // Compare all secret bytes before checking the result.
    // TODO: Use a tested constant-time comparison when each export has its own secret.
    for (size_t i = 0; i < sizeof(record->Secret); i++) {
        difference |= token->Secret[i] ^ record->Secret[i];
    }

    if (difference != 0) {
        return false;
    }

    // Check each field to avoid comparing structure padding. GrantTokenVerify
    // returns OS_EINVALPARAMS for any mismatch here.
    if (token->Version != GRANT_TOKEN_VERSION ||
        token->ControlHandleID != token->ExportGlobalHandleID) {
        return false;
    }

    if (token->Options.TargetType != record->Options.TargetType ||
        token->Options.TargetGlobalHandleID != record->Options.TargetGlobalHandleID) {
        return false;
    }

    if (token->Options.Scope != record->Options.Scope) {
        return false;
    }

    return token->Options.Offset == record->Options.Offset &&
           token->Options.Length == record->Options.Length;
}

static void
__GrantTokenDisable(
    _In_ void* resource)
{
    struct __GrantTokenRecord* record = resource;
    uuid_t                     handleID = UUID_INVALID;

    // Clear Active under the same lock used by verification. Only the first
    // revoke releases the export's resource reference.
    SpinlockAcquireIrq(&record->Lock);
    if (record->Active) {
        record->Active = false;
        handleID = record->ResourceHandleID;
    }
    SpinlockReleaseIrq(&record->Lock);

    // Release the resource outside the export lock. References added by earlier
    // verification calls remain valid.
    if (handleID != UUID_INVALID) {
        ReleaseGlobalHandle(handleID);
    }
}

static void
__GrantTokenDelete(
    _In_ void* resource)
{
    // If the handle had no process grant, grant cleanup has not disabled the
    // export. Disable it here before freeing the record.
    __GrantTokenDisable(resource);
    kfree(resource);
}

oserr_t
GrantTokenCreate(
    _In_  uuid_t                     handleID,
    _In_  const GrantTokenOptions_t* options,
    _Out_ GrantToken_t*              tokenOut)
{
    struct __GrantTokenRecord* record;
    struct SHMBuffer*          buffer;
    GrantToken_t               token = {0};
    uuid_t                     controlHandleID;
    void*                      resource;
    oserr_t                    oserr;

    if (options == NULL || tokenOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (MemorySpaceGetSecurityContext() == NULL) {
        return OS_ENOTSUPPORTED;
    }

    // Copy the options before validating them. The caller must have a grant
    // for the SHM handle, and the export needs a separate reference to it.
    token.Options = *options;
    oserr = AcquireHandleReference(handleID, HandleTypeSHM, &resource);
    if (oserr != OS_EOK) {
        return oserr;
    }

    buffer = resource;

    oserr = __GrantTokenValidateOptions(&token.Options, buffer);
    if (oserr != OS_EOK) {
        ReleaseGlobalHandle(handleID);
        return oserr;
    }

    record = kmalloc(sizeof(struct __GrantTokenRecord));
    if (record == NULL) {
        ReleaseGlobalHandle(handleID);
        return OS_EOOM;
    }
    record->Options = token.Options;
    record->ResourceHandleID = handleID;
    record->Active = true;
    SpinlockConstruct(&record->Lock);
    memcpy(record->Secret, g_grantTokenSecret, sizeof(record->Secret));

    // The record must be initialized before its handle is visible. Use the
    // same ID for the creator's control handle and the token's global record ID.
    controlHandleID = CreateHandle(
        HandleTypeGrantToken,
        __GrantTokenDisable,
        __GrantTokenDelete,
        record
    );
    if (controlHandleID == UUID_INVALID) {
        __GrantTokenDelete(record);
        return OS_EOOM;
    }

    // tokenOut must point to kernel memory. The syscall copies the token to
    // user memory and revokes the export if that copy fails.
    token.ControlHandleID = controlHandleID;
    token.ExportGlobalHandleID = controlHandleID;
    token.Version = GRANT_TOKEN_VERSION;
    memcpy(token.Secret, g_grantTokenSecret, sizeof(token.Secret));
    *tokenOut = token;
    return OS_EOK;
}

oserr_t
GrantTokenRevoke(
    _In_ uuid_t controlHandleID)
{
    struct __GrantTokenRecord* record;
    void*                     resource;
    oserr_t                   oserr;

    if (MemorySpaceGetSecurityContext() == NULL) {
        return OS_ENOTSUPPORTED;
    }

    // The caller must have a grant for the control handle. Keep a reference
    // to the record while disabling the export and closing the handle.
    oserr = AcquireHandleReference(controlHandleID, HandleTypeGrantToken, &resource);
    if (oserr != OS_EOK) {
        return oserr;
    }
    record = resource;
    __GrantTokenDisable(record);
    oserr = DestroyHandle(controlHandleID);
    ReleaseGlobalHandle(controlHandleID);
    return oserr == OS_EINCOMPLETE ? OS_EOK : oserr;
}

oserr_t
GrantTokenVerify(
    _In_  const GrantToken_t* token,
    _In_  GrantTokenScope_t   scope,
    _Out_ uuid_t*             handleIDOut)
{
    struct __GrantTokenRecord* record;
    MSContextSecurity_t*      securityContext;
    GrantToken_t              snapshot;
    uuid_t                    resourceHandleID;
    void*                     resource;
    oserr_t                   oserr;

    if (token == NULL || handleIDOut == NULL) {
        return OS_EINVALPARAMS;
    }

    securityContext = MemorySpaceGetSecurityContext();
    if (securityContext == NULL) {
        return OS_ENOTSUPPORTED;
    }

    // The export ID locates the record. We must verify the token before adding
    // a reference to the SHM buffer.
    snapshot = *token;
    oserr = AcquireGlobalHandle(snapshot.ExportGlobalHandleID, HandleTypeGrantToken, &resource);
    if (oserr != OS_EOK) {
        return OS_EINVALPARAMS;
    }

    record = resource;

    oserr = OS_EINVALPARAMS;
    if (!__GrantTokenMatches(&snapshot, record)) {
        goto exit;
    }

    // Check the receiver and scope only after all token fields match the record.
    oserr = OS_EPERMISSIONS;
    if (record->Options.TargetType == GrantTokenTargetTypeSpecific &&
        record->Options.TargetGlobalHandleID != securityContext->GlobalHandleID) {
        goto exit;
    }
    if (!__GrantTokenScopeValid(scope) || (scope & record->Options.Scope) != scope) {
        goto exit;
    }

    // Hold the export lock while checking Active and adding the SHM reference.
    // A later revoke leaves this reference valid.
    SpinlockAcquireIrq(&record->Lock);
    oserr = OS_ECANCELLED;
    if (record->Active) {
        resourceHandleID = record->ResourceHandleID;
        oserr = AcquireGlobalHandle(resourceHandleID, HandleTypeSHM, NULL);
    }
    SpinlockReleaseIrq(&record->Lock);
    if (oserr == OS_EOK) {
        *handleIDOut = resourceHandleID;
    }

exit:
    ReleaseGlobalHandle(snapshot.ExportGlobalHandleID);
    return oserr;
}
