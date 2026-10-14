#include <hnsw.h>
#include <malloc.h>
#include <printk.h>

using namespace hnsw;

constexpr size_t K = 1;
constexpr size_t EF = 100;
constexpr uintptr_t HNSW_DUMP_ADDRESS = 0xECCCC000;
constexpr uintptr_t DDR_END = 0x180000000;

static const char *
status_name (Status status)
{
	switch (status)
	{
		case Status::Ok:
			return "Ok";
		case Status::InvalidArgument:
			return "InvalidArgument";
		case Status::InvalidGraph:
			return "InvalidGraph";
		case Status::WorkspaceTooSmall:
			return "WorkspaceTooSmall";
		case Status::CandidateHeapFull:
			return "CandidateHeapFull";
	}
	return "Unknown";
}

static bool
graph_prepare (GraphView &graph)
{
	// check
	const DumpHeader *data = reinterpret_cast<const DumpHeader *> (HNSW_DUMP_ADDRESS);
	const uint64_t bytes = data->total_bytes;
	if (bytes < sizeof (DumpHeader) || bytes > DDR_END - HNSW_DUMP_ADDRESS)
	{
		printk ("Invalid HNSW dump size: %llu\n", (unsigned long long) bytes);
		return false;
	}

	printk ("HNSW dump: %p, %llu bytes\n", (const void *) data, (unsigned long long) bytes);

	// load dumped HNSW graph
	Status status = open_graph (data, bytes, graph);
	if (status != Status::Ok)
	{
		printk ("open_graph: %s\n", status_name (status));
		return false;
	}
	if (graph.h.dim_ != hnsw::dimension)
	{
		printk ("query dimension mismatch: graph=%llu, query=%lu\n", (unsigned long long) graph.h.dim_, (unsigned long) hnsw::dimension);
		return false;
	}

	printk ("nodes: %lu, dim: %llu, entry ID: %u, max level: %d\n", (unsigned long) graph.h.cur_element_count, (unsigned long long) graph.h.dim_, graph.h.enterpoint_node_, graph.h.maxlevel_);

	return true;
}

static bool
graph_search (GraphView &graph, Workspace &work, const int16_t *query)
{
	Result result[K];
	size_t count = 0;
	Status status;
	uint64_t start_cycle, end_cycle;

	asm volatile ("csrr %0, mcycle" : "=r" (start_cycle) : : "memory");
	status = search_knn (graph, query, K, EF, work, result, K, count);
	asm volatile ("csrr %0, mcycle" : "=r" (end_cycle) : : "memory");
	printk ("search_knn: cycles=%llu\n", (unsigned long long) (end_cycle - start_cycle));
	if (status != Status::Ok)
	{
		printk ("search_knn failed: %s\n", status_name (status));
		return false;
	}
	for (size_t i = 0; i < count; ++i)
	{
		printk ("label=%llu\n", (unsigned long long) result[i].label);
	}

	return true;
}

static bool
workspace_prepare (GraphView &graph, Workspace &work)
{
	const size_t n = size_t (graph.h.cur_element_count);
	const size_t ef = EF > K ? EF : K;
	const size_t nearest_capacity = (ef < n ? ef : n) + 1;

	work = {
		static_cast<HeapItem *> (malloc (n * sizeof (HeapItem))),
		n,
		static_cast<HeapItem *> (malloc (nearest_capacity * sizeof (HeapItem))),
		nearest_capacity,
		static_cast<uint32_t *> (malloc (n * sizeof (uint32_t))),
		n,
		0
	};

	if (!work.candidates || !work.nearest || !work.visited)
	{
		printk ("cannot allocate search workspace\n");
		return false;
	}

	initialize_workspace(work);
	return true;
}

static void
workspace_release (Workspace &work)
{
	if (work.candidates)
		free (work.candidates);
	if (work.nearest)
		free (work.nearest);
	if (work.visited)
		free (work.visited);

	work.candidates = nullptr;
	work.nearest = nullptr;
	work.visited = nullptr;
}

extern "C"
void
hnsw_demo (void)
{
	GraphView graph;
	Workspace work;

	if (!graph_prepare (graph))
	{
		return;
	}
	if (!workspace_prepare(graph, work))
	{
		return;
	}
	if (!graph_search(graph, work, hnsw::queries[0]))
	{
		return;
	}
	workspace_release (work);
}

