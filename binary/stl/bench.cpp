#include <sdm.h>
#include <base.h>
#include <printk.h>
#include <bench.h>

alignas(64) static uint64_t frames[SDM::MAX_COROUTINE_COUNT + 1][32];
alignas(16) static uint8_t stacks[coroutine_count][4096];

static Work works[coroutine_count];
static const Bench *current;

static uint64_t
prepare (const Bench *bench)
{
	uint64_t start_cycle, end_cycle;
	uint64_t count = 0;

	for (uint32_t cid = 0; cid < coroutine_count; ++cid)
	{
		works[cid] = { cid, cid + 1, operation_count / coroutine_count, 0, 0 };
	}

	asm volatile ("csrr %0, mcycle" : "=r" (start_cycle) :: "memory");

	const uint64_t built = bench->prepare (works);

	asm volatile ("csrr %0, mcycle" : "=r" (end_cycle) :: "memory");

	if (built)
	{
		printk ("[STL %s] BUILD nodes=%lu build_ms=%llu\n", bench->container,
				(unsigned long) built, (unsigned long long) ((end_cycle - start_cycle) / 40000));
	}

	for (uint32_t cid = 0; cid < coroutine_count; ++cid)
	{
		count += works[cid].count;
	}

	return count;
}

static void
report (const Bench *bench, uint64_t start_cycle, uint64_t end_cycle)
{
	uint64_t count = 0, found = 0, sum = 0;

	for (uint32_t cid = 0; cid < coroutine_count; ++cid)
	{
		count += works[cid].count;
		found += works[cid].found;
		sum += works[cid].sum;
	}

	printk ("[STL %s] found=%lu sum=%lu%s\n", bench->container, (unsigned long) found, (unsigned long) sum,
			found == count ? "" : " MISMATCH");
	hnsw::print_elapsed (start_cycle, end_cycle);
}

extern "C" void
bench_sequential (const Bench *bench)
{
	uint64_t start_cycle, end_cycle;

	const uint64_t count = prepare (bench);

	printk ("[STL %s] START %s count=%lu\n", bench->container, bench->operation, (unsigned long) count);

	asm volatile ("csrr %0, mcycle" : "=r" (start_cycle) :: "memory");

	for (uint32_t cid = 0; cid < coroutine_count; ++cid)
	{
		bench->run (works[cid]);
	}

	asm volatile ("csrr %0, mcycle" : "=r" (end_cycle) :: "memory");

	report (bench, start_cycle, end_cycle);
}

extern "C" void
bench_sdm (const Bench *bench)
{
	uint64_t start_cycle, end_cycle;
	SDM &sdm = SDM::get ();
	SDM::Core *core = sdm.getCore (0);

	const uint64_t count = prepare (bench);

	// coroutine
	current = bench;

	const auto worker = [] (unsigned int, void *arg) -> int {
		current->run (*static_cast<Work *> (arg));

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

	printk ("[SDM STL %s] START contexts=%u %s count=%lu\n", bench->container, coroutine_count,
			bench->operation, (unsigned long) count);

	asm volatile ("csrr %0, mcycle" : "=r" (start_cycle) :: "memory");

	sdm.run (coroutine_count);

	asm volatile ("csrr %0, mcycle" : "=r" (end_cycle) :: "memory");

	report (bench, start_cycle, end_cycle);
}
