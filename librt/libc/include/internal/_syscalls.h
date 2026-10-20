#ifndef __INTERNAL_CRT_SYSCALLS__
#define __INTERNAL_CRT_SYSCALLS__

#include <os/osdefs.h>

#if defined(i386) || defined(__i386__)
#define SCTYPE int
#elif defined(amd64) || defined(__amd64__) || defined(__aarch64__)
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
#define Syscall_Debug(_level, _msg)                                                   (oserr_t)syscall2(0, SCPARAM(_level), SCPARAM(_msg))
#define Syscall_MigrateLogs(_buffer, _size, _readOut)                                 (oserr_t)syscall3(1, SCPARAM(_buffer), SCPARAM(_size), SCPARAM(_readOut))
#define Syscall_MapBootFramebuffer(BufferOut)                                         (oserr_t)syscall1(2, SCPARAM(BufferOut))
#define Syscall_MapRamdisk(BufferOut, LengthOut)                                      (oserr_t)syscall2(3, SCPARAM(BufferOut), SCPARAM(LengthOut))

#define Syscall_CreateMemorySpace(Flags, HandleOut)                                   (oserr_t)syscall2(4, SCPARAM(Flags), SCPARAM(HandleOut))
#define Syscall_GetMemorySpaceForThread(ThreadHandle, HandleOut)                      (oserr_t)syscall2(5, SCPARAM(ThreadHandle), SCPARAM(HandleOut))
#define Syscall_CreateMemorySpaceMapping(Handle, Parameters, AddressOut)              (oserr_t)syscall3(6, SCPARAM(Handle), SCPARAM(Parameters), SCPARAM(AddressOut))

#define Syscall_FirmwareQuery(InfoOut)                                                (oserr_t)syscall1(7, SCPARAM(InfoOut))
#define Syscall_FirmwareTableLocate(Key, TableOut)                                    (oserr_t)syscall2(8, SCPARAM(Key), SCPARAM(TableOut))
#define Syscall_FirmwareTableRead(Key, Buffer, Size, LengthOut)                       (oserr_t)syscall4(9, SCPARAM(Key), SCPARAM(Buffer), SCPARAM(Size), SCPARAM(LengthOut))
#define Syscall_FirmwareTableMap(Key, MappingOut, LengthOut)                          (oserr_t)syscall3(10, SCPARAM(Key), SCPARAM(MappingOut), SCPARAM(LengthOut))
#define Syscall_AcpiQueryInterrupt(Bus, Slot, Pin, Interrupt, Conform)                (oserr_t)syscall5(11, SCPARAM(Bus), SCPARAM(Slot), SCPARAM(Pin), SCPARAM(Interrupt), SCPARAM(Conform))
#define Syscall_IoSpaceRegister(IoSpace)                                              (oserr_t)syscall1(12, SCPARAM(IoSpace))
#define Syscall_IoSpaceAcquire(IoSpace)                                               (oserr_t)syscall1(13, SCPARAM(IoSpace))
#define Syscall_IoSpaceRelease(IoSpace)                                               (oserr_t)syscall1(14, SCPARAM(IoSpace))
#define Syscall_IoSpaceDestroy(IoSpaceId)                                             (oserr_t)syscall1(15, SCPARAM(IoSpaceId))
#define Syscall_InterruptAllocate(Request, Result)                                    (oserr_t)syscall2(16, SCPARAM(Request), SCPARAM(Result))
#define Syscall_InterruptBind(Handle, Request)                                        (oserr_t)syscall2(17, SCPARAM(Handle), SCPARAM(Request))
#define Syscall_InterruptEnable(Handle)                                               (oserr_t)syscall1(18, SCPARAM(Handle))
#define Syscall_InterruptDisable(Handle)                                              (oserr_t)syscall1(19, SCPARAM(Handle))
#define Syscall_InterruptUnbind(Handle)                                               (oserr_t)syscall1(20, SCPARAM(Handle))
#define Syscall_InterruptRelease(Handle)                                              (oserr_t)syscall1(21, SCPARAM(Handle))
#define Syscall_GetProcessBaseAddress(BaseAddressOut)                                 (oserr_t)syscall1(22, SCPARAM(BaseAddressOut))

#define Syscall_MapThreadMemoryRegion(ThreadHandle, Address, TopOfStack, PointerOut)  (oserr_t)syscall4(23, SCPARAM(ThreadHandle), SCPARAM(Address), SCPARAM(TopOfStack), SCPARAM(PointerOut))

