/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <arch/interrupts.h>
#include <arch/thread.h>
#include <arch/utils.h>
#include <debug.h>
#include <heap.h>
#include <machine.h>
#include <memoryspace.h>
#include <string.h>
#include <threading.h>
#include "private.h"

/**
 * @brief Allocates the AArch64 register state needed by a new thread.
 *
 * Starts with cleared SIMD/floating-point state and no userspace TLS pointer,
 * so the first context switch does not expose another thread's register values.
 * The thread owns the state buffer until ArchThreadDestroy releases it.
 *
 * @param thread Thread whose platform state is being initialized.
 * @return OS_EOK on success, or OS_EOOM if the register buffer cannot be allocated.
 */
oserr_t
ArchThreadInitialize(
    _In_ Thread_t* thread)
{
    PlatformThreadBlock_t* block = ThreadPlatformBlock(thread);

    // Every thread needs its own saved register state because the CPU's SIMD
    // and floating-point registers are reused whenever another thread runs.
    block->MathBuffer = kmalloc(ARM64_FP_STATE_SIZE);
    if (!block->MathBuffer) {
        return OS_EOOM;
    }
    
    // A new thread has nothing to restore yet. Zero also supplies the initial
    // floating-point control/status values rather than stale allocator contents.
    memset(block->MathBuffer, 0, ARM64_FP_STATE_SIZE);
    block->UserTls = 0;
    return OS_EOK;
}

/**
 * @brief Releases the saved SIMD and floating-point state of a retired thread.
 *
 * The thread must no longer run or participate in context switches. This only
 * frees the platform register buffer; stack cleanup is handled separately.
 *
 * @param thread Thread whose platform resources are being destroyed.
 * @return OS_EOK after releasing the buffer.
 */
oserr_t
ArchThreadDestroy(
    _In_ Thread_t* thread)
{
    kfree(ThreadPlatformBlock(thread)->MathBuffer);
    return OS_EOK;
}

/**
 * @brief Saves the outgoing thread's SIMD and floating-point registers.
 *
 * Called on the CPU running the thread before another thread uses those
 * registers. General registers are saved separately in the exception context.
 *
 * @param thread Outgoing thread with an initialized platform register buffer.
 */
void
ArchThreadLeave(
    _In_ Thread_t* thread)
{
    Arm64SaveFp(ThreadPlatformBlock(thread)->MathBuffer);
}

/**
 * @brief Installs the memory, TLS, and floating-point state of an incoming thread.
 *
 * Called on the CPU that will resume the thread, after the outgoing thread's
 * state has been saved. The exception return path restores general registers.
 *
 * @param core CPU selected by the shared scheduler. The hardware operations
 *             affect the calling CPU, so this implementation does not use it.
 * @param thread Incoming thread with an initialized platform register buffer.
 */
void
ArchThreadEnter(
    _In_ SystemCpuCore_t* core,
    _In_ Thread_t*        thread)
{
    PlatformThreadBlock_t* block = ThreadPlatformBlock(thread);
    (void)core;

    // Install the incoming thread's mappings before it can resume and access
    // its code, stack, or TLS. The switch is performed on the current CPU.
    MemorySpaceSwitch(ThreadMemorySpace(thread));

    // User code reads TPIDR_EL0 to find this thread's local storage. Changing it
    // does not change TPIDR_EL1, which identifies the kernel's current CPU data.
    __asm__ volatile("msr tpidr_el0, %0" :: "r"(block->UserTls) : "memory");

    // Restore the incoming thread's vector values and floating-point settings,
    // rather than letting it inherit whatever the outgoing thread left behind.
    Arm64RestoreFp(block->MathBuffer);
}

/**
 * @brief Requests that the scheduler reconsider which thread runs on this CPU.
 *
 * Outside an interrupt handler, enters the kernel's yield exception directly.
 * During interrupt handling, requests a later software interrupt instead of
 * entering the scheduler through another nested exception.
 */
void
ArchThreadYield(void)
{
    // The interrupt path already owns a saved context and may be completing
    // another request. Defer scheduling until that handler can finish, rather
    // than nesting a yield and replacing its interrupt bookkeeping.
    if (InterruptGetActiveStatus()) {
        ArchProcessorSendInterrupt(ArchGetProcessorCoreId(), INTERRUPT_LAPIC);
        return;
    }
    
    // SVC #1 saves the current execution state and enters the kernel yield
    // path, which can return to this thread or a different runnable thread.
    __asm__ volatile("svc %0" :: "i"(ARM64_SVC_YIELD) : "memory");
}

