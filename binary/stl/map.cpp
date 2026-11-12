#include <map>
#include <sdm.h>
#include <base.h>
#include <printk.h>

using Index = std::map<uint64_t, uint64_t>;

constexpr uint32_t coroutine_count = 4;
// keys in the map, a power of two
constexpr uint64_t node_count = 1 << 20;
// lookups in total, split evenly among the coroutines
constexpr uint64_t lookup_count = 1 << 16;
// elements read by one range scan
constexpr uint64_t scan_length = 1024;

struct Work
{
	uint64_t seed;
	uint64_t count;
	uint64_t found;
	uint64_t sum;
};

alignas(64) static uint64_t frames[SDM::MAX_COROUTINE_COUNT + 1][32];
alignas(16) static uint8_t stacks[coroutine_count][4096];

static Index *tree;
static Work works[coroutine_count];

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

static inline void
lookup (Work &work)
{
	const Index &index = *tree;
	uint64_t seed = work.seed;
	uint64_t found = 0, sum = 0;

	for (uint64_t i = 0; i < work.count; ++i)
	{
		seed = seed * 6364136223846793005ull + 1442695040888963407ull;

		const auto it = index.find (scatter ((seed >> 33) & (node_count - 1)));
		if (it != index.end ())
		{
			++found;
			sum += it->second;
		}
	}

	work.found = found;
	work.sum = sum;
}

static void
prepare (void)
{
	uint64_t start_cycle, end_cycle;

	if (!tree)
	{
		tree = new Index;

		asm volatile ("csrr %0, mcycle" : "=r" (start_cycle) :: "memory");

		for (uint64_t i = 0; i < node_count; ++i)
		{
			tree->emplace (scatter (i), i);
		}

		asm volatile ("csrr %0, mcycle" : "=r" (end_cycle) :: "memory");

		printk ("[STL MAP] BUILD nodes=%lu build_ms=%llu\n",
				(unsigned long) tree->size (), (unsigned long long) ((end_cycle - start_cycle) / 40000));
	}

	for (uint32_t cid = 0; cid < coroutine_count; ++cid)
	{
		works[cid] = { cid + 1, lookup_count / coroutine_count, 0, 0 };
	}
}

static void
report (uint64_t start_cycle, uint64_t end_cycle)
{
	uint64_t count = 0, found = 0, sum = 0;

	for (uint32_t cid = 0; cid < coroutine_count; ++cid)
	{
		count += works[cid].count;
		found += works[cid].found;
		sum += works[cid].sum;
	}

	printk ("[STL MAP] found=%lu sum=%lu%s\n", (unsigned long) found, (unsigned long) sum,
			found == count ? "" : " MISMATCH");
	hnsw::print_elapsed (start_cycle, end_cycle);
}

extern "C" void
map_demo (void)
{
	uint64_t start_cycle, end_cycle;

	prepare ();

	printk ("[STL MAP] START lookups=%lu\n", (unsigned long) (lookup_count / coroutine_count * coroutine_count));

	asm volatile ("csrr %0, mcycle" : "=r" (start_cycle) :: "memory");

	for (uint32_t cid = 0; cid < coroutine_count; ++cid)
	{
		lookup (works[cid]);
	}

	asm volatile ("csrr %0, mcycle" : "=r" (end_cycle) :: "memory");

	report (start_cycle, end_cycle);
}

extern "C" void
sdmmap_demo (void)
{
	uint64_t start_cycle, end_cycle;
	SDM &sdm = SDM::get ();
	SDM::Core *core = sdm.getCore (0);

	prepare ();

	// coroutine
	const auto worker = [] (unsigned int, void *arg) -> int {
		lookup (*static_cast<Work *> (arg));

		return 0;
	};

	// SDM
	core->clear ();
	core->setFrame (frames);

	for (uint32_t cid = 0; cid < coroutine_count; ++cid)
	{
		if (!core->addCoroutine (stacks[cid] + sizeof (stacks[cid]), worker, &works[cid]))
		{
			printk ("failed to add the coroutine to sdm core\n");
			return;
		}
	}

	printk ("[SDM STL MAP] START contexts=%u lookups=%lu\n", coroutine_count,
			(unsigned long) (lookup_count / coroutine_count * coroutine_count));

	asm volatile ("csrr %0, mcycle" : "=r" (start_cycle) :: "memory");

	sdm.run (coroutine_count);

	asm volatile ("csrr %0, mcycle" : "=r" (end_cycle) :: "memory");

	report (start_cycle, end_cycle);
}

static inline void
scan (Work &work)
{
	const Index &index = *tree;
	uint64_t seed = work.seed;
	uint64_t found = 0, sum = 0;

	while (found < work.count)
	{
		seed = seed * 6364136223846793005ull + 1442695040888963407ull;

		auto it = index.lower_bound (seed);
		for (uint64_t i = 0; i < scan_length && found < work.count; ++i)
		{
			if (it == index.end ())
			{
				it = index.begin ();
			}

			++found;
			sum += it->second;
			++it;
		}
	}

	work.found = found;
	work.sum = sum;
}

extern "C" void
mapscan_demo (void)
{
	uint64_t start_cycle, end_cycle;

	prepare ();

	printk ("[STL MAP] START scan elements=%lu length=%lu\n",
			(unsigned long) (lookup_count / coroutine_count * coroutine_count), (unsigned long) scan_length);

	asm volatile ("csrr %0, mcycle" : "=r" (start_cycle) :: "memory");

	for (uint32_t cid = 0; cid < coroutine_count; ++cid)
	{
		scan (works[cid]);
	}

	asm volatile ("csrr %0, mcycle" : "=r" (end_cycle) :: "memory");

	report (start_cycle, end_cycle);
}

extern "C" void
sdmmapscan_demo (void)
{
	uint64_t start_cycle, end_cycle;
	SDM &sdm = SDM::get ();
	SDM::Core *core = sdm.getCore (0);

	prepare ();

	// coroutine
	const auto worker = [] (unsigned int, void *arg) -> int {
		scan (*static_cast<Work *> (arg));

		return 0;
	};

	// SDM
	core->clear ();
	core->setFrame (frames);

	for (uint32_t cid = 0; cid < coroutine_count; ++cid)
	{
		if (!core->addCoroutine (stacks[cid] + sizeof (stacks[cid]), worker, &works[cid]))
		{
			printk ("failed to add the coroutine to sdm core\n");
			return;
		}
	}

	printk ("[SDM STL MAP] START contexts=%u scan elements=%lu length=%lu\n", coroutine_count,
			(unsigned long) (lookup_count / coroutine_count * coroutine_count), (unsigned long) scan_length);

	asm volatile ("csrr %0, mcycle" : "=r" (start_cycle) :: "memory");

	sdm.run (coroutine_count);

	asm volatile ("csrr %0, mcycle" : "=r" (end_cycle) :: "memory");

	report (start_cycle, end_cycle);
}
