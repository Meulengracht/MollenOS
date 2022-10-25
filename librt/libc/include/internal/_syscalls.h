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

#define Syscall_FutexWait(Parameters)                                      (oserr_t)syscall1(30, SCPARAM(Parameters))
#define Syscall_FutexWake(Parameters)                                      (oserr_t)syscall1(31, SCPARAM(Parameters))
#define Syscall_EventCreate(InitialValue, Flags, HandleOut, SyncAddress)   (oserr_t)syscall4(32, SCPARAM(InitialValue), SCPARAM(Flags), SCPARAM(HandleOut), SCPARAM(SyncAddress))

#define Syscall_IpcContextCreate(Size, HandleOut, UserContextOut)          (oserr_t)syscall3(33, SCPARAM(Size), SCPARAM(HandleOut), SCPARAM(UserContextOut))
#define Syscall_IpcContextSend(Messages, MessageCount, Timeout)            (oserr_t)syscall3(34, SCPARAM(Messages), SCPARAM(MessageCount), SCPARAM(Timeout))

#define Syscall_MemoryAllocate(Hint, Size, Flags, MemoryOut)               (oserr_t)syscall4(35, SCPARAM(Hint), SCPARAM(Size), SCPARAM(Flags), SCPARAM(MemoryOut))
#define Syscall_MemoryFree(Pointer, Size)                                  (oserr_t)syscall2(36, SCPARAM(Pointer), SCPARAM(Size))
#define Syscall_MemoryProtect(MemoryPointer, Length, Flags, PreviousFlags) (oserr_t)syscall4(37, SCPARAM(MemoryPointer), SCPARAM(Length), SCPARAM(Flags), SCPARAM(PreviousFlags))
#define Syscall_MemoryQueryAllocation(MemoryPointer, Descriptor)           (oserr_t)syscall2(38, SCPARAM(MemoryPointer), SCPARAM(Descriptor))
#define Syscall_MemoryQueryAttributes(MemoryPointer, Length, Attributes)   (oserr_t)syscall3(39, SCPARAM(MemoryPointer), SCPARAM(Length), SCPARAM(Attributes))

#define Syscall_DmaCreate(CreateInfo, Attachment)                          (oserr_t)syscall2(40, SCPARAM(CreateInfo), SCPARAM(Attachment))
#define Syscall_DmaExport(Buffer, ExportInfo, Attachment)                  (oserr_t)syscall3(41, SCPARAM(Buffer), SCPARAM(ExportInfo), SCPARAM(Attachment))
#define Syscall_DmaAttach(Handle, Attachment)                              (oserr_t)syscall2(42, SCPARAM(Handle), SCPARAM(Attachment))
#define Syscall_DmaAttachmentMap(Attachment, AccessFlags)                  (oserr_t)syscall2(43, SCPARAM(Attachment), SCPARAM(AccessFlags))
#define Syscall_DmaAttachmentResize(Attachment, Length)                    (oserr_t)syscall2(44, SCPARAM(Attachment), SCPARAM(Length))
#define Syscall_DmaAttachmentRefresh(Attachment)                           (oserr_t)syscall1(45, SCPARAM(Attachment))
#define Syscall_DmaAttachmentCommit(Attachment, Address, Length)           (oserr_t)syscall3(46, SCPARAM(Attachment), SCPARAM(Address), SCPARAM(Length))
#define Syscall_DmaAttachmentUnmap(Attachment)                             (oserr_t)syscall1(47, SCPARAM(Attachment))
#define Syscall_DmaDetach(Attachment)                                      (oserr_t)syscall1(48, SCPARAM(Attachment))
#define Syscall_DmaGetMetrics(Handle, SizeOut, VectorsOut)                 (oserr_t)syscall3(49, SCPARAM(Handle), SCPARAM(SizeOut), SCPARAM(VectorsOut))

#define Syscall_CreateHandle(HandleOut)                                    (oserr_t)syscall1(50, SCPARAM(HandleOut))
#define Syscall_DestroyHandle(Handle)                                      (oserr_t)syscall1(51, SCPARAM(Handle))
#define Syscall_RegisterHandlePath(Handle, Path)                           (oserr_t)syscall2(52, SCPARAM(Handle), SCPARAM(Path))
#define Syscall_LookupHandle(Path, HandleOut)                              (oserr_t)syscall2(53, SCPARAM(Path), SCPARAM(HandleOut))
#define Syscall_HandleSetActivity(Handle, Flags)                           (oserr_t)syscall2(54, SCPARAM(Handle), SCPARAM(Flags))

#define Syscall_CreateHandleSet(Flags, HandleOut)                          (oserr_t)syscall2(55, SCPARAM(Flags), SCPARAM(HandleOut))
#define Syscall_ControlHandleSet(SetHandle, Operation, Handle, Event)      (oserr_t)syscall4(56, SCPARAM(SetHandle), SCPARAM(Operation), SCPARAM(Handle), SCPARAM(Event))
#define Syscall_ListenHandleSet(Handle, WaitContext, EventsOut)            (oserr_t)syscall3(57, SCPARAM(Handle), SCPARAM(WaitContext), SCPARAM(EventsOut))

#define Syscall_InstallSignalHandler(HandlerAddress)                       (oserr_t)syscall1(58, SCPARAM(HandlerAddress))
#define Syscall_FlushHardwareCache(CacheType, AddressStart, Length)        (oserr_t)syscall3(59, SCPARAM(CacheType), SCPARAM(AddressStart), SCPARAM(Length))
#define Syscall_SystemQuery(SystemInformation)                             (oserr_t)syscall1(60, SCPARAM(SystemInformation))
#define Syscall_ClockTick(Source, TickOut)                                 (oserr_t)syscall2(61, SCPARAM(Source), SCPARAM(TickOut))
#define Syscall_ClockFrequency(Source, FrequencyOut)                       (oserr_t)syscall2(62, SCPARAM(Source), SCPARAM(FrequencyOut))
#define Syscall_ReadWallClock(Time)                                        (oserr_t)syscall1(63, SCPARAM(Time))
#define Syscall_Sleep(DurationNs, RemainingNs)                             (oserr_t)syscall2(64, SCPARAM(DurationNs), SCPARAM(RemainingNs))
#define Syscall_Stall(DurationNs)                                          (oserr_t)syscall1(65, SCPARAM(DurationNs))

#endif //!__INTERNAL_CRT_SYSCALLS__