/**
 * @brief Reserves a downward-growing stack and backs its initial pages with RAM.
 *
 * Uses the current memory space. Kernel stacks use shared kernel addresses;
 * userspace stacks use the thread-specific range and permit userspace access.
 * Mapping or commit failure releases the reservation acquired by this call.
 *
 * @param contextType THREADING_CONTEXT_LEVEL0 for a kernel stack; otherwise userspace.
 * @param reserved Total stack reservation in bytes, supplied at page granularity.
 * @param committed Initial bytes to back with RAM, rounded up to whole pages.
 *                  The rounded size must fit within the reservation.
 * @param topOut Receives the address just above the stack on success.
 * @return OS_EOK on success, OS_EOOM for temporary allocation failure, or an
 *         error returned by the mapping or commit operation.
 */
static oserr_t
__AllocateStack(
    _In_  int        contextType,
    _In_  size_t     reserved,
    _In_  size_t     committed,
    _Out_ uintptr_t* topOut)
{
    MemorySpace_t* space = GetCurrentMemorySpace();
    unsigned int   flags = MAPPING_DOMAIN | MAPPING_STACK;
    unsigned int   placement = MAPPING_VIRTUAL_GLOBAL;
    uintptr_t      top;
    uintptr_t*     pages;
    oserr_t        status;

    // A userspace stack must be accessible at EL0 and placed in this thread's
    // private range. Kernel stacks stay inaccessible to userspace.
    if (contextType != THREADING_CONTEXT_LEVEL0) {
        flags |= MAPPING_USERSPACE;
        placement = MAPPING_VIRTUAL_THREAD;
    }
    
    // MAPPING_STACK requests stack handling, including a guard page. Reserve
    // addresses first; MemorySpaceCommit below supplies the initial RAM pages.
    // For a stack mapping, the returned address is its top, not its lowest byte.
    status = MemorySpaceMap(space, &(struct MemorySpaceMapOptions) {
        .Length = reserved,
        .Flags = flags,
        .PlacementFlags = placement
    }, &top);
    if (status != OS_EOK) {
        return status;
    }
    
    // Physical backing is assigned in whole pages. The temporary array gives
    // the memory subsystem one physical-address slot per committed page.
    committed = (committed + ARM64_PAGE_SIZE - 1) & ~(ARM64_PAGE_SIZE - 1ULL);
    pages = kmalloc((committed / ARM64_PAGE_SIZE) * sizeof(uintptr_t));
    if (!pages) {
        // The address reservation belongs to us until the complete stack is
        // ready. Do not leave it allocated when temporary storage is unavailable.
        MemorySpaceUnmap(space, top - reserved, reserved);
        return OS_EOOM;
    }
    
    // Stacks grow toward lower addresses, so back the pages immediately below
    // the top. UINT64_MAX permits RAM anywhere in the supported physical range;
    // these pages do not need a device-specific DMA address limit.
    status = MemorySpaceCommit(space, top - committed, pages, committed, UINT64_MAX, 0);

    // Only the address list is temporary; freeing it does not free the pages
    // attached to the stack mapping. Unmap the reservation if commit failed.
    kfree(pages);
    if (status != OS_EOK) {
        MemorySpaceUnmap(space, top - reserved, reserved);
        return status;
    }

    *topOut = top;
    return OS_EOK;
}

/**
 * @brief Locates context storage on the stack already used by the idle CPU.
 *
 * Does not allocate a stack or initialize register contents. The startup path
 * must have provided a stack page with room for the context at its upper end.
 *
 * @return Context address immediately below the current stack page's upper boundary.
 */
Context_t*
ArchThreadContextIdle(void)
{
    uintptr_t stack;

    // Reuse startup's current stack rather than allocating one for idle. SP
    // lies below its top because the stack grows downward; rounding upward
    // identifies the page boundary used to locate the base context.
    __asm__ volatile("mov %0, sp" : "=r"(stack));
    stack = (stack + ARM64_PAGE_SIZE - 1) & ~(ARM64_PAGE_SIZE - 1ULL);
    return (Context_t*)(stack - sizeof(Context_t));
}

