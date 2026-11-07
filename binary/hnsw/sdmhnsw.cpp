#include <sdm.h>
#include <base.h>
#include <printk.h>

using namespace hnsw;

constexpr uint32_t coroutine_count = 2;
constexpr size_t K = 1;
constexpr size_t EF = 100;

alignas(64) static uint64_t frames[SDM::MAX_COROUTINE_COUNT + 1][32];
alignas(16) static uint8_t stacks[coroutine_count][4096];

static GraphView graph;
static Workspace works[coroutine_count] { };
static Result results[query_count] { };
static Status statuses[query_count];

// SDM requires no intervening load between the counter read and write
static size_t next_query;

extern "C" void
sdmhnsw_demo (void)
{
	uint64_t start_cycle, end_cycle;
	SDM &sdm = SDM::get ();
	SDM::Core *core = sdm.getCore (0);

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

	// coroutine
	const auto worker = [] (unsigned int cid, void *) -> int {
		while (true)
		{
			const size_t q = next_query++;
			if (q >= query_count)
			{
				break;
			}

			statuses[q] = search_knn (static_cast<int> (q), graph, queries[q], K, EF, works[cid], &results[q]);
		}

		return 0;
	};

	// SDM
	core->clear ();
	core->setFrame (frames);

	for (uint32_t cid = 0; cid < coroutine_count; ++cid)
	{
		if (!core->addCoroutine (stacks[cid] + sizeof (stacks[cid]), worker, nullptr))
		{
			printk ("failed to add the coroutine to sdm core\n");
			workspace_release (works, coroutine_count);
			return;
		}
	}

	printk ("[SDM HNSW] START contexts=%u queries=%lu\n", coroutine_count, (unsigned long) query_count);

	asm volatile ("csrr %0, mcycle" : "=r" (start_cycle) :: "memory");

	sdm.run (coroutine_count);

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
