# Week 8 — SOME/IP: from broadcasting signals to calling services

## 1. Concept

Weeks 4 to 7 could do exactly one thing: **announce**. A CAN frame is a label
on a broadcast, and week 7's UDP tunnel changed the wire without changing that
— `eth_gw` published, and whoever listened, listened. Nobody could ask a
question.

```text
weeks 4-7    CAN ID  ──►  message  ──►  signal
week 8       service ──►  method / event / field
```

A SOME/IP message names a **service**, says what to do with it, and carries
**who asked** and **which attempt this is**:

```text
┌─ 16 bytes, big-endian throughout ──────────────────────────────┐
│  Message ID   │ Service ID (2) │ Method ID (2)                 │
│  Length       │ 4 bytes -- see the trap below                  │
│  Request ID   │ Client ID (2)  │ Session ID (2)                │
│  Proto ver 1  │ Iface ver 1    │ Msg type 1 │ Return code 1    │
└────────────────────────────────────────────────────────────────┘
```

Two traps live in that box, and both stay invisible until a foreign stack
appears:

- **The Length field does not measure the message.** It counts from the Request
  ID onward: 8 header bytes plus the payload. An empty payload declares 8, not
  16 and not 0. Two implementations that are wrong the same way interoperate
  perfectly with each other and with nobody else.
- **Everything is big-endian**, which collides head-on with week 6:
  `cockpit.dbc`'s `@1` signals are Intel byte order. Opposite conventions, one
  process, and no compiler warning if they are confused.

Method ids split by bit 15: `0x0000-0x7FFF` are methods, `0x8000-0xFFFF` are
events. Not a separate field — a convention of the standard.

### These are not three competing choices

A recurring confusion worth settling early, because it is an interview question
in disguise:

```text
        SOME/IP          application protocol
           │
        UDP / TCP        transport
           │
          IP
           │
       Ethernet          the wire
```

**SOME/IP runs on UDP.** They are not alternatives. `apps/svc_server` links
both `av_service` and `av_eth` at the same time, because one sits on the other.
CAN is the genuine alternative, because it is a different wire entirely.

So there are two questions, not three options:

| Question | Answer |
|---|---|
| CAN or Ethernet? | Small + cyclic + safety-related → CAN. Large or on-demand → Ethernet. |
| On Ethernet, what protocol? | Service semantics, several teams, discovery → SOME/IP. One-way bulk stream you own both ends of → raw UDP/RTP. Must-be-intact file → TCP. |

And SOME/IP itself then chooses: **events over UDP** (a lost sample is replaced
by the next one), **large method payloads over TCP** (nothing replaces a
missing byte). That is week 7's head-of-line argument, reused.

## 2. Why it exists in a Cockpit DC

**The startup problem.** A cluster powers on and must show the fuel level *now*.

```text
CAN:      0.0s  cluster boots -- screen blank, it knows nothing
                ...waits...
          1.0s  the fuel ECU's next cyclic frame arrives

SOME/IP:  0.0s  cluster boots
          0.0s  GetFuelLevel()  ──►
          0.0s  ◄── 42.5 %                (85 us later, measured)
```

There is no way to ask on CAN. Every cockpit therefore needs both shapes:
`GetSpeed()` for *right now* — at boot, or when a user opens a screen — and
`OnSpeedChanged` for the continuous stream while driving.

**Failures acquire names.** Send an unwanted id on CAN and the result is total
silence, indistinguishable from a healthy bus. SOME/IP answers with a reason:
`E_UNKNOWN_SERVICE` (nothing here), `E_UNKNOWN_METHOD` (service present,
function absent), `E_NOT_READY`, `E_WRONG_INTERFACE_VERSION`.

**A new failure appears too: the timeout.** On a bus no question is ever
outstanding, so nothing can time out. Here it can — and it is *ambiguous*: the
client cannot distinguish a lost request from a lost reply from a dead server.
Three causes, one symptom.

