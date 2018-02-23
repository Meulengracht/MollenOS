#include <math.h>
#include "private.h"

long double
lgammal(long double x) {
	return (lgammal_r(x, __signgam()));
}
