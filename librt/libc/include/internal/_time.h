#ifndef __INTERNAL_TIME_INC__
#define __INTERNAL_TIME_INC__

#include <time.h>

#define TIME_MAX                2147483647L

#define SECSPERMIN	60L
#define MINSPERHOUR	60L
#define HOURSPERDAY	24L
#define SECSPERHOUR	(SECSPERMIN * MINSPERHOUR)
#define SECSPERDAY	(SECSPERHOUR * HOURSPERDAY)
#define DAYSPERWEEK	7
#define MONSPERYEAR	12

#define YEAR_BASE	1900
#define EPOCH_YEAR      1970
#define EPOCH_WDAY      4
#define EPOCH_YEARS_SINCE_LEAP 2
#define EPOCH_YEARS_SINCE_CENTURY 70
#define EPOCH_YEARS_SINCE_LEAP_CENTURY 370

#define SECS_DAY                (24L * 60L * 60L)
#define isleap(y) ((((y) % 4) == 0 && ((y) % 100) != 0) || ((y) % 400) == 0)
#define YEARSIZE(year)          (isleap(year) ? 366 : 365)
#define FIRSTSUNDAY(timp)       (((timp)->tm_yday - (timp)->tm_wday + 420) % 7)
#define FIRSTDAYOF(timp)        (((timp)->tm_wday - (timp)->tm_yday + 420) % 7)

extern tm * _mktm(const time_t *, tm *, int __is_gmtime);
extern int  __tzcalc_limits(int __year);
extern tm *gmtime_r(const time_t *timer, tm *tmbuf);
extern long _timezone;
extern const int _ytab[2][12];

#pragma pack(push, 1)
typedef struct __tzrule_struct
{
	char ch;
	int m;
	int n;
	int d;
	int s;
	time_t change;
	long offset; /* Match type of _timezone. */
} __tzrule_type;

typedef struct __tzinfo_struct
{
	int __tznorth;
	int __tzyear;
	__tzrule_type __tzrule[2];
} __tzinfo_type;
#pragma pack(pop)

extern __tzinfo_type *__gettzinfo(void);

/* locks for multi-threading */
#define TZ_LOCK
#define TZ_UNLOCK

#endif