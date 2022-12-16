/**
 * Copyright 2017, Philip Meulengracht
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
 * PE Image Definitions & Structures
 * - This header describes the pe structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */
#ifndef __OS_PEIMAGE__
#define __OS_PEIMAGE__

#include <os/osdefs.h>

/* Magic Constants used for data integrity */
#define MZ_MAGIC                            0x5A4D
#define PE_MAGIC                            0x00004550

/* Used by PeValidate, and is returned in either case
 * to indicate whether or not the PE file is a valid file */
#define PE_INVALID                          0
#define PE_VALID                            1

/* The available values for the machine field in the
 * PE headers, and must of course match the current machine
 * that is running the pe-file, otherwise its build for a 
 * differnet machine */
#define PE_MACHINE_UNKNOWN                  0x0
#define PE_MACHINE_AM33                     0x1D3
#define PE_MACHINE_X64                      0x8664
#define PE_MACHINE_ARM                      0x1C0
#define PE_MACHINE_ARMNT                    0x1C4
#define PE_MACHINE_ARM64                    0xAA64
#define PE_MACHINE_EFI                      0xEBC
#define PE_MACHINE_X32                      0x14C
#define PE_MACHINE_IA64                     0x200

/* Describes any special attribute that the PE-file
 * expects to be executed with. Could be no relocations
 * large addresses or only 32 bit */
#define PE_ATTRIBUTE_NORELOCATION           0x0001
#define PE_ATTRIBUTE_VALID                  0x0002
#define PE_ATTRIBUTE_NOLINENUMS             0x0004
#define PE_ATTRIBUTE_LARGEADDRESSES         0x0020
#define PE_ATTRIBUTE_32BIT                  0x0100
#define PE_ATTRIBUTE_NODEBUG                0x0200
#define PE_ATTRIBUTE_SYSTEM                 0x1000
#define PE_ATTRIBUTE_DLL                    0x2000

/* Available architectures for a PE-file. Which at the
 * moment only means 32 and 64 bit archs */
#define PE_ARCHITECTURE_32                  0x10B
#define PE_ARCHITECTURE_64                  0x20B

/* The available subsystem types in the subsystem
 * field of the PE headers. Use this to determine the
 * kind of environment the PE-file expects to be executed
 * in */
#define PE_SUBSYSTEM_UNKNOWN                0x0
#define PE_SUBSYSTEM_NATIVE                 0x1
#define PE_SUBSYSTEM_WINDOWS_GUI            0x2
#define PE_SUBSYSTEM_WINDOWS_CUI            0x3
#define PE_SUBSYSTEM_POSIX_CUI              0x7
#define PE_SUBSYSTEM_WINDOWS_CE_CUI         0x9
#define PE_SUBSYSTEM_EFI_APPLICATION        0xA
#define PE_SUBSYSTEM_EFI_BOOT_SERVICE       0xB
#define PE_SUBSYSTEM_EFI_RUNTIME_DRV        0xC
#define PE_SUBSYSTEM_EFI_ROM                0xD
#define PE_SUBSYSTEM_XBOX                   0xE

#define PE_DLL_ATTRIBUTE_DYNAMIC            0x0040
#define PE_DLL_ATTRIBUTE_FORCE_INTEGRITY    0x0080
#define PE_DLL_ATTRIBUTE_NX_COMPAT          0x0100
#define PE_DLL_ATTRIBUTE_NO_ISOLATION       0x0200
#define PE_DLL_ATTRIBUTE_NO_SEH             0x0400
#define PE_DLL_ATTRIBUTE_NO_BIND            0x0800
#define PE_DLL_ATTRIBUTE_WDM_DRIVER         0x2000
#define PE_DLL_ATTRIBUTE_TERMINAL_AWARE     0x8000


/* Section definitions, these are the possible
 * section types available in a PE image. Not all these
 * are relevant for MollenOS */
