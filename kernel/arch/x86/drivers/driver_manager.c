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
* MollenOS X86-32 Driver Manager
* Version 1. PCI Support Only (No PCI Express)
*/

/* Includes */
#include <arch.h>
#include <assert.h>
#include <acpi.h>
#include <pci.h>
#include <list.h>
#include <heap.h>
#include <stddef.h>
#include <stdio.h>
#include <limits.h>

/* Drivers */
#include <drivers\clock\clock.h>
#include <drivers\usb\ohci\ohci.h>

/* Prototypes */
void pci_check_function(uint8_t bus, uint8_t device, uint8_t function);
void pci_check_device(uint8_t bus, uint8_t device);
void pci_check_bus(uint8_t bus);

pci_device_t *pci_add_object(ACPI_HANDLE handle, ACPI_HANDLE parent, uint32_t type);

/* Globals */
list_t *glb_pci_devices = NULL;
list_t *glb_pci_acpica = NULL;
volatile uint32_t glb_bus_counter = 0;

/* Externs */

/* Decode PCI Device to String */
char *pci_to_string(uint8_t class, uint8_t sub_class, uint8_t prog_if)
{
	switch (class)
	{
		/* Device was built prior definition of the class code field */
		case 0:
		{
			switch (sub_class)
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
			switch (sub_class)
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
					if (prog_if == 0x20)
						return "ATA Controller (Single DMA)";
					else
						return "ATA Controller (Chained DMA)";
				} break;

				case 6:
				{
					/* AHCI */
					if (prog_if == 0x0)
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
			switch (sub_class)
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
			switch (sub_class)
			{
				case 0:
				{
					if (prog_if == 0x0)
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
			switch (sub_class)
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
			switch (sub_class)
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
			switch (sub_class)
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
					if (prog_if == 0x0)
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
					if (prog_if == 0x40)
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
			switch (sub_class)
			{
				case 0:
				{
					switch (prog_if)
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
					switch (prog_if)
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
					switch (prog_if)
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
			switch (sub_class)
			{
				case 0:
				{
					switch (prog_if)
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
					switch (prog_if)
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
					switch (prog_if)
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
					if (prog_if == 0x0)
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
			switch (sub_class)
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
					if (prog_if == 0x0)
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
			if (sub_class == 0x0)
				return "Generic Docking Station";
			else
				return "Other Docking Station";
		} break;

		/* Processors */
		case 11:
		{
			switch (sub_class)
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
			switch (sub_class)
			{
				case 0:
				{
					if (prog_if == 0x0)
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
					switch (prog_if)
					{
						case 0:
						{
							return "USB UHCI (Universal Host Controller Spec)";
						} break;

						case 0x10:
						{
							return "USB OHCI (Open Host Controller Spec)";
						} break;

						case 0x20:
						{
							return "USB2 Host Controller (Intel Enhanced Host Controller Interface)";
						} break;

						case 0x30:
						{
							return "USB3 Host Controller (Intel Enhanced Host Controller Interface)";
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
					switch (prog_if)
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
			switch (sub_class)
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
			switch (sub_class)
			{
				case 0:
				{
					if (prog_if == 0x0)
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
			switch (sub_class)
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
			switch (sub_class)
			{
				case 0:
				{
					return "Network and Computing Encrpytion/Decryption";
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
			switch (sub_class)
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
					return "Communications Syncrhonization Plus Time and Frequency Test/Measurment";
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

/* PCI Interface I/O */
uint32_t pci_read_dword(const uint16_t bus, const uint16_t dev,
	const uint16_t func, const uint32_t reg)
{
	/* Select Bus/Device/Function/Register */
	outl(X86_PCI_SELECT, 0x80000000L | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
		((uint32_t)func << 8) | (reg & ~3));

	/* Read Data */
	return inl(X86_PCI_DATA + (reg & 3));
}

uint16_t pci_read_word(const uint16_t bus, const uint16_t dev,
	const uint16_t func, const uint32_t reg)
{
	/* Select Bus/Device/Function/Register */
	outl(X86_PCI_SELECT, 0x80000000L | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
		((uint32_t)func << 8) | (reg & ~3));

	/* Read Data */
	return inw(X86_PCI_DATA + (reg & 3));
}

uint8_t pci_read_byte(const uint16_t bus, const uint16_t dev,
	const uint16_t func, const uint32_t reg)
{
	/* Select Bus/Device/Function/Register */
	outl(X86_PCI_SELECT, 0x80000000L | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
		((uint32_t)func << 8) | (reg & ~3));

	/* Read Data */
	return inb(X86_PCI_DATA + (reg & 3));
}

void pci_write_dword(const uint16_t bus, const uint16_t dev,
	const uint16_t func, const uint32_t reg, uint32_t value)
{
	/* Select Bus/Device/Function/Register */
	outl(X86_PCI_SELECT, 0x80000000L | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
		((uint32_t)func << 8) | (reg & ~3));

	/* Write DATA */
	outl(X86_PCI_DATA + (reg & 3), value);
	return;
}

void pci_write_word(const uint16_t bus, const uint16_t dev,
	const uint16_t func, const uint32_t reg, uint16_t value)
{
	/* Select Bus/Device/Function/Register */
	outl(X86_PCI_SELECT, 0x80000000L | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
		((uint32_t)func << 8) | (reg & ~3));

	/* Write DATA */
	outw(X86_PCI_DATA + (reg & 3), value);
	return;
}

void pci_write_byte(const uint16_t bus, const uint16_t dev,
	const uint16_t func, const uint32_t reg, uint8_t value)
{
	/* Select Bus/Device/Function/Register */
	outl(X86_PCI_SELECT, 0x80000000L | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
		((uint32_t)func << 8) | (reg & ~3));

	/* Write DATA */
	outb(X86_PCI_DATA + (reg & 3), value);
	return;
}

/* Reads the vendor id at given location */
uint16_t pci_read_vendor_id(const uint16_t bus, const uint16_t device, const uint16_t function)
{
	/* Get the dword and parse the vendor and device ID */
	uint32_t tmp = pci_read_dword(bus, device, function, 0);
	uint16_t vendor = (tmp & 0xFFFF);

	return vendor;
}

/* Reads a PCI header at given location */
void pci_read_function(pci_device_header_t *pcs, const uint16_t bus, const uint16_t device, const uint16_t function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = pci_read_vendor_id(bus, device, function);
	uint32_t i;

	if (vendor && vendor != 0xFFFF)
	{
		/* Valid device! Okay, so the config space is 256 bytes long
		* and we read in dwords: 64 reads should do it.
		*/

		for (i = 0; i < 64; i += 16)
		{
			*(uint32_t*)((size_t)pcs + i) = pci_read_dword(bus, device, function, i);
			*(uint32_t*)((size_t)pcs + i + 4) = pci_read_dword(bus, device, function, i + 4);
			*(uint32_t*)((size_t)pcs + i + 8) = pci_read_dword(bus, device, function, i + 8);
			*(uint32_t*)((size_t)pcs + i + 12) = pci_read_dword(bus, device, function, i + 12);
		}
	}
}

/* Reads the base class at given location */
uint8_t pci_read_base_class(const uint16_t bus, const uint16_t device, const uint16_t function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = pci_read_vendor_id(bus, device, function);

	if (vendor && vendor != 0xFFFF)
	{
		/* Valid device! Okay, so read the base_class
		*/
		uint32_t offset = pci_read_dword(bus, device, function, 0x08);
		return (uint8_t)((offset >> 24) & 0xFF);
	}
	else
		return 0xFF;
}

/* Reads the sub class at given location */
uint8_t pci_read_sub_class(const uint16_t bus, const uint16_t device, const uint16_t function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = pci_read_vendor_id(bus, device, function);

	if (vendor && vendor != 0xFFFF)
	{
		/* Valid device! Okay, so read the base_class
		*/
		uint32_t offset = pci_read_dword(bus, device, function, 0x08);
		return (uint8_t)((offset >> 16) & 0xFF);
	}
	else
		return 0xFF;
}

/* Reads the secondary bus number at given location */
uint8_t pci_read_secondary_bus_number(const uint16_t bus, const uint16_t device, const uint16_t function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = pci_read_vendor_id(bus, device, function);

	if (vendor && vendor != 0xFFFF)
	{
		/* Valid device! Okay, so read the base_class
		*/
		uint32_t offset = pci_read_dword(bus, device, function, 0x18);
		return (uint8_t)((offset >> 8) & 0xFF);
	}
	else
		return 0xFF;
}

/* Reads the sub class at given location */
/* Bit 7 - MultiFunction, Lower 4 bits is type. 
 * Type 0 is standard, Type 1 is PCI-PCI Bridge,
 * Type 2 is CardBus Bridge */
uint8_t pci_read_header_type(const uint16_t bus, const uint16_t device, const uint16_t function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = pci_read_vendor_id(bus, device, function);

	if (vendor && vendor != 0xFFFF)
	{
		/* Valid device! Okay, so read the base_class
		*/
		uint32_t offset = pci_read_dword(bus, device, function, 0x0C);
		return (uint8_t)((offset >> 16) & 0xFF);
	}
	else
		return 0xFF;
}

/* Check a function */
/* For each function we create a 
 * pci_device and add it to the list */
void pci_check_function(uint8_t bus, uint8_t device, uint8_t function)
{
	uint8_t sec_bus;
	pci_device_header_t *pcs;
	pci_driver_t *pci_driver;

	/* Allocate */
	pcs = (pci_device_header_t*)kmalloc(sizeof(pci_device_header_t));
	pci_driver = (pci_driver_t*)kmalloc(sizeof(pci_driver_t));

	/* Get full information */
	pci_read_function(pcs, bus, device, function);

	/* Set information */
	pci_driver->header = pcs;
	pci_driver->bus = bus;
	pci_driver->device = device;
	pci_driver->function = function;
	pci_driver->children = NULL;

	/* Info */
	printf("    * [%d:%d:%d][%d:%d:%d] Vendor 0x%x, Device 0x%x : %s\n",
		pcs->class_code, pcs->subclass, pcs->ProgIF, 
		bus, device, function,
		pcs->vendor_id, pcs->device_id,
		pci_to_string(pcs->class_code, pcs->subclass, pcs->ProgIF));

	/* Add to list */
	if (bus == 0)
	{
		if (pcs->class_code == 0x06 && pcs->subclass == 0x04)
		{
			pci_driver->type = X86_PCI_TYPE_BRIDGE;
			list_append(glb_pci_devices, list_create_node(X86_PCI_TYPE_BRIDGE, pci_driver));
		}
		else
		{
			pci_driver->type = X86_PCI_TYPE_DEVICE;
			list_append(glb_pci_devices, list_create_node(X86_PCI_TYPE_DEVICE, pci_driver));
		}
	}
	else
	{
		/* Find correct bus */
	}

	/* Is this a secondary (PCI) bus */
	if ((pcs->class_code == 0x06) && (pcs->subclass == 0x04))
	{
		/* Uh oh, this dude has children */
		pci_driver->children = list_create(LIST_SAFE);

		sec_bus = pci_read_secondary_bus_number(bus, device, function);
		pci_check_bus(sec_bus);
	}
}

/* Check a device */
void pci_check_device(uint8_t bus, uint8_t device)
{
	uint8_t function = 0;
	uint16_t vendor_id = 0;
	uint8_t header_type = 0;

	/* Get vendor id */
	vendor_id = pci_read_vendor_id(bus, device, function);

	/* Sanity */
	if (vendor_id == 0xFFFF)
		return;

	/* Check function 0 */
	pci_check_function(bus, device, function);
	header_type = pci_read_header_type(bus, device, function);

	/* Multi-function or single? */
	if (header_type & 0x80) 
	{
		/* It is a multi-function device, so check remaining functions */
		for (function = 1; function < 8; function++) 
		{
			/* Only check if valid vendor */
			if (pci_read_vendor_id(bus, device, function) != 0xFFFF)
				pci_check_function(bus, device, function);
		}
	}
}

/* Check a bus */
void pci_check_bus(uint8_t bus) 
{
	uint8_t device;

	for (device = 0; device < 32; device++)
		pci_check_device(bus, device);
}

/* First of all, devices exists on TWO different
 * busses. PCI and PCI Express. */
void drivers_enumerate(void)
{
	uint8_t function;
	uint8_t bus;
	uint8_t header_type;

	header_type = pci_read_header_type(0, 0, 0);
	
	if ((header_type & 0x80) == 0) 
	{
		/* Single PCI host controller */
		printf("    * Single Bus Present\n");
		pci_check_bus(0);
	}
	else 
	{
		/* Multiple PCI host controllers */
		printf("    * Multi Bus Present\n");
		for (function = 0; function < 8; function++) 
		{
			if (pci_read_vendor_id(0, 0, function) != 0xFFFF)
				break;

			/* Check bus */
			bus = function;
			pci_check_bus(bus);
		}
	}
}

/* Video Backlight Capability Callback */
ACPI_STATUS pci_video_backlight_callback(ACPI_HANDLE handle, UINT32 level, void *context, void **return_value)
{
	ACPI_HANDLE null_handle = NULL;
	uint64_t *video_features = (uint64_t*)context;

	if (ACPI_SUCCESS(AcpiGetHandle(handle, "_BCM", &null_handle)) &&
		ACPI_SUCCESS(AcpiGetHandle(handle, "_BCL", &null_handle)))
	{
		/* Add */
		*video_features |= ACPI_VIDEO_BACKLIGHT;
		
		/* Check for Brightness */
		if (ACPI_SUCCESS(AcpiGetHandle(handle, "_BQC", &null_handle)))
			*video_features |= ACPI_VIDEO_BRIGHTNESS;
		
		/* We have backlight support, no need to scan further */
		return AE_CTRL_TERMINATE;
	}

	return AE_OK;
}

/* Is this a video device? 
 * If it is, we also retrieve capabilities */
ACPI_STATUS pci_device_is_video(pci_device_t *device)
{
	ACPI_HANDLE null_handle = NULL;
	uint64_t video_features = 0;

	/* Sanity */
	if (device == NULL)
		return AE_ABORT_METHOD;

	/* Does device support Video Switching */
	if (ACPI_SUCCESS(AcpiGetHandle(device->handle, "_DOD", &null_handle)) &&
		ACPI_SUCCESS(AcpiGetHandle(device->handle, "_DOS", &null_handle)))
		video_features |= ACPI_VIDEO_SWITCHING;

	/* Does device support Video Rom? */
	if (ACPI_SUCCESS(AcpiGetHandle(device->handle, "_ROM", &null_handle)))
		video_features |= ACPI_VIDEO_ROM;

	/* Does device support configurable video head? */
	if (ACPI_SUCCESS(AcpiGetHandle(device->handle, "_VPO", &null_handle)) &&
		ACPI_SUCCESS(AcpiGetHandle(device->handle, "_GPD", &null_handle)) &&
		ACPI_SUCCESS(AcpiGetHandle(device->handle, "_SPD", &null_handle)))
		video_features |= ACPI_VIDEO_POSTING;

	/* Only call this if it is a video device */
	if (video_features != 0)
	{
		AcpiWalkNamespace(ACPI_TYPE_DEVICE, device->handle, ACPI_UINT32_MAX, 
			pci_video_backlight_callback, NULL, &video_features, NULL);

		/* Update ONLY if video device */
		device->xfeatures = video_features;

		return AE_OK;
	}
	else 
		return AE_NOT_FOUND;
}

/* Is this a docking device? 
 * If it has a _DCK method, yes */
ACPI_STATUS pci_device_is_dock(pci_device_t *device)
{
	ACPI_HANDLE null_handle = NULL;

	/* Sanity */
	if (device == NULL)
		return AE_ABORT_METHOD;
	else
		return AcpiGetHandle(device->handle, "_DCK", &null_handle);
}

/* Is this a BAY (i.e cd-rom drive with a ejectable bay) 
 * We check several ACPI methods here */
ACPI_STATUS pci_device_is_bay(pci_device_t *device)
{
	ACPI_STATUS status;
	ACPI_HANDLE parent_handle = NULL;
	ACPI_HANDLE null_handle = NULL;

	/* Sanity, make sure it is even ejectable */
	status = AcpiGetHandle(device->handle, "_EJ0", &null_handle);

	if (ACPI_FAILURE(status))
		return status;

	/* Fine, lets try to fuck it up, _GTF, _GTM, _STM and _SDD,
	 * we choose you! */
	if ((ACPI_SUCCESS(AcpiGetHandle(device->handle, "_GTF", &null_handle))) ||
		(ACPI_SUCCESS(AcpiGetHandle(device->handle, "_GTM", &null_handle))) ||
		(ACPI_SUCCESS(AcpiGetHandle(device->handle, "_STM", &null_handle))) ||
		(ACPI_SUCCESS(AcpiGetHandle(device->handle, "_SDD", &null_handle))))
		return AE_OK;

	/* Uh... ok... maybe we are sub-device of an ejectable parent */
	status = AcpiGetParent(device->handle, &parent_handle);

	if (ACPI_FAILURE(status))
		return status;

	/* Now, lets try to fuck up parent ! */
	if ((ACPI_SUCCESS(AcpiGetHandle(parent_handle, "_GTF", &null_handle))) ||
		(ACPI_SUCCESS(AcpiGetHandle(parent_handle, "_GTM", &null_handle))) ||
		(ACPI_SUCCESS(AcpiGetHandle(parent_handle, "_STM", &null_handle))) ||
		(ACPI_SUCCESS(AcpiGetHandle(parent_handle, "_SDD", &null_handle))))
		return AE_OK;

	return AE_NOT_FOUND;
}

/* Get Irq by Bus / Dev / Pin 
 * Returns -1 if no overrides exists */
int pci_device_get_irq(uint32_t bus, uint32_t device, uint32_t pin)
{
	/* Locate correct bus */
	int n = 0;
	pci_device_t *dev;
	
	while (1)
	{
		dev = (pci_device_t*)list_get_data_by_id(glb_pci_acpica, ACPI_BUS_ROOT_BRIDGE, n);

		if (dev == NULL)
			break;

		if (dev->bus == bus && (dev->routings != NULL))
			return dev->routings->interrupts[device * 4 + (pin - 1)];
	}

	return -1;
}

/* Set Device Data Callback */
void pci_device_set_data_callback(ACPI_HANDLE handle, void *data)
{
	/* TODO */
}

/* Set Device Data */
ACPI_STATUS pci_device_set_data(pci_device_t *device, uint32_t type)
{
	/* Store, unless its power/sleep buttons */
	if ((type != ACPI_BUS_TYPE_POWER) &&
		(type != ACPI_BUS_TYPE_SLEEP))
	{
		return AcpiAttachData(device->handle, pci_device_set_data_callback, (void*)device);
	}

	return AE_OK;
}

/* Gets Device Status */
ACPI_STATUS pci_get_device_status(pci_device_t* device)
{
	ACPI_STATUS status = AE_OK;
	ACPI_BUFFER buffer;
	char lbuf[sizeof(ACPI_OBJECT)];

	/* Set up buffer */
	buffer.Length = sizeof(lbuf);
	buffer.Pointer = lbuf;

	/* Sanity */
	if (device->features & X86_ACPI_FEATURE_STA)
	{
		status = AcpiEvaluateObjectTyped(device->handle, "_STA", NULL, &buffer, ACPI_TYPE_INTEGER);

		if (ACPI_FAILURE(status))
			return status;

		device->status = (uint32_t)((ACPI_OBJECT *)buffer.Pointer)->Integer.Value;
	}
	else
	{
		/* The child in should not inherit the parents status if the parent is 
		 * functioning but not present (ie does not support dynamic status) */
		device->status = ACPI_STA_DEVICE_PRESENT | ACPI_STA_DEVICE_ENABLED |
							ACPI_STA_DEVICE_UI | ACPI_STA_DEVICE_FUNCTIONING;
	}

	return AE_OK;
}

/* Gets Device Bus Number */
ACPI_STATUS pci_get_device_bus_seg(pci_device_t* device)
{
	ACPI_STATUS status = AE_OK;
	ACPI_BUFFER buffer;
	char lbuf[sizeof(ACPI_OBJECT)];

	/* Set up buffer */
	buffer.Length = sizeof(lbuf);
	buffer.Pointer = lbuf;

	/* Sanity */
	if (device->features & X86_ACPI_FEATURE_BBN)
	{
		status = AcpiEvaluateObjectTyped(device->handle, "_BBN", NULL, &buffer, ACPI_TYPE_INTEGER);

		if (ACPI_FAILURE(status))
			return status;

		device->bus = (uint32_t)((ACPI_OBJECT *)buffer.Pointer)->Integer.Value;
	}
	else
	{
		/* Bus number is 0 :( */
		device->bus = 0;
	}

	/* Sanity */
	if (device->features & X86_ACPI_FEATURE_SEG)
	{
		status = AcpiEvaluateObjectTyped(device->handle, "_SEG", NULL, &buffer, ACPI_TYPE_INTEGER);

		if (ACPI_FAILURE(status))
			return status;

		device->seg = (uint32_t)((ACPI_OBJECT *)buffer.Pointer)->Integer.Value;
	}
	else
	{
		/* Bus number is 0 :( */
		device->bus = 0;
	}

	return AE_OK;
}

/* IRQ Routing Callback */
ACPI_STATUS pci_irq_routings_callback(ACPI_RESOURCE *res, void *context)
{
	pci_irq_resource_t *ires = (pci_irq_resource_t*)context;
	pci_device_t *device = (pci_device_t*)ires->device;
	ACPI_PCI_ROUTING_TABLE *tbl = (ACPI_PCI_ROUTING_TABLE*)ires->table;

	/* Normal IRQ Resource? */
	if (res->Type == ACPI_RESOURCE_TYPE_IRQ) 
	{
		ACPI_RESOURCE_IRQ *irq;
		UINT32 offset = (((tbl->Address >> 16) & 0xFF) * 4) + tbl->Pin;

		irq = &res->Data.Irq;
		device->routings->interrupts[offset] = irq->Interrupts[tbl->SourceIndex];
	}
	else if (res->Type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ) 
	{
		ACPI_RESOURCE_EXTENDED_IRQ *irq;
		UINT32 offset = (((tbl->Address >> 16) & 0xFF) * 4) + tbl->Pin;

		irq = &res->Data.ExtendedIrq;
		device->routings->interrupts[offset] = irq->Interrupts[tbl->SourceIndex];
	}

	return AE_OK;
}

/* Gets IRQ Routings */
ACPI_STATUS pci_get_device_irq_routings(pci_device_t *device)
{
	ACPI_STATUS status;
	ACPI_BUFFER abuff;
	ACPI_PCI_ROUTING_TABLE *tbl;
	int i;
	pci_routing_table_t *table;

	/* Setup Buffer */
	abuff.Length = 0x2000;
	abuff.Pointer = (char*)kmalloc(0x2000);

	/* Try to get routings */
	status = AcpiGetIrqRoutingTable(device->handle, &abuff);
	if (ACPI_FAILURE(status))
		goto done;

	/* Allocate Table */
	table = (pci_routing_table_t*)kmalloc(sizeof(pci_routing_table_t));
	
	/* Reset it */
	for (i = 0; i < 128; i++)
		table->interrupts[i] = -1;

	/* Link it */
	device->routings = table;

	for (tbl = (ACPI_PCI_ROUTING_TABLE *)abuff.Pointer; tbl->Length;
		tbl = (ACPI_PCI_ROUTING_TABLE *)
		((char *)tbl + tbl->Length)) 
	{
		ACPI_HANDLE src_handle;
		pci_irq_resource_t ires;

		/* Wub, we have a routing */
		if (*(char*)tbl->Source == '\0') 
		{
			/* Ok, eol */

			/* Set it */
			UINT32 offset = (((tbl->Address >> 16) & 0xFF) * 4) + tbl->Pin;
			table->interrupts[offset] = tbl->SourceIndex;
			continue;
		}

		/* Get handle of source */
		status = AcpiGetHandle(device->handle, tbl->Source, &src_handle);
		if (ACPI_FAILURE(status)) {
			printf("Failed AcpiGetHandle\n");
			continue;
		}

		/* Get all IRQ resources */
		ires.device = (void*)device;
		ires.table = (void*)tbl;
		
		status = AcpiWalkResources(src_handle, METHOD_NAME__CRS, pci_irq_routings_callback, &ires);
		
		if (ACPI_FAILURE(status)) {
			printf("Failed IRQ resource\n");
			continue;
		}
	}

done:
	kfree(abuff.Pointer);
	return AE_OK;
}

/* Gets Device Name */
ACPI_STATUS pci_get_bus_id(pci_device_t *device, uint32_t type)
{
	ACPI_BUFFER buffer;
	char bus_id[8];

	/* Setup Buffer */
	buffer.Pointer = bus_id;
	buffer.Length = sizeof(bus_id);

	/* Get Object Name based on type */
	switch (type)
	{
		case ACPI_BUS_SYSTEM:
			strcpy(device->bus_id, "ACPISB");
			break;
		case ACPI_BUS_TYPE_POWER:
			strcpy(device->bus_id, "POWERF");
			break;
		case ACPI_BUS_TYPE_SLEEP:
			strcpy(device->bus_id, "SLEEPF");
			break;
		default:
			AcpiGetName(device->handle, ACPI_SINGLE_NAME, &buffer);
			strcpy(device->bus_id, bus_id);
			break;
	}

	return AE_OK;
}

/* Gets Device Features */
ACPI_STATUS pci_get_features(pci_device_t *device)
{
	ACPI_STATUS status;
	ACPI_HANDLE null_handle = NULL;

	/* Supports dynamic status? */
	status = AcpiGetHandle(device->handle, "_STA", &null_handle);
	
	if (ACPI_SUCCESS(status))
		device->features |= X86_ACPI_FEATURE_STA;

	/* Is compatible ids present? */
	status = AcpiGetHandle(device->handle, "_CID", &null_handle);

	if (ACPI_SUCCESS(status))
		device->features |= X86_ACPI_FEATURE_CID;

	/* Supports removable? */
	status = AcpiGetHandle(device->handle, "_RMV", &null_handle);

	if (ACPI_SUCCESS(status))
		device->features |= X86_ACPI_FEATURE_RMV;

	/* Supports ejecting? */
	status = AcpiGetHandle(device->handle, "_EJD", &null_handle);

	if (ACPI_SUCCESS(status))
		device->features |= X86_ACPI_FEATURE_EJD;
	else
	{
		status = AcpiGetHandle(device->handle, "_EJ0", &null_handle);

		if (ACPI_SUCCESS(status))
			device->features |= X86_ACPI_FEATURE_EJD;
	}

	/* Supports device locking? */
	status = AcpiGetHandle(device->handle, "_LCK", &null_handle);

	if (ACPI_SUCCESS(status))
		device->features |= X86_ACPI_FEATURE_LCK;

	/* Supports power management? */
	status = AcpiGetHandle(device->handle, "_PS0", &null_handle);

	if (ACPI_SUCCESS(status))
		device->features |= X86_ACPI_FEATURE_PS0;
	else
	{
		status = AcpiGetHandle(device->handle, "_PR0", &null_handle);

		if (ACPI_SUCCESS(status))
			device->features |= X86_ACPI_FEATURE_PS0;
	}

	/* Supports wake? */
	status = AcpiGetHandle(device->handle, "_PRW", &null_handle);

	if (ACPI_SUCCESS(status))
		device->features |= X86_ACPI_FEATURE_PRW;
	
	/* Has IRQ Routing Table Present ?  */
	status = AcpiGetHandle(device->handle, "_PRT", &null_handle);

	if (ACPI_SUCCESS(status))
		device->features |= X86_ACPI_FEATURE_PRT; 

	/* Supports Bus Numbering ?  */
	status = AcpiGetHandle(device->handle, "_BBN", &null_handle);

	if (ACPI_SUCCESS(status))
		device->features |= X86_ACPI_FEATURE_BBN;

	/* Supports Bus Segment ?  */
	status = AcpiGetHandle(device->handle, "_SEG", &null_handle);

	if (ACPI_SUCCESS(status))
		device->features |= X86_ACPI_FEATURE_SEG;

	/* Supports PCI Config Space ?  */
	status = AcpiGetHandle(device->handle, "_REG", &null_handle);

	if (ACPI_SUCCESS(status))
		device->features |= X86_ACPI_FEATURE_REG;

	return AE_OK;
}

/* Gets Device Information */
ACPI_STATUS pci_get_device_hw_info(pci_device_t *device, ACPI_HANDLE dev_parent, uint32_t type)
{
	ACPI_STATUS status;
	ACPI_DEVICE_INFO *dev_info;
	char lbuf[2048];
	ACPI_BUFFER buffer;
	ACPI_PNP_DEVICE_ID_LIST *cid = NULL;
	char *hid = NULL;
	char *uid = NULL;
	const char *cid_add = NULL;

	/* Set up initial variables */
	buffer.Length = sizeof(lbuf);
	buffer.Pointer = lbuf;
	dev_info = buffer.Pointer;

	/* What are we dealing with? */
	switch (type)
	{
		/* Normal Device */
		case ACPI_BUS_TYPE_DEVICE:
		{
			/* Get Object Info */
			status = AcpiGetObjectInfo(device->handle, &dev_info);

			/* Sanity */
			if (ACPI_FAILURE(status))
				return status;

			/* Get only valid fields */
			if (dev_info->Valid & ACPI_VALID_HID)
				hid = dev_info->HardwareId.String;
			if (dev_info->Valid & ACPI_VALID_UID)
				uid = dev_info->UniqueId.String;
			if (dev_info->Valid & ACPI_VALID_CID)
				cid = &dev_info->CompatibleIdList;
			if (dev_info->Valid & ACPI_VALID_ADR)
			{
				device->address = dev_info->Address;
				device->features |= X86_ACPI_FEATURE_ADR;
			}

			/* Check for special device, i.e Video / Bay / Dock */
			if (pci_device_is_video(device) == AE_OK)
				cid_add = "VIDEO";
			else if (pci_device_is_dock(device) == AE_OK)
				cid_add = "DOCK";
			else if (pci_device_is_bay(device) == AE_OK)
				cid_add = "BAY";
			
		} break;

		case ACPI_BUS_SYSTEM:
			hid = "LNXSYBUS";
			break;
		case ACPI_BUS_TYPE_POWER:
			hid = "LNXPWRBN";
			break;
		case ACPI_BUS_TYPE_PROCESSOR:
			hid = "LNXCPU";
			break;
		case ACPI_BUS_TYPE_SLEEP:
			hid = "LNXSLPBN";
			break;
		case ACPI_BUS_TYPE_THERMAL:
			hid = "LNXTHERM";
			break;
		case ACPI_BUS_TYPE_PWM:
			hid = "LNXPOWER";
			break;
	}

	/* Fix for Root System Bus (\_SB) */
	if (((ACPI_HANDLE)dev_parent == ACPI_ROOT_OBJECT) && (type == ACPI_BUS_TYPE_DEVICE)) 
		hid = "LNXSYSTM";

	/* Store HID and UID */
	if (hid) 
	{
		strcpy(device->hid, hid);
		device->features |= X86_ACPI_FEATURE_HID;
	}
	
	if (uid) 
	{
		strcpy(device->uid, uid);
		device->features |= X86_ACPI_FEATURE_UID;
	}
	
	/* Now store CID */
	if (cid != NULL || cid_add != NULL)
	{
		ACPI_PNP_DEVICE_ID_LIST *list;
		ACPI_SIZE size = 0;
		UINT32 count = 0;

		/* Get size if list exists */
		if (cid)
			size = cid->ListSize;
		else if (cid_add)
		{
			/* Allocate a bare structure */
			size = sizeof(ACPI_PNP_DEVICE_ID_LIST);
			cid = ACPI_ALLOCATE_ZEROED(size);

			/* Set */
			cid->ListSize = size;
			cid->Count = 0;
		}

		/* Do we need to manually add extra entry ? */
		if (cid_add)
			size += sizeof(ACPI_PNP_DEVICE_ID_LIST);

		/* Allocate new list */
		list = (ACPI_PNP_DEVICE_ID_LIST*)kmalloc((size_t)size);

		/* Copy list */
		if (cid)
		{
			memcpy(list, cid, cid->ListSize);
			count = cid->Count;
		}
		
		if (cid_add)
		{
			list->Ids[count].Length = sizeof(cid_add) + 1;
			list->Ids[count].String = (char*)kmalloc(sizeof(cid_add) + 1);
			strncpy(list->Ids[count].String, cid_add, sizeof(cid_add));
			count++;
		}

		/* Set information */
		list->Count = count;
		list->ListSize = size;
		device->cid = list;
		device->features |= X86_ACPI_FEATURE_CID;
	}

	return AE_OK;
}

/* Scan Callback */
ACPI_STATUS pci_subscan_callback(ACPI_HANDLE handle, UINT32 level, void *context, void **return_value)
{
	pci_device_t *parent_device = (pci_device_t*)context;
	pci_device_t *device = NULL;
	ACPI_STATUS status = AE_OK;
	ACPI_OBJECT_TYPE type = 0;

	/* Have we already enumerated this device? */
	/* HINT, look at attached data */

	/* Get Type */
	status = AcpiGetType(handle, &type);

	if (ACPI_FAILURE(status))
		return AE_OK;

	/* We dont want ALL types obviously */
	switch (type)
	{
		case ACPI_TYPE_DEVICE:
			type = ACPI_BUS_TYPE_DEVICE;
			break;
		case ACPI_TYPE_PROCESSOR:
			type = ACPI_BUS_TYPE_PROCESSOR;
			break;
		case ACPI_TYPE_THERMAL:
			type = ACPI_BUS_TYPE_THERMAL;
			break;
		case ACPI_TYPE_POWER:
			type = ACPI_BUS_TYPE_PWM;
		default:
			return AE_OK;
	}

	/* Add object to list */
	printf("Subchild adding %u\n", type);
	device = pci_add_object(handle, parent_device->handle, type);

	/* Sanity */
	if (!device)
		return AE_CTRL_DEPTH;

	//acpi_scan_init_hotplug(device);

	return AE_OK;
}

/* Adds an object to the Acpi List */
pci_device_t *pci_add_object(ACPI_HANDLE handle, ACPI_HANDLE parent, uint32_t type)
{
	ACPI_STATUS status;
	//ACPI_PCI_ID pci_id;
	pci_device_t *device;

	/* Allocate Resources */
	device = (pci_device_t*)kmalloc(sizeof(pci_device_t));

	/* Memset */
	memset(device, 0, sizeof(pci_device_t));

	/* Set handle */
	device->handle = handle;

	/* Get Bus Identifier */
	pci_get_bus_id(device, type);

	/* Which namespace functions is supported? */
	pci_get_features(device);

	/* Get Bus and Seg Number */
	pci_get_device_bus_seg(device);

	/* Check device status */
	switch (type)
	{
		/* Same handling for these */
		case ACPI_BUS_TYPE_DEVICE:
		case ACPI_BUS_TYPE_PROCESSOR:
		{
			/* Get Status */
			status = pci_get_device_status(device);

			if (ACPI_FAILURE(status))
			{
				kfree(device);
				return NULL;
			}

			/* Is it present and functioning? */
			if (!(device->status & ACPI_STA_DEVICE_PRESENT) &&
				!(device->status & ACPI_STA_DEVICE_FUNCTIONING))
			{
				kfree(device);
				return NULL;
			}
		}

		default:
			device->status = ACPI_STA_DEVICE_PRESENT | ACPI_STA_DEVICE_ENABLED |
								ACPI_STA_DEVICE_UI | ACPI_STA_DEVICE_FUNCTIONING;
	}

	/* Now, get HID, ADDR and UUID */
	status = pci_get_device_hw_info(device, parent, type);
	
	if (ACPI_FAILURE(status))
	{
		kfree(device);
		return NULL;
	}

	/* Store the device structure with the object itself */
	status = pci_device_set_data(device, type);
	
	if (ACPI_FAILURE(status))
	{
		kfree(device);
		return NULL;
	}

	/* Convert ADR to device / function */
	if (device->features & X86_ACPI_FEATURE_ADR)
	{
		device->dev = ACPI_HIWORD(ACPI_LODWORD(device->address));
		device->func = ACPI_LOWORD(ACPI_LODWORD(device->address));

		/* Sanity Values */
		if (device->dev > 31)
			device->dev = 0;
		if (device->func > 8)
			device->func = 0;
	}
	else
	{
		device->dev = 0;
		device->func = 0;
	}

	/* Here we would handle all kinds of shizzle */
	/*printf("[%u:%u:%u]: %s (Name %s, Flags 0x%x)\n", device->bus,
		device->dev, device->func, device->hid, device->bus_id, device->features);*/

	/* Does it contain routings */
	if (device->features & X86_ACPI_FEATURE_PRT)
		pci_get_device_irq_routings(device);

	/* Is this root bus? */
	if (strncmp(device->hid, "PNP0A03", 7) == 0 ||
		strncmp(device->hid, "PNP0A08", 7) == 0)	/* PCI or PCI-express */
	{
		/* Save it root bridge list */

		device->type = ACPI_BUS_ROOT_BRIDGE;
		device->bus = glb_bus_counter;
		glb_bus_counter++;

		/* Perform PCI Config Space Initialization */
		AcpiInstallAddressSpaceHandler(device->handle, ACPI_ADR_SPACE_PCI_CONFIG, ACPI_DEFAULT_HANDLER, NULL, NULL);

	}
	else
		device->type = type;

	/* Add to list and return */
	list_append(glb_pci_acpica, list_create_node(device->type, device));

	return device;
}

/* Scan Callback */
ACPI_STATUS pci_scan_callback(ACPI_HANDLE handle, UINT32 level, void *context, void **return_value)
{
	pci_device_t *device = NULL;
	ACPI_STATUS status = AE_OK;
	ACPI_OBJECT_TYPE type = 0;
	ACPI_HANDLE parent = (ACPI_HANDLE)context;

	/* Have we already enumerated this device? */
	/* HINT, look at attached data */

	/* Get Type */
	status = AcpiGetType(handle, &type);

	if (ACPI_FAILURE(status))
		return AE_OK;

	/* We dont want ALL types obviously */
	switch (type)
	{
		case ACPI_TYPE_DEVICE:
			type = ACPI_BUS_TYPE_DEVICE;
			break;
		case ACPI_TYPE_PROCESSOR:
			type = ACPI_BUS_TYPE_PROCESSOR;
			break;
		case ACPI_TYPE_THERMAL:
			type = ACPI_BUS_TYPE_THERMAL;
			break;
		case ACPI_TYPE_POWER:
			type = ACPI_BUS_TYPE_PWM;
		default:
			return AE_OK;
	}

	/* Get Parent */
	status = AcpiGetParent(handle, &parent);

	/* Add object to list */
	device = pci_add_object(handle, parent, type);
	
	/* Sanity */
	if (!device)
		return AE_CTRL_DEPTH;

	//acpi_scan_init_hotplug(device);

	return AE_OK;
}

/* Scans a bus from a given start object */
void pci_scan_device_bus(ACPI_HANDLE handle)
{
	ACPI_STATUS status = AE_OK;

	/* Walk */
	status = AcpiWalkNamespace(ACPI_TYPE_ANY, handle, ACPI_UINT32_MAX, pci_scan_callback, NULL, (void*)handle, NULL);
}

void drivers_enumerate_acpica(void)
{
	/* Step 1. Enumerate Fixed Objects */
	if (AcpiGbl_FADT.Flags & ACPI_FADT_POWER_BUTTON)
		pci_add_object(NULL, ACPI_ROOT_OBJECT, ACPI_BUS_TYPE_POWER);

	if (AcpiGbl_FADT.Flags & ACPI_FADT_SLEEP_BUTTON)
		pci_add_object(NULL, ACPI_ROOT_OBJECT, ACPI_BUS_TYPE_SLEEP);

	/* Step 2. Enumerate bus */
	pci_scan_device_bus(ACPI_ROOT_OBJECT);
}

/* This installs a driver for each device present (if we have a driver!) */
void drivers_setup_device(void *data, int n)
{
	pci_driver_t *driver = (pci_driver_t*)data;
	list_t *sub_bus;
	n = n;

	switch (driver->type)
	{
		case X86_PCI_TYPE_BRIDGE:
		{
			/* Get bus list */
			sub_bus = (list_t*)driver->children;

			/* Install drivers on that bus */
			list_execute_all(sub_bus, drivers_setup_device);

		} break;

		case X86_PCI_TYPE_DEVICE:
		{
			/* Get driver */

			/* Serial Bus Comms */
			if (driver->header->class_code == 0x0C)
			{
				/* Usb? */
				if (driver->header->subclass == 0x03)
				{
					/* Controller Type? */

					/* UHCI -> 0. OHCI -> 0x10. EHCI -> 0x20. xHCI -> 0x30 */
					if (driver->header->ProgIF == 0x10)
					{
						/* Initialise Controller */
						ohci_init(driver, pci_device_get_irq(driver->bus, driver->device, driver->header->interrupt_pin));
					}
				}
			}

		} break;

		default:
			break;
	}
}

/* Initialises all available drivers in system */
void drivers_init(void)
{
	/* Init list, this is "bus 0" */
	glb_pci_devices = list_create(LIST_SAFE);
	glb_pci_acpica = list_create(LIST_SAFE);

	/* Start out by enumerating devices */
	drivers_enumerate();
	drivers_enumerate_acpica();

	/* Fixed Driver Install Here */
	clock_init();

	/* Now, for each driver we have available install it */
	list_execute_all(glb_pci_devices, drivers_setup_device);
}