#pragma once

#include "mini_thread/context.hpp"

#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace mini_thread {

using ThreadId = std::size_t;

enum class ThreadState {
    Ready,
    Running,
    Blocked,
    Finished,
    Failed
};

class Thread final {
public:
    [[nodiscard]] ThreadId id() const noexcept { return id_; }
    [[nodiscard]] ThreadState state() const noexcept { return state_; }
    [[nodiscard]] bool finished() const noexcept
    {
        return state_ == ThreadState::Finished ||
               state_ == ThreadState::Failed;
    }

private:
    friend class Scheduler;
    friend class Mutex;
    friend class Semaphore;

    Thread(ThreadId id, Context::Entry entry, std::size_t stack_size);
    void run_entry() noexcept;

    ThreadId id_;
    ThreadState state_ = ThreadState::Ready;
    Context::Entry entry_;
    std::unique_ptr<Context> context_;
    std::exception_ptr failure_;
    std::exception_ptr wake_failure_;
    std::vector<ThreadId> joiners_;
};

class Mutex;
class Semaphore;

class Scheduler final {
public:
    explicit Scheduler(std::size_t stack_size = 64 * 1024);
    ~Scheduler() = default;

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&) = delete;
    Scheduler& operator=(Scheduler&&) = delete;

    ThreadId spawn(Context::Entry entry);
    void run();
    void join(ThreadId id);

    static void yield();
    [[noreturn]] static void exit();

    [[nodiscard]] const Thread& thread(ThreadId id) const;

private:
    friend class Mutex;
    friend class Semaphore;

    static Scheduler& active();
    static ThreadId active_thread_id();

    void yield_current();
    void block_current();
    void wake(ThreadId id);
    void check_wake_failure(ThreadId id);
    void wake_joiners(Thread& thread);
    void recover_deadlock();
    [[nodiscard]] bool all_finished() const noexcept;

    std::size_t stack_size_;
    std::vector<std::unique_ptr<Thread>> threads_;
    std::deque<ThreadId> ready_;
    ThreadId current_id_ = 0;
    bool has_current_ = false;
    bool running_ = false;

    static thread_local Scheduler* active_scheduler_;
};

class Mutex final {
public:
    void lock();
    void unlock();

private:
    Scheduler* scheduler_ = nullptr;
    std::optional<ThreadId> owner_;
    std::deque<ThreadId> waiters_;
};

class Semaphore final {
public:
    explicit Semaphore(std::size_t initial_permits = 0) noexcept
        : permits_(initial_permits)
    {
    }

    void wait();
    void post();

private:
    std::size_t permits_;
    std::deque<ThreadId> waiters_;
};

class LockGuard final {
public:
    explicit LockGuard(Mutex& mutex) : mutex_(&mutex)
    {
        mutex_->lock();
    }

    ~LockGuard() noexcept
    {
        if (mutex_ != nullptr) {
            mutex_->unlock();
        }
    }

    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
    LockGuard(LockGuard&&) = delete;
    LockGuard& operator=(LockGuard&&) = delete;

private:
    Mutex* mutex_;
};

} // namespace mini_thread
