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

static bool
search (const int id, GraphView &graph, Workspace &work, const int16_t *query)
{
	static_assert (K == 1);
	Status status;

	status = search_knn (id, graph, query, K, EF, work);
	if (status != Status::Ok)
	{
		printk ("search_knn failed: %s\n", status_name (status));
		return false;
	}
	return true;
}

static void
hnsw_run (void)
{
	uint64_t start_cycle, end_cycle;

	printk ("[HNSW] START");

	asm volatile ("csrr %0, mcycle" : "=r" (start_cycle) : : "memory");

	for (size_t i = 0; i < QueryCount; i++)
	{
		(void) search (i, graph, *works, hnsw::queries[i]);
	}

	asm volatile ("csrr %0, mcycle" : "=r" (end_cycle) : : "memory");

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

