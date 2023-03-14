#ifndef __INTERNAL_CRT_SYSCALLS__
#define __INTERNAL_CRT_SYSCALLS__

#include <os/osdefs.h>

#if defined(i386) || defined(__i386__)
#define SCTYPE int
#elif defined(amd64) || defined(__amd64__)
#define SCTYPE long long
#endif
#define SCPARAM(Arg) ((SCTYPE)(Arg))

_CODE_BEGIN
CRTDECL(SCTYPE, syscall0(SCTYPE Function));
CRTDECL(SCTYPE, syscall1(SCTYPE Function, SCTYPE Arg0));
CRTDECL(SCTYPE, syscall2(SCTYPE Function, SCTYPE Arg0, SCTYPE Arg1));
CRTDECL(SCTYPE, syscall3(SCTYPE Function, SCTYPE Arg0, SCTYPE Arg1, SCTYPE Arg2));
CRTDECL(SCTYPE, syscall4(SCTYPE Function, SCTYPE Arg0, SCTYPE Arg1, SCTYPE Arg2, SCTYPE Arg3));
CRTDECL(SCTYPE, syscall5(SCTYPE Function, SCTYPE Arg0, SCTYPE Arg1, SCTYPE Arg2, SCTYPE Arg3, SCTYPE Arg4));
_CODE_END

///////////////////////////////////////////////
// Operating System (Module) Interface
#define Syscall_Debug(Type, Message)                                                         (oserr_t)syscall2(0, SCPARAM(Type), SCPARAM(Message))
#define Syscall_SystemStart()                                                                (oserr_t)syscall0(1)
#define Syscall_DisplayInformation(Descriptor)                                               (oserr_t)syscall1(2, SCPARAM(Descriptor))
#define Syscall_MapBootFramebuffer(BufferOut)                                                (oserr_t)syscall1(3, SCPARAM(BufferOut))
#define Syscall_MapRamdisk(BufferOut, LengthOut)                                             (oserr_t)syscall2(4, SCPARAM(BufferOut), SCPARAM(LengthOut))

#define Syscall_CreateMemorySpace(Flags, HandleOut)                                          (oserr_t)syscall2(5, SCPARAM(Flags), SCPARAM(HandleOut))
#define Syscall_GetMemorySpaceForThread(ThreadHandle, HandleOut)                             (oserr_t)syscall2(6, SCPARAM(ThreadHandle), SCPARAM(HandleOut))
#define Syscall_CreateMemorySpaceMapping(Handle, Parameters, AddressOut)                     (oserr_t)syscall3(7, SCPARAM(Handle), SCPARAM(Parameters), SCPARAM(AddressOut))

#define Syscall_AcpiQuery(Descriptor)                                                        (oserr_t)syscall1(8, SCPARAM(Descriptor))
#define Syscall_AcpiGetHeader(Signature, Header)                                             (oserr_t)syscall2(9, SCPARAM(Signature), SCPARAM(Header))
#define Syscall_AcpiGetTable(Signature, Table)                                               (oserr_t)syscall2(10, SCPARAM(Signature), SCPARAM(Table))
#define Syscall_AcpiQueryInterrupt(Bus, Slot, Pin, Interrupt, Conform)                       (oserr_t)syscall5(11, SCPARAM(Bus), SCPARAM(Slot), SCPARAM(Pin), SCPARAM(Interrupt), SCPARAM(Conform))
#define Syscall_IoSpaceRegister(IoSpace)                                                     (oserr_t)syscall1(12, SCPARAM(IoSpace))
#define Syscall_IoSpaceAcquire(IoSpace)                                                      (oserr_t)syscall1(13, SCPARAM(IoSpace))
#define Syscall_IoSpaceRelease(IoSpace)                                                      (oserr_t)syscall1(14, SCPARAM(IoSpace))
#define Syscall_IoSpaceDestroy(IoSpaceId)                                                    (oserr_t)syscall1(15, SCPARAM(IoSpaceId))
#define Syscall_InterruptAdd(Descriptor, Flags)                                              (uuid_t)syscall2(16, SCPARAM(Descriptor), SCPARAM(Flags))
#define Syscall_InterruptRemove(InterruptId)                                                 (oserr_t)syscall1(17, SCPARAM(InterruptId))
#define Syscall_GetProcessBaseAddress(BaseAddressOut)                                        (oserr_t)syscall1(18, SCPARAM(BaseAddressOut))

