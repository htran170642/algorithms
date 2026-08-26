# CLAUDE.md — Cockpit Domain Controller / Automotive C++ Learning Roadmap

## Purpose

This repository is a learning lab for becoming productive as a Senior C++ Engineer working on a **Cockpit Domain Controller (Cockpit DC)** / automotive cockpit platform.

Primary architecture:

```text
                    Cockpit Domain Controller
                              |
                         High-Performance SoC
                              |
                         Hypervisor
                              |
             +----------------+----------------+
             |                |                |
            QNX             Linux           Android
             |                |                |
          Cluster         Services             IVI
             |                |                |
             +----------------+----------------+
                              |
                          Middleware
                              |
                 +------------+------------+
                 |                         |
              CAN-FD                    Ethernet
                 |                         |
            Vehicle Signals             SOME/IP
                 |                         |
                 +------------+------------+
                              |
                       Vehicle Services
                              |
                           Qt/QML
                              |
                              HMI
```

The goal is not to learn every automotive technology equally. The target is to understand how C++ software participates in a real Cockpit DC from vehicle networking through middleware and OS boundaries to the HMI.

---

## 1. Learning Strategy

Use this loop for every topic:

```text
30% Theory
20% Read real code
40% Implement
10% Senior interview questions
```

Prefer:

```text
Understand -> Implement -> Break -> Observe -> Debug -> Explain
```

Every topic should produce a concrete artifact: code, test, trace, benchmark, assembly evidence, design note, or debugging exercise.

---

# 2. Roadmap

## Phase 1 — C++ / Linux Foundations

### Week 1 — Modern C++17

Focus:

- RAII
- object lifetime
- Rule of 3/5/0
- copy/move semantics
- RVO / NRVO / copy elision
- smart pointers
- STL containers
- templates
- `constexpr`
- `noexcept`
- undefined behavior
- alignment
- cache awareness
- `volatile`
- `std::atomic`
- C++ memory model

Must explain:

- Why `std::vector` move is usually O(1)
- Why `std::array` move is O(N)
- `volatile` vs `atomic`
- object lifetime
- dangling pointers
- iterator invalidation
- exception-safety guarantees

Practical:

- implement a small vector
- implement Rule of 5
- create compiler/assembly evidence tests
- compare inline vs heap storage

### Week 2 — Concurrency

Focus:

- `std::thread`
- mutexes
- condition variables
- futures/promises
- atomics
- memory ordering
- lock-free programming
- CAS
- ABA
- producer/consumer
- false sharing
- starvation
- deadlock
- priority inversion

Memory ordering:

```text
relaxed
acquire
release
acq_rel
seq_cst
```

Practical:

- bounded blocking queue
- producer/consumer pipeline
- atomic counter
- lock-free SPSC queue
- race-condition demonstration

### Week 3 — Linux System Programming

Focus:

- process vs thread
- virtual memory
- ELF
- shared libraries
- dynamic linking
- system calls
- file descriptors
- signals
- `epoll`
- `mmap`
- `ioctl`
- `/proc`
- `/sys`
- scheduling
- CPU affinity

IPC:

- pipe
- Unix Domain Socket
- shared memory
- message queue
- socket

Practical:

```text
Process A -- Unix Domain Socket --> Process B
Process A -------- Shared Memory --> Process B
```

---

# 3. Phase 2 — Automotive Networking

## Week 4 — CAN

Learn:

- CAN architecture
- CAN controller
- CAN transceiver
- CAN frame
- CAN ID
- DLC
- CRC
- ACK
- bit stuffing
- dominant / recessive
- arbitration
- error detection
- error active
- error passive
- bus-off

Must explain:

- Why CAN arbitration is non-destructive
- Why a lower CAN ID normally has higher priority

Practical:

- create CAN frames
- decode frame fields
- simulate arbitration

## Week 5 — CAN-FD + DBC + SocketCAN

Learn:

- Classic CAN vs CAN-FD
- payload differences
- bit-rate switching
- CAN-FD frame
- DBC
- message
- signal
- start bit
- length
- factor
- offset
- endianness
- range
- unit

Linux path:

