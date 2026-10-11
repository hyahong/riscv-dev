#include <coroutine>
#include <cstdint>
#include <printk.h>

namespace {

[[noreturn]] void
coro_fail (const char *message)
{
    printk ("coro: %s\n", message);
    for (;;)
        asm volatile ("wfi");
}

std::uint64_t
read_cycle ()
{
    std::uint64_t value;
    asm volatile ("csrr %0, mcycle" : "=r" (value) : : "memory");
    return value;
}

struct Task {
    struct promise_type;
    using handle_t = std::coroutine_handle<promise_type>;

    struct promise_type {
        Task
				get_return_object () noexcept
        {
            return Task {handle_t::from_promise (*this)};
        }

        std::suspend_always
				initial_suspend () noexcept
        {
            return {};
        }

        std::suspend_always
				final_suspend () noexcept
        {
            return {};
        }

        void
				return_void () noexcept
        {
        }

        void
				unhandled_exception () noexcept
        {
            coro_fail ("unhandled exception");
        }
    };

    handle_t h;

    explicit Task (handle_t handle) noexcept :
			h (handle)
    {
    }

    Task (const Task &) = delete;
    Task &operator= (const Task &) = delete;

    ~Task ()
    {
        if (h)
            h.destroy ();
    }

    bool
		done () const noexcept
    {
        return h.done ();
    }

    void
		resume () const noexcept
    {
        h.resume ();
    }
};

Task
worker ()
{
    printk ("coro: A\n");
    co_await std::suspend_always {};

    printk ("coro: B\n");
    co_await std::suspend_always {};

    printk ("coro: C\n");
}

} // namespace

extern "C"
void
coro_demo (void)
{
    const std::uint64_t cycle_start = read_cycle ();
    printk ("coro: begin\n");

    {
        Task task = worker ();
        unsigned n = 0;

        while (!task.done ()) {
            printk ("coro: resume %u\n", n++);
            task.resume ();
        }
    }

    const std::uint64_t cycle_end = read_cycle ();
    printk ("coro: end\n");
    printk ("coro: total cycle = %lu\n",
            static_cast<unsigned long> (cycle_end - cycle_start));
}
