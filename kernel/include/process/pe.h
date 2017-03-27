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
 * MollenOS MCore - PE Format Loader
 */

#ifndef __MCORE_PELOADER__
#define __MCORE_PELOADER__

/* Includes 
 * - System */
#include <arch.h>

/* Includes
 * - C-Library */
#include <ds/list.h>
#include <ds/mstring.h>
#include <os/osdefs.h>

/* Magic Constants used for data integrity */
#define MZ_MAGIC			0x5A4D
#define PE_MAGIC			0x00004550

/* Used by PeValidate, and is returned in either case
 * to indicate whether or not the PE file is a valid file */
#define PE_INVALID			0
#define PE_VALID			1

/* The available values for the machine field in the
 * PE headers, and must of course match the current machine
 * that is running the pe-file, otherwise its build for a 
 * differnet machine */
#define PE_MACHINE_UNKNOWN	0x0
#define PE_MACHINE_AM33		0x1D3
#define PE_MACHINE_X64		0x8664
#define PE_MACHINE_ARM		0x1C0
#define PE_MACHINE_ARMNT	0x1C4
#define PE_MACHINE_ARM64	0xAA64
#define PE_MACHINE_EFI		0xEBC
#define PE_MACHINE_X32		0x14C
#define PE_MACHINE_IA64		0x200

/* Describes any special attribute that the PE-file
 * expects to be executed with. Could be no relocations
 * large addresses or only 32 bit */
#define PE_ATTRIBUTE_NORELOCATION		0x0001
#define PE_ATTRIBUTE_VALID				0x0002
#define PE_ATTRIBUTE_NOLINENUMS			0x0004
#define PE_ATTRIBUTE_LARGEADDRESSES		0x0020
#define PE_ATTRIBUTE_32BIT				0x0100
#define PE_ATTRIBUTE_NODEBUG			0x0200
#define PE_ATTRIBUTE_SYSTEM				0x1000
#define PE_ATTRIBUTE_DLL				0x2000

/* Available architectures for a PE-file. Which at the
 * moment only means 32 and 64 bit archs untill 128 some day comes :-) */
#define PE_ARCHITECTURE_32				0x10B
#define PE_ARCHITECTURE_64				0x20B

/* The available subsystem types in the subsystem
 * field of the PE headers. Use this to determine the
 * kind of environment the PE-file expects to be executed
 * in */
#define PE_SUBSYSTEM_UNKNOWN			0x0
#define PE_SUBSYSTEM_NATIVE				0x1
#define PE_SUBSYSTEM_WINDOWS_GUI		0x2
#define PE_SUBSYSTEM_WINDOWS_CUI		0x3
#define PE_SUBSYSTEM_POSIX_CUI			0x7
#define PE_SUBSYSTEM_WINDOWS_CE_CUI		0x9
#define PE_SUBSYSTEM_EFI_APPLICATION	0xA
#define PE_SUBSYSTEM_EFI_BOOT_SERVICE	0xB
#define PE_SUBSYSTEM_EFI_RUNTIME_DRV	0xC
#define PE_SUBSYSTEM_EFI_ROM			0xD
#define PE_SUBSYSTEM_XBOX				0xE

#define PE_DLL_ATTRIBUTE_DYNAMIC			0x0040
#define PE_DLL_ATTRIBUTE_FORCE_INTEGRITY	0x0080
#define PE_DLL_ATTRIBUTE_NX_COMPAT			0x0100
#define PE_DLL_ATTRIBUTE_NO_ISOLATION		0x0200
#define PE_DLL_ATTRIBUTE_NO_SEH				0x0400
#define PE_DLL_ATTRIBUTE_NO_BIND			0x0800
#define PE_DLL_ATTRIBUTE_WDM_DRIVER			0x2000
#define PE_DLL_ATTRIBUTE_TERMINAL_AWARE		0x8000

/* We define some helpers in order to determine 
 * some information about the current build, this
 * is so we can do proper validation of the loaded
 * binary */
#if defined(_X86_32) || defined(_X86_64)
#ifdef _X86_32
#define PE_CURRENT_MACHINE	PE_MACHINE_X32
#define PE_CURRENT_ARCH		PE_ARCHITECTURE_32
#else
#define PE_CURRENT_MACHINE	PE_MACHINE_X64
#define PE_CURRENT_ARCH		PE_ARCHITECTURE_64
#endif
#else
/* Uh arm and such.. */
#endif