/**
 * @brief Allocates a stack and returns its base context storage.
 *
 * Kernel stacks are fully backed before use because exception entry cannot
 * safely wait for a missing kernel stack page. Userspace stacks start with one
 * backed page; the remainder stays reserved. Register contents are set later
 * by ArchThreadContextReset.
 *
 * @param contextType THREADING_CONTEXT_LEVEL0 for a kernel stack; otherwise userspace.
 * @param contextSize Page-granular stack reservation, large enough for a context.
 * @return Base context at the top of the stack, or NULL if stack allocation fails.
 */
Context_t*
ArchThreadContextCreate(
    _In_ int    contextType,
    _In_ size_t contextSize)
{
    uintptr_t top;
    size_t    committed = contextType == THREADING_CONTEXT_LEVEL0 ? contextSize : ARM64_PAGE_SIZE;
    oserr_t   status;

    status = __AllocateStack(contextType, contextSize, committed, &top);
    if (status != OS_EOK) {
        return NULL;
    }

    // Keep the base context just below the stack's upper boundary so later
    // reset and destruction can recover the same top from its address.
    return (Context_t*)(top - sizeof(Context_t));
}

/**
 * @brief Prepares a base context to start executing at a supplied entry address.
 *
 * Selects kernel or userspace execution and supplies the entry's first argument.
 * For userspace, uses the current thread's separate kernel stack for exceptions
 * and installs its TLS pointer in both x18 and TPIDR_EL0.
 *
 * @param context Base context storage returned by context creation or idle setup.
 * @param contextType THREADING_CONTEXT_LEVEL0 for kernel execution; otherwise userspace.
 * @param address First instruction to execute when the context is restored.
 * @param argument Entry argument, passed in x0 according to the AArch64 calling convention.
 */
void
ArchThreadContextReset(
    _In_ Context_t* context,
    _In_ int        contextType,
    _In_ uintptr_t  address,
    _In_ uintptr_t  argument)
{
    uintptr_t top = (uintptr_t)context + sizeof(*context);

    // Discard old register and exception values before supplying the new entry
    // point. EL1h resumes kernel code using the kernel stack selected by Sp.
    memset(context, 0, sizeof(*context));
    context->Pc = address;
    context->X[0] = argument;
    context->Sp = top;
    context->Pstate = ARM64_PSTATE_EL1H;

    // User code must resume at EL0, not with the kernel's privilege level.
    // Keep Sp on the thread's kernel stack for exception handling; UserSp
    // names the user stack below this base context so user calls cannot
    // immediately overwrite the context storage at the top of that stack.
    if (contextType != THREADING_CONTEXT_LEVEL0) {
        Thread_t*              thread = ThreadCurrentForCore(ArchGetProcessorCoreId());
        PlatformThreadBlock_t* block = ThreadPlatformBlock(thread);

        context->Sp = (uintptr_t)ThreadContext(thread, THREADING_CONTEXT_LEVEL0) + sizeof(*context);
        context->UserSp = (uintptr_t)context;
        context->Pstate = ARM64_PSTATE_EL0T;
        
        // x18 is this platform's TLS register. Set its saved value as well as
        // the hardware TLS pointer so both user access paths select this thread.
        context->X[ARM64_REGISTER_TLS] = block->UserTls;
        __asm__ volatile("msr tpidr_el0, %0" :: "r"(block->UserTls) : "memory");
    }
}

/**
 * @brief Redirects a userspace return to an interceptor while saving the old context.
 *
 * Copies the original register state onto the selected user stack and passes
 * its address as the interceptor's first argument. The caller must provide
 * writable stack storage with room for the saved context and interceptor use.
 * This function does not validate or allocate that storage.
 *
 * @param context Saved context to redirect on its next userspace return.
 * @param temporaryStack Alternate stack top, or zero to use the saved user stack pointer.
 * @param address Interceptor's entry address.
 * @param argument0 Additional interceptor argument, passed in x1.
 * @param argument1 Additional interceptor argument, passed in x2.
 * @param argument2 Additional interceptor argument, passed in x3.
 */
