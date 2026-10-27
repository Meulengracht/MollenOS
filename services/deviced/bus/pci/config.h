/**
 * MollenOS
 *
 * Copyright 2015, Philip Meulengracht
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

#ifndef __DEVICED_BUS_PCI_CONFIG_H__
#define __DEVICED_BUS_PCI_CONFIG_H__

#include <bus/pci/host.h>
#include <bus/pci/registers.h>

struct PciDevice;

/**
 * @brief Reads a 32-bit PCI configuration register.
 *
 * @param Io Initialized host containing the selected bus.
 * @param Bus Bus number within the host bus range.
 * @param Slot Device slot number from 0 through 31.
 * @param Function Function number from 0 through 7.
 * @param Register Byte offset within configuration space, aligned to 4 bytes.
 * @return Register value, or all bits set if the access is invalid.
 */
__EXTERN uint32_t
PciRead32(
    _In_ PciHost_t*   Io,
    _In_ unsigned int Bus,
    _In_ unsigned int Slot,
    _In_ unsigned int Function,
    _In_ size_t       Register);

/**
 * @brief Reads a 16-bit PCI configuration register.
 *
 * @param Io Initialized host containing the selected bus.
 * @param Bus Bus number within the host bus range.
 * @param Device Device slot number from 0 through 31.
 * @param Function Function number from 0 through 7.
 * @param Register Byte offset within configuration space, aligned to 2 bytes.
 * @return Register value, or all bits set if the access is invalid.
 */
__EXTERN uint16_t
PciRead16(
    _In_ PciHost_t*   Io,
    _In_ unsigned int Bus,
    _In_ unsigned int Device,
    _In_ unsigned int Function,
    _In_ size_t       Register);

/**
 * @brief Reads an 8-bit PCI configuration register.
 *
 * @param Io Initialized host containing the selected bus.
 * @param Bus Bus number within the host bus range.
 * @param Device Device slot number from 0 through 31.
 * @param Function Function number from 0 through 7.
 * @param Register Byte offset within configuration space.
 * @return Register value, or all bits set if the access is invalid.
 */
__EXTERN uint8_t
PciRead8(
    _In_ PciHost_t*   Io,
    _In_ unsigned int Bus,
    _In_ unsigned int Device,
    _In_ unsigned int Function,
    _In_ size_t       Register);

/**
 * @brief Writes a 32-bit PCI configuration register.
 *
 * @param Io Initialized host containing the selected bus.
 * @param Bus Bus number within the host bus range.
 * @param Device Device slot number from 0 through 31.
 * @param Function Function number from 0 through 7.
 * @param Register Byte offset within configuration space, aligned to 4 bytes.
 * @param Value Value to write.
 * @return No value. Invalid accesses leave configuration space unchanged.
 */
__EXTERN void
PciWrite32(
    _In_ PciHost_t*   Io,
    _In_ unsigned int Bus,
    _In_ unsigned int Device,
    _In_ unsigned int Function,
    _In_ size_t       Register,
    _In_ uint32_t     Value);

/**
 * @brief Writes a 16-bit PCI configuration register.
 *
 * @param Io Initialized host containing the selected bus.
 * @param Bus Bus number within the host bus range.
 * @param Device Device slot number from 0 through 31.
 * @param Function Function number from 0 through 7.
 * @param Register Byte offset within configuration space, aligned to 2 bytes.
 * @param Value Value to write.
 * @return No value. Invalid accesses leave configuration space unchanged.
 */
__EXTERN void
PciWrite16(
    _In_ PciHost_t*   Io,
    _In_ unsigned int Bus,
    _In_ unsigned int Device,
    _In_ unsigned int Function,
    _In_ size_t       Register,
    _In_ uint16_t     Value);

/**
 * @brief Writes an 8-bit PCI configuration register.
 *
 * @param Io Initialized host containing the selected bus.
 * @param Bus Bus number within the host bus range.
 * @param Device Device slot number from 0 through 31.
 * @param Function Function number from 0 through 7.
 * @param Register Byte offset within configuration space.
 * @param Value Value to write.
 * @return No value. Invalid accesses leave configuration space unchanged.
 */
