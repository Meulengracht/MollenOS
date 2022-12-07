/**
 * Copyright 2011, Philip Meulengracht
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
 * C11-Support Time Implementation
 * - Definitions, prototypes and information needed.
 */

#ifndef __STDC_TIME__
#define __STDC_TIME__

#include <os/osdefs.h>
#include <locale.h>

#ifndef _CLOCK_T_DEFINED
#define _CLOCK_T_DEFINED
typedef unsigned long long clock_t;
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

/**
 * The frequency of the clock varies based on the clock source. So the CLOCKS_PER_SEC
 * macro is actually a function call.
 */
CRTDECL(clock_t, clock_getfreq(void));
#define CLOCKS_PER_SEC clock_getfreq()

/**
 * On Vali UTC and TAI are almost alike. The difference is that UTC is affected by daylight savings, NTP etc.
 */
#define TIME_UTC       0 // The epoch for this clock is 2000-01-01 00:00:00 in Coordinated Universal Time (UTC)
#define TIME_TAI       1 // The epoch for this clock is 2000-01-01 00:00:00 in International Atomic Time (TAI)
#define TIME_MONOTONIC 2 // The epoch is when the computer was booted.
#define TIME_PROCESS   3 // The epoch for this clock is at some time during the generation of the current process.
#define TIME_THREAD    4 // The epic is like TIME_PROCESS, but locally for the calling thread.

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

typedef struct __tzinfo_struct {
    int __tznorth;
    int __tzyear;
    __tzrule_type __tzrule[2];
} __tzinfo_type;

_CODE_BEGIN
/**
 * @brief Returns the current calendar time encoded as a time_t object, and
 * also stores it in the time_t object pointed to by tim (unless tim is a null pointer).
 *
 * @param tim
 * @return
 */
CRTDECL(time_t,
time(
    _Out_Opt_ time_t* tim));

/**
 * @brief
 * 1. Modifies the timespec object pointed to by ts to hold the current calendar
 *    time in the time base base.
 * 2. Expands to a value suitable for use as the base argument of timespec_get
 * Other macro constants beginning with TIME_ may be provided by the implementation 
 * to indicate additional time bases. If base is TIME_UTC, then
 * 1. ts->tv_sec is set to the number of seconds since an implementation defined epoch,
 *    truncated to a whole value
 * 2. ts->tv_nsec member is set to the integral number of nanoseconds, rounded to the
 *    resolution of the system clock
 *
 * @param[In] ts   pointer to an object of type struct timespec
 * @param[In] base TIME_UTC or another nonzero integer value indicating the time base
 * @return The value of base if base is supported, zero otherwise
 */
CRTDECL(int,
timespec_get(
    _In_ struct timespec* ts,
    _In_ int              base));

/**
 * @brief If ts is non-null and base is supported by timespec_get, modifies *ts to hold the resolution
 * of time provided by timespec_get for base. For each supported base, multiple calls to timespec_getres
 * during the same program execution have identical results.
 *
 * @param[In] ts   pointer to an object of type struct timespec
 * @param[In] base TIME_UTC or another nonzero integer value indicating the time base
 * @return The value of base if base is supported, zero otherwise.
 */
CRTDECL(int, timespec_getres(struct timespec* ts, int base));

/**
 * @brief Subtracts two timespecs and stores the result into <result>
 * @param result The timespec where the result should be stored.
 * @param a The left operand.
 * @param b The right operand.
 */
CRTDECL(void, timespec_sub(const struct timespec* a, const struct timespec* b, struct timespec* result));
#define timespec_diff timespec_sub

/**
 * @brief Adds two timespecs and stores the result into <result>
 * @param result The timespec where the result should be stored.
 * @param a The left operand.
 * @param b The right operand.
 */
CRTDECL(void, timespec_add(const struct timespec* a, const struct timespec* b, struct timespec* result));

/**
 * @brief Renormalizes local calendar tm expressed as a struct tm object and also
 * converts it to tm since epoch as a time_t object. tm->tm_wday and
 * tm->tm_yday are ignored. The values in tm are not checked for being out of range.
 *
 * @param tm
 * @return
 */
