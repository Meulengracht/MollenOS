/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS MCore - Virtual FileSystem
* - Path Functions
*/

/* Includes 
 * - System */
#include <Vfs/Vfs.h>
#include <Process.h>

/* Includes
 * - C-Library */
#include <ds/list.h>
#include <string.h>
#include <ctype.h>

/* Externs 
 * We import from VFS */
extern List_t *GlbFileSystems;

/* Environment String Array */
const char *GlbEnvironmentalPaths[PathEnvironmentCount] = {
	"./",

	"./",
	":/Shared/AppData/",

	":/",
	":/System/",

	":/Shared/Bin/",
	":/Shared/Documents/",
	":/Shared/Includes/",
	":/Shared/Libraries/",
	":/Shared/Media/",

	":/Users/"
};


/* Vfs - Resolve Environmental Path
 * @Base - Environmental Path */
MString_t *VfsResolveEnvironmentPath(VfsEnvironmentPath_t Base)
{
	/* Handle Special Case - 0 & 1
	* Just return the current working directory */
	if (Base == PathCurrentWorkingDir
		|| Base == PathApplicationBase)
	{
		/* Get current process */
		Cpu_t CurrentCpu = ApicGetCpu();
		MCoreThread_t *cThread = ThreadingGetCurrentThread(CurrentCpu);

		if (Base == PathCurrentWorkingDir)
			return PmGetWorkingDirectory(cThread->ProcessId);
		else
			return PmGetBaseDirectory(cThread->ProcessId);
	}

	/* Otherwise we have to lookup in a string table */
	MString_t *ResolvedPath = MStringCreate(NULL, StrUTF8);
	ListNode_t *fNode = NULL;
	int pIndex = (int)Base;
	int pFound = 0;

	/* Get system path */
	_foreach(fNode, GlbFileSystems)
	{
		/* Cast */
		MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)fNode->Data;

		/* Boot drive? */
		if (Fs->Flags & VFS_MAIN_DRIVE) {
			MStringAppendString(ResolvedPath, Fs->Identifier);
			pFound = 1;
			break;
		}
	}

	/* Sanity */
	if (!pFound) {
		MStringDestroy(ResolvedPath);
		return NULL;
	}

	/* Now append the special paths */
	MStringAppendCharacters(ResolvedPath, GlbEnvironmentalPaths[pIndex], StrUTF8);

	/* Done! */
	return ResolvedPath;
}

/* Vfs - Canonicalize Path
 * @Path - UTF-8 String */
MString_t *VfsCanonicalizePath(VfsEnvironmentPath_t Base, const char *Path)
{
	/* Store result */
	MString_t *AbsPath = MStringCreate(NULL, StrUTF8);
	ListNode_t *fNode = NULL;
	uint32_t Itr = 0;

	/* Start by copying cwd over
	* if Path is not absolute or specifier */
	if (strchr(Path, ':') == NULL
		&& strchr(Path, '%') == NULL)
	{
		/* Get base directory */
		MString_t *BasePath = VfsResolveEnvironmentPath(Base);

		/* Unless Base is null, then we have a problem */
		if (BasePath == NULL)
		{
			/* Fuck */
			MStringDestroy(AbsPath);
			return NULL;
		}
		else {
			/* Start in working directory */
			MStringCopy(AbsPath, BasePath, -1);

			/* Make sure the path ends on a '/' */
			if (MStringGetCharAt(AbsPath, MStringLength(AbsPath) - 1) != '/')
				MStringAppendCharacter(AbsPath, '/');
		}
	}

	/* Now, we have to resolve the path in Path */
	while (Path[Itr])
	{
		/* Sanity */
		if (Path[Itr] == '/'
			&& Itr == 0)
		{
			Itr++;
			continue;
		}

		/* Identifier/Keyword? */
		if (Path[Itr] == '%')
		{
			/* Which kind of keyword? */
			if ((Path[Itr + 1] == 'S' || Path[Itr + 1] == 's')
				&& Path[Itr + 2] == 'y'
				&& Path[Itr + 3] == 's'
				&& Path[Itr + 4] == '%')
			{
				/* Get boot drive identifer */
				_foreach(fNode, GlbFileSystems)
				{
					/* Cast */
					MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)fNode->Data;

					/* Boot drive? */
					if (Fs->Flags & VFS_MAIN_DRIVE) {
						MStringAppendString(AbsPath, Fs->Identifier);
						break;
					}
				}

				/* Now append the system path */
				MStringAppendCharacters(AbsPath, 
					GlbEnvironmentalPaths[PathSystemDirectory], StrUTF8);

				/* Skip */
				Itr += 5;

				/* Is the next char a '/'? If so skip */
				if (Path[Itr] == '/' || Path[Itr] == '\\') {
					Itr++;
				}

				/* Done! */
				continue;
			}
		}

		/* What char is it ? */
		if (Path[Itr] == '.'
			&& (Path[Itr + 1] == '/' || Path[Itr + 1] == '\\'))
		{
			/* Ignore */
			Itr += 2;
			continue;
		}
		else if (Path[Itr] == '.'
			&& Path[Itr + 1] == '.')
		{
			/* Go one directory back, if we are in one */
			int Index = MStringFindReverse(AbsPath, '/');
			if (MStringGetCharAt(AbsPath, Index - 1) != ':')
			{
				/* Build a new string */
				MString_t *NewAbs = MStringSubString(AbsPath, 0, Index);
				MStringDestroy(AbsPath);
				AbsPath = NewAbs;
			}
		}
		else
		{
			/* Copy over */
			if (Path[Itr] == '\\')
				MStringAppendCharacter(AbsPath, '/');
			else
				MStringAppendCharacter(AbsPath, Path[Itr]);
		}

		/* Increase */
		Itr++;
	}

	/* Replace dublicate // with / */
	//MStringReplace(AbsPath, "//", "/");

	/* Done! */
	return AbsPath;
}