/* Section definitions, these are the possible
 * section types available in a PE image. Not all these
 * are relevant for MollenOS */
#define PE_SECTION_EXPORT			0x0
#define PE_SECTION_IMPORT			0x1
#define PE_SECTION_RESOURCE			0x2
#define PE_SECTION_EXCEPTION		0x3
#define PE_SECTION_CERTIFICATE		0x4
#define PE_SECTION_BASE_RELOCATION	0x5
#define PE_SECTION_DEBUG			0x6
#define PE_SECTION_ARCHITECTURE		0x7
#define PE_SECTION_GLOBAL_PTR		0x8
#define PE_SECTION_TLS				0x9
#define PE_SECTION_LOAD_CONFIG		0xA
#define PE_SECTION_BOUND_IMPORT		0xB
#define PE_SECTION_IAT				0xC /* Import Address Table */
#define PE_SECTION_DID				0xD /* Delay Import Descriptor */
#define PE_SECTION_CLR				0xE /* CLR Runtime Header */

#define PE_NUM_DIRECTORIES			0x10
#define PE_SECTION_NAME_LENGTH		8

/* Section flags definitions, every section has
 * some available flags to determine which kind of
 * section it is, and how it should be handled */
#define PE_SECTION_NO_PADDING		0x00000008
#define PE_SECTION_CODE				0x00000020
#define PE_SECTION_DATA				0x00000040
#define PE_SECTION_BSS				0x00000080
#define PE_SECTION_INFO				0x00000200
#define PE_SECTION_IGNORE			0x00000800
#define PE_SECTION_COMDAT			0x00001000
#define PE_SECTION_GPREL			0x00008000
#define PE_SECTION_EXT_RELOC		0x01000000	/* If this is set, see below */
#define PE_SECTION_DISCARDABLE		0x02000000
#define PE_SECTION_NOT_CACHED		0x04000000
#define PE_SECTION_NOT_PAGED		0x08000000
#define PE_SECTION_SHARED			0x10000000
#define PE_SECTION_EXECUTE			0x20000000
#define PE_SECTION_READ				0x40000000
#define PE_SECTION_WRITE			0x80000000

/* If PE_SECTION_EXT_RELOC is set, then the actual relocation count 
 * is stored in the 32 bit virtual-address field of the first relocation entry */

/* Relocation Types */
#define PE_RELOCATION_ALIGN				0
#define PE_RELOCATION_HIGH				1
#define PE_RELOCATION_LOW				2
#define PE_RELOCATION_HIGHLOW			3
#define PE_RELOCATION_HIGHADJ			4

/* Import Types */
#define PE_IMPORT_CODE					0
#define PE_IMPORT_DATA					1
#define PE_IMPORT_CONST					2

#define PE_IMPORT_NAME_ORDINAL			0
#define PE_IMPORT_NAME					1
#define PE_IMPORT_NAME_NOPREFIX			2
#define PE_IMPORT_NAME_UNDECORATE		3

#define PE_IMPORT_ORDINAL_32			0x80000000
#define PE_IMPORT_NAMEMASK				0x7FFFFFFF
#define PE_IMPORT_ORDINAL_64			0x8000000000000000

/* Debug Types */
#define PE_DEBUG_TYPE_UNKNOWN			0
#define PE_DEBUG_TYPE_COFF				1
#define PE_DEBUG_TYPE_PDB				2
#define PE_DEBUG_TYPE_FPO				3
#define PE_DEBUG_TYPE_DBG				4
#define PE_DEBUG_TYPE_EXCEPTION			5
#define PE_DEBUG_TYPE_FIXUP				6
#define PE_DEBUG_TYPE_OMAP2SRC			7
#define PE_DEBUG_TYPE_OMAP_FROM_SRC		8
#define PE_DEBUG_TYPE_BORLAND			9
#define PE_DEBUG_TYPE_RESERVED			10
#define PE_DEBUG_TYPE_CLSID				11

/* The MZ header which is the base for a PE/COM/DOS
 * file, extra headers are added afterwards */
