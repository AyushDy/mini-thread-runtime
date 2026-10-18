#include "mini_thread/context.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main()
{
    try {
        bool empty_entry_rejected = false;
        try {
            mini_thread::Context empty({});
        } catch (const std::invalid_argument&) {
            empty_entry_rejected = true;
        }
        require(empty_entry_rejected, "empty context entry was accepted");

        bool small_stack_rejected = false;
        try {
            mini_thread::Context too_small([] {}, 1);
        } catch (const std::invalid_argument&) {
            small_stack_rejected = true;
        }
        require(small_stack_rejected, "undersized context stack was accepted");

        int first_entries = 0;
        int second_entries = 0;
        int first_resumptions = 0;
        int second_resumptions = 0;

        mini_thread::Context first([&] {
            ++first_entries;
            require(first_entries == 1, "first context entry count");

            int local = 10;
            for (int expected = 11; expected <= 13; ++expected) {
                ++local;
                mini_thread::Context::yield();
                require(local == expected,
                        "first context local did not persist");
                ++first_resumptions;
            }
        });

        mini_thread::Context second([&] {
            ++second_entries;
            require(second_entries == 1, "second context entry count");

            int local = 100;
            for (int expected = 110; expected <= 130; expected += 10) {
                local += 10;
                mini_thread::Context::yield();
                require(local == expected,
                        "second context local did not persist");
                ++second_resumptions;
            }
        });

        require(!first.completed(), "first context completed before entry");
        require(!second.completed(), "second context completed before entry");

        first.resume();
        require(first_entries == 1 && !first.completed(),
                "first context did not start and yield");
        second.resume();
        require(second_entries == 1 && !second.completed(),
                "second context did not start and yield");

        first.resume();
        second.resume();
        require(first_resumptions == 1 && second_resumptions == 1,
                "contexts did not resume independently");

        first.resume();
        second.resume();
        require(first_resumptions == 2 && second_resumptions == 2,
                "contexts lost state on a later yield");

        first.resume();
        second.resume();
        require(first.completed() && second.completed(),
                "contexts did not complete normally");
        require(first_resumptions == 3 && second_resumptions == 3,
                "contexts did not pass through all suspension points");

        bool completed_resume_rejected = false;
        try {
            first.resume();
        } catch (const std::logic_error&) {
            completed_resume_rejected = true;
        }
        require(completed_resume_rejected,
                "resuming a completed context was accepted");

        bool outside_yield_rejected = false;
        try {
            mini_thread::Context::yield();
        } catch (const std::logic_error&) {
            outside_yield_rejected = true;
        }
        require(outside_yield_rejected, "yield outside a context was accepted");

        bool entry_unwound = false;
        mini_thread::Context failing([&] {
            struct ScopeMarker {
                bool& unwound;

                ~ScopeMarker()
                {
                    unwound = true;
                }
            };

            [[maybe_unused]] ScopeMarker marker{entry_unwound};
            throw std::runtime_error("entry failure");
        });

        bool failure_propagated = false;
        try {
            failing.resume();
        } catch (const std::runtime_error& error) {
            failure_propagated =
                std::string_view(error.what()) == "entry failure";
        }
        require(failure_propagated && entry_unwound && failing.completed(),
                "entry exception was not unwound and returned to caller");

        std::cout << "Context test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Context test failed: " << error.what() << '\n';
        return 1;
    }
}
