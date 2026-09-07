# Week 7 — Automotive Ethernet: buying bandwidth with certainty

## 1. Concept

CAN has a hard ceiling: **1 Mbit/s and 8 bytes** (CAN-FD: ~5 Mbit/s and 64).
For *the speed is 87.5 km/h* that is more than enough, and CAN is not going
away. But a reversing camera needs 5–20 Mbit/s, a surround-view system needs
hundreds, and a 4 GB OTA update over CAN would take about eighteen hours
against six minutes over 100 Mbit/s Ethernet.

So a modern vehicle carries **both**. CAN for small, time-critical signals;
Ethernet for everything large. Anyone who says Ethernet replaces CAN has the
architecture backwards.

The trade is not free. CAN gives four things away, and Ethernet takes all four
back:

| CAN gives for free | Guaranteed by | On Ethernet |
|---|---|---|
| Frames arrive in order | one physical bus | a switch may reorder |
| A frame is retried until ACKed | the controller, in hardware | a datagram is dropped in silence |
| Priority enforced bit by bit | arbitration, by physics | DSCP/PCP is a *hint* a switch may ignore |
| Every node hears every frame | the bus is a broadcast medium | a switch forwards only where it believes a listener is |

**That table is the whole week.** Every automotive middleware — SOME/IP, DDS,
AUTOSAR Adaptive — exists to buy those four properties back in software. A
learner who has not watched them disappear will treat week 8's sequence
numbers, timeouts and retries as ritual.

### The physical layer is different too

Office Ethernet cable carries four twisted pairs in a shielded jacket with an
RJ45 plug. On a vehicle that is unacceptable: the wiring harness is the third
heaviest component after the engine and the body, RJ45 does not survive
vibration, oil and −40 °C, and the electromagnetic environment is far dirtier
than an office.

| 100BASE-TX (office) | 100BASE-T1 (automotive) |
|---|---|
| 4 pairs, shielded | **1 pair, unshielded** |
| RJ45 | small sealed connector |
| hubs and bus topology possible | **strictly point-to-point** |
| separate power or PoE | PoDL — power over that same pair |
| 100 m | 15 m |

"Strictly point-to-point" is the consequential row. CAN is a *bus*: add an ECU
and everybody hears it. 100BASE-T1 connects exactly two devices, so three ECUs
that need to talk require a **switch** — and the switch is where reordering,
silent drops and ignored priorities all come from.

## 2. Why it exists in a Cockpit DC

- **It is the backbone the cockpit hangs off.** Cameras, ADAS domain
  controllers, rear-seat displays and the diagnostics link all arrive over
  Ethernet. CAN reaches the cockpit for signals; Ethernet reaches it for
  everything else.
- **It changes what a bug looks like.** Week 4's failures announced
  themselves: bus-off, rising TEC/REC, error frames on the wire. Week 7's
  failures are *silent* — a join on the wrong NIC, a switch without IGMP
  snooping, a DSCP field erased in transit, a datagram that overtakes another.
  Nothing throws, nothing logs, tests stay green.
- **It is the layer SOME/IP sits on.** Week 8 is about serialisation and
  service semantics; it assumes a datagram pipe exists. Building the pipe
  first keeps the two questions apart — *how do bytes reach another ECU* is
  not the same question as *what do those bytes mean*.
- **Multicast is publish/subscribe one layer down.** The decoupling SOME/IP's
  event model provides at the service layer, IP multicast already provides at
  the network layer. Seeing it twice is what makes the second one obvious.

## 3. Architecture

```text
                      vehicle signals
                             │
                        dbc/cockpit.dbc  ── one file, three readers ──┐
                             │                                        │
                    ┌────────▼────────┐                               │
                    │     eth_gw      │  CanFrame                     │
                    │                 │      │                        │
                    │                 │  tunnel_encode                │
                    │                 │      │  + sequence number     │
                    └────────┬────────┘                               │
                             │ sendto()                               │
                    239.10.0.1:30490                                  │
                       TTL 1, DSCP 46                                 │
                             │                                        │
              ┌──────────────┼──────────────┐                         │
              │              │              │    switch replicates    │
              ▼              ▼              ▼    only to ports that   │
          eth_sub        eth_sub         (logger)   sent an IGMP      │
        (cluster)         (IVI)                     membership report │
              │                                                       │
        tunnel_decode ──► SequenceTracker ──► DBC decode ◄────────────┘
                                 │
                     Gap / Stale / Duplicate
```

Two facts the diagram is making:

1. **The publisher does not know its subscribers.** It sends once, to a group
   address. Adding a fourth consumer changes nothing in `eth_gw`.
2. **The sequence number is the only thing carrying ordering information.**
   Not the switch, not UDP, not the kernel.

### Why UDP, and not TCP or raw Ethernet

Raw Ethernet has MAC addresses only — enough to reach the right *machine*, not
the right *program*. A port number is what lets a cluster, an IVI and a logger
each receive their own stream on one NIC, and going through IP additionally
buys multicast, IGMP and every standard tool (`tcpdump`, Wireshark).

