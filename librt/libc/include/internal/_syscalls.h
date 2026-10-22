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
#define Syscall_CapabilityGrant(_capability, _handle)                                 (oserr_t)syscall2(4, SCPARAM(_capability), SCPARAM(_handle))

#define Syscall_CreateMemorySpace(Flags, HandleOut)                                   (oserr_t)syscall2(5, SCPARAM(Flags), SCPARAM(HandleOut))
#define Syscall_GetMemorySpaceForThread(ThreadHandle, HandleOut)                      (oserr_t)syscall2(6, SCPARAM(ThreadHandle), SCPARAM(HandleOut))
#define Syscall_CreateMemorySpaceMapping(Handle, Parameters, AddressOut)              (oserr_t)syscall3(7, SCPARAM(Handle), SCPARAM(Parameters), SCPARAM(AddressOut))

#define Syscall_FirmwareQuery(InfoOut)                                                (oserr_t)syscall1(8, SCPARAM(InfoOut))
#define Syscall_FirmwareTableLocate(Key, TableOut)                                    (oserr_t)syscall2(9, SCPARAM(Key), SCPARAM(TableOut))
#define Syscall_FirmwareTableRead(Key, Buffer, Size, LengthOut)                       (oserr_t)syscall4(10, SCPARAM(Key), SCPARAM(Buffer), SCPARAM(Size), SCPARAM(LengthOut))
#define Syscall_FirmwareTableMap(Key, MappingOut, LengthOut)                          (oserr_t)syscall3(11, SCPARAM(Key), SCPARAM(MappingOut), SCPARAM(LengthOut))
#define Syscall_AcpiQueryInterrupt(Bus, Slot, Pin, Interrupt, Conform)                (oserr_t)syscall5(12, SCPARAM(Bus), SCPARAM(Slot), SCPARAM(Pin), SCPARAM(Interrupt), SCPARAM(Conform))

#define Syscall_IoSpaceRegister(IoSpace)                                              (oserr_t)syscall1(13, SCPARAM(IoSpace))
#define Syscall_IoSpaceAcquire(IoSpace)                                               (oserr_t)syscall1(14, SCPARAM(IoSpace))
#define Syscall_IoSpaceMap(Id, Offset, Length, Mapping)                               (oserr_t)syscall4(15, SCPARAM(Id), SCPARAM(Offset), SCPARAM(Length), SCPARAM(Mapping))
#define Syscall_IoSpaceRelease(IoSpace)                                               (oserr_t)syscall1(16, SCPARAM(IoSpace))
#define Syscall_IoSpaceDestroy(IoSpaceId)                                             (oserr_t)syscall1(17, SCPARAM(IoSpaceId))

#define Syscall_InterruptAllocate(Request, Result)                                    (oserr_t)syscall2(18, SCPARAM(Request), SCPARAM(Result))
#define Syscall_InterruptBind(Handle, Request)                                        (oserr_t)syscall2(19, SCPARAM(Handle), SCPARAM(Request))
#define Syscall_InterruptEnable(Handle)                                               (oserr_t)syscall1(20, SCPARAM(Handle))
#define Syscall_InterruptDisable(Handle)                                              (oserr_t)syscall1(21, SCPARAM(Handle))
#define Syscall_InterruptUnbind(Handle)                                               (oserr_t)syscall1(22, SCPARAM(Handle))
#define Syscall_InterruptRelease(Handle)                                              (oserr_t)syscall1(23, SCPARAM(Handle))
#define Syscall_InterruptGetMessage(Handle, Index, Message)                           (oserr_t)syscall3(24, SCPARAM(Handle), SCPARAM(Index), SCPARAM(Message))
#define Syscall_InterruptControllerResolve(Query, Domain)                             (oserr_t)syscall2(25, SCPARAM(Query), SCPARAM(Domain))

#define Syscall_GetProcessBaseAddress(BaseAddressOut)                                 (oserr_t)syscall1(26, SCPARAM(BaseAddressOut))
#define Syscall_MapThreadMemoryRegion(ThreadHandle, Address, TopOfStack, PointerOut)  (oserr_t)syscall4(27, SCPARAM(ThreadHandle), SCPARAM(Address), SCPARAM(TopOfStack), SCPARAM(PointerOut))

