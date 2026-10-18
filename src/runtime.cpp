#include "mini_thread/runtime.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace mini_thread {

namespace {

struct ThreadExit final {};

} // namespace

thread_local Scheduler* Scheduler::active_scheduler_ = nullptr;

Thread::Thread(ThreadId id, Context::Entry entry, std::size_t stack_size)
    : id_(id), entry_(std::move(entry))
{
    context_ = std::make_unique<Context>(
        [this] { run_entry(); }, stack_size);
}

void Thread::run_entry() noexcept
{
    try {
        entry_();
    } catch (const ThreadExit&) {
        return;
    } catch (...) {
        failure_ = std::current_exception();
    }
}

Scheduler::Scheduler(std::size_t stack_size) : stack_size_(stack_size)
{
    if (stack_size_ == 0) {
        throw std::invalid_argument("scheduler stack size must be nonzero");
    }
}

ThreadId Scheduler::spawn(Context::Entry entry)
{
    if (!entry) {
        throw std::invalid_argument("thread entry function is empty");
    }
    if (running_) {
        if (active_scheduler_ != this || !has_current_) {
            throw std::logic_error("cannot spawn from outside the running scheduler");
        }
    }

    const ThreadId id = threads_.size();
    auto new_thread = std::unique_ptr<Thread>(
        new Thread(id, std::move(entry), stack_size_));
    threads_.push_back(std::move(new_thread));
    ready_.push_back(id);
    return id;
}

void Scheduler::run()
{
    if (running_) {
        throw std::logic_error("scheduler is already running");
    }
    if (active_scheduler_ != nullptr) {
        throw std::logic_error("nested scheduler execution is not supported");
    }

    running_ = true;
    try {
        while (!all_finished()) {
            if (ready_.empty()) {
                recover_deadlock();
                continue;
            }

            const ThreadId id = ready_.front();
            ready_.pop_front();
            Thread& current = *threads_.at(id);
            if (current.state_ != ThreadState::Ready) {
                continue;
            }

            current_id_ = id;
            has_current_ = true;
            current.state_ = ThreadState::Running;
            active_scheduler_ = this;

            try {
                current.context_->resume();
            } catch (...) {
                current.failure_ = std::current_exception();
                if (!current.context_->completed()) {
                    current.state_ = ThreadState::Failed;
                    wake_joiners(current);
                }
            }

            active_scheduler_ = nullptr;
            has_current_ = false;

            if (current.context_->completed()) {
                if (current.failure_) {
                    current.state_ = ThreadState::Failed;
                } else {
                    current.state_ = ThreadState::Finished;
                }
                wake_joiners(current);
            } else if (current.state_ == ThreadState::Running) {
                current.state_ = ThreadState::Ready;
                ready_.push_back(id);
            }
        }
    } catch (...) {
        active_scheduler_ = nullptr;
        has_current_ = false;
        running_ = false;
        throw;
    }

    running_ = false;
    for (const auto& thread_ptr : threads_) {
        if (thread_ptr->failure_) {
            std::rethrow_exception(thread_ptr->failure_);
        }
    }
}

void Scheduler::join(ThreadId id)
{
    if (id >= threads_.size()) {
        throw std::out_of_range("invalid thread ID");
    }
    if (has_current_ && active_scheduler_ == this && id == current_id_) {
        throw std::logic_error("a thread cannot join itself");
    }

    if (!has_current_ || active_scheduler_ != this) {
        run();
        if (threads_[id]->failure_) {
            std::rethrow_exception(threads_[id]->failure_);
        }
        return;
    }

    Thread& target = *threads_[id];
    while (!target.finished()) {
        target.joiners_.push_back(current_id_);
        try {
            block_current();
            check_wake_failure(current_id_);
        } catch (...) {
            auto& joiners = target.joiners_;
            joiners.erase(std::remove(joiners.begin(), joiners.end(), current_id_),
                          joiners.end());
            throw;
        }
    }
    if (target.failure_) {
        std::rethrow_exception(target.failure_);
    }
}

void Scheduler::yield()
{
    active().yield_current();
}

[[noreturn]] void Scheduler::exit()
{
    static_cast<void>(active());
    throw ThreadExit{};
}

const Thread& Scheduler::thread(ThreadId id) const
{
    if (id >= threads_.size()) {
        throw std::out_of_range("invalid thread ID");
    }
    return *threads_[id];
}

