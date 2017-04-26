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
* MollenOS C Library - Standard Time
*/

#ifndef __TIME_INC__
#define __TIME_INC__

/* Includes */
#include <os/osdefs.h>
#include <sys/types.h>

#define __need_size_t
#include <stddef.h>

/* C Guard */
#ifdef __cplusplus
extern "C" {
#endif

/* According to the C-standard we must have
 * NULL, size_t and CLOCKS_PER_SEC defined 
 * in this header, we get two first from stddef */
#define CLOCKS_PER_SEC	100

/* clock_t must be defined in this header, and should
 * be as wide as possible in the given platform */
#if defined(_X86_32)
typedef unsigned long clock_t;
#elif defined(_X86_64)
typedef unsigned long long clock_t;
#else
typedef size_t clock_t;
#endif

/* Define the time structures, this one is the base
 * structure that describes a point in time */
struct tm
{
	int tm_sec;		//Seconds
	int tm_min;		//Minutes
	int tm_hour;	//Hours
	int tm_mday;	//Day of the month
	int tm_mon;		//Months
	int tm_year;	//Years
	int tm_wday;	//Days since sunday
	int tm_yday;	//Days since January 1'st
	int tm_isdst;	//Is daylight saving?
	long tm_gmtoff; //Offset from UTC in seconds
	char *tm_zone;
};

/* Define the timezone rule structure, this is 
 * related to the current timezone */
typedef struct __tzrule_struct
{
	char ch;
	char p[3];
	int m;
	int n;
	int d;
	int s;
	int p1;
	time_t change;
	long offset; /* Match type of _timezone. */
	int p2;
} __tzrule_type;

/* The timezone information structure, used by
 * locale and current timezone settings given by
 * getenv */
typedef struct __tzinfo_struct
{
	int __tznorth;
	int __tzyear;
	__tzrule_type __tzrule[2];
} __tzinfo_type;

/* Get the current calendar time as a 
 * value of type time_t. */
_CRTIMP time_t time(time_t*);

/* Returns the value of type time_t that represents the local time 
 * described by the tm structure pointed by timeptr (which may be modified). */
_CRTIMP time_t mktime(struct tm*);

/* Calculates the difference in seconds 
 * between start and end. */
_CRTIMP double difftime(time_t end, time_t start);

/* Returns the processor time consumed by the program. 
 * To calculate the number of seconds the program has
 * been running divide by CLOCKS_PER_SEC */
_CRTIMP clock_t clock(void);

/* Timezone functions */
_CRTIMP __tzinfo_type *__gettzinfo(void);

/* Uses the value pointed by timer to fill a tm structure 
 * with the values that represent the corresponding time, 
 * expressed as a UTC time (i.e., the time at the GMT timezone). */
_CRTIMP struct tm *gmtime(__CONST time_t*);

/* Uses the value pointed by timer to fill a tm structure with the 
 * values that represent the corresponding time, expressed for the local timezone. */
_CRTIMP struct tm *localtime(__CONST time_t*);

/* Formats a given timebuffer to a 
 * string of format Www Mmm dd hh:mm:ss yyyy */
_CRTIMP char* asctime(__CONST struct tm*);

/* Interprets the value pointed by timer as a calendar time and 
 * converts it to a C-string containing a human-readable version 
 * of the corresponding time and date, in terms of local time. */
_CRTIMP char *ctime(__CONST time_t*);

/* Copies into ptr the content of format, expanding its format 
 * specifiers into the corresponding values that represent the 
 * time described in timeptr, with a limit of maxsize characters. */
_CRTIMP size_t strftime(char *__restrict dest, size_t maxsize, 
	__CONST char *__restrict format,
	__CONST struct tm *__restrict tmptr);

#ifdef __cplusplus
}
#endif

#endif