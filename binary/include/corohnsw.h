#ifndef _COROHNSW_H_
#define _COROHNSW_H_

#include <coroutine>
#include <new>
#include <hnsw.h>
#include <printk.h>

namespace hnsw
{

constexpr size_t chunk_elements = 128; // zero means the whole vector

// The graph, query, workspace, output and result count must outlive the task.
// Each simultaneously active task needs its own workspace and output storage.
class SearchTask
{
public:
	struct promise_type;
	using handle_type = std::coroutine_handle<promise_type>;

	struct promise_type
	{
		Status result = Status::InvalidArgument;

		SearchTask get_return_object () noexcept
		{
			return SearchTask { handle_type::from_promise (*this) };
		}
		std::suspend_always initial_suspend () noexcept { return { }; }
		std::suspend_always final_suspend () noexcept { return { }; }
		void return_value (Status status) noexcept { result = status; }
		void unhandled_exception () noexcept { __builtin_trap (); }
	};

	SearchTask () noexcept = default;
	explicit SearchTask (handle_type handle) noexcept : handle_ (handle) { }
	SearchTask (const SearchTask &) = delete;
	SearchTask &operator= (const SearchTask &) = delete;
	SearchTask (SearchTask &&other) noexcept : handle_ (other.handle_)
	{
		other.handle_ = nullptr;
	}
	SearchTask &operator= (SearchTask &&other) noexcept
	{
		if (this != &other)
		{
			if (handle_)
				handle_.destroy ();
			handle_ = other.handle_;
			other.handle_ = nullptr;
		}
		return *this;
	}
	~SearchTask ()
	{
		if (handle_)
			handle_.destroy ();
	}
	bool done () const noexcept { return !handle_ || handle_.done (); }
	void resume () const noexcept
	{
		if (!done ())
			handle_.resume ();
	}
	Status status () const noexcept
	{
		return handle_ ? handle_.promise ().result : Status::InvalidArgument;
	}

private:
	handle_type handle_ = nullptr;
};

namespace coro_detail
{

inline void
prefetch_range (const void *memory, size_t bytes)
{
	constexpr uintptr_t line_mask = 63;
	const uintptr_t address = reinterpret_cast<uintptr_t> (memory);
	const uintptr_t last = (address + bytes - 1) & ~line_mask;
	for (uintptr_t line = address & ~line_mask; ; line += line_mask + 1)
	{
		asm volatile ("prefetch.r 0(%0)" :: "r" (line));
		if (line == last)
			break;
	}
}

inline int64_t
dot_chunk (const int16_t *query, const int16_t *vector, size_t count)
{
	int64_t sum = 0;
	size_t j = 0;

	int64_t sum1 = 0, sum2 = 0, sum3 = 0;
	for (; count - j >= 4; j += 4)
	{
		sum += int64_t (query[j]) * vector[j];
		sum1 += int64_t (query[j + 1]) * vector[j + 1];
		sum2 += int64_t (query[j + 2]) * vector[j + 2];
		sum3 += int64_t (query[j + 3]) * vector[j + 3];
	}
	sum += sum1 + sum2 + sum3;

	// partial
	//for (; j < count; ++j)
	//	sum += int64_t (query[j]) * vector[j];

	return sum;
}

// The same bounds checks as GraphView::link_list, split before its first
// list load so a task can issue the request and run other queries meanwhile.
inline bool
link_list_address (const GraphView &graph, uint32_t id, uint32_t level,
		const uint8_t *&list, uint64_t &bytes)
{
	const DumpHeader &h = graph.h;
	if (id >= h.cur_element_count || level > uint32_t (h.maxlevel_))
		return false;
	bytes = level == 0 ? h.size_links_level0_ : h.size_links_per_element_;
	uint64_t offset;
	if (level == 0)
		offset = h.data_level0_memory_offset_ + uint64_t (id) * h.size_data_per_element_ + h.offsetLevel0_;
	else
	{
		offset = reinterpret_cast<const uint64_t *> (graph.base + h.linkLists_offset_)[id];
		if (offset < h.linkLists_data_offset_ || offset >= h.total_bytes || offset % 4 != 0)
			return false;
		if (uint64_t (level - 1) >= (h.total_bytes - offset) / bytes)
			return false;
		offset += uint64_t (level - 1) * bytes;
	}
	if (!fits (offset, bytes, h.total_bytes))
		return false;
	list = graph.base + offset;
	return true;
}

} // namespace coro_detail

inline SearchTask
co_search_knn (const int id, const GraphView &graph, const int16_t *query, size_t k, size_t ef, Workspace &work)
{
	// 1. validate input and start a fresh visited epoch for this query
	if (k == 0)
		co_return Status::Ok;
	if (!graph.base || !query)
		co_return Status::InvalidArgument;

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
		co_return Status::WorkspaceTooSmall;

	next_epoch (work);
	const size_t dimension = size_t (graph.h.dim_);

	// prefetch query
	coro_detail::prefetch_range (query, dimension * sizeof (*query));

#define HNSW_CO_DISTANCE(node, destination) \
	do { \
		const int16_t *vector = reinterpret_cast<const int16_t *> (graph.record (node) + graph.h.offsetData_); \
		int64_t dot = 0; \
		for (size_t first = 0; first < dimension; ) \
		{ \
			const size_t remaining = dimension - first; \
			const size_t length = chunk_elements < remaining ? chunk_elements : remaining; \
			coro_detail::prefetch_range (vector + first, length * sizeof (*vector)); \
			co_await std::suspend_always { }; \
			dot += coro_detail::dot_chunk (query + first, vector + first, length); \
			first += length; \
		} \
		destination = -dot; \
	} while (0)

	// 2. greedy descent
	uint32_t current = graph.h.enterpoint_node_;
	int64_t current_distance;
	HNSW_CO_DISTANCE (current, current_distance);
	for (int32_t level = graph.h.maxlevel_; level > 0; --level)
	{
		bool moved = true;
		while (moved)
		{
			const uint8_t *list;
			uint32_t count;
			uint64_t list_bytes;

			moved = false;

			// prefetch metadata
			if (current >= n)
				co_return Status::InvalidGraph;

			coro_detail::prefetch_range (graph.base + graph.h.linkLists_offset_ + uint64_t (current) * sizeof (uint64_t), sizeof (uint64_t));
			co_await std::suspend_always { };

			if (!coro_detail::link_list_address (graph, current, uint32_t (level), list, list_bytes))
				co_return Status::InvalidGraph;

			coro_detail::prefetch_range (list, size_t (list_bytes));
			co_await std::suspend_always { };

			count = *reinterpret_cast<const uint16_t *> (list);
			if (count > graph.h.maxM_)
				co_return Status::InvalidGraph;

			for (uint32_t j = 0; j < count; ++j)
			{
				const uint32_t id = reinterpret_cast<const uint32_t *> (list + 4)[j];
				int64_t distance;

				if (id >= n)
					co_return Status::InvalidGraph;

				HNSW_CO_DISTANCE (id, distance);
				if (distance < current_distance)
				{
					current = id;
					current_distance = distance;
					moved = true;
				}
			}
		}
	}

	// 3. L0 best-first search
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

		// prefetch metadata
		uint64_t list_bytes;

		if (!coro_detail::link_list_address (graph, candidate.id, 0, list, list_bytes))
			co_return Status::InvalidGraph;

		coro_detail::prefetch_range (list, size_t (list_bytes));
		co_await std::suspend_always { };

		count = *reinterpret_cast<const uint16_t *> (list);
		if (count > graph.h.maxM0_)
			co_return Status::InvalidGraph;

		for (uint32_t j = 0; j < count; ++j)
		{
			const uint32_t id = reinterpret_cast<const uint32_t *> (list + 4)[j];

			coro_detail::prefetch_range (work.visited + id, sizeof (*work.visited));
		}
		if (count != 0)
			co_await std::suspend_always { };

		for (uint32_t j = 0; j < count; ++j)
		{
			const uint32_t id = reinterpret_cast<const uint32_t *> (list + 4)[j];
			if (id >= n)
				co_return Status::InvalidGraph;
			if (work.visited[id] == work.epoch)
				continue;
			work.visited[id] = work.epoch;

			int64_t distance;
			HNSW_CO_DISTANCE (id, distance);
			if (nearest.count < ef || distance < nearest.top ().distance)
			{
				const HeapItem item = { distance, id };

				if (!candidates.push (item))
					co_return Status::CandidateHeapFull;
				nearest.push (item);
				if (nearest.count > ef)
					nearest.pop ();
			}
		}
	}

	// 4. keep the best k results
	while (nearest.count > k)
		nearest.pop ();

	// 5. assert k == 1 in this demo
	printk ("%d,label=%llu\n", id, (unsigned long long) graph.label (nearest.pop ().id));

	co_return Status::Ok;
#undef HNSW_CO_DISTANCE
}

} // namespace hnsw

#endif
