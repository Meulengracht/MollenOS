/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * Kernel Module System
 *   - Implements loading and management of modules that exists on the initrd. 
 */

#define __MODULE "INRD"
//#define __TRACE

#include <modules/ramdisk.h>
#include <modules/manager.h>
#include <debug.h>
#include <crc32.h>

OsStatus_t
RamdiskParse(
    _In_ struct VBoot* bootInformation)
{
    SystemRamdiskHeader_t* ramdisk;
    SystemRamdiskEntry_t*  ramdiskEntry;
    int                    counter;
    SystemModuleType_t     moduleType;

    TRACE("RamdiskParse(address=0x%" PRIxIN ", size=0x%" PRIxIN ")",
          bootInformation->Ramdisk.Data, bootInformation->Ramdisk.Length);
    if (bootInformation->Ramdisk.Data == 0 || bootInformation->Ramdisk.Length == 0) {
        return OsSuccess; // no ramdisk, skip
    }
    
    // Initialize the pointer and read the signature value, must match
    ramdisk = (SystemRamdiskHeader_t*)(uintptr_t)bootInformation->Ramdisk.Data;
    if (ramdisk->Magic != RAMDISK_MAGIC) {
        ERROR("RamdiskParse Invalid magic - 0x%" PRIxIN "", ramdisk->Magic);
        return OsError;
    }

    if (ramdisk->Version != RAMDISK_VERSION_1) {
        ERROR("RamdiskParse Invalid version - 0x%" PRIxIN "", ramdisk->Version);
        return OsError;
    }

    ramdiskEntry = (SystemRamdiskEntry_t*)
        ((uintptr_t)bootInformation->Ramdisk.Data + sizeof(SystemRamdiskHeader_t));
    counter      = ramdisk->FileCount;

    // Keep iterating untill we reach the end of counter
    TRACE("Parsing %" PRIiIN " number of files in the ramdisk", counter);
    while (counter != 0) {
        if (ramdiskEntry->Type == RAMDISK_MODULE || ramdiskEntry->Type == RAMDISK_FILE) {
            TRACE("Entry %s type: %" PRIuIN "", &ramdiskEntry->Name[0], ramdiskEntry->Type);
            SystemRamdiskModuleHeader_t* moduleHeader =
                (SystemRamdiskModuleHeader_t*)(
                        (uintptr_t)bootInformation->Ramdisk.Data + (uintptr_t)ramdiskEntry->DataHeaderOffset);
            uint8_t* moduleData;
            uint32_t crcOfData;

            if (ramdiskEntry->Type == RAMDISK_FILE) {
                moduleType = FileResource;
            }
            else {
                if (moduleHeader->Flags & RAMDISK_MODULE_SERVER) {
                    moduleType = ServiceResource;
                }
                else {
                    moduleType = ModuleResource;
                }
            }

            // Perform CRC validation
            moduleData = (uint8_t*)((uintptr_t)bootInformation->Ramdisk.Data
                                    + ramdiskEntry->DataHeaderOffset + sizeof(SystemRamdiskModuleHeader_t));
            crcOfData  = Crc32Generate(-1, moduleData, moduleHeader->LengthOfData);
            if (crcOfData == moduleHeader->Crc32OfData) {
                if (RegisterModule((const char*)&ramdiskEntry->Name[0], (const void*)moduleData, moduleHeader->LengthOfData,
                                   moduleType, moduleHeader->VendorId, moduleHeader->DeviceId, moduleHeader->DeviceType, moduleHeader->DeviceSubType) != OsSuccess) {
                    // @todo ?
                    FATAL(FATAL_SCOPE_KERNEL, "failed to register module");
                }
            }
            else
            {
                ERROR("CRC-Validation(%s): Failed (Calculated 0x%" PRIxIN " != Stored 0x%" PRIxIN ")",
                      &ramdiskEntry->Name[0], crcOfData, moduleHeader->Crc32OfData);
                break;
            }
        }
        else {
            WARNING("Unknown entry type: %" PRIuIN "", ramdiskEntry->Type);
        }
        counter--;
        ramdiskEntry++;
    }
    return OsSuccess;
}
