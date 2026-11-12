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
 */

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

// BEGIN GENERATED SYSCALL WRAPPERS - tools/generate_syscalls.py
#define Syscall_Debug(_level, _msg) (oserr_t)syscall2(0, SCPARAM(_level), SCPARAM(_msg))
#define Syscall_MigrateLogs(_buffer, _size, _readOut) (oserr_t)syscall3(1, SCPARAM(_buffer), SCPARAM(_size), SCPARAM(_readOut))
#define Syscall_MapBootFramebuffer(BufferOut) (oserr_t)syscall1(2, SCPARAM(BufferOut))
#define Syscall_MapRamdisk(BufferOut, LengthOut) (oserr_t)syscall2(3, SCPARAM(BufferOut), SCPARAM(LengthOut))
#define Syscall_CapabilityGrant(_capability, _handle) (oserr_t)syscall2(4, SCPARAM(_capability), SCPARAM(_handle))
#define Syscall_CreateMemorySpace(Flags, ResultOut) (oserr_t)syscall2(5, SCPARAM(Flags), SCPARAM(ResultOut))
#define Syscall_GetMemorySpaceForThread(ThreadHandle, HandleOut) (oserr_t)syscall2(6, SCPARAM(ThreadHandle), SCPARAM(HandleOut))
#define Syscall_CreateMemorySpaceMapping(Handle, Parameters, AddressOut) (oserr_t)syscall3(7, SCPARAM(Handle), SCPARAM(Parameters), SCPARAM(AddressOut))
#define Syscall_FirmwareQuery(InfoOut) (oserr_t)syscall1(8, SCPARAM(InfoOut))
#define Syscall_FirmwareTableLocate(Key, TableOut) (oserr_t)syscall2(9, SCPARAM(Key), SCPARAM(TableOut))
#define Syscall_FirmwareTableRead(Key, Buffer, Size, LengthOut) (oserr_t)syscall4(10, SCPARAM(Key), SCPARAM(Buffer), SCPARAM(Size), SCPARAM(LengthOut))
#define Syscall_FirmwareTableMap(Key, MappingOut, LengthOut) (oserr_t)syscall3(11, SCPARAM(Key), SCPARAM(MappingOut), SCPARAM(LengthOut))
#define Syscall_AcpiQueryPci(Query, Result) (oserr_t)syscall2(12, SCPARAM(Query), SCPARAM(Result))
#define Syscall_IoSpaceRegister(IoSpace) (oserr_t)syscall1(13, SCPARAM(IoSpace))
#define Syscall_IoSpaceAcquire(IoSpace) (oserr_t)syscall1(14, SCPARAM(IoSpace))
#define Syscall_IoSpaceMap(Id, Offset, Length, Mapping) (oserr_t)syscall4(15, SCPARAM(Id), SCPARAM(Offset), SCPARAM(Length), SCPARAM(Mapping))
#define Syscall_IoSpaceRelease(IoSpace) (oserr_t)syscall1(16, SCPARAM(IoSpace))
#define Syscall_IoSpaceDestroy(IoSpaceId) (oserr_t)syscall1(17, SCPARAM(IoSpaceId))
#define Syscall_InterruptAllocate(Request, Result) (oserr_t)syscall2(18, SCPARAM(Request), SCPARAM(Result))
#define Syscall_InterruptBind(Handle, Request) (oserr_t)syscall2(19, SCPARAM(Handle), SCPARAM(Request))
#define Syscall_InterruptEnable(Handle) (oserr_t)syscall1(20, SCPARAM(Handle))
#define Syscall_InterruptDisable(Handle) (oserr_t)syscall1(21, SCPARAM(Handle))
#define Syscall_InterruptLineSourceResolve(Query, Controller) (oserr_t)syscall2(22, SCPARAM(Query), SCPARAM(Controller))
#define Syscall_InterruptRelease(Handle) (oserr_t)syscall1(23, SCPARAM(Handle))
#define Syscall_InterruptGetMessage(Handle, Index, Message) (oserr_t)syscall3(24, SCPARAM(Handle), SCPARAM(Index), SCPARAM(Message))
#define Syscall_InterruptReap(Handle) (oserr_t)syscall1(25, SCPARAM(Handle))
#define Syscall_InterruptBusHandlerCreate(HandleOut) (oserr_t)syscall1(26, SCPARAM(HandleOut))
#define Syscall_InterruptBusHandlerBind(Handle, Resources) (oserr_t)syscall2(27, SCPARAM(Handle), SCPARAM(Resources))
#define Syscall_InterruptBusHandlerRelease(Handle) (oserr_t)syscall1(28, SCPARAM(Handle))
#define Syscall_InterruptBusAttach(Request) (oserr_t)syscall1(29, SCPARAM(Request))
#define Syscall_InterruptBusStatus(Handle, Principal, Device) (oserr_t)syscall3(30, SCPARAM(Handle), SCPARAM(Principal), SCPARAM(Device))
#define Syscall_InterruptBusClose(Handle, Principal, Device) (oserr_t)syscall3(31, SCPARAM(Handle), SCPARAM(Principal), SCPARAM(Device))
#define Syscall_InterruptMessageControllerResolve(Query, Controller) (oserr_t)syscall2(32, SCPARAM(Query), SCPARAM(Controller))
#define Syscall_GetProcessBaseAddress(BaseAddressOut) (oserr_t)syscall1(33, SCPARAM(BaseAddressOut))
#define Syscall_MapThreadMemoryRegion(ThreadHandle, Address, TopOfStack, PointerOut) (oserr_t)syscall4(34, SCPARAM(ThreadHandle), SCPARAM(Address), SCPARAM(TopOfStack), SCPARAM(PointerOut))
#define Syscall_ThreadCreate(Entry, Argument, Parameters, HandleOut) (oserr_t)syscall4(35, SCPARAM(Entry), SCPARAM(Argument), SCPARAM(Parameters), SCPARAM(HandleOut))
#define Syscall_ThreadExit(ExitCode) (oserr_t)syscall1(36, SCPARAM(ExitCode))
#define Syscall_ThreadSignal(ThreadId, Signal) (oserr_t)syscall2(37, SCPARAM(ThreadId), SCPARAM(Signal))
#define Syscall_ThreadJoin(ThreadId, ExitCode) (oserr_t)syscall2(38, SCPARAM(ThreadId), SCPARAM(ExitCode))
#define Syscall_ThreadDetach(ThreadId) (oserr_t)syscall1(39, SCPARAM(ThreadId))
#define Syscall_ThreadYield() (oserr_t)syscall0(40)
#define Syscall_ThreadId() (uuid_t)syscall0(41)
#define Syscall_ThreadCookie() (UUId_t)syscall0(42)
#define Syscall_ThreadSetCurrentName(Name) (uuid_t)syscall1(43, SCPARAM(Name))
#define Syscall_ThreadGetCurrentName(NameBuffer, MaxLength) (uuid_t)syscall2(44, SCPARAM(NameBuffer), SCPARAM(MaxLength))
#define Syscall_FutexWait(context, params) (oserr_t)syscall2(45, SCPARAM(context), SCPARAM(params))
#define Syscall_FutexWake(Parameters) (oserr_t)syscall1(46, SCPARAM(Parameters))
#define Syscall_EventCreate(InitialValue, Flags, HandleOut, SyncAddress) (oserr_t)syscall4(47, SCPARAM(InitialValue), SCPARAM(Flags), SCPARAM(HandleOut), SCPARAM(SyncAddress))
#define Syscall_IPCSend(Messages, MessageCount, Deadline, Context) (oserr_t)syscall4(48, SCPARAM(Messages), SCPARAM(MessageCount), SCPARAM(Deadline), SCPARAM(Context))
#define Syscall_MemoryAllocate(Hint, Size, Flags, MemoryOut) (oserr_t)syscall4(49, SCPARAM(Hint), SCPARAM(Size), SCPARAM(Flags), SCPARAM(MemoryOut))
#define Syscall_MemoryFree(Pointer, Size) (oserr_t)syscall2(50, SCPARAM(Pointer), SCPARAM(Size))
#define Syscall_MemoryProtect(MemoryPointer, Length, Flags, PreviousFlags) (oserr_t)syscall4(51, SCPARAM(MemoryPointer), SCPARAM(Length), SCPARAM(Flags), SCPARAM(PreviousFlags))
#define Syscall_MemoryQueryAllocation(MemoryPointer, Descriptor) (oserr_t)syscall2(52, SCPARAM(MemoryPointer), SCPARAM(Descriptor))
#define Syscall_MemoryQueryAttributes(MemoryPointer, Length, Attributes) (oserr_t)syscall3(53, SCPARAM(MemoryPointer), SCPARAM(Length), SCPARAM(Attributes))
#define Syscall_SHMCreate(_shm, _handle) (oserr_t)syscall2(54, SCPARAM(_shm), SCPARAM(_handle))
#define Syscall_SHMAttachToken(_token, _handleOut) (oserr_t)syscall2(55, SCPARAM(_token), SCPARAM(_handleOut))
#define Syscall_SHMAttach(_key, _handle) (oserr_t)syscall2(56, SCPARAM(_key), SCPARAM(_handle))
#define Syscall_SHMMap(_handle, _offset, _length, _flags) (oserr_t)syscall4(57, SCPARAM(_handle), SCPARAM(_offset), SCPARAM(_length), SCPARAM(_flags))
#define Syscall_SHMCommit(_handle, _address, _length) (oserr_t)syscall3(58, SCPARAM(_handle), SCPARAM(_address), SCPARAM(_length))
#define Syscall_SHMUnmap(_handle, _address, _length) (oserr_t)syscall3(59, SCPARAM(_handle), SCPARAM(_address), SCPARAM(_length))
#define Syscall_SHMDetach(_handle) (oserr_t)syscall1(60, SCPARAM(_handle))
#define Syscall_SHMMetrics(_handle, _sizeOut, _vectorsOut) (oserr_t)syscall3(61, SCPARAM(_handle), SCPARAM(_sizeOut), SCPARAM(_vectorsOut))
#define Syscall_HandleCreate(Registration, Access, HandleOut) (oserr_t)syscall3(62, SCPARAM(Registration), SCPARAM(Access), SCPARAM(HandleOut))
#define Syscall_HandleImport(_token, _memorySpace, _out) (oserr_t)syscall3(63, SCPARAM(_token), SCPARAM(_memorySpace), SCPARAM(_out))
#define Syscall_HandleDestroy(_handle) (oserr_t)syscall1(64, SCPARAM(_handle))
#define Syscall_HandleLookup(_path, _handleOut) (oserr_t)syscall2(65, SCPARAM(_path), SCPARAM(_handleOut))
#define Syscall_HandleSetActivity(_handle, _flags) (oserr_t)syscall2(66, SCPARAM(_handle), SCPARAM(_flags))
#define Syscall_HandleProviderRegister(_queue, _out) (oserr_t)syscall2(67, SCPARAM(_queue), SCPARAM(_out))
#define Syscall_HandleValidate(_registration, _group, _reference, _access, _out) (oserr_t)syscall5(68, SCPARAM(_registration), SCPARAM(_group), SCPARAM(_reference), SCPARAM(_access), SCPARAM(_out))
#define Syscall_HandleProviderGetEvents(_registration, _ids, _capacity, _count) (oserr_t)syscall4(69, SCPARAM(_registration), SCPARAM(_ids), SCPARAM(_capacity), SCPARAM(_count))
#define Syscall_GrantTokenCreate(Handle, Options, TokenOut) (oserr_t)syscall3(70, SCPARAM(Handle), SCPARAM(Options), SCPARAM(TokenOut))
#define Syscall_GrantTokenRevoke(ControlHandle) (oserr_t)syscall1(71, SCPARAM(ControlHandle))
#define Syscall_CreateHandleSet(Flags, HandleOut) (oserr_t)syscall2(72, SCPARAM(Flags), SCPARAM(HandleOut))
#define Syscall_ControlHandleSet(SetHandle, Operation, Handle, Event) (oserr_t)syscall4(73, SCPARAM(SetHandle), SCPARAM(Operation), SCPARAM(Handle), SCPARAM(Event))
#define Syscall_ListenHandleSet(handle, context, params, eventsOut) (oserr_t)syscall4(74, SCPARAM(handle), SCPARAM(context), SCPARAM(params), SCPARAM(eventsOut))
#define Syscall_InstallSignalHandler(HandlerAddress) (oserr_t)syscall1(75, SCPARAM(HandlerAddress))
#define Syscall_FlushHardwareCache(CacheType, AddressStart, Length) (oserr_t)syscall3(76, SCPARAM(CacheType), SCPARAM(AddressStart), SCPARAM(Length))
#define Syscall_SystemQuery(_req, _buffer, _size, _sizeOut) (oserr_t)syscall4(77, SCPARAM(_req), SCPARAM(_buffer), SCPARAM(_size), SCPARAM(_sizeOut))
#define Syscall_ClockTick(Source, TickOut) (oserr_t)syscall2(78, SCPARAM(Source), SCPARAM(TickOut))
#define Syscall_ClockFrequency(Source, FrequencyOut) (oserr_t)syscall2(79, SCPARAM(Source), SCPARAM(FrequencyOut))
#define Syscall_Time(source, timeOut) (oserr_t)syscall2(80, SCPARAM(source), SCPARAM(timeOut))
#define Syscall_Sleep(DurationNs, RemainingNs) (oserr_t)syscall2(81, SCPARAM(DurationNs), SCPARAM(RemainingNs))
#define Syscall_Stall(DurationNs) (oserr_t)syscall1(82, SCPARAM(DurationNs))
#define Syscall_DmaOpen(Device, Parameters, ContextOut) (oserr_t)syscall3(83, SCPARAM(Device), SCPARAM(Parameters), SCPARAM(ContextOut))
#define Syscall_DmaAllocate(Context, Requirements, ResultOut) (oserr_t)syscall3(84, SCPARAM(Context), SCPARAM(Requirements), SCPARAM(ResultOut))
#define Syscall_DmaMapShm(Context, Shm, Parameters, ResultOut) (oserr_t)syscall4(85, SCPARAM(Context), SCPARAM(Shm), SCPARAM(Parameters), SCPARAM(ResultOut))
#define Syscall_DmaGetSegments(Mapping, Entries, Capacity, CountOut) (oserr_t)syscall4(86, SCPARAM(Mapping), SCPARAM(Entries), SCPARAM(Capacity), SCPARAM(CountOut))
#define Syscall_ShmAccessCreate(Shm, Offset, Length, Handle) (oserr_t)syscall4(87, SCPARAM(Shm), SCPARAM(Offset), SCPARAM(Length), SCPARAM(Handle))
#define Syscall_ShmAccessBegin(Async, Handle, Parameters) (oserr_t)syscall3(88, SCPARAM(Async), SCPARAM(Handle), SCPARAM(Parameters))
#define Syscall_ShmAccessEnd(Handle, Kind, CompletedLength) (oserr_t)syscall3(89, SCPARAM(Handle), SCPARAM(Kind), SCPARAM(CompletedLength))
#define Syscall_ResourceRelease(Handle) (oserr_t)syscall1(90, SCPARAM(Handle))
// END GENERATED SYSCALL WRAPPERS

#endif //!__INTERNAL_CRT_SYSCALLS__
