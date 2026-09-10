# Week 9 — Service Discovery: deleting the address

## 1. Concept

Week 8's client worked because a header told it where to go:

```cpp
inline constexpr std::uint16_t kMethodPort = 30509;      // vehicle_service.hpp
inline constexpr const char*   kEventGroup = "239.10.0.2";
```

That is a compiled-in belief about the world. Service Discovery replaces it
with a conversation:

```text
server ──OfferService (cyclic, TTL 3 s)──► 239.10.0.9:30490
client ──FindService─────────────────────► 239.10.0.9:30490
server ──OfferService (unicast, at once)─► the client that asked
```

The offer **carries the endpoints**, so the client learns them instead of
being built with them. Exactly one address still has to be agreed in advance —
the SD group itself, because discovery cannot discover itself — and it is the
same on every vehicle for every service.

### The lease is the whole idea

An offer is not a fact. It is a lease valid for TTL seconds, and the server
must keep renewing it.

```text
t=0.0  OfferService ttl=3     client: AVAILABLE
t=1.0  OfferService ttl=3     lease extended
t=2.0  OfferService ttl=3     lease extended
       ...server is killed, and says nothing...
t=5.0  (nothing arrives)      client: UNAVAILABLE
```

Nothing was sent at t=5.0. The client learned the server died **from silence**,
which no single message can express. This is the one failure detector in the
system that works when a peer is absent rather than merely wrong — and it is
what week 8's timeout could not do.

Renewing the lease *is* the heartbeat. There is no separate liveness protocol,
and there does not need to be.

### The message

SD is an ordinary SOME/IP Notification: service `0xFFFF`, method `0x8100`. Week
8's `serialize()`/`deserialize()` carry it with no special case, and only the
payload is new.

```text
┌─ SD payload ───────────────────────────────────────────────┐
│ Flags (1)  bit7 Reboot, bit6 Unicast    │ Reserved (3)      │
│ Length of Entries Array (4)                                 │
│ Entry [16] · Entry [16] · ...                               │
│ Length of Options Array (4)                                 │
│ Option [12] · Option [12] · ...                             │
└─────────────────────────────────────────────────────────────┘
```

Entries and options are separate arrays because several entries usually share
one endpoint. An entry points into the options array by **index and count** —
the count being four bits, packed two per byte.

## 2. Why it exists in a Cockpit DC

Three failures, all ordinary, none solvable by a constant:

| Situation | Without SD | With SD |
|---|---|---|
| The ECU boots after the cluster | calls into a void; timeouts that cannot separate "not yet" from "never" | the client waits in SEARCHING, calls nothing, then finds it |
| The ECU moves — new board, new address | recompile every client | nothing changes; the offer says where it is now |
| The trim has no rear display | permanent timeouts against something that does not exist | the service is simply never offered |

The third is the argument that survives an interview. A vehicle line builds a
dozen variants from one software image. Which services exist is a
**deployment** fact, and a deployment fact must not be a compile-time constant.

That is why `vehicle_service.hpp` now has a line through it:

```text
above the line   service id, method ids, interface version   the contract
below the line   ports, multicast groups                     the deployment
```

The ids are the same on every vehicle and nobody discovers them. The addresses
are not, and the server is the only process that actually knows its own.

**Availability owns resources, not just log lines.** Once the event group is
learned rather than compiled in, the IGMP membership belongs to the service:
joined when it appears, dropped when the lease expires. A subscription that
outlives its service is a switch flooding a port nobody reads.

## 3. Architecture

```text
┌──────── svc_client ────────┐              ┌──────── svc_server ───────┐
│ SD socket :30490           │──FindService─►                           │
│   joined 239.10.0.9        │◄─OfferService─  SD socket :30490         │
│                            │              │   offers every 1 s, TTL 3 │
│ socket A  unbound          │──REQUEST────►│                           │
│   (ephemeral, for replies) │◄──RESPONSE───│  socket :30509            │
│                            │              │                           │
│ socket B  created ONLY     │◄─NOTIFICATION│  (the same socket)        │
│   once an offer names it   │              │                           │
└────────────────────────────┘              └───────────────────────────┘
```

Week 8's asymmetry survives — the server has one traffic socket, the client
two — and SD adds a third to each. The new part is socket B's **lifetime**: it
does not exist at startup, is created when an offer names a group, and is
destroyed when the lease ends.

Client states:

```text
        ┌──────────► SEARCHING ──────────┐
        │        (FindService, 1 Hz)     │  offer arrives
   lease expires                         ▼
   or StopOffer  ◄────────────────── AVAILABLE
                                (call, subscribe, listen)
```

