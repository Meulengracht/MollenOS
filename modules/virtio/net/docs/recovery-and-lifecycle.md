# Recovery and lifecycle

[Documentation index](README.md)

## Stop is a reset plus a completion barrier

netd first stops publishing new work and resolves every published batch admission.
It sends `stop_run(run, fence)` with the final consumed batch ID. `VirtioNetStop`
checks the run and fence, records `StopFence` and enters `STOPPING` before any
reset attempt. New batches cannot enter that run after this point.

```mermaid
sequenceDiagram
    participant N as netd core
    participant D as virtio-net session
    participant V as Device and DMA mappings
    N->>N: Freeze publication; resolve batch admissions
    N->>D: stop_run(run, fence)
    D->>D: Save fence; enter STOPPING
    D->>V: Reset device and confirm status zero
    D->>V: End active slot transfers; abort and destroy queues
    D->>D: Journal cancellations; save StopBarrier; enter STOPPED
    D-->>N: New completions and stop reply with barrier
    N->>D: Drain missing results and acknowledge prefixes
    D-->>N: Completion data and retirement progress
    N->>N: Enter STOPPED after barrier and retirement checks
    N->>D: prepare_run(next run)
    D->>V: Negotiate and create new queues
```

`VirtioNetQueuesReset` writes device status zero and requires reset confirmation.
Only then does it scan fixed pool slots, end active transfers with zero completed
bytes, and record cancellation for each outstanding packet. It also aborts queued
chains and destroys the queues. The fixed slot scan covers DMA cleanup that failed
after a used entry had already been consumed or before a chain was published.
Repeated completion calls are harmless because each slot has one final result.

This stop path cancels outstanding work after reset; it does not wait for every
pending frame to finish successfully. A failed reset returns an error with the run
still `STOPPING`, the fence unchanged and resources still allocated. An identical
stop can retry; a changed fence is rejected. Once stopped, identical retries
return the saved barrier without resetting again.

Driver `STOPPED` can precede netd `STOPPED`. netd must process and obtain confirmed
ACK progress through the barrier and retire all admitted batches. Next-run
preparation requires an idle configured session, no fault, all batch results
retired, and mapped TX/RX pools. Run IDs increase exactly by one; batch and
completion sequences do not reset. The driver recreates hardware queues while
keeping pool mappings.

## Close and local packet lifetime

`VirtioNetClose` enters `STOPPING`, resets/cleans the queues and detaches every
pool. Only after all those operations succeed does it record the closed identity
and clear the active session. `close` does not require netd to recover all journal
events first: its successful response is the final guarantee that the driver no
longer accesses the pools.

On that response, netd cancels unresolved local work and marks its pools retired.
RX packets held by consumers and direct TX packets still being built remain valid
in netd's SHM until explicitly released/cancelled. Closing the driver's mappings
does not revoke those local pointers. netd delays adapter destruction and driver
replacement until all local tokens are returned.

If reset, DMA end, unmapping or SHM release fails, the driver returns failure and
keeps the session/resources needed for another close. Some earlier cleanup steps
may already have succeeded; retry continues with the resources that remain.
netd must not infer pool release from an error or timeout. Its uncertain/failed
close path enters quarantine and needs explicit recovery.

Sources: [session.c](../session.c), [queue.c](../queue.c), [pools.c](../pools.c),
[netd lifecycle](../../../../services/netd/docs/recovery-and-lifecycle.md).

## Failure boundaries

| Failure point | Driver behavior | Meaning for netd |
| --- | --- | --- |
| Packet validation or ordinary failure before publication | Reject descriptor; undo any outstanding reservation after safe DMA rollback. | That descriptor has no future terminal result; admission reports the error. |
| Notification fails after available-index publication | Keep packet accepted, fault the session and return its saved admission as success. | Completion/reset is still required before storage reuse. |
| Pre-publication DMA rollback fails | Keep packet accepted through the `OS_EINPROGRESS` path and fault. | Even without a published chain, reset/cleanup must settle the mapping. |
| `DMAEndTransfer` fails after a used entry | Fault; slot remains outstanding and reachable in its fixed pool entry. | No successful completion is published for uncertain access. |
| Bad RX header/length or nonzero TX used length | Record packet error with zero payload length. | Consume a terminal error result; do not read RX payload. |
| Admission/completion event send fails | Keep replay/journal data for retry or drain. | Publication loss does not undo admission or completion. |
| Pool rollback/unregister release fails | Disable affected pool and preserve remaining mapping/SHM handles. | Retry cleanup; failure is not permission to free shared storage. |
| Reset fails | Keep queue and packet memory; new work remains gated off by stop/close. | No completion or close guarantee can be inferred. |