```text
C++ Application
      |
   SocketCAN
      |
CAN Interface
      |
CAN Controller
```

Practical:

```text
Vehicle Simulator
      |
     CAN
      |
 SocketCAN
      |
C++ Receiver
      |
DBC Decoder
      |
Vehicle Signal
```

## Week 6 — Automotive Ethernet

Learn:

- Ethernet fundamentals
- MAC
- ARP
- IP
- TCP
- UDP
- multicast
- VLAN
- QoS
- bandwidth
- latency
- packet loss

Then:

- Automotive Ethernet
- SOME/IP
- Service Discovery
- PTP
- TSN basics

Priority:

```text
CAN-FD + Automotive Ethernet + SOME/IP
```

---

# 4. Phase 3 — Cockpit Middleware

## Week 7 — SOME/IP

Understand the shift:

```text
Traditional:
CAN ID -> Message -> Signal

Modern:
Service -> Method / Event / Field
```

Learn:

- service
- method
- event
- field
- request/response
- event notification
- Service Discovery
- OfferService
- FindService
- serialization
- timeout
- retry
- availability

Example:

```text
VehicleService
├── GetSpeed()
├── GetRPM()
├── GetSteeringAngle()
└── SubscribeVehicleSpeed()
```

Practical: implement a simplified service-oriented middleware before studying a full SOME/IP stack.

## Week 8 — IPC + Middleware Architecture

Focus:

- synchronous vs asynchronous communication
- request/response
- publish/subscribe
- queueing
- shared memory
- sockets
- serialization
- backpressure
- timeout
- retry
- fault handling
- latency
- throughput

Target:

```text
CAN Receiver
      |
Signal Manager
      |
Vehicle Data Model
      |
Middleware
      |
Vehicle Service
      |
Cockpit Application
```

Practice API boundaries, ownership, thread models, failure models, logging, and observability.

---

# 5. Phase 4 — Cockpit HMI

## Week 9 — Qt / C++

Focus:

- `QObject`
- signals/slots
- event loop
- `QThread`
- `QTimer`
- `QSocketNotifier`
- `QProcess`
- `QVariant`
- `Q_PROPERTY`
- model/view
- C++ / QML boundary

Critical question:

> Why must GUI work not block the event loop?

Practical:

```text
Vehicle Data -> Qt Model -> QML
```

## Week 10 — QML

Learn:

- properties
- bindings
- signals
- models
- `ListModel`
- `ListView`
- anchors
- states
- animations
- C++ integration

Build a small cluster showing:

- speed
- RPM
- temperature
- door status
- warnings

## Week 11 — Android Automotive / AAOS

Do not aim to become a full Android developer.

Focus on:

```text
Application
     |
Android Framework
     |
System Services
     |
HAL
     |
Linux Kernel
     |
Hardware
```

Learn:

- AAOS architecture
- Android Auto vs Android Automotive
- Vehicle HAL / VHAL
- Car Service
- Binder
- AIDL
- HAL boundaries
- vehicle properties

Important flow:

```text
CAN -> Vehicle Interface -> VHAL -> Car Service -> Android App
```

---

# 6. Phase 5 — OS / Virtualization / Safety

## Week 12 — QNX

Focus:

- microkernel architecture
- process
- thread
- message passing
- IPC
- scheduling
- priorities
- real-time behavior
- resource managers
- driver model overview
- fault isolation

Compare QNX vs Linux on:

- architecture
- scheduling
- IPC
- real-time behavior
- drivers
- fault isolation

## Week 13 — Hypervisor

Learn:

- virtualization
- hypervisor
- Type 1 vs Type 2
- VM
- guest OS
- CPU virtualization
- memory virtualization
- IOMMU
- virtual devices
- device assignment
- resource partitioning
- isolation

Target:

```text
             SoC
              |
          Hypervisor
              |
      +-------+-------+
      |       |       |
    QNX     Linux   Android
      |       |       |
   Cluster Services  IVI
```

Core question:

> How do you prevent an Android/Linux failure from affecting a safety-related cluster domain?

## Week 14 — Functional Safety

Learn:

