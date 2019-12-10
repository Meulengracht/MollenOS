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
 * MollenOS C11-Support Time Implementation
 * - Definitions, prototypes and information needed.
 */

#ifndef __STDC_TIME__
#define __STDC_TIME__

#include <os/osdefs.h>
#include <locale.h>

#ifndef _CLOCK_T_DEFINED
#define _CLOCK_T_DEFINED
typedef __SIZE_TYPE__ clock_t;
#endif //!_CLOCK_T_DEFINED

#ifndef _TIME32_T_DEFINED
#define _TIME32_T_DEFINED
  typedef long __time32_t;
#endif
#ifndef _TIME64_T_DEFINED
#define _TIME64_T_DEFINED
#if _INTEGRAL_MAX_BITS >= 64
  typedef long long __time64_t;
#endif
#endif
#ifndef _TIME_T_DEFINED
#define _TIME_T_DEFINED
#ifdef _USE_32BIT_TIME_T
  typedef __time32_t time_t;
#else
  typedef __time64_t time_t;
#endif
#endif

#define CLOCKS_PER_SEC      1000
#define TIME_UTC            0 // The epoch for this clock is 1970-01-01 00:00:00 in Coordinated Universal Time (UTC)
#define TIME_TAI            1 // The epoch for this clock is 1970-01-01 00:00:00 in International Atomic Time (TAI)
#define TIME_MONOTONIC      2 // The epoch is when the computer was booted.
#define TIME_PROCESS        3 // The epoch for this clock is at some time during the generation of the current process.
#define TIME_THREAD         4 // The epic is like TIME_PROCESS, but locally for the calling thread.

#ifndef _TM_DEFINED
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
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

/* tzrule_struct
 * Define the timezone rule structure, this is related to the current timezone */
