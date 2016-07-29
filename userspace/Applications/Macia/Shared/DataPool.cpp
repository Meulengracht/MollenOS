/* The Macia Language (MACIA)
*
* Copyright 2016, Philip Meulengracht
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
* Macia - IL Data Pool
* - This keeps track of all data for a program
* - Allows us to serialize it into bytecode easier
*/

/* Includes */
#include "DataPool.h"

/* Constructor 
 * Initialize the data pool etc */
DataPool::DataPool() {

	/* Initialize */
	m_iIdGen = 0;

	/* Clear out to be sure */
	m_sTable.clear();
}

/* Destructor 
 * - Handles cleanup */
DataPool::~DataPool() {

}

/* Checks for dublicates path */
int DataPool::CheckDublicate(char *pIdentifier, char *pPath) {
	
	/* Iterate our code objects */
	for (std::map<int, CodeObject*>::iterator Itr = m_sTable.begin();
		Itr != m_sTable.end(); ++Itr)
	{
		/* Get dataobject */
		CodeObject *Obj = Itr->second;

		/* Compare path */
		if (!strcmpi(Obj->GetPath(), pPath)) {

			/* So, it exists */
			printf("Dublicate objects with name %s\n", pIdentifier);

			/* Err, bail out */
			return -1;
		}
	}

	/* Yay! */
	return 0;
}

/* Calculates and creates a path for the given
 * identifier, so it lets us easily check for dubs */
char *DataPool::CreatePath(int ScopeId, char *Identifier) {

	/* Static storage */
	char Buffer[256];

	/* Iterate our code objects */
	for (std::map<int, CodeObject*>::iterator Itr = m_sTable.begin();
		Itr != m_sTable.end(); ++Itr)
	{
		/* Get dataobject */
		CodeObject *Obj = Itr->second;
		int Id = Itr->first;
		
		/* Have we found the mothership? */
		if (Id == ScopeId) {

			/* Yes, calculate a new path 
			 * Clear out temp storage */
			memset(&Buffer[0], 0, sizeof(Buffer));

			/* Now combine efforts here */
			sprintf(&Buffer[0], "%s.%s", Obj->GetPath(), Identifier);

			/* Done, why thank you very much */
			return strdup(&Buffer[0]);
		}
	}

	/* Uh, in the rare case there is none */
	return strdup(Identifier);
}

/* Create a new object and return
 * the id for the current scope */
int DataPool::CreateObject(char *pIdentifier) {

	/* Variables */
	CodeObject *dObj = NULL;
	int Id = 0;

	/* Should calculate path here but we don't 
	 * need to support nested objects for now */

	/* Step 1. Does it exist? */
	if (CheckDublicate(pIdentifier, pIdentifier))
		return -1;

	/* Allocate id */
	Id = m_iIdGen++;

	/* Create a new object */
	dObj = new CodeObject(CTObject, pIdentifier, pIdentifier, -1);

	/* Insert */
	m_sTable[Id] = dObj;

	/* Done! */
	return Id;
}

/* Create a new function for the given scope
 * and return the id for the current scope */
int DataPool::CreateFunction(char *pIdentifier, int ScopeId) {

	/* Variables */
	CodeObject *dObj = NULL;
	char *Path = NULL;
	int Id = 0;

	/* Calculate path first based on scope */
	Path = CreatePath(ScopeId, pIdentifier);

	/* Step 1. Does it exist? */
	if (CheckDublicate(pIdentifier, Path))
		return -1;

	/* Allocate id */
	Id = m_iIdGen++;

	/* Create a new object */
	dObj = new CodeObject(CTFunction, pIdentifier, Path, ScopeId);

	/* Insert */
	m_sTable[Id] = dObj;

	/* Done! */
	return Id;
}

/* Create a new variable for the given scope
 * and return the id of the variable */
int DataPool::DefineVariable(char *pIdentifier, int ScopeId) {

	/* Variables */
	CodeObject *dObj = NULL;
	char *Path = NULL;
	int Id = 0;

	/* Calculate path first based on scope */
	Path = CreatePath(ScopeId, pIdentifier);

	/* Step 1. Does it exist? */
	if (CheckDublicate(pIdentifier, Path))
		return -1;

	/* Allocate id */
	Id = m_iIdGen++;

	/* Create a new object */
	dObj = new CodeObject(CTVariable, pIdentifier, Path, ScopeId);

	/* Insert */
	m_sTable[Id] = dObj;

	/* Done! */
	return Id;
}

/* Retrieve a variable Id from the given 
 * scope and identifier */
int DataPool::LookupSymbol(char *pIdentifier, int ScopeId) {

	/* Iterate our code objects */
	for (std::map<int, CodeObject*>::iterator Itr = m_sTable.begin();
		Itr != m_sTable.end(); ++Itr)
	{
		/* Get dataobject */
		CodeObject *Obj = Itr->second;

		/* Compare path */
		if (!strcmpi(Obj->GetIdentifier(), pIdentifier)) {
			
			/* Yay! Found! */
			return Itr->first;
		}
	}

	/* Err - Not found - Bail */
	return -1;
}