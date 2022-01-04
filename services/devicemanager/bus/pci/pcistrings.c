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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS X86 Bus Driver (Strings)
 * - Enumerates the bus and registers the devices/controllers
 *   available in the system
 */

#include <os/osdefs.h>

/* PciToString
 * Converts the given class, subclass and interface into
 * descriptive string to give the pci-entry a description */
const char*
PciToString(
	_In_ uint8_t Class,
	_In_ uint8_t SubClass,
	_In_ uint8_t Interface)
{
	switch (Class)
	{
		/* Device was built prior definition of the class code field */
		case 0:
		{
			switch (SubClass)
			{
				case 0:
				{
					return "Non-VGA-Compatible devices";
				} break;

				case 1:
				{
					return "VGA-Compatible Device";
				} break;

				default:
				{
					return "Unclassified Device";
				} break;
			}
		} break;

	/* Mass Storage Controller */
	case 1:
	{
		switch (SubClass)
		{
			case 0:
			{
				return "SCSI Bus Controller";
			} break;

			case 1:
			{
				return "IDE Controller";
			} break;

			case 2:
			{
				return "Floppy Disk Controller";
			} break;

			case 3:
			{
				return "IPI Bus Controller";
			} break;

			case 4:
			{
				return "RAID Controller";
			} break;

			case 5:
			{
				/* ATA */
				if (Interface == 0x20)
					return "ATA Controller (Single DMA)";
				else
					return "ATA Controller (Chained DMA)";
			} break;

			case 6:
			{
				/* AHCI */
				if (Interface == 0x0)
					return "Serial ATA (Vendor Specific Interface)";
				else
					return "Serial ATA (AHCI 1.0)";
			} break;

			case 7:
			{
				return "Serial Attached SCSI (SAS)";
			} break;

			case 0x80:
			{
				return "Other Mass Storage Controller";
			} break;

			default:
			{
				return "Unclassified Mass Storage Controller";
			} break;
		}
	} break;

	/* Network Controller */
	case 2:
	{
		switch (SubClass)
		{
			case 0:
			{
				return "Ethernet Controller";
			} break;

			case 1:
			{
				return "Token Ring Controller";
			} break;

			case 2:
			{
				return "FDDI Controller";
			} break;

			case 3:
			{
				return "ATM Controller";
			} break;

			case 4:
			{
				return "ISDN Controller";
			} break;

			case 5:
			{
				return "WorldFip Controller";
			} break;

			case 6:
			{
				return "PICMG 2.14 Multi Computing";
			} break;

			case 0x80:
			{
				return "Other Network Controller";
			} break;

			default:
			{
				return "Unclassified Network Controller";
			} break;
		}
	} break;

	/* Display Controller */
	case 3:
	{
		switch (SubClass)
		{
			case 0:
			{
				if (Interface == 0x0)
					return "VGA-Compatible Controller";
				else
					return "8512-Compatible Controller";
			} break;

			case 1:
			{
				return "XGA Controller";
			} break;

			case 2:
			{
				return "3D Controller (Not VGA-Compatible)";
			} break;

			case 0x80:
			{
				return "Other Display Controller";
			} break;

			default:
			{
				return "Unclassified Display Controller";
			} break;
		}
	} break;

	/* Multimedia Controller */
	case 4:
	{
		switch (SubClass)
		{
			case 0:
			{
				return "Video Device";
			} break;

			case 1:
			{
				return "Audio Device";
			} break;

			case 2:
			{
				return "Computer Telephony Device";
			} break;

			case 3:
			{
				return "Flash Card Controller";
			} break;

			case 0x80:
			{
				return "Other Multimedia Device";
			} break;

			default:
			{
				return "Unclassified Multimedia Device";
			} break;
		}
	} break;

	/* Memory Controller */
	case 5:
	{
		switch (SubClass)
		{
			case 0:
			{
				return "RAM Controller";
			} break;

			case 1:
			{
				return "Flash Controller";
			} break;

			case 0x80:
			{
				return "Other Memory Controller";
			} break;

			default:
			{
				return "Unclassified Memory Controller";
			} break;
		}
	} break;

	/* Bridge Device */
	case 6:
	{
		switch (SubClass)
		{
			case 0:
			{
				return "Host Bridge";
			} break;

			case 1:
			{
				return "ISA Bridge";
			} break;

			case 2:
			{
				return "EISA Bridge";
			} break;

			case 3:
			{
				return "MCA Bridge";
			} break;

			case 4:
			{
				if (Interface == 0x0)
					return "PCI-to-PCI Bridge";
				else
					return "PCI-to-PCI Bridge (Subtractive Decode)";
			} break;

			case 5:
			{
				return "PCMCIA Bridge";
			} break;

			case 6:
			{
				return "NuBus Bridge";
			} break;

			case 7:
			{
				return "CardBus Bridge";
			} break;

			case 8:
			{
				return "RACEway Bridge";
			} break;

			case 9:
			{
				if (Interface == 0x40)
					return "PCI-to-PCI Bridge (Semi-Transparent, Primary)";
				else
					return "PCI-to-PCI Bridge (Semi-Transparent, Secondary)";
			} break;

			case 10:
			{
				return "InfiniBrand-to-PCI Host Bridge";
			} break;

			case 0x80:
			{
				return "Other Bridge Device";
			} break;

			default:
			{
				return "Unclassified Bridge Device";
			} break;
		}
	} break;

	/* Simple Communication Controllers */
	case 7:
	{
		switch (SubClass)
		{
			case 0:
			{
				switch (Interface)
				{
				case 0:
				{
					return "Generic XT-Compatible Serial Controller";
				} break;

				case 1:
				{
					return "16450-Compatible Serial Controller";
				} break;

				case 2:
				{
					return "16550-Compatible Serial Controller";
				} break;

				case 3:
				{
					return "16650-Compatible Serial Controller";
				} break;

				case 4:
				{
					return "16750-Compatible Serial Controller";
				} break;

				case 5:
				{
					return "16850-Compatible Serial Controller";
				} break;

				case 6:
				{
					return "16950-Compatible Serial Controller";
				} break;

				default:
				{
					return "Generic Serial Controller";
				} break;
				}
			} break;

			case 1:
			{
				switch (Interface)
				{
				case 0:
				{
					return "Parallel Port";
				} break;

				case 1:
				{
					return "Bi-Directional Parallel Port";
				} break;

				case 2:
				{
					return "ECP 1.X Compliant Parallel Port";
				} break;

				case 3:
				{
					return "IEEE 1284 Controller";
				} break;

				case 0xFE:
				{
					return "IEEE 1284 Target Device";
				} break;

				default:
				{
					return "Unclassified Port";
				} break;
				}
			} break;

			case 2:
			{
				return "Multiport Serial Controller";
			} break;

			case 3:
			{
				switch (Interface)
				{
				case 0:
				{
					return "Generic Modem";
				} break;

				case 1:
				{
					return "Hayes Compatible Modem (16450-Compatible Interface)";
				} break;

				case 2:
				{
					return "Hayes Compatible Modem (16550-Compatible Interface)";
				} break;

				case 3:
				{
					return "Hayes Compatible Modem (16650-Compatible Interface)";
				} break;

				case 4:
				{
					return "Hayes Compatible Modem (16750-Compatible Interface)";
				} break;

				default:
				{
					return "Unclassified Modem";
				} break;
				}
			} break;

			case 4:
			{
				return "IEEE 488.1/2 (GPIB) Controller";
			} break;

			case 5:
			{
				return "Smart Card";
			} break;

			case 0x80:
			{
				return "Other Communications Device";
			} break;

			default:
			{
				return "Unclassified Communications Device";
			} break;
		}
	} break;

	/* Base System Peripherals */
	case 8:
	{
		switch (SubClass)
		{
			case 0:
			{
				switch (Interface)
				{
				case 0:
				{
					return "Generic 8259 PIC";
				} break;

				case 1:
				{
					return "ISA PIC";
				} break;

				case 2:
				{
					return "EISA PIC";
				} break;

				case 0x10:
				{
					return "I/O APIC Interrupt Controller";
				} break;

				case 0x20:
				{
					return "I/O(x) APIC Interrupt Controller";
				} break;

				default:
				{
					return "Unclassified ISA Device";
				} break;
				}
			} break;

			case 1:
			{
				switch (Interface)
				{
				case 0:
				{
					return "Generic 8237 DMA Controller";
				} break;

				case 1:
				{
					return "ISA DMA Controller";
				} break;

				case 2:
				{
					return "EISA DMA Controller";
				} break;

				default:
				{
					return "Unclassified DMA Controller";
				} break;
				}
			} break;

			case 2:
			{
				switch (Interface)
				{
				case 0:
				{
					return "Generic 8254 System Timer";
				} break;

				case 1:
				{
					return "ISA System Timer";
				} break;

				case 2:
				{
					return "EISA System Timer";
				} break;

				default:
				{
					return "Unclassified System Timer";
				} break;
				}
			} break;

			case 3:
			{
				if (Interface == 0x0)
					return "Generic RTC Controller";
				else
					return "ISA RTC Controller";
			} break;

			case 4:
			{
				return "Generic PCI Hot-Plug Controller";
			} break;

			case 0x80:
			{
				return "Other System Peripheral";
			} break;

			default:
			{
				return "Unclassified System Peripheral";
			} break;
		}
	} break;

	/* Input Devices */
	case 9:
	{
		switch (SubClass)
		{
			case 0:
			{
				return "Keyboard Controller";
			} break;

			case 1:
			{
				return "Digitizer";
			} break;

			case 2:
			{
				return "Mouse Controller";
			} break;

			case 3:
			{
				return "Scanner Controller";
			} break;

			case 4:
			{
				if (Interface == 0x0)
					return "Gameport Controller (Generic)";
				else
					return "Gameport Contrlller (Legacy)";
			} break;

			case 0x80:
			{
				return "Other Input Controller";
			} break;

			default:
			{
				return "Unclassified Input Controller";
			} break;
		}
	} break;

	/* Docking Stations */
	case 10:
	{
		if (SubClass == 0x0)
			return "Generic Docking Station";
		else
			return "Other Docking Station";
	} break;

	/* Processors */
	case 11:
	{
		switch (SubClass)
		{
			case 0:
			{
				return "386 Processor";
			} break;

			case 1:
			{
				return "486 Processor";
			} break;

			case 2:
			{
				return "Pentium Processor";
			} break;

			case 0x10:
			{
				return "Alpha Processor";
			} break;

			case 0x20:
			{
				return "PowerPC Processor";
			} break;

			case 0x30:
			{
				return "MIPS Processor";
			} break;

			case 0x40:
			{
				return "Co-Processor";
			} break;

			default:
			{
				return "Unclassified Processor";
			} break;
		}
	} break;

	/* Serial Bus Controllers */
	case 12:
	{
		switch (SubClass)
		{
			case 0:
			{
				if (Interface == 0x0)
					return "IEEE 1394 Controller (FireWire)";
				else
					return "IEEE 1394 Controller (1394 OpenHCI Spec)";
			} break;

			case 1:
			{
				return "ACCESS.bus";
			} break;

			case 2:
			{
				return "SSA";
			} break;

			case 3:
			{
				switch (Interface)
				{
				case 0:
				{
					return "USB UHCI (Universal Host Controller)";
				} break;

				case 0x10:
				{
					return "USB OHCI (Open Host Controller)";
				} break;

				case 0x20:
				{
					return "USB EHCI (Intel Enhanced Host Controller)";
				} break;

				case 0x30:
				{
					return "USB XHCI (Intel xHanced Host Controller)";
				} break;

				case 0x80:
				{
					return "Other USB Controller";
				} break;

				case 0xFE:
				{
					return "USB (Not Host Controller)";
				} break;

				default:
				{
					return "Unclassified USB Controller";
				} break;
				}
			} break;

			case 4:
			{
				return "Fibre Channel";
			} break;

			case 5:
			{
				return "SMBus";
			} break;

			case 6:
			{
				return "InfiniBand";
			} break;

			case 7:
			{
				switch (Interface)
				{
				case 0:
				{
					return "IPMI SMIC Interface";
				} break;

				case 1:
				{
					return "IPMI Kybd Controller Style Interface";
				} break;

				case 2:
				{
					return "IPMI Block Transfer Interface";
				} break;

				default:
				{
					return "Unclassified IPMI Controller";
				} break;
				}
			} break;

			case 8:
			{
				return "SERCOS Interface Standard (IEC 61491)";
			} break;

			case 9:
			{
				return "CANbus";
			} break;

			case 0x80:
			{
				return "Other Serial Bus Controllers";
			} break;

			default:
			{
				return "Unclassified Serial Bus Controllers";
			} break;
		}
	} break;

	/* Wireless Controllers */
	case 13:
	{
		switch (SubClass)
		{
			case 0:
			{
				return "iRDA Compatible Controller";
			} break;

			case 1:
			{
				return "Consumer IR Controller";
			} break;

			case 0x10:
			{
				return "RF Controller";
			} break;

			case 0x11:
			{
				return "Bluetooth Controller";
			} break;

			case 0x12:
			{
				return "Broadband Controller";
			} break;

			case 0x20:
			{
				return "Ethernet Controller (802.11a)";
			} break;

			case 0x21:
			{
				return "Ethernet Controller (802.11b)";
			} break;

			case 0x80:
			{
				return "Other Wireless Controller";
			} break;

			default:
			{
				return "Unclassified Wireless Controller";
			} break;
		}
	} break;

	/* Intelligent I/O Controllers */
	case 14:
	{
		switch (SubClass)
		{
			case 0:
			{
				if (Interface == 0x0)
					return "Message FIFO";
				else
					return "I20 Architecture";
			} break;

			case 0x80:
			{
				return "Other Intelligent I/O Controllers";
			} break;

			default:
			{
				return "Unclassified Intelligent I/O Controllers";
			} break;
		}
	} break;

	/* Satellite Communication Controllers */
	case 15:
	{
		switch (SubClass)
		{
			case 1:
			{
				return "TV Controller";
			} break;

			case 2:
			{
				return "Audio Controller";
			} break;

			case 3:
			{
				return "Voice Controller";
			} break;

			case 4:
			{
				return "Data Controller";
			} break;

			case 0x80:
			{
				return "Other Communication Controller";
			} break;

			default:
			{
				return "Unclassified Communication Controller";
			} break;
		}
	} break;

	/* Encryption/Decryption Controllers */
	case 16:
	{
		switch (SubClass)
		{
			case 0:
			{
				return "Network and Computing Encryption/Decryption";
			} break;

			case 0x10:
			{
				return "Entertainment Encryption/Decryption";
			} break;

			case 0x80:
			{
				return "Other Encryption/Decryption";
			} break;

			default:
			{
				return "Unclassified Encryption/Decryption";
			} break;
		}
	} break;

	/* Data Acquisition and Signal Processing Controllers */
	case 17:
	{
		switch (SubClass)
		{
			case 0:
			{
				return "DPIO Modules";
			} break;

			case 1:
			{
				return "Performance Counters";
			} break;

			case 0x10:
			{
				return "Communications Synchronization Plus Time and Frequency Test/Measurment";
			} break;

			case 0x20:
			{
				return "Management Card";
			} break;

			case 0x80:
			{
				return "Other Data Acquisition/Signal Processing Controller";
			} break;

			default:
			{
				return "Unclassified Data Acquisition/Signal Processing Controller";
			} break;
		}
	} break;

	default:
	{
		return "Unclassified Device";
	} break;

	}
}
