#include <base.h>
#include <malloc.h>
#include <printk.h>

constexpr uintptr_t DumpAddress = 0xECCCC000;
constexpr uintptr_t DdrEnd = 0x180000000;

namespace hnsw
{

bool
graph_prepare (GraphView &graph)
{
	// check
	const DumpHeader *data = reinterpret_cast<const DumpHeader *> (DumpAddress);
	const uint64_t bytes = data->total_bytes;
	if (bytes < sizeof (DumpHeader) || bytes > DdrEnd - DumpAddress)
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
	if (graph.h.dim_ != dimension)
	{
		printk ("query dimension mismatch: graph=%llu, query=%lu\n", (unsigned long long) graph.h.dim_, (unsigned long) dimension);
		return false;
	}

	printk ("nodes: %lu, dim: %llu, entry ID: %u, max level: %d\n", (unsigned long) graph.h.cur_element_count, (unsigned long long) graph.h.dim_, graph.h.enterpoint_node_, graph.h.maxlevel_);

	return true;
}

bool
workspace_prepare (const GraphView &graph, Workspace *works, size_t count, size_t k, size_t ef)
{
	const size_t n = size_t (graph.h.cur_element_count);
	if (ef < k)
		ef = k;
	const size_t nearest_capacity = (ef < n ? ef : n) + 1;

	if (n > SIZE_MAX / sizeof (HeapItem) || n > SIZE_MAX / sizeof (uint32_t))
	{
		printk ("cannot allocate search workspace\n");
		return false;
	}

	for (size_t i = 0; i < count; i++)
	{
		works[i] = {
			static_cast<HeapItem *> (malloc (n * sizeof (HeapItem))),
			n,
			static_cast<HeapItem *> (malloc (nearest_capacity * sizeof (HeapItem))),
			nearest_capacity,
			static_cast<uint32_t *> (malloc (n * sizeof (uint32_t))),
			n,
			0
		};

		if (!works[i].candidates || !works[i].nearest || !works[i].visited)
		{
			printk ("cannot allocate search workspace\n");
			return false;
		}

		initialize_workspace (works[i]);
	}

	return true;
}

void
workspace_release (Workspace *works, size_t count)
{
	for (size_t i = 0; i < count; i++)
	{
		free (works[i].candidates);
		free (works[i].nearest);
		free (works[i].visited);
		works[i] = { };
	}
}

void
print_elapsed (uint64_t start_cycle, uint64_t end_cycle)
{
	constexpr unsigned long long clock = 40 * 1000 * 1000;
	unsigned long long elapsed_cycle;
	unsigned long long integer, fractional;

	elapsed_cycle = (unsigned long long) (end_cycle - start_cycle) / (clock / 1000);
	// elapsed_cycle is milliseconds
	integer = elapsed_cycle / 1000;
	fractional = elapsed_cycle % 1000;
	printk ("\n\nsearch. elapsed=%llu.%llu\n", integer, fractional);
}

} // namespace hnsw
