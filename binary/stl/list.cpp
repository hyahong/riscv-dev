#include <list>
#include <sdm.h>
#include <base.h>
#include <printk.h>

using Index = std::list<uint64_t>;

constexpr uint32_t coroutine_count = 4;
// nodes in total, split evenly among the lists
constexpr uint64_t node_count = 1 << 20;

struct Work
{
	const Index *list;
	uint64_t count;
	uint64_t found;
	uint64_t sum;
};

alignas(64) static uint64_t frames[SDM::MAX_COROUTINE_COUNT + 1][32];
alignas(16) static uint8_t stacks[coroutine_count][4096];

static Index *lists;
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
traverse (Work &work)
{
	uint64_t found = 0, sum = 0;

	for (const uint64_t value : *work.list)
	{
		++found;
		sum += value;
	}

	work.found = found;
	work.sum = sum;
}

static void
prepare (void)
{
	uint64_t start_cycle, end_cycle;

	if (!lists)
	{
		lists = new Index[coroutine_count];

		asm volatile ("csrr %0, mcycle" : "=r" (start_cycle) :: "memory");

		for (uint64_t i = 0; i < node_count; ++i)
		{
			lists[i / 2 % coroutine_count].push_back (scatter (i));
		}

		// sorted by value, a list no longer follows the order in memory
		for (uint32_t cid = 0; cid < coroutine_count; ++cid)
		{
			lists[cid].sort ();
		}

		asm volatile ("csrr %0, mcycle" : "=r" (end_cycle) :: "memory");

		printk ("[STL LIST] BUILD nodes=%lu build_ms=%llu\n",
				(unsigned long) node_count, (unsigned long long) ((end_cycle - start_cycle) / 40000));
	}

	for (uint32_t cid = 0; cid < coroutine_count; ++cid)
	{
		works[cid] = { &lists[cid], lists[cid].size (), 0, 0 };
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

	printk ("[STL LIST] found=%lu sum=%lu%s\n", (unsigned long) found, (unsigned long) sum,
			found == count ? "" : " MISMATCH");
	hnsw::print_elapsed (start_cycle, end_cycle);
}

extern "C" void
list_demo (void)
{
	uint64_t start_cycle, end_cycle;

	prepare ();

	printk ("[STL LIST] START nodes=%lu\n", (unsigned long) node_count);

	asm volatile ("csrr %0, mcycle" : "=r" (start_cycle) :: "memory");

	for (uint32_t cid = 0; cid < coroutine_count; ++cid)
	{
		traverse (works[cid]);
	}

	asm volatile ("csrr %0, mcycle" : "=r" (end_cycle) :: "memory");

	report (start_cycle, end_cycle);
}

extern "C" void
sdmlist_demo (void)
{
	uint64_t start_cycle, end_cycle;
	SDM &sdm = SDM::get ();
	SDM::Core *core = sdm.getCore (0);

	prepare ();

	// coroutine
	const auto worker = [] (unsigned int, void *arg) -> int {
		traverse (*static_cast<Work *> (arg));

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

	printk ("[SDM STL LIST] START contexts=%u nodes=%lu\n", coroutine_count, (unsigned long) node_count);

	asm volatile ("csrr %0, mcycle" : "=r" (start_cycle) :: "memory");

	sdm.run (coroutine_count);

	asm volatile ("csrr %0, mcycle" : "=r" (end_cycle) :: "memory");

	report (start_cycle, end_cycle);
}
