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
 * @brief Reads a 32 bit value from the pci-bus at the specified location bus, slot, function and register.
 */
__EXTERN uint32_t
PciRead32(
    _In_ PciHost_t*   Io,
    _In_ unsigned int Bus,
    _In_ unsigned int Slot,
    _In_ unsigned int Function,
    _In_ size_t       Register);

/**
 * @brief Reads a 8/16 bit value from the pci-bus at the specified location bus, device, function and register.
 */
__EXTERN uint16_t
PciRead16(
    _In_ PciHost_t*   Io,
    _In_ unsigned int Bus,
    _In_ unsigned int Device,
    _In_ unsigned int Function,
    _In_ size_t       Register);
__EXTERN uint8_t
PciRead8(
    _In_ PciHost_t*   Io,
    _In_ unsigned int Bus,
    _In_ unsigned int Device,
    _In_ unsigned int Function,
    _In_ size_t       Register);

/**
 * @brief Writes a 8/16/32 bit value to the pci-bus at the specified location bus, device, function and register.
 */
__EXTERN void
PciWrite32(
    _In_ PciHost_t*   Io,
    _In_ unsigned int Bus,
    _In_ unsigned int Device,
    _In_ unsigned int Function,
    _In_ size_t       Register,
    _In_ uint32_t     Value);
__EXTERN void
PciWrite16(
    _In_ PciHost_t*   Io,
    _In_ unsigned int Bus,
    _In_ unsigned int Device,
    _In_ unsigned int Function,
    _In_ size_t       Register,
    _In_ uint16_t     Value);
__EXTERN void
PciWrite8(
    _In_ PciHost_t*   Io,
    _In_ unsigned int Bus,
    _In_ unsigned int Device,
    _In_ unsigned int Function,
    _In_ size_t       Register,
    _In_ uint8_t      Value);

/**
 * @brief Read or writes a value of the given length from the given register of the specified PCI device.
 */
__EXTERN uint32_t
PciDeviceRead(
    _In_ struct PciDevice* Device,
    _In_ size_t            Register,
    _In_ size_t            Length);
__EXTERN void
PciDeviceWrite(
    _In_ struct PciDevice* Device,
    _In_ size_t            Register,
    _In_ uint32_t          Value,
    _In_ size_t            Length);

/**
 * @brief Reads the vendor id at given bus/device/function location.
 */
__EXTERN uint16_t
PciReadVendorId(
    _In_ PciHost_t*   Host,
    _In_ unsigned int Bus,
    _In_ unsigned int Device,
    _In_ unsigned int Function);

/**
 * @brief Reads in the pci header that exists at the given location and fills out the information into <Pcs>.
 */
__EXTERN void
PciReadFunction(
    _InOut_ PciNativeHeader_t* Pcs,
    _In_    PciHost_t*         Host,
    _In_    unsigned int       Bus,
    _In_    unsigned int       Device,
    _In_    unsigned int       Function);

/**
 * @brief Reads the secondary bus number at given pci device location. This can be used to get the bus-number behind a bridge.
 */
__EXTERN uint8_t
PciReadSecondaryBusNumber(
    _In_ PciHost_t*   Host,
    _In_ unsigned int Bus,
    _In_ unsigned int Device,
    _In_ unsigned int Function);

/**
 * @brief Reads the sub class at given location.
 * Bit 7 - MultiFunction, Lower 4 bits is type.
 * Type 0 is standard, Type 1 is PCI-PCI Bridge,
 * Type 2 is CardBus Bridge.
 */
__EXTERN uint8_t
PciReadHeaderType(
    _In_ PciHost_t*   Host,
    _In_ unsigned int Bus,
    _In_ unsigned int Device,
    _In_ unsigned int Function);

#endif // __DEVICED_BUS_PCI_CONFIG_H__
