#ifndef _HNSW_H_
#define _HNSW_H_

#include <printk.h>
#include <stdint.h>
#include <stddef.h>

namespace hnsw
{

// header

struct DumpHeader
{
	uint64_t cur_element_count;
	uint32_t enterpoint_node_;
	int32_t maxlevel_;
	uint64_t M_, maxM_, maxM0_;
	uint64_t dim_, data_size_;
	uint64_t size_data_per_element_;
	uint64_t size_links_per_element_;
	uint64_t size_links_level0_;
	uint64_t offsetLevel0_, offsetData_, label_offset_;
	uint64_t data_level0_memory_offset_;
	uint64_t linkLists_offset_;
	uint64_t linkLists_data_offset_;
	uint64_t total_bytes;
};
static_assert(sizeof (DumpHeader) == 136, "DumpHeader ABI mismatch");
static_assert(sizeof (size_t) == 8, "This code targets a 64-bit host / RV64");

// binary heap

struct HeapItem
{
	int64_t distance;
	uint32_t id; // node id, not external label
};

struct Heap
{
	HeapItem* items;
	size_t count;
	size_t capacity;
	bool nearest_first;

	bool
	before (const HeapItem &a, const HeapItem &b) const
	{
		return nearest_first ? a.distance < b.distance : a.distance > b.distance;
	}

	const
	HeapItem &top () const
	{
		return items[0];
	}

	bool
	push (HeapItem item)
	{
		if (count == capacity)
			return false;

		size_t hole = count++;
		while (hole != 0)
		{
			const size_t parent = (hole - 1) / 2;
			if (!before (item, items[parent]))
				break;
			items[hole] = items[parent];
			hole = parent;
		}
		items[hole] = item;
		return true;
	}

	HeapItem
	pop ()
	{
		const HeapItem root = items[0];
		const HeapItem last = items[--count];

		if (count == 0)
			return root;

		size_t hole = 0;
		while (hole < count / 2)
		{
			size_t child = hole * 2 + 1;
			if (child + 1 < count && !before (items[child], items[child + 1]))
					++child;
			items[hole] = items[child];
			hole = child;
		}

		while (hole != 0)
		{
			const size_t parent = (hole - 1) / 2;
			if (!before (last, items[parent]))
				break;
			items[hole] = items[parent];
			hole = parent;
		}
		items[hole] = last;
		return root;
	}
};

struct Workspace
{
	HeapItem *candidates;
	size_t candidate_capacity;
	HeapItem *nearest;
	size_t nearest_capacity;
	uint32_t *visited;
	size_t visited_capacity;
	uint32_t epoch;
};

enum class Status
{
	Ok,
	InvalidArgument,
	InvalidGraph,
	WorkspaceTooSmall,
	CandidateHeapFull
};

inline const char *
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

inline bool
fits (uint64_t offset, uint64_t bytes, uint64_t limit)
{
	return offset <= limit && bytes <= limit - offset;
}

// HNSW graph viewer
struct GraphView
{
	const uint8_t *base;
	DumpHeader h;

	const uint8_t *
	record (uint32_t id) const
	{
		return base + h.data_level0_memory_offset_ + uint64_t (id) * h.size_data_per_element_;
	}

	uint64_t
	label (uint32_t id) const 
	{
		// labels are 4-byte aligned, but not always 8-byte aligned
		const uint32_t *words = reinterpret_cast<const uint32_t *> (record (id) + h.label_offset_);
		return uint64_t (words[0]) | (uint64_t (words[1]) << 32);
	}

	int64_t 
	distance (const int16_t *query, uint32_t id) const
	{
		const int16_t *vector = reinterpret_cast<const int16_t *> (record (id) + h.offsetData_);
		int64_t dot = 0;
		for (uint64_t j = 0; j < h.dim_; ++j)
				dot += int64_t (query[j]) * vector[j];
		return -dot;
	}

