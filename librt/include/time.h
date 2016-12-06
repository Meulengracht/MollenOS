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
#include <crtdefs.h>
#include <sys/types.h>

/* C Guard */
#ifdef __cplusplus
extern "C" {
#endif

/*******************************
 *        Definitions          *
 *******************************/
#define CLOCKS_PER_SEC	100

/*******************************
 *       Time Structures       *
 *******************************/
typedef struct TimeStructure
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

} tm;

/* Types */
typedef unsigned long clock_t;

/*******************************
 *         Prototypes          *
 *******************************/

/* Basic time functions */
_CRTIMP time_t time(time_t *timer);
_CRTIMP time_t mktime(tm *timeptr);
_CRTIMP double difftime(time_t time1, time_t time2);
_CRTIMP clock_t clock(void);

/* Get Time Formats */
_CRTIMP tm *gmtime(const time_t *timer);		//GMT time
_CRTIMP tm *localtime(const time_t *timer);  //UTC

/* Time -> String functions */
_CRTIMP char* asctime(const tm *timeptr);
_CRTIMP char *ctime(const time_t *tim_p);

/* Not implemented */
_CRTIMP size_t strftime(char * ptr, size_t maxsize, const char * format,
                        const tm * timeptr);		//Format time as string, NA

#ifdef __cplusplus
}
#endif

#endif