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

#define kFuncCreateThread		11
#define kFuncYield				12
#define kFuncSleepThread		13
#define kFuncWakeThread			14

#define kFuncInstallIrqPci		15
#define kFuncInstallIrqISA		16

#define kFuncRegisterDevice		17
#define kFuncUnregisterDevice	18


#endif //!__MOLLENOS_DRIVER__