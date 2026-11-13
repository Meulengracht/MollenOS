# Codestyle

This document serves to document the current coding style of the Vali/MollenOS project. The project has lived
since 2011, and my coding style has changed a lot over 15 years. In the OS project, you will find several coding
styles that have evolved. The purpose of this document is to document coding style going forward for introduced
code, or code being refactored.

The examples below illustrate the intended style. They omit license notices and unrelated declarations;
they are not replacements for the existing source files.

## General rules

* Prefix file-local functions and structures with `__`.
* Always add proper logging using existing logging facilities. 
* All source and header files should start with the default GPL copyright header.
  * A .c/.h file may additionally detail the responsibility of the file inside the same
    comment block as the GPL header.
* Ensure detailed inline documentation. 
  * If a function orchestrates it should explain the flow and why the order is important.
  * If a function uses hardcoded values or accesses hardware registers, they must be defined
    and documented.
* Variables should always be declared at the start of the scope they are used.
* Structure members and variable declarations should always be block indented. 
* Function parameters should be block indented
* If a coding style is not documented, always look at adjacent files.
* Error handling must be consistent, and every function that can return an error should
  either be logged and/or propagated as a minimum.
* Comments inside functions must always be in style of `//`.
* Inside headers, we use the following format to describe functions
* For non-trivial structures, document what information they carry.
* For function calls that are long, please use a newline per parameter.

## The bootloaders, kernel, modules and services

* Use PascalCase for functions, structure names and structure members.
  * Functions should start with their namespace. Examples:
    * DMAOpen (the first abbreviation should be in upper-case)
    * MemorySpaceCreate
    * ByteBufferCreate
* Use camelCase for parameters and local variables. 
* Function parameter declarations should use SAL notation.
* All outer-facing structures should be typedef'd, and
  the typedef'd type is the one that should be used.
* Ensure detailed documentation, as seen in the examples.
* Always prefer the mstring_t framework over a char* if possible.

* Kernel specific rules:
  * Function declarations in header must use `KERNELABI <return-type> KERNELAPI`.
* Bootloaders, modules and service rules:
  * Function declarations in header must use `__EXTERN`.

### Examples

#### Header example: `byte_buffer.h`

```c
/**
 * GPL header ...
 */

#ifndef __BYTE_BUFFER_H__
#define __BYTE_BUFFER_H__

#include <os/osdefs.h>

enum ByteBufferType {
    BYTEBUFFER_TYPE_SIMPLE,
    BYTEBUFFER_TYPE_COMPLEX
};

/**
 * @brief Tracks a byte buffer's storage, capacity and used length.
 * Data must remain valid for Capacity bytes, and Length must not exceed Capacity.
 */
typedef struct ByteBuffer {
    enum ByteBufferType Type;
    uint8_t*            Data;
    size_t              Capacity;
    size_t              Length;
} ByteBuffer_t;

/**
 * @brief Appends bytes to an initialized buffer without allocating storage.
 *
 * @param buffer Buffer to update. Must satisfy the ByteBuffer_t constraints.
 * @param data Input bytes. Must be readable for length bytes and may overlap Data.
 * @param length Number of bytes to append.
 * @return OS_EOK on success, or OS_EBUFFER if there is insufficient space.
 *         The buffer is unchanged on failure.
 */
__EXTERN oserr_t
ByteBufferAppend(
    _InOut_ ByteBuffer_t* buffer,
    _In_    const void*   data,
    _In_    size_t        length);

#endif //!__BYTE_BUFFER_H__
```

#### Source example: `byte_buffer.c`

```c
/**
 * GPL header ...
 */

#include "byte_buffer.h"
#include <string.h>

oserr_t
ByteBufferAppend(
    _InOut_ ByteBuffer_t* buffer,
    _In_    const void*   data,
    _In_    size_t        length)
{
    size_t available;

    // Ensure length actually fits into the buffer
    available = buffer->Capacity - buffer->Length;
    if (length > available) {
        return OS_EBUFFER;
    }

    // For zero lengths, we just return
    if (length == 0) {
        return OS_EOK;
    }

    // A comment on why we use memmove
    memmove(
        buffer->Data + buffer->Length,
        data,
        length
    );
    buffer->Length += length;
    return OS_EOK;
}
```

