/* MollenOS Errno LibC
 *
 */

#include <errno.h>

//errno
int _errno = 0;

int *__errno (void)
{
	return &_errno;
}