TCP is the tempting wrong answer. Consider one lost datagram in a 10 Hz stream:

```text
TCP                                     UDP
t=0.0  A (40 km/h)  ── lost ──✗         A lost, never seen
t=0.1  B arrives → held in kernel       B delivered at t=0.1
t=0.2  C arrives → held in kernel       C delivered at t=0.2
t=0.3  A retransmitted, arrives
       → A,B,C,D released together
```

Head-of-line blocking: B, C and D were in the receiving machine the whole time,
and TCP refused to hand them over to preserve order. On a cluster that is a
speedometer frozen for 300 ms and then jumping.

Underneath it is a deeper point: **the speed at t=0.0 has no value at t=0.3.**
Retransmission is precisely the wrong service for cyclic data — a file transfer
needs the missing byte, a signal stream needs the *next* sample. And TCP is
one-to-one; multicast does not exist on it.

So: UDP for detection, not correction. Buy what is needed (knowing a datagram
was lost or reordered), decline what is not (getting a stale one back).

## 4. C++ / Linux implementation

Three pieces, deliberately layered so each answers one question.

### `av_eth::UdpSocket` — how bytes reach another ECU

`socket(AF_INET, SOCK_DGRAM)`, then options that are four *separate* decisions:

| Call | Syscall | Decides |
|---|---|---|
| `bind_any(port)` | `SO_REUSEADDR` then `bind` | which port this socket owns |
| `join_multicast(group, iface)` | `IP_ADD_MEMBERSHIP` | which group, **and via which NIC** |
| `set_multicast_interface(iface)` | `IP_MULTICAST_IF` | which NIC *outgoing* multicast leaves by |
| `set_multicast_ttl(1)` | `IP_MULTICAST_TTL` | never leaves the local link |
| `set_dscp(46)` | `IP_TOS`, value `<< 2` | asks for priority; 2 low bits are ECN |

Three details worth defending:

- **`SO_REUSEADDR` before `bind`, not after** — the kernel reads the flag while
  binding, and without it a second subscriber on the same host fails with
  `EADDRINUSE`. Two subscribers on one machine is the normal case, not an edge
  case.
- **`send_to` refuses anything over `kMaxDatagram` (1472 = 1500 − 20 − 8)**
  rather than letting IP fragment. Losing *any* fragment discards the whole
  datagram, so a two-fragment message vanishes roughly twice as often.
- **`set_dscp` always succeeds** even when no switch honours it. This is the
  sharpest contrast with CAN in the whole week: arbitration is enforced by
  physics on every bit; DSCP is a request each switch may honour, remap or
  erase.

### `av_can::tunnel_*` — packing frames with a sequence number

```text
┌─ header, 16 bytes ─────────────────────────────────────────┐
│ 'A''V''E''T' │ ver │ count │ rsvd │ sequence u32 │ ms u32   │
└────────────────────────────────────────────────────────────┘
┌─ record, 16 bytes × count ─────────────────────────────────┐
│ id u32 (bit31 = extended) │ len │ flags │ rsvd │ 8 data     │
└────────────────────────────────────────────────────────────┘
```

- **Big-endian on the wire** so a big-endian ECU and a little-endian one agree
  without negotiating. CAN payload bytes are copied verbatim — their meaning
  belongs to the DBC, not to this layer.
- **Bit 31 as the extended-id flag** is the same convention week 6 met in the
  DBC's `BO_` line. One rule to remember, one off-by-one bug to recognise.
- **The length check on decode is exact, not "at least".** A datagram is
  atomic — one `recvfrom` returns one `send_to` — so a length disagreeing with
  `count` is malformed, never a partial packet awaiting more. A TCP-shaped
  mental model gets this wrong.
- **8 payload bytes per record, so CAN-FD does not fit.** A stated limitation,
  not a hidden one; week 8 replaces the format with SOME/IP serialisation.

### `SequenceTracker` — the three lines that matter

```cpp
const auto delta = static_cast<std::int32_t>(sequence - highest_);
if (delta == 0) { return Duplicate; }
if (delta < 0)  { return Stale; }     // reordered — must not be applied
```

Subtracting first, then casting to signed, is what makes the comparison
**wrap-safe**. Plain `sequence < highest_` is correct for 4.29 billion
datagrams and then wrong forever: at the wrap, sequence 0 reads as older than
4294967295 and the receiver silently rejects everything from that moment on.
No short test run would ever catch it, which is why
`SurvivesTheThirtyTwoBitWrap` exists.

Stale datagrams are **dropped, not buffered**. For a signal stream a newer
value has already superseded the late one, so reordering it back into place
would only display something older, later. A file transfer needs the opposite
policy — which is exactly why this decision belongs to the application and not
to the transport.

## 5. Hands-on exercise

```bash
# terminal 1
./build/debug/apps/eth_sub/eth_sub --naive

# terminal 2
./build/debug/apps/eth_gw/eth_gw --reorder 3 --cycles 9
```

