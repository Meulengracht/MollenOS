/**
 * Common definitions and includes for unit test environment
 */

#define _In_
#define _Out_
#define __EXTERN extern

#define _MAXPATH 512
#define __BITS 64

typedef int OsStatus_t;
typedef unsigned int UUId_t;

#define OsSuccess     (int)0
#define OsOutOfMemory (int)-1

#include <stdint.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdarg.h>

#define PRIxIN "lx"
#define PRIuIN "lu"

#define STR(str)                    str "\n"
#define TRACE(...)   printf(__VA_ARGS__)
#define WARNING(...) printf(__VA_ARGS__)
#define ERROR(...)   fprintf(stderr, __VA_ARGS__)

#define MIN(a,b)                                (((a)<(b))?(a):(b))
#define MAX(a,b)                                (((a)>(b))?(a):(b))
