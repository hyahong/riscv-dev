#include <hchnsw.h>
#include <base.h>
#include <printk.h>

using namespace hnsw;

constexpr size_t Workers = 8;
constexpr size_t K = 1;
constexpr size_t Ef = 100;
constexpr size_t StackBytes = 16384;

static GraphView graph;
static Workspace works[Workers];
static unsigned active_workers;
static uint64_t seen;
alignas(64) static uint8_t stacks[Workers][StackBytes];

template<unsigned Cid>
inline void
configure_frame ()
{
	hc_cfg (0x10 + 2 * Cid, stacks[Cid] + StackBytes);
	hc_cfg (0x11 + 2 * Cid, 0);
}

void
worker (void *, uint64_t cid)
{
	if (cid >= active_workers)
	{
		printk ("something is wrong: cid >= active_workers.\n");
		return;
	}
	seen |= uint64_t (1) << cid;
	for (;;)
	{
		const int64_t q = hc_next ();
		if (q < 0)
		{
			// ra points to worker_exit
			return;
		}
		if (uint64_t (q) >= query_count)
		{
			printk ("something is wrong: uint64_t (q) >= query_count.\n");
			return;
		}
		(void) hc_search_knn (q, graph, queries[q], K, Ef, works[cid]);
	}
}

[[noreturn]] void
worker_exit ()
{
	hc_exit ();
}

bool
worker_prepare (unsigned active)
{
	uintptr_t gp_value, tp_value;

	// stack guards (to verify)
	for (unsigned c = 0; c < active; ++c)
	{
		for (size_t j = 0; j < StackBytes; ++j)
			stacks[c][j] = 0xa5;
	}
	active_workers = active;
	seen = 0;

	// register the worker information and function
	asm volatile ("mv %0,gp\nmv %1,tp" : "=r" (gp_value), "=r" (tp_value));
	hc_cfg (0, worker);
	hc_cfg (1, worker_exit);
	hc_cfg (2, (1u << active) - 1);
	hc_cfg (3, gp_value);
	hc_cfg (4, tp_value);
	hc_cfg (5, query_count);
	hc_cfg (6, 0);

	// register the stack to worker frame
	configure_frame<0> ();
	configure_frame<1> ();
	configure_frame<2> ();
	configure_frame<3> ();
	configure_frame<4> ();
	configure_frame<5> ();
	configure_frame<6> ();
	configure_frame<7> ();

	return true;
}

static void
hnsw_run (void)
{
	uint64_t start_cycle, end_cycle;

	printk ("[HNSW] START");

	asm volatile ("csrr %0, mcycle" : "=r" (start_cycle) :: "memory");

	hc_run ();

	asm volatile ("csrr %0, mcycle" : "=r" (end_cycle) :: "memory");

	print_elapsed (start_cycle, end_cycle);
}

extern "C" void
hchnsw_demo (unsigned active)
{
	if (!graph_prepare (graph))
	{
		printk ("invalid graph.\n");
		return;
	}

	if (!workspace_prepare (graph, works, active, K, Ef))
	{
		workspace_release (works, Workers);
		printk ("failed to allocate the memory.\n");
		return;
	}

	if (!worker_prepare (active))
	{
		workspace_release (works, Workers);
		printk ("failed to config the workers.\n");
		return;
	}

	hnsw_run ();

	workspace_release (works, Workers);
}