- ISO 26262
- HARA
- Safety Goal
- ASIL A/B/C/D
- Functional Safety Concept
- Technical Safety Concept
- FMEA
- FTA
- fault containment
- watchdog
- redundancy
- fail-safe
- safe state
- freedom from interference
- WCET
- deadline
- determinism

Mental model:

```text
Hazard -> HARA -> Safety Goal -> ASIL
       -> Safety Concept -> Safety Mechanisms
       -> Verification / Validation
```

Do not confuse:

```text
ASIL != MISRA
ASIL != quality level
ASIL != coding standard
```

## Week 15 — AUTOSAR + A-SPICE + Coding Standards

Prioritize **AUTOSAR Adaptive** for Cockpit DC.

Learn:

- ARA
- Execution Management
- Communication Management
- Persistency
- Diagnostics
- State Management

Also:

- MISRA C/C++
- static analysis
- coding guidelines
- A-SPICE lifecycle
- requirements
- architecture
- implementation
- unit testing
- integration testing
- system testing

---

# 7. Week 16 — Capstone: Mini Cockpit Domain Controller

Build a Linux-based simulator:

```text
                    +-----------------------+
                    |   Vehicle Simulator   |
                    +-----------+-----------+
                                |
                               CAN
                                |
                    +-----------v-----------+
                    |     CAN Receiver      |
                    +-----------+-----------+
                                |
                         Signal Decoder
                                |
                    +-----------v-----------+
                    |   Vehicle Data Model  |
                    +-----------+-----------+
                                |
                         Middleware
                                |
                    +-----------v-----------+
                    |    Vehicle Service    |
                    +-----------+-----------+
                                |
                             SOME/IP
                                |
                    +-----------v-----------+
                    |     Cockpit App       |
                    +-----------+-----------+
                                |
                             Qt/QML
                                |
                    +-----------v-----------+
                    |      Cluster UI       |
                    +-----------------------+
```

Required signals:

- vehicle speed
- RPM
- fuel
- engine temperature
- steering angle
- door status
- warning status

## Required engineering features

### CAN

- frame representation
- encoding/decoding
- signal extraction
- signal validation
- DBC-like configuration

### Concurrency

- CAN receiver thread
- signal processing thread
- UI thread
- queueing
- synchronization
- shutdown
- timeout
- backpressure

### Middleware

```text
VehicleService
├── GetSpeed()
├── GetRPM()
├── GetTemperature()
└── SubscribeSpeed()
```

### Qt/QML

Display:

- speed
- RPM
- temperature
- doors
- warnings

### Fault injection

Support:

- CAN timeout
- invalid signal
- out-of-range value
- malformed frame
- service unavailable
- SOME/IP timeout
- queue overflow
- worker thread failure

Expected behavior:

```text
Fault -> Detection -> Logging -> Fallback -> Safe State
```

---

# 8. Engineering Quality

The capstone must demonstrate Senior C++ engineering.

Required:

- C++17
- CMake
- Git
- unit tests
- integration tests
- sanitizers
- static analysis where practical
- structured logging
- clean ownership
- explicit thread ownership
- graceful shutdown
- error handling
- timeout handling
- deterministic tests
- documentation

Build configurations:

```text
Debug
Release
ASan
UBSan
TSan (where applicable)
```

---

# 9. Debugging Mindset

When a vehicle signal does not appear on the Cluster, trace the complete path:

```text
CAN network
    |
CAN controller
    |
driver / SocketCAN
    |
CAN frame
    |
DBC decoder
    |
Signal Manager
    |
Vehicle Data Model
    |
Middleware
    |
SOME/IP
    |
Cockpit Service
    |
Qt Model
    |
QML binding
    |
UI
```

Do not immediately assume the UI is broken.

Always determine:

1. Where the data was last known to be correct.
2. Where it became incorrect or disappeared.
3. What contract exists between components.
4. What timeout/error state should occur.
5. How to reproduce the failure.
6. What evidence proves the root cause.

---

# 10. Interview Question Bank

## C++

- object lifetime
- Rule of 5
- move semantics
- RVO/NRVO
- exception safety
- `volatile`
- atomics
- memory ordering
- container complexity
- allocator behavior
- UB
- ABI

## Concurrency

