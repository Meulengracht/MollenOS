/* local header used by libc/time routines */
#include <time.h>

#define SECSPERMIN   60L
#define MINSPERHOUR  60L
#define HOURSPERDAY  24L
#define SECSPERHOUR  (SECSPERMIN * MINSPERHOUR)
#define SECSPERDAY   (SECSPERHOUR * HOURSPERDAY)
#define DAYSPERWEEK  7
#define MONSPERYEAR  12

#define YEAR_BASE                      1900
#define EPOCH_YEAR                     1970
#define EPOCH_WDAY                     4
#define EPOCH_YEARS_SINCE_LEAP         2
#define EPOCH_YEARS_SINCE_CENTURY      70
#define EPOCH_YEARS_SINCE_LEAP_CENTURY 370

#define isleap(Year) ((((Year) % 4) == 0 && ((Year) % 100) != 0) || ((Year) % 400) == 0)
#define DAYSPERYEAR(Year) (isleap(Year) ? 366 : 365)

int __tzcalc_limits(int __year);

// Shared tabels that can be accessed for information and help for conversions
extern const int __month_lengths[2][MONSPERYEAR];
extern const int __days_before_month[2][MONSPERYEAR];
extern int       __daylight;
extern long      __timezone;
extern char*     __tzname[2];

void _tzset_unlocked_r(void);
void _tzset_unlocked(void);
void __tz_lock(void);
void __tz_unlock(void);

/* locks for multi-threading */
#ifdef __SINGLE_THREAD__
#define TZ_LOCK
#define TZ_UNLOCK
#else
#define TZ_LOCK __tz_lock()
#define TZ_UNLOCK __tz_unlock()
#endif

/* Declare reentrency versions */
extern struct tm *gmtime_r(const time_t *__restrict tim_p, struct tm *__restrict res);