///////////////////////////////////////////////
//Operating System (Process) Interface
#define Syscall_ThreadCreate(Entry, Argument, Parameters, HandleOut)       (oserr_t)syscall4(24, SCPARAM(Entry), SCPARAM(Argument), SCPARAM(Parameters), SCPARAM(HandleOut))
#define Syscall_ThreadExit(ExitCode)                                       (oserr_t)syscall1(25, SCPARAM(ExitCode))
#define Syscall_ThreadSignal(ThreadId, Signal)                             (oserr_t)syscall2(26, SCPARAM(ThreadId), SCPARAM(Signal))
#define Syscall_ThreadJoin(ThreadId, ExitCode)                             (oserr_t)syscall2(27, SCPARAM(ThreadId), SCPARAM(ExitCode))
#define Syscall_ThreadDetach(ThreadId)                                     (oserr_t)syscall1(28, SCPARAM(ThreadId))
#define Syscall_ThreadYield()                                              (oserr_t)syscall0(29)
#define Syscall_ThreadId()                                                 (uuid_t)syscall0(30)
#define Syscall_ThreadCookie()                                             (UUId_t)syscall0(31)
#define Syscall_ThreadSetCurrentName(Name)                                 (uuid_t)syscall1(32, SCPARAM(Name))
#define Syscall_ThreadGetCurrentName(NameBuffer, MaxLength)                (uuid_t)syscall2(33, SCPARAM(NameBuffer), SCPARAM(MaxLength))

#define Syscall_FutexWait(context, params)                                 (oserr_t)syscall2(34, SCPARAM(context), SCPARAM(params))
#define Syscall_FutexWake(Parameters)                                      (oserr_t)syscall1(35, SCPARAM(Parameters))
#define Syscall_EventCreate(InitialValue, Flags, HandleOut, SyncAddress)   (oserr_t)syscall4(36, SCPARAM(InitialValue), SCPARAM(Flags), SCPARAM(HandleOut), SCPARAM(SyncAddress))
#define Syscall_IPCSend(Messages, MessageCount, Deadline, Context)         (oserr_t)syscall4(37, SCPARAM(Messages), SCPARAM(MessageCount), SCPARAM(Deadline), SCPARAM(Context))

#define Syscall_MemoryAllocate(Hint, Size, Flags, MemoryOut)               (oserr_t)syscall4(38, SCPARAM(Hint), SCPARAM(Size), SCPARAM(Flags), SCPARAM(MemoryOut))
#define Syscall_MemoryFree(Pointer, Size)                                  (oserr_t)syscall2(39, SCPARAM(Pointer), SCPARAM(Size))
#define Syscall_MemoryProtect(MemoryPointer, Length, Flags, PreviousFlags) (oserr_t)syscall4(40, SCPARAM(MemoryPointer), SCPARAM(Length), SCPARAM(Flags), SCPARAM(PreviousFlags))
#define Syscall_MemoryQueryAllocation(MemoryPointer, Descriptor)           (oserr_t)syscall2(41, SCPARAM(MemoryPointer), SCPARAM(Descriptor))
#define Syscall_MemoryQueryAttributes(MemoryPointer, Length, Attributes)   (oserr_t)syscall3(42, SCPARAM(MemoryPointer), SCPARAM(Length), SCPARAM(Attributes))

#define Syscall_SHMCreate(_shm, _handle)                                   (oserr_t)syscall2(43, SCPARAM(_shm), SCPARAM(_handle))
#define Syscall_SHMExport(_buffer, _shm, _handle)                          (oserr_t)syscall3(44, SCPARAM(_buffer), SCPARAM(_shm), SCPARAM(_handle))
#define Syscall_SHMConform(_key, _params, _handle)                         (oserr_t)syscall3(45, SCPARAM(_key), SCPARAM(_params), SCPARAM(_handle))
#define Syscall_SHMAttach(_key, _handle)                                   (oserr_t)syscall2(46, SCPARAM(_key), SCPARAM(_handle))
#define Syscall_SHMMap(_handle, _offset, _length, _flags)                  (oserr_t)syscall4(47, SCPARAM(_handle), SCPARAM(_offset), SCPARAM(_length), SCPARAM(_flags))
#define Syscall_SHMCommit(_handle, _address, _length)                      (oserr_t)syscall3(48, SCPARAM(_handle), SCPARAM(_address), SCPARAM(_length))
#define Syscall_SHMUnmap(_handle, _address, _length)                       (oserr_t)syscall3(49, SCPARAM(_handle), SCPARAM(_address), SCPARAM(_length))
#define Syscall_SHMDetach(_handle)                                         (oserr_t)syscall1(50, SCPARAM(_handle))
#define Syscall_SHMMetrics(_handle, _sizeOut, _vectorsOut)                 (oserr_t)syscall3(51, SCPARAM(_handle), SCPARAM(_sizeOut), SCPARAM(_vectorsOut))

