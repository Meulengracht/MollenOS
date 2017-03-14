/* MollenOS Module
 * A module in MollenOS is equal to a driver.
 * Drivers has access to a lot of functions,
 * with information that is passed directly through a device-structure
 * along with information about the device
 */

#ifndef __MOLLENOS_MODULE__
#define __MOLLENOS_MODULE__

/* Define Export */
#ifdef MODULES_EXPORTS
#define MODULES_API __declspec(dllexport)
#else
#define MODULES_API __declspec(dllimport)
#endif

/* Includes 
 * - System */
#include <Arch.h>

/* Includes
 * - C-Library */
#include <os/osdefs.h>
#include <stddef.h>

/* Module Setup 
 * This is called as soon as the module 
 * ór server has been loaded, for servers
 * this is the main loop as well */
MODULES_API void ModuleInit(void *Data);
//MODULES_API void ModuleDestroy(void *Data);

#endif //!__MOLLENOS_MODULE__
