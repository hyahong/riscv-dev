#include <queue>
#include <sdm.h>
#include <base.h>
#include <printk.h>

using Index = std::priority_queue<uint64_t>;

constexpr uint32_t coroutine_count = 4;
// elements in total, split evenly among the queues
constexpr uint64_t node_count = 1 << 22;
// pops in total, each followed by a push, split evenly among the coroutines
constexpr uint64_t pop_count = 1 << 16;

struct Work
{
	Index *queue;
	uint64_t seed;
	uint64_t count;
	uint64_t found;
	uint64_t sum;
};

alignas(64) static uint64_t frames[SDM::MAX_COROUTINE_COUNT + 1][32];
alignas(16) static uint8_t stacks[coroutine_count][4096];

static Index *queues;
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
replace (Work &work)
{
	Index &queue = *work.queue;
	uint64_t seed = work.seed;
	uint64_t found = 0, sum = 0;

	for (uint64_t i = 0; i < work.count; ++i)
	{
		seed = seed * 6364136223846793005ull + 1442695040888963407ull;

		++found;
		sum += queue.top ();
		queue.pop ();
		queue.push (seed);
	}

	work.found = found;
	work.sum = sum;
}

static void
prepare (void)
{
	uint64_t start_cycle, end_cycle;

	// a run changes the queues, so every run builds them again
	delete[] queues;
	queues = new Index[coroutine_count];

	asm volatile ("csrr %0, mcycle" : "=r" (start_cycle) :: "memory");

	for (uint64_t i = 0; i < node_count; ++i)
	{
		queues[i % coroutine_count].push (scatter (i));
	}

	asm volatile ("csrr %0, mcycle" : "=r" (end_cycle) :: "memory");

	printk ("[STL PRIORITY_QUEUE] BUILD nodes=%lu build_ms=%llu\n",
			(unsigned long) node_count, (unsigned long long) ((end_cycle - start_cycle) / 40000));

	for (uint32_t cid = 0; cid < coroutine_count; ++cid)
	{
		works[cid] = { &queues[cid], cid + 1, pop_count / coroutine_count, 0, 0 };
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

	printk ("[STL PRIORITY_QUEUE] found=%lu sum=%lu%s\n", (unsigned long) found, (unsigned long) sum,
			found == count ? "" : " MISMATCH");
	hnsw::print_elapsed (start_cycle, end_cycle);
}

extern "C" void
priority_queue_demo (void)
{
	uint64_t start_cycle, end_cycle;

	prepare ();

	printk ("[STL PRIORITY_QUEUE] START pops=%lu\n", (unsigned long) (pop_count / coroutine_count * coroutine_count));

	asm volatile ("csrr %0, mcycle" : "=r" (start_cycle) :: "memory");

	for (uint32_t cid = 0; cid < coroutine_count; ++cid)
	{
		replace (works[cid]);
	}

	asm volatile ("csrr %0, mcycle" : "=r" (end_cycle) :: "memory");

	report (start_cycle, end_cycle);
}

extern "C" void
sdmpriority_queue_demo (void)
{
	uint64_t start_cycle, end_cycle;
	SDM &sdm = SDM::get ();
	SDM::Core *core = sdm.getCore (0);

	prepare ();

	// coroutine
	const auto worker = [] (unsigned int, void *arg) -> int {
		replace (*static_cast<Work *> (arg));

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

	printk ("[SDM STL PRIORITY_QUEUE] START contexts=%u pops=%lu\n", coroutine_count,
			(unsigned long) (pop_count / coroutine_count * coroutine_count));

	asm volatile ("csrr %0, mcycle" : "=r" (start_cycle) :: "memory");

	sdm.run (coroutine_count);

	asm volatile ("csrr %0, mcycle" : "=r" (end_cycle) :: "memory");

	report (start_cycle, end_cycle);
}