#define Syscall_MapThreadMemoryRegion(ThreadHandle, Address, TopOfStack, PointerOut)         (oserr_t)syscall4(19, SCPARAM(ThreadHandle), SCPARAM(Address), SCPARAM(TopOfStack), SCPARAM(PointerOut))

///////////////////////////////////////////////
//Operating System (Process) Interface
#define Syscall_ThreadCreate(Entry, Argument, Parameters, HandleOut)       (oserr_t)syscall4(20, SCPARAM(Entry), SCPARAM(Argument), SCPARAM(Parameters), SCPARAM(HandleOut))
#define Syscall_ThreadExit(ExitCode)                                       (oserr_t)syscall1(21, SCPARAM(ExitCode))
#define Syscall_ThreadSignal(ThreadId, Signal)                             (oserr_t)syscall2(22, SCPARAM(ThreadId), SCPARAM(Signal))
#define Syscall_ThreadJoin(ThreadId, ExitCode)                             (oserr_t)syscall2(23, SCPARAM(ThreadId), SCPARAM(ExitCode))
#define Syscall_ThreadDetach(ThreadId)                                     (oserr_t)syscall1(24, SCPARAM(ThreadId))
#define Syscall_ThreadYield()                                              (oserr_t)syscall0(25)
#define Syscall_ThreadId()                                                 (uuid_t)syscall0(26)
#define Syscall_ThreadCookie()                                             (UUId_t)syscall0(27)
#define Syscall_ThreadSetCurrentName(Name)                                 (uuid_t)syscall1(28, SCPARAM(Name))
#define Syscall_ThreadGetCurrentName(NameBuffer, MaxLength)                (uuid_t)syscall2(29, SCPARAM(NameBuffer), SCPARAM(MaxLength))

#define Syscall_FutexWait(context, params)                                 (oserr_t)syscall2(30, SCPARAM(context), SCPARAM(params))
#define Syscall_FutexWake(Parameters)                                      (oserr_t)syscall1(31, SCPARAM(Parameters))
#define Syscall_EventCreate(InitialValue, Flags, HandleOut, SyncAddress)   (oserr_t)syscall4(32, SCPARAM(InitialValue), SCPARAM(Flags), SCPARAM(HandleOut), SCPARAM(SyncAddress))
#define Syscall_IPCSend(Messages, MessageCount, Deadline, Context)         (oserr_t)syscall4(34, SCPARAM(Messages), SCPARAM(MessageCount), SCPARAM(Deadline), SCPARAM(Context))

#define Syscall_MemoryAllocate(Hint, Size, Flags, MemoryOut)               (oserr_t)syscall4(35, SCPARAM(Hint), SCPARAM(Size), SCPARAM(Flags), SCPARAM(MemoryOut))
#define Syscall_MemoryFree(Pointer, Size)                                  (oserr_t)syscall2(36, SCPARAM(Pointer), SCPARAM(Size))
#define Syscall_MemoryProtect(MemoryPointer, Length, Flags, PreviousFlags) (oserr_t)syscall4(37, SCPARAM(MemoryPointer), SCPARAM(Length), SCPARAM(Flags), SCPARAM(PreviousFlags))
#define Syscall_MemoryQueryAllocation(MemoryPointer, Descriptor)           (oserr_t)syscall2(38, SCPARAM(MemoryPointer), SCPARAM(Descriptor))
#define Syscall_MemoryQueryAttributes(MemoryPointer, Length, Attributes)   (oserr_t)syscall3(39, SCPARAM(MemoryPointer), SCPARAM(Length), SCPARAM(Attributes))

