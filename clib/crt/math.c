// implementation of pow function as 2^(y*log2(x)). this is the 
// intrinsic version so base and exponent are already on the fpu 
// stack ST0 and ST1. the result is pushed to ST0.
// note that earlier rounding for fist was set to truncate

#include <internal/_all.h>
#include <math.h>


double _cdecl _CIpow(void)
{
	FPU_DOUBLES(x, y);
	return pow(x, y);
}

/*
 * @implemented
 */
//double _cdecl _CItan(void)
//{
//	FPU_DOUBLE(x);
//	return tan(x);
//}

/*
 * @implemented
 */
double _cdecl _CIsinh(void)
{
	FPU_DOUBLE(x);
	return sinh(x);
}

/*
 * @implemented
 */
double _cdecl _CIcosh(void)
{
	FPU_DOUBLE(x);
	return cosh(x);
}

/*
 * @implemented
 */
double _cdecl _CItanh(void)
{
	FPU_DOUBLE(x);
	return tanh(x);
}

/*
 * @implemented
 */
//double _cdecl _CIasin(void)
//{
//	FPU_DOUBLE(x);
//	return asin(x);
//}

/*
 * @implemented
 */
//double _cdecl _CIacos(void)
//{
//	FPU_DOUBLE(x);
//	return acos(x);
//}

/*
 * @implemented
 */
//double _cdecl _CIatan(void)
//{
//	FPU_DOUBLE(x);
//	return atan(x);
//}

/*
 * @implemented
 */
//double _cdecl _CIatan2(void)
//{
//	FPU_DOUBLES(x, y);
//	return atan2(x, y);
//}

/*
 * @implemented
 */
double _cdecl _CIexp(void)
{
	FPU_DOUBLE(x);
	return exp(x);
}

/*
 * @implemented
 */
double _cdecl _CIlog10(void)
{
	FPU_DOUBLE(x);
	return log10(x);
}

/*
 * @implemented
 */
//double _cdecl _CIfmod(void)
//{
//	FPU_DOUBLES(x, y);
//	return fmod(x, y);
//}

/*
 * @implemented
 */
double _cdecl _CIcos(void)
{
	FPU_DOUBLE(x);
	return cos(x);
}

/*
 * @implemented
 */
double _cdecl _CIlog(void)
{
	FPU_DOUBLE(x);
	return log(x);
}

/*
 * @implemented
 */
double _cdecl _CIsin(void)
{
	FPU_DOUBLE(x);
	return sin(x);
}

/*
 * @implemented
 */
double _cdecl _CIsqrt(void)
{
	FPU_DOUBLE(x);
	return sqrt(x);
}
