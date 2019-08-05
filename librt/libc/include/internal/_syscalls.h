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
#define Syscall_Debug(Type, Module, Message) (OsStatus_t)syscall3(0, SCPARAM(Type), SCPARAM(Module), SCPARAM(Message))
#define Syscall_SystemStart() (OsStatus_t)syscall0(1)
#define Syscall_DisplayInformation(Descriptor) (OsStatus_t)syscall1(2, SCPARAM(Descriptor))
#define Syscall_CreateDisplayFramebuffer() (void*)syscall0(3)

#define Syscall_ModuleGetStartupInfo(InheritanceBlock, InheritanceBlockLength, ArgumentBlock, ArgumentBlockLength) (OsStatus_t)syscall4(4, SCPARAM(InheritanceBlock), SCPARAM(InheritanceBlockLength), SCPARAM(ArgumentBlock), SCPARAM(ArgumentBlockLength))
#define Syscall_ModuleId(HandleOut) (OsStatus_t)syscall1(5, SCPARAM(HandleOut))
#define Syscall_ModuleName(Buffer, MaxLength) (OsStatus_t)syscall2(6, SCPARAM(Buffer), SCPARAM(MaxLength))
#define Syscall_ModuleGetModuleHandles(HandleList) (OsStatus_t)syscall1(7, SCPARAM(HandleList))
#define Syscall_ModuleGetModuleEntryPoints(HandleList) (OsStatus_t)syscall1(8, SCPARAM(HandleList))
#define Syscall_ModuleExit(ExitCode) (OsStatus_t)syscall1(9, SCPARAM(ExitCode))

#define Syscall_LibraryLoad(Path, HandleOut) (OsStatus_t)syscall2(10, SCPARAM(Path), SCPARAM(HandleOut))
#define Syscall_LibraryFunction(Handle, FunctionName) (uintptr_t)syscall2(11, SCPARAM(Handle), SCPARAM(FunctionName))
#define Syscall_LibraryUnload(Handle) (OsStatus_t)syscall1(12, SCPARAM(Handle))

#define Syscall_GetWorkingDirectory(Buffer, MaxLength) (OsStatus_t)syscall2(13, SCPARAM(Buffer), SCPARAM(MaxLength))
#define Syscall_SetWorkingDirectory(Path) (OsStatus_t)syscall1(14, SCPARAM(Path))
#define Syscall_GetAssemblyDirectory(Buffer, MaxLength) (OsStatus_t)syscall2(15, SCPARAM(Buffer), SCPARAM(MaxLength))

#define Syscall_CreateMemorySpace(Flags, HandleOut) (OsStatus_t)syscall2(16, SCPARAM(Flags), SCPARAM(HandleOut))
#define Syscall_GetMemorySpaceForThread(ThreadHandle, HandleOut) (OsStatus_t)syscall2(17, SCPARAM(ThreadHandle), SCPARAM(HandleOut))
#define Syscall_CreateMemorySpaceMapping(Handle, Parameters, AddressOut) (OsStatus_t)syscall3(18, SCPARAM(Handle), SCPARAM(Parameters), SCPARAM(AddressOut))

#define Syscall_AcpiQuery(Descriptor) (OsStatus_t)syscall1(19, SCPARAM(Descriptor))
#define Syscall_AcpiGetHeader(Signature, Header) (OsStatus_t)syscall2(20, SCPARAM(Signature), SCPARAM(Header))
#define Syscall_AcpiGetTable(Signature, Table) (OsStatus_t)syscall2(21, SCPARAM(Signature), SCPARAM(Table))
#define Syscall_AcpiQueryInterrupt(Bus, Slot, Pin, Interrupt, Conform) (OsStatus_t)syscall5(22, SCPARAM(Bus), SCPARAM(Slot), SCPARAM(Pin), SCPARAM(Interrupt), SCPARAM(Conform))
#define Syscall_IoSpaceRegister(IoSpace) (OsStatus_t)syscall1(23, SCPARAM(IoSpace))
#define Syscall_IoSpaceAcquire(IoSpace) (OsStatus_t)syscall1(24, SCPARAM(IoSpace))
#define Syscall_IoSpaceRelease(IoSpace) (OsStatus_t)syscall1(25, SCPARAM(IoSpace))
#define Syscall_IoSpaceDestroy(IoSpaceId) (OsStatus_t)syscall1(26, SCPARAM(IoSpaceId))
#define Syscall_RegisterService(Alias) (OsStatus_t)syscall1(27, SCPARAM(Alias))
#define Syscall_LoadDriver(Device, Length, Buffer, BufferLength) (OsStatus_t)syscall4(28, SCPARAM(Device), SCPARAM(Length), SCPARAM(Buffer), SCPARAM(BufferLength))
#define Syscall_InterruptAdd(Descriptor, Flags) (UUId_t)syscall2(29, SCPARAM(Descriptor), SCPARAM(Flags))
#define Syscall_InterruptRemove(InterruptId) (OsStatus_t)syscall1(30, SCPARAM(InterruptId))
#define Syscall_GetProcessBaseAddress(BaseAddressOut) (OsStatus_t)syscall1(31, SCPARAM(BaseAddressOut))

