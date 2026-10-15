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
			static void* operator new (std::size_t n)
			{
				printk ("coroutine frame: %u bytes\n", n);
				return ::operator new(n);
			}

			static void operator delete (void* p, std::size_t) noexcept
			{
				::operator delete(p);
			}

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

static Task
co_search (int id, const GraphView &graph, const int16_t *query, size_t k, size_t ef, Workspace &work)
{
	Result result[K];
	size_t count = 0;

	if (!graph.base || !query)
	{
		printk ("InvalidArgument\n");
		co_return;
	}

	const size_t n = size_t (graph.h.cur_element_count);

	if (k > n)
		k = n;
	if (ef < k)
		ef = k;
	if (ef > n)
		ef = n;

	if (!work.candidates || !work.nearest || !work.visited ||
			work.candidate_capacity == 0 || work.nearest_capacity < ef + 1 ||
			work.visited_capacity < n)
	{
		printk ("WorkspaceTooSmall\n");
		co_return;
	}

	next_epoch(work);

	// 1. greedy descent through the sparse upper levels
	uint32_t current = graph.h.enterpoint_node_;
	int64_t current_distance = graph.distance (query, current);
	for (int32_t level = graph.h.maxlevel_; level > 0; --level)
	{
		bool moved = true;
		while (moved)
		{
			const uint8_t *list;
			uint32_t count;

			moved = false;

			if (!graph.link_list (current, uint32_t (level), list, count))
			{
				printk ("InvalidGraph\n");
				co_return;
			}

			for (uint32_t j = 0; j < count; ++j)
			{
				const uint32_t id = reinterpret_cast<const uint32_t *> (list + 4)[j];
				if (id >= n)
				{
					printk ("InvalidGraph\n");
					co_return;
				}
				const int64_t distance = graph.distance (query, id);
				if (distance < current_distance)
				{
					current = id;
					current_distance = distance;
					moved = true;
				}
			}
		}
	}

	// 2. best-first L0 search
	Heap candidates = { work.candidates, 0, work.candidate_capacity, true };
	Heap nearest = { work.nearest, 0, work.nearest_capacity, false };
	const HeapItem entry = { current_distance, current };

	candidates.push (entry);
	nearest.push (entry);
	work.visited[current] = work.epoch;

	while (candidates.count != 0)
	{
		if (candidates.top ().distance > nearest.top ().distance)
			break;

		const HeapItem candidate = candidates.pop ();
		const uint8_t *list;
		uint32_t count;

		if (!graph.link_list (candidate.id, 0, list, count))
		{
			printk ("InvalidGraph\n");
			co_return;
		}

		for (uint32_t j = 0; j < count; ++j)
		{
			const uint32_t id = reinterpret_cast<const uint32_t *> (list + 4)[j];

			if (id >= n)
			{
				printk ("InvalidGraph\n");
				co_return;
			}

			if (work.visited[id] == work.epoch)
				continue;

			work.visited[id] = work.epoch;

			const int64_t distance = graph.distance (query, id);
			if (nearest.count < ef || distance < nearest.top ().distance)
			{
				const HeapItem item = { distance, id };

				if (!candidates.push (item))
				{
					printk ("CandidateHeapFull\n");
					co_return;
				}

				// ef+1 capacity was checked above
				nearest.push(item);

				if (nearest.count > ef)
					nearest.pop ();
			}
		}
	}

	// 3. keep k results, then pop worst-first into the output from the end
	while (nearest.count > k)
		nearest.pop ();

	count = nearest.count;

	for (size_t i = count; i != 0; --i)
	{
		const HeapItem item = nearest.pop ();
		result[i - 1] = { item.id, graph.label (item.id), item.distance };
	}

	for (size_t i = 0; i < count; ++i)
	{
		printk ("[%d] label=%llu\n", id, (unsigned long long) result[i].label);
	}
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

extern "C"
void
corohnsw_demo (void)
{
	GraphView graph;
	Workspace works[SMCORO] { };
	Task tasks[SMCORO];
	bool done;

	uint64_t start_cycle, end_cycle;

	if (!graph_prepare (graph))
	{
		return;
	}

	asm volatile ("csrr %0, mcycle" : "=r" (start_cycle) : : "memory");

	if (!workspace_prepare(graph, works))
	{
		workspace_release (works);
		return;
	}

	asm volatile ("csrr %0, mcycle" : "=r" (end_cycle) : : "memory");

	printk ("allocate memory. cycles=%llu\n", (unsigned long long) (end_cycle - start_cycle));

	asm volatile ("csrr %0, mcycle" : "=r" (start_cycle) : : "memory");

	for (size_t i = 0; i < SMCORO; i++)
	{
		tasks[i] = co_search (static_cast<int> (i), graph, hnsw::queries[i], K, EF, works[i]);
		//printk ("size of ... Task=%d, tasks[i]=%d, handle=%d\n", sizeof (Task), sizeof (tasks[i]), sizeof (tasks[i].h));
	}

	do
	{
		done = true;
		for (size_t i = 0; i < SMCORO; i++)
		{
			if (!tasks[i].done ())
			{
				tasks[i].resume ();

				done = false;
			}
		}
	}
	while (!done);

	asm volatile ("csrr %0, mcycle" : "=r" (end_cycle) : : "memory");

	workspace_release (works);

	printk ("co_search. cycles / 1000 = %llu\n", (unsigned long long) (end_cycle - start_cycle) / 1000);
}