#define Syscall_CreateHandle(HandleOut)                                    (oserr_t)syscall1(52, SCPARAM(HandleOut))
#define Syscall_DestroyHandle(Handle)                                      (oserr_t)syscall1(53, SCPARAM(Handle))
#define Syscall_LookupHandle(Path, HandleOut)                              (oserr_t)syscall2(54, SCPARAM(Path), SCPARAM(HandleOut))
#define Syscall_HandleSetActivity(Handle, Flags)                           (oserr_t)syscall2(55, SCPARAM(Handle), SCPARAM(Flags))

#define Syscall_CreateHandleSet(Flags, HandleOut)                          (oserr_t)syscall2(56, SCPARAM(Flags), SCPARAM(HandleOut))
#define Syscall_ControlHandleSet(SetHandle, Operation, Handle, Event)      (oserr_t)syscall4(57, SCPARAM(SetHandle), SCPARAM(Operation), SCPARAM(Handle), SCPARAM(Event))
#define Syscall_ListenHandleSet(handle, context, params, eventsOut)        (oserr_t)syscall4(58, SCPARAM(handle), SCPARAM(context), SCPARAM(params), SCPARAM(eventsOut))

#define Syscall_InstallSignalHandler(HandlerAddress)                       (oserr_t)syscall1(59, SCPARAM(HandlerAddress))
#define Syscall_FlushHardwareCache(CacheType, AddressStart, Length)        (oserr_t)syscall3(60, SCPARAM(CacheType), SCPARAM(AddressStart), SCPARAM(Length))
#define Syscall_SystemQuery(_req, _buffer, _size, _sizeOut)                (oserr_t)syscall4(61, SCPARAM(_req), SCPARAM(_buffer), SCPARAM(_size), SCPARAM(_sizeOut))
#define Syscall_ClockTick(Source, TickOut)                                 (oserr_t)syscall2(62, SCPARAM(Source), SCPARAM(TickOut))
#define Syscall_ClockFrequency(Source, FrequencyOut)                       (oserr_t)syscall2(63, SCPARAM(Source), SCPARAM(FrequencyOut))
#define Syscall_Time(source, timeOut)                                      (oserr_t)syscall2(64, SCPARAM(source), SCPARAM(timeOut))
#define Syscall_Sleep(DurationNs, RemainingNs)                             (oserr_t)syscall2(65, SCPARAM(DurationNs), SCPARAM(RemainingNs))
#define Syscall_Stall(DurationNs)                                          (oserr_t)syscall1(66, SCPARAM(DurationNs))

#define Syscall_DmaOpen(Grant, Limit, ContextOut)                          (oserr_t)syscall3(67, SCPARAM(Grant), SCPARAM(Limit), SCPARAM(ContextOut))
#define Syscall_DmaAllocate(Context, Requirements, ResultOut)              (oserr_t)syscall3(68, SCPARAM(Context), SCPARAM(Requirements), SCPARAM(ResultOut))
#define Syscall_DmaMapShm(Context, Shm, Parameters, ResultOut)             (oserr_t)syscall4(69, SCPARAM(Context), SCPARAM(Shm), SCPARAM(Parameters), SCPARAM(ResultOut))
#define Syscall_DmaGetSegments(Mapping, Entries, Capacity, CountOut)       (oserr_t)syscall4(70, SCPARAM(Mapping), SCPARAM(Entries), SCPARAM(Capacity), SCPARAM(CountOut))
#define Syscall_DmaSyncForDevice(Mapping, Offset, Length)                  (oserr_t)syscall3(71, SCPARAM(Mapping), SCPARAM(Offset), SCPARAM(Length))
#define Syscall_DmaSyncForCpu(Mapping, Offset, Length)                     (oserr_t)syscall3(72, SCPARAM(Mapping), SCPARAM(Offset), SCPARAM(Length))
#define Syscall_DmaUnmap(Mapping)                                          (oserr_t)syscall1(73, SCPARAM(Mapping))
#define Syscall_DmaClose(Context)                                          (oserr_t)syscall1(74, SCPARAM(Context))

#endif //!__INTERNAL_CRT_SYSCALLS__
