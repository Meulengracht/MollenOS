#include "LibC.h"

/*
	To test the library, include "LibC.h" from an application project
	and call LibCTest().
	
	Do not forget to add the library to Project Dependencies in Visual Studio.
*/

static int s_Test = 0;

int LibCTest(void)
{
	return ++s_Test;
}