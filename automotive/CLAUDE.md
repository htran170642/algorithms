# CLAUDE.md — Automotive C++ / CDC Learning Roadmap

## 0. Mission

This repository is a structured learning and interview-preparation program for becoming a **Senior C++ Automotive Software Engineer**, with a focus on:

- Central Domain Controller (CDC)
- IVI / Cockpit / Cluster platforms
- Automotive Linux / QNX / Android Automotive
- High-performance C++ middleware
- Automotive networking
- Qt/QML HMI
- Hypervisor-based mixed OS architecture
- AUTOSAR Adaptive
- Embedded Linux / Yocto
- Functional Safety and Automotive Cybersecurity

### Primary target roles

- Senior C++ Automotive Software Engineer
- Senior C++ IVI / CDC Engineer
- Automotive Linux / QNX Engineer
- Automotive Middleware Engineer
- Qt/QML Automotive Engineer
- AUTOSAR Adaptive Engineer
- Automotive Platform Engineer

### Learning principle

Do not study every automotive technology to the same depth.

Prioritize:

1. C++ / Linux / concurrency / systems programming
2. Automotive networking and middleware
3. CDC architecture and virtualization
4. Qt/QML and HMI
5. AUTOSAR Adaptive
6. Build / test / Yocto
7. Functional safety / cybersecurity / process

The goal is not to become an expert in every ECU technology. The goal is to become a strong **Senior C++ platform engineer who understands automotive constraints and architecture**.

---

# 1. Target Competency Model

## Tier A — Must Be Strong

- C++11/14/17
- Memory management and RAII
- Move semantics
- Templates and STL
- Multithreading
- Atomics and memory ordering
- Lock-free concepts
- Linux internals
- POSIX
- IPC
- Networking
- Real-time concepts
- CAN / CAN-FD
- Automotive Ethernet
- SOME/IP
- Diagnostics / UDS concepts
- CDC architecture
- Hypervisor concepts
- Qt / QML
- CMake
- Cross compilation
- Unit / integration testing

## Tier B — Must Understand

- QNX
- VxWorks
- Android Automotive OS
- Android HAL / AIDL / Binder
- AUTOSAR Classic architecture
- AUTOSAR Adaptive architecture
- Yocto
- Bootloader / BSP / device-driver concepts
- LIN
- SPI / I2C / UART
- CANoe / CANalyzer
- DBC
- DoIP
- A-SPICE
- ISO 26262
- MISRA C/C++
- Automotive cybersecurity

## Tier C — Awareness

- OpenSynergy COQOS
- QNX Hypervisor
- ACRN
- DDS
- TSN
- SOME/IP implementation details
- GPU virtualization
- HSM
- OTA architecture
- Secure Boot
- AUTOSAR security mechanisms
- Vehicle data platform architecture

---

# 2. Important Corrections / Additions

The original roadmap is strong, but the following areas should be added because they are highly relevant to CDC / IVI / platform engineering.

## 2.1 ISO 26262

Use **ISO 26262**, not ISO 26226.

Study:

- Functional Safety
- Hazard Analysis and Risk Assessment (HARA)
- ASIL A/B/C/D
- QM
- Safety Goal
- Functional Safety Concept
- Technical Safety Concept
- Safety mechanisms
- Freedom from interference
- Fault detection
- Safe state
- Fail-safe / fail-operational concepts
- Safety lifecycle

Do not initially aim for certification-level knowledge.

## 2.2 Automotive Cybersecurity

Add:

- ISO/SAE 21434
- UNECE R155
- UNECE R156
- Threat analysis
- Secure Boot
- Secure Update
- Authentication
- Encryption
- Key management
- HSM concepts
- Secure communication
- ECU identity
- Certificate concepts
- Attack surface
- Intrusion detection concepts

For CDC platforms, cybersecurity is increasingly important because the system is highly connected.

## 2.3 Diagnostics

Add:

- UDS
- ISO 14229 concepts
- DTC
- Diagnostic Session
- Diagnostic Request / Response
- ECU Reset
- Read Data By Identifier
- Read / Clear DTC
- Security Access concepts
- Routine Control concepts
- DoIP

Understand the architecture; exact byte-level implementation can come later.

## 2.4 Boot / BSP / Drivers

Add:

