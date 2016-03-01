/*
 * strings.h
 *
 * Definitions for string operations.
 */

#ifndef _STRINGS_H_
#define _STRINGS_H_

#include <crtdefs.h> /* for size_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Str comparison */
extern int strcasecmp(const char *, const char *);
extern int strncasecmp(const char *, const char *, size_t);

#ifdef __cplusplus
}
#endif


#endif /* _STRINGS_H_ */