#pragma pack(push, 1)
typedef struct _MzHeader
{
	/* Signature */
	uint16_t Signature;

	/* Extra Bytes in last page */
	uint16_t PageExtraBytes;

	/* Number of whole Pages */
	uint16_t NumPages;

	/* Number of relocation entries */
	uint16_t NumRelocations;

	/* Header Size */
	uint16_t HeaderSize;

	/* Allocation Sizes */
	uint16_t MinAllocation;
	uint16_t MaxAllocation;

	/* Stack Values */
	uint16_t InitialSS;
	uint16_t InitialSP;

	/* Checksum */
	uint16_t Checksum;

	/* Code Values */
	uint16_t InitialIP;
	uint16_t InitialCS;

	/* Relocation Table */
	uint16_t RelocationTableAddr;

	/* Overlay */
	uint16_t Overlay;

	/* Reserved */
	uint16_t Reserved0[4];

	/* Oem Information */
	uint16_t OemId;
	uint16_t OemInfo;

	/* More Reserved */
	uint16_t Reserved1[10];

	/* Pe Header Address */
	uint32_t PeAddr;

} MzHeader_t;
#pragma pack(pop)

/* The PE header, which is an extension of the
 * MZ header, and adds a lot of new features */
#pragma pack(push, 1)
typedef struct _PeHeader
{
	/* Magic */
	uint32_t Magic;

	/* Machine */
	uint16_t Machine;

	/* Number of sections */
	uint16_t NumSections;

	/* Low 32 bits when file was created, 
	 * date is offset from 1970 January 1 */
	uint32_t DateTimeStamp;

	/* Pointer to Symbol Table
	 * (the FILE offset) */
	uint32_t SymbolTablePtr;

	/* Number of symbol entries in table */
	uint32_t NumSymbols;

	/* Size of the Optional Header */
	uint16_t SizeOfOptionalHeader;

	/* Attributes */
	uint16_t Attributes;

} PeHeader_t;
#pragma pack(pop)

/* Describes a data-directory entry in the
 * PE metadata. There is a max of 16 directories, and each
 * entry is fixed for its type/index */
#pragma pack(push, 1)
typedef struct _PeDataDirectory
{
	/* RVA Address */
	uint32_t AddressRVA;

	/* Size */
	uint32_t Size;

} PeDataDirectory_t;
#pragma pack(pop)

/* PE-Optional-Header
 * This is the shared optional header, we can use it to 
 * determine which optional-version to use. It follows
 * directly after the base header */
#pragma pack(push, 1)
typedef struct _PeOptionalHeader
{
	/* Architecture */
	uint16_t Architecture;

	/* Linker Version */
	uint8_t LinkerVersionMajor;
	uint8_t LinkerVersionMinor;

	/* Sizes, or Sums if multiple */
	uint32_t SizeOfCode;
	uint32_t SizeOfData;
	uint32_t SizeOfBss;

	/* Entry Point 
	 * Relative Offset from loaded address,
	 * not relative offset in file */
	uint32_t EntryPoint;

	/* Code Begin */
	uint32_t BaseOfCode;

} PeOptionalHeader_t;

/* PE-Optional-Header
 * The 32 bit version of the optional header */
typedef struct _PeOptionalHeader32
{
	/* Architecture */
	uint16_t Architecture;

	/* Linker Version */
	uint8_t LinkerVersionMajor;
	uint8_t LinkerVersionMinor;

	/* Sizes, or Sums if multiple */
	uint32_t SizeOfCode;
	uint32_t SizeOfData;
	uint32_t SizeOfBss;

	/* Entry Point
	* Relative Offset from loaded address,
	* not relative offset in file */
	uint32_t EntryPoint;

	/* Bases */
	uint32_t BaseOfCode;
	uint32_t BaseOfData;

	/* Preffered Base Address */
	uint32_t BaseAddress;

	/* Section Alignment (Bytes) */
	uint32_t SectionAlignment;

	/* File Alignment (Bytes) */
	uint32_t FileAlignment;

	/* Unused, windows only */
	uint8_t Unused[16];

	/* Size of Image when loaded
	 * must be a multiple of SectionAlignment */
	uint32_t SizeOfImage;

	/* Size of all headers 
	 * a multiple of FileAlignment */
	uint32_t SizeOfHeaders;

	/* Checksum of Image */
	uint32_t ImageChecksum;

	/* SubSystem */
	uint16_t SubSystem;

	/* Dll Attributes */
	uint16_t DllAttributes;

	/* Windows Stuff - 32 bytes x64 */
	uint8_t Reserved[16];

	/* Loader Flags */
	uint32_t LoaderFlags;

	/* Numbers of data directories */
	uint32_t NumDataDirectories;

	/* Data Directories */
	PeDataDirectory_t Directories[PE_NUM_DIRECTORIES];

} PeOptionalHeader32_t;