No `sudo`, no `vcan0`. The gateway's generated speed only ever climbs, so any
decrease on screen came from the network.

```text
[rx] seq=   3  speed=  41.5  ** datagram(s) LOST **
[rx] seq=   2  speed=  41.0  ** stale, applied anyway **   <<< SPEED WENT BACKWARDS
```

Now drop `--naive` and run the identical gateway:

```text
[rx] seq=   4  speed=  42.0  ** datagram(s) LOST **
[rx] seq=   3                 STALE -- dropped (an older reading arrived late)

speed went backwards: 0
```

Same datagrams, same order of arrival, opposite outcome. Run two subscribers at
once — one plain, one `--naive` — against a single gateway, and the two windows
disagree while receiving byte-identical traffic. (That both can bind port 30490
is `SO_REUSEADDR` doing its job.)

Then the real chain, once `sudo ./scripts/setup_vcan.sh` has run:

```bash
./build/debug/apps/sim_vehicle/sim_vehicle vcan0
./build/debug/apps/eth_gw/eth_gw --can vcan0 --reorder 5
./build/debug/apps/eth_sub/eth_sub
```

## 6. Failure / debugging exercise

The point of the week is that **none of these produce an error**.

| Fault | Inject it by | What you see | Where the evidence is |
|---|---|---|---|
| Reordering applied blindly | `eth_sub --naive` | speed steps backwards | `SequenceTracker` already said `Stale` — nobody read it |
| Join on the wrong NIC | `eth_sub --iface 0.0.0.0` on a multi-NIC host | subscriber silent forever | `setsockopt` returned 0; `ip maddr show` reveals the group is on another interface |
| TTL too low for the topology | `set_multicast_ttl(0)` | local delivery works, remote does not | `tcpdump -i <nic> host 239.10.0.1` on the far side is empty |
| Wrong group or port | `--group 239.10.0.2` | silence | `tcpdump` sees traffic; the socket does not |
| Foreign traffic on the group | send arbitrary bytes to the port | one warn line, no crash | `tunnel_decode` rejects on magic — this is why the magic exists |
| Fragmentation | try to send > 1472 | `send_to` refuses | logged as `datagram would fragment` |
| Sequence wrap | `SequenceTracker` from `0xFFFFFFFE` | with a naive `<`, permanent silence | covered by `SurvivesTheThirtyTwoBitWrap` |

Trace discipline (CLAUDE.md §9) for "the cluster shows nothing":

```text
gateway sending?          → its own stdout, then `tcpdump -i lo -n port 30490`
leaving the host?         → tcpdump on the sending NIC
arriving at the host?     → tcpdump on the receiving NIC
reaching the socket?      → `ss -unlp | grep 30490`, `ip maddr show`
parsed?                   → the "not a tunnel datagram" warn line
accepted by sequence?     → Stale/Duplicate counters in the summary
decoded?                  → does the DBC have that id
```

The first question is never "is the UI broken".

## 7. Senior interview questions

1. Why did vehicles add Ethernet, and why did CAN not disappear?
2. Office Ethernet uses four twisted pairs. Why does 100BASE-T1 use one, and
   what does that force about topology?
3. Name the four guarantees CAN provides that UDP does not. Where is each one
   bought back?
4. A speed signal is published at 10 Hz. Argue for UDP over TCP, then say what
   the application must add as a result.
5. What is head-of-line blocking, and why is it worse than packet loss for
   cyclic data?
6. How does DSCP differ from CAN arbitration? Which is enforced, and by what?
7. Why is `SO_REUSEADDR` mandatory for multicast subscribers?
8. A subscriber receives nothing and no error is logged anywhere. Give five
   candidate causes and the command that distinguishes each.
9. Why does `send_to` refuse a 1500-byte payload instead of letting IP
   fragment it?
10. Show the wrap-safe sequence comparison and explain what breaks without it.
11. Why are stale datagrams dropped rather than reordered into place? When
    would the opposite be right?
12. Why does the tunnel header carry a magic number at all?

## 8. Review checklist

- [ ] I can state the four lost guarantees without looking.
- [ ] I can explain 100BASE-T1's single pair in terms of harness weight and
      point-to-point topology.
- [ ] I can justify UDP over TCP with the head-of-line argument, using a
      concrete 10 Hz example.
- [ ] I can derive 1472 and say why fragmentation is worse than refusal.
- [ ] I can explain what `IP_ADD_MEMBERSHIP` puts on the wire and why the
      interface argument matters.
- [ ] I can write the wrap-safe sequence comparison from memory.
- [ ] I can name five silent failure modes and the command that exposes each.
- [ ] I have run the demo both ways and seen the speed walk backwards.

---

**Still open at the end of week 7:** VLAN (segmentation, 3-bit PCP), TSN
(bandwidth reservation and time-scheduled queues) and PTP (IEEE 1588 clock
sync, needed both for sensor fusion timestamps and for TSN scheduling) are
named here but not implemented — they are switch and NIC configuration rather
than application code. Know what problem each solves; that is the level an
interview asks at.