- race conditions
- deadlock
- lock-free
- wait-free
- CAS
- ABA
- memory reclamation
- false sharing
- priority inversion

## Linux

- process/thread
- virtual memory
- ELF
- mmap
- epoll
- syscall
- IPC
- shared libraries
- scheduling

## Automotive

- CAN arbitration
- CAN-FD
- DBC
- SocketCAN
- Automotive Ethernet
- SOME/IP
- Service Discovery
- UDS
- DoIP
- PTP
- TSN

## Cockpit

- Cockpit Domain Controller architecture
- Cluster vs IVI
- QNX vs Linux
- AAOS
- VHAL
- Qt event loop
- QML/C++ boundary
- hypervisor isolation
- multi-OS architecture

## Safety

- ISO 26262
- ASIL
- HARA
- WCET
- watchdog
- safe state
- fault containment
- freedom from interference

---

# 11. Priority If Time Is Limited

## Tier 1 — MUST KNOW

```text
C++17
Linux
Concurrency
IPC
CAN/CAN-FD
Automotive Ethernet
SOME/IP
Qt/QML
```

## Tier 2 — IMPORTANT

```text
QNX
AAOS
Hypervisor
ISO 26262
ASIL
UDS / DoIP
AUTOSAR Adaptive
```

## Tier 3 — SUPPORTING

```text
LIN
PTP
TSN
DDS
AUTOSAR Classic
Deep driver development
Deep MCU development
```

Suggested effort:

```text
C++ / Linux             25%
CAN / CAN-FD            15%
Ethernet / SOME-IP      18%
Qt / QML                12%
Concurrency / IPC       12%
QNX                      7%
AAOS                     5%
Hypervisor               3%
Safety / AUTOSAR         3%
```

---

# 12. Definition of Done

The roadmap is successful when the learner can:

1. Explain a Cockpit Domain Controller end-to-end.
2. Explain how CAN signals reach a Cluster application.
3. Explain CAN arbitration and CAN-FD.
4. Decode a CAN signal from raw bytes using a DBC-like definition.
5. Explain Automotive Ethernet and SOME/IP.
6. Explain Service Discovery.
7. Design an IPC/middleware boundary in C++.
8. Explain Qt's event loop and threading model.
9. Explain QNX vs Linux.
10. Explain Android Automotive/VHAL at architecture level.
11. Explain how a hypervisor isolates multiple OS domains.
12. Explain ASIL and basic ISO 26262 concepts.
13. Diagnose a missing vehicle signal by tracing the full software stack.
14. Build and test the Mini Cockpit Domain Controller.
15. Discuss architecture and trade-offs at Senior Engineer level.

---

# 13. Default Format for Future Lessons

Unless explicitly requested otherwise, every lesson should contain:

1. Concept
2. Why it exists in Cockpit DC
3. Architecture
4. C++ / Linux implementation
5. Hands-on exercise
6. Failure/debugging exercise
7. Senior interview questions
8. Short review checklist

Do not move on just because a definition can be memorized. Move on when the concept can be explained, implemented, debugged, and defended in an interview.

---

# 14. First Lesson

Start with:

> **Cockpit Domain Controller Architecture — trace one vehicle signal from CAN-FD to C++ middleware to Qt/QML Cluster UI.**

The first implementation should be intentionally small, then expanded throughout the roadmap.

## Core mental model

```text
                    COCKPIT DOMAIN CONTROLLER
                              |
                       High Performance SoC
                              |
                         Hypervisor
                              |
             +----------------+----------------+
             |                |                |
            QNX             Linux           Android
             |                |                |
          Cluster         Services             IVI
             |                |                |
             +----------------+----------------+
                              |
                          Middleware
                              |
                 +------------+------------+
                 |                         |
              CAN-FD                    Ethernet
                 |                         |
            Vehicle Signals             SOME/IP
                 |                         |
                 +------------+------------+
                              |
                       Vehicle Services
                              |
                           Qt/QML
                              |
                              HMI
```

The end goal is to move from:

> "I know C++."

to:

> "I understand how C++ software participates in a real Cockpit Domain Controller, from vehicle networking through middleware and OS boundaries to the HMI, and I can design, test, debug, and explain that system."