/*
 * @implemented
 */
#include <string.h>
#include <stddef.h>

#pragma warning(disable:4738)

float strtof(const char *s, char **sret)
{
	double dret = strtod(s, sret);
	float result = (float)dret;
	return result;
}
