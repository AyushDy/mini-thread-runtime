#include "mini_thread/runtime.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_yield_and_join()
{
    mini_thread::Scheduler scheduler;
    std::vector<int> order;
    int first_local = 0;
    int second_local = 10;

    const auto first = scheduler.spawn([&] {
        first_local = 1;
        order.push_back(1);
        mini_thread::Scheduler::yield();
        require(first_local == 1, "first thread local changed across yield");
        order.push_back(3);
    });
    const auto second = scheduler.spawn([&] {
        second_local = 20;
        order.push_back(2);
        mini_thread::Scheduler::yield();
        require(second_local == 20, "second thread local changed across yield");
        order.push_back(4);
        scheduler.join(first);
        order.push_back(5);
    });

    scheduler.run();
    require(order == std::vector<int>({1, 2, 3, 4, 5}),
            "round-robin execution or join ordering failed");
    require(scheduler.thread(first).state() == mini_thread::ThreadState::Finished,
            "first thread did not finish");
    require(scheduler.thread(second).finished(), "second thread did not finish");
    scheduler.join(first);
}

void test_multiple_joiners()
{
    mini_thread::Scheduler scheduler;
    std::vector<int> order;
    mini_thread::ThreadId target = 0;

    target = scheduler.spawn([&] {
        mini_thread::Scheduler::yield();
        order.push_back(1);
    });
    scheduler.spawn([&] {
        scheduler.join(target);
        order.push_back(2);
    });
    scheduler.spawn([&] {
        scheduler.join(target);
        order.push_back(3);
    });

    scheduler.run();
    require(order == std::vector<int>({1, 2, 3}),
            "multiple joiners were not awakened after target completion");
}

void test_join_waits_for_blocked_target()
{
    mini_thread::Scheduler scheduler;
    mini_thread::Semaphore release_target;
    bool target_completed = false;
    bool join_returned = false;

    const auto target = scheduler.spawn([&] {
        release_target.wait();
        target_completed = true;
    });
    scheduler.spawn([&] {
        scheduler.join(target);
        require(target_completed, "join returned before blocked target finished");
        join_returned = true;
    });
    scheduler.spawn([&] {
        mini_thread::Scheduler::yield();
        release_target.post();
    });

    scheduler.run();
    require(join_returned, "joiner was not resumed after blocked target finished");
}

void test_scheduler_reuse_and_mutex_fifo()
{
    mini_thread::Scheduler scheduler;
    mini_thread::Mutex mutex;
    std::vector<int> order;

    scheduler.spawn([&] {
        mini_thread::LockGuard lock(mutex);
        mini_thread::Scheduler::yield();
    });
    scheduler.spawn([&] {
        mini_thread::LockGuard lock(mutex);
        order.push_back(1);
    });
    scheduler.spawn([&] {
        mini_thread::LockGuard lock(mutex);
        order.push_back(2);
    });

    scheduler.run();
    require(order == std::vector<int>({1, 2}),
            "mutex waiters were not handed ownership in FIFO order");

    int later_thread_ran = 0;
    scheduler.spawn([&] { ++later_thread_ran; });
    scheduler.run();
    require(later_thread_ran == 1,
            "scheduler did not accept work after a completed run");
}

void test_invalid_operations()
{
    mini_thread::Scheduler scheduler;
    bool empty_entry = false;
    try {
        scheduler.spawn({});
    } catch (const std::invalid_argument&) {
        empty_entry = true;
    }
    require(empty_entry, "empty thread entry was accepted");

    bool invalid_id = false;
    try {
        scheduler.join(99);
    } catch (const std::out_of_range&) {
        invalid_id = true;
    }
    require(invalid_id, "invalid thread ID was accepted");

    bool self_join = false;
    scheduler.spawn([&] {
        try {
            scheduler.join(0);
        } catch (const std::logic_error&) {
            self_join = true;
        }
    });
    scheduler.run();
    require(self_join, "self-join was not rejected");

    bool outside_yield = false;
    try {
        mini_thread::Scheduler::yield();
    } catch (const std::logic_error&) {
        outside_yield = true;
    }
    require(outside_yield, "yield outside a scheduled thread was accepted");
}

