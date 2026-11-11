/* MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * Process Type Definitions & Structures
 * - This header describes the base process-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __OS_INTERNAL_PROCESS_H__
#define __OS_INTERNAL_PROCESS_H__

#include <os/types/process.h>
#include <string.h>

/**
 * @brief Checks the header of a startup payload and finds its sections. The
 * process manager uses this to place the sections when writing the payload,
 * and the new process uses it to find them again when reading.
 * The header is copied out, so the payload does not need to be aligned.
 *
 * @param buffer      The payload, starting with a ProcessStartupInformation_t.
 * @param length      Number of payload bytes in buffer.
 * @param sectionsOut Receives the header and the section locations.
 * @return OS_EOK, or OS_EINVALPARAMS if the section lengths do not add up to
 *         exactly length.
 */
static inline oserr_t
__ProcessStartupSplit(
    _In_  void*                          buffer,
    _In_  size_t                         length,
    _Out_ struct ProcessStartupSections* sectionsOut)
{
    struct ProcessStartupSections sections;
    size_t                        remaining;

    if (length < sizeof(ProcessStartupInformation_t)) {
        return OS_EINVALPARAMS;
    }
    memcpy(&sections.Header, buffer, sizeof(ProcessStartupInformation_t));

    // Subtract one section at a time, so the lengths cannot overflow when added.
    remaining = length - sizeof(ProcessStartupInformation_t);
    if (sections.Header.ArgumentsLength > remaining) {
        return OS_EINVALPARAMS;
    }
    remaining -= sections.Header.ArgumentsLength;

    if (sections.Header.InheritationLength > remaining) {
        return OS_EINVALPARAMS;
    }
    remaining -= sections.Header.InheritationLength;

    if (sections.Header.LibraryEntriesLength > remaining) {
        return OS_EINVALPARAMS;
    }
    remaining -= sections.Header.LibraryEntriesLength;

    if (sections.Header.EnvironmentBlockLength != remaining) {
        return OS_EINVALPARAMS;
    }

    sections.Arguments = (char*)buffer + sizeof(ProcessStartupInformation_t);
    sections.Inheritance = sections.Arguments + sections.Header.ArgumentsLength;
    sections.LibraryEntries = sections.Inheritance + sections.Header.InheritationLength;
    sections.Environment = sections.LibraryEntries + sections.Header.LibraryEntriesLength;

    *sectionsOut = sections;
    return OS_EOK;
}

/**
 * @brief Checks an environment block and counts its entries. The block holds
 * zero-terminated "KEY=VALUE" strings followed by one extra zero, so an empty
 * string ends the list. That extra zero must be the last byte of the block.
 * A block of length 0 is valid and has no entries.
 *
 * @param block    The environment block. May be NULL when length is 0.
 * @param length   The exact length of the block in bytes.
 * @param countOut Receives the number of entries.
 * @return OS_EOK, or OS_EINVALPARAMS if the block does not end exactly at length.
 */
static inline oserr_t
__EnvironmentBlockValidate(
    _In_  const char* block,
    _In_  size_t      length,
    _Out_ size_t*     countOut)
{
    size_t offset = 0;
    size_t count = 0;
    size_t entryLength;

    if (length == 0) {
        *countOut = 0;
        return OS_EOK;
    }

    if (block == NULL) {
        return OS_EINVALPARAMS;
    }

    while (offset < length && block[offset] != '\0') {
        entryLength = strnlen(block + offset, length - offset);
        if (entryLength == length - offset) {
            return OS_EINVALPARAMS;
        }
        offset += entryLength + 1;
        count++;
    }

    // The empty string that ends the list must be the last byte.
    if (offset + 1 != length) {
        return OS_EINVALPARAMS;
    }

    *countOut = count;
    return OS_EOK;
}

#endif //!__OS_INTERNAL_PROCESS_H__
