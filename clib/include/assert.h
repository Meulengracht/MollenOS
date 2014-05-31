/* MollenOS
	- assert()
*/

#ifndef _KERNEL_ASSERT
#define _KERNEL_ASSERT

extern void _cdecl kernel_panic (const char* fmt, ...);

#define __symbol2value( x ) #x
#define __symbol2string( x ) __symbol2value( x )

#undef assert

#ifdef NDEBUG
#define assert( ignore ) ( (void) 0 )
#else

#define assert( expression ) ( ( expression ) ? (void) 0 \
        : kernel_panic( "Assertion failed: " #expression \
                          ", file " __symbol2string( __FILE__ ) \
                          ", line " __symbol2string( __LINE__ ) \
                          "." ) )
#endif
#endif
