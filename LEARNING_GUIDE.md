# Learning Guide

This project is meant to be read from the lowest-level mechanism upward. The
runtime is deliberately cooperative: calls to `yield`, `join`, mutex lock, or
semaphore wait are the points at which another user thread can run.

## Suggested reading order

1. **`tests/smoke_test.cpp`** — the smallest C++20 test target.
2. **`include/mini_thread/context.hpp`** — the public stackful-context API.
3. **`src/context.cpp`** — stack allocation, `ucontext` setup, switching, and
   returning to the caller.
4. **`tests/context_test.cpp`** — how two independent stacks preserve local
   variables across yields.
5. **`include/mini_thread/runtime.hpp`** — thread states and the small public
   scheduler/synchronization API.
6. **`src/runtime.cpp`** — ready queue, dispatch, joining, blocking, wake-up,
   exception propagation, and deadlock recovery.
7. **`tests/runtime_test.cpp`** — deterministic examples of lifecycle and
   synchronization behavior, including invalid operations.
8. **`examples/producer_consumer.cpp`** — combines the mutex and semaphores in
   a bounded-buffer example.
9. **`CMakeLists.txt`** — how the library, tests, and example are compiled and
   registered with CTest.
10. **`README.md`** — build instructions, architecture diagram, behavior, and
    limitations.

## Concepts to notice

- Each `Context` owns a separate stack. A context switch saves one execution
  point and restores another; it does not create an operating-system thread.
- `ThreadState` makes transitions visible. In particular, a blocked thread is
  not on the ready queue until the operation it is waiting for wakes it.
- `Scheduler::exit()` uses an internal exception to unwind the current stack
  and finish the thread without abandoning local C++ objects. Like any C++
  exception, a surrounding `catch (...)` can intercept it; doing so can let
  code after the call continue.
- The scheduler stores stable `Thread` objects behind `unique_ptr`, while the
  ready queue stores only IDs. This keeps the context trampoline's captured
  thread address stable as the collection grows.
- A mutex hands ownership to the oldest waiter before it is made ready. A
  semaphore passes a posted permit directly to the oldest waiter. This avoids
  a newly arriving thread stealing a just-released resource.
- Deadlock recovery is explicit: if no thread is ready but unfinished threads
  remain blocked, the scheduler wakes those blocked operations with an error.
  Their ordinary C++ stack frames unwind instead of being abandoned.
- Entry exceptions are recorded and surfaced after the scheduler has driven
  work to completion. A thread can also observe a target's failure through
  `join`.
- `makecontext()`'s variadic arguments are not a portable way to pass pointers.
  The trampoline takes no arguments and consults thread-local active-context
  state.

## Safe experiments

- Add another context in `context_test.cpp` and verify its local data after
  every yield.
- Add a third scheduled worker and assert exact ordering only where a yield or
  blocking operation establishes that order.
- Make two threads contend for the mutex and compare the protected counter
  against the expected total.
- Change the producer-consumer buffer capacity while keeping its empty/full
  permit accounting balanced.
- Deliberately create a deadlock and verify that `run()` reports it rather than
  spinning.

Avoid infinite loops without yields: they intentionally demonstrate the
cooperative scheduler's main limitation by preventing the scheduler from
regaining control. Do not switch contexts from a signal handler or treat this
runtime as preemptive.