#define Syscall_SHMCreate(_shm, _handle)                                   (oserr_t)syscall2(40, SCPARAM(_shm), SCPARAM(_handle))
#define Syscall_SHMExport(_buffer, _shm, _handle)                          (oserr_t)syscall3(41, SCPARAM(_buffer), SCPARAM(_shm), SCPARAM(_handle))
#define Syscall_SHMAttach(_key, _handle)                                   (oserr_t)syscall2(42, SCPARAM(_key), SCPARAM(_handle))
#define Syscall_SHMMap(_handle, _offset, _length, _flags)                  (oserr_t)syscall4(43, SCPARAM(_handle), SCPARAM(_offset), SCPARAM(_length), SCPARAM(_flags))
#define Syscall_SHMCommit(_handle, _address, _length)                      (oserr_t)syscall3(44, SCPARAM(_handle), SCPARAM(_address), SCPARAM(_length))
#define Syscall_SHMUnmap(_handle, _address, _length)                       (oserr_t)syscall3(45, SCPARAM(_handle), SCPARAM(_address), SCPARAM(_length))
#define Syscall_SHMDetach(_handle)                                         (oserr_t)syscall1(46, SCPARAM(_handle))
#define Syscall_SHMMetrics(_handle, _sizeOut, _vectorsOut)                 (oserr_t)syscall3(47, SCPARAM(_handle), SCPARAM(_sizeOut), SCPARAM(_vectorsOut))

#define Syscall_CreateHandle(HandleOut)                                    (oserr_t)syscall1(48, SCPARAM(HandleOut))
#define Syscall_DestroyHandle(Handle)                                      (oserr_t)syscall1(49, SCPARAM(Handle))
#define Syscall_LookupHandle(Path, HandleOut)                              (oserr_t)syscall2(50, SCPARAM(Path), SCPARAM(HandleOut))
#define Syscall_HandleSetActivity(Handle, Flags)                           (oserr_t)syscall2(51, SCPARAM(Handle), SCPARAM(Flags))

#define Syscall_CreateHandleSet(Flags, HandleOut)                          (oserr_t)syscall2(52, SCPARAM(Flags), SCPARAM(HandleOut))
#define Syscall_ControlHandleSet(SetHandle, Operation, Handle, Event)      (oserr_t)syscall4(53, SCPARAM(SetHandle), SCPARAM(Operation), SCPARAM(Handle), SCPARAM(Event))
#define Syscall_ListenHandleSet(handle, context, params, eventsOut)        (oserr_t)syscall4(54, SCPARAM(handle), SCPARAM(context), SCPARAM(params), SCPARAM(eventsOut))

#define Syscall_InstallSignalHandler(HandlerAddress)                       (oserr_t)syscall1(55, SCPARAM(HandlerAddress))
#define Syscall_FlushHardwareCache(CacheType, AddressStart, Length)        (oserr_t)syscall3(56, SCPARAM(CacheType), SCPARAM(AddressStart), SCPARAM(Length))
#define Syscall_SystemQuery(SystemInformation)                             (oserr_t)syscall1(57, SCPARAM(SystemInformation))
#define Syscall_ClockTick(Source, TickOut)                                 (oserr_t)syscall2(58, SCPARAM(Source), SCPARAM(TickOut))
#define Syscall_ClockFrequency(Source, FrequencyOut)                       (oserr_t)syscall2(59, SCPARAM(Source), SCPARAM(FrequencyOut))
#define Syscall_Time(source, timeOut)                                      (oserr_t)syscall2(60, SCPARAM(source), SCPARAM(timeOut))
#define Syscall_Sleep(DurationNs, RemainingNs)                             (oserr_t)syscall2(61, SCPARAM(DurationNs), SCPARAM(RemainingNs))
#define Syscall_Stall(DurationNs)                                          (oserr_t)syscall1(62, SCPARAM(DurationNs))

#endif //!__INTERNAL_CRT_SYSCALLS__