#define PE_SECTION_EXPORT                   0x0
#define PE_SECTION_IMPORT                   0x1
#define PE_SECTION_RESOURCE                 0x2
#define PE_SECTION_EXCEPTION                0x3
#define PE_SECTION_CERTIFICATE              0x4
#define PE_SECTION_BASE_RELOCATION          0x5
#define PE_SECTION_DEBUG                    0x6
#define PE_SECTION_ARCHITECTURE             0x7
#define PE_SECTION_GLOBAL_PTR               0x8 // VPE uses this for auto-relocations
#define PE_SECTION_TLS                      0x9
#define PE_SECTION_LOAD_CONFIG              0xA
#define PE_SECTION_BOUND_IMPORT             0xB
#define PE_SECTION_IAT                      0xC // Import Address Table
#define PE_SECTION_DID                      0xD // Delay Import Descriptor
#define PE_SECTION_CLR                      0xE // CLR Runtime Header

#define PE_NUM_DIRECTORIES                  0x10
#define PE_SECTION_NAME_LENGTH              8

/* Section flags definitions, every section has
 * some available flags to determine which kind of
 * section it is, and how it should be handled */
#define PE_SECTION_NO_PADDING               0x00000008
#define PE_SECTION_CODE                     0x00000020
#define PE_SECTION_DATA                     0x00000040
#define PE_SECTION_BSS                      0x00000080
#define PE_SECTION_INFO                     0x00000200
#define PE_SECTION_IGNORE                   0x00000800
#define PE_SECTION_COMDAT                   0x00001000
#define PE_SECTION_GPREL                    0x00008000
#define PE_SECTION_EXT_RELOC                0x01000000    // If this is set, see below
#define PE_SECTION_DISCARDABLE              0x02000000
#define PE_SECTION_NOT_CACHED               0x04000000
#define PE_SECTION_NOT_PAGED                0x08000000
#define PE_SECTION_SHARED                   0x10000000
#define PE_SECTION_EXECUTE                  0x20000000
#define PE_SECTION_READ                     0x40000000
#define PE_SECTION_WRITE                    0x80000000

/* If PE_SECTION_EXT_RELOC is set, then the actual relocation count 
 * is stored in the 32 bit virtual-address field of the first relocation entry */

/* Relocation Types */
#define PE_RELOCATION_ALIGN                 0
#define PE_RELOCATION_HIGH                  1
#define PE_RELOCATION_LOW                   2
#define PE_RELOCATION_HIGHLOW               3
#define PE_RELOCATION_HIGHADJ               4
#define PE_RELOCATION_RELATIVE64            10

/* Import Types */
#define PE_IMPORT_CODE                      0
#define PE_IMPORT_DATA                      1
#define PE_IMPORT_CONST                     2

#define PE_IMPORT_NAME_ORDINAL              0
#define PE_IMPORT_NAME                      1
#define PE_IMPORT_NAME_NOPREFIX             2
#define PE_IMPORT_NAME_UNDECORATE           3

#define PE_IMPORT_ORDINAL_32                0x80000000
#define PE_IMPORT_NAMEMASK                  0x7FFFFFFF
#define PE_IMPORT_ORDINAL_64                0x8000000000000000

/* Debug Types */
#define PE_DEBUG_TYPE_UNKNOWN               0
#define PE_DEBUG_TYPE_COFF                  1
#define PE_DEBUG_TYPE_PDB                   2
#define PE_DEBUG_TYPE_FPO                   3
#define PE_DEBUG_TYPE_DBG                   4
#define PE_DEBUG_TYPE_EXCEPTION             5
#define PE_DEBUG_TYPE_FIXUP                 6
#define PE_DEBUG_TYPE_OMAP2SRC              7
#define PE_DEBUG_TYPE_OMAP_FROM_SRC         8
#define PE_DEBUG_TYPE_BORLAND               9
#define PE_DEBUG_TYPE_RESERVED              10
#define PE_DEBUG_TYPE_CLSID                 11

/**
 * The MZ header which is the base for a PE/COM/DOS file, extra headers are added afterwards
 */