	bool
	link_list (uint32_t id, uint32_t level, const uint8_t *&list, uint32_t &count) const
	{
		if (id >= h.cur_element_count || level > uint32_t (h.maxlevel_))
				return false;

		const uint64_t bytes = level == 0 ? h.size_links_level0_ : h.size_links_per_element_;
		uint64_t offset;

		if (level == 0)
		{
			offset = h.data_level0_memory_offset_ +
							 uint64_t (id) * h.size_data_per_element_ + h.offsetLevel0_;
		}
		else
		{
			offset = reinterpret_cast<const uint64_t *> (base + h.linkLists_offset_)[id];
			if (offset < h.linkLists_data_offset_ || offset >= h.total_bytes || offset % 4 != 0)
					return false;

			if (uint64_t (level - 1) >= (h.total_bytes - offset) / bytes)
					return false;

			offset += uint64_t (level - 1) * bytes;
		}

		if (!fits (offset, bytes, h.total_bytes))
			return false;

		list = base + offset;
		count = *reinterpret_cast<const uint16_t *> (list);

		return count <= (level == 0 ? h.maxM0_ : h.maxM_);
	}
};

inline Status
open_graph (const void *memory, uint64_t available_bytes, GraphView &graph)
{
	graph.base = nullptr;

	if (!memory || available_bytes < sizeof(DumpHeader) ||
			reinterpret_cast<uintptr_t> (memory) % alignof(DumpHeader) != 0)
			return Status::InvalidArgument;

	const uint16_t endian = 1;
	if (*reinterpret_cast<const uint8_t *> (&endian) != 1)
			return Status::InvalidArgument;

	const uint8_t *base = static_cast<const uint8_t *> (memory);
	graph.h = *static_cast<const DumpHeader *> (memory);
	const DumpHeader &h = graph.h;

	if (h.cur_element_count == 0 || h.cur_element_count > UINT32_MAX ||
			h.enterpoint_node_ >= h.cur_element_count || h.maxlevel_ < 0 ||
			h.dim_ == 0 || h.dim_ > UINT32_MAX || h.data_size_ != h.dim_ * 2 ||
			h.total_bytes > available_bytes || h.total_bytes < sizeof (DumpHeader))
			return Status::InvalidGraph;

	// direct loads below require aligned records and fields.
	if (h.data_level0_memory_offset_ % 4 != 0 || h.size_data_per_element_ % 4 != 0 ||
			h.offsetLevel0_ % 4 != 0 || h.offsetData_ % 2 != 0 || h.label_offset_ % 4 != 0)
		return Status::InvalidGraph;

	// dim <= UINT32_MAX also keeps every possible INT16 dot within INT64.
	if (h.maxM_ > 65535 || h.maxM0_ > 65535 ||
			h.size_links_per_element_ != 4 + h.maxM_ * 4 ||
			h.size_links_level0_ != 4 + h.maxM0_ * 4 ||
			!fits(h.offsetLevel0_, h.size_links_level0_, h.offsetData_) ||
			!fits(h.offsetData_, h.data_size_, h.label_offset_) ||
			!fits(h.label_offset_, 8, h.size_data_per_element_))
			return Status::InvalidGraph;

	if (h.data_level0_memory_offset_ < sizeof (DumpHeader) ||
			h.size_data_per_element_ == 0 ||
			h.data_level0_memory_offset_ > h.total_bytes ||
			h.cur_element_count > (h.total_bytes - h.data_level0_memory_offset_) /
														h.size_data_per_element_)
			return Status::InvalidGraph;

	const uint64_t level0_end = h.data_level0_memory_offset_ +
														 h.cur_element_count * h.size_data_per_element_;
	if (h.linkLists_offset_ < level0_end || h.linkLists_offset_ % 8 != 0 ||
			!fits(h.linkLists_offset_, h.cur_element_count * 8, h.total_bytes) ||
			h.linkLists_data_offset_ != h.linkLists_offset_ + h.cur_element_count * 8 ||
			h.linkLists_data_offset_ > h.total_bytes)
			return Status::InvalidGraph;

	graph.base = base;
	return Status::Ok;
}

inline void
initialize_workspace (Workspace &work)
{
	for (size_t i = 0; i < work.visited_capacity; ++i)
		work.visited[i] = 0;
	work.epoch = 0;
}

inline void
next_epoch (Workspace &work)
{
	if (++work.epoch == 0)
	{
		initialize_workspace (work);
		work.epoch = 1;
	}
}

struct Result
{
	uint32_t id; // node id
	uint64_t label; // external id
	int64_t distance; // smaller is better
};

inline Status
search_knn (const int id, const GraphView &graph, const int16_t *query, size_t k, size_t ef,
		Workspace &work, Result *output = nullptr)
{
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
					return Status::InvalidGraph;

			for (uint32_t j = 0; j < count; ++j)
			{
				const uint32_t id = reinterpret_cast<const uint32_t *> (list + 4)[j];
				if (id >= n)
					return Status::InvalidGraph;
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
				return Status::InvalidGraph;

		for (uint32_t j = 0; j < count; ++j)
		{
			const uint32_t id = reinterpret_cast<const uint32_t *> (list + 4)[j];

			if (id >= n)
				return Status::InvalidGraph;

			if (work.visited[id] == work.epoch)
				continue;

			work.visited[id] = work.epoch;

			const int64_t distance = graph.distance (query, id);
			if (nearest.count < ef || distance < nearest.top ().distance)
			{
				const HeapItem item = { distance, id };

				if (!candidates.push (item))
					return Status::CandidateHeapFull;

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

	// 4. assert k == 1 in this demo
	const HeapItem item = nearest.pop ();
	const uint64_t label = graph.label (item.id);
	if (output)
		*output = { item.id, label, item.distance };
	else
		printk ("%d,%llu", id, (unsigned long long) label);

	return Status::Ok;
}

// external

extern const size_t dimension;
inline constexpr size_t query_count = 160;
extern const int16_t queries[query_count][512];

}

#endif