**It is a contract between organisations.** Service ids are generated from
ARXML or a Franca `.fidl`, not typed by hand, because client and server are
built by different companies. This is the same role the DBC plays for CAN — and
the reason `vehicle_service.hpp` lives in `av_service` rather than an
`apps/common/` header. Week 6 deleted `apps/common` because it *duplicated* the
DBC; this is the opposite case, where nothing else states these numbers.

## 3. Architecture

```text
┌─────── svc_client ────────┐              ┌─────── svc_server ────────┐
│                           │              │                           │
│ socket A  unbound         │──REQUEST────►│                           │
│           ephemeral port  │◄──RESPONSE───│  socket bound :30509      │
│                           │              │                           │
│ socket B  bound :30510    │◄─NOTIFICATION│  (the same socket)        │
│           joined 239.10.0.2              │                           │
└───────────────────────────┘              └───────────────────────────┘

    method   unicast    client ──► server ──► client    somebody asked
    event    multicast  server ──► group                nobody asked
```

**Why the client needs two sockets and the server needs one.** The server only
ever *sends* outward: the response goes to `result.from`, an address that
arrived inside the request; the event goes to a constant group address. One
socket does both. The client must *receive* in two different places — the
response returns to socket A's kernel-assigned ephemeral port, the event
arrives at the agreed port 30510 on a joined group. One socket cannot both
leave its port free for replies and bind hard to 30510.

Forcing both through one socket is possible. Keeping them apart puts the two
communication models in the code instead of only in the comments.

## 4. C++ / Linux implementation

### The reply echoes the question

```cpp
Header reply = request;      // copy everything
reply.type = type;           // then change exactly three things
reply.code = code;
reply.interface_version = version;
```

Service id, method id, client id and session id are **echoed**, because they
identify the question. Only type, return code and payload are the answer. The
server never invents a session id.

### Session matching is the whole point

```cpp
if (!pending || pending->session != message.header.session_id) {
    // "IGNORED: no call is waiting for this (late reply)"
    return;
}
```

Without this, a late reply to a call already abandoned would be applied to the
*next* call: ask for the speed at t=5 s, receive the value from t=3 s, believe
it. That is week 7's stale datagram wearing a different hat — and the Session
ID is week 7's sequence number, promoted into the standard.

`SessionCounter` wraps to **1**, not 0, because 0 is reserved to mean "this
endpoint does not do session handling". Wrapping to 0 would silently announce
that ordering had stopped being tracked.

### Reader returns optional, never a default

```cpp
std::optional<float> get_f32() noexcept;
```

A truncated payload is a normal thing to receive from a network. A reader that
returns 0 for "missing" is indistinguishable from one that read a real 0.

### `deserialize` refuses rather than guesses

Short buffer, unknown protocol version, a message type outside the five
(`0x20` is SOME/IP-TP — a real type, but a *fragment*, not a whole message),
and a Length that disagrees with the bytes present. That last check is where a
peer's Length bug surfaces, on the receiving side, instead of three layers up
as "the payload looks like noise".

### The bug the byte-level test caught

`deserialize` read the protocol version from offset 8 and the message type from
offset 10 — both inside the Request ID. A round-trip test would have **passed**,
because encode and decode would have been wrong together. Only pinning the
bytes found it:

```cpp
0x00, 0x00, 0x00, 0x0C,  // length = 8 + 4 payload  <- NOT 20, NOT 4
```

That is the argument for testing a wire format by its bytes and never by its
round trip.

## 5. Hands-on exercise

```bash
# terminal 1
./build/debug/apps/svc_server/svc_server

# terminal 2
./build/debug/apps/svc_client/svc_client
```

```text
[cli] --> 0x0001()  session=    2
[cli] <-- RESPONSE   176.0   (85 us)
[cli]  ~  EVENT      176.5   session=53  (nobody asked)
[cli]  ~  EVENT      179.0   session=54  (nobody asked)

calls made     : 4
answered       : 4
events received: 8   <- these needed no call at all
```

The `-->` lines and the `~` lines are two different worlds sharing one wire
format. Then:

