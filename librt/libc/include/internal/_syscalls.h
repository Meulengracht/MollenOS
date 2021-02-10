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
#define Syscall_CreateDisplayFramebuffer()                                                   (void*)syscall0(3)

#define Syscall_ModuleGetStartupInfo(StartupInformation, ProcessIdOut, Buffer, BufferLength) (OsStatus_t)syscall4(4, SCPARAM(StartupInformation), SCPARAM(ProcessIdOut), SCPARAM(Buffer), SCPARAM(BufferLength))
#define Syscall_ModuleName(Buffer, MaxLength)                                                (OsStatus_t)syscall2(5, SCPARAM(Buffer), SCPARAM(MaxLength))
#define Syscall_ModuleExit(ExitCode)                                                         (OsStatus_t)syscall1(6, SCPARAM(ExitCode))

#define Syscall_LibraryLoad(Path, HandleOut)                                                 (OsStatus_t)syscall2(7, SCPARAM(Path), SCPARAM(HandleOut))
#define Syscall_LibraryFunction(Handle, FunctionName)                                        (uintptr_t)syscall2(8, SCPARAM(Handle), SCPARAM(FunctionName))
#define Syscall_LibraryUnload(Handle)                                                        (OsStatus_t)syscall1(9, SCPARAM(Handle))

#define Syscall_GetWorkingDirectory(Buffer, MaxLength)                                       (OsStatus_t)syscall2(10, SCPARAM(Buffer), SCPARAM(MaxLength))
#define Syscall_SetWorkingDirectory(Path)                                                    (OsStatus_t)syscall1(11, SCPARAM(Path))
#define Syscall_GetAssemblyDirectory(Buffer, MaxLength)                                      (OsStatus_t)syscall2(12, SCPARAM(Buffer), SCPARAM(MaxLength))

#define Syscall_CreateMemorySpace(Flags, HandleOut)                                          (OsStatus_t)syscall2(13, SCPARAM(Flags), SCPARAM(HandleOut))
#define Syscall_GetMemorySpaceForThread(ThreadHandle, HandleOut)                             (OsStatus_t)syscall2(14, SCPARAM(ThreadHandle), SCPARAM(HandleOut))
#define Syscall_CreateMemorySpaceMapping(Handle, Parameters, AddressOut)                     (OsStatus_t)syscall3(15, SCPARAM(Handle), SCPARAM(Parameters), SCPARAM(AddressOut))

#define Syscall_AcpiQuery(Descriptor)                                                        (OsStatus_t)syscall1(16, SCPARAM(Descriptor))
#define Syscall_AcpiGetHeader(Signature, Header)                                             (OsStatus_t)syscall2(17, SCPARAM(Signature), SCPARAM(Header))
#define Syscall_AcpiGetTable(Signature, Table)                                               (OsStatus_t)syscall2(18, SCPARAM(Signature), SCPARAM(Table))
#define Syscall_AcpiQueryInterrupt(Bus, Slot, Pin, Interrupt, Conform)                       (OsStatus_t)syscall5(19, SCPARAM(Bus), SCPARAM(Slot), SCPARAM(Pin), SCPARAM(Interrupt), SCPARAM(Conform))
#define Syscall_IoSpaceRegister(IoSpace)                                                     (OsStatus_t)syscall1(20, SCPARAM(IoSpace))
#define Syscall_IoSpaceAcquire(IoSpace)                                                      (OsStatus_t)syscall1(21, SCPARAM(IoSpace))
#define Syscall_IoSpaceRelease(IoSpace)                                                      (OsStatus_t)syscall1(22, SCPARAM(IoSpace))
#define Syscall_IoSpaceDestroy(IoSpaceId)                                                    (OsStatus_t)syscall1(23, SCPARAM(IoSpaceId))
#define Syscall_LoadDriver(Device, Buffer, BufferLength)                                     (OsStatus_t)syscall3(24, SCPARAM(Device), SCPARAM(Buffer), SCPARAM(BufferLength))
#define Syscall_InterruptAdd(Descriptor, Flags)                                              (UUId_t)syscall2(25, SCPARAM(Descriptor), SCPARAM(Flags))
#define Syscall_InterruptRemove(InterruptId)                                                 (OsStatus_t)syscall1(26, SCPARAM(InterruptId))
#define Syscall_GetProcessBaseAddress(BaseAddressOut)                                        (OsStatus_t)syscall1(27, SCPARAM(BaseAddressOut))

