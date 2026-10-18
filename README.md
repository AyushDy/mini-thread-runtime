# Mini Thread Runtime

An educational C++20 runtime for cooperative, stackful user-level threads. The
runtime runs every context on the calling operating-system thread; it does not
create `std::thread`s.

## Prerequisites and build

- Linux with GCC and its `ucontext` implementation (tested in Ubuntu WSL2).
- CMake 3.20 or newer.
- GNU Make (or another CMake-supported generator).

Configure and build out of source:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/producer_consumer
```

The build uses C++20 and enables `-Wall -Wextra -Wpedantic` for the library,
tests, and example.

## Architecture

- `Context` owns a stack and a `ucontext_t`. It starts an entry callable,
  switches back to the caller on `yield()`, and marks itself complete when the
  entry returns.
- `Thread` associates a stable ID and lifecycle state with one context and
  records an entry exception.
- `Scheduler` owns threads, selects ready IDs in FIFO order, dispatches their
  contexts, and handles joining and deadlock.
- `Mutex` transfers ownership directly to the next FIFO waiter on unlock.
- `Semaphore` transfers posted permits directly to FIFO waiters before storing
  any excess permits.
- `LockGuard` releases a mutex during ordinary C++ stack unwinding.

```text
Scheduler::run
   |
   +-- pop Ready thread --> Context::resume --> thread entry
   |                              ^                 |
   |                              |                 +-- yield / block
   |                              +--------------------- back to scheduler
   |
   +-- Finished/Failed --> wake joiners
   +-- no Ready threads, some Blocked --> report deadlock and unwind waiters
```

Create work with `Scheduler::spawn`, start execution with `run`, and use
`Scheduler::yield()` from an entry to give other ready threads a turn. A thread
can call `scheduler.join(id)` to wait for another thread. Calling `join` outside
a running thread runs scheduled work until completion. Invalid IDs and
self-joins throw standard exceptions. `Scheduler::exit()` ends the current
thread by unwinding its C++ stack. It uses an internal exception, so a
surrounding `catch (...)` can intercept it and allow code after `exit()` to
continue. Do not catch all exceptions around this call; exit is cooperative
control flow, not an uncatchable cancellation mechanism.

Thread states are `Ready`, `Running`, `Blocked`, `Finished`, and `Failed`.
`Scheduler::run()` drives all created work to completion, then rethrows a
thread-entry failure. Stored failures remain observable, so later calls to
`run()` report a prior failed thread again. A join also rethrows the target
thread's failure.

Use `LockGuard` rather than manually pairing mutex calls:

```cpp
mini_thread::LockGuard lock(mutex);
// Protected section. This thread may yield while holding the lock.
```

Mutexes are non-recursive and reject unlock by a non-owner. Semaphore waits
block at zero; posts wake queued waiters in FIFO order. Semaphore permit
overflow is reported.

## Limitations and safety notes

- `ucontext`, `getcontext`, `makecontext`, and `swapcontext` are Linux/POSIX
  platform facilities with limited portability. This project does not provide
  another backend.
- Scheduling is cooperative. A thread that never yields or blocks prevents
  other threads from running. There is no signal-driven preemption.
- The runtime is single-operating-system-thread-only and is not thread-safe.
- Context stacks are allocated per context (64 KiB by default, with a 16 KiB
  minimum). There is no guard page or stack-overflow detection.
- A started context cannot be cancelled by discarding its stack. The scheduler
  handles deadlock by waking blocked operations with an exception, allowing
  stack-local C++ objects to unwind before the context is destroyed. A
  standalone started `Context` must likewise be resumed to completion.
- Synchronization objects and objects captured by thread callables must outlive
  every thread using them. `LockGuard` must be constructed and destroyed inside
  a running runtime thread.
- `makecontext()` accepts variadic integer arguments, which do not portably
  transport arbitrary pointers. The context trampoline therefore takes no
  arguments and finds its active context through thread-local state.
- The implementation does not promise safety for asynchronous interruption,
  signal handlers, I/O blocking, or arbitrary cancellation.
- AddressSanitizer can produce false positives or require special fiber-switch
  integration for manually switched stacks; sanitizer compatibility has not
  been established here.

## Tests and example

CTest includes:

- `mini_thread_smoke_test`: confirms the C++20 test target builds and runs.
- `mini_thread_context_test`: checks two independent contexts, local-state
  preservation, completion, stack-size validation, and exception unwinding.
- `mini_thread_runtime_test`: checks lifecycle, round-robin yield, joining,
  error cases, mutex contention and FIFO handoff, semaphore permits and FIFO
  wake-up, and deadlock reporting.

`examples/producer_consumer.cpp` demonstrates a bounded buffer protected by a
cooperative mutex and counting semaphores.

Historical C experiments remain in the project root as reference programs and
are not part of the C++ library or its CTest suite.