Scheduler& Scheduler::active()
{
    if (active_scheduler_ == nullptr || !active_scheduler_->has_current_) {
        throw std::logic_error("operation requires a running scheduler thread");
    }
    return *active_scheduler_;
}

ThreadId Scheduler::active_thread_id()
{
    return active().current_id_;
}

void Scheduler::yield_current()
{
    Thread& current = *threads_.at(current_id_);
    current.state_ = ThreadState::Ready;
    ready_.push_back(current_id_);
    Context::yield();
}

void Scheduler::block_current()
{
    Thread& current = *threads_.at(current_id_);
    current.state_ = ThreadState::Blocked;
    Context::yield();
}

void Scheduler::wake(ThreadId id)
{
    Thread& thread = *threads_.at(id);
    if (thread.state_ == ThreadState::Blocked) {
        thread.state_ = ThreadState::Ready;
        ready_.push_back(id);
    }
}

void Scheduler::check_wake_failure(ThreadId id)
{
    Thread& thread = *threads_.at(id);
    if (thread.wake_failure_) {
        auto failure = std::exchange(thread.wake_failure_, nullptr);
        std::rethrow_exception(failure);
    }
}

void Scheduler::wake_joiners(Thread& thread)
{
    for (const ThreadId id : thread.joiners_) {
        wake(id);
    }
    thread.joiners_.clear();
}

void Scheduler::recover_deadlock()
{
    bool found_blocked = false;
    for (const auto& thread_ptr : threads_) {
        if (thread_ptr->state_ == ThreadState::Blocked) {
            found_blocked = true;
            thread_ptr->wake_failure_ = std::make_exception_ptr(
                std::runtime_error("scheduler deadlock: no runnable threads"));
            thread_ptr->state_ = ThreadState::Ready;
            ready_.push_back(thread_ptr->id_);
        }
    }
    if (!found_blocked) {
        throw std::logic_error("scheduler stopped with unfinished threads");
    }
}

bool Scheduler::all_finished() const noexcept
{
    return std::all_of(threads_.begin(), threads_.end(),
                       [](const auto& thread) { return thread->finished(); });
}

void Mutex::lock()
{
    Scheduler& scheduler = Scheduler::active();
    const ThreadId id = Scheduler::active_thread_id();
    if (owner_ && scheduler_ != &scheduler) {
        throw std::logic_error("mutex is owned by a different scheduler");
    }
    if (!owner_) {
        scheduler_ = &scheduler;
        owner_ = id;
        return;
    }
    if (*owner_ == id) {
        throw std::logic_error("recursive mutex lock");
    }

    waiters_.push_back(id);
    try {
        scheduler.block_current();
        scheduler.check_wake_failure(id);
    } catch (...) {
        waiters_.erase(std::remove(waiters_.begin(), waiters_.end(), id),
                       waiters_.end());
        throw;
    }
}

void Mutex::unlock()
{
    Scheduler& scheduler = Scheduler::active();
    const ThreadId id = Scheduler::active_thread_id();
    if (!owner_ || scheduler_ != &scheduler || *owner_ != id) {
        throw std::logic_error("mutex unlock by non-owner");
    }

    while (!waiters_.empty()) {
        const ThreadId next = waiters_.front();
        waiters_.pop_front();
        if (!scheduler.threads_.at(next)->wake_failure_) {
            owner_ = next;
            scheduler.wake(next);
            return;
        }
    }
    owner_.reset();
    scheduler_ = nullptr;
}

void Semaphore::wait()
{
    Scheduler& scheduler = Scheduler::active();
    const ThreadId id = Scheduler::active_thread_id();

    if (permits_ != 0) {
        --permits_;
        return;
    }

    waiters_.push_back(id);
    try {
        scheduler.block_current();
        scheduler.check_wake_failure(id);
    } catch (...) {
        waiters_.erase(std::remove(waiters_.begin(), waiters_.end(), id),
                       waiters_.end());
        throw;
    }
}

void Semaphore::post()
{
    Scheduler& scheduler = Scheduler::active();

    if (waiters_.empty()) {
        if (permits_ == std::numeric_limits<std::size_t>::max()) {
            throw std::overflow_error("semaphore permit count overflow");
        }
        ++permits_;
        return;
    }

    while (!waiters_.empty()) {
        const ThreadId next = waiters_.front();
        waiters_.pop_front();
        if (!scheduler.threads_.at(next)->wake_failure_) {
            scheduler.wake(next);
            return;
        }
    }
    if (permits_ == std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error("semaphore permit count overflow");
    }
    ++permits_;
}

} // namespace mini_thread
