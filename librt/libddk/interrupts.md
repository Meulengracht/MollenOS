# Interrupt API boundaries

All device interrupts use one lifecycle: initialize, allocate, describe handlers
and resources, enable, disable, destroy. Allocation does not enable delivery.
Disabling waits for handlers and releases their mappings, while preserving the
reservation for a later enable. Destroy releases provider ownership before the
kernel reservation. Failed cleanup retains ownership and must be retried.
An ordinary failed disable is recoverable: Enable first completes the stop and
then binds again, including manual/PCI INTx lines with no provider. Destroy is
terminal for every source kind. After it starts, finish destruction and allocate
again before changing handlers or enabling the object.

## Implementation and state invariants

`interrupt.c` owns allocation storage, handler/resource configuration, and the
shared enable/disable/destroy ordering. `interrupt-pci.c` owns PCI capability
queries, mode preference and fallback, vector negotiation, message programming,
and translation into PCI provider requests. Its public PCI entry points enter
the same allocator and lifecycle; there is no second PCI teardown path.

The non-installed `interrupt-private.h` describes their internal boundary.
Provider commands use a bus-neutral enum and are translated into the PCI or
platform wire protocol at that boundary. PCI strategy constants live in
`types/pci-interrupt.h`, where both drivers and bus providers can use them without
putting PCI capabilities into the common interrupt types.

Allocation phase (`Preparing`, `Ready`, `Destroying`) is independent of cleanup
obligations. An object can be terminal while still owning enabled handlers or an
uncertain provider reservation. Only the shared lifecycle changes those duties:

- Enabled implies Bound. Bound includes possible partial mappings after failure.
- Bound without Enabled at a public-call boundary implies PendingCleanup.
- Binding records its cleanup duty before calling the kernel. Lost provider
  replies likewise retain ownership until stop/release is confirmed.
- One kernel-stop helper clears mapping and delivery duties only after complete
  stop/unmap success. Every failed transition retains enough state for retry.
- Ready alone is insufficient for editing: neither mappings nor pending cleanup
  may remain, even when a provider stop failed before any handler was bound.
- Destroying never becomes Ready again. Successful Disable does not undo Destroy.

Internal assertions check the mapping/delivery implications; fault-injection
tests also check them in release builds. The public result fields remain
read-only by contract, and callers must serialize operations on each object.

## Driver API

`ddk/interrupt.h` exposes the lifecycle and common allocation results. `Kind`
describes the kernel resource, not a PCI capability. Drivers must not modify
`Kind`, `Count`, `Vectors` or `State` directly.

For platform devices, select a firmware interrupt index before allocating:

```c
DeviceInterruptInitialize(&interrupt, device);
status = DeviceInterruptSelectSource(&interrupt, sourceIndex);
if (status != OS_EOK) {
    return status;
}
status = DeviceInterruptAllocate(&interrupt, 1, 1, &count);
```

Each selected platform source owns one object and one vector. Handler and
resource setters use vector index zero, regardless of the firmware source index.
The provider can deliver that source as a fixed line or translate it into a
message. This selection works identically for native GIC and RP1 child devices.
Devices requiring activation must be prepared before allocation.

Manual line objects initialized with a null device retain the existing explicit
`Line` configuration. These do not use a provider lease or firmware source list.

## PCI policy

PCI-aware drivers include `ddk/pci-interrupt.h` and use
`DeviceInterruptAllocatePci` to constrain the modes they can configure. The
generic allocator accepts all PCI modes by default. Both entry points return the
same object and use the same enable/disable/destroy implementation.

PCI modes are tried in MSI-X, MSI, INTx order. MSI-X negotiates the vector count;
MSI and INTx require a minimum of one and reserve exactly one vector. Only absent
capabilities and kernel exhaustion allow fallback. Provider ownership conflicts,
configuration failures and transport failures are returned without selecting a
different mode. `DeviceInterruptGetPciMode` reports the selected PCI capability
for drivers that must configure queue/cause routing. It returns zero for platform
sources and objects with no allocation.

