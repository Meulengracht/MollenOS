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
* MollenOS CRT Library - VC++ Crt Init
*/

/* Includes */
#include <stddef.h>

/* Typedefs */
typedef void(*_PVFV)(void);
typedef int(*_PIFV)(void);
typedef void(*_PVFI)(int);

/* Allocate sections */
#pragma data_seg(".CRT$XIA")
__declspec(allocate(".CRT$XIA")) _PIFV __xi_a[] = { 0 };
#pragma data_seg(".CRT$XIZ")
__declspec(allocate(".CRT$XIZ")) _PIFV __xi_z[] = { 0 };
#pragma data_seg(".CRT$XCA")
__declspec(allocate(".CRT$XCA")) _PVFV __xc_a[] = { 0 };
#pragma data_seg(".CRT$XCZ")
__declspec(allocate(".CRT$XCZ")) _PVFV __xc_z[] = { 0 };
#pragma data_seg(".CRT$XPA")
__declspec(allocate(".CRT$XPA")) _PVFV __xp_a[] = { 0 };
#pragma data_seg(".CRT$XPZ")
__declspec(allocate(".CRT$XPZ")) _PVFV __xp_z[] = { 0 };
#pragma data_seg(".CRT$XTA")
__declspec(allocate(".CRT$XTA")) _PVFV __xt_a[] = { 0 };
#pragma data_seg(".CRT$XTZ")
__declspec(allocate(".CRT$XTZ")) _PVFV __xt_z[] = { 0 };
#pragma data_seg()
#pragma comment(linker, "/merge:.CRT=.data")

/* Onexit stuff */
static _PVFV onexitarray[32];
static _PVFV *onexitbegin, *onexitend;

/***
* static void _initterm(_PVFV * pfbegin, _PVFV * pfend) - call entries in
*       function pointer table
*
*Purpose:
*       Walk a table of function pointers, calling each entry, as follows:
*
*           1. walk from beginning to end, pfunctbl is assumed to point
*              to the beginning of the table, which is currently a null entry,
*              as is the end entry.
*           2. skip NULL entries
*           3. stop walking when the end of the table is encountered
*
*Entry:
*       _PVFV *pfbegin  - pointer to the beginning of the table (first
*                         valid entry).
*       _PVFV *pfend    - pointer to the end of the table (after last
*                         valid entry).
*
*Exit:
*       No return value
*
*Notes:
*       This routine must be exported in the CRT DLL model so that the client
*       EXE and client DLL(s) can call it to initialize their C++ constructors.
*
*Exceptions:
*       If either pfbegin or pfend is NULL, or invalid, all bets are off!
*
*******************************************************************************/

#ifdef CRTDLL
void __cdecl _initterm(
#else  /* CRTDLL */
static void __cdecl _initterm(
#endif  /* CRTDLL */
	_PVFV * pfbegin,
	_PVFV * pfend
	)
{
	/*
	* walk the table of function pointers from the bottom up, until
	* the end is encountered.  Do not skip the first entry.  The initial
	* value of pfbegin points to the first valid entry.  Do not try to
	* execute what pfend points to.  Only entries before pfend are valid.
	*/
	while (pfbegin < pfend)
	{
		/*
		* if current table entry is non-NULL, call thru it.
		*/
		if (*pfbegin != NULL)
			(**pfbegin)();
		++pfbegin;
	}
}

/***
* static int  _initterm_e(_PIFV * pfbegin, _PIFV * pfend) - call entries in
*       function pointer table, return error code on any failure
*
*Purpose:
*       Walk a table of function pointers in the same way as _initterm, but
*       here the functions return an error code.  If an error is returned, it
*       will be a nonzero value equal to one of the _RT_* codes.
*
*Entry:
*       _PIFV *pfbegin  - pointer to the beginning of the table (first
*                         valid entry).
*       _PIFV *pfend    - pointer to the end of the table (after last
*                         valid entry).
*
*Exit:
*       No return value
*
*Notes:
*       This routine must be exported in the CRT DLL model so that the client
*       EXE and client DLL(s) can call it.
*
*Exceptions:
*       If either pfbegin or pfend is NULL, or invalid, all bets are off!
*
*******************************************************************************/

int __cdecl _initterm_e(
	_PIFV * pfbegin,
	_PIFV * pfend
	)
{
	int ret = 0;

	/*
	* walk the table of function pointers from the bottom up, until
	* the end is encountered.  Do not skip the first entry.  The initial
	* value of pfbegin points to the first valid entry.  Do not try to
	* execute what pfend points to.  Only entries before pfend are valid.
	*/
	while (pfbegin < pfend  && ret == 0)
	{
		/*
		* if current table entry is non-NULL, call thru it.
		*/
		if (*pfbegin != NULL)
			ret = (**pfbegin)();
		++pfbegin;
	}

	return ret;
}

#ifdef LIBC_KERNEL
int __cdecl _purecall(void)
{
	// print error message
	return 0;
}
#else
#include <os/MollenOS.h>
int __cdecl _purecall(void)
{
	MollenOSSystemLog("PURECALL HAS BEEN MADE");
	return 0;
}
#endif

int __cdecl atexit(_PVFV fn)
{
	if (32 * 4 < ((int)onexitend - (int)onexitbegin) + 4)
		return 1;
	else
		*(onexitend++) = fn;
	return 0;
}

/* Cpp Init */
EXTERN void __CppInit(void)
{
	/* Initialize non-ret functions */
	_initterm(__xc_a, __xc_z);

	/* Initialize ret functions */
	_initterm_e(__xi_a, __xi_z);
}

EXTERN void __CppFinit(void)
{
	if (onexitbegin)
	{
		while (--onexitend >= onexitbegin)
			if (*onexitend != 0)
				(**onexitend)();
	}

	_initterm(__xp_a, __xp_z);
	_initterm(__xt_a, __xt_z);
}

EXTERN int onexitinit(void)
{
	onexitend = onexitbegin = onexitarray;
	*onexitbegin = 0;
	return 0;
}

// run onexitinit automatically
#pragma data_seg(".CRT$XIB")      
__declspec(allocate(".CRT$XIB")) static _PIFV pinit = onexitinit;
#pragma data_seg()