void test_entry_failure()
{
    mini_thread::Scheduler scheduler;
    bool peer_ran = false;
    bool later_thread_ran = false;
    const auto failed = scheduler.spawn([] {
        throw std::runtime_error("thread entry failure");
    });
    scheduler.spawn([&] { peer_ran = true; });

    bool propagated = false;
    try {
        scheduler.run();
    } catch (const std::runtime_error& error) {
        propagated = std::string_view(error.what()) == "thread entry failure";
    }
    require(propagated, "thread exception was not propagated");
    require(peer_ran, "entry failure prevented other ready threads from running");
    require(scheduler.thread(failed).state() == mini_thread::ThreadState::Failed,
            "thread failure state was not recorded");

    scheduler.spawn([&] { later_thread_ran = true; });
    bool old_failure_repeated = false;
    try {
        scheduler.run();
    } catch (const std::runtime_error& error) {
        old_failure_repeated =
            std::string_view(error.what()) == "thread entry failure";
    }
    require(old_failure_repeated && later_thread_ran,
            "repeated run did not run new work and report stored failure");
}

void test_explicit_exit_unwinds_stack()
{
    mini_thread::Scheduler scheduler;
    bool scope_unwound = false;
    bool code_after_exit_ran = false;

    const auto id = scheduler.spawn([&] {
        struct ScopeMarker {
            bool& unwound;
            ~ScopeMarker() { unwound = true; }
        };

        [[maybe_unused]] ScopeMarker marker{scope_unwound};
        mini_thread::Scheduler::exit();
        code_after_exit_ran = true;
    });

    scheduler.run();
    require(scope_unwound, "explicit thread exit skipped stack unwinding");
    require(!code_after_exit_ran, "thread continued after explicit exit");
    require(scheduler.thread(id).state() == mini_thread::ThreadState::Finished,
            "explicit thread exit was not normal completion");
}

void test_explicit_exit_can_be_caught_by_catch_all()
{
    mini_thread::Scheduler scheduler;
    bool exit_was_caught = false;
    bool code_after_exit_ran = false;

    scheduler.spawn([&] {
        try {
            mini_thread::Scheduler::exit();
        } catch (...) {
            exit_was_caught = true;
        }
        code_after_exit_ran = true;
    });

    scheduler.run();
    require(exit_was_caught && code_after_exit_ran,
            "catch-all interception of cooperative exit behavior changed");
}

void test_mutex_and_semaphore()
{
    mini_thread::Scheduler scheduler;
    mini_thread::Mutex mutex;
    mini_thread::Semaphore available(0);
    int counter = 0;
    std::vector<int> sequence;

    scheduler.spawn([&] {
        for (int i = 0; i < 50; ++i) {
            mini_thread::LockGuard lock(mutex);
            const int old_value = counter;
            mini_thread::Scheduler::yield();
            counter = old_value + 1;
        }
        sequence.push_back(1);
        available.post();
    });

    scheduler.spawn([&] {
        for (int i = 0; i < 50; ++i) {
            mini_thread::LockGuard lock(mutex);
            const int old_value = counter;
            mini_thread::Scheduler::yield();
            counter = old_value + 1;
        }
        sequence.push_back(2);
        available.post();
    });

    scheduler.spawn([&] {
        available.wait();
        sequence.push_back(3);
        available.wait();
        sequence.push_back(4);
    });

    scheduler.run();
    require(counter == 100, "mutex failed to protect shared counter");
    const auto first_wakeup = std::find(sequence.begin(), sequence.end(), 3);
    const auto second_wakeup = std::find(sequence.begin(), sequence.end(), 4);
    require(first_wakeup != sequence.end() &&
                second_wakeup != sequence.end() &&
                first_wakeup < second_wakeup,
            "semaphore waiter did not resume after a permit was posted");
}

void test_semaphore_fifo_wake_order()
{
    mini_thread::Scheduler scheduler;
    mini_thread::Semaphore semaphore;
    std::vector<int> awakened;

    scheduler.spawn([&] {
        semaphore.wait();
        awakened.push_back(1);
    });
    scheduler.spawn([&] {
        semaphore.wait();
        awakened.push_back(2);
    });
    scheduler.spawn([&] {
        semaphore.post();
        semaphore.post();
    });

    scheduler.run();
    require(awakened == std::vector<int>({1, 2}),
            "semaphore waiters were not awakened in FIFO order");

    mini_thread::Scheduler permit_scheduler;
    mini_thread::Semaphore permits(2);
    int permit_count = 0;
    permit_scheduler.spawn([&] {
        permits.wait();
        ++permit_count;
        permits.wait();
        ++permit_count;
    });
    permit_scheduler.run();
    require(permit_count == 2, "initial semaphore permits were not counted");
}

