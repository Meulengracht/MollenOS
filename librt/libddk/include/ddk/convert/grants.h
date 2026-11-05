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
__GrantTokenFieldsValid(
    _In_ const GrantToken_t* token)
{
    unsigned int knownFlags = GrantTokenScopeRead | GrantTokenScopeWrite;

    if (token->Version != GRANT_TOKEN_VERSION) {
        return false;
    }
    if (token->ExportGlobalHandleID == UUID_INVALID ||
        token->ControlHandleID != token->ExportGlobalHandleID) {
        return false;
    }
    if (token->Options.Scope == 0 ||
        ((unsigned int)token->Options.Scope & ~knownFlags) != 0) {
        return false;
    }
    if (token->Options.Length == 0) {
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
    if (transfer->RangeMode == SHMTransferRangeFull) {
        return transfer->RelativeOffset == 0 && transfer->RequestedLength == 0;
    }
    if (transfer->RangeMode != SHMTransferRangeSubrange) {
        return false;
    }
    if (transfer->RequestedLength == 0 ||
        transfer->RelativeOffset > transfer->Token.Options.Length) {
        return false;
    }

    // Subtract before comparing so that offset + length cannot wrap.
    return transfer->RequestedLength <=
           transfer->Token.Options.Length - transfer->RelativeOffset;
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
    token.buffer_offset = in->Options.Offset;
    token.buffer_length = in->Options.Length;
    token.target_id = in->Options.TargetGlobalHandleID;
    token.scope = (uint32_t)in->Options.Scope;
    token.target_type = (uint32_t)in->Options.TargetType;
    token.secret0 = __GrantSecretWord(in->Secret);
    token.secret1 = __GrantSecretWord(in->Secret + 4);
    token.secret2 = __GrantSecretWord(in->Secret + 8);
    token.secret3 = __GrantSecretWord(in->Secret + 12);
    *out = token;
    return OS_EOK;
}

/**
 * @brief Converts a decoded Gracht token to the native type and checks its fields.
 * Gracht decoding must have succeeded before this call. The kernel must verify
 * the converted token before allowing buffer access. ControlHandleID is copied
 * from export_id. Revoking the export still requires a grant for that handle.
 *
 * @param in Decoded contract token.
 * @param out Receives the native token on success. Unchanged on failure.
 * @return OS_EOK or OS_EINVALPARAMS for invalid arguments or token fields.
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
    token.Options.Offset = in->buffer_offset;
    token.Options.Length = in->buffer_length;
    token.Options.TargetGlobalHandleID = in->target_id;
    token.Options.Scope = (GrantTokenScope_t)in->scope;
    token.Options.TargetType = (GrantTokenTargetType_t)in->target_type;
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
 * A subrange must have a nonzero length and fit within Token.Options.Length.
 * The requested range leaves the token's range and access flags unchanged.
 *
 * @param in Native transfer description.
 * @param out Receives the contract description on success. Unchanged on failure.
 * @return OS_EOK or OS_EINVALPARAMS for invalid arguments, token fields or range.
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
 * Gracht decoding must have succeeded before this call. The kernel must still
 * verify the token. In full-range mode, RelativeOffset and RequestedLength stay
 * zero. Use offset zero and Token.Options.Length within the attachment when
 * executing the transfer.
 *
 * @param in Decoded contract description.
 * @param out Receives the native description on success. Unchanged on failure.
 * @return OS_EOK or OS_EINVALPARAMS for invalid arguments, token fields or range.
 */
static inline oserr_t
__FromSysSHMTransfer(
    _In_  const struct sys_shm_transfer* in,
    _Out_ SHMTransfer_t*                out)
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

#endif //!__DDK_CONVERT_GRANTS_H__
