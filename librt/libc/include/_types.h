/**
 * Copyright 2023, Philip Meulengracht
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
 */

#if !defined(__ITYPES_H) || defined(__need_tm) || \
    defined(__need_mbstate) || defined(__need_imaxdiv)

#if !defined(__need_tm) && !defined(__need_mbstate) && \
    !defined(__need_imaxdiv)
#define __ITYPES_H
#define __need_tm
#define __need_mbstate
#define __need_imaxdiv
#endif //!defined(...)

#if defined(__need_tm)
#if !defined(_TM_DEFINED)
#define _TM_DEFINED
struct tm {
    int tm_sec;     //Seconds
    int tm_min;     //Minutes
    int tm_hour;    //Hours
    int tm_mday;    //Day of the month
    int tm_mon;     //Months
    int tm_year;    //Years
    int tm_wday;    //Days since sunday
    int tm_yday;    //Days since January 1'st
    int tm_isdst;   //Is daylight saving?
    long tm_gmtoff; //Offset from UTC in seconds
    char *tm_zone;
};
#endif
#undef __need_tm
#endif //defined(__need_tm)

#if defined(__need_mbstate)
#if !defined(_MBSTATE_DEFINED)
#define _MBSTATE_DEFINED
#define __need_wint_t
#include <stddef.h>
typedef struct
{
	int __count;
	union
	{
		wint_t __wch;
		unsigned char __wchb[4];
	} __val;		/* Value so far.  */
} _mbstate_t;
typedef _mbstate_t mbstate_t;
#endif
#undef __need_mbstate
#endif //defined(__need_mbstate)

#if defined(__need_imaxdiv)
#if !defined(_IMAXDIV_DEFINED)
#define _IMAXDIV_DEFINED
#include <stdint.h>
typedef struct _imaxdiv {
  intmax_t quot;
  intmax_t rem;
} imaxdiv_t;
#endif
#undef __need_imaxdiv
#endif //defined(__need_imaxdiv)

#endif //!__ITYPES_H && !defined(...)