PACKED_TYPESTRUCT(MzHeader, {
    uint16_t Signature;
    uint16_t PageExtraBytes; // Extra Bytes in last page
    uint16_t NumPages;
    uint16_t NumRelocations;
    uint16_t HeaderSize;
    uint16_t MinAllocation;
    uint16_t MaxAllocation;
    uint16_t InitialSS;
    uint16_t InitialSP;
    uint16_t Checksum;
    uint16_t InitialIP;
    uint16_t InitialCS;
    uint16_t RelocationTableAddress;
    uint16_t Overlay;
    uint16_t Reserved0[4];
    uint16_t OemId;
    uint16_t OemInfo;
    uint16_t Reserved1[10];
    uint32_t PeHeaderAddress;
});

/**
 * The PE header, which is an extension of the MZ header, and adds a lot of new features
 */
PACKED_TYPESTRUCT(PeHeader, {
    uint32_t Magic;
    uint16_t Machine;
    uint16_t NumSections;
    uint32_t DateTimeStamp; // Date is offset from 1970 January 1
    uint32_t SymbolTableOffset; // File offset
    uint32_t NumSymbolsInTable;
    uint16_t SizeOfOptionalHeader;
    uint16_t Attributes;
});

/**
 * Describes a data-directory entry in the PE metadata. There is a max of 16 directories, and each
 * entry is fixed for its type/index */
PACKED_TYPESTRUCT(PeDataDirectory, {
    uint32_t AddressRVA;
    uint32_t Size;
});

/**
 * This is the shared optional header, we can use it to determine which optional-version to use. It follows
 * directly after the base header
 */
PACKED_TYPESTRUCT(PeOptionalHeader, {
    uint16_t Architecture;
    uint8_t  LinkerVersionMajor;
    uint8_t  LinkerVersionMinor;
    uint32_t SizeOfCode;
    uint32_t SizeOfData;
    uint32_t SizeOfBss;
    uint32_t EntryPointRVA;
    uint32_t BaseOfCode;
});

/* PE-Optional-Header
 * The 32 bit version of the optional header */
PACKED_TYPESTRUCT(PeOptionalHeader32, {
    PeOptionalHeader_t  Base;
    uint32_t            BaseOfData;
    uint32_t            BaseAddress;
    uint32_t            SectionAlignment;
    uint32_t            FileAlignment;
    uint8_t             Unused[16];
    uint32_t            SizeOfImage; // Must be multiple of SectionAlignment
    uint32_t            SizeOfHeaders; // Must be a multiple of FileAlignment
    uint32_t            ImageChecksum;
    uint16_t            SubSystem;
    uint16_t            DllAttributes;
    uint8_t             Reserved[16];
    uint32_t            LoaderFlags;
    uint32_t            NumDataDirectories;
    PeDataDirectory_t   Directories[PE_NUM_DIRECTORIES];
});

/* PE-Optional-Header
 * The 64 bit version of the optional header */
PACKED_TYPESTRUCT(PeOptionalHeader64, {
    PeOptionalHeader_t  Base;
    uint64_t            BaseAddress;
    uint32_t            SectionAlignment;
    uint32_t            FileAlignment;
    uint8_t             Unused[16];
    uint32_t            SizeOfImage; // Must be multiple of SectionAlignment
    uint32_t            SizeOfHeaders; // Must be a multiple of FileAlignment
    uint32_t            ImageChecksum;
    uint16_t            SubSystem;
    uint16_t            DllAttributes;
    uint8_t             Reserved[32];
    uint32_t            LoaderFlags;
    uint32_t            NumDataDirectories;
    PeDataDirectory_t   Directories[PE_NUM_DIRECTORIES];
});

PACKED_TYPESTRUCT(PeSectionHeader, {
    uint8_t             Name[PE_SECTION_NAME_LENGTH];
    uint32_t            VirtualSize;
    uint32_t            VirtualAddress;
    uint32_t            RawSize;
    uint32_t            RawAddress;
    uint32_t            PointerToFileRelocations;
    uint32_t            PointerToFileLineNumbers;
    uint16_t            NumRelocations;
    uint16_t            NumLineNumbers;
    uint32_t            Flags;
});

/* PE-Directory
 * The Debug Directory, either it actually contains
 * debug information (old versions of pe), or it contains
 * a PDB entry as described by the pdb header */
