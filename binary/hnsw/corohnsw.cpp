#include <corohnsw.h>
#include <printk.h>

constexpr uint32_t coroutine_count = 2;
constexpr uint32_t step_count = 3;

static Coro::Task
worker (unsigned int cid, void *)
{
	for (uint32_t step = 0; step < step_count; ++step)
	{
		printk ("[CORO] cid=%u step=%u\n", cid, step);

		co_await Coro::yield ();
	}
}

extern "C"
void
corohnsw_demo (void)
{
	Coro coro;

	for (uint32_t cid = 0; cid < coroutine_count; ++cid)
	{
		if (!coro.addCoroutine (worker, nullptr))
		{
			printk ("failed to add the coroutine\n");
			return;
		}
	}

	coro.run ();
}