No call is attempted while searching. That narrows what a timeout *means*:
week 8's also covered "the server does not exist"; here the registry has
already established that it does, so a timeout means a lost datagram or a
wedged server. A smaller set of causes is what makes an error message useful.

## 4. C++ / Linux implementation

### The length trap, second instance

```cpp
constexpr std::uint16_t kIpv4OptionLength = 9;   // and the option occupies 12
```

The Length field counts neither itself nor the Type byte that follows it, so an
option is `declared + 3` bytes long. Week 8 had the same shape of bug (SOME/IP's
Length counts from the Request ID). Getting it wrong is invisible in a
round-trip test and invisible with one option: the *first* option parses fine,
and everything after it is misaligned.

That is why the test asserts bytes:

```cpp
0x00, 0x09,              // length 9  <- the option occupies 12
0x04,                    // IPv4 unicast endpoint
0x00,                    // reserved
0x7F, 0x00, 0x00, 0x01,  // 127.0.0.1
0x00,                    // reserved
0x11,                    // L4 = UDP
0x77, 0x2D,              // port 30509
```

### One entry, two option types

`0x04` is a unicast endpoint and `0x14` a multicast group — different option
*types*, not a flag. That is how one offer says "call me here, listen for my
events over there" in a single entry, and how a client learns both of week 8's
constants at once.

### The registry separates bytes from meaning

```cpp
Availability observe(const Entry& entry, Clock::time_point now);
std::vector<Change> expire(Clock::time_point now);
```

The clock is a parameter, not a call to `now()` inside. A lease expiring is
then tested in microseconds instead of by sleeping through three seconds — and
a test that waits three seconds is a test nobody runs.

A renewal returns `Unchanged`, not `Available`. Reporting news on every cyclic
offer would make a client re-run its "the service came up" logic twice a
second.

### TTL is three times the interval

```cpp
constexpr auto kOfferInterval = std::chrono::seconds{1};
constexpr std::uint32_t kOfferTtl = 3;
```

Equal values would mean one lost offer expires the lease and every client
briefly believes the server died. Three tolerates two consecutive losses.
Larger, and a real crash takes proportionally longer to notice. The whole
availability/responsiveness trade is this one ratio.

### The bug the run found, and reading would not have

Reboot detection compares the reboot flag against the last session id seen from
a peer. The first version kept **one** counter and applied it to every SD
message. But the client joins the SD group with loopback on, so it receives its
own `FindService` — and compared its own session ids with the server's:

```text
[cli]  !  REBOOT  127.0.0.1 restarted: session went 2 -> 1
```

printed before the first offer had even arrived. Two fixes, both principled:
state is kept **per sender**, and only messages that actually **offer**
something are tracked. A question carries no state worth remembering.

This is exactly the trap `udp_socket.hpp` warned about in week 7 — *"a program
that both publishes and subscribes sees its own traffic and mistakes it for the
peer's"* — collected in practice two weeks later.

### The reboot flag alone proves nothing

It means "my session ids have not wrapped yet", which is still true of a server
that has been up for an hour. A restart is the flag *together with* a session
id that went backwards.

## 5. Hands-on exercise

Start the **client first**. That is the point.

```bash
# terminal 1
./build/debug/apps/svc_client/svc_client

# terminal 2, a few seconds later
./build/debug/apps/svc_server/svc_server
```

```text
[cli]  ?  FindService  service=0x1234  -> 239.10.0.9:30490   (still searching)
[cli]  ?  FindService  service=0x1234  -> 239.10.0.9:30490   (still searching)
[cli] +++ AVAILABLE    methods at 127.0.0.1:30509   events at 239.10.0.2:30510
[cli]  +  subscribed to 239.10.0.2:30510   (IGMP join)
[cli] --> 0x0001()  session=    1  to 127.0.0.1:30509
[cli] <-- RESPONSE    41.0   (71 us)
```

Then compare the two ways a service disappears.

```text
# Ctrl-C the server -- a clean goodbye
[cli] !!! UNAVAILABLE  StopOffer received -- the server withdrew it on purpose
[cli]  -  left 239.10.0.2:30510

# kill -9 the server -- no goodbye at all
[cli] !!! TIMEOUT  session=8  after 300 ms
[cli] !!! TIMEOUT  session=9  after 300 ms
[cli] !!! UNAVAILABLE  service=0x1234 -- the lease ran out, nobody said goodbye
```

| | latency | failed calls |
|---|---|---|
| StopOffer | immediate | **0** |
| crash | ~3 s (the TTL) | **2** |

That table is the week. Restart the server afterwards and watch:

```text
[cli]  !  REBOOT  127.0.0.1:30490 restarted: session went 5 -> 1
```

Then:

```bash
./build/debug/apps/svc_client/svc_client --static     # week 8: believe the constant
./build/debug/apps/svc_server/svc_server --no-sd      # exist, but never offer
```

