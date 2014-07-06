/* Standard C Library Time Implementation
 * 
 */


#ifndef __TIME_INC__
#define __TIME_INC__

/* Includes */
#include <crtdefs.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

//Defines
#define CLOCKS_PER_SEC	100

//Structs
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

} tm;

/* Types */
typedef unsigned long clock_t;

/* Basic time functions */
extern time_t time(time_t *timer);
extern time_t mktime(tm *timeptr);
extern double difftime(time_t time1, time_t time2);
extern clock_t clock(void);

/* Get Time Formats */
extern tm *gmtime(const time_t *timer);		//GMT time
extern tm *localtime(const time_t *timer);  //UTC

/* Time -> String functions */
extern char* asctime(const tm *timeptr);
extern char *ctime(const time_t *tim_p);

/* Not implemented */
extern size_t strftime (char * ptr, size_t maxsize, const char * format,
                        const tm * timeptr);		//Format time as string, NA

#ifdef __cplusplus
}
#endif

#endif