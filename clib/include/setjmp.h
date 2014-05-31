/* Saves registers & stack pointer & ip
 *
 */

#ifndef __SETJMP_INC__
#define __SETJMP_INC__

//Includes
#include <stdint.h>

//Defines
#define _JBLEN  16
#define _JBTYPE int
typedef _JBTYPE jmp_buf[_JBLEN];

#ifdef __cplusplus
extern "C" {
#endif

//Functions
extern int setjmp(jmp_buf env);
extern void longjmp(jmp_buf env, int value);

#ifdef __cplusplus
}
#endif

#endif
