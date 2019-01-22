/*
 * mktime.c
 * Original Author:    G. Haley
 *
 * Converts the broken-down time, expressed as local time, in the structure
 * pointed to by TimePointer into a calendar time value. The original values of the
 * tm_wday and tm_yday fields of the structure are ignored, and the original
 * values of the other fields have no restrictions. On successful completion
 * the fields of the structure are set to represent the specified calendar
 * time. Returns the specified calendar time. If the calendar time can not be
 * represented, returns the value (time_t) -1.
 *
 * Modifications:    Fixed tm_isdst usage - 27 August 2008 Craig Howland.
 */

#include <stdlib.h>
#include <time.h>
#include "local.h"

static void 
validate_structure(
    _In_ struct tm* TimePointer)
{
    int   Leap = isleap(TimePointer->tm_year);
    div_t res;

    if (TimePointer->tm_sec < 0 || TimePointer->tm_sec > 59) {
        res = div (TimePointer->tm_sec, 60);
        TimePointer->tm_min += res.quot;
        if ((TimePointer->tm_sec = res.rem) < 0) {
            TimePointer->tm_sec += 60;
            --TimePointer->tm_min;
        }
    }

    if (TimePointer->tm_min < 0 || TimePointer->tm_min > 59) {
        res = div (TimePointer->tm_min, 60);
        TimePointer->tm_hour += res.quot;
        if ((TimePointer->tm_min = res.rem) < 0) {
            TimePointer->tm_min += 60;
            --TimePointer->tm_hour;
        }
    }

    if (TimePointer->tm_hour < 0 || TimePointer->tm_hour > 23) {
        res = div (TimePointer->tm_hour, 24);
        TimePointer->tm_mday += res.quot;
        if ((TimePointer->tm_hour = res.rem) < 0) {
            TimePointer->tm_hour += 24;
            --TimePointer->tm_mday;
        }
    }

    if (TimePointer->tm_mon < 0 || TimePointer->tm_mon > 11) {
        res = div (TimePointer->tm_mon, 12);
        TimePointer->tm_year += res.quot;
        if ((TimePointer->tm_mon = res.rem) < 0) {
            TimePointer->tm_mon += 12;
            --TimePointer->tm_year;
        }
    }

    if (TimePointer->tm_mday <= 0) {
        while (TimePointer->tm_mday <= 0) {
            if (--TimePointer->tm_mon == -1) {
                TimePointer->tm_year--;
                TimePointer->tm_mon = 11;
            }
            TimePointer->tm_mday += __month_lengths[Leap][TimePointer->tm_mon];
        }
    }
    else {
        while (TimePointer->tm_mday > __month_lengths[Leap][TimePointer->tm_mon]) {
            TimePointer->tm_mday -= __month_lengths[Leap][TimePointer->tm_mon];
            if (++TimePointer->tm_mon == 12) {
                TimePointer->tm_year++;
                TimePointer->tm_mon = 0;
            }
        }
    }
}

