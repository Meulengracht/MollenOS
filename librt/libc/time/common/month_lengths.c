#include "../local.h"

// Whether or not the current timezone has daylight savings
int         __daylight  = 0;
long        __timezone  = 0;
char*       __tzname[2] = {
    "GMT", "GMT"
};

const int __month_lengths[2][MONSPERYEAR] = {
    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
    {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
};

const int __days_before_month[2][MONSPERYEAR] = {
    {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334},
    {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335}
};
