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
#include <handles/provider.h>
#include <handles/grants.h>
#include <handles/handle.h>
#include <heap.h>
#include <memoryspace.h>
#include <spinlock.h>
#include <shm/view.h>
#include <string.h>

// TODO: Replace the fixed key with a separate, unpredictable secret for each export.
static const uint8_t g_grantTokenSecret[16] = {
    0x56, 0x61, 0x6C, 0x69, 0x47, 0x72, 0x61, 0x6E,
    0x74, 0x54, 0x6F, 0x6B, 0x65, 0x6E, 0x30, 0x31
};

enum __GrantTokenStatus {
    GrantTokenStatusInvalid = 0,
    GrantTokenStatusActive,
    GrantTokenStatusRevoked
};

struct __GrantTokenRecord {
    enum __GrantTokenStatus Status;
    GrantTokenOptions_t     Options;
    uint8_t                 Secret[16];
    uuid_t                  ResourceHandleID;
    HandleType_t            ResourceHandleType;
    Spinlock_t              Lock;
};

static oserr_t
__GrantTokenValidateTarget(
    _In_ const GrantTokenOptions_t* options)
{
    if (options->TargetType == GrantTokenTargetTypeAny) {
        return options->TargetGlobalHandleID == UUID_INVALID ? OS_EOK : OS_EINVALPARAMS;
    }

    if (options->TargetType == GrantTokenTargetTypeSpecific) {
        return options->TargetGlobalHandleID != UUID_INVALID ? OS_EOK : OS_EINVALPARAMS;
    }

    return OS_EINVALPARAMS;
}

static oserr_t
__GrantTokenReferenceResource(
    _In_  uuid_t                     handleID,
    _In_  const GrantTokenOptions_t* options,
    _Out_ HandleType_t*              handleTypeOut)
{
    switch (options->ResourceType) {
        case GrantTokenResourceTypeSHM:
            return SHMReferenceForExport(handleID, options, handleTypeOut);
        case GrantTokenResourceTypeProvider:
            return HandleProviderReferenceForExport(handleID, options, handleTypeOut);
        default:
            return OS_ENOTSUPPORTED;
    }
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

    // Check each field to avoid comparing structure padding. GrantTokenImport
    // returns OS_EINVALPARAMS for any mismatch here.
    if (token->Version != GRANT_TOKEN_VERSION ||
        token->ControlHandleID != token->ExportGlobalHandleID) {
        return false;
    }

    if (token->Options.TargetType != record->Options.TargetType ||
        token->Options.TargetGlobalHandleID != record->Options.TargetGlobalHandleID) {
        return false;
    }

    if (token->Options.ResourceType != record->Options.ResourceType ||
        token->Options.Scope != record->Options.Scope ||
        token->Options.Flags != record->Options.Flags) {
        return false;
    }

    return token->Options.Data.Words[0] == record->Options.Data.Words[0] &&
           token->Options.Data.Words[1] == record->Options.Data.Words[1];
}