CRTDECL(time_t,
mktime(
    _In_ struct tm *tm));

/**
 * @brief Computes difference between two calendar times as time_t objects
 * (time_end - time_beg) in seconds. If time_end refers to time point
 * before time_beg then the result is negative
 *
 * @param time_end
 * @param time_beg
 * @return
 */
CRTDECL(double,
difftime(
    _In_ time_t time_end,
    _In_ time_t time_beg));

/**
 * @brief Returns the approximate processor time used by the process since the beginning of
 * an implementation-defined era related to the program's execution.
 * To convert result value to seconds, divide it by CLOCKS_PER_SEC.
 *
 * @return
 */
CRTDECL(clock_t, clock(void));
CRTDECL(__tzinfo_type*, __gettzinfo(void));

/* gmtime
 * Converts given time since epoch (a time_t value pointed to by time) into calendar time,
 * expressed in Coordinated Universal Time (UTC) in the struct tm format. 
 * The result is stored in static storage and a pointer to that static storage is returned. */
CRTDECL(struct tm*,
gmtime(
    _In_ const time_t *time));

/* gmtime_s
 * Same as gmtime, except that the function uses user-provided storage result for the 
 * result and that the following errors are detected at runtime and call the currently 
 * installed constraint handler function. */
#ifdef __STDC_LIB_EXT1__
CRTDECL(struct tm*,
gmtime_s(
    _In_ const time_t *restrict time,
    _In_ struct tm *restrict result));
#endif

/* localtime
 * Uses the value pointed by timer to fill a tm structure with the 
 * values that represent the corresponding time, expressed for the local timezone. */
CRTDECL(struct tm*, localtime(const time_t*));
CRTDECL(int,        localtime_s(struct tm *__restrict, const time_t *__restrict));

/* asctime
 * Converts given calendar time tm to a textual representation of the 
 * following fixed 25-character form: Www Mmm dd hh:mm:ss yyyy\n */
CRTDECL(char*,
asctime(
    _In_ const struct tm* time_ptr));

/* asctime_s
 * Same as asctime, except that the message is copied into user-provided storage buf, 
 * which is guaranteed to be null-terminated, and the following errors are 
 * detected at runtime and call the currently installed constraint handler function */
#ifdef __STDC_LIB_EXT1__
CRTDECL(errno_t,
asctime_s(
    _In_ char *buf,
    _In_ rsize_t bufsz,
    _In_ const struct tm *time_ptr));
#endif

/* ctime
 * Interprets the value pointed by timer as a calendar time and 
 * converts it to a C-string containing a human-readable version 
 * of the corresponding time and date, in terms of local time. */
CRTDECL(char*,
ctime(
    _In_ const time_t* time));

/* ctime_s
 * Same as ctime, except that the function is equivalent to 
 * asctime_s(buffer, bufsz, localtime_s(time, &(struct tm){0})), and the following 
 * errors are detected at runtime and call the currently installed constraint handler function: */
#ifdef __STDC_LIB_EXT1__
CRTDECL(errno_t,
ctime_s(
    _In_ char *buffer,
    _In_ rsize_t bufsz,
    _In_ const time_t *time));
#endif

/* strftime
 * Copies into ptr the content of format, expanding its format 
 * specifiers into the corresponding values that represent the 
 * time described in timeptr, with a limit of maxsize characters. */
CRTDECL(size_t, strftime(
    _In_ char *__restrict dest,
    _In_ size_t maxsize, 
    _In_ const char *__restrict format,
    _In_ const struct tm *__restrict tmptr));
CRTDECL(size_t, strftime_l(
    _In_ char *__restrict s,
    _In_ size_t maxsize,
    _In_ const char *__restrict format,
	_In_ const struct tm *__restrict tim_p,
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
    _In_ const wchar_t*__restrict format, 
    _In_ const struct tm*__restrict time));
CRTDECL(size_t, wcsftime_l(
    _In_ wchar_t*__restrict str,
    _In_ size_t maxsize,
    _In_ const wchar_t*__restrict format, 
    _In_ const struct tm*__restrict time,
    _In_ struct __locale_t *locale));
#endif
_CODE_END

#endif //!__STDC_TIME__
