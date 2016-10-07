/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS MCore - Serial ATA Header
*/

#ifndef _SATA_H_
#define _SATA_H_

/* Includes */
#include <os/osdefs.h>

/* The SATA specs specify these kinds of
* FIS (Frame Information Structure) */
typedef enum _AHCIFisType
{
	/* Register FIS - Host To Device */
	FISRegisterH2D = 0x27,

	/* Register FIS - Device To Host */
	FISRegisterD2H = 0x34,

	/* DMA Activate FIS - Device To Host */
	FISDmaActivate = 0x39,

	/* DMA Setup FIS - Bidirectional */
	FISDmaSetup = 0x41,

	/* Data FIS - Bidirectional */
	FISData = 0x46,

	/* BIST Activate FIS - Bidirectional */
	FISBistActivate = 0x58,

	/* PIO Setup FIS - Device To Host */
	FISPioSetup = 0x5F,

	/* Set device bits FIS - Device To Host */
	FISDeviceBits = 0xA1

} AHCIFisType_t;

/* The FISRegisterH2D structure 
 * as described in the SATA Specification */
#pragma pack(push, 1)
typedef struct _SATAFISRegisterH2D
{
	/* Descriptor Type 
	 * - All FIS start with this */
	uint8_t	FISType;

	/* Flags 
	 * Bits 0-3: Port Multiplier
	 * Bits 4-6: Reserved, should be 0
	 * Bit 7: (1) for Command, (0) for Control */
	uint8_t Flags;

	/* Command Register */
	uint8_t Command;
	
	/* Features 0:7 */
	uint8_t FeaturesLow;

	/* Lba Register 0:15 */
	uint16_t Lba0;

	/* Lba Register 16:23 */
	uint8_t	Lba1;

	/* Device Register */
	uint8_t	Device;

	/* Lba Register 24:39 */
	uint16_t Lba2;

	/* Lba Register 40:47 */
	uint8_t Lba3;

	/* Features 8:15 */
	uint8_t	FeaturesHigh;

	/* Count Register */
	uint16_t Count;

	/* Isochronous Command Completion */
	uint8_t Icc;

	/* Control Register */
	uint8_t Control;

	/* Reserved */
	uint32_t Reserved;

} FISRegisterH2D_t;
#pragma pack(pop)

/* The FISRegisterD2H structure 
 * as described in the SATA Specification */
#pragma pack(push, 1)
typedef struct _SATAFISRegisterD2H
{
	/* Descriptor Type
	 * - All FIS start with this */
	uint8_t	FISType;

	/* Flags 
	 * Bits 0-3: Port Multiplier
	 * Bits 4-5: Reserved, should be 0
	 * Bit 6: Interrupt Bit 
	 * Bit 7: Reserved, should be 0 */
	uint8_t Flags;

	/* Status */
	uint8_t Status;
	
	/* Error */
	uint8_t Error;

	/* Lba Register 0:15 */
	uint16_t Lba0;

	/* Lba Register 16:23 */
	uint8_t	Lba1;

	/* Device Register */
	uint8_t	Device;

	/* Lba Register 24:39 */
	uint16_t Lba2;

	/* Lba Register 40:47 */
	uint8_t Lba3;

	/* Reserved */
	uint8_t Reserved0;

	/* Count Register */
	uint16_t Count;

	/* Reserved */
	uint16_t Reserved1;

	/* DWORD 4: Reserved */
	uint32_t Reserved2;

} FISRegisterD2H_t;
#pragma pack(pop)

/* Data FIS – Bidirectional 
 * This FIS is used by the host or device to send data payload. 
 * The data size can be varied. */
#pragma pack(push, 1)
typedef struct _SATAFISData
{
	/* Descriptor Type
	 * - All FIS start with this */
	uint8_t	FISType;

	/* Flags 
	 * Bits 0-3: Port Multiplier
	 * Bits 4-7: Reserved, should be 0 */
	uint8_t Flags;

	/* Reserved */
	uint16_t Reserved;

	/* Data, 1 - N */
	uint32_t Payload[1];

} FISData_t;
#pragma pack(pop)

/* PIO Setup – Device to Host
 * This FIS is used by the device to tell the host that it’s about to send 
 * or ready to receive a PIO data payload. */
#pragma pack(push, 1)
typedef struct _SATAFISPioSetup
{
	/* Descriptor Type
	 * - All FIS start with this */
	uint8_t	FISType;

	/* Flags 
	 * Bits 0-3: Port Multiplier
	 * Bit 4: Reserved, should be 0
	 * Bit 5: Data transfer direction, 1 - device to host, 0 - host to device
	 * Bit 6: Interrupt Bit 
	 * Bit 7: Reserved, should be 0 */
	uint8_t Flags;

	/* Status */
	uint8_t Status;

	/* Error */
	uint8_t Error;

	/* Lba Register 0:15 */
	uint16_t Lba0;

	/* Lba Register 16:23 */
	uint8_t	Lba1;

	/* Device Register */
	uint8_t	Device;

	/* Lba Register 24:39 */
	uint16_t Lba2;

	/* Lba Register 40:47 */
	uint8_t Lba3;

	/* Reserved */
	uint8_t Reserved0;

	/* Count Register */
	uint16_t Count;

	/* Reserved */
	uint8_t Reserved1;

	/* New value of status register */
	uint8_t UpdatedStatus;

	/* Transfer Count */
	uint16_t TransferCount;

	/* Reserved */
	uint16_t Reserved2;

} FISPioSetup_t;
#pragma pack(pop)

/* The FISDmaSetup structure 
 * as described in the SATA Specification */
#pragma pack(push, 1)
typedef struct _SATAFISDmaSetup
{
	/* Descriptor Type
	 * - All FIS start with this */
	uint8_t	FISType;

	/* Flags 
	 * Bits 0-3: Port Multiplier
	 * Bit 4: Reserved, should be 0
	 * Bit 5: Data transfer direction, 1 - device to host, 0 - host to device
	 * Bit 6: Interrupt Bit 
	 * Bit 7: Auto-activate. Specifies if DMA Activate FIS is needed */
	uint8_t Flags;

	/* Reserved */
	uint16_t Reserved0;

	/* DMA Buffer Identifier. Used to Identify DMA buffer in host memory. 
	 * SATA Spec says host specific and not in Spec. Trying AHCI spec might work. */
	uint64_t DmaBufferId;

	/* Reserved */
	uint32_t Reserved1;

	/* Dma Offset - Byte offset into buffer. First 2 bits must be 0 */
	uint32_t DmaBufferOffset;

	/* Transfer Count - Number of bytes to transfer. Bit 0 must be 0 */
	uint32_t TransferCount;

	/* Reserved, again */
	uint32_t Reserved2;

} FISDmaSetup_t;
#pragma pack(pop)

/* The FISDeviceBits structure 
 * as described in the SATA Specification */
#pragma pack(push, 1)
typedef struct _SATAFISDeviceBits
{
	/* Descriptor Type
	 * - All FIS start with this */
	uint8_t	FISType;

	/* Uh, flags */
	uint8_t Flags;

	/* Status */
	uint8_t Status;

	/* Error */
	uint8_t Error;

	/* Reserved */
	uint32_t Reserved;

} FISDeviceBits_t;
#pragma pack(pop)

#endif //!_SATA_H_
