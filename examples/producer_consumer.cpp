#include "mini_thread/runtime.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

int main()
{
    constexpr int item_count = 10;
    std::array<int, 3> buffer{};
    std::size_t write_position = 0;
    std::size_t read_position = 0;
    mini_thread::Semaphore empty_slots(buffer.size());
    mini_thread::Semaphore full_slots;
    mini_thread::Mutex buffer_mutex;
    std::vector<int> consumed;

    mini_thread::Scheduler scheduler;
    scheduler.spawn([&] {
        for (int item = 1; item <= item_count; ++item) {
            empty_slots.wait();
            {
                mini_thread::LockGuard lock(buffer_mutex);
                buffer[write_position] = item;
                write_position = (write_position + 1) % buffer.size();
            }
            std::cout << "Produced " << item << '\n';
            full_slots.post();
        }
    });

    scheduler.spawn([&] {
        for (int item = 0; item < item_count; ++item) {
            full_slots.wait();
            int value = 0;
            {
                mini_thread::LockGuard lock(buffer_mutex);
                value = buffer[read_position];
                read_position = (read_position + 1) % buffer.size();
            }
            consumed.push_back(value);
            std::cout << "Consumed " << value << '\n';
            empty_slots.post();
        }
    });

    try {
        scheduler.run();
    } catch (const std::exception& error) {
        std::cerr << "Producer-consumer failed: " << error.what() << '\n';
        return 1;
    }

    if (consumed.size() != item_count) {
        std::cerr << "Incorrect number of consumed items\n";
        return 1;
    }
    for (int i = 0; i < item_count; ++i) {
        if (consumed[static_cast<std::size_t>(i)] != i + 1) {
            std::cerr << "Items were not consumed in FIFO order\n";
            return 1;
        }
    }

    std::cout << "Producer-consumer completed successfully\n";
    return 0;
}