void
ArchThreadContextPushInterceptor(
    _In_ Context_t* context,
    _In_ uintptr_t  temporaryStack,
    _In_ uintptr_t  address,
    _In_ uintptr_t  argument0,
    _In_ uintptr_t  argument1,
    _In_ uintptr_t  argument2)
{
    uintptr_t stack = temporaryStack ? temporaryStack : context->UserSp;

    // Preserve the interrupted state before changing any registers. Round the
    // new downward-growing stack position to the ABI's 16-byte alignment so
    // the interceptor starts with a correctly aligned stack.
    stack = (stack - sizeof(*context)) & ~(ARM64_STACK_ALIGNMENT - 1ULL);
    memcpy((void*)stack, context, sizeof(*context));

    // x0 points to the saved original context; x1-x3 carry the caller's values.
    // The interceptor can use that saved context when arranging signal return.
    context->Pc = address;
    context->UserSp = stack;
    context->X[0] = stack;
    context->X[1] = argument0;
    context->X[2] = argument1;
    context->X[3] = argument2;
}

/**
 * @brief Adjusts saved stack references after copying a kernel stack.
 *
 * Treats values in x0-x29 that fall within the old used stack range as stack
 * addresses, then repairs the copied frame-pointer chain. This is an address
 * range check, not type information about the saved register values. Does not
 * rewrite x30, saved return addresses, or arbitrary pointers in stack data.
 *
 * @param context Copied register context whose stack references will be adjusted.
 * @param oldStack Lowest address of the original used stack range.
 * @param oldTop Address just above the original stack.
 * @param newStack Lowest address of the copied used stack range.
 * @param newTop Address just above the new stack, with copied frame records already present.
 */
static void
__RelocateStackReferences(
    _InOut_ Context_t* context,
    _In_    uintptr_t  oldStack,
    _In_    uintptr_t  oldTop,
    _In_    uintptr_t  newStack,
    _In_    uintptr_t  newTop)
{
    uintptr_t  frame;
    uintptr_t* previous;
    uintptr_t  next;

    // Keep each stack reference at the same offset in the copied range. Values
    // outside that range remain unchanged; x30/LR is always a code return
    // address and is excluded even if its numeric value happens to fall inside.
    for (unsigned int index = 0; index < ARM64_REGISTER_LR; index++) {
        if (context->X[index] >= oldStack && context->X[index] < oldTop) {
            context->X[index] = newStack + (context->X[index] - oldStack);
        }
    }

    // x29 now points into the copied stack. Each frame record contains the
    // previous frame pointer followed by a saved return address. Rewrite only
    // the frame pointer; copying the return address unchanged keeps control
    // flow in the same code rather than accidentally redirecting it into RAM.
    frame = context->X[ARM64_REGISTER_FRAME];
    while (frame >= newStack && frame <= newTop - ARM64_FRAME_RECORD_SIZE) {
        // Stop before reading a misaligned record. The loop bounds also require
        // the complete frame record to fit in the copied stack storage.
        if (frame & (ARM64_STACK_ALIGNMENT - 1)) {
            break;
        }
        previous = (uintptr_t*)frame;
        next = *previous;

        // A link outside the old used stack is not ours to relocate. This also
        // ends traversal at a normal null frame pointer without dereferencing it.
        if (next < oldStack || next >= oldTop) {
            break;
        }
        next = newStack + (next - oldStack);
        // Older frames must move toward the top of a downward-growing stack.
        // Reject backward or self links so a damaged chain cannot loop forever.
        if (next <= frame) {
            break;
        }
        *previous = next;
        frame = next;
    }
}

/**
 * @brief Copies a kernel stack and builds a context that resumes the copied work.
 *
 * Allocates a fully backed destination stack, copies its used bytes, and
 * relocates saved stack references. Preserves arithmetic flags but prepares
 * kernel execution without restoring the old interrupt mask or user stack.
 * Only kernel contexts are supported by this operation.
 *
 * @param baseContext Original base context identifying the old stack's upper boundary.
 * @param returnContext Register state to resume, including the old kernel stack pointer.
 * @param contextType Must be THREADING_CONTEXT_LEVEL0.
 * @param contextSize Page-granular destination stack size, at least sizeof(Context_t)
 *                    and large enough for the copied bytes plus the new saved context.
 * @param baseContextOut Receives the new stack's base context on success.
 * @param contextOut Receives the copied return context below the new used stack on success.
 * @return OS_EOK on success, OS_EINVALPARAMS for unsupported or non-fitting contexts,
 *         or an error from stack allocation. Output pointers are written only on success.
 */