## LibC

LibC is a different project. The standard C library coding style specifies a different coding style than the one
I normally would use, but consistency is always king. For libc specific code, we follow the standard used by the
libc library API.

Keep standard API names and signatures. Use snake_case for library-specific names, including structure names
and members, and prefix file-local helpers with `__`. Public declarations use `CRTDECL` inside
`_CODE_BEGIN` and `_CODE_END`. Keep each function's specified return values and `errno` behavior.

### Examples

#### Header example: [string.h](librt/libc/include/string.h) (excerpt)

```c
/**
 * GPL header ...
 */

#ifndef __STDC_STRING__
#define __STDC_STRING__

#include <crtdefs.h>
#include <stddef.h>

_CODE_BEGIN

/**
 * @brief Allocates a copy of a null-terminated string.
 *
 * @param string String to copy. Must be a valid null-terminated string.
 * @return The allocated copy, to be released with free, or NULL on allocation
 *         failure with errno set to ENOMEM.
 */
CRTDECL(char*,
strdup(
    _In_ const char* string));

_CODE_END

#endif //!__STDC_STRING__
```

#### Source example: [strdup.c](librt/libc/string/strdup.c)

```c
/**
 * GPL header ...
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

char*
strdup(
    _In_ const char* string)
{
    size_t string_size;
    char*  copy;

    string_size = strlen(string) + 1;
    copy = malloc(string_size);
    if (copy == NULL) {
        _set_errno(ENOMEM);
        return NULL;
    }

    memcpy(copy, string, string_size);
    return copy;
}
```

## LibOS

Keep the main project's function layout and CamelCase structure names and members. 
Public runtime declarations use `CRTDECL` inside `_CODE_BEGIN` and `_CODE_END`. 

* Use an `OS`-prefixed API and returns `oserr_t` directly instead of translating errors to `errno`.
* Use PascalCase for functions, structure names and structure members.
  * Functions should start with their namespace. Examples:
    * OSDmaOpen
    * OSBufferCreate
* Use camelCase for parameters and local variables. 
* Function parameter declarations should use SAL notation.
* All outer-facing structures should be typedef'd, and the 
  typedef'd type is the one that should be used.
* Ensure detailed documentation, as seen in the examples.


### Examples

#### Header example: [time.h](librt/libos/include/os/time.h) (excerpt)

```c
/**
 * GPL header ...
 */

#ifndef __OS_TIME_H__
#define __OS_TIME_H__

#include <os/osdefs.h>
#include <os/types/time.h>

_CODE_BEGIN

/**
 * @brief Reads a clock source's frequency in ticks per second.
 *
 * @param source Clock source to query.
 * @param frequencyOut Receives the frequency on success. Must be writable.
 * @return OS_EOK on success, OS_EINVALPARAMS for a missing output pointer,
 *         or an error from the kernel.
 */
CRTDECL(oserr_t,
OSGetClockFrequency(
    _In_  enum OSClockSource source,
    _Out_ UInteger64_t*      frequencyOut));

_CODE_END

#endif
```

#### Source example: [time.c](librt/libos/time.c) (excerpt)

```c
/**
 * GPL header ...
 */

#include <internal/_syscalls.h>
#include <os/time.h>

oserr_t
OSGetClockFrequency(
    _In_  enum OSClockSource source,
    _Out_ UInteger64_t*      frequencyOut)
{
    if (frequencyOut == NULL) {
        return OS_EINVALPARAMS;
    }

    return Syscall_ClockFrequency(source, frequencyOut);
}
```

## Anything else

For any project not specified in the above titles, please align with the bootloader/kernel/modules/services section.
Use its [header and source examples](#the-bootloaders-kernel-modules-and-services).
