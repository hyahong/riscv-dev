#include <vector>
#include <algorithm>
#include <bench.h>

using Index = std::vector<uint64_t>;

static Index *table;

static uint64_t
prepare (Work *)
{
	if (table)
	{
		return 0;
	}

	table = new Index (node_count);

	for (uint64_t i = 0; i < node_count; ++i)
	{
		(*table)[i] = scatter (i);
	}
	std::sort (table->begin (), table->end ());

	return table->size ();
}

static void
lookup (Work &work)
{
	const Index &index = *table;
	uint64_t seed = work.seed;
	uint64_t found = 0, sum = 0;

	for (uint64_t i = 0; i < work.count; ++i)
	{
		seed = seed * 6364136223846793005ull + 1442695040888963407ull;

		const uint64_t key = scatter ((seed >> 33) & (node_count - 1));
		const auto it = std::lower_bound (index.begin (), index.end (), key);
		if (it != index.end () && *it == key)
		{
			++found;
			sum += *it;
		}
	}

	work.found = found;
	work.sum = sum;
}

static void
scan (Work &work)
{
	const Index &index = *table;
	uint64_t seed = work.seed;
	uint64_t found = 0, sum = 0;

	while (found < work.count)
	{
		seed = seed * 6364136223846793005ull + 1442695040888963407ull;

		auto it = std::lower_bound (index.begin (), index.end (), seed);
		for (uint64_t i = 0; i < scan_length && found < work.count; ++i)
		{
			if (it == index.end ())
			{
				it = index.begin ();
			}

			++found;
			sum += *it;
			++it;
		}
	}

	work.found = found;
	work.sum = sum;
}

extern "C" const Bench vector_lookup = { "VECTOR", "lookup", prepare, lookup };
extern "C" const Bench vector_scan = { "VECTOR", "scan", prepare, scan };