oserr_t
ArchThreadContextFork(
    _In_  Context_t*  baseContext,
    _In_  Context_t*  returnContext,
    _In_  int         contextType,
    _In_  size_t      contextSize,
    _Out_ Context_t** baseContextOut,
    _Out_ Context_t** contextOut)
{
    uintptr_t  oldTop = (uintptr_t)baseContext + sizeof(*baseContext);
    uintptr_t  oldStack = returnContext->Sp;
    size_t     used = oldTop - oldStack;
    uintptr_t  newTop;
    uintptr_t  newStack;
    Context_t* context;
    oserr_t    status;

    // Do not copy userspace stacks through this kernel-only path. The source
    // stack must point below its top, and the destination must also have room
    // for a saved exception context below the bytes that will be copied.
    if (contextType != THREADING_CONTEXT_LEVEL0 || oldStack > oldTop ||
        used > contextSize - sizeof(*context)) {
        return OS_EINVALPARAMS;
    }
    
    // Exception entry must always have committed stack storage. Unlike a
    // userspace stack, a missing kernel stack page cannot fault in safely.
    status = __AllocateStack(contextType, contextSize, contextSize, &newTop);
    if (status != OS_EOK) {
        return status;
    }
    
    // Position the copied bytes at the same distance below the new top. Place
    // the return context below them so restoring it leaves Sp at the beginning
    // of the copied live stack, ready to continue the suspended kernel work.
    newStack = newTop - used;
    memcpy((void*)newStack, (void*)oldStack, used);
    context = (Context_t*)(newStack - sizeof(*context));
    *context = *returnContext;
    context->Sp = newStack;
    
    // Retain comparison/arithmetic results, but rebuild the execution mode for
    // EL1h. The old interrupt mask and user stack do not define this new context.
    context->Pstate = (returnContext->Pstate & ARM64_PSTATE_NZCV) | ARM64_PSTATE_EL1H;
    context->UserSp = 0;
    __RelocateStackReferences(context, oldStack, oldTop, newStack, newTop);
    
    // Publish both addresses only after copying and relocation have finished:
    // the base tracks stack ownership, while context is the actual resume frame.
    *baseContextOut = (Context_t*)(newTop - sizeof(*context));
    *contextOut = context;
    return OS_EOK;
}

/**
 * @brief Releases the stack mapping associated with a kernel base context.
 *
 * Only kernel stack mappings are explicitly unmapped here. Userspace mappings
 * are left unchanged by this function. The context must no longer be in use.
 *
 * @param context Base context at the top of the stack, or NULL for no work.
 * @param contextType THREADING_CONTEXT_LEVEL0 to unmap the kernel stack.
 * @param contextSize Original stack reservation size, used to recover its lower address.
 */
void
ArchThreadContextDestroy(
    _In_ Context_t* context,
    _In_ int        contextType,
    _In_ size_t     contextSize)
{
    // Only the base context has the fixed relationship to the stack's top.
    // Add its size to recover that top, then subtract the reservation size to
    // find the mapping's lower address. Skip absent and userspace contexts.
    if (context && contextType == THREADING_CONTEXT_LEVEL0) {
        MemorySpaceUnmap(
            GetCurrentMemorySpace(),
            (uintptr_t)context + sizeof(*context) - contextSize,
            contextSize
        );
    }
}

oserr_t
ArchThreadContextDump(
    _In_ Context_t* context)
{
    for (unsigned int i = 0; i < ARM64_REGISTER_LR; i += 2) {
        DEBUG("x%u=%llx x%u=%llx", i, context->X[i], i + 1, context->X[i + 1]);
    }
    DEBUG("LR=%llx PC=%llx SP=%llx EL0SP=%llx SPSR=%llx ESR=%llx FAR=%llx",
        context->X[ARM64_REGISTER_LR], context->Pc, context->Sp, context->UserSp,
        context->Pstate, context->ErrorCode, context->FaultAddress);
    return OS_EOK;
}
