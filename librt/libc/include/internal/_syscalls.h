#ifndef __INTERNAL_CRT_SYSCALLS__
#define __INTERNAL_CRT_SYSCALLS__

#include <os/osdefs.h>

#if defined(i386) || defined(__i386__)
#define SCTYPE int
#elif defined(amd64) || defined(__amd64__)
#define SCTYPE long long
#endif
#define SCPARAM(Arg) ((SCTYPE)Arg)
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
#define Syscall_Debug(Type, Message)                                                         (OsStatus_t)syscall2(0, SCPARAM(Type), SCPARAM(Message))
#define Syscall_SystemStart()                                                                (OsStatus_t)syscall0(1)
#define Syscall_DisplayInformation(Descriptor)                                               (OsStatus_t)syscall1(2, SCPARAM(Descriptor))
#define Syscall_MapBootFramebuffer(BufferOut)                                                (OsStatus_t)syscall1(3, SCPARAM(BufferOut))
#define Syscall_MapRamdisk(BufferOut, LengthOut)                                             (OsStatus_t)syscall2(4, SCPARAM(BufferOut), SCPARAM(LengthOut))

#define Syscall_CreateMemorySpace(Flags, HandleOut)                                          (OsStatus_t)syscall2(5, SCPARAM(Flags), SCPARAM(HandleOut))
#define Syscall_GetMemorySpaceForThread(ThreadHandle, HandleOut)                             (OsStatus_t)syscall2(6, SCPARAM(ThreadHandle), SCPARAM(HandleOut))
#define Syscall_CreateMemorySpaceMapping(Handle, Parameters, AddressOut)                     (OsStatus_t)syscall3(7, SCPARAM(Handle), SCPARAM(Parameters), SCPARAM(AddressOut))

#define Syscall_AcpiQuery(Descriptor)                                                        (OsStatus_t)syscall1(8, SCPARAM(Descriptor))
#define Syscall_AcpiGetHeader(Signature, Header)                                             (OsStatus_t)syscall2(9, SCPARAM(Signature), SCPARAM(Header))
#define Syscall_AcpiGetTable(Signature, Table)                                               (OsStatus_t)syscall2(10, SCPARAM(Signature), SCPARAM(Table))
#define Syscall_AcpiQueryInterrupt(Bus, Slot, Pin, Interrupt, Conform)                       (OsStatus_t)syscall5(11, SCPARAM(Bus), SCPARAM(Slot), SCPARAM(Pin), SCPARAM(Interrupt), SCPARAM(Conform))
#define Syscall_IoSpaceRegister(IoSpace)                                                     (OsStatus_t)syscall1(12, SCPARAM(IoSpace))
#define Syscall_IoSpaceAcquire(IoSpace)                                                      (OsStatus_t)syscall1(13, SCPARAM(IoSpace))
#define Syscall_IoSpaceRelease(IoSpace)                                                      (OsStatus_t)syscall1(14, SCPARAM(IoSpace))
#define Syscall_IoSpaceDestroy(IoSpaceId)                                                    (OsStatus_t)syscall1(15, SCPARAM(IoSpaceId))
#define Syscall_InterruptAdd(Descriptor, Flags)                                              (UUId_t)syscall2(16, SCPARAM(Descriptor), SCPARAM(Flags))
#define Syscall_InterruptRemove(InterruptId)                                                 (OsStatus_t)syscall1(17, SCPARAM(InterruptId))
#define Syscall_GetProcessBaseAddress(BaseAddressOut)                                        (OsStatus_t)syscall1(18, SCPARAM(BaseAddressOut))

#define Syscall_MapThreadMemoryRegion(ThreadHandle, Address, TopOfStack, PointerOut)         (OsStatus_t)syscall4(19, SCPARAM(ThreadHandle), SCPARAM(Address), SCPARAM(TopOfStack), SCPARAM(PointerOut))

