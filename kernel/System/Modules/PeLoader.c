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
* MollenOS MCore - PE Format Loader
*/

/* Includes */
#include <Modules\PeLoader.h>
#include <Log.h>
#include <stddef.h>

/* Keep track of where to load 
 * the next module */
Addr_t GlbModuleLoadAddr = MEMORY_LOCATION_MODULES;

/* Load Module into memory */
MCorePeFile_t *PeLoadModule(uint8_t *Buffer)
{
	/* Headers */
	PeHeader_t *BaseHeader = NULL;
	PeOptionalHeader_t *OptHeader = NULL;

	/* Depends on Arch */
	PeOptionalHeader32_t *OptHeader32 = NULL;
	PeOptionalHeader64_t *OptHeader64 = NULL;

	/* Vars */
	MCorePeFile_t *PeInfo = NULL;


	/* Let's see */
	BaseHeader = (PeHeader_t*)Buffer;

	/* Validate */
	if (BaseHeader->Magic != PE_MAGIC)
	{
		LogFatal("PELD", "Invalid PE File Magic 0x%x", BaseHeader->Magic);
		return NULL;
	}

	/* Validate Machine */
	if (BaseHeader->Machine != PE_MACHINE_X32
		&& BaseHeader->Machine != PE_MACHINE_X64)
	{
		LogFatal("PELD", "Unsupported Machine 0x%x", BaseHeader->Machine);
		return NULL;
	}

	/* Load Optional Header */
	OptHeader = (PeOptionalHeader_t*)(Buffer + sizeof(PeHeader_t));

	/* We need to re-cast based on architecture */
	if (OptHeader->Architecture == PE_ARCHITECTURE_32)
	{
		/* This is an 32 bit */
		OptHeader32 = (PeOptionalHeader32_t*)(Buffer + sizeof(PeHeader_t));


	}
	else if (OptHeader->Architecture == PE_ARCHITECTURE_64)
	{
		/* 64 Bit! */
		OptHeader64 = (PeOptionalHeader64_t*)(Buffer + sizeof(PeHeader_t));


	}
	else
	{
		LogFatal("PELD", "Unsupported Architecture in Optional 0x%x", OptHeader->Architecture);
		return NULL;
	}



	/* Done */
	return PeInfo;
}