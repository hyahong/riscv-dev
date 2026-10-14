#include <coroutine>
#include <hnsw.h>
#include <malloc.h>
#include <printk.h>

using namespace hnsw;

constexpr size_t SMCORO = 8;
constexpr size_t K = 1;
constexpr size_t EF = 100;
constexpr uintptr_t HNSW_DUMP_ADDRESS = 0xECCCC000;
constexpr uintptr_t DDR_END = 0x180000000;

struct Task
{
	struct promise_type;
	using handle_t = std::coroutine_handle<promise_type>;

	struct promise_type
	{
			Task
			get_return_object () noexcept
			{
					return Task { handle_t::from_promise (*this) };
			}

			std::suspend_always
			initial_suspend () noexcept
			{
				return { };
			}

			std::suspend_always
			final_suspend () noexcept
			{
				return { };
			}

			void
			return_void () noexcept
			{
			}

			void
			unhandled_exception () noexcept
			{
				printk ("coro exception");
			}
	};

	handle_t h;

	explicit Task (handle_t handle = nullptr) noexcept :
		h (handle)
	{
	}

	Task (const Task &) = delete;
	Task &operator= (const Task &) = delete;

	Task (Task &&other) noexcept :
		h (other.h)
	{
		other.h = nullptr;
	}

	Task &operator= (Task &&other) noexcept
	{
		if (this != &other)
		{
			if (h)
				h.destroy ();
			h = other.h;
			other.h = nullptr;
		}
		return *this;
	}

	~Task ()
	{
		if (h)
			h.destroy ();
	}

	bool
	done () const noexcept
	{
		return h.done ();
	}

	void
	resume () const noexcept
	{
		h.resume ();
	}
};


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
workspace_prepare (GraphView &graph, Workspace *works)
{
	const size_t n = size_t (graph.h.cur_element_count);
	const size_t ef = EF > K ? EF : K;
	const size_t nearest_capacity = (ef < n ? ef : n) + 1;

	for (size_t i = 0; i < SMCORO; i++)
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

static void
workspace_release (Workspace *works)
{
	for (size_t i = 0; i < SMCORO; i++)
	{
		if (works[i].candidates)
		{
			free (works[i].candidates);
			works[i].candidates = nullptr;
		}
		if (works[i].nearest)
		{
			free (works[i].nearest);
			works[i].nearest = nullptr;
		}
		if (works[i].visited)
		{
			free (works[i].visited);
			works[i].visited = nullptr;
		}
	}
}

static Task
co_search (int id, GraphView &graph, Workspace &work, const int16_t *query)
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
		co_return;
	}
	for (size_t i = 0; i < count; ++i)
	{
		printk ("[%d] label=%llu\n", id, (unsigned long long) result[i].label);
	}

  co_await std::suspend_always { };
}

extern "C"
void
corohnsw_demo (void)
{
	GraphView graph;
	Workspace works[SMCORO];
	Task tasks[SMCORO];

	if (!graph_prepare (graph))
	{
		return;
	}

	if (!workspace_prepare(graph, works))
	{
		workspace_release (works);
		return;
	}

	for (size_t i = 0; i < SMCORO; i++)
	{
		tasks[i] = co_search (static_cast<int> (i), graph, works[i], hnsw::queries[i]);
	}
	bool done = true;
	while (done)
	{
		done = true;
		for (size_t i = 0; i < SMCORO; i++)
		{
			tasks[i].resume ();

			done = done && tasks[i].done ();
		}
	}

	workspace_release (works);

	printk ("[END]\n");
}

