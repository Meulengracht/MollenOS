# Adapter contract implementation

[Documentation index](README.md) · [Wire contract](../../../../protocols/contracts/netadapter.gr)

## Entry points and netd callers

Generated server bindings call the handlers in [requests.c](../requests.c).
Each handler resolves the device/session under the module lock. netd emits
requests through [adapters/requests.c](../../../../services/netd/adapters/requests.c)
and handles directed events through
[adapters/client.c](../../../../services/netd/adapters/client.c).

| Contract operation | Driver implementation | Connection to netd |
| --- | --- | --- |
| `get_info` | Find device, require port zero, return `Info`. | Discover capabilities and size resources. |
| `open` | Validate features/client, reserve close-record space and create or return the current session. | Establish the session before allocating/registering pools. |
| `register_pool` | `VirtioNetPoolRegister`. | Register TX then RX with stable retry keys. |
| `configure` | Require idle session, MTU 1500 and exact unicast/broadcast filter; set `Configured`. | Finish configuration before preparing a run. |
| `prepare_run` | Validate next run and idle/retirement conditions; call `VirtioNetQueuesPrepare`. | Create queues before posting RX. |
| `post_rx_batch` / `submit_tx_batch` | `VirtioNetSubmit`, then `batch_admitted`. | One-way batch publication with asynchronous per-packet results. |
| `start_run` | Check run, fence and minimum RX; call `VirtioPciFinishInitialization`. | Enable hardware after RX admission is resolved. |
| `stop_run` | `VirtioNetStop`; return `StopBarrier`. | End one run, then let netd drain and acknowledge the barrier. |
| `acknowledge` | `VirtioNetAcknowledge`, then `ack_progress`. | Confirm capacity can be reused, including when idle. |
| `drain` | Poll hardware, validate ACK/cursor and copy a journal prefix; emit completions and `drain_end`. | Recover missed final results. |
| `get_link` | Re-read configuration and return `Link`. | Refresh link state; `link_changed` also reports changes. |
| `get_counters` | Return the session's software counters. | Serve netd's asynchronous snapshot refresh. |
| `unregister_pool` | `VirtioNetPoolUnregister`. | Available in the contract; normal netd teardown uses close instead. |
| `close` | `VirtioNetClose`, or recognize an already closed session. | Final confirmation that the driver no longer uses netd's pools. |

Unknown sessions receive `OS_ENOENT` on request/reply operations. One-way batch,
ACK and drain handlers silently return when session lookup fails rather than
sending directed information for an unmatched session.

## Session identity and repeatable open/close

`VirtioNetFindSession` requires an active session, matching `message->client`,
session ID and generation. Session IDs increase from one across the module.
`VirtioNetGeneration` uses the native handle of the module server endpoint.
These checks route operations and reject stale identities; their security depends
on the IPC runtime's client identity guarantees.

An active device accepts an identical zero-feature open from the same client and
returns the existing session. Another client receives `OS_EBUSY`. Nonzero
requested adapter features are unsupported.

Open has no separate replay key. After a client endpoint has closed a session on
that device, opening again through the same endpoint returns `OS_ENOENT` so a
delayed old open cannot create a new session. netd creates a fresh client when it
attaches a replacement adapter.

Close records store session identity and client endpoint for the device object's
lifetime. Storage is reserved during open: initially 16 records, then enlarged as
needed. A successful close appends its record without allocating and clears the
active session. A repeated matching close succeeds while the device remains in
the active registry. These records are not a fixed-size cache that drops the
oldest session after 16 closes.

## Pool registration and cleanup

[pools.c](../pools.c) gives a nonzero registration key one meaning for the whole
session. An identical retry for a mapped pool returns the original ID. Changed
layout or retry after its removal returns `OS_EPROTOCOL`. Successful pool IDs
start at one; unregistering does not make IDs, keys or slot sequence history
available for new registrations.

For a new pool, the driver checks:

1. The session is idle: `OPENED` or `STOPPED`, no outstanding packets, and all
   completion records acknowledged. The four-registration lifetime limit must
   have space.
2. Direction is TX/RX, slot count is 1..32, slot size is at least 1514, and slots
   fit the advertised region. Region size/offset and the attached SHM object's
   capacity are bounded by 128 KiB.
