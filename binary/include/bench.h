#ifndef _BENCH_H_
#define _BENCH_H_

#include <stdint.h>

constexpr uint32_t coroutine_count = 4;
// elements in a container, a power of two
constexpr uint64_t node_count = 1 << 20;
// operations in total, split evenly among the coroutines
constexpr uint64_t operation_count = 1 << 16;
// elements read by one range scan
constexpr uint64_t scan_length = 1024;

struct Work
{
	uint32_t cid;
	uint64_t seed;
	uint64_t count;
	uint64_t found;
	uint64_t sum;
};

struct Bench
{
	const char *container;
	const char *operation;
	uint64_t (*prepare) (Work *works);
	void (*run) (Work &work);
};

static inline uint64_t
scatter (uint64_t x)
{
	x ^= x >> 33;
	x *= 0xff51afd7ed558ccdull;
	x ^= x >> 33;
	x *= 0xc4ceb9fe1a85ec53ull;
	x ^= x >> 33;

	return x;
}

#endif
