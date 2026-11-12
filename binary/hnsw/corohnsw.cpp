#include <corohnsw.h>
#include <base.h>
#include <printk.h>

using namespace hnsw;

constexpr uint32_t coroutine_count = 8;
constexpr size_t K = 1;
constexpr size_t EF = 100;

static GraphView graph;
static Workspace works[coroutine_count] { };
static Result results[query_count] { };
static Status statuses[query_count];

static size_t next_query;

static Coro::Task
worker (unsigned int cid, void *)
{
	Workspace &work = works[cid];
	const size_t n = size_t (graph.h.cur_element_count);
	const size_t k = K < n ? K : n;
	const size_t ef = EF < n ? (EF > k ? EF : k) : n;

	while (true)
	{
		const size_t q = next_query++;
		if (q >= query_count)
		{
			break;
		}

		const int16_t *query = queries[q];

		next_epoch (work);

		// 1. greedy descent through the sparse upper levels
		uint32_t current = graph.h.enterpoint_node_;

		prefetch_vector (current);
		co_await Coro::yield ();
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
					statuses[q] = Status::InvalidGraph;
					co_return;
				}

				for (uint32_t j = 0; j < count; ++j)
				{
					const uint32_t id = reinterpret_cast<const uint32_t *> (list + 4)[j];
					if (id >= n)
					{
						statuses[q] = Status::InvalidGraph;
						co_return;
					}

					prefetch_vector (id);
					co_await Coro::yield ();
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
			{
				break;
			}

			const HeapItem candidate = candidates.pop ();
			const uint8_t *list;
			uint32_t count;

			if (!graph.link_list (candidate.id, 0, list, count))
			{
				statuses[q] = Status::InvalidGraph;
				co_return;
			}

			for (uint32_t j = 0; j < count; ++j)
			{
				const uint32_t id = reinterpret_cast<const uint32_t *> (list + 4)[j];
				if (id >= n)
				{
					statuses[q] = Status::InvalidGraph;
					co_return;
				}

				if (work.visited[id] == work.epoch)
				{
					continue;
				}
				work.visited[id] = work.epoch;

				prefetch_vector (id);
				co_await Coro::yield ();
				const int64_t distance = graph.distance (query, id);

				if (nearest.count < ef || distance < nearest.top ().distance)
				{
					const HeapItem item = { distance, id };

					if (!candidates.push (item))
					{
						statuses[q] = Status::CandidateHeapFull;
						co_return;
					}

					nearest.push (item);
					if (nearest.count > ef)
					{
						nearest.pop ();
					}
				}
			}
		}

		// 3. keep k results, the demo takes the nearest one
		while (nearest.count > k)
			nearest.pop ();

		const HeapItem item = nearest.pop ();
		results[q] = { item.id, graph.label (item.id), item.distance };
		statuses[q] = Status::Ok;
	}
}

extern "C"
void
corohnsw_demo (void)
{
	uint64_t start_cycle, end_cycle;
	Coro coro;

	if (!graph_prepare (graph))
	{
		return;
	}

	if (!workspace_prepare (graph, works, coroutine_count, K, EF))
	{
		workspace_release (works, coroutine_count);
		return;
	}

	next_query = 0;
	for (size_t q = 0; q < query_count; ++q)
	{
		statuses[q] = Status::InvalidArgument;
	}

	for (uint32_t cid = 0; cid < coroutine_count; ++cid)
	{
		if (!coro.addCoroutine (worker, nullptr))
		{
			printk ("failed to add the coroutine\n");
			workspace_release (works, coroutine_count);
			return;
		}
	}

	printk ("[CORO HNSW] START contexts=%u queries=%lu\n", coroutine_count, (unsigned long) query_count);

	asm volatile ("csrr %0, mcycle" : "=r" (start_cycle) :: "memory");

	coro.run ();

	asm volatile ("csrr %0, mcycle" : "=r" (end_cycle) :: "memory");

	for (size_t q = 0; q < query_count; ++q)
	{
		if (statuses[q] == Status::Ok)
		{
			printk ("%lu,%llu\n", (unsigned long) q, (unsigned long long) results[q].label);
		}
		else
		{
			printk ("search_knn query=%lu failed: %s\n", (unsigned long) q, status_name (statuses[q]));
		}
	}
	print_elapsed (start_cycle, end_cycle);

	workspace_release (works, coroutine_count);
}
