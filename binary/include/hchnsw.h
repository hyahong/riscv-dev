#ifndef _HCHNSW_H_
#define _HCHNSW_H_

#ifndef HCHNSW_QUERY_REHINT
#define HCHNSW_QUERY_REHINT 1
#endif

// Port of the scalar/prefetch pipeline executed on the integrated5 FPGA demo.
#include <hnsw.h>
#include "hc_prefwait.h"
#include "hc_prefetch_arm.h"
namespace hnsw
{

inline constexpr size_t hw_worker_chunk_elements = 128;
namespace hw_worker_detail
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

__attribute__((always_inline)) inline int64_t
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

	return sum;
}

// Keep the same WAIT -> next ARM -> scalar dot pipeline at every call site.
__attribute__((always_inline)) inline int64_t
distance (const GraphView &graph, const int16_t *query, uint32_t id)
{
	const size_t dimension = size_t (graph.h.dim_);
	const int16_t *vector = reinterpret_cast<const int16_t *> (graph.record (id) + graph.h.offsetData_);
	int64_t dot = 0;
	if (dimension)
	{
		const size_t initial = hw_worker_chunk_elements < dimension ? hw_worker_chunk_elements : dimension;
		hc_prefetch_arm_range (vector, initial * sizeof (*vector));
	}
	for (size_t first = 0; first < dimension; )
	{
		const size_t remaining = dimension - first;
		const size_t length = hw_worker_chunk_elements < remaining ? hw_worker_chunk_elements : remaining;
		const size_t next = first + length;
		hc_wait ();
		if (next < dimension)
		{
			const size_t left = dimension - next;
			const size_t lead = hw_worker_chunk_elements < left ? hw_worker_chunk_elements : left;
			const uintptr_t address = reinterpret_cast<uintptr_t> (vector + next);
			const uintptr_t arm_address = lead == hw_worker_chunk_elements
					? (address + 63) & ~uintptr_t (63) : address;
			hc_prefetch_arm_range (reinterpret_cast<const void *> (arm_address), lead * sizeof (*vector));
#if HCHNSW_QUERY_REHINT
			prefetch_range (query + next, lead * sizeof (*query));
#endif
		}
		dot += dot_chunk (query + first, vector + first, length);
		for (size_t tail = length & ~size_t (3); tail < length; ++tail)
			dot += int64_t (query[first + tail]) * vector[first + tail];
		first = next;
	}
	return -dot;
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

} // namespace hw_worker_detail

__attribute__((noinline)) inline Status
hc_search_knn (const int id, const GraphView &graph, const int16_t *query, size_t k, size_t ef,
		Workspace &work, Result *output = nullptr)
{
	// 1. validate input and start a fresh visited epoch for this query
	if (k == 0)
		return Status::Ok;
	if (!graph.base || !query)
		return Status::InvalidArgument;

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
		return Status::WorkspaceTooSmall;

	next_epoch (work);
	const size_t dimension = size_t (graph.h.dim_);

	// prefetch query
	hw_worker_detail::prefetch_range (query, dimension * sizeof (*query));

	// 2. greedy descent
	uint32_t current = graph.h.enterpoint_node_;
	int64_t current_distance = hw_worker_detail::distance (graph, query, current);
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
				return Status::InvalidGraph;

			hc_prefwait (graph.base + graph.h.linkLists_offset_ + uint64_t (current) * sizeof (uint64_t), sizeof (uint64_t));

			if (!hw_worker_detail::link_list_address (graph, current, uint32_t (level), list, list_bytes))
				return Status::InvalidGraph;

			hc_prefwait (list, size_t (list_bytes));

			count = *reinterpret_cast<const uint16_t *> (list);
			if (count > graph.h.maxM_)
				return Status::InvalidGraph;

			for (uint32_t j = 0; j < count; ++j)
			{
				const uint32_t id = reinterpret_cast<const uint32_t *> (list + 4)[j];

				if (id >= n)
					return Status::InvalidGraph;

				const int64_t distance = hw_worker_detail::distance (graph, query, id);
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

		if (!hw_worker_detail::link_list_address (graph, candidate.id, 0, list, list_bytes))
			return Status::InvalidGraph;

		hc_prefwait (list, size_t (list_bytes));

		count = *reinterpret_cast<const uint16_t *> (list);
		if (count > graph.h.maxM0_)
			return Status::InvalidGraph;

		for (uint32_t j = 0; j < count; ++j)
		{
			const uint32_t id = reinterpret_cast<const uint32_t *> (list + 4)[j];

			hw_worker_detail::prefetch_range (work.visited + id, sizeof (*work.visited));
		}
		if (count != 0)
			hc_yield ();

		for (uint32_t j = 0; j < count; ++j)
		{
			const uint32_t id = reinterpret_cast<const uint32_t *> (list + 4)[j];
			if (id >= n)
				return Status::InvalidGraph;
			if (work.visited[id] == work.epoch)
				continue;
			work.visited[id] = work.epoch;

			const int64_t distance = hw_worker_detail::distance (graph, query, id);
			if (nearest.count < ef || distance < nearest.top ().distance)
			{
				const HeapItem item = { distance, id };

				if (!candidates.push (item))
					return Status::CandidateHeapFull;
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
	const HeapItem item = nearest.pop ();
	const uint64_t label = graph.label (item.id);
	if (output)
		*output = { item.id, label, item.distance };
	else
		printk ("%d,%llu", id, (unsigned long long) label);

	return Status::Ok;
}

} // namespace hnsw

#endif
