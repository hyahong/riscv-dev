#include <queue>
#include <bench.h>

using Index = std::priority_queue<uint64_t>;

static Index *queues;

static uint64_t
prepare (Work *)
{
	// a run changes the queues, so every run builds them again
	delete[] queues;
	queues = new Index[coroutine_count];

	// node_count elements in each queue
	for (uint64_t i = 0; i < node_count * coroutine_count; ++i)
	{
		queues[i % coroutine_count].push (scatter (i));
	}

	return node_count * coroutine_count;
}

static void
replace (Work &work)
{
	Index &queue = queues[work.cid];
	uint64_t seed = work.seed;
	uint64_t found = 0, sum = 0;

	for (uint64_t i = 0; i < work.count; ++i)
	{
		seed = seed * 6364136223846793005ull + 1442695040888963407ull;

		++found;
		sum += queue.top ();
		queue.pop ();
		queue.push (seed);
	}

	work.found = found;
	work.sum = sum;
}

extern "C" const Bench priority_queue_replace = { "PRIORITY_QUEUE", "replace", prepare, replace };