void test_mutex_errors()
{
    mini_thread::Scheduler scheduler;
    mini_thread::Mutex mutex;
    bool recursive_rejected = false;
    bool non_owner_rejected = false;

    scheduler.spawn([&] {
        mutex.lock();
        try {
            mutex.lock();
        } catch (const std::logic_error&) {
            recursive_rejected = true;
        }
        mutex.unlock();
    });
    scheduler.spawn([&] {
        mini_thread::Scheduler::yield();
        try {
            mutex.unlock();
        } catch (const std::logic_error&) {
            non_owner_rejected = true;
        }
    });

    scheduler.run();
    require(recursive_rejected, "recursive mutex lock was accepted");
    require(non_owner_rejected, "non-owner mutex unlock was accepted");

    bool unlock_outside_rejected = false;
    try {
        mutex.unlock();
    } catch (const std::logic_error&) {
        unlock_outside_rejected = true;
    }
    require(unlock_outside_rejected, "mutex unlock outside runtime was accepted");
}

void test_semaphore_and_deadlock_errors()
{
    mini_thread::Scheduler semaphore_scheduler;
    mini_thread::Semaphore semaphore;
    bool outside_wait_rejected = false;
    try {
        semaphore.wait();
    } catch (const std::logic_error&) {
        outside_wait_rejected = true;
    }
    require(outside_wait_rejected, "semaphore wait outside runtime was accepted");
    bool outside_post_rejected = false;
    try {
        semaphore.post();
    } catch (const std::logic_error&) {
        outside_post_rejected = true;
    }
    require(outside_post_rejected, "semaphore post outside runtime was accepted");

    mini_thread::Scheduler deadlocked_scheduler;
    mini_thread::Semaphore never_posted;
    bool deadlock_reported = false;
    const auto blocked = deadlocked_scheduler.spawn([&] {
        never_posted.wait();
    });
    try {
        deadlocked_scheduler.run();
    } catch (const std::runtime_error& error) {
        deadlock_reported =
            std::string_view(error.what()).find("deadlock") != std::string_view::npos;
    }
    require(deadlock_reported, "scheduler deadlock was not reported");
    require(deadlocked_scheduler.thread(blocked).state() ==
                mini_thread::ThreadState::Failed,
            "deadlocked thread did not unwind to completion");

    bool permit_overflow = false;
    mini_thread::Scheduler overflow_scheduler;
    mini_thread::Semaphore maximum(std::numeric_limits<std::size_t>::max());
    overflow_scheduler.spawn([&] {
        try {
            maximum.post();
        } catch (const std::overflow_error&) {
            permit_overflow = true;
        }
    });
    overflow_scheduler.run();
    require(permit_overflow, "semaphore permit overflow was accepted");
}

void test_mutex_deadlock_unwinds_guards()
{
    mini_thread::Scheduler scheduler;
    mini_thread::Mutex first;
    mini_thread::Mutex second;
    mini_thread::ThreadId first_id = 0;
    mini_thread::ThreadId second_id = 0;

    first_id = scheduler.spawn([&] {
        mini_thread::LockGuard first_lock(first);
        mini_thread::Scheduler::yield();
        mini_thread::LockGuard second_lock(second);
    });
    second_id = scheduler.spawn([&] {
        mini_thread::LockGuard second_lock(second);
        mini_thread::Scheduler::yield();
        mini_thread::LockGuard first_lock(first);
    });

    bool deadlock_reported = false;
    try {
        scheduler.run();
    } catch (const std::runtime_error& error) {
        deadlock_reported =
            std::string_view(error.what()).find("deadlock") != std::string_view::npos;
    }

    require(deadlock_reported, "mutex deadlock was not reported");
    require(scheduler.thread(first_id).state() == mini_thread::ThreadState::Failed &&
                scheduler.thread(second_id).state() == mini_thread::ThreadState::Failed,
            "deadlocked mutex owners did not unwind to failure");

    bool locks_released = false;
    scheduler.spawn([&] {
        mini_thread::LockGuard first_lock(first);
        mini_thread::LockGuard second_lock(second);
        locks_released = true;
    });
    try {
        scheduler.run();
    } catch (const std::runtime_error&) {
        // Previously failed threads remain failed, so run reports their error again.
    }
    require(locks_released, "deadlock recovery left a mutex locked");
}

} // namespace

int main()
{
    try {
        test_yield_and_join();
        test_multiple_joiners();
        test_join_waits_for_blocked_target();
        test_scheduler_reuse_and_mutex_fifo();
        test_invalid_operations();
        test_entry_failure();
        test_explicit_exit_unwinds_stack();
        test_explicit_exit_can_be_caught_by_catch_all();
        test_mutex_and_semaphore();
        test_semaphore_fifo_wake_order();
        test_mutex_errors();
        test_semaphore_and_deadlock_errors();
        test_mutex_deadlock_unwinds_guards();
        std::cout << "Runtime test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Runtime test failed: " << error.what() << '\n';
        return 1;
    }
}