typedef struct __tzrule_struct {
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

/* tzinfo_type
 * The timezone information structure, used by
 * locale and current timezone settings given by getenv */
typedef struct __tzinfo_struct {
    int __tznorth;
    int __tzyear;
    __tzrule_type __tzrule[2];
} __tzinfo_type;

_CODE_BEGIN
/* time
 * Returns the current calendar time encoded as a time_t object, and 
 * also stores it in the time_t object pointed to by arg (unless arg is a null pointer). */
_CRTIMP time_t
time(
    _Out_Opt_ time_t *arg);

/* timespec_get
 * 1. Modifies the timespec object pointed to by ts to hold the current calendar 
 *    time in the time base base.
 * 2. Expands to a value suitable for use as the base argument of timespec_get
 * Other macro constants beginning with TIME_ may be provided by the implementation 
 * to indicate additional time bases. If base is TIME_UTC, then
 * 1. ts->tv_sec is set to the number of seconds since an implementation defined epoch, 
 *    truncated to a whole value
 * 2. ts->tv_nsec member is set to the integral number of nanoseconds, rounded to the 
 *    resolution of the system clock*/
_CRTIMP
int
timespec_get(
    _In_ struct timespec* ts,
    _In_ int              base);

/* timespec_diff
 * The difference between two timespec with the same base. Result
 * is stored in static storage provided by user. */
_CRTIMP
void
timespec_diff(
    _In_ const struct timespec* start,
    _In_ const struct timespec* stop,
    _In_ struct timespec*       result);

/* mktime
 * Renormalizes local calendar time expressed as a struct tm object and also 
 * converts it to time since epoch as a time_t object. time->tm_wday and 
 * time->tm_yday are ignored. The values in time are not checked for being out of range. */
_CRTIMP
time_t
mktime(
    _In_ struct tm *time);

/* difftime
 * Computes difference between two calendar times as time_t objects 
 * (time_end - time_beg) in seconds. If time_end refers to time point 
 * before time_beg then the result is negative. */
_CRTIMP
double
difftime(
    _In_ time_t time_end,
    _In_ time_t time_beg);

/* clock
 * Returns the approximate processor time used by the process since the beginning of 
 * an implementation-defined era related to the program's execution. 
 * To convert result value to seconds, divide it by CLOCKS_PER_SEC. */
_CRTIMP
clock_t
clock(void);

/* Timezone functions */
_CRTIMP
__tzinfo_type*
__gettzinfo(void);

/* gmtime
 * Converts given time since epoch (a time_t value pointed to by time) into calendar time,
 * expressed in Coordinated Universal Time (UTC) in the struct tm format. 
 * The result is stored in static storage and a pointer to that static storage is returned. */
_CRTIMP
struct tm*
gmtime(
    _In_ __CONST time_t *time);

/* gmtime_s
 * Same as gmtime, except that the function uses user-provided storage result for the 
 * result and that the following errors are detected at runtime and call the currently 
 * installed constraint handler function. */
#ifdef __STDC_LIB_EXT1__
_CRTIMP
struct tm*
gmtime_s(
    _In_ __CONST time_t *restrict time,
    _In_ struct tm *restrict result);
#endif

/* localtime
 * Uses the value pointed by timer to fill a tm structure with the 
 * values that represent the corresponding time, expressed for the local timezone. */
CRTDECL(struct tm*, localtime(const time_t*));
CRTDECL(int,        localtime_s(struct tm *__restrict, const time_t *__restrict));

/* asctime
 * Converts given calendar time tm to a textual representation of the 
 * following fixed 25-character form: Www Mmm dd hh:mm:ss yyyy\n */
_CRTIMP
char*
asctime(
    _In_ __CONST struct tm* time_ptr);

/* asctime_s
 * Same as asctime, except that the message is copied into user-provided storage buf, 
 * which is guaranteed to be null-terminated, and the following errors are 
 * detected at runtime and call the currently installed constraint handler function */
#ifdef __STDC_LIB_EXT1__
_CRTIMP
errno_t
asctime_s(
    _In_ char *buf,
    _In_ rsize_t bufsz,
    _In_ __CONST struct tm *time_ptr);
#endif

/* ctime
 * Interprets the value pointed by timer as a calendar time and 
 * converts it to a C-string containing a human-readable version 
 * of the corresponding time and date, in terms of local time. */
_CRTIMP
char*
ctime(
    _In_ __CONST time_t* time);

/* ctime_s
 * Same as ctime, except that the function is equivalent to 
 * asctime_s(buffer, bufsz, localtime_s(time, &(struct tm){0})), and the following 
 * errors are detected at runtime and call the currently installed constraint handler function: */
#ifdef __STDC_LIB_EXT1__
_CRTIMP
errno_t
ctime_s(
    _In_ char *buffer,
    _In_ rsize_t bufsz,
    _In_ __CONST time_t *time);
#endif

/* strftime
 * Copies into ptr the content of format, expanding its format 
 * specifiers into the corresponding values that represent the 
 * time described in timeptr, with a limit of maxsize characters. */
CRTDECL(size_t, strftime(
    _In_ char *__restrict dest,
    _In_ size_t maxsize, 
    _In_ __CONST char *__restrict format,
    _In_ __CONST struct tm *__restrict tmptr));
CRTDECL(size_t, strftime_l(
    _In_ char *__restrict s,
    _In_ size_t maxsize,
    _In_ __CONST char *__restrict format,
	_In_ __CONST struct tm *__restrict tim_p,
    _In_ struct __locale_t *locale));

/* wcsftime
 * Converts the date and time information from a given calendar time time 
 * to a null-terminated wide character string str according to format string format. 
 * Up to count bytes are written. */
#ifndef _WCSFTIME_DEFINED
#define _WCSFTIME_DEFINED
CRTDECL(size_t, wcsftime(
    _In_ wchar_t*__restrict str,
    _In_ size_t maxsize,
    _In_ __CONST wchar_t*__restrict format, 
    _In_ __CONST struct tm*__restrict time));
CRTDECL(size_t, wcsftime_l(
    _In_ wchar_t*__restrict str,
    _In_ size_t maxsize,
    _In_ __CONST wchar_t*__restrict format, 
    _In_ __CONST struct tm*__restrict time,
    _In_ struct __locale_t *locale));
#endif
_CODE_END

#endif //!__STDC_TIME__