- Bootloader
- U-Boot concepts
- Boot sequence
- Kernel startup
- Device Tree
- BSP
- Cross compiler
- Sysroot
- Kernel modules
- Character device concept
- ioctl
- mmap
- Interrupt concept
- DMA
- Device driver architecture

This is important for a Senior platform engineer even if the job is not pure driver development.

## 2.5 OTA / Software Update

Add:

- OTA architecture
- A/B partitioning
- Rollback
- Update package
- Version management
- Secure update
- Failure recovery
- Software Update Management System concepts
- AUTOSAR Update and Configuration Management concepts

## 2.6 Time Synchronization

Add:

- Clock sources
- Monotonic vs realtime clock
- PTP
- Automotive Ethernet time synchronization
- Timestamping
- Time-sensitive systems

This becomes useful for distributed CDC / ADAS / multimedia systems.

## 2.7 Multimedia / Display / GPU

For CDC / IVI roles, understand at least conceptually:

- DRM / KMS
- Wayland
- EGL / OpenGL ES concepts
- GPU acceleration
- Display pipeline
- Hardware compositor
- Zero-copy
- Video pipeline
- Camera pipeline concepts

Existing knowledge of GStreamer / DeepStream can be leveraged here.

---

# 3. Six-Month Core Roadmap

Recommended pace:

- 12–15 hours/week
- 6 months
- 70% hands-on
- 20% theory
- 10% interview review

---

# Phase 1 — C++ Embedded Core
## Weeks 1–5

### Week 1 — Memory and Object Lifetime

Study:

- Stack / heap
- Object lifetime
- RAII
- Rule of 0/3/5
- Copy / move
- Smart pointers
- Ownership
- Dangling pointers
- Alignment
- Padding
- Placement new
- Dynamic allocation
- Fragmentation
- Custom allocators

Practice:

- SimpleVector
- String
- UniquePtr
- SharedPtr
- MemoryPool
- ObjectPool
- RingBuffer

Interview questions:

- Why is RAII important in embedded software?
- Why can dynamic allocation be problematic?
- unique_ptr vs shared_ptr?
- Why is volatile not atomic?
- What causes undefined behavior around object lifetime?

### Week 2 — Concurrency

Study:

- std::thread
- mutex
- lock_guard
- unique_lock
- condition_variable
- atomic
- future
- promise
- async
- memory ordering
- race condition
- deadlock
- livelock
- starvation
- priority inversion
- false sharing

Practice:

- ThreadPool
- ProducerConsumerQueue
- LockFreeQueue
- TimerScheduler
- EventDispatcher

### Week 3 — Embedded C++

Study:

- volatile
- bit operations
- memory-mapped I/O
- endianess
- register access
- interrupts
- DMA
- static storage
- linker concepts
- startup code
- ABI

### Week 4 — Performance

Study:

- CPU cache
- cache line
- locality
- branch prediction
- CPU pipeline
- SIMD concepts
- false sharing
- allocation cost
- zero-copy
- copy vs move

### Week 5 — Coding Standards

Study:

- MISRA C
- MISRA C++
- AUTOSAR C++14 Guidelines
- CERT C++

Focus on understanding why rules exist.

---

# Phase 2 — Linux / POSIX / RTOS
## Weeks 6–9

### Week 6 — Linux Internals

Study:

- Process
- Thread
- Virtual memory
- Page
- Stack
- Heap
- File descriptor
- Syscall
- User space / kernel space
- Signal

Important APIs:

- fork
- exec
- wait
- mmap
- munmap
- read
- write
- open
- close
- poll
- epoll
- ioctl

### Week 7 — IPC

Study:

- Pipe
- FIFO
- Unix Domain Socket
- TCP
- UDP
- Shared Memory
- Message Queue
- Signal
- Event notification

Project:

Vehicle Data Service with multiple processes.

### Week 8 — Real-Time

Study:

- Hard real-time
- Soft real-time
- Deadline
- Latency
- Jitter
- WCET
- Scheduling
- Priority inversion
- SCHED_FIFO
- SCHED_RR
- CPU affinity
- PREEMPT_RT

### Week 9 — QNX / VxWorks

QNX:

- Microkernel
- Process
- Thread
- Message passing
- Channel
- Connection
- Resource manager
- Neutrino

Understand QNX vs Linux architecture.

VxWorks:

- RTOS architecture
- Task scheduling
- IPC
- Real-time characteristics
- Where it fits in automotive

---

# Phase 3 — Automotive Networking
## Weeks 10–13

### Week 10 — CAN