///////////////////////////////////////////////
//Operating System (Process) Interface
#define Syscall_ThreadCreate(Entry, Argument, Parameters, HandleOut)       (oserr_t)syscall4(28, SCPARAM(Entry), SCPARAM(Argument), SCPARAM(Parameters), SCPARAM(HandleOut))
#define Syscall_ThreadExit(ExitCode)                                       (oserr_t)syscall1(29, SCPARAM(ExitCode))
#define Syscall_ThreadSignal(ThreadId, Signal)                             (oserr_t)syscall2(30, SCPARAM(ThreadId), SCPARAM(Signal))
#define Syscall_ThreadJoin(ThreadId, ExitCode)                             (oserr_t)syscall2(31, SCPARAM(ThreadId), SCPARAM(ExitCode))
#define Syscall_ThreadDetach(ThreadId)                                     (oserr_t)syscall1(32, SCPARAM(ThreadId))
#define Syscall_ThreadYield()                                              (oserr_t)syscall0(33)
#define Syscall_ThreadId()                                                 (uuid_t)syscall0(34)
#define Syscall_ThreadCookie()                                             (UUId_t)syscall0(35)
#define Syscall_ThreadSetCurrentName(Name)                                 (uuid_t)syscall1(36, SCPARAM(Name))
#define Syscall_ThreadGetCurrentName(NameBuffer, MaxLength)                (uuid_t)syscall2(37, SCPARAM(NameBuffer), SCPARAM(MaxLength))

#define Syscall_FutexWait(context, params)                                 (oserr_t)syscall2(38, SCPARAM(context), SCPARAM(params))
#define Syscall_FutexWake(Parameters)                                      (oserr_t)syscall1(39, SCPARAM(Parameters))
#define Syscall_EventCreate(InitialValue, Flags, HandleOut, SyncAddress)   (oserr_t)syscall4(40, SCPARAM(InitialValue), SCPARAM(Flags), SCPARAM(HandleOut), SCPARAM(SyncAddress))
#define Syscall_IPCSend(Messages, MessageCount, Deadline, Context)         (oserr_t)syscall4(41, SCPARAM(Messages), SCPARAM(MessageCount), SCPARAM(Deadline), SCPARAM(Context))

#define Syscall_MemoryAllocate(Hint, Size, Flags, MemoryOut)               (oserr_t)syscall4(42, SCPARAM(Hint), SCPARAM(Size), SCPARAM(Flags), SCPARAM(MemoryOut))
#define Syscall_MemoryFree(Pointer, Size)                                  (oserr_t)syscall2(43, SCPARAM(Pointer), SCPARAM(Size))
#define Syscall_MemoryProtect(MemoryPointer, Length, Flags, PreviousFlags) (oserr_t)syscall4(44, SCPARAM(MemoryPointer), SCPARAM(Length), SCPARAM(Flags), SCPARAM(PreviousFlags))
#define Syscall_MemoryQueryAllocation(MemoryPointer, Descriptor)           (oserr_t)syscall2(45, SCPARAM(MemoryPointer), SCPARAM(Descriptor))
#define Syscall_MemoryQueryAttributes(MemoryPointer, Length, Attributes)   (oserr_t)syscall3(46, SCPARAM(MemoryPointer), SCPARAM(Length), SCPARAM(Attributes))

#define Syscall_SHMCreate(_shm, _handle)                                   (oserr_t)syscall2(47, SCPARAM(_shm), SCPARAM(_handle))
#define Syscall_SHMExport(_buffer, _shm, _handle)                          (oserr_t)syscall3(48, SCPARAM(_buffer), SCPARAM(_shm), SCPARAM(_handle))
#define Syscall_SHMConform(_key, _params, _handle)                         (oserr_t)syscall3(49, SCPARAM(_key), SCPARAM(_params), SCPARAM(_handle))
#define Syscall_SHMAttach(_key, _handle)                                   (oserr_t)syscall2(50, SCPARAM(_key), SCPARAM(_handle))
#define Syscall_SHMMap(_handle, _offset, _length, _flags)                  (oserr_t)syscall4(51, SCPARAM(_handle), SCPARAM(_offset), SCPARAM(_length), SCPARAM(_flags))
#define Syscall_SHMCommit(_handle, _address, _length)                      (oserr_t)syscall3(52, SCPARAM(_handle), SCPARAM(_address), SCPARAM(_length))
#define Syscall_SHMUnmap(_handle, _address, _length)                       (oserr_t)syscall3(53, SCPARAM(_handle), SCPARAM(_address), SCPARAM(_length))
#define Syscall_SHMDetach(_handle)                                         (oserr_t)syscall1(54, SCPARAM(_handle))
#define Syscall_SHMMetrics(_handle, _sizeOut, _vectorsOut)                 (oserr_t)syscall3(55, SCPARAM(_handle), SCPARAM(_sizeOut), SCPARAM(_vectorsOut))