#define Syscall_MapThreadMemoryRegion(ThreadHandle, Address, Length, PointerOut)             (OsStatus_t)syscall4(28, SCPARAM(ThreadHandle), SCPARAM(Address), SCPARAM(Length), SCPARAM(PointerOut))

///////////////////////////////////////////////
//Operating System (Process) Interface
#define Syscall_ThreadCreate(Entry, Argument, Parameters, HandleOut)       (OsStatus_t)syscall4(29, SCPARAM(Entry), SCPARAM(Argument), SCPARAM(Parameters), SCPARAM(HandleOut))
#define Syscall_ThreadExit(ExitCode)                                       (OsStatus_t)syscall1(30, SCPARAM(ExitCode))
#define Syscall_ThreadSignal(ThreadId, Signal)                             (OsStatus_t)syscall2(31, SCPARAM(ThreadId), SCPARAM(Signal))
#define Syscall_ThreadJoin(ThreadId, ExitCode)                             (OsStatus_t)syscall2(32, SCPARAM(ThreadId), SCPARAM(ExitCode))
#define Syscall_ThreadDetach(ThreadId)                                     (OsStatus_t)syscall1(33, SCPARAM(ThreadId))
#define Syscall_ThreadSleep(Milliseconds, MillisecondsSlept)               (OsStatus_t)syscall2(34, SCPARAM(Milliseconds), SCPARAM(MillisecondsSlept))
#define Syscall_ThreadYield()                                              (OsStatus_t)syscall0(35)
#define Syscall_ThreadId()                                                 (UUId_t)syscall0(36)
#define Syscall_ThreadCookie()                                             (UUId_t)syscall0(37)
#define Syscall_ThreadSetCurrentName(Name)                                 (UUId_t)syscall1(38, SCPARAM(Name))
#define Syscall_ThreadGetCurrentName(NameBuffer, MaxLength)                (UUId_t)syscall2(39, SCPARAM(NameBuffer), SCPARAM(MaxLength))

#define Syscall_FutexWait(Parameters)                                      (OsStatus_t)syscall1(40, SCPARAM(Parameters))
#define Syscall_FutexWake(Parameters)                                      (OsStatus_t)syscall1(41, SCPARAM(Parameters))
#define Syscall_EventCreate(InitialValue, Flags, HandleOut, SyncAddress)   (OsStatus_t)syscall4(42, SCPARAM(InitialValue), SCPARAM(Flags), SCPARAM(HandleOut), SCPARAM(SyncAddress))

#define Syscall_IpcContextCreate(Size, HandleOut, UserContextOut)          (OsStatus_t)syscall3(43, SCPARAM(Size), SCPARAM(HandleOut), SCPARAM(UserContextOut))
#define Syscall_IpcContextSend(Messages, MessageCount, Timeout)            (OsStatus_t)syscall3(44, SCPARAM(Messages), SCPARAM(MessageCount), SCPARAM(Timeout))
#define Syscall_IpcContextRespond(Replies, ReplyMessages, Count)           (OsStatus_t)syscall3(45, SCPARAM(Replies), SCPARAM(ReplyMessages), SCPARAM(Count))

#define Syscall_MemoryAllocate(Hint, Size, Flags, MemoryOut)               (OsStatus_t)syscall4(46, SCPARAM(Hint), SCPARAM(Size), SCPARAM(Flags), SCPARAM(MemoryOut))
#define Syscall_MemoryFree(Pointer, Size)                                  (OsStatus_t)syscall2(47, SCPARAM(Pointer), SCPARAM(Size))
#define Syscall_MemoryProtect(MemoryPointer, Length, Flags, PreviousFlags) (OsStatus_t)syscall4(48, SCPARAM(MemoryPointer), SCPARAM(Length), SCPARAM(Flags), SCPARAM(PreviousFlags))
#define Syscall_MemoryQuery(MemoryPointer, Descriptor)                     (OsStatus_t)syscall2(49, SCPARAM(MemoryPointer), SCPARAM(Descriptor))