A session fault prevents later per-packet admission but is not a DMA-stop
confirmation. A new session generation likewise cannot establish that old hardware
access ended.

## Device removal and module unload

`OnUnregister` removes the device from the active registry **before** destruction.
`VirtioNetDeviceDestroy` resets queues unless transport teardown is already in
progress, removes interrupt resources, detaches pools, frees metadata, closes
the PCI transport and finally frees the object and close history.

Any destruction error returns with remaining resources intact. The module keeps
the device on `g_cleanupDevices`, outside normal device/session/closed-session
lookup. Failed initial creation uses the same private cleanup list. The current
module has no periodic worker that retries this list; `OnUnload` makes another
destruction attempt. Repeated unregister of an already removed device returns
`OS_ENOENT`.

At process unload, an unsuccessful destruction cannot wait for a later retry.
The module attempts `VirtioPciTransportDisable` to disable PCI bus mastering,
address decoding and interrupts; it logs an error if that fallback also fails.
This is a final unload path, not a successful adapter close response.

Device-service removal also tells netd to close its ports. The driver's removal
path and netd's close request are separate operations. Once a device is absent
from the driver's active registry, an old close can return `OS_ENOENT`, even if
hardware teardown happened independently. netd does not treat that response as
proof that its pool memory is safe to free. A removed driver-side device and a
quarantined netd entry can therefore coexist.

Source: [main.c](../main.c), [device.c](../device.c),
[common transport](../../common/pci.c).

## Diagnostics and tests

Inspect both sides of a stalled flow:

- No adapter advertisement: inspect device creation, capability/feature checks,
  configuration reads, metadata layout and interrupt registration.
- `prepare_run` fails: inspect pool presence, ACK progress, actual queue size and
  worst-case descriptor requirements. A configured session alone is insufficient.
- TX queue pressure: distinguish free netd slots from driver outstanding limits,
  journal capacity and free hardware descriptors.
- RX traffic without netd packets: check `rx_dropped`, malformed-header errors and
  destination filtering. Filtered frames can reuse a posted slot internally.
- Driver stopped but netd stopping: compare the returned barrier with netd's
  processed/acknowledged completion prefixes and batch retirement.
- Fault followed by close failure: inspect the driver's reset/DMA cleanup error.
  netd's fault handler currently requests close without storing the fault detail.

Driver counters are session software counters, not hardware registers. TX byte
counts use original frame length, excluding private header and short-frame padding.
RX drops count destination-filtered frames; cancelled packets do not count as
packet errors. `resets` increments after successful `VirtioNetStop`, rather than
counting every low-level reset during initialization or close.

| Test | Coverage boundary |
| --- | --- |
| [virtio_test.py](../../../../testing/netadapter/virtio_test.py) and [virtio_test.c](../../../../testing/netadapter/virtio_test.c) | Real pool/session/packet code against host DMA/queue models, including netd core integration, replay, event loss, restart and cleanup failures. |
| Same runner with `--sessions-only` | Session/open/close and setup coverage without the netd integration harness. |
| Same runner with `--mutation-check` | Confirms tests detect forced contiguous metadata and incorrect first-segment addressing. |
| [virtio_registry_test.sh](../../../../testing/netadapter/virtio_registry_test.sh) | Registry isolation after failed cleanup, removal and final PCI-disable fallback. |
| [virtio_ipc_test.c](../../../../testing/netadapter/virtio_ipc_test.c) | Boot test through actual netd discovery, driver RPC, virtqueues and interrupts. Sends short ARP frames, restarts, and checks packet lifetimes through close. |

In a host environment with the repository's Python, Clang and Unix linker setup,
the host runners can be invoked from the repository root:

```sh
python3 testing/netadapter/virtio_test.py
python3 testing/netadapter/virtio_test.py --sessions-only
bash testing/netadapter/virtio_registry_test.sh
```

The boot suite is enabled with `VALI_VIRTIO_NET_TESTS=ON` and
`VALI_NET_ADAPTER_TESTS=OFF` in the normal Vali build configuration. The two
adapter boot suites cannot be enabled together because they install the same netd
consumer hooks. Given a built image containing the Virtio suite,
[run_qemu.py](../../../../testing/netadapter/run_qemu.py) supports:

```sh
python3 testing/netadapter/run_qemu.py build/disk.img --virtio
```

The runner needs QEMU and OVMF; firmware paths can be supplied with `--ovmf-code`
and `--ovmf-vars`. It uses a disposable disk overlay and restricted user networking.
Success requires `VIRTIO NET PASS all:` in the serial output; a boot prompt or
timeout is not a pass. Host models do not establish real PCI/DMA behavior, and
the boot ARP workload does not establish working TCP/IP sockets.