static void
__GrantTokenDisable(
    _In_ void* resource)
{
    struct __GrantTokenRecord* record = resource;
    uuid_t                     handleID = UUID_INVALID;

    // Take the same lock as GrantTokenImport, so a source reference cannot be
    // handed out while we revoke an active export.
    SpinlockAcquireIrq(&record->Lock);
    if (record->Status == GrantTokenStatusActive) {
        record->Status = GrantTokenStatusRevoked;
        handleID = record->ResourceHandleID;
    }
    SpinlockReleaseIrq(&record->Lock);

    // Release the resource outside of the export lock. References handed out
    // by earlier imports stay valid. A used-up single-use export is no longer
    // active, as its reference was handed to the importer. So in this case
    // there is nothing to release here.
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
    HandleType_t               sourceType;
    GrantToken_t               token = { 0 };
    uuid_t                     controlHandleID;
    oserr_t                    oserr;

    if (options == NULL || tokenOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (MemorySpaceGetSecurityContext() == NULL) {
        return OS_ENOTSUPPORTED;
    }

    // Copy the options into the token first. All the checks below run on
    // that copy.
    token.Options = *options;
    
    if (token.Options.Flags & ~(uint32_t)GrantTokenFlagsSingleUse) {
        return OS_EINVALPARAMS;
    }

    oserr = __GrantTokenValidateTarget(&token.Options);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = __GrantTokenReferenceResource(handleID, &token.Options, &sourceType);
    if (oserr != OS_EOK) {
        return oserr;
    }

    record = kmalloc(sizeof(struct __GrantTokenRecord));
    if (record == NULL) {
        ReleaseGlobalHandle(handleID);
        return OS_EOOM;
    }

    record->Options = token.Options;
    record->ResourceHandleID = handleID;
    record->ResourceHandleType = sourceType;
    record->Status = GrantTokenStatusActive;
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
    void*                      resource;
    oserr_t                    oserr;

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
GrantTokenImport(
    _In_  const GrantToken_t*      token,
    _In_  GrantTokenResourceType_t expectedType,
    _In_  GrantTokenScope_t        scope,
    _Out_ uuid_t*                  handleIDOut)
{
    struct __GrantTokenRecord* record;
    MSContextSecurity_t*       securityContext;
    GrantToken_t               tokenCopy;
    uuid_t                     resourceHandleID;
    void*                      resource;
    oserr_t                    oserr;

    if (token == NULL || handleIDOut == NULL) {
        return OS_EINVALPARAMS;
    }

    securityContext = MemorySpaceGetSecurityContext();
    if (securityContext == NULL) {
        return OS_ENOTSUPPORTED;
    }

    // Work on a local copy of the token from here on. The export ID tells us
    // which record to look up, and the token must be verified before we add
    // any reference to the resource.
    tokenCopy = *token;

    oserr = AcquireGlobalHandle(tokenCopy.ExportGlobalHandleID, HandleTypeGrantToken, &resource);
    if (oserr != OS_EOK) {
        return OS_EINVALPARAMS;
    }

    record = resource;

    oserr = OS_EINVALPARAMS;
    if (!__GrantTokenMatches(&tokenCopy, record)) {
        goto exit;
    }

    // The token must be for the resource type the caller expects, as the
    // caller will treat the returned handle as that type.
    if (record->Options.ResourceType != expectedType) {
        goto exit;
    }

    // Check the receiver and scope only after all token fields match the record.
    oserr = OS_EPERMISSIONS;
    if (record->Options.TargetType == GrantTokenTargetTypeSpecific &&
        record->Options.TargetGlobalHandleID != securityContext->GlobalHandleID) {
        goto exit;
    }

    if ((scope == 0 && expectedType == GrantTokenResourceTypeSHM) ||
        (scope & record->Options.Scope) != scope) {
        goto exit;
    }

    // Hold the export lock while we check the status and hand out the resource
    // reference, so a revoke or a second import cannot happen in between. If a
    // revoke happens after this point, our reference stays valid.
    SpinlockAcquireIrq(&record->Lock);
    oserr = OS_ECANCELLED;
    if (record->Status == GrantTokenStatusActive) {
        resourceHandleID = record->ResourceHandleID;
        if (record->Options.Flags & GrantTokenFlagsSingleUse) {
            // The export ends here. Instead of releasing its resource reference
            // we hand it to the caller, so no new reference is needed.
            record->Status = GrantTokenStatusRevoked;
            oserr = OS_EOK;
        } else {
            oserr = AcquireGlobalHandle(resourceHandleID, record->ResourceHandleType, NULL);
        }
    }
    SpinlockReleaseIrq(&record->Lock);
    if (oserr == OS_EOK) {
        *handleIDOut = resourceHandleID;
    }

exit:
    ReleaseGlobalHandle(tokenCopy.ExportGlobalHandleID);
    return oserr;
}
