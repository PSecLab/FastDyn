FastDyn now fuzzes and falsifies 😎

# Fuzzing (Task-Centric Fuzzing for Embedded Firmware in FastDyn)

Embedded firmware rarely behaves like a single “program” with clean start/inputs/exit. Instead, it is typically a **collection of long-running loops** that together implement the device behavior. Concurrency arises either explicitly (via an RTOS scheduler) or implicitly (via **interrupt handlers** that preempt foreground code). Conceptually, the firmware is **always on**: each loop continuously reacts to hardware events, shared state, timers, and peripheral side effects.

## Firmware as Concurrent Loops

Most embedded systems can be understood as:

- **Foreground control loops** (polling, state machines, periodic work)
- **RTOS tasks** (cooperatively or preemptively scheduled loops)
- **ISRs** (interrupt service routines) that asynchronously wake tasks, push events, or update state
- **MMIO-driven I/O** (peripheral registers, FIFOs, DMA status, timers)

This is fundamentally different from Linux-style applications where inputs often arrive through a well-defined API boundary (e.g., syscalls for files, sockets, stdin).

## FreeRTOS Example

A typical FreeRTOS firmware structure looks like:

1. `main()` initializes hardware
2. `main()` creates tasks
3. `main()` starts the scheduler
4. Each task runs as an **infinite loop** (`for(;;) { ... }`)
5. ISRs fire asynchronously and interact with tasks via queues/semaphores

Example *shape* (conceptually):

- `main()`  
  - initializes clocks, GPIO, UART, timers  
  - `xTaskCreate(SensorTask, ...)`  
  - `xTaskCreate(ControlTask, ...)`  
  - `xTaskCreate(CommsTask, ...)`  
  - `vTaskStartScheduler()`

- `SensorTask` (infinite loop)  
  - reads ADC/DMA status via **MMIO**  
  - updates shared buffers  
  - `vTaskDelayUntil(...)`

- `ControlTask` (infinite loop)  
  - waits on a queue/semaphore  
  - computes control outputs  
  - writes PWM registers via **MMIO**

- `CommsTask` (infinite loop)  
  - reads UART FIFO/status via **MMIO**  
  - parses protocol messages  
  - signals other tasks via queues

- ISRs (UART RX, timer tick, DMA complete, etc.)  
  - update device state  
  - push queue items / give semaphores  
  - wake tasks

## Binary-Only Reality: Why Fuzzing Firmware Is Harder

FastDyn executes a firmware **binary-only**: we do not have source-level task boundaries, device headers, or a rich syscall layer. A binary analyzer can still infer useful semantics (e.g., likely scheduler loops, dispatch patterns, MMIO-like access sequences, interrupt-related code regions), but the *input model* remains different:

- Primary inputs are **MMIO**, **interrupts/exceptions**, **DMA-visible memory**, and **timing**
- There is no “argv/stdin/socket” boundary to hook like in Linux

Therefore, to fuzz efficiently we must identify:

1. **Which MMIO regions matter** (what the firmware actually reads/writes)
2. **How concurrent loops depend on each other** (shared memory, queues, semaphores, ISR-driven state transitions)
3. **How to drive each loop deep** without getting stuck in wait/poll trivial paths

## Task-Centric Fuzzing: Like Per-Process Fuzzing, But for Firmware

In Linux, fuzzing is often organized around isolatable units (processes, request handlers, services). For embedded firmware, the analogue is to fuzz **each task/loop** as an independently driven unit:

- Treat each FreeRTOS task as a “fuzz target” (often visible as an infinite loop)
- Run tasks **individually** while still satisfying their dependencies
  - e.g., a control loop may only execute meaningful logic after a sensor loop updates buffers *and* an ISR signals “data ready”

This means the fuzzer must orchestrate not only random inputs, but also the **dependency conditions** that let each loop explore meaningful paths.

## Running Tasks Individually

We already have mechanisms to execute loops in isolation:

- **Virtual instructions** and **modifiers** allow us to carve out/control loops inside the firmware
- This enables single-task execution (or selectively advancing a chosen loop), even though the original binary was designed for concurrent scheduling and asynchronous preemption

## Coverage: In-JIT Logging of Translation-Block PCs

The remaining core requirement is **coverage**.

FastDyn includes a high-speed logger that can log machine state **without ever leaving JIT-translated code**. Coverage becomes simple:

- Log the **PC when entering each translation block (TB)**
- A TB is a practical proxy for a **basic-block region**
- This provides the standard greybox fuzzing signal with extremely low overhead

## Why Lockstep Was Too Slow

Previously, logs were dumped to an external file and consumed by a **lockstep** LibAFL-based fuzzer. That design is slow because it introduces:

- **IPC overhead**
- synchronization costs
- lockstep coordination (slow by nature)

For chaos-style fuzzing engines, **throughput is everything**—and a plugin called FastDyn should not be architecturally constrained to be slow.

## Bringing LibAFL Inside FastDyn

To eliminate lockstep overhead, we embed LibAFL **inside** FastDyn as an in-process plugin:

- No external log shipping
- No IPC
- The fuzzer observes coverage *first hand*
- The fuzzer can inject mutations immediately through plugin APIs

