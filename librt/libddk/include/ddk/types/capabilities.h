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
 * Capabilities Interface
 * - Defines the capabilities that can be granted to processes. Mostly this
 *   applies to privileged operations within the kernel, that drivers or 
 *   services may require.
 */

#ifndef __DDK_TYPES_CAPABILITIES_H__
#define __DDK_TYPES_CAPABILITIES_H__

typedef enum Capabilities {
    // Grant the capability to grant other processes capabilities.
    // This is a super privilege.
    CAPABILITY_GRANT_CAPABILITY       = 0x00000001U,
    // Grant the capability to query and access firmware resources.
    // This is either ACPI or DeviceTree resources.
    CAPABILITY_FIRMWARE_ACCESS        = 0x00000002U,
    // Grant the capability to access the kernel initramfs.
    CAPABILITY_RAMDISK_ACCESS         = 0x00000004U,
    // Grant the capability to access device I/O spaces.
    CAPABILITY_DEVICEIO_ACCESS        = 0x00000008U,
    // Grant the capability to register new device I/O spaces.
    CAPABILITY_DEVICEIO_MANAGEMENT    = (0x00000010U | CAPABILITY_DEVICEIO_ACCESS),
    // Grant the capability to allocate and manage interrupts.
    CAPABILITY_INTERRUPT_CONTROL      = 0x00000020U,
    // Grant the capability to create memory spaces.
    CAPABILITY_MEMORYSPACE_MANAGEMENT = 0x00000040U
} Capabilities_t;

// All defined capability bits. Unknown bits must not become latent privileges.
#define CAPABILITY_ALL (CAPABILITY_GRANT_CAPABILITY | CAPABILITY_FIRMWARE_ACCESS | \
    CAPABILITY_RAMDISK_ACCESS | CAPABILITY_DEVICEIO_MANAGEMENT | \
    CAPABILITY_INTERRUPT_CONTROL | CAPABILITY_MEMORYSPACE_MANAGEMENT)

#endif //!__DDK_TYPES_CAPABILITIES_H__
