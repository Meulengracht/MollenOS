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

/* Arch Specific */
#include <Arch.h>
#include <x86/Pci.h>

/* MollenOS */
#include <List.h>
#include <Threading.h>
#include <Semaphore.h>

/* Structures */
typedef struct _MCoreModuleDescriptor
{
	/* Information about 
	 * the device that we want a driver for */
	PciDevice_t *Device;

	/* Functions Below */

	/* Utilities */
	void (*KernelPanic)(const char *);

	/* Stdio */
	int (*DebugPrint)(const char *format, ...);

	/* Heap */
	void *(*MemAlloc)(size_t Size);
	void *(*MemAllocAligned)(size_t Size);
	void (*MemFree)(void *Addr);

	/* Memory */
	VirtAddr_t *(*MemMapSystemMemory)(PhysAddr_t PhysicalAddr, int Pages);
	PhysAddr_t (*MemAllocDma)(void);
	PhysAddr_t (*MemVirtualGetMapping)(void *PageDirectory, VirtAddr_t VirtualAddr);
	void (*MemPhysicalFreeBlock)(PhysAddr_t Addr);

	/* I/O */
	uint16_t (*PciReadWord)(PciDevice_t *Device, uint32_t Register);
	void (*PciWriteWord)(PciDevice_t *Device, uint32_t Register, uint16_t Value);

	/* Timing */
	void (*StallMs)(uint32_t MilliSeconds);
	void (*DelayMs)(uint32_t MilliSeconds);

	/* Threading */
	TId_t (*CreateThread)(char *Name, ThreadEntry_t Function, void *Args, int Flags);
	void (*Yield)(void);

	/* Scheduling */
	void (*SchedulerSleepThread)(Addr_t *Resource);
	int (*SchedulerWakeupOneThread)(Addr_t *Resource);

	/* Irqs */
	void (*InterruptInstallPci)(PciDevice_t *PciDevice, IrqHandler_t Callback, void *Args);

	/* List Functions */
	list_t *(*ListCreate)(int Attributes);
	list_node_t *(*ListCreateNode)(int Id, void *Data);
	void (*ListAppend)(list_t *List, list_node_t *Node);
	list_node_t *(*ListPopFront)(list_t *List);
	void *(*ListGetDataById)(list_t *List, int Id, int n);
	void (*ListRemoveByNode)(list_t *List, list_node_t* Node);

	/* Spinlock */
	void (*SpinlockReset)(Spinlock_t *Spinlock);
	OsStatus_t (*SpinlockAcquire)(Spinlock_t *Spinlock);
	void (*SpinlockRelease)(Spinlock_t *Spinlock);

	/* Semaphore */
	Semaphore_t *(*SemaphoreCreate)(int Value);
	void (*SemaphoreDestroy)(Semaphore_t *Semaphore);
	void (*SemaphoreP)(Semaphore_t *Semaphore);
	void (*SemaphoreV)(Semaphore_t *Semaphore);

} MCoreModuleDescriptor_t;

#endif //!__MOLLENOS_MODULE__