///////////////////////////////////////////////
//Operating System (Process) Interface
#define Syscall_ThreadCreate(Entry, Argument, Parameters, HandleOut)       (OsStatus_t)syscall4(32, SCPARAM(Entry), SCPARAM(Argument), SCPARAM(Parameters), SCPARAM(HandleOut))
#define Syscall_ThreadExit(ExitCode)                                       (OsStatus_t)syscall1(33, SCPARAM(ExitCode))
#define Syscall_ThreadSignal(ThreadId, Signal)                             (OsStatus_t)syscall2(34, SCPARAM(ThreadId), SCPARAM(Signal))
#define Syscall_ThreadJoin(ThreadId, ExitCode)                             (OsStatus_t)syscall2(35, SCPARAM(ThreadId), SCPARAM(ExitCode))
#define Syscall_ThreadDetach(ThreadId)                                     (OsStatus_t)syscall1(36, SCPARAM(ThreadId))
#define Syscall_ThreadSleep(Milliseconds, MillisecondsSlept)               (OsStatus_t)syscall2(37, SCPARAM(Milliseconds), SCPARAM(MillisecondsSlept))
#define Syscall_ThreadYield()                                              (OsStatus_t)syscall0(38)
#define Syscall_ThreadId()                                                 (UUId_t)syscall0(39)
#define Syscall_ThreadCookie()                                             (UUId_t)syscall0(40)
#define Syscall_ThreadSetCurrentName(Name)                                 (UUId_t)syscall1(41, SCPARAM(Name))
#define Syscall_ThreadGetCurrentName(NameBuffer, MaxLength)                (UUId_t)syscall2(42, SCPARAM(NameBuffer), SCPARAM(MaxLength))
#define Syscall_ThreadGetContext(Context)                                  (OsStatus_t)syscall1(43, SCPARAM(ContextOut))

#define Syscall_FutexWait(Parameters)                                      (OsStatus_t)syscall1(44, SCPARAM(Parameters))
#define Syscall_FutexWake(Parameters)                                      (OsStatus_t)syscall1(45, SCPARAM(Parameters))

#define Syscall_IpcInvoke(Target, Message, Flags, Timeout)                 (OsStatus_t)syscall4(46, SCPARAM(Target), SCPARAM(Message), SCPARAM(Flags), SCPARAM(Timeout))
#define Syscall_IpcGetResponse(Timeout, BufferOut)                         (OsStatus_t)syscall2(47, SCPARAM(Timeout), SCPARAM(Buffer))
#define Syscall_IpcReply(Buffer, Length)                                   (OsStatus_t)syscall2(48, SCPARAM(Buffer), SCPARAM(Length))
#define Syscall_IpcListen(Timeout, MessageOut)                             (OsStatus_t)syscall2(49, SCPARAM(Timeout), SCPARAM(MessageOut))
#define Syscall_IpcReplyAndListen(Buffer, Length, Timeout, MessageOut)     (OsStatus_t)syscall4(50, SCPARAM(Buffer), SCPARAM(Length), SCPARAM(Timeout), SCPARAM(MessageOut))

#define Syscall_CreateSocket(Flags, HandleOut, RecieveQueueOut)            (OsStatus_t)syscall3(00, SCPARAM(Flags), SCPARAM(HandleOut), SCPARAM(RecieveQueueOut))
#define Syscall_InheritSocket(Handle, RecieveQueueOut)                     (OsStatus_t)syscall2(00, SCPARAM(Handle), SCPARAM(RecieveQueueOut))
#define Syscall_BindSocket(Handle, Address)                                (OsStatus_t)syscall2(00, SCPARAM(Handle), SCPARAM(Address))
#define Syscall_WriteSocket(Address, Buffer, Length, BytesWrittenOut)      (OsStatus_t)syscall4(00, SCPARAM(Address), SCPARAM(Buffer), SCPARAM(Length), SCPARAM(BytesWrittenOut))
#define Syscall_GetSocketAddress(Handle, AddressOut, AddressLengthOut)     (OsStatus_t)syscall3(00, SCPARAM(Handle), SCPARAM(AddressOut), SCPARAM(AddressLengthOut))

