#include "mini_thread/context.hpp"

#include <cerrno>
#include <exception>
#include <stdexcept>
#include <system_error>
#include <ucontext.h>
#include <utility>

namespace mini_thread {

namespace {

constexpr std::size_t minimum_stack_size = 16 * 1024;

} // namespace

struct Context::Impl {
    Impl(Entry entry_function, std::size_t requested_stack_size)
        : stack(std::make_unique<std::byte[]>(requested_stack_size)),
          stack_size(requested_stack_size),
          entry(std::move(entry_function))
    {
        if (getcontext(&context) == -1) {
            throw std::system_error(errno, std::generic_category(),
                                    "getcontext");
        }

        context.uc_stack.ss_sp = stack.get();
        context.uc_stack.ss_size = stack_size;
        context.uc_stack.ss_flags = 0;
        context.uc_link = &caller_context;

        // The trampoline takes no makecontext arguments, avoiding the
        // platform-specific pointer-through-varargs behavior.
        makecontext(&context, &Context::entry_trampoline, 0);
    }

    std::unique_ptr<std::byte[]> stack;
    std::size_t stack_size;
    Entry entry;
    ucontext_t context{};
    ucontext_t caller_context{};
    std::exception_ptr entry_failure;
    bool entered = false;
    bool running = false;
    bool finished = false;
};

thread_local Context* Context::active_context_ = nullptr;

Context::Context(Entry entry, std::size_t stack_size)
{
    if (!entry) {
        throw std::invalid_argument("context entry function is empty");
    }
    if (stack_size < minimum_stack_size) {
        throw std::invalid_argument("context stack size must be at least 16 KiB");
    }

    impl_ = std::make_unique<Impl>(std::move(entry), stack_size);
}

Context::~Context()
{
    if (active_context_ == this ||
        (impl_->entered && !impl_->finished)) {
        std::terminate();
    }
}

void Context::resume()
{
    if (impl_->running) {
        throw std::logic_error("cannot resume a context that is running");
    }
    if (impl_->finished) {
        throw std::logic_error("cannot resume a completed context");
    }

    Context* previous_context = active_context_;
    impl_->running = true;
    active_context_ = this;

    if (swapcontext(&impl_->caller_context, &impl_->context) == -1) {
        const int error = errno;
        impl_->running = false;
        active_context_ = previous_context;
        throw std::system_error(error, std::generic_category(),
                                "swapcontext while resuming");
    }

    active_context_ = previous_context;
    if (impl_->entry_failure) {
        std::rethrow_exception(impl_->entry_failure);
    }
}

void Context::yield()
{
    Context* context = active_context_;
    if (context == nullptr || !context->impl_->running) {
        throw std::logic_error("yield called outside a running context");
    }

    context->impl_->running = false;
    if (swapcontext(&context->impl_->context,
                    &context->impl_->caller_context) == -1) {
        const int error = errno;
        context->impl_->running = true;
        throw std::system_error(error, std::generic_category(),
                                "swapcontext while yielding");
    }
    context->impl_->running = true;
}

bool Context::completed() const noexcept
{
    return impl_->finished;
}

void Context::entry_trampoline()
{
    Context* context = active_context_;
    if (context == nullptr) {
        std::terminate();
    }

    context->impl_->entered = true;
    try {
        context->impl_->entry();
    } catch (...) {
        context->impl_->entry_failure = std::current_exception();
    }

    context->impl_->finished = true;
    context->impl_->running = false;
}

} // namespace mini_thread