#define Syscall_CreateHandle(HandleOut)                                    (oserr_t)syscall1(56, SCPARAM(HandleOut))
#define Syscall_DestroyHandle(Handle)                                      (oserr_t)syscall1(57, SCPARAM(Handle))
#define Syscall_LookupHandle(Path, HandleOut)                              (oserr_t)syscall2(58, SCPARAM(Path), SCPARAM(HandleOut))
#define Syscall_HandleSetActivity(Handle, Flags)                           (oserr_t)syscall2(59, SCPARAM(Handle), SCPARAM(Flags))

#define Syscall_CreateHandleSet(Flags, HandleOut)                          (oserr_t)syscall2(60, SCPARAM(Flags), SCPARAM(HandleOut))
#define Syscall_ControlHandleSet(SetHandle, Operation, Handle, Event)      (oserr_t)syscall4(61, SCPARAM(SetHandle), SCPARAM(Operation), SCPARAM(Handle), SCPARAM(Event))
#define Syscall_ListenHandleSet(handle, context, params, eventsOut)        (oserr_t)syscall4(62, SCPARAM(handle), SCPARAM(context), SCPARAM(params), SCPARAM(eventsOut))

#define Syscall_InstallSignalHandler(HandlerAddress)                       (oserr_t)syscall1(63, SCPARAM(HandlerAddress))
#define Syscall_FlushHardwareCache(CacheType, AddressStart, Length)        (oserr_t)syscall3(64, SCPARAM(CacheType), SCPARAM(AddressStart), SCPARAM(Length))
#define Syscall_SystemQuery(_req, _buffer, _size, _sizeOut)                (oserr_t)syscall4(65, SCPARAM(_req), SCPARAM(_buffer), SCPARAM(_size), SCPARAM(_sizeOut))
#define Syscall_ClockTick(Source, TickOut)                                 (oserr_t)syscall2(66, SCPARAM(Source), SCPARAM(TickOut))
#define Syscall_ClockFrequency(Source, FrequencyOut)                       (oserr_t)syscall2(67, SCPARAM(Source), SCPARAM(FrequencyOut))
#define Syscall_Time(source, timeOut)                                      (oserr_t)syscall2(68, SCPARAM(source), SCPARAM(timeOut))
#define Syscall_Sleep(DurationNs, RemainingNs)                             (oserr_t)syscall2(69, SCPARAM(DurationNs), SCPARAM(RemainingNs))
#define Syscall_Stall(DurationNs)                                          (oserr_t)syscall1(70, SCPARAM(DurationNs))

#define Syscall_DmaOpen(Grant, Limit, ContextOut)                          (oserr_t)syscall3(71, SCPARAM(Grant), SCPARAM(Limit), SCPARAM(ContextOut))
#define Syscall_DmaAllocate(Context, Requirements, ResultOut)              (oserr_t)syscall3(72, SCPARAM(Context), SCPARAM(Requirements), SCPARAM(ResultOut))
#define Syscall_DmaMapShm(Context, Shm, Parameters, ResultOut)             (oserr_t)syscall4(73, SCPARAM(Context), SCPARAM(Shm), SCPARAM(Parameters), SCPARAM(ResultOut))
#define Syscall_DmaGetSegments(Mapping, Entries, Capacity, CountOut)       (oserr_t)syscall4(74, SCPARAM(Mapping), SCPARAM(Entries), SCPARAM(Capacity), SCPARAM(CountOut))
#define Syscall_DmaSyncForDevice(Mapping, Offset, Length)                  (oserr_t)syscall3(75, SCPARAM(Mapping), SCPARAM(Offset), SCPARAM(Length))
#define Syscall_DmaSyncForCpu(Mapping, Offset, Length)                     (oserr_t)syscall3(76, SCPARAM(Mapping), SCPARAM(Offset), SCPARAM(Length))
#define Syscall_DmaUnmap(Mapping)                                          (oserr_t)syscall1(77, SCPARAM(Mapping))
#define Syscall_DmaClose(Context)                                          (oserr_t)syscall1(78, SCPARAM(Context))

#endif //!__INTERNAL_CRT_SYSCALLS__