#define Syscall_MemoryAllocate(Hint, Size, Flags, MemoryOut)               (OsStatus_t)syscall4(51, SCPARAM(Hint), SCPARAM(Size), SCPARAM(Flags), SCPARAM(MemoryOut))
#define Syscall_MemoryFree(Pointer, Size)                                  (OsStatus_t)syscall2(52, SCPARAM(Pointer), SCPARAM(Size))
#define Syscall_MemoryProtect(MemoryPointer, Length, Flags, PreviousFlags) (OsStatus_t)syscall4(53, SCPARAM(MemoryPointer), SCPARAM(Length), SCPARAM(Flags), SCPARAM(PreviousFlags))

#define Syscall_DmaCreate(CreateInfo, Attachment)                          (OsStatus_t)syscall2(54, SCPARAM(CreateInfo), SCPARAM(Attachment))
#define Syscall_DmaExport(Buffer, ExportInfo, Attachment)                  (OsStatus_t)syscall3(55, SCPARAM(Buffer), SCPARAM(ExportInfo), SCPARAM(Attachment))
#define Syscall_DmaAttach(Handle, Attachment)                              (OsStatus_t)syscall2(56, SCPARAM(Handle), SCPARAM(Attachment))
#define Syscall_DmaAttachmentMap(Attachment)                               (OsStatus_t)syscall1(57, SCPARAM(Attachment))
#define Syscall_DmaAttachmentResize(Attachment, Length)                    (OsStatus_t)syscall2(58, SCPARAM(Attachment), SCPARAM(Length))
#define Syscall_DmaAttachmentRefresh(Attachment)                           (OsStatus_t)syscall1(59, SCPARAM(Attachment))
#define Syscall_DmaAttachmentUnmap(Attachment)                             (OsStatus_t)syscall1(60, SCPARAM(Attachment))
#define Syscall_DmaDetach(Attachment)                                      (OsStatus_t)syscall1(61, SCPARAM(Attachment))
#define Syscall_DmaGetMetrics(Attachment, SizeOut, VectorsOut)             (OsStatus_t)syscall3(62, SCPARAM(Attachment), SCPARAM(SizeOut), SCPARAM(VectorsOut))

#define Syscall_DestroyHandle(Handle)                                      (OsStatus_t)syscall1(63, SCPARAM(Handle))
#define Syscall_InstallSignalHandler(HandlerAddress)                       (OsStatus_t)syscall1(64, SCPARAM(HandlerAddress))
#define Syscall_GetSignalOriginalContext(Context)                          (OsStatus_t)syscall1(65, SCPARAM(Context))
#define Syscall_CreateMemoryHandler(Flags, Length, HandleOut, AddressOut)  (OsStatus_t)syscall4(66, SCPARAM(Flags), SCPARAM(Length), SCPARAM(HandleOut), SCPARAM(AddressOut))
#define Syscall_DestroyMemoryHandler(Handle)                               (OsStatus_t)syscall1(67, SCPARAM(Handle))
#define Syscall_FlushHardwareCache(CacheType, AddressStart, Length)        (OsStatus_t)syscall3(68, SCPARAM(CacheType), SCPARAM(AddressStart), SCPARAM(Length))
#define Syscall_SystemQuery(SystemInformation)                             (OsStatus_t)syscall1(69, SCPARAM(SystemInformation))
#define Syscall_SystemTick(Base, Tick)                                     (OsStatus_t)syscall2(70, SCPARAM(Base), SCPARAM(Tick))
#define Syscall_SystemPerformanceFrequency(Frequency)                      (OsStatus_t)syscall1(71, SCPARAM(Frequency))
#define Syscall_SystemPerformanceTime(Value)                               (OsStatus_t)syscall1(72, SCPARAM(Value))
#define Syscall_SystemTime(Time)                                           (OsStatus_t)syscall1(73, SCPARAM(Time))
#define Syscall_IsServiceAvailable(ServiceId)                              (OsStatus_t)syscall1(74, SCPARAM(ServiceId))

#endif //!__INTERNAL_CRT_SYSCALLS__
