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

#ifndef __DDK_CONVERT_GRANTS_H__
#define __DDK_CONVERT_GRANTS_H__

#include <os/types/grants.h>
#include <os/handle.h>
#include <os/shm.h>
#include <string.h>
#include <sys_process_service.h>

// Gracht writes scalar fields in host byte order. Grant contracts require
// little-endian fields, which all current Vali targets use.
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error Grant contracts require little-endian Gracht scalar serialization
#endif

static inline uint32_t
__GrantSecretWord(
    _In_ const uint8_t* bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static inline void
__GrantSecretBytes(
    _In_  uint32_t word,
    _Out_ uint8_t* bytes)
{
    bytes[0] = (uint8_t)word;
    bytes[1] = (uint8_t)(word >> 8);
    bytes[2] = (uint8_t)(word >> 16);
    bytes[3] = (uint8_t)(word >> 24);
}

static inline bool
__GrantTokenPolicyValid(
    _In_ const GrantTokenOptions_t* options)
{
    unsigned int knownFlags = GrantTokenScopeRead | GrantTokenScopeWrite;

    if (options->Flags & ~(uint32_t)GrantTokenFlagsSingleUse) {
        return false;
    }

    switch (options->ResourceType) {
        case GrantTokenResourceTypeSHM:
            return options->Scope != 0 &&
                   ((unsigned int)options->Scope & ~knownFlags) == 0 &&
                   options->Data.SHM.Length != 0;
        case GrantTokenResourceTypeProvider:
            return options->Data.Words[0] == 0 && options->Data.Words[1] == 0;
        default:
            return false;
    }
}

static inline bool
__GrantTokenFieldsValid(
    _In_ const GrantToken_t* token)
{
    if (token->Version != GRANT_TOKEN_VERSION) {
        return false;
    }

    if (token->ExportGlobalHandleID == UUID_INVALID ||
        token->ControlHandleID != token->ExportGlobalHandleID) {
        return false;
    }

    if (!__GrantTokenPolicyValid(&token->Options)) {
        return false;
    }

    if (token->Options.TargetType == GrantTokenTargetTypeAny) {
        return token->Options.TargetGlobalHandleID == UUID_INVALID;
    }
    
    if (token->Options.TargetType == GrantTokenTargetTypeSpecific) {
        return token->Options.TargetGlobalHandleID != UUID_INVALID;
    }
    return false;
}

static inline bool
__SHMTransferSelectorValid(
    _In_ const SHMTransfer_t* transfer)
{
    if (transfer->Token.Options.ResourceType != GrantTokenResourceTypeSHM) {
        return false;
    }

    if (transfer->RangeMode == SHMTransferRangeFull) {
        return transfer->RelativeOffset == 0 && transfer->RequestedLength == 0;
    }

    if (transfer->RangeMode != SHMTransferRangeSubrange) {
        return false;
    }

    if (transfer->RequestedLength == 0 ||
        transfer->RelativeOffset > transfer->Token.Options.Data.SHM.Length) {
        return false;
    }

    // Subtract before comparing so that offset + length cannot wrap.
    return transfer->RequestedLength <=
           transfer->Token.Options.Data.SHM.Length - transfer->RelativeOffset;
}

/**
 * @brief Returns the selected byte length. Selector fields must be valid.
 * @param transfer Validated transfer description.
 * @return Selected byte length.
 */
static inline size_t
__SHMTransferLength(
    _In_ const SHMTransfer_t* transfer)
{
    return transfer->RangeMode == SHMTransferRangeFull ?
        transfer->Token.Options.Data.SHM.Length : transfer->RequestedLength;
}

/**
 * @brief Checks required access before attaching a validated transfer.
 * @param transfer Validated transfer description.
 * @param scope Required token access.
 * @param buffer Receives the attachment on success.
 * @return OS_EOK, OS_EPERMISSIONS, or an attachment error.
 */
static inline oserr_t
__SHMTransferAttach(
    _In_  const SHMTransfer_t* transfer,
    _In_  GrantTokenScope_t   scope,
    _Out_ OSHandle_t*         buffer)
{
    if ((transfer->Token.Options.Scope & scope) != scope) {
        return OS_EPERMISSIONS;
    }
    return SHMAttachToken(&transfer->Token, buffer);
}

/**
 * @brief Attaches and maps the selected bytes of a validated transfer.
 * @param transfer Validated transfer description.
 * @param access SHM_ACCESS_READ and/or SHM_ACCESS_WRITE.
 * @param buffer Receives the mapped attachment. Destroy it after use.
 * @return OS_EOK or an access, attachment or mapping error.
 */
static inline oserr_t
__SHMTransferMap(
    _In_  const SHMTransfer_t* transfer,
    _In_  unsigned int         access,
    _Out_ OSHandle_t*          buffer)
{
    OSHandle_t        mapping;
    GrantTokenScope_t scope = 0;
    oserr_t           status;

    if (access & SHM_ACCESS_READ) {
        scope |= GrantTokenScopeRead;
    }

    if (access & SHM_ACCESS_WRITE) {
        scope |= GrantTokenScopeWrite;
    }

    status = __SHMTransferAttach(transfer, scope, &mapping);
    if (status != OS_EOK) {
        return status;
    }

    status = SHMMap(
        &mapping,
        transfer->RelativeOffset,
        __SHMTransferLength(transfer),
        access
    );
    if (status != OS_EOK) {
        OSHandleDestroy(&mapping);
        return status;
    }

    *buffer = mapping;
    return OS_EOK;
}

/**
 * @brief Copies the first bytes of a validated transfer into local memory.
 * The transfer is mapped for reading only while the bytes are copied.
 * @param transfer Validated transfer description.
 * @param buffer Receives the copied bytes.
 * @param length Number of bytes to copy. Must not exceed __SHMTransferLength(transfer).
 * @return OS_EOK or an access, attachment or mapping error.
 */
static inline oserr_t
__SHMTransferRead(
    _In_  const SHMTransfer_t* transfer,
    _Out_ void*                buffer,
    _In_  size_t               length)
{
    OSHandle_t mapping;
    oserr_t    status;

    status = __SHMTransferMap(transfer, SHM_ACCESS_READ, &mapping);
    if (status != OS_EOK) {
        return status;
    }

    memcpy(buffer, SHMBuffer(&mapping), length);
    OSHandleDestroy(&mapping);
    return OS_EOK;
}

/**
 * @brief Copies local memory into the first bytes of a validated transfer.
 * The transfer is mapped for reading and writing only while the bytes are copied.
 * @param transfer Validated transfer description.
 * @param buffer The bytes to copy.
 * @param length Number of bytes to copy. Must not exceed __SHMTransferLength(transfer).
 * @return OS_EOK or an access, attachment or mapping error.
 */
static inline oserr_t
__SHMTransferWrite(
    _In_ const SHMTransfer_t* transfer,
    _In_ const void*          buffer,
    _In_ size_t               length)
{
    OSHandle_t mapping;
    oserr_t    status;

    status = __SHMTransferMap(transfer, SHM_ACCESS_READ | SHM_ACCESS_WRITE, &mapping);
    if (status != OS_EOK) {
        return status;
    }

    memcpy(SHMBuffer(&mapping), buffer, length);
    OSHandleDestroy(&mapping);
    return OS_EOK;
}

/**
 * @brief Converts a native token to the Gracht contract type.
 * ControlHandleID must equal ExportGlobalHandleID. The field values are checked
 * before conversion. The kernel must still verify the secret and export record.
 * Gracht serializes the converted fields.
 *
 * @param in Native token to convert.
 * @param out Receives the contract token on success. Unchanged on failure.
 * @return OS_EOK or OS_EINVALPARAMS for invalid arguments or token fields.
 */
static inline oserr_t
__ToSysGrantToken(
    _In_  const GrantToken_t*     in,
    _Out_ struct sys_grant_token* out)
{
    struct sys_grant_token token = { 0 };

    if (in == NULL || out == NULL) {
        return OS_EINVALPARAMS;
    }

    if (!__GrantTokenFieldsValid(in)) {
        return OS_EINVALPARAMS;
    }

    token.version = in->Version;
    token.export_id = in->ExportGlobalHandleID;
    token.resource_type = (uint32_t)in->Options.ResourceType;
    token.data0 = in->Options.Data.Words[0];
    token.data1 = in->Options.Data.Words[1];
    token.target_id = in->Options.TargetGlobalHandleID;
    token.scope = (uint32_t)in->Options.Scope;
    token.target_type = (uint32_t)in->Options.TargetType;
    token.flags = in->Options.Flags;
    token.secret0 = __GrantSecretWord(in->Secret);
    token.secret1 = __GrantSecretWord(in->Secret + 4);
    token.secret2 = __GrantSecretWord(in->Secret + 8);
    token.secret3 = __GrantSecretWord(in->Secret + 12);

    *out = token;
    return OS_EOK;
}

/**
 * @brief Converts a decoded Gracht token to the native type and checks its fields.
 * Gracht decoding must have succeeded before this is called. The kernel must still
 * verify the converted token before any resource access is allowed.
 * ControlHandleID is set from export_id. Revoking the export still requires a grant
 * for that handle.
 *
 * @param in Decoded contract token.
 * @param out Receives the native token on success. Unchanged on failure.
 * @return OS_EOK, or OS_EINVALPARAMS for invalid arguments or token fields.
 */
static inline oserr_t
__FromSysGrantToken(
    _In_  const struct sys_grant_token* in,
    _Out_ GrantToken_t*                out)
{
    GrantToken_t token = { 0 };

    if (in == NULL || out == NULL) {
        return OS_EINVALPARAMS;
    }

    token.Version = in->version;
    token.ExportGlobalHandleID = in->export_id;
    token.ControlHandleID = in->export_id;
    token.Options.ResourceType = (GrantTokenResourceType_t)in->resource_type;
    token.Options.Data.Words[0] = in->data0;
    token.Options.Data.Words[1] = in->data1;
    token.Options.TargetGlobalHandleID = in->target_id;
    token.Options.Scope = (GrantTokenScope_t)in->scope;
    token.Options.TargetType = (GrantTokenTargetType_t)in->target_type;
    token.Options.Flags = in->flags;
    __GrantSecretBytes(in->secret0, token.Secret);
    __GrantSecretBytes(in->secret1, token.Secret + 4);
    __GrantSecretBytes(in->secret2, token.Secret + 8);
    __GrantSecretBytes(in->secret3, token.Secret + 12);

    if (!__GrantTokenFieldsValid(&token)) {
        return OS_EINVALPARAMS;
    }

    *out = token;
    return OS_EOK;
}

/**
 * @brief Converts an SHM transfer to the Gracht contract type.
 * In full-range mode, RelativeOffset and RequestedLength must both be zero.
 * In subrange mode, the length must be nonzero and the range must fit within
 * Token.Options.Data.SHM.Length.
 * The requested range does not change the range or access flags of the token.
 *
 * @param in Native transfer description.
 * @param out Receives the contract description on success. Unchanged on failure.
 * @return OS_EOK, or OS_EINVALPARAMS for invalid arguments, token fields or range.
 */
static inline oserr_t
__ToSysSHMTransfer(
    _In_  const SHMTransfer_t*     in,
    _Out_ struct sys_shm_transfer* out)
{
    struct sys_shm_transfer transfer = { 0 };
    oserr_t                 oserr;

    if (in == NULL || out == NULL) {
        return OS_EINVALPARAMS;
    }

    if (!__SHMTransferSelectorValid(in)) {
        return OS_EINVALPARAMS;
    }

    oserr = __ToSysGrantToken(&in->Token, &transfer.token);
    if (oserr != OS_EOK) {
        return oserr;
    }

    transfer.range_mode = (uint32_t)in->RangeMode;
    transfer.relative_offset = in->RelativeOffset;
    transfer.requested_length = in->RequestedLength;

    *out = transfer;
    return OS_EOK;
}

/**
 * @brief Converts a decoded SHM transfer and checks its token fields and range.
 * Gracht decoding must have succeeded before this is called. The kernel must still
 * verify the token. In full-range mode, RelativeOffset and RequestedLength stay zero.
 * When the transfer is executed, use offset zero and a length of
 * Token.Options.Data.SHM.Length within the attachment.
 *
 * @param in Decoded contract description.
 * @param out Receives the native description on success. Unchanged on failure.
 * @return OS_EOK, or OS_EINVALPARAMS for invalid arguments, token fields or range.
 */
static inline oserr_t
__FromSysSHMTransfer(
    _In_  const struct sys_shm_transfer* in,
    _Out_ SHMTransfer_t*                 out)
{
    SHMTransfer_t transfer = { 0 };
    oserr_t       oserr;

    if (in == NULL || out == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = __FromSysGrantToken(&in->token, &transfer.Token);
    if (oserr != OS_EOK) {
        return oserr;
    }

    transfer.RangeMode = (SHMTransferRangeMode_t)in->range_mode;
    transfer.RelativeOffset = in->relative_offset;
    transfer.RequestedLength = in->requested_length;

    if (!__SHMTransferSelectorValid(&transfer)) {
        return OS_EINVALPARAMS;
    }
    
    *out = transfer;
    return OS_EOK;
}

/**
 * @brief Checks whether two transfer descriptions are equal. Used to detect a
 * registration that is retried with the same request.
 * Only the contract fields are compared. The kernel still verifies the token.
 *
 * @param left First transfer description.
 * @param right Second transfer description.
 * @return true if the token, range mode, offset and length all match.
 */
static inline bool
__SameSysSHMTransfer(
    _In_ const struct sys_shm_transfer* left,
    _In_ const struct sys_shm_transfer* right)
{
    return left->token.version == right->token.version &&
           left->token.export_id == right->token.export_id &&
           left->token.resource_type == right->token.resource_type &&
           left->token.data0 == right->token.data0 &&
           left->token.data1 == right->token.data1 &&
           left->token.target_id == right->token.target_id &&
           left->token.scope == right->token.scope &&
           left->token.target_type == right->token.target_type &&
           left->token.flags == right->token.flags &&
           left->token.secret0 == right->token.secret0 &&
           left->token.secret1 == right->token.secret1 &&
           left->token.secret2 == right->token.secret2 &&
           left->token.secret3 == right->token.secret3 &&
           left->range_mode == right->range_mode &&
           left->relative_offset == right->relative_offset &&
           left->requested_length == right->requested_length;
}

#endif //!__DDK_CONVERT_GRANTS_H__
