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

/* Includes */
#include <Arch.h>

/* CLib */
#include <crtdefs.h>
#include <stddef.h>
#include <stdint.h>

/* Module Setup */
MODULES_API void ModuleInit(void *Data);

#endif //!__MOLLENOS_MODULE__