time_t
handle_daylight_savings(
    _In_ struct tm* TimePointer,
    _In_ int        Year,
    _In_ int        DayCount,
    _In_ time_t     LinearTime)
{
    __tzinfo_type* tz    = __gettzinfo();
    int            isdst = 0;

    TZ_LOCK;
    _tzset_unlocked();
    if (__daylight) {
        int tm_isdst;
        int y = TimePointer->tm_year + YEAR_BASE;
        /* Convert user positive into 1 */
        tm_isdst = TimePointer->tm_isdst > 0  ?  1 : TimePointer->tm_isdst;
        isdst = tm_isdst;

        if (y == tz->__tzyear || __tzcalc_limits (y)) {
        /* calculate start of dst in dst local time and 
            start of std in both std local time and dst local time */
            time_t startdst_dst = tz->__tzrule[0].change - (time_t) tz->__tzrule[1].offset;
        time_t startstd_dst = tz->__tzrule[1].change - (time_t) tz->__tzrule[1].offset;
        time_t startstd_std = tz->__tzrule[1].change - (time_t) tz->__tzrule[0].offset;
        /* if the time is in the overlap between dst and std local times */
        if (LinearTime >= startstd_std && LinearTime < startstd_dst)
        ; /* we let user decide or leave as -1 */
            else
        {
            isdst = (tz->__tznorth
                ? (LinearTime >= startdst_dst && LinearTime < startstd_std)
                : (LinearTime >= startdst_dst || LinearTime < startstd_std));
            /* if user committed and was wrong, perform correction, but not
            * if the user has given a negative value (which
            * asks mktime() to determine if DST is in effect or not) */
            if (tm_isdst >= 0  &&  (isdst ^ tm_isdst) == 1)
        {
            /* we either subtract or add the difference between
                time zone offsets, depending on which way the user got it
                wrong. The diff is typically one hour, or 3600 seconds,
                and should fit in a 16-bit int, even though offset
                is a long to accomodate 12 hours. */
            int diff = (int) (tz->__tzrule[0].offset
                    - tz->__tzrule[1].offset);
            if (!isdst)
            diff = -diff;
            TimePointer->tm_sec += diff;
            LinearTime += diff;  /* we also need to correct our current time calculation */
            int mday = TimePointer->tm_mday;
            validate_structure (TimePointer);
            mday = TimePointer->tm_mday - mday;
            /* roll over occurred */
            if (mday) {
            /* compensate for month roll overs */
            if (mday > 1)
                mday = -1;
            else if (mday < -1)
                mday = 1;
            /* update days for wday calculation */
            DayCount += mday;
            /* handle yday */
            if ((TimePointer->tm_yday += mday) < 0) {
                --Year;
                TimePointer->tm_yday = DAYSPERYEAR(Year) - 1;
            } else {
                mday = DAYSPERYEAR(Year);
                if (TimePointer->tm_yday > (mday - 1))
                TimePointer->tm_yday -= mday;
            }
            }
        }
        }
    }
    }

    // add appropriate offset to put time in gmt format
    if (isdst == 1) {
        LinearTime += (time_t) tz->__tzrule[1].offset;
    }
    else {
        // otherwise assume std time
        LinearTime += (time_t) tz->__tzrule[0].offset;
    }
    TZ_UNLOCK;

    // Update the daylight savings result
    TimePointer->tm_isdst = isdst;
    TimePointer->tm_wday  = (DayCount + 4) % 7;
    if (TimePointer->tm_wday < 0) {
        TimePointer->tm_wday += 7;
    }
    return LinearTime;
}

time_t 
mktime(
    _In_ struct tm *TimePointer)
{
    time_t Result = 0;
    long   Days   = 0;
    int    Year   = TimePointer->tm_year;
    int    Leap   = isleap(Year);

    validate_structure(TimePointer);
    if (TimePointer->tm_year > 10000 || TimePointer->tm_year < -10000) {
        return (time_t)-1;
    }

    // compute hours, minutes, seconds
    Result += TimePointer->tm_sec + (TimePointer->tm_min * SECSPERMIN) + (TimePointer->tm_hour * SECSPERHOUR);

    // compute days in year, get how many days untill now, and how many days has passed this year
    Days += __days_before_month[Leap][TimePointer->tm_mon];
    Days += TimePointer->tm_mday - 1;

    // Update the time structure to include this information
    TimePointer->tm_yday = Days;

    // compute days in other years
    if (Year > 70) {
        for (Year = 70; Year < TimePointer->tm_year; Year++) {
            Days += DAYSPERYEAR(Year);
        }
    }
    else if (Year < 70) {
        for (Year = 69; Year > TimePointer->tm_year; Year--) {
            Days -= DAYSPERYEAR(Year);
        }
        Days -= DAYSPERYEAR(Year);
    }
    return handle_daylight_savings(TimePointer, Year, Days, Result + (Days * SECSPERDAY));
}
