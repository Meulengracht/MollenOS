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
#define kFuncDelay				11
#define kFuncReadTSC			12

#define kFuncCreateThread		13
#define kFuncYield				14
#define kFuncSleepThread		15
#define kFuncWakeThread			16

#define kFuncInstallIrqPci		17
#define kFuncInstallIrqISA		18

#define kFuncRegisterDevice		19
#define kFuncUnregisterDevice	20

#define kFuncReadPciDevice		21
#define kFuncWritePciDevice		22

#define kFuncSemaphoreCreate	23
#define kFuncSemaphoreV			24
#define kFuncSemaphoreP			25
#define kFuncSemaphoreDestroy	26

#define kFuncMutexCreate		27
#define kFuncMutexConstruct		28
#define kFuncMutexDestruct		29
#define kFuncMutexLock			30
#define kFuncMutexUnlock		31

#endif //!__MOLLENOS_DRIVER__