///////////////////////////////////////////////
//Operating System (Process) Interface
#define Syscall_ThreadCreate(Entry, Argument, Parameters, HandleOut)       (OsStatus_t)syscall4(20, SCPARAM(Entry), SCPARAM(Argument), SCPARAM(Parameters), SCPARAM(HandleOut))
#define Syscall_ThreadExit(ExitCode)                                       (OsStatus_t)syscall1(21, SCPARAM(ExitCode))
#define Syscall_ThreadSignal(ThreadId, Signal)                             (OsStatus_t)syscall2(22, SCPARAM(ThreadId), SCPARAM(Signal))
#define Syscall_ThreadJoin(ThreadId, ExitCode)                             (OsStatus_t)syscall2(23, SCPARAM(ThreadId), SCPARAM(ExitCode))
#define Syscall_ThreadDetach(ThreadId)                                     (OsStatus_t)syscall1(24, SCPARAM(ThreadId))
#define Syscall_ThreadSleep(Milliseconds, MillisecondsSlept)               (OsStatus_t)syscall2(25, SCPARAM(Milliseconds), SCPARAM(MillisecondsSlept))
#define Syscall_ThreadYield()                                              (OsStatus_t)syscall0(26)
#define Syscall_ThreadId()                                                 (UUId_t)syscall0(27)
#define Syscall_ThreadCookie()                                             (UUId_t)syscall0(28)
#define Syscall_ThreadSetCurrentName(Name)                                 (UUId_t)syscall1(29, SCPARAM(Name))
#define Syscall_ThreadGetCurrentName(NameBuffer, MaxLength)                (UUId_t)syscall2(30, SCPARAM(NameBuffer), SCPARAM(MaxLength))

#define Syscall_FutexWait(Parameters)                                      (OsStatus_t)syscall1(31, SCPARAM(Parameters))
#define Syscall_FutexWake(Parameters)                                      (OsStatus_t)syscall1(32, SCPARAM(Parameters))
#define Syscall_EventCreate(InitialValue, Flags, HandleOut, SyncAddress)   (OsStatus_t)syscall4(33, SCPARAM(InitialValue), SCPARAM(Flags), SCPARAM(HandleOut), SCPARAM(SyncAddress))

#define Syscall_IpcContextCreate(Size, HandleOut, UserContextOut)          (OsStatus_t)syscall3(34, SCPARAM(Size), SCPARAM(HandleOut), SCPARAM(UserContextOut))
#define Syscall_IpcContextSend(Messages, MessageCount, Timeout)            (OsStatus_t)syscall3(35, SCPARAM(Messages), SCPARAM(MessageCount), SCPARAM(Timeout))

#define Syscall_MemoryAllocate(Hint, Size, Flags, MemoryOut)               (OsStatus_t)syscall4(36, SCPARAM(Hint), SCPARAM(Size), SCPARAM(Flags), SCPARAM(MemoryOut))
#define Syscall_MemoryFree(Pointer, Size)                                  (OsStatus_t)syscall2(37, SCPARAM(Pointer), SCPARAM(Size))
#define Syscall_MemoryProtect(MemoryPointer, Length, Flags, PreviousFlags) (OsStatus_t)syscall4(38, SCPARAM(MemoryPointer), SCPARAM(Length), SCPARAM(Flags), SCPARAM(PreviousFlags))
#define Syscall_MemoryQueryAllocation(MemoryPointer, Descriptor)           (OsStatus_t)syscall2(39, SCPARAM(MemoryPointer), SCPARAM(Descriptor))
#define Syscall_MemoryQueryAttributes(MemoryPointer, Length, Attributes)   (OsStatus_t)syscall3(40, SCPARAM(MemoryPointer), SCPARAM(Length), SCPARAM(Attributes))

#define Syscall_DmaCreate(CreateInfo, Attachment)                          (OsStatus_t)syscall2(41, SCPARAM(CreateInfo), SCPARAM(Attachment))
#define Syscall_DmaExport(Buffer, ExportInfo, Attachment)                  (OsStatus_t)syscall3(42, SCPARAM(Buffer), SCPARAM(ExportInfo), SCPARAM(Attachment))
#define Syscall_DmaAttach(Handle, Attachment)                              (OsStatus_t)syscall2(43, SCPARAM(Handle), SCPARAM(Attachment))
#define Syscall_DmaAttachmentMap(Attachment, AccessFlags)                  (OsStatus_t)syscall2(44, SCPARAM(Attachment), SCPARAM(AccessFlags))
#define Syscall_DmaAttachmentResize(Attachment, Length)                    (OsStatus_t)syscall2(45, SCPARAM(Attachment), SCPARAM(Length))
#define Syscall_DmaAttachmentRefresh(Attachment)                           (OsStatus_t)syscall1(46, SCPARAM(Attachment))
#define Syscall_DmaAttachmentCommit(Attachment, Address, Length)           (OsStatus_t)syscall3(47, SCPARAM(Attachment), SCPARAM(Address), SCPARAM(Length))
#define Syscall_DmaAttachmentUnmap(Attachment)                             (OsStatus_t)syscall1(48, SCPARAM(Attachment))
#define Syscall_DmaDetach(Attachment)                                      (OsStatus_t)syscall1(49, SCPARAM(Attachment))
#define Syscall_DmaGetMetrics(Handle, SizeOut, VectorsOut)                 (OsStatus_t)syscall3(50, SCPARAM(Handle), SCPARAM(SizeOut), SCPARAM(VectorsOut))

#define Syscall_CreateHandle(HandleOut)                                    (OsStatus_t)syscall1(51, SCPARAM(HandleOut))
#define Syscall_DestroyHandle(Handle)                                      (OsStatus_t)syscall1(52, SCPARAM(Handle))
#define Syscall_RegisterHandlePath(Handle, Path)                           (OsStatus_t)syscall2(53, SCPARAM(Handle), SCPARAM(Path))
#define Syscall_LookupHandle(Path, HandleOut)                              (OsStatus_t)syscall2(54, SCPARAM(Path), SCPARAM(HandleOut))
#define Syscall_HandleSetActivity(Handle, Flags)                           (OsStatus_t)syscall2(55, SCPARAM(Handle), SCPARAM(Flags))

#define Syscall_CreateHandleSet(Flags, HandleOut)                          (OsStatus_t)syscall2(56, SCPARAM(Flags), SCPARAM(HandleOut))
#define Syscall_ControlHandleSet(SetHandle, Operation, Handle, Event)      (OsStatus_t)syscall4(57, SCPARAM(SetHandle), SCPARAM(Operation), SCPARAM(Handle), SCPARAM(Event))
#define Syscall_ListenHandleSet(Handle, WaitContext, EventsOut)            (OsStatus_t)syscall3(58, SCPARAM(Handle), SCPARAM(WaitContext), SCPARAM(EventsOut))

#define Syscall_InstallSignalHandler(HandlerAddress)                       (OsStatus_t)syscall1(59, SCPARAM(HandlerAddress))
#define Syscall_FlushHardwareCache(CacheType, AddressStart, Length)        (OsStatus_t)syscall3(60, SCPARAM(CacheType), SCPARAM(AddressStart), SCPARAM(Length))
#define Syscall_SystemQuery(SystemInformation)                             (OsStatus_t)syscall1(61, SCPARAM(SystemInformation))
#define Syscall_SystemTick(Base, Tick)                                     (OsStatus_t)syscall2(62, SCPARAM(Base), SCPARAM(Tick))
#define Syscall_SystemPerformanceFrequency(Frequency)                      (OsStatus_t)syscall1(63, SCPARAM(Frequency))
#define Syscall_SystemPerformanceTime(Value)                               (OsStatus_t)syscall1(64, SCPARAM(Value))
#define Syscall_SystemTime(Time)                                           (OsStatus_t)syscall1(65, SCPARAM(Time))

#endif //!__INTERNAL_CRT_SYSCALLS__
