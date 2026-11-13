#include <map>
#include <bench.h>

using Index = std::map<uint64_t, uint64_t>;

static Index *tree;

static uint64_t
prepare (Work *)
{
	if (tree)
	{
		return 0;
	}

	tree = new Index;

	for (uint64_t i = 0; i < node_count; ++i)
	{
		tree->emplace (scatter (i), i);
	}

	return tree->size ();
}

static void
lookup (Work &work)
{
	const Index &index = *tree;
	uint64_t seed = work.seed;
	uint64_t found = 0, sum = 0;

	for (uint64_t i = 0; i < work.count; ++i)
	{
		seed = seed * 6364136223846793005ull + 1442695040888963407ull;

		const auto it = index.find (scatter ((seed >> 33) & (node_count - 1)));
		if (it != index.end ())
		{
			++found;
			sum += it->second;
		}
	}

	work.found = found;
	work.sum = sum;
}

static void
scan (Work &work)
{
	const Index &index = *tree;
	uint64_t seed = work.seed;
	uint64_t found = 0, sum = 0;

	while (found < work.count)
	{
		seed = seed * 6364136223846793005ull + 1442695040888963407ull;

		auto it = index.lower_bound (seed);
		for (uint64_t i = 0; i < scan_length && found < work.count; ++i)
		{
			if (it == index.end ())
			{
				it = index.begin ();
			}

			++found;
			sum += it->second;
			++it;
		}
	}

	work.found = found;
	work.sum = sum;
}

extern "C" const Bench map_lookup = { "MAP", "lookup", prepare, lookup };
extern "C" const Bench map_scan = { "MAP", "scan", prepare, scan };
