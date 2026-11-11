/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __DDK_CONVERT_INHERITANCE_H__
#define __DDK_CONVERT_INHERITANCE_H__

#include <ddk/convert/grants.h>
#include <os/types/process.h>

/**
 * @brief Copies metadata for the child payload without copying its arrays.
 * The input must remain valid while the result is used. Only resources is
 * allocated separately by the caller; do not destroy the shared arrays.
 * @param input Decoded parent input.
 * @return Child metadata with no resource-ID array.
 */
static inline struct sys_process_inheritance_output
__InheritanceOutput(
    _In_ const struct sys_process_inheritance_input* input)
{
    return (struct sys_process_inheritance_output) {
        .resources_count = input->resources_count,
        .descriptors_count = input->descriptors_count,
        .descriptors = input->descriptors
    };
}

/**
 * @brief Returns the encoded length of the child inheritance block. Each array
 * is stored as a 32-bit count followed by its elements. A resource is one
 * 32-bit ID, and a descriptor is four 32-bit fields.
 * The result is computed in 64 bits, so any pair of 32-bit counts fits.
 * @param resources Number of entries in the resources array.
 * @param descriptors Number of entries in the descriptors array.
 * @return Encoded length in bytes.
 */
static inline uint64_t
__InheritanceOutputLength(
    _In_ uint32_t resources,
    _In_ uint32_t descriptors)
{
    return 2 * sizeof(uint32_t) +
           (uint64_t)resources * sizeof(uint32_t) +
           (uint64_t)descriptors * 4 * sizeof(uint32_t);
}

/**
 * @brief Checks decoded array counts, indexes and duplicate destinations.
 * Descriptor signatures, flags and device-specific state are not interpreted.
 * The counts must also give a child block that fits in a startup payload.
 * @param output Successfully decoded child metadata, or metadata from parent input.
 * @return OS_EOK or OS_EINVALPARAMS.
 */
static inline oserr_t
__ValidateInheritanceLayout(
    _In_ const struct sys_process_inheritance_output* output)
{
    if (output->descriptors_count == 0 ||
        output->resources_count == 0 ||
        output->resources_count > output->descriptors_count) {
        return OS_EINVALPARAMS;
    }

    // This also bounds the descriptor loops below.
    if (__InheritanceOutputLength(output->resources_count, output->descriptors_count) >
        PROCESS_STARTUP_MAX_SIZE) {
        return OS_EINVALPARAMS;
    }

    for (uint32_t i = 0; i < output->descriptors_count; i++) {
        if (output->descriptors[i].resource_index >= output->resources_count) {
            return OS_EINVALPARAMS;
        }
        for (uint32_t j = 0; j < i; j++) {
            if (output->descriptors[i].descriptor == output->descriptors[j].descriptor) {
                return OS_EINVALPARAMS;
            }
        }
    }

    for (uint32_t i = 0; i < output->resources_count; i++) {
        bool used = false;
        for (uint32_t j = 0; j < output->descriptors_count; j++) {
            used |= output->descriptors[j].resource_index == i;
        }
        if (!used) {
            return OS_EINVALPARAMS;
        }
    }
    return OS_EOK;
}

/**
 * @brief Decodes the bounded parent contract before any token is imported.
 * Checks layout and token policy, not libc descriptor semantics or access flags.
 * @param buffer Private input snapshot. May be NULL when empty.
 * @param length Exact section length.
 * @param inputOut Receives decoded arrays; destroy them after use. Empty on failure.
 * @return OS_EOK or a decoding, layout or token-policy error.
 */
static inline oserr_t
__ReadInheritanceInput(
    _In_  const void*                           buffer,
    _In_  uint32_t                              length,
    _Out_ struct sys_process_inheritance_input* inputOut)
{
    struct sys_process_inheritance_output metadata;
    gracht_buffer_t                       wire = { .data = (char*)buffer, .limit = length };
    GrantToken_t                          token;
    oserr_t                               status;

    memset(inputOut, 0, sizeof(struct sys_process_inheritance_input));
    
    if (length == 0) {
        return OS_EOK;
    }

    if (length > PROCESS_STARTUP_MAX_SIZE) {
        return OS_EBUFFER;
    }

    if (buffer == NULL) {
        return OS_EINVALPARAMS;
    }

    deserialize_sys_process_inheritance_input(&wire, inputOut);
    if (wire.error || wire.index != length) {
        status = wire.error == ENOMEM ? OS_EOOM : OS_EINVALPARAMS;
        goto error;
    }

    metadata = __InheritanceOutput(inputOut);
    status = __ValidateInheritanceLayout(&metadata);
    if (status != OS_EOK) {
        goto error;
    }

    for (uint32_t i = 0; i < inputOut->resources_count; i++) {
        status = __FromSysGrantToken(&inputOut->resources[i], &token);
        if (status != OS_EOK) {
            goto error;
        }

        if (token.Options.ResourceType != GrantTokenResourceTypeProvider &&
            token.Options.ResourceType != GrantTokenResourceTypeSHM) {
            status = OS_ENOTSUPPORTED;
            goto error;
        }

        if (token.Options.TargetType != GrantTokenTargetTypeAny ||
            token.Options.Flags != GrantTokenFlagsSingleUse) {
            status = OS_EINVALPARAMS;
            goto error;
        }

        for (uint32_t j = 0; j < i; j++) {
            if (inputOut->resources[j].export_id == token.ExportGlobalHandleID) {
                status = OS_EINVALPARAMS;
                goto error;
            }
        }
    }
    return OS_EOK;

error:
    sys_process_inheritance_input_destroy(inputOut);
    memset(inputOut, 0, sizeof(*inputOut));
    return status;
}

#endif //!__DDK_CONVERT_INHERITANCE_H__