```bash
./build/debug/apps/svc_client/svc_client --method 0x0009   # E_UNKNOWN_METHOD
./build/debug/apps/svc_client/svc_client --events-only     # subscribe, never ask
./build/debug/apps/svc_server/svc_server --no-events       # methods only
./build/debug/apps/svc_server/svc_server --wrong-version   # payload not decoded
```

And with the server stopped:

```text
[cli] !!! TIMEOUT  session=    2  after 300 ms -- no answer, and no way to know why
timed out      : 3
```

## 6. Failure / debugging exercise

| Fault | Inject it by | What the client sees | Where the evidence is |
|---|---|---|---|
| Method not implemented | `--method 0x0009` | `ERROR E_UNKNOWN_METHOD` in ~60 us | the server's `switch` default |
| Wrong service on the port | any other service id | `ERROR E_UNKNOWN_SERVICE` | the server's first check |
| Interface changed | `svc_server --wrong-version` | response arrives, payload **not decoded** | a version mismatch means the layout is no longer agreed |
| Server gone | stop `svc_server` | `TIMEOUT` after 300 ms, three causes indistinguishable | nothing — that is the point |
| Late reply | none needed; raise the load | `IGNORED: no call is waiting for this` | the session id did not match `pending` |
| Length computed over the whole message | a peer's bug | `length field disagrees with the bytes received` | `deserialize`, on the receiving side |
| Events silent | `svc_server --no-events` | methods keep working, `~` lines stop | proves the two paths are independent |

Trace discipline (CLAUDE.md §9) for "the cluster shows a stale speed":

```text
is the server publishing?     its own stdout
leaving the host?             tcpdump -i lo -n port 30510
joined the group?             ip maddr show
parsed?                       the "not a SOME/IP message" warn line
right message type?           Notification vs Response
session advancing?            the session id printed per event
decoded?                      payload long enough to hold a float
```

## 7. Senior interview questions

1. What can a service-oriented protocol express that a signal-oriented bus
   cannot? Give a concrete cockpit example.
2. SOME/IP or UDP — which do you choose? (The question is a trap; explain why.)
3. When does SOME/IP use UDP and when TCP, and what is the reasoning?
4. What exactly does the Length field count? What happens when a peer gets it
   wrong, and where does the error first become visible?
5. Why is everything big-endian, and where in this repository does that clash
   with an opposite convention?
6. What is the Session ID for? Relate it to week 7.
7. Why does `SessionCounter` wrap to 1 rather than 0?
8. A client sends three requests and receives one reply. How does it know which
   request was answered?
9. Distinguish `E_UNKNOWN_SERVICE`, `E_UNKNOWN_METHOD` and a timeout. Which is
   ambiguous, and why does that matter?
10. Why does the client open two sockets while the server opens one?
11. Why must a version mismatch stop the payload being decoded rather than
    decoding it anyway?
12. Why test a wire format by asserting bytes instead of by round-tripping it?

## 8. Review checklist

- [ ] I can draw the 16-byte header from memory, including what Length counts.
- [ ] I can explain why "SOME/IP vs UDP" is the wrong question.
- [ ] I can give the CAN / Ethernet decision rule and two examples each way.
- [ ] I can state the boot-time argument for methods in one sentence.
- [ ] I can explain Session ID by relating it to week 7's sequence number.
- [ ] I can name three return codes and say what each rules out.
- [ ] I can explain why a timeout is ambiguous and what that forces on the
      application.
- [ ] I have run the client with `--method 0x0009` and with the server stopped.

---

**Still open at the end of week 8:** reading a real stack. The ROADMAP names
`vsomeip` (COVESA) as the code to read, and this note is written from the
specification and from the implementation here, not from that source. Worth
doing before week 10's middleware design note, because vsomeip's routing
manager is the design decision this repository has not yet had to make.

**Next:** week 9. The client currently knows the server is at
`127.0.0.1:30509` because `vehicle_service.hpp` says so. On a vehicle that
fails the moment an ECU boots late, moves, or is not fitted. Service Discovery
replaces the constant with `OfferService` / `FindService`, and with it come
availability, timeout and retry.
