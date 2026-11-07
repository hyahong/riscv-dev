#include <hnsw.h>
#include <base.h>
#include <printk.h>

using namespace hnsw;

constexpr size_t QueryCount = hnsw::query_count;
constexpr size_t WorkspaceCount = 1;
constexpr size_t K = 1;
constexpr size_t EF = 100;

static GraphView graph;
static Workspace works[WorkspaceCount] { };
static Result results[QueryCount] { };
static Status statuses[QueryCount];

static void
hnsw_run (void)
{
	uint64_t start_cycle, end_cycle;

	printk ("[HNSW] START queries=%lu\n", (unsigned long) QueryCount);

	asm volatile ("csrr %0, mcycle" : "=r" (start_cycle) : : "memory");

	for (size_t i = 0; i < QueryCount; i++)
	{
		statuses[i] = search_knn (static_cast<int> (i), graph, hnsw::queries[i], K, EF, *works, &results[i]);
	}

	asm volatile ("csrr %0, mcycle" : "=r" (end_cycle) : : "memory");

	for (size_t i = 0; i < QueryCount; ++i)
	{
		if (statuses[i] == Status::Ok)
		{
			printk ("%lu,%llu\n", (unsigned long) i, (unsigned long long) results[i].label);
		}
		else
		{
			printk ("search_knn query=%lu failed: %s\n", (unsigned long) i, status_name (statuses[i]));
		}
	}
	print_elapsed (start_cycle, end_cycle);
}

extern "C"
void
hnsw_demo (void)
{
	if (!graph_prepare (graph))
	{
		return;
	}

	if (!workspace_prepare (graph, works, WorkspaceCount, K, EF))
	{
		workspace_release (works, WorkspaceCount);
		return;
	}

	hnsw_run ();

	workspace_release (works, WorkspaceCount);
}

