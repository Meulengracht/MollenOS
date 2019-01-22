
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "local.h"

#define sscanf sscanf	/* avoid to pull in FP functions. */

static char __tzname_std[11];
static char __tzname_dst[11];
static char *prev_tzenv = NULL;

void _tzset_unlocked_r(void)
{
  char *tzenv;
  unsigned short hh, mm, ss, m, w, d;
  int sign, n;
  int i, ch;
  __tzinfo_type *tz = __gettzinfo ();

  if ((tzenv = getenv("TZ")) == NULL)
      {
	__timezone = 0;
	__daylight = 0;
	__tzname[0] = "GMT";
	__tzname[1] = "GMT";
	free(prev_tzenv);
	prev_tzenv = NULL;
	return;
      }

  if (prev_tzenv != NULL && strcmp(tzenv, prev_tzenv) == 0)
    return;

  free(prev_tzenv);
  prev_tzenv = malloc(strlen(tzenv) + 1);
  if (prev_tzenv != NULL)
    strcpy (prev_tzenv, tzenv);

  /* ignore implementation-specific format specifier */
  if (*tzenv == ':')
    ++tzenv;  

  if (sscanf (tzenv, "%10[^0-9,+-]%n", __tzname_std, &n) <= 0)
    return;
 
  tzenv += n;

  sign = 1;
  if (*tzenv == '-')
    {
      sign = -1;
      ++tzenv;
    }
  else if (*tzenv == '+')
    ++tzenv;

  mm = 0;
  ss = 0;
 
  if (sscanf (tzenv, "%hu%n:%hu%n:%hu%n", &hh, &n, &mm, &n, &ss, &n) < 1)
    return;
  
  tz->__tzrule[0].offset = sign * (ss + SECSPERMIN * mm + SECSPERHOUR * hh);
  __tzname[0] = __tzname_std;
  tzenv += n;
  
  if (sscanf (tzenv, "%10[^0-9,+-]%n", __tzname_dst, &n) <= 0)
    { /* No dst */
      __tzname[1] = __tzname[0];
      __timezone = tz->__tzrule[0].offset;
      __daylight = 0;
      return;
    }
  else
    __tzname[1] = __tzname_dst;

  tzenv += n;

  /* otherwise we have a dst name, look for the offset */
  sign = 1;
  if (*tzenv == '-')
    {
      sign = -1;
      ++tzenv;
    }
  else if (*tzenv == '+')
    ++tzenv;

  hh = 0;  
  mm = 0;
  ss = 0;
  
  n  = 0;
  if (sscanf (tzenv, "%hu%n:%hu%n:%hu%n", &hh, &n, &mm, &n, &ss, &n) <= 0)
    tz->__tzrule[1].offset = tz->__tzrule[0].offset - 3600;
  else
    tz->__tzrule[1].offset = sign * (ss + SECSPERMIN * mm + SECSPERHOUR * hh);

  tzenv += n;

  for (i = 0; i < 2; ++i)
    {
      if (*tzenv == ',')
        ++tzenv;

      if (*tzenv == 'M')
	{
	  if (sscanf (tzenv, "M%hu%n.%hu%n.%hu%n", &m, &n, &w, &n, &d, &n) != 3 ||
	      m < 1 || m > 12 || w < 1 || w > 5 || d > 6)
	    return;
	  
	  tz->__tzrule[i].ch = 'M';
	  tz->__tzrule[i].m = m;
	  tz->__tzrule[i].n = w;
	  tz->__tzrule[i].d = d;
	  
	  tzenv += n;
	}
      else 
	{
	  char *end;
	  if (*tzenv == 'J')
	    {
	      ch = 'J';
	      ++tzenv;
	    }
	  else
	    ch = 'D';
	  
	  d = (unsigned short)strtoul(tzenv, &end, 10);
	  
	  /* if unspecified, default to US settings */
	  /* From 1987-2006, US was M4.1.0,M10.5.0, but starting in 2007 is
	   * M3.2.0,M11.1.0 (2nd Sunday March through 1st Sunday November)  */
	  if (end == tzenv)
	    {
	      if (i == 0)
		{
		  tz->__tzrule[0].ch = 'M';
		  tz->__tzrule[0].m = 3;
		  tz->__tzrule[0].n = 2;
		  tz->__tzrule[0].d = 0;
		}
	      else
		{
		  tz->__tzrule[1].ch = 'M';
		  tz->__tzrule[1].m = 11;
		  tz->__tzrule[1].n = 1;
		  tz->__tzrule[1].d = 0;
		}
	    }
	  else
	    {
	      tz->__tzrule[i].ch = (char)ch;
	      tz->__tzrule[i].d = d;
	    }
	  
	  tzenv = end;
	}
      
      /* default time is 02:00:00 am */
      hh = 2;
      mm = 0;
      ss = 0;
      n = 0;
      
      if (*tzenv == '/')
	sscanf (tzenv, "/%hu%n:%hu%n:%hu%n", &hh, &n, &mm, &n, &ss, &n);

      tz->__tzrule[i].s = ss + SECSPERMIN * mm + SECSPERHOUR  * hh;
      
      tzenv += n;
    }

  __tzcalc_limits (tz->__tzyear);
  __timezone = tz->__tzrule[0].offset;  
  __daylight = tz->__tzrule[0].offset != tz->__tzrule[1].offset;
}

void 
_tzset_r(void)
{
    TZ_LOCK;
    _tzset_unlocked_r();
    TZ_UNLOCK;
}
