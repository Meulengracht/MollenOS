/*
	Type Conversion
*/
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

int atoi(const char* string)
{
	return strtol(string, NULL, 10);
}



//Old implementation
//int value = 0;

//if (string)
//{
//	while (*string && (*string <= '9' && *string >= '0'))
//	{
//		value = (value * 10) + (*string - '0');
//		string++;
//	}
//}
//return value;