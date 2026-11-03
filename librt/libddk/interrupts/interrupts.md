# Interrupt API boundaries

Device interrupts are initialized, allocated, configured with handlers and
resources, enabled, disabled and destroyed in that order. Allocation leaves
delivery disabled. Disable waits for handlers to finish and releases their
mappings. The reservation stays available for a later Enable.

Destroy releases the bus driver's reservation before the kernel reservation.
If cleanup fails, keep the remaining resources and retry cleanup.
After an ordinary failed Disable, Enable completes the stop before binding
again. This also applies to manual and PCI INTx lines that have no bus driver.
Once Destroy starts, finish destruction and allocate again before changing
handlers or enabling the object. This applies to every source kind.

## Implementation and state rules

`interrupt.c` manages allocation storage, handler and resource configuration,
and the shared enable/disable/destroy sequence. `interrupt-pci.c` handles PCI
capability queries, mode selection and fallback, vector negotiation, message
programming and requests to the PCI bus driver. Its public entry points use the
same allocator and cleanup sequence.

The internal interface is declared in `interrupt-private.h`, which is not
installed. Commands use a bus-neutral enum and are translated into PCI or
platform wire requests at this interface. PCI strategy constants are declared
in `types/interrupt-pci.h` so drivers and bus drivers can use them without adding
PCI capabilities to the common interrupt types.

Allocation phase (`Preparing`, `Ready`, `Destroying`) is separate from pending
cleanup. An object in Destroying can still have enabled handlers or a bus
reservation whose state is uncertain. Only the shared lifecycle code updates
this state, according to the following rules:

- Enabled requires Bound. Bound also covers partial mappings left after failure.
- When a public call returns with Bound set and Enabled clear, PendingCleanup
  must be set.
- Before calling the kernel to bind handlers, record that cleanup may be needed.
  If a bus driver reply is lost, keep the reservation until stop or release is
  confirmed.
- The kernel-stop helper clears mapping and delivery state only after all stop
  and unmap operations succeed. On failure, keep enough state to retry.
- Before editing a Ready object, complete any pending cleanup and remove its
  mappings. This also applies when a bus driver stop failed before any handler
  was bound.
- Destroying cannot return to Ready. A successful Disable does not undo Destroy.

Internal assertions check these mapping and delivery rules. Fault-injection
tests also check them in release builds. Callers must serialize operations on
each object and treat the public result fields as read-only.

## Driver API

`ddk/interrupts/interrupt.h` declares the lifecycle functions and common
allocation results. `Kind` describes the kernel resource. It does not describe
a PCI capability. Drivers must not modify `Kind`, `Count`, `Vectors` or `State`
directly.

For platform devices, select a firmware interrupt index before allocating:

```c
DeviceInterruptInitialize(&interrupt, device);
status = DeviceInterruptSelectSource(&interrupt, sourceIndex);
if (status != OS_EOK) {
    return status;
}
status = DeviceInterruptAllocate(&interrupt, 1, 1, &count);
```

Each selected platform source uses one object and one vector. When setting
handlers and resources, use vector index zero regardless of the firmware source
index. The bus driver can deliver the source as a fixed line or translate it
into a message. Native GIC and RP1 child devices use the same selection process.
If the device requires preparation, complete it before allocation.

For manual line objects initialized with a null device, configure `Line`
explicitly as before. These objects use neither a bus driver lease nor a firmware
source list.

## PCI policy

PCI-aware drivers include `ddk/interrupts/pci.h` and call
`DeviceInterruptAllocatePci` with the modes they can configure. The generic
allocator accepts all PCI modes by default. Both entry points return the same
object and use the same enable/disable/destroy implementation.

Modes are tried in MSI-X, MSI, INTx order. MSI-X negotiates the vector count.
MSI and INTx require a minimum of one and reserve exactly one vector. Fallback
is allowed only when a capability is absent or the kernel runs out of resources.
Return bus driver reservation conflicts, configuration failures and transport
failures without trying another mode.
`DeviceInterruptGetPciMode` reports the selected PCI capability for drivers that
configure queue or cause routing. It returns zero for platform sources and
objects with no allocation.

Each kernel request specifies one resource kind. PCI preference and fallback
are handled outside the kernel. Message allocation is independent of the
source, which may use MSI, MSI-X or platform translation. Legacy line allocation
keeps PCI pin and firmware routing. Fixed-line allocation requires an exact
controller match and a backend that preserves fixed routing. ARM currently
supports this. Fixed-line requests on x86 remain unsupported because its
resolver applies ACPI overrides.

## Bus API and wire requests

`ddk/interrupts/bus.h` declares privileged resolution, attachment and completion
operations. Message-controller resolution accepts only a platform message
fabric or a PCI host identity. Line-source resolution uses a separate versioned
identity with explicit firmware-controller and line fields. These operations
do not allocate resources, grant device access or make userspace responsible
for the controller's lifetime.

Platform bus drivers use `DeviceInterruptSourceQuery` to describe one firmware
source and `DeviceInterruptSourceRequest` to reserve, enable, disable or release
it. Reserve selects the firmware index. Subsequent requests select the
allocation lease. There is no platform `Program` request.

RP1 gets the trusted message from the kernel attachment and programs it with
delivery masked before reserve succeeds. Its PCI table and upstream completion
stay private to the bus driver. Native fixed-line bus drivers attach a device
setup lease without a message or completion callback.

Native bus drivers track confirmed attachments separately from leases kept
after an uncertain attach or rollback. If a later request is rejected because
the process does not control the group or the kind is unsupported, an
unconfirmed lease can be cleared. A confirmed attachment must remain intact.

The kernel disable syscall stops dispatch, waits for handlers and unmaps their
resources under one group update. If any step fails, binding and enabling remain
blocked until cleanup succeeds. Before calling it, the bus driver must stop
hardware delivery. Serializing the kernel operations does not stop the device.

Allocation and binding use separate syscalls so a mapping failure always leaves
a handle for cleanup. Completion creation and binding have the same requirement.
Bus attachment, enabled-state verification and closure operate on a child's
group, not the caller's group. Before reaping a group, the process responsible
for it must be dead.
Message queries are used for PCI programming, the RP1 upstream reservation and
diagnostic drivers. Keeping these calls separate makes the resource lifetime
and cleanup requirements explicit.

PCI query and control structures are declared in `ddk/types/interrupt-pci.h` and
contain only PCI capability and programming data. Common resource values are
in `ddk/types/interrupt.h`. Syscall group requests are in `interrupt-group.h`,
bus driver firmware identities in `interrupt-controller.h`, and platform source
requests in `interrupt-source.h`. The common values do not include driver
lifecycle declarations.

## Migration and compatibility

Rebuild the kernel, libddk, deviced and drivers together. The source API and
experimental wire ABI have changed. Previous formats are unsupported.

- Generic `DeviceInterruptAllocate` no longer takes a PCI strategy argument.
  Explicit PCI callers pass that argument to `DeviceInterruptAllocatePci`.
- Replace reads of `DeviceInterrupt.Strategy` with `DeviceInterruptGetPciMode`
  when configuring PCI hardware, or `Kind` when inspecting the resource type.
- Resolve controllers with `DeviceInterruptMessageControllerResolve` (syscall 32)
  or `DeviceInterruptLineSourceResolve` (syscall 22). Line sources are no longer
  encoded through a message identity's reserved field.
- The separate unbind syscall has been removed. Syscall 21 stops dispatch, waits
  for handlers and unmaps their resources under one group update. Line-source
  resolution moves from 86 into the freed slot 22, and the syscall table shrinks
  from 87 to 86 entries. Rebuild all userspace against these wrappers. Binaries
  using the previous slot assignments are incompatible.
- Group requests contain a resource kind and one controller ID. They have no PCI
  strategy mask or optional line-validation switch. Results contain handle,
  count and kind. Retrieve message payloads by indexed query.
- PCI support is 16 bytes. Platform source queries are 24 bytes and lease
  requests are 12 bytes. Group requests/results are 32/12 bytes. Compile-time
  assertions check these layouts on i386, amd64 and ARM64.

Cleanup requirements are unchanged. A failed operation may leave resources
reserved. If an enable result is uncertain, stop delivery before unmapping
handlers. Once a bus attachment is closed, a delayed reserve request cannot
reopen it.
