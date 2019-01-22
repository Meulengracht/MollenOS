/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS - Serial-ATA Header
 * - Contains shared definitions for SATA that can
 *   be used by all storage drivers that implement the SATA protocol
 */

#ifndef _SATA_H_
#define _SATA_H_

#include <os/osdefs.h>

/* SATA Device Signatures
 * Used to identify which kind of device is attached to a SATA port */
#define	SATA_SIGNATURE_ATA		0x00000101
#define	SATA_SIGNATURE_ATAPI	0xEB140101
#define	SATA_SIGNATURE_SEMB		0xC33C0101	/* Enclosure Management Bridge */
#define	SATA_SIGNATURE_PM		0x96690101	/* Port Multiplier */

/* The SATA specs specify these kinds of
 * FIS (Frame Information Structure) */
typedef enum _AHCIFisType {
	FISRegisterH2D = 0x27,		/* Register FIS - Host To Device */
	FISRegisterD2H = 0x34,		/* Register FIS - Device To Host */
	FISDmaActivate = 0x39,		/* DMA Activate FIS - Device To Host */
	FISDmaSetup = 0x41,			/* DMA Setup FIS - Bidirectional */
	FISData = 0x46,				/* Data FIS - Bidirectional */
	FISBistActivate = 0x58,		/* BIST Activate FIS - Bidirectional */
	FISPioSetup = 0x5F,			/* PIO Setup FIS - Device To Host */
	FISDeviceBits = 0xA1		/* Set device bits FIS - Device To Host */
} AHCIFisType_t;

/* The FISRegisterH2D structure 
 * as described in the SATA Specification */
PACKED_TYPESTRUCT(FISRegisterH2D, {
	uint8_t					Type;

	/* Flags 
	 * Bits 0-3: Port Multiplier
	 * Bits 4-6: Reserved, should be 0
	 * Bit 7: (1) for Command, (0) for Control */
	uint8_t					Flags;

	uint8_t					Command;		/* Command Register */
	uint8_t					FeaturesLow;	/* Features 0:7 */
	uint8_t					SectorNo;		/* Lba Register 0:7 */

	/* Contents of the least significant 8 bits 
	 * of Cylinder Number or LBA 15:8 */
	uint8_t					CylinderLow;

	/* Contents of the least significant 8 bits 
	 * of Cylinder Number or LBA 23:16 */
	uint8_t					CylinderHigh;

	/* Device Register 
	 * Bit 0-3: Head when using CHS 
	 * Bit   4: Drive number, 0 or 1 */
	uint8_t					Device;

	/* Contents of the upper 8 bits of the expandend 
	 * sector number value (LBA 31:24) */
	uint8_t					SectorNoExtended;
	
	/* Contents of the most significant 8 bits of 
	 * the Cylinder number, or LBA 39:32 */
	uint8_t					CylinderLowExtended;

	/* Contents of the most significant 8 bits of 
	 * the Cylinder number, or LBA 47:40 */
	uint8_t					CylinderHighExtended;
	uint8_t					FeaturesHigh;	/* Features 8:15 */
	uint16_t				Count;	/* Only using bits 8-15 in LBA48 mode */
	uint8_t					Icc;	/* Isochronous Command Completion */
	uint8_t					Control;
	uint32_t				Reserved;
});

/* FISRegisterH2D Definitions 
 * - Flags */
#define FIS_HOST_TO_DEVICE			0x80

/* The FISRegisterD2H structure 
 * as described in the SATA Specification */
PACKED_TYPESTRUCT(FISRegisterD2H, {
	uint8_t					Type;

	/* Flags 
	 * Bits 0-3: Port Multiplier
	 * Bits 4-5: Reserved, should be 0
	 * Bit 6: Interrupt Bit 
	 * Bit 7: Reserved, should be 0 */
	uint8_t					Flags;
	uint8_t					Status;
	uint8_t					Error;

	uint16_t				Lba0;			/* Lba Register 0:15 */
	uint8_t					Lba1;			/* Lba Register 16:23 */
	uint8_t					Device;
	uint16_t				Lba2;			/* Lba Register 24:39 */
	uint8_t					Lba3;			/* Lba Register 40:47 */

	uint8_t					Reserved0;
	uint16_t				Count;			/* Count Register */
	uint16_t				Reserved1;
	uint32_t				Reserved2;		/* DWORD 4: Reserved */
});

/* Data FIS � Bidirectional 
 * This FIS is used by the host or device to send data payload. 
 * The data size can be varied. */
PACKED_TYPESTRUCT(FISData, {
	uint8_t					Type;

	/* Flags 
	 * Bits 0-3: Port Multiplier
	 * Bits 4-7: Reserved, should be 0 */
	uint8_t					Flags;
	uint16_t				Reserved;
	uint32_t				Payload[1];
});

/* PIO Setup � Device to Host
 * This FIS is used by the device to tell the host that it�s about to send 
 * or ready to receive a PIO data payload. */
PACKED_TYPESTRUCT(FISPioSetup, {
	uint8_t						Type;

	/* Flags 
	 * Bits 0-3: Port Multiplier
	 * Bit 4: Reserved, should be 0
	 * Bit 5: Data transfer direction, 1 - device to host, 0 - host to device
	 * Bit 6: Interrupt Bit 
	 * Bit 7: Reserved, should be 0 */
	uint8_t						Flags;
	uint8_t						Status;
	uint8_t						Error;

	uint16_t					Lba0;	// Lba Register 0:15
	uint8_t						Lba1;	// Lba Register 16:23
	uint8_t						Device;
	uint16_t					Lba2;	// Lba Register 24:39
	uint8_t						Lba3;	// Lba Register 40:47

	uint8_t						Reserved0;
	uint16_t					Count;
	uint8_t						Reserved1;

	uint8_t						UpdatedStatus;	// New value of status register
	uint16_t					TransferCount;
	uint16_t					Reserved2;
});

/* The FISDmaSetup structure 
 * as described in the SATA Specification */
PACKED_TYPESTRUCT(FISDmaSetup, {
	uint8_t						Type;

	/* Flags 
	 * Bits 0-3: Port Multiplier
	 * Bit 4: Reserved, should be 0
	 * Bit 5: Data transfer direction, 1 - device to host, 0 - host to device
	 * Bit 6: Interrupt Bit 
	 * Bit 7: Auto-activate. Specifies if DMA Activate FIS is needed */
	uint8_t						Flags;
	uint16_t					Reserved0;

	/* DMA Buffer Identifier. Used to Identify DMA buffer in host memory. 
	 * SATA Spec says host specific and not in Spec. Trying AHCI spec might work. */
	uint64_t					DmaBufferId;
	uint32_t					Reserved1;
	uint32_t					DmaBufferOffset;	// Byte offset into buffer. First 2 bits must be 0
	uint32_t					TransferCount;		// Number of bytes to transfer. Bit 0 must be 0
	uint32_t					Reserved2;
});

/* The FISDeviceBits structure 
 * as described in the SATA Specification */
PACKED_TYPESTRUCT(FISDeviceBits, {
	uint8_t						Type;
	uint8_t						Flags;
	uint8_t						Status;
	uint8_t						Error;
	uint32_t					Reserved;
});

#endif //!_SATA_H_