The kernel receives one resource kind per request and does not implement PCI
preference or fallback. Message allocation is independent of whether a device
uses MSI, MSI-X or a platform translation. Legacy line allocation retains PCI
pin/firmware routing. Fixed-line allocation requires an exact controller match
and a backend that preserves fixed routing, currently ARM; x86 fixed-line
requests remain unsupported because its resolver applies ACPI overrides.

## Provider API and wire contracts

`ddk/interrupt-provider.h` exposes privileged resolution, attachment and completion
operations. Message-controller resolution accepts only a platform message fabric
or a PCI host identity. Line-source resolution has a separate versioned identity
with explicit firmware-controller and line fields. Neither operation allocates
resources, grants device access or transfers controller lifetime to userspace.

Platform providers use `DeviceInterruptSourceQuery` to describe one firmware
source, and `DeviceInterruptSourceRequest` for reserve/enable/disable/release.
The reserve request identifies the firmware index; subsequent operations identify
the allocation lease. There is no platform `Program` request. RP1 obtains the
trusted message from kernel attachment and programs it while delivery is masked,
before reserve succeeds. Its PCI table and upstream completion remain private
to the provider. Native fixed-line providers attach an activation lease without
a message or completion callback.
Native providers distinguish confirmed attachments from leases retained after
an uncertain attach/rollback. A later ownership or unsupported-kind rejection
can clear an unconfirmed lease; it must not discard a confirmed attachment.

The kernel disable syscall combines dispatch shutdown, handler synchronization
and resource unmapping under one group update. On any failure the group remains
blocked from binding/enabling until cleanup succeeds. Providers must still stop
hardware delivery first; this serialization does not replace device quiescence.

The remaining syscalls represent distinct ownership boundaries. Allocation and
binding stay separate so a mapping failure always has a handle for cleanup.
Completion creation and binding have the same requirement. Provider attachment,
enabled-state verification and closure act on a child's group rather than the
caller's own group, and reaping requires a dead owner. Message queries remain in
use by PCI programming, the RP1 upstream reservation and diagnostic drivers.
Combining these would hide ownership or cleanup requirements rather than remove
an unnecessary lifecycle step.

PCI query/control structures remain in `ddk/types/pci-interrupt.h` and contain
only PCI capability and programming data. Common resource values live in
`ddk/types/interrupt.h`; syscall group requests in `interrupt-group.h`; provider
firmware identities in `interrupt-controller.h`; and platform source requests in
`interrupt-source.h`. Common values do not include driver lifecycle declarations.

## Migration and compatibility

Rebuild the kernel, libddk, deviced and drivers together. This changes the source
API and experimental wire ABI; no compatibility interpretation is provided.

- Generic `DeviceInterruptAllocate` no longer takes a PCI strategy argument.
  Explicit PCI callers use `DeviceInterruptAllocatePci` with that argument.
- Replace reads of `DeviceInterrupt.Strategy` with `DeviceInterruptGetPciMode`
  when configuring PCI hardware, or `Kind` when inspecting the resource type.
- Provider resolution is now `DeviceInterruptMessageControllerResolve` or
  `DeviceInterruptLineSourceResolve`. The former uses syscall 32; the latter uses
  syscall 22. No line source is encoded through a message identity's reserved field.
- The separate unbind syscall has been removed. Syscall 21 now stops dispatch,
  synchronizes handlers and unmaps their resources under one group update.
  Line-source resolution moves from 86 into the freed slot 22; the syscall table
  shrinks from 87 to 86 entries. Rebuild all userspace against these wrappers;
  binaries using the previous slot assignments are incompatible.
- Group requests contain a resource kind and one controller ID, without a PCI
  strategy mask or optional line-validation switch. Results contain handle,
  count and kind; retrieve message payloads by indexed query.
- PCI support is 16 bytes. Platform source queries are 24 bytes and lease
  requests are 12 bytes. Group requests/results are 32/12 bytes. Compile-time
  assertions check these layouts on i386, amd64 and ARM64.

The lifecycle's cleanup obligations are unchanged: a failed operation may retain
ownership, an uncertain enable must be stopped before handler unmapping, and a
closed provider attachment cannot be reopened by a delayed reserve request.