/* PE-Optional-Header
 * The 64 bit version of the optional header */
typedef struct _PeOptionalHeader64
{
	/* Architecture */
	uint16_t Architecture;

	/* Linker Version */
	uint8_t LinkerVersionMajor;
	uint8_t LinkerVersionMinor;

	/* Sizes, or Sums if multiple */
	uint32_t SizeOfCode;
	uint32_t SizeOfData;
	uint32_t SizeOfBss;

	/* Entry Point
	* Relative Offset from loaded address,
	* not relative offset in file */
	uint32_t EntryPoint;

	/* Code Begin */
	uint32_t BaseOfCode;

	/* Preffered Base Address */
	uint64_t BaseAddress;

	/* Section Alignment (Bytes) */
	uint32_t SectionAlignment;

	/* File Alignment (Bytes) */
	uint32_t FileAlignment;

	/* Unused, windows only */
	uint8_t Unused[16];

	/* Size of Image when loaded
	* must be a multiple of SectionAlignment */
	uint32_t SizeOfImage;

	/* Size of all headers
	* a multiple of FileAlignment */
	uint32_t SizeOfHeaders;

	/* Checksum of Image */
	uint32_t ImageChecksum;

	/* SubSystem */
	uint16_t SubSystem;

	/* Dll Attributes */
	uint16_t DllAttributes;

	/* Windows Stuff - 32 bytes x64 */
	uint8_t Reserved[32];

	/* Loader Flags */
	uint32_t LoaderFlags;

	/* Numbers of data directories */
	uint32_t NumDataDirectories;

	/* Data Directories */
	PeDataDirectory_t Directories[PE_NUM_DIRECTORIES];

} PeOptionalHeader64_t;
#pragma pack(pop)

/* Section Header 
 * they are located directly after 
 * the optional headers */
#pragma pack(push, 1)
typedef struct _PeSectionHeader
{
	/* Section Name */
	uint8_t Name[PE_SECTION_NAME_LENGTH];

	/* Virtual Size */
	uint32_t VirtualSize;

	/* Virtual Address */
	uint32_t VirtualAddr;

	/* File Size */
	uint32_t RawSize;

	/* Location in File */
	uint32_t RawAddr;

	/* file Pointer to relocations */
	uint32_t PtrToFileRelocations;

	/* File Pointer to line numbers */
	uint32_t PtrToFileLineNumbers;

	/* Number of relocations */
	uint16_t NumRelocations;

	/* Number of line numbers */
	uint16_t NumLineNumbers;

	/* Flags */
	uint32_t Flags;

} PeSectionHeader_t;
#pragma pack(pop)

/* PE-Directory
 * The Debug Directory, either it actually contains
 * debug information (old versions of pe), or it contains
 * a PDB entry as described by the pdb header */
typedef struct _PeDebugDirectory 
{
	/* Flags */
	uint32_t Flags;
	uint32_t TimeStamp;

	/* Major / Minor */
	uint16_t MajorVersion;
	uint16_t MinorVersion;

	/* Type */
	uint32_t Type;

	/* Size of debug data */
	uint32_t SizeOfData;
	
	/* Pointers to data */
	uint32_t AddressOfRawData;
	uint32_t PointerToRawData;

} PeDebugDirectory_t;

/* PE-PDB Entry Header
 * Describes where and the name of the pdb
 * file resides, usually in same folder as binary
 * as the name is just the name of the file */
#pragma pack(push, 1)
typedef struct _PePdbInformation
{
	/* CodeView Signature */
	uint32_t Signature;

	/* PDB Guid */
	uint8_t Guid[16];

	/* Information Age */
	uint32_t Age;

	/* Null-termianted string to PDB */
	char PdbFileName[1];

} PePdbInformation_t;
#pragma pack(pop)

/* PE-Directory
 * The Export Directory, contains a list
 * of exported functions, their ordinals and
 * function names. */
typedef struct _PeExportDirectory
{
	/* Flags */
	uint32_t Flags;

	/* DateTime Stamp */
	uint32_t TimeStamp;

	/* Major / Minor */
	uint16_t VersionMajor;
	uint16_t VersionMinor;

	/* Name of Dll */
	uint32_t DllName;

	/* Ordinal Start Nr */
	uint32_t OrdinalBase;

	/* Number of Entries */
	uint32_t NumberOfFunctions;

	/* Number of name pointers & ordinals */
	uint32_t NumberOfOrdinals;

	/* Address of the Export Table (RVA) */
	uint32_t AddressOfFunctions;
	uint32_t AddressOfNames;
	uint32_t AddressOfOrdinals;

} PeExportDirectory_t;

