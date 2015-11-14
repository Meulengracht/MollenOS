/* MollenOS Module
* A module in MollenOS is equal to a driver.
* Drivers has access to a lot of functions,
* with information that is passed directly through a device-structure
* along with information about the device
*/
#ifndef __MOLLENOS_DRIVER__
#define __MOLLENOS_DRIVER__

/* Includes */
#include <stdint.h>

/* Definitions */
#define kFuncKernelPanic		0
#define kFuncDebugPrint			1

#define kFuncMemAlloc			2
#define kFuncMemAllocAligned	3
#define kFuncMemFree			4

#define kFuncMemMapDeviceMem	5
#define kFuncMemAllocDma		6
#define kFuncMemGetMapping		7
#define kFuncMemFreeDma			8

#define kFuncStall				9
#define kFuncSleep				10
#define kFuncReadTSC			11

#define kFuncCreateThread		12
#define kFuncYield				13
#define kFuncSleepThread		14
#define kFuncWakeThread			15

#define kFuncInstallIrqPci		16
#define kFuncInstallIrqISA		17

#define kFuncRegisterDevice		18
#define kFuncUnregisterDevice	19

#define kFuncSemaphoreCreate	20
#define kFuncSemaphoreV			21
#define kFuncSemaphoreP			22
#define kFuncSemaphoreDestroy	23

#define kFuncMutexCreate		24
#define kFuncMutexConstruct		25
#define kFuncMutexDestruct		26
#define kFuncMutexLock			27
#define kFuncMutexUnlock		28

#endif //!__MOLLENOS_DRIVER__