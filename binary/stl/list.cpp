#include <list>
#include <bench.h>

using Index = std::list<uint64_t>;

static Index *lists;

static uint64_t
prepare (Work *works)
{
	uint64_t built = 0;

	if (!lists)
	{
		lists = new Index[coroutine_count];

		// node_count nodes in total, split evenly among the lists
		for (uint64_t i = 0; i < node_count; ++i)
		{
			lists[i / 2 % coroutine_count].push_back (scatter (i));
		}

		// sorted by value, a list no longer follows the order in memory
		for (uint32_t cid = 0; cid < coroutine_count; ++cid)
		{
			lists[cid].sort ();
		}

		built = node_count;
	}

	for (uint32_t cid = 0; cid < coroutine_count; ++cid)
	{
		works[cid].count = lists[cid].size ();
	}

	return built;
}

static void
traverse (Work &work)
{
	uint64_t found = 0, sum = 0;

	for (const uint64_t value : lists[work.cid])
	{
		++found;
		sum += value;
	}

	work.found = found;
	work.sum = sum;
}

extern "C" const Bench list_traverse = { "LIST", "traverse", prepare, traverse };
