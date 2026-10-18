#pragma once

#include <cstddef>
#include <functional>
#include <memory>

namespace mini_thread {

// Linux ucontext-backed stackful context; this interface is not portable to
// platforms that do not provide ucontext. A started context must be resumed
// until completion before it is destroyed because suspended stack frames
// cannot be safely cancelled or unwound.
class Context final {
public:
    using Entry = std::function<void()>;

    explicit Context(Entry entry, std::size_t stack_size = 64 * 1024);
    ~Context();

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) = delete;
    Context& operator=(Context&&) = delete;

    void resume();
    static void yield();

    [[nodiscard]] bool completed() const noexcept;

private:
    struct Impl;

    static void entry_trampoline();
    static thread_local Context* active_context_;

    std::unique_ptr<Impl> impl_;
};

} // namespace mini_thread