This enables direct mutation of:

- **MMIO inputs** (via the device model)
- **registers** and **stack** (via plugin APIs)
- and controlled triggering of **interrupt/timing events** to satisfy inter-loop dependencies

## Outcome

This architecture gives a firmware-native analogue to per-process fuzzing on Linux:

- fuzz each **task/loop** independently
- enforce or satisfy **dependencies** between loops
- obtain high-rate coverage via **in-JIT TB-PC logging**
- inject mutations directly into **MMIO / registers / stack**
- avoid lockstep/IPC overhead entirely by running LibAFL **in-process** inside FastDyn


## Internals
Internally, as mentioned, our fuzzer is built on top of the logger. The fuzzer, in principle, can consume an unbounded stream of coverage events, but the original logger is implemented as a circular buffer and will eventually wrap around if it isn’t drained quickly enough. To bridge this mismatch, we spawn a dedicated tracer thread that continuously drains the logger’s circular buffer and appends entries into an effectively unbounded linear stream (growing as needed). In practice, tracer() polls the circular buffer index, and whenever it observes new entries, it reads out the logged PCs and forwards them to add_observed_value(), which stores them in the fuzzer’s linear coverage stream.

```C
void* tracer(void* arg) {
    cc_list.count = 1;
    cc_ret.entry = &cc_entry;
    cc_ret.list  = &cc_list;
    cc_ret.entry->reg = 15;

    cc_ret.list->log_buf.buffer = malloc(UINT16_MAX + 1);
    uint16_t tracer_index = 0;

    tracer_ready = 1;

    // TODO: make reads atomic / add proper synchronization
    while (true) {
        while ((uint16_t)(cc_list.log_buf.index - tracer_index)) {
            add_observed_value(cc_list.log_buf.buffer[tracer_index / 4]);
            tracer_index += 4; // 32-bit PC
        }
    }
}
```


The tracer is created in core_init() and runs continuously:

```C
// Create a new thread
if (pthread_create(&thread, NULL, tracer, NULL) != 0) {
    perror("Failed to create thread");
    exit(1);
}
```


Since firmware loops can be arbitrarily long, this design ensures that logging does not lose events due to buffer wraparound: the tracer keeps consuming entries and add_observed_value() keeps appending them, expanding the linear storage if required.

## Fuzzer Design.
Our novelty is that we are **not firmware-, RTOS-, or device-model dependent**. Instead of building a fuzzer that only works when we recognize “this is FreeRTOS” or “this is Zephyr” or “this is an STM32 peripheral driver,” we leverage a simple observation that holds across most embedded systems: they are **continuous closed-loop systems** that repeatedly perform sensing → processing → actuation. That structure almost always appears in the binary as **persistent loops** (or loop-like scheduling patterns) that run for the lifetime of the device. So our algorithm is: (1) find these loops directly in the binary, (2) for each loop, identify its **input points**—the places where the loop consumes external or environment-dependent values—and (3) fuzz each loop by mutating those inputs, using FastDyn’s execution control (virtual instructions/modifiers) to isolate and drive the loop without needing source code or OS hooks.

For example, in a flight or motor controller, one loop reads IMU/gyro data (sensor registers or DMA buffers), another loop runs control logic (e.g., PID/state estimation), and another writes PWM outputs to actuators. In a thermostat, the core loop reads temperature (ADC/MMIO), compares to a setpoint, and drives a heater relay (GPIO/MMIO). In an automotive ECU, a loop reads CAN frames (MMIO-backed FIFO), updates state machines, and triggers outputs (actuator control registers). Across all of these, the “API” is not a syscall boundary like Linux; the inputs arrive through **MMIO reads, interrupts, DMA-updated memory, and shared buffers**, and the program is designed to run indefinitely. Our fuzzer targets exactly those input-consumption sites inside the loops—for example, a load from a status register, a read from a ring buffer filled by an ISR, or a queue/semaphore path that gates deeper control logic—and mutates them to explore alternative behaviors.

This is novel because many firmware fuzzing approaches either (a) become **platform-specific** by relying on detailed device models and RTOS-aware hooks, or (b) assume **application-like boundaries** (single entrypoint, well-formed inputs) that embedded binaries typically lack. We instead propose a **binary-structural decomposition**: loops are the natural unit of execution in continuous embedded systems, and loop input points define the fuzzing surface. This generalizes across bare-metal and RTOS firmware and still works when we only have the binary.

Finally, it’s important to be explicit about false positives: fuzzing low-level input points can surface behaviors that would be hard (or impossible) to realize in a real deployment, especially when mutations violate implicit physical/protocol assumptions. This is common in fuzzing in general—high-coverage exploration often produces “weird” states. That is exactly why **triaging** is part of the workflow: we treat fuzzing as a high-recall bug discovery stage, then triage findings by replaying, minimizing, checking feasibility constraints (device/protocol/physics), and prioritizing issues that correspond to realistic executions and real security impact.



# Falsification (TODO).
Our Phy module, adds physics awareness in FastDyn. This brings ways to falsify. we do both :) .