/* PE-Directory
 * The Import Directory, contains a list of
 * Pe-Import-Headers that each describe a new
 * image dependancy */
typedef struct _PeImportDirectory
{
	/* Signature 1 - Must be 0 */
	uint16_t Signature1;

	/* Signature 2 - Must be 0xFFFF */
	uint16_t Signature2;

	/* Structure Version */
	uint16_t Version;

	/* Machine */
	uint16_t Machine;

	/* DateTime Stamp */
	uint32_t TimeStamp;

	/* Size of Data */
	uint32_t DataSize;

	/* Ordinal Hint */
	uint16_t Ordinal;

	/* Flags 
	 * Bits 0:1 - Import Type 
	 * Bits 2:4 - Import Name Type */
	uint16_t Flags;

} PeImportDirectory_t;

/* PE-Import-Header 
 * The Import Descriptor, describes a new
 * library dependancy */
typedef struct _PeImportDescriptor
{
	/* Either it's the one,
	 * or the second */
	union {
		uint32_t Attributes;

		/* This is an RVA Address */
		uint32_t ImportLookupTable;
	} Variable;

	/* DateTimeStamp 
	 * if 0 == module not bound 
	 * This is set to 0 as long as 
	 * the image is not bound */
	uint32_t TimeStamp;

	/* Forwarder Chain Id */
	uint32_t ForwarderChainId;

	/* Module Name - RVA Address */
	uint32_t ModuleName;

	/* IAT - RVA Address */
	uint32_t ImportAddressTable;

} PeImportDescriptor_t;

/* The Pe-Image file exports a number of functions
 * and to avoid re-parsing every time we want to resolve
 * a function we cache them here, an exported function consists
 * of a name, ordinal and where they reside in memory */
typedef struct _MCorePeExportFunction {
	char *Name;
	int Ordinal;
	uintptr_t Address; /* Absolute Address */
} MCorePeExportFunction_t;

/* The Pe-Image file structure, this contains the
 * loaded binaries and libraries, the functions an 
 * image exports and base-information */
typedef struct _MCorePeFile {
	MString_t *Name;
	uint32_t Architecture;
	uintptr_t VirtualAddress;
	uintptr_t EntryAddress;
	int References;
	int UsingInitRD;
	List_t *ExportedFunctions;
	List_t *LoadedLibraries;
} MCorePeFile_t;

/* PeValidate
 * Validates a file-buffer of the given length,
 * does initial header checks and performs a checksum
 * validation. Returns either PE_INVALID or PE_VALID */
__EXTERN int PeValidate(uint8_t *Buffer, size_t Length);

/* PeResolveLibrary
 * Resolves a dependancy or a given module path, a load address must be provided
 * together with a pe-file header to fill out and the parent that wants to resolve
 * the library */
__EXTERN MCorePeFile_t *PeResolveLibrary(MCorePeFile_t *Parent,
	MCorePeFile_t *PeFile, MString_t *LibraryName, uintptr_t *LoadAddress);

/* PeResolveFunction
 * Resolves a function by name in the given pe image, the return
 * value is the address of the function */
__EXTERN uintptr_t PeResolveFunction(MCorePeFile_t *Library, const char *Function);

/* PeLoadImage
 * Loads the given file-buffer as a pe image into the current address space 
 * at the given Base-Address, which is updated after load to reflect where
 * the next address is available for load */
__EXTERN MCorePeFile_t *PeLoadImage(MCorePeFile_t *Parent, MString_t *Name,
	uint8_t *Buffer, size_t Length, uintptr_t *BaseAddress, int UsingInitRD);

/* PeUnloadLibrary
 * Unload dynamically loaded library 
 * This only cleans up in the case there are no more references */
__EXTERN void PeUnloadLibrary(MCorePeFile_t *Parent, MCorePeFile_t *Library);

/* PeUnloadImage
 * Unload executables, all it's dependancies and free it's resources */
__EXTERN void PeUnloadImage(MCorePeFile_t *Executable);

#endif //!__MCORE_PELOADER__