__EXTERN void
PciWrite8(
    _In_ PciHost_t*   Io,
    _In_ unsigned int Bus,
    _In_ unsigned int Device,
    _In_ unsigned int Function,
    _In_ size_t       Register,
    _In_ uint8_t      Value);

/**
 * @brief Reads a configuration register using the device host and bus address.
 *
 * @param Device Device with an initialized host and bus address.
 * @param Register Byte offset within configuration space, aligned to Length.
 * @param Length Access size in bytes. Callers must pass 1, 2, or 4.
 * @return Register value, or all bits set in the selected width for an invalid access.
 */
__EXTERN uint32_t
PciDeviceRead(
    _In_ struct PciDevice* Device,
    _In_ size_t            Register,
    _In_ size_t            Length);

/**
 * @brief Writes a configuration register using the device host and bus address.
 *
 * @param Device Device with an initialized host and bus address.
 * @param Register Byte offset within configuration space, aligned to Length.
 * @param Value Value to write. Only the selected width is written.
 * @param Length Access size in bytes. Callers must pass 1, 2, or 4.
 * @return No value. Invalid accesses leave configuration space unchanged.
 */
__EXTERN void
PciDeviceWrite(
    _In_ struct PciDevice* Device,
    _In_ size_t            Register,
    _In_ uint32_t          Value,
    _In_ size_t            Length);

/**
 * @brief Reads the vendor ID at the selected PCI address.
 *
 * @param Host Initialized host containing the selected bus.
 * @param Bus Bus number within the host bus range.
 * @param Device Device slot number from 0 through 31.
 * @param Function Function number from 0 through 7.
 * @return Vendor ID, or 0xFFFF if the access is invalid or no function responds.
 */
__EXTERN uint16_t
PciReadVendorId(
    _In_ PciHost_t*   Host,
    _In_ unsigned int Bus,
    _In_ unsigned int Device,
    _In_ unsigned int Function);

/**
 * @brief Copies the first 64 bytes of configuration space into the native header.
 *
 * @param Pcs Header storage. Unchanged when the vendor ID is zero or 0xFFFF.
 * @param Host Initialized host containing the selected bus.
 * @param Bus Bus number within the host bus range.
 * @param Device Device slot number from 0 through 31.
 * @param Function Function number from 0 through 7.
 * @return No value.
 */
__EXTERN void
PciReadFunction(
    _InOut_ PciNativeHeader_t* Pcs,
    _In_    PciHost_t*         Host,
    _In_    unsigned int       Bus,
    _In_    unsigned int       Device,
    _In_    unsigned int       Function);

/**
 * @brief Reads the bus number on the secondary side of a PCI bridge.
 *
 * The selected function must use a bridge header.
 *
 * @param Host Initialized host containing the selected bus.
 * @param Bus Bus number within the host bus range.
 * @param Device Device slot number from 0 through 31.
 * @param Function Function number from 0 through 7.
 * @return Secondary bus number, or 0xFF when the vendor ID is zero or 0xFFFF.
 */
__EXTERN uint8_t
PciReadSecondaryBusNumber(
    _In_ PciHost_t*   Host,
    _In_ unsigned int Bus,
    _In_ unsigned int Device,
    _In_ unsigned int Function);

/**
 * @brief Reads the header type and multifunction flag.
 *
 * Bit 7 indicates a multifunction device. Bits 6 through 0 give the header
 * type: 0 for a standard device, 1 for a PCI-to-PCI bridge, or 2 for CardBus.
 *
 * @param Host Initialized host containing the selected bus.
 * @param Bus Bus number within the host bus range.
 * @param Device Device slot number from 0 through 31.
 * @param Function Function number from 0 through 7.
 * @return Header type byte, or 0xFF when the vendor ID is zero or 0xFFFF.
 */
__EXTERN uint8_t
PciReadHeaderType(
    _In_ PciHost_t*   Host,
    _In_ unsigned int Bus,
    _In_ unsigned int Device,
    _In_ unsigned int Function);

#endif // __DEVICED_BUS_PCI_CONFIG_H__