3. No mapped pool already has the same direction or handle. After attaching, the
   requested region must fit inside the actual SHM capacity.
4. The complete attached SHM geometry has at most 32 nonempty physical ranges,
   does not overflow, does not overlap itself, and does not overlap another
   mapped pool, including aliases through a different handle.
5. Every slot can be mapped through the device DMA context. TX uses `DMAToDevice`;
   RX uses `DMAFromDevice`. CPU mapping is read-only for TX and read/write for RX.

Only after setup succeeds does the driver publish the pool ID and advance
`PoolCount`. Failure unwinds mappings and SHM. If that rollback itself fails, the
unpublished fixed pool entry still contains the remaining resources; the next
registration attempt or close retries their cleanup.

Unregister disables the pool and releases mappings and SHM only when idle.
Partial release failure leaves it disabled with remaining resources available
for another unregister or close. Session close visits all four pool entries,
including unpublished registrations whose rollback failed.

## Batch replay and packet admission

`VirtioNetSubmit` first validates and applies both piggybacked ACK cursors. Even a
batch that cannot otherwise proceed may therefore free confirmed replay capacity.
The replay table has four entries indexed by batch ID modulo the window.

For a fresh batch, require the current nonzero run, exactly the next batch ID,
1..16 records and room in the replay window. RX may enter `PREPARED` or `RUNNING`;
TX may enter only `RUNNING`. The driver snapshots descriptors before admitting
them, saves every result in order, then advances `consumed_batch_id`. A batch
can be consumed even if every packet is rejected.

For an existing unretired batch, compare run, direction, count and every
descriptor field. An exact retry emits the saved results without submitting
again. A changed retry fails with `OS_EPROTOCOL`; a retired batch fails with
`OS_ENOENT`. Current-run replay is checked before the new-work state gate, so it
can still resolve admissions while stopping/stopped. A gap or full window fails
with `OS_EBUSY` and does not consume that attempt's batch ID.

Per-packet admission checks pool/direction, queue zero, slot index and an
increasing nonzero submission sequence. Once a valid slot and new sequence have
been identified, that sequence is recorded even if later checks reject the
packet. Further checks require:

- No outstanding use of the slot; flags zero; offset and length inside the slot.
- TX length 14..1514, or RX capacity at least 1514. Hardware receives into only
  1514 frame bytes even if the submitted RX region is larger.
- No session fault; TX link up; fewer than 32 outstanding packets in that direction.
- Journal space for both existing unacknowledged results and all outstanding
  packets, with no completion-sequence overflow.

The driver copies the packet descriptor into the slot and increments outstanding
work **before** calling `VirtioNetQueuePacket`. This reserves a terminal result
before hardware can see the chain. An ordinary pre-publication failure undoes
that reservation. `OS_EINPROGRESS` is treated as accepted plus a device fault:
publication or DMA cleanup is uncertain, so reporting rejection would let netd
reuse memory too early.

## Completion journal and ACKs

`VirtioNetComplete` ignores a slot that already has a final result. Otherwise it
appends one record to the 128-entry journal, increments the session-wide sequence,
clears the slot's outstanding flag and updates per-direction counters. Success
reports the frame length; error/cancellation reports zero length and a detail
status. Cancellation does not increment packet error counters.

`VirtioNetPushCompletions` sends new records in chunks of at most 16 with
`request_id=0`. A send failure stops that push; saved records remain available to
drain. There is no successful-send shortcut that discards the journal.

| Progress cursor | Driver meaning | netd consequence |
| --- | --- | --- |
| `consumed_batch_id` | Highest batch whose descriptor results were saved. | Later progress may reveal a missing admission event, but cannot replace its results. |
| `retired_batch_id` | Admission results acknowledged by netd. | Corresponding replay/window positions can be reused. |
| `highest_completion_sequence` | Highest final result in the journal. | A larger value can reveal missing completion records. |
| `retired_completion_sequence` | Final results acknowledged by netd. | Corresponding completion capacity can be reused. |

`VirtioNetAcknowledge` checks both cursors against issued progress before advancing
either. Older valid cursors do not move state backward. netd is responsible for
ACKing only contiguous processed prefixes. Driver confirmation lets netd update
its own limits; sending an ACK alone is insufficient. Batch retirement does not
complete packets, and completion retirement does not resolve missing admission.
