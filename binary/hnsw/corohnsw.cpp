#include <corohnsw.h>
#include <malloc.h>
#include <printk.h>

using namespace hnsw;

namespace
{
constexpr size_t Active = 2;
constexpr size_t K = 1;
constexpr size_t Ef = 100;
constexpr uintptr_t DumpAddress = 0xECCCC000;
constexpr uintptr_t DdrEnd = 0x180000000;
static_assert (Active > 0 && Active <= query_count);

static bool
prepare (const GraphView &graph, Workspace *works)
{
	const size_t n = graph.h.cur_element_count;
	const size_t nearest = (Ef < n ? Ef : n) + 1;
	for (size_t i = 0; i < Active; ++i)
	{
		works[i] = {
			static_cast<HeapItem *> (malloc (n * sizeof (HeapItem))),
			n,
			static_cast<HeapItem *> (malloc (nearest * sizeof (HeapItem))),
			nearest,
			static_cast<uint32_t *> (malloc (n * sizeof (uint32_t))),
			n,
			0
		};

		if (!works[i].candidates || !works[i].nearest || !works[i].visited)
			return false;

		initialize_workspace (works[i]);
	}
	return true;
}

static void
release (Workspace *works)
{
	for (size_t i = 0; i < Active; ++i)
	{
		free (works[i].candidates);
		free (works[i].nearest);
		free (works[i].visited);
	}
}
} // namespace

extern "C" void
corohnsw_demo ()
{
	static_assert (K == 1);

	GraphView graph;
	const auto *header = reinterpret_cast<const DumpHeader *> (DumpAddress);
	const uint64_t bytes = header->total_bytes;

	Workspace works[Active] { };

	uint64_t start_cycle, end_cycle;

	if (bytes > DdrEnd - DumpAddress || open_graph (header, bytes, graph) != Status::Ok || graph.h.dim_ != dimension)
	{
		printk ("CORO_DONE FAIL invalid graph\n");
		return;
	}

	if (!prepare (graph, works))
	{
		release (works);
		printk ("CORO_DONE FAIL allocation\n");
		return;
	}

	asm volatile ("csrr %0, mcycle" : "=r" (start_cycle) :: "memory");
	{
		SearchTask tasks[Active];
		size_t indices[Active];
		size_t next = 0, completed = 0;
		for (size_t slot = 0; slot < Active; ++slot)
		{
			indices[slot] = next++;
			const size_t q = indices[slot];
			tasks[slot] = co_search_knn (q, graph, queries[q], K, Ef, works[slot]);
		}
		while (completed < query_count)
		{
			for (size_t slot = 0; slot < Active; ++slot)
			{
				if (indices[slot] == query_count)
					continue;
				tasks[slot].resume ();
				if (!tasks[slot].done ())
					continue;
				++completed;
				if (next < query_count)
				{
					indices[slot] = next++;
					const size_t q = indices[slot];
					tasks[slot] = co_search_knn (q, graph, queries[q], K, Ef, works[slot]);
				}
				else
				{
					indices[slot] = query_count;
					tasks[slot] = SearchTask { };
				}
			}
		}
	}
	asm volatile ("csrr %0, mcycle" : "=r" (end_cycle) :: "memory");

	release (works);

	{
		constexpr unsigned long long clock = 40 * 1000 * 1000;
		unsigned long long elapsed_cycle;
		unsigned long long integer, fractional;

		elapsed_cycle = (unsigned long long) (end_cycle - start_cycle) / (clock / 1000);
		// elapsed_cycle is milliseconds
		integer = elapsed_cycle / 1000;
		fractional = elapsed_cycle % 1000;
		printk ("co_search. elapsed=%llu.%llu\n", integer, fractional);
	}
}
