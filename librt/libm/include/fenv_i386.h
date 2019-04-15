// i386 helpers for floating point environment support
#ifndef __FENV_I386_H__
#define __FENV_I386_H__

#if defined(_MSC_VER) && !defined(__clang__)
#define __finit()           _asm finit
#define	__fldcw(__cw)		_asm fldcw dword ptr [__cw]
#define	__fldenv(__env)		_asm fldenv dword ptr [__env]
#define	__fldenvx(__env)	_asm fldenv dword ptr [__env]
#define	__fnclex()			_asm fnclex
#define	__fnstenv(__env)	_asm fnstenv dword ptr [__env]
#define	__fnstcw(__cw)		_asm fnstcw dword ptr [__cw]
#define	__fnstsw(__sw)		_asm fnstsw dword ptr [__sw]
#define	__fwait()			_asm fwait
#define	__ldmxcsr(__csr)	_asm ldmxcsr dword ptr [__csr]
#define	__stmxcsr(__csr)	_asm stmxcsr dword ptr [__csr]
#else
#define	__finit()           __asm __volatile("fninit")
#define	__fldcw(__cw)		__asm __volatile("fldcw %0" : : "m" (__cw))
#define	__fldenv(__env)		__asm __volatile("fldenv %0" : : "m" (__env))
#define	__fldenvx(__env)	__asm __volatile("fldenv %0" : : "m" (__env)  \
				: "st", "st(1)", "st(2)", "st(3)", "st(4)",   \
				"st(5)", "st(6)", "st(7)")
#define	__fnclex()		__asm __volatile("fnclex")
#define	__fnstenv(__env)	__asm __volatile("fnstenv %0" : "=m" (*(__env)))
#define	__fnstcw(__cw)		__asm __volatile("fnstcw %0" : "=m" (*(__cw)))
#define	__fnstsw(__sw)		__asm __volatile("fnstsw %0" : "=am" (*(__sw)))
#define	__fwait()		__asm __volatile("fwait")
#define	__ldmxcsr(__csr)	__asm __volatile("ldmxcsr %0" : : "m" (__csr))
#define	__stmxcsr(__csr)	__asm __volatile("stmxcsr %0" : "=m" (*(__csr)))
#endif

#endif //!__FENV_I386_H__