`--static` against `--no-sd` works, because both sides were built with the same
numbers. The ordinary client against `--no-sd` searches forever — correctly,
because as far as the network is concerned the service is not there.

## 6. Failure / debugging exercise

| Fault | Inject it by | What the client sees | Where the evidence is |
|---|---|---|---|
| Service not offered | `svc_server --no-sd` | `FindService` forever, no calls | nothing to see, which is the answer |
| Clean shutdown | Ctrl-C the server | `UNAVAILABLE` at once, 0 timeouts | the StopOffer, TTL 0 |
| Crash | `kill -9` the server | 2 timeouts, then `UNAVAILABLE` | the lease, not a message |
| Server restarted | stop and start it | `REBOOT`, session went backwards | the flag *and* the session id |
| Wrong option Length | a peer's bug | `IPv4 option with the wrong length` | `parse_options`, on the receiving side |
| Entry indexes options that are absent | a peer's index/count bug | `entry references options that are not there` | the entry is dropped, the message is not |
| Unknown option type | a newer supplier ECU | nothing — it is skipped | deliberate: refusing it would break the day they upgrade |
| Membership outlives the service | delete the `unsubscribe` call | events keep arriving from a service that is gone | `ip maddr show` still lists the group |

Trace discipline (CLAUDE.md §9) for "the cluster never finds the speed service":

```text
is anything offering?          tcpdump -i lo -n port 30490
is the client asking?          its own "FindService" lines
same SD group?                 ip maddr show  -- 239.10.0.9 on both
parsed as SD?                  service id 0xFFFF, method 0x8100
right service id in the entry? 0x1234, and the instance
TTL sane?                      0 is a StopOffer, not an offer
options resolved?              "entry references options that are not there"
endpoint reachable?            the address in the offer, not the one you assumed
```

Note where the last line points. With SD, "the address you assumed" stops being
the thing to check, and "the address it advertised" starts.

## 7. Senior interview questions

1. Why is a compiled-in endpoint acceptable in a lab and not on a vehicle?
   Give three distinct failures.
2. Why is an offer a lease rather than a fact? What does the TTL buy that a
   one-shot announcement cannot?
3. How does a client learn that a server crashed, when a crash by definition
   sends nothing?
4. StopOffer versus a lease expiring: same outcome, different cost. Quantify
   the difference from the demo.
5. How would you choose the offer interval and the TTL? What does the ratio
   trade?
6. What does the SD option Length field count, and where does the error first
   appear when a peer gets it wrong?
7. What is the reboot flag for, and why is it not sufficient on its own?
8. Why must reboot state be kept per sender? What broke when it was not?
9. What belongs in a generated interface header and what does not? Defend the
   line drawn in `vehicle_service.hpp`.
10. Why does the client's event socket not exist at startup?
11. Why does the standard ramp the FindService retry rate instead of using a
    fixed interval?
12. With SD in front of it, what does a request timeout now mean that it did
    not mean in week 8?

## 8. Review checklist

- [ ] I can explain why SD exists in one sentence about vehicle variants.
- [ ] I can draw the SD payload: flags, entries array, options array.
- [ ] I can explain what an entry's index/count fields point at.
- [ ] I can state what the option Length field counts, and what breaks if not.
- [ ] I can explain a lease, and why silence is the only crash detector.
- [ ] I can give the interval/TTL ratio and defend the number.
- [ ] I can explain StopOffer and quantify what it saves.
- [ ] I can explain reboot detection and why the flag alone is not enough.
- [ ] I can say what a timeout means now that discovery is in front of it.
- [ ] I have run the client before the server, and killed the server both ways.

---

**Still open at the end of week 9.** Two things this week names and does not
implement, recorded so the gap is not mistaken for completeness:

- **SubscribeEventgroup / SubscribeEventgroupAck.** The entry types exist in
  `sd.hpp` and are parsed, but this client subscribes by joining a multicast
  group directly, which the server never learns about. A full stack has the
  client *ask* to subscribe and the server acknowledge — which is what makes a
  per-client TCP event path, and eventgroup-level access control, possible.
- **`vsomeip` (COVESA), still deferred.** Carried over from week 8. Its routing
  manager is the design decision this repository has not had to make: one SD
  daemon per ECU that every process talks to locally, instead of every process
  opening its own SD socket the way these two do. That is the natural opening
  of week 10.

**Next:** week 10, middleware architecture. The three weeks behind this one
produced a wire format, a service, and a way to find it — as three programs
that each do their own socket setup. Week 10 asks what the *boundary* should
be: what an application sees, who owns the threads, what happens under
backpressure, and where timeout and retry policy lives. Then `av_service` gets
refactored to match the answer.