Study deeply:

- CAN frame
- Identifier
- DLC
- CRC
- ACK
- Arbitration
- Bit stuffing
- Error detection
- Error active
- Error passive
- Bus-off

Practice:

- CAN frame parser
- CAN simulator
- CAN signal decoder

### Week 11 — CAN-FD / LIN

CAN-FD:

- Classic CAN vs CAN-FD
- Data phase
- Bit rate switching
- Payload
- Error handling

LIN:

- Master
- Slave
- Schedule
- Frame
- Checksum

### Week 12 — Automotive Ethernet

Study:

- 100BASE-T1
- 1000BASE-T1
- TCP/IP
- UDP
- SOME/IP
- Service Discovery
- DoIP
- TSN concepts
- PTP concepts

### Week 13 — Low-Level Interfaces

Study:

- UART
- SPI
- I2C
- GPIO concepts
- Interrupts
- DMA

Understand master/slave, clock, address, duplex, and use cases.

---

# Phase 4 — CDC / Hypervisor / AAOS
## Weeks 14–17

### Week 14 — CDC Architecture

Study:

- Central Domain Controller
- Domain consolidation
- SoC architecture
- ECU consolidation
- High-performance computing
- Safety domain vs non-safety domain
- Mixed-criticality systems

Be able to draw:

Hardware
→ Hypervisor
→ QNX
→ Linux
→ Android

### Week 15 — Hypervisor

Study:

- Type 1 hypervisor
- VM
- CPU virtualization
- Memory virtualization
- Device virtualization
- IOMMU
- Shared memory
- Virtual devices
- GPU passthrough concepts
- Isolation
- Resource partitioning

Know conceptually:

- QNX Hypervisor
- OpenSynergy COQOS
- ACRN

### Week 16 — Android Automotive

Study:

- AAOS
- Android Framework
- Binder
- AIDL
- HAL
- Vehicle HAL
- Car Service
- Vehicle properties

Understand:

Android App
→ Android Framework
→ Car Service / VHAL
→ Vehicle network

### Week 17 — Mixed OS Integration

Design:

- QNX Cluster
- Linux IVI
- Android IVI
- Hypervisor
- Virtual networking
- Shared memory / IPC
- Device assignment
- CPU / memory partitioning

Interview questions:

- What happens if Linux crashes?
- How do you isolate QNX from Linux?
- How do VMs communicate?
- How do you partition CPU and memory?
- How do you share devices safely?

---

# Phase 5 — Qt / QML
## Weeks 18–20

### Week 18 — Qt C++

Study:

- QObject
- Signal / Slot
- Event loop
- QThread
- QTimer
- QMutex
- QProcess
- QSocket
- Model/View

### Week 19 — QML

Study:

- Property
- Signal
- Binding
- Component
- Model
- ListView
- StackView
- State
- Animation

Project:

Vehicle Dashboard.

### Week 20 — Qt + C++ Architecture

Architecture:

QML
→ Presentation
→ C++ Controller
→ Vehicle Service
→ CAN / Ethernet

Study:

- Thread boundaries
- Signal/slot thread affinity
- UI thread
- Worker thread
- Backend abstraction
- Testability

---

# Phase 6 — AUTOSAR
## Weeks 21–24

### Classic AUTOSAR

Understand:

Application
→ RTE
→ BSW
→ MCAL
→ Hardware

Study:

- SWC
- RTE
- BSW
- MCAL
- ECU
- COM
- OS
- DEM
- DCM

### Adaptive AUTOSAR

Study:

Application
→ ARA
→ Adaptive Platform
→ POSIX OS

Study:

- Execution Management
- Communication Management
- Persistency
- Diagnostics
- Update and Configuration Management
- Time Synchronization
- State Management

Deepen:

- SOME/IP
- Service Discovery
- Service-oriented architecture

Focus more on Adaptive AUTOSAR for CDC / HPC roles.

---

# Phase 7 — Build / Embedded Linux
## Weeks 25–27

### Week 25 — CMake

Master:

- target
- library
- executable
- PUBLIC / PRIVATE / INTERFACE
- find_package
- install
- export
- toolchain
- cross compilation
- testing

### Week 26 — Yocto

Study:

- BitBake
- Recipe
- Layer
- Image
- Machine
- Distro
- SDK
- Cross compilation

Build a custom image containing:

- Qt
- CAN utilities
- Automotive application
- systemd service
- test utilities