#define Syscall_DmaCreate(CreateInfo, Attachment)                          (OsStatus_t)syscall2(50, SCPARAM(CreateInfo), SCPARAM(Attachment))
#define Syscall_DmaExport(Buffer, ExportInfo, Attachment)                  (OsStatus_t)syscall3(51, SCPARAM(Buffer), SCPARAM(ExportInfo), SCPARAM(Attachment))
#define Syscall_DmaAttach(Handle, Attachment)                              (OsStatus_t)syscall2(52, SCPARAM(Handle), SCPARAM(Attachment))
#define Syscall_DmaAttachmentMap(Attachment, AccessFlags)                  (OsStatus_t)syscall2(53, SCPARAM(Attachment), SCPARAM(AccessFlags))
#define Syscall_DmaAttachmentResize(Attachment, Length)                    (OsStatus_t)syscall2(54, SCPARAM(Attachment), SCPARAM(Length))
#define Syscall_DmaAttachmentRefresh(Attachment)                           (OsStatus_t)syscall1(55, SCPARAM(Attachment))
#define Syscall_DmaAttachmentUnmap(Attachment)                             (OsStatus_t)syscall1(56, SCPARAM(Attachment))
#define Syscall_DmaDetach(Attachment)                                      (OsStatus_t)syscall1(57, SCPARAM(Attachment))
#define Syscall_DmaGetMetrics(Handle, SizeOut, VectorsOut)                 (OsStatus_t)syscall3(58, SCPARAM(Handle), SCPARAM(SizeOut), SCPARAM(VectorsOut))

#define Syscall_CreateHandle(HandleOut)                                    (OsStatus_t)syscall1(59, SCPARAM(HandleOut))
#define Syscall_DestroyHandle(Handle)                                      (OsStatus_t)syscall1(60, SCPARAM(Handle))
#define Syscall_RegisterHandlePath(Handle, Path)                           (OsStatus_t)syscall2(61, SCPARAM(Handle), SCPARAM(Path))
#define Syscall_LookupHandle(Path, HandleOut)                              (OsStatus_t)syscall2(62, SCPARAM(Path), SCPARAM(HandleOut))
#define Syscall_HandleSetActivity(Handle, Flags)                           (OsStatus_t)syscall2(63, SCPARAM(Handle), SCPARAM(Flags))

#define Syscall_CreateHandleSet(Flags, HandleOut)                          (OsStatus_t)syscall2(64, SCPARAM(Flags), SCPARAM(HandleOut))
#define Syscall_ControlHandleSet(SetHandle, Operation, Handle, Event)      (OsStatus_t)syscall4(65, SCPARAM(SetHandle), SCPARAM(Operation), SCPARAM(Handle), SCPARAM(Event))
#define Syscall_ListenHandleSet(Handle, WaitContext, EventsOut)            (OsStatus_t)syscall3(66, SCPARAM(Handle), SCPARAM(WaitContext), SCPARAM(EventsOut))

#define Syscall_InstallSignalHandler(HandlerAddress)                       (OsStatus_t)syscall1(67, SCPARAM(HandlerAddress))
#define Syscall_CreateMemoryHandler(Flags, Length, HandleOut, AddressOut)  (OsStatus_t)syscall4(68, SCPARAM(Flags), SCPARAM(Length), SCPARAM(HandleOut), SCPARAM(AddressOut))
#define Syscall_FlushHardwareCache(CacheType, AddressStart, Length)        (OsStatus_t)syscall3(69, SCPARAM(CacheType), SCPARAM(AddressStart), SCPARAM(Length))
#define Syscall_SystemQuery(SystemInformation)                             (OsStatus_t)syscall1(70, SCPARAM(SystemInformation))
#define Syscall_SystemTick(Base, Tick)                                     (OsStatus_t)syscall2(71, SCPARAM(Base), SCPARAM(Tick))
#define Syscall_SystemPerformanceFrequency(Frequency)                      (OsStatus_t)syscall1(72, SCPARAM(Frequency))
#define Syscall_SystemPerformanceTime(Value)                               (OsStatus_t)syscall1(73, SCPARAM(Value))
#define Syscall_SystemTime(Time)                                           (OsStatus_t)syscall1(74, SCPARAM(Time))

#endif //!__INTERNAL_CRT_SYSCALLS__