PACKED_TYPESTRUCT(PeDebugDirectory, {
    uint32_t            Flags;
    uint32_t            TimeStamp;
    uint16_t            MajorVersion;
    uint16_t            MinorVersion;
    uint32_t            Type;
    uint32_t            SizeOfData;
    uint32_t            AddressOfRawData;
    uint32_t            PointerToRawData;
});

/* PE-PDB Entry Header
 * Describes where and the name of the pdb
 * file resides, usually in same folder as binary
 * as the name is just the name of the file */
PACKED_TYPESTRUCT(PePdbInformation, {
    uint32_t            Signature;
    uint8_t             Guid[16];
    uint32_t            Age;
    char                PdbFileName[1];
});

PACKED_TYPESTRUCT(PeExportDirectory, {
    // Flags is reserved, must be 0.
    uint32_t Flags;
    // TimeStamp is the time and date that the export data was created.
    uint32_t TimeStamp;
    // VersionMajor/VersionMinor is the major and minor version number.
    // The major and minor version numbers can be set by the user.
    uint16_t VersionMajor;
    uint16_t VersionMinor;
    // DllName is the address of the ASCII string that contains the name of the DLL.
    // This address is relative to the image base. It is possible for one DLL to export
    // functions from another DLL.
    uint32_t DllName;
    // OrdinalBase is the starting ordinal number for exports in this image.
    // This field specifies the starting ordinal number for the export address table.
    // It is usually set to 1.
    uint32_t OrdinalBase;
    // NumberOfFunctions is the number of entries in the export address table.
    uint32_t NumberOfFunctions;
    // NumberOfNames is the number of entries in the name pointer table.
    // This is also the number of entries in the ordinal table.
    uint32_t NumberOfNames;
    // AddressOfFunctions is the address of the export address table, relative to the image base.
    uint32_t AddressOfFunctions;
    // AddressOfNames is the address of the export name pointer table, relative to the image base.
    // The table size is given by the NumberOfNames field.
    uint32_t AddressOfNames;
    // AddressOfOrdinals is the address of the ordinal table, relative to the image base.
    uint32_t AddressOfOrdinals;
});

PACKED_TYPESTRUCT(PeImportDirectory, {
    uint16_t            Signature1; // Must be 0 
    uint16_t            Signature2; // Must be 0xFFFF
    uint16_t            Version;
    uint16_t            Machine;
    uint32_t            TimeStamp;
    uint32_t            DataSize;
    uint16_t            Ordinal;
    /* Flags 
     * Bits 0:1 - Import Type 
     * Bits 2:4 - Import Name Type */
    uint16_t            Flags;
});

PACKED_TYPESTRUCT(PeImportDescriptor, {
    union {
        uint32_t Attributes;
        // ImportLookupTable is the RVA of the ILT.
        uint32_t ImportLookupTable;
    } Variable;
    // TimeStamp is initially set to 0 if not bound and set to -1 if bound.
    // In case of an unbound import the time date stamp gets updated to the time date
    // stamp of the DLL after the image is bound.
    // In case of a bound import it stays set to -1 and the real time date stamp of
    // the DLL can be found in the Bound Import Directory Table in the corresponding
    // IMAGE_BOUND_IMPORT_DESCRIPTOR .
    uint32_t TimeStamp;
    // ForwarderChainId is the index of the first forwarder chain reference.
    // This is something responsible for DLL forwarding. (DLL forwarding is when
    // a DLL forwards some of its exported functions to another DLL.) This has the
    // value of -1 if there are no forwarder chains. Originally used for old-style
    // binding.
    uint32_t ForwarderChainId;
    // ModuleName is an RVA of an ASCII string that contains the name of the imported DLL.
    uint32_t ModuleName;
    // ImportAddressTable is the RVA of the IAT. On disk, the IAT is identical to the ILT,
    // however during bounding when the binary is being loaded into memory, the entries
    // of the IAT get overwritten with the addresses of the functions that are being imported.
    // If this is bound, then this IAT contains a table of fixed address. The IAT is a duplicate
    // of the ILT.
    uint32_t ImportAddressTable;
});

PACKED_TYPESTRUCT(PeImportNameDescriptor, {
    uint16_t OrdinalHint;
    uint8_t  Name[1]; // RVA
});

#endif //!__OS_PEIMAGE__
