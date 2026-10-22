#include <corohnsw.h>
#include <base.h>
#include <printk.h>

using namespace hnsw;

constexpr size_t Active = 2;
constexpr size_t K = 1;
constexpr size_t Ef = 100;
static_assert (Active > 0 && Active <= query_count);

static GraphView graph;
static Workspace works[Active] { };

static void
hnsw_run (void)
{
	SearchTask tasks[Active];
	size_t indices[Active];
	size_t next = 0, completed = 0;
	uint64_t start_cycle, end_cycle;

	printk ("[HNSW] START");

	asm volatile ("csrr %0, mcycle" : "=r" (start_cycle) :: "memory");

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

	asm volatile ("csrr %0, mcycle" : "=r" (end_cycle) :: "memory");

	print_elapsed (start_cycle, end_cycle);
}

extern "C" void
corohnsw_demo ()
{
	if (!graph_prepare (graph))
	{
		printk ("invalid graph.\n");
		return;
	}

	if (!workspace_prepare (graph, works, Active, K, Ef))
	{
		workspace_release (works, Active);
		printk ("failed to allocate the memory.\n");
		return;
	}

	hnsw_run ();

	workspace_release (works, Active);
}