### Week 27 — Boot / BSP / Drivers

Study:

- Bootloader
- U-Boot concepts
- Boot sequence
- Device Tree
- BSP
- Kernel
- Kernel module
- Character device
- ioctl
- mmap
- Interrupt
- DMA
- Sysroot
- Toolchain
- ABI

---

# Phase 8 — Testing / Tools / Process
## Weeks 28–31

### Week 28 — Testing

Study:

- GoogleTest
- GoogleMock
- CTest
- Unit testing
- Integration testing
- System testing
- Hardware-in-the-loop concepts

### Week 29 — CANoe / CANalyzer

Understand:

- Trace
- Signal
- Message
- DBC
- CAPL concepts
- Simulation
- Measurement
- Diagnostics

If commercial tools are unavailable, use open-source CAN tooling and simulators for practice.

### Week 30 — A-SPICE

Study:

- Requirements
- Architecture
- Design
- Implementation
- Unit verification
- Integration verification
- System verification
- Traceability
- Configuration management
- Change management
- Defect management

### Week 31 — Safety + Security

Functional Safety:

- ISO 26262
- HARA
- ASIL
- Safety Goal
- FSC
- TSC
- Safety mechanisms
- Fault detection
- Safe state
- Fail-operational

Cybersecurity:

- ISO/SAE 21434
- UNECE R155
- UNECE R156
- Threat analysis
- Secure Boot
- Secure Update
- Authentication
- Encryption
- HSM
- Key management

---

# Phase 9 — CDC Capstone
## Weeks 32–36

## Project: Automotive CDC Simulator

Architecture:

Qt/QML
→ HMI
→ Vehicle Service
→ CAN / Ethernet
→ Diagnostics

Processes:

- hmi
- vehicle-service
- can-service
- diagnostic-service
- network-service

Communication:

- Unix Domain Socket
- Shared Memory
- Event notification
- SOME/IP-inspired service interface

### Features

Vehicle signals:

- VehicleSpeed
- EngineRPM
- CoolantTemperature
- FuelLevel
- BatteryVoltage
- DoorStatus
- TurnSignal

HMI:

- Speed
- RPM
- Fuel
- Battery
- Temperature
- Warnings

Diagnostics:

- DTC
- Diagnostic Session
- Read Data
- Clear DTC
- ECU Reset concepts

Networking:

- CAN simulation
- CAN-FD concepts
- SOME/IP
- DoIP concepts

Safety:

- Invalid signal detection
- Timeout
- Last valid value
- Safe fallback
- Fault state

Build:

- CMake
- GoogleTest
- Docker for development where useful
- Cross-compilation-ready toolchain
- Optional Yocto image

---

# 4. Additional Advanced Topics

After the six-month core roadmap, continue with:

## Automotive Platform

- POSIX
- systemd
- containers
- service management
- watchdog
- logging
- tracing
- telemetry
- resource monitoring

## Networking

- SOME/IP
- DDS
- DoIP
- PTP
- TSN
- VLAN
- multicast
- QoS
- service discovery

## Security

- Secure Boot
- TPM concepts
- HSM
- PKI
- certificate lifecycle
- TLS
- MACsec concepts
- secure diagnostics
- OTA security

## Multimedia / Cockpit

- Wayland
- DRM/KMS
- EGL
- OpenGL ES
- GPU acceleration
- hardware compositor
- GStreamer
- zero-copy
- camera/display pipelines

Existing GStreamer / DeepStream experience should be reused here rather than relearned from scratch.

---

# 5. Weekly Learning Method

For every topic, follow:

1. Learn the concept
2. Explain it without notes
3. Implement a small example
4. Integrate it into the capstone
5. Write interview questions
6. Review trade-offs
7. Document the architecture

Recommended weekly split:

- 30% theory
- 40% coding
- 20% project
- 10% interview preparation

---

# 6. Interview Preparation

For each major topic, maintain:

## Concept Questions

Example:

- What is CAN arbitration?
- Why is volatile not atomic?
- QNX vs Linux?
- What is SOME/IP?
- Why use a hypervisor?
- What is Vehicle HAL?
- Classic vs Adaptive AUTOSAR?
- Why use Yocto?
- What is ASIL?
- What is freedom from interference?

## Design Questions

Example:

- Design an IVI architecture.
- Design a CDC platform.
- Design communication between QNX and Linux.
- Design a vehicle data service.
- Design a diagnostic service.
- Design an OTA update mechanism.
- Design a fault-tolerant speed signal pipeline.

## Coding Questions

Focus on:

- RAII
- smart pointers
- concurrency
- atomics
- lock-free structures
- ring buffer
- memory pool
- serialization
- networking
- producer/consumer
- thread pool

---

# 7. Project Architecture Goals

The capstone should demonstrate:

```text
C++
Linux
POSIX
Multithreading
IPC
CAN
Ethernet
SOME/IP concepts
Qt/QML
CMake
Testing
Diagnostics
Safety
Security
Logging
Monitoring
```

Do not build everything into one process.

Prefer:

```text
                    HMI
                     │
                     ▼
              Vehicle Service
                /     |      \
               /      |       \
             CAN   Ethernet  Diagnostics
              │       │          │
              ▼       ▼          ▼
           Vehicle  Network    UDS/DoIP
            Data     Service
```

Use clear interfaces between components.

---

# 8. Engineering Quality Requirements

The project should demonstrate Senior-level engineering.

## Code Quality

- RAII
- const correctness
- ownership clarity
- no unnecessary dynamic allocation
- thread safety
- error handling
- logging
- testability
- clean interfaces
- dependency inversion where useful

## Performance

Measure:

- latency
- throughput
- CPU usage
- memory usage
- allocation count
- IPC overhead
- queue depth
- dropped messages

## Reliability

Test:

- timeout
- malformed message
- disconnected process
- service restart
- invalid CAN signal
- queue overflow
- network failure
- process crash

## Observability

Implement:

- structured logs
- metrics
- latency measurements
- health status
- watchdog concepts

---

# 9. What NOT to Do

Avoid:

- Memorizing AUTOSAR documents without understanding architecture
- Learning every CAN frame field before understanding system architecture
- Spending months on MCU programming if targeting CDC/HPC
- Trying to become a QNX kernel expert immediately
- Learning Yocto recipes without understanding Linux boot architecture
- Learning Qt widgets without understanding event loops/thread affinity
- Collecting certificates instead of building projects
- Building a toy project that never uses IPC/networking/concurrency

---

# 10. Final Skill Map

At the end of the roadmap, aim for:

```text
                    Senior C++ Engineer
                           │
          ┌────────────────┼────────────────┐
          │                │                │
        C++              Linux           RTOS
          │                │                │
   Memory/Threading     POSIX/IPC       QNX concepts
          │                │                │
          └────────────────┼────────────────┘
                           │
                  Automotive Platform
                           │
          ┌────────────────┼────────────────┐
          │                │                │
        CAN             Ethernet         Diagnostics
          │                │                │
          └────────────────┼────────────────┘
                           │
                         CDC
                           │
                    Hypervisor
                           │
              ┌────────────┼────────────┐
              │            │            │
             QNX         Linux        AAOS
              │            │            │
              └────────────┼────────────┘
                           │
                    Qt/QML / HMI
                           │
                    AUTOSAR Adaptive
                           │
             ┌─────────────┼─────────────┐
             │             │             │
           Yocto         Testing       Safety
             │             │             │
             └─────────────┼─────────────┘
                           │
                  Security / OTA
                           │
                    CDC Capstone
```

---

# 11. Definition of Done

The roadmap is complete when you can independently:

- Explain a CDC architecture
- Explain QNX vs Linux
- Explain why a hypervisor is used
- Design QNX + Linux mixed-criticality architecture
- Explain CAN arbitration
- Decode a CAN signal using a DBC
- Explain CAN-FD
- Explain SOME/IP and Service Discovery
- Explain UDS and DoIP
- Explain Vehicle HAL
- Explain Classic vs Adaptive AUTOSAR
- Build a C++ project with CMake
- Cross-compile a Linux application
- Build a basic Yocto image
- Explain bootloader / BSP / Device Tree concepts
- Build a Qt/QML HMI
- Implement safe C++ concurrency
- Explain real-time constraints
- Explain ASIL and ISO 26262 concepts
- Explain automotive cybersecurity fundamentals
- Write unit/integration tests
- Diagnose performance and IPC bottlenecks
- Design and explain the CDC Simulator end-to-end

The final test is not "Can I memorize the technology?"

The final test is:

> **Can I design, implement, debug, test, and explain a production-style automotive C++ component under performance, reliability, safety, and security